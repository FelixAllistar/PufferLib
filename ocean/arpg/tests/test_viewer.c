// Exercise the real viewer's checkpoint layout and independent model task heads.
#define main arpg_viewer_main
#include "../arpg.c"
#undef main
#include <assert.h>
#include <unistd.h>

static void test_camera(void) {
    ARClient c={0};
    for(int width=1280;width<=1920;width+=320)for(int height=720;height<=1080;height+=180)
    for(int x=-300;x<=300;x+=60)for(int y=-300;y<=300;y+=60)
    for(int z=0;z<3;z++) {
        c.zoom=z==0 ? 0.55f : z==1 ? 1.0f : 1.65f;c.cam_x=x;c.cam_y=y;
        ar_camera_project(&c,width,height);
        Vector2 screen=ar_iso(&c,x,y,0),back=ar_unproject(&c,screen);
        assert(fabsf(screen.x-width*0.5f)<0.01f);
        assert(fabsf(screen.y-(height-150+76)*0.5f)<0.01f);
        assert(fabsf(back.x-x)<0.001f && fabsf(back.y-y)<0.001f);
    }
}

static void test_policy(void) {
    int sizes[]=ACT_SIZES;
    const int widths[]={16,64,128,129};
    for(int w=0;w<4;w++)for(int layers=1;layers<=2;layers++) {
        int hidden=widths[w],count=ar_expected_floats(hidden,layers);
        Weights weights={.data=calloc((size_t)count,sizeof(float)),.size=count,.idx=0};
        PufferNet* net=make_puffernet(&weights,1,AR_OBS_SIZE,hidden,layers,sizes,NUM_ATNS);
        assert(weights.idx==count);
        float obs[AR_OBS_SIZE]={0},actions[NUM_ATNS]={0};
        int expected[NUM_ATNS]={0,0,0,0,0,AR_TASK_GATHER,AR_TASK_ESCORT,AR_TASK_HOLD,AR_TASK_HOME,AR_TASK_HUNT,AR_TASK_GATHER,AR_TASK_ESCORT,AR_TASK_AUTO};
        int offset=0;
        for(int h=0;h<NUM_ATNS;h++) {
            net->decoder->weights[(offset+expected[h])*hidden]=1;
            offset+=sizes[h];
        }
        ar_policy_step(net,obs,actions,1);
        for(int h=0;h<NUM_ATNS;h++)assert(actions[h]==expected[h]);
        ar_reset_policy(net);
        for(int i=0;i<hidden*layers;i++)assert(net->mingru->state[i]==0);
        free_puffernet(net);free(weights.data);
    }
    // A legacy/malformed checkpoint is rejected before constructing the network.
    char path[]="/tmp/arpg-policy-test.XXXXXX";
    int fd=mkstemp(path);assert(fd>=0);close(fd);
    Weights* weights=NULL;
    assert(ar_load_policy(path,&weights)==NULL && weights==NULL);
    int hidden=puf_ini_get_int(&g_controls_ini,"policy","hidden_size");
    int layers=puf_ini_get_int(&g_controls_ini,"policy","num_layers");
    int count=ar_expected_floats(hidden,layers);
    float* zero=calloc((size_t)count,sizeof(float));
    FILE* file=fopen(path,"wb");assert(file);
    assert(fwrite(zero,sizeof(float),(size_t)count,file)==(size_t)count);
    fclose(file);free(zero);
    PufferNet* net=ar_load_policy(path,&weights);assert(net && weights);
    free_puffernet(net);free(weights);unlink(path);
}

static void test_orders(void) {
    ARPG e={0};e.cfg=ar_config_from_kwargs(puf_ini_section(&g_controls_ini,"env",0));e.rng=42;
    float obs[AR_OBS_SIZE],actions[NUM_ATNS]={0},reward=0,terminal=0;
    e.agents[0].observations=obs;e.agents[0].actions=actions;e.agents[0].rewards=&reward;e.agents[0].terminals=&terminal;
    c_reset(&e);ARClient c={0};c.zoom=1;ar_camera_project(&c,1440,900);
    int porter=ar_summon_pet(&e,0,AR_PET_MULE);assert(porter>=0);
    c.selected_mask=1u<<1;
    ar_context_order(&e,&c,ar_iso(&c,e.shard_x[0],e.shard_y[0],0.4f),0);
    assert(e.pets.command[1]==AR_CMD_GATHER && e.pets.command[0]==AR_CMD_AUTO && e.pets.command[porter]==AR_CMD_AUTO);
    assert(e.pets.goal_x[1]==e.shard_x[0]);
    c.selected_mask=1u<<porter;
    ar_context_order(&e,&c,ar_iso(&c,e.shard_x[1],e.shard_y[1],0.4f),0);
    assert(e.pets.command[porter]==AR_CMD_GATHER && e.pets.goal_x[porter]==e.shard_x[1]);
    assert(e.pets.goal_x[1]==e.shard_x[0]);
    c.selected_mask=1;
    ar_context_order(&e,&c,ar_iso(&c,e.nest_x[0],e.nest_y[0],2.0f),0);
    assert(e.pets.command[0]==AR_CMD_ATTACK && e.pets.goal_x[0]==e.nest_x[0]);
    assert(e.pets.command[1]==AR_CMD_GATHER && e.pets.command[porter]==AR_CMD_GATHER);
    ar_select_pet(&c,&e,1,1);assert(c.selected_mask==3);
    ar_select_pet(&c,&e,0,1);assert(c.selected_mask==2 && c.selected_pet==1);
    puf_close(&e);
}

