// Exercise the real viewer's checkpoint layout and independent model task heads.
#define main arpg_viewer_main
#include "../arpg.c"
#undef main
#include <assert.h>
#include <unistd.h>

static void test_camera(void) {
    ARClient c={0};
    assert(!ar_world_pointer(&c,(Vector2){1300,297},1440,900)); // Lighting button, not a move order.
    assert(!ar_world_pointer(&c,(Vector2){140,347},1440,900)); // Selected companion inspector.
    c.selected_pet=-1;assert(ar_world_pointer(&c,(Vector2){140,347},1440,900));
    c.map_open=1;assert(!ar_world_pointer(&c,(Vector2){700,400},1440,900));c.map_open=0;
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
    ar_select_pet(&c,&e,1,0);ar_select_pet(&c,&e,1,0);assert(c.selected_mask==2);
    ar_select_pet(&c,&e,7,0);assert(c.selected_mask==2); // Empty slot is not a selection.
    Vector2 badges[AR_MAX_PETS];ar_pet_badges(&c,&e,badges);
    for(int p=0;p<AR_MAX_PETS;p++)if(e.pets.active[p])assert(ar_pick_pet(&e,&c,badges[p])==p);
    c.selected_mask=0;ar_context_order(&e,&c,ar_iso(&c,3.3f,2.1f,0),0);
    assert(c.move_target && e.pets.command[1]==AR_CMD_GATHER && e.pets.command[0]==AR_CMD_ATTACK);
    assert(ar_read_move_action(3)==0 && ar_read_move_action(12)==0 && ar_read_move_action(15)==0);
    assert(ar_read_move_action(7)==3 && ar_read_move_action(13)==1);
    // Continuous click steering must arrive at a non-octant destination without
    // oscillating between two quantized directions.
    memset(e.dungeon,AR_TILE_GRASS,sizeof(e.dungeon));
    for(int p=0;p<AR_MAX_PETS;p++)if(e.pets.active[p])ar_free_pet(&e,0,p);
    for(int i=0;i<300;i++) {
        actions[0]=(float)ar_move_toward(&e,3.3f,2.1f);if(!actions[0])break;c_step(&e);
    }
    assert(ar_geometry_dist2(e.px,e.py,3.3f,2.1f)<.04f && !e.guide_keeper);
    puf_close(&e);
}

static void test_sheets(void) {
    const char* paths[]={"ocean/arpg/assets/reach-companions-v4.png","ocean/arpg/assets/reach-keepers-v4.png","ocean/arpg/assets/reach-biomes-v4.png","ocean/arpg/assets/reach-fauna-v5.png"};
    for(int sheet=0;sheet<4;sheet++) {
        Image image=LoadImage(paths[sheet]);assert(image.data);
        ARSpriteSheet s={0};ar_sheet_measure(&s,image,sheet==2 ? 4 : 8,4,sheet!=2);
        Color* pixels=LoadImageColors(image);assert(pixels);
        uint64_t hashes[32]={0};
        for(int i=0;i<s.columns*s.rows;i++) {
            Rectangle r=s.frame[i];int count=0;
            assert(r.width>10 && r.height>10 && r.x>=0 && r.y>=0 && r.x+r.width<=image.width && r.y+r.height<=image.height);
            uint64_t hash=UINT64_C(14695981039346656037);
            for(int y=(int)r.y;y<(int)(r.y+r.height);y++)for(int x=(int)r.x;x<(int)(r.x+r.width);x++) {
                Color pixel=pixels[y*image.width+x];count+=pixel.a>40;
                hash=ar_world_hash(hash,&pixel,sizeof(pixel));
            }
            assert(count>200);hashes[i]=hash;
            if(sheet!=2)assert(s.body_height[i/8]>60 && s.pivot[i].y==s.pivot[(i/8)*8].y);
        }
        if(sheet!=2)for(int row=0;row<4;row++)for(int a=0;a<8;a++)for(int b=a+1;b<8;b++)assert(hashes[row*8+a]!=hashes[row*8+b]);
        UnloadImageColors(pixels);UnloadImage(image);
    }
    assert(ar_animation_frame(0,0,-1)==0);
    for(int i=0;i<4;i++)assert(ar_animation_frame((i+.1f)/3.2f,1,-1)==i+1);
    for(int i=0;i<3;i++)assert(ar_animation_frame(0,0,(i+.1f)*.1f)==i+5);
}

static void capture(const char* path) {
    Image screen=LoadImageFromScreen();assert(ExportImage(screen,path));UnloadImage(screen);
}

