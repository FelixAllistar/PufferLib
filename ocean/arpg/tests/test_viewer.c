// Exercise the real viewer's checkpoint layout and independent model task heads.
#define main arpg_viewer_main
#include "../arpg.c"
#undef main
#include <assert.h>
#include <unistd.h>

static void test_policy(void) {
    int sizes[]=ACT_SIZES;
    const int widths[]={16,64,128,129};
    for(int w=0;w<4;w++)for(int layers=1;layers<=2;layers++) {
        int hidden=widths[w],count=ar_expected_floats(hidden,layers);
        Weights weights={.data=calloc((size_t)count,sizeof(float)),.size=count,.idx=0};
        PufferNet* net=make_puffernet(&weights,1,AR_OBS_SIZE,hidden,layers,sizes,NUM_ATNS);
        assert(weights.idx==count);
        float obs[AR_OBS_SIZE]={0},actions[NUM_ATNS]={0};
        int expected[]={0,0,0,0,0,AR_TASK_GATHER,AR_TASK_ESCORT,AR_TASK_HOLD,AR_TASK_HOME};
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

int main(int argc,char** argv) {
    ARPG e={0};ARControls controls;
    ar_load_config(&e,&controls);
    test_policy();
    if(argc>1) {
        // Optional screenshot uses real production and construction; no mock HUD.
        float obs[AR_OBS_SIZE],actions[NUM_ATNS]={0},reward=0,terminal=0;
        e.rng=42;e.agents[0].observations=obs;e.agents[0].actions=actions;
        e.agents[0].rewards=&reward;e.agents[0].terminals=&terminal;
        c_reset(&e);c_step(&e);c_render(&e);
        assert(ar_build_at(&e,0,AR_BUILD_HARVESTER,5.5f,0)>=0);
        assert(ar_build_at(&e,0,AR_BUILD_TOTEM,5,-3)>=0);
        for(int t=0;t<3600;t++) {
            actions[1]=t==3000 ? AR_PET_FANG+1 : 0;
            c_step(&e);
        }
        ARClient* client=ar_client(&e);client->notice_time=0;
        for(int frame=0;frame<3;frame++)c_render(&e);
        Image screen=LoadImageFromScreen();assert(ExportImage(screen,argv[1]));UnloadImage(screen);
        puf_close(&e);
    }
    puf_ini_free(&g_controls_ini);
    puts("ARPG viewer checkpoint validation, recurrent reset, independent pet policy heads: PASS");
}