static void capture(const char* path) {
    Image screen=LoadImageFromScreen();assert(ExportImage(screen,path));UnloadImage(screen);
}

int main(int argc,char** argv) {
    ARPG e={0};ARControls controls;
    ar_load_config(&e,&controls);
    test_camera();
    test_policy();
    test_orders();
    if(argc>1) {
        // Optional screenshot uses real production and construction; no mock HUD.
        float obs[AR_OBS_SIZE],actions[NUM_ATNS]={0},reward=0,terminal=0;
        e.rng=42;e.agents[0].observations=obs;e.agents[0].actions=actions;
        e.agents[0].rewards=&reward;e.agents[0].terminals=&terminal;
        e.cfg.max_steps=2147483647;
        c_reset(&e);ar_world_begin(&e);c_step(&e);c_render(&e);
        if(argc>3) {
            SetMousePosition(GetScreenWidth()-65,GetScreenHeight()-150+33+56+12);
            for(int frame=0;frame<3;frame++)c_render(&e);
            assert(ar_client(&e)->hover_type==1 && ar_client(&e)->hover_id==AR_PET_EMBER);
            capture(argv[3]);SetMousePosition(720,410);
        }
        assert(ar_build_at(&e,0,AR_BUILD_HARVESTER,5.5f,0)>=0);
        assert(ar_build_at(&e,0,AR_BUILD_TOTEM,5,-3)>=0);
        for(int t=0;t<3600;t++) {
            actions[1]=argc<=2 && t==3000 ? AR_PET_FANG+1 : 0;
            c_step(&e);
        }
        if(argc>2) {
            while(e.harvested<120 && e.tick<60000){c_step(&e);if(e.tick%1800==0)c_render(&e);}
            assert(e.harvested>=120);
            const int classes[]={AR_PET_FANG,AR_PET_AEGIS,AR_PET_BURROWER,AR_PET_EMBER};
            for(int i=0;i<4;i++)for(int t=0;t<200;t++) {actions[1]=t==0 ? classes[i]+1 : 0;c_step(&e);}
            assert(e.pets_alive==6);
            while((e.cores<10 || e.shards<75) && e.tick<90000){c_step(&e);if(e.tick%1800==0)c_render(&e);}
            assert(e.cores>=10 && e.shards>=75);
            assert(ar_build_at(&e,0,AR_BUILD_ARTILLERY,-5.5f,-4)>=0);
        }
        ARClient* client=ar_client(&e);client->notice_time=0;
        for(int frame=0;frame<3;frame++)c_render(&e);
        // Fast-forwarded production is genuine, but should not stack a whole
        // minute's feedback over the final still image.
        memset(client->fx,0,sizeof(client->fx));client->notice_time=0;c_render(&e);
        capture(argv[1]);
        if(argc>4) {
            int n=ar_nearest_nest(&e,0,e.home_x,e.home_y,1e9f);assert(n>=0);
            client->camera_free=1;client->cam_x=e.nest_x[n];client->cam_y=e.nest_y[n];
            assert(ar_fire_artillery(&e,0,e.nest_x[n],e.nest_y[n]));
            for(int t=0;t<38;t++)c_step(&e);
            for(int frame=0;frame<3;frame++)c_render(&e);
            capture(argv[4]);client->camera_free=0;client->cam_x=e.px;client->cam_y=e.py;
        }
        if(argc>5) {
            SetWindowSize(1280,720);
            // Let the window manager finish resize/reposition events before
            // warping the pointer; those events can otherwise undo the warp.
            for(int frame=0;frame<8;frame++)c_render(&e);
            SetMousePosition(GetScreenWidth()-65,GetScreenHeight()-150+33+56+12);
            for(int frame=0;frame<4;frame++)c_render(&e);
            if(ar_client(&e)->hover_type!=1 || ar_client(&e)->hover_id!=AR_PET_EMBER)
                fprintf(stderr,"Compact hover: window %dx%d, mouse %.0f %.0f, hit %d/%d\n",
                    GetScreenWidth(),GetScreenHeight(),GetMousePosition().x,GetMousePosition().y,
                    ar_client(&e)->hover_type,ar_client(&e)->hover_id);
            assert(ar_client(&e)->hover_type==1 && ar_client(&e)->hover_id==AR_PET_EMBER);
            capture(argv[5]);
        }
        puf_close(&e);
    }
    puf_ini_free(&g_controls_ini);
    puts("ARPG viewer camera projection, contextual selection/orders, checkpoint validation, recurrent reset, independent pet policy heads: PASS");
}