static void capture_reach(ARPG* e,const char* dir) {
    float obs[AR_OBS_SIZE],actions[NUM_ATNS]={0},reward=0,terminal=0;
    e->rng=42;e->agents[0].observations=obs;e->agents[0].actions=actions;
    e->agents[0].rewards=&reward;e->agents[0].terminals=&terminal;
    e->cfg.max_steps=2147483647;c_reset(e);ar_world_begin(e);c_render(e);
    ARClient* c=ar_client(e);ARWorld* w=e->campaign;
    // Visual fixture only: spawn the six classes using their real implementation.
    for(int cls=AR_PET_FANG;cls<AR_PET_CLASS_COUNT;cls++)if(cls!=AR_PET_MULE)assert(ar_summon_pet(e,0,cls)>=0);
    c->selected_mask=1;ar_drive_selected(c,e,0);c->notice_time=0;
    char path[4096];
    for(int i=0;i<240;i++){c_step(e);if(i%30==0)c_render(e);}
    c->notice_time=0;c_render(e);
    SetTargetFPS(0);double started=GetTime(),world_ms=0,ui_ms=0,present_ms=0;
    for(int frame=0;frame<90;frame++){c_render(e);world_ms+=c->render_world_ms;ui_ms+=c->render_ui_ms;present_ms+=c->render_present_ms;}
    printf("Reach warm-frame render: %.2f ms (90 frames, includes presentation)\n",(GetTime()-started)*1000/90);
    printf("Reach render stages: world %.2f / UI %.2f / presentation %.2f ms\n",world_ms/90,ui_ms/90,present_ms/90);
    SetTargetFPS(60);
    snprintf(path,sizeof(path),"%s/reach-control.png",dir);capture(path);
    const char* tags[]={"meadow","pine","autumn","marsh","dunes","highland"};
    for(int biome=0;biome<AR_BIOME_COUNT;biome++) {
        int best_x=0,best_y=0,found=0;float best=1e30f;
        for(int y=-600;y<=600;y+=8)for(int x=-600;x<=600;x+=8) {
            int tile=ar_terrain_tile(w->seed,x,y);
            if(ar_biome(w->seed,x,y)!=biome || tile==AR_TILE_DEEP || tile==AR_TILE_ROCK)continue;
            // Prefer a region interior instead of a one-tile climate sliver.
            if(ar_biome(w->seed,x-15,y)!=biome || ar_biome(w->seed,x+15,y)!=biome ||
                    ar_biome(w->seed,x,y-15)!=biome || ar_biome(w->seed,x,y+15)!=biome)continue;
            float score=x*x+y*y;if(score<best){best=score;best_x=x;best_y=y;found=1;}
        }
        assert(found);ar_world_drive(e,-1);
        e->pets.x[0]=(best_x+.5f-w->origin_x)*ar_world_cell(e);
        e->pets.y[0]=(best_y+.5f-w->origin_y)*ar_world_cell(e);
        e->pets.dormant[0]=0;
        // Position fixture actor, then use the real possession/streaming path.
        if(!B3_IS_NULL(e->pet_body[0]))ar_phys_teleport(e->pet_body[0],e->pets.x[0],e->pets.y[0]);
        c->selected_mask=1;ar_drive_selected(c,e,0);c->notice_time=0;
        for(int i=0;i<5;i++)c_render(e);
        snprintf(path,sizeof(path),"%s/reach-%s.png",dir,tags[biome]);capture(path);
        printf("Reach visual seed %u / %s at %d,%d\n",w->seed,tags[biome],best_x,best_y);
    }
    c->map_open=1;c->map_x=c->map_y=0;c->map_span=640;
    for(int i=0;i<4;i++)c_render(e);
    snprintf(path,sizeof(path),"%s/reach-map.png",dir);capture(path);
    c->map_open=0;
    // Render all imported poses through the same fixed-pivot path as the game.
    BeginDrawing();ClearBackground(AR_INK);
    ar_text(c,"REACH / RUNTIME ANIMATION FRAMES",28,17,24,AR_GOLD);
    for(int row=0;row<8;row++) {
        const char* label=row==0 ? "Keeper" : row==7 ? "Thornling" : AR_PET_NAMES[row-1];
        ar_text(c,label,28,75+row*100,18,AR_TEXT);
        for(int frame=0;frame<8;frame++) {
            Vector2 feet={220+frame*145.0f,139+row*100.0f};
            DrawLine((int)feet.x-45,(int)feet.y,(int)feet.x+45,(int)feet.y,AR_LINE);
            ar_actor(c,row,frame,feet,69,1,WHITE);
        }
    }
    EndDrawing();snprintf(path,sizeof(path),"%s/reach-frames.png",dir);capture(path);
    puf_close(e);
}

static void capture_lantern(ARPG* e,const char* dir) {
    float obs[AR_OBS_SIZE],actions[NUM_ATNS]={0},reward=0,terminal=0;
    e->rng=42;e->agents[0].observations=obs;e->agents[0].actions=actions;
    e->agents[0].rewards=&reward;e->agents[0].terminals=&terminal;
    e->cfg.max_steps=2147483647;c_reset(e);ar_world_begin(e);c_render(e);
    assert(ar_build_at(e,0,AR_BUILD_HARVESTER,5.5f,0)>=0);
    assert(ar_build_at(e,0,AR_BUILD_TOTEM,5,-3)>=0);
    for(int t=0;t<3600;t++){actions[1]=t==3000 ? AR_PET_FANG+1 : 0;c_step(e);}
    ARClient* c=ar_client(e);c->selected_mask=1;c->notice_time=0;c_render(e);
    memset(c->fx,0,sizeof(c->fx));c->notice_time=0;
    ARPG before=*e;float time=c->nature_time;char path[4096];
    SetTargetFPS(0);
    for(int mode=0;mode<4;mode++) {
        c->light_mode=mode;double start=GetTime();
        for(int n=0;n<45;n++)c_render(e);
        printf("Lantern mode %s: %.2f ms/frame (45 stationary frames)\n",AR_LIGHT_NAMES[mode],(GetTime()-start)*1000/45);
        assert(!memcmp(&before,e,sizeof(before)));assert(c->nature_time==time);
        assert(!mode || (c->lightmap.id && !c->light_failed));
        snprintf(path,sizeof(path),"%s/lantern-%d.png",dir,mode);capture(path);
    }
    uint64_t signature=c->light_signature;
    ar_lighting_prepare(c,e);assert(signature==c->light_signature);
    e->px+=.25f;ar_lighting_prepare(c,e);assert(signature!=c->light_signature);
    e->px=before.px;ar_lighting_prepare(c,e);assert(signature==c->light_signature);
    c->off_x+=10;ar_lighting_prepare(c,e);assert(signature!=c->light_signature);
    c->off_x-=10;ar_lighting_prepare(c,e);assert(signature==c->light_signature);
    uint8_t tile=e->dungeon[0];e->dungeon[0]=tile==AR_TILE_GRASS ? AR_TILE_SAND : AR_TILE_GRASS;
    uint32_t old_revision=c->minimap_revision;c_render(e);
    assert(c->minimap_revision==c->ground_revision && c->minimap_revision!=old_revision);
    e->dungeon[0]=tile;c_render(e);assert(c->minimap_revision==old_revision);
    SetWindowSize(1280,720);
    for(int n=0;n<8;n++)c_render(e);
    assert(c->lightmap.texture.width==(GetScreenWidth()+1)/2);
    assert(c->lightmap.texture.height==(GetScreenHeight()+1)/2);
    snprintf(path,sizeof(path),"%s/lantern-compact.png",dir);capture(path);
    // Simulate an unavailable render target: the world must remain renderable.
    c->light_failed=1;c_render(e);assert(!memcmp(&before,e,sizeof(before)));
    c->light_failed=0;
    BeginDrawing();ClearBackground(AR_INK);
    const char* names[]={"Meadow hare","Woodland deer","Spore toad (light)","Slate boar (heavy)"};
    for(int row=0;row<4;row++) {
        ar_text(c,names[row],20,34+row*155,18,AR_GOLD);
        for(int frame=0;frame<8;frame++)ar_sheet_draw(&c->fauna,row*8+frame,(Vector2){200+frame*140.0f,145+row*155.0f},90,1,WHITE);
    }
    EndDrawing();snprintf(path,sizeof(path),"%s/lantern-fauna.png",dir);capture(path);
    puf_close(e);
}

int main(int argc,char** argv) {
    ARPG e={0};ARControls controls;
    ar_load_config(&e,&controls);
    test_camera();
    test_policy();
    test_orders();
    test_sheets();
    if(argc>2 && !strcmp(argv[1],"--lantern"))capture_lantern(&e,argv[2]);
    else if(argc>2 && !strcmp(argv[1],"--reach"))capture_reach(&e,argv[2]);
    else if(argc>1) {
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
