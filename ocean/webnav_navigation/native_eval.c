#include "../../src/puffercpu.c"
#include "webnav_navigation.h"

int main(int argc,char **argv) {
    if(argc<4||argc>5){fprintf(stderr,"usage: %s CHECKPOINT|random panels|menus EPISODES [TASK_NAME]\n",argv[0]);return 2;}
    int family=!strcmp(argv[2],"panels")?0:!strcmp(argv[2],"menus")?1:-1;
    int episodes=atoi(argv[3]);if(family<0||episodes<1||episodes>100000)return 2;
    Weights *weights=NULL;PufferNet *net=NULL;
    if(strcmp(argv[1],"random")) {
        weights=load_weights(argv[1]);
        if(!weights||weights->size-7!=64*OBS_SIZE+64*(WFN_ACTIONS+1)+3*64*64){fputs("expected H64/L1 navigation weights\n",stderr);return 2;}
        int sizes[]={WFN_ACTIONS};net=make_puffernet(weights,WFN_LANES,OBS_SIZE,64,1,sizes,1);
    }
    unsigned evaluated=0;
    for(unsigned task=0;task<(family?2u:9u);task++) {
        Dict config={0};dict_set(&config,"family",family);dict_set(&config,"task_mask",1u<<task);
        Env env={0};env.rng=4000000+task*10000;puf_init(&env,&config);
        const char *name=env.family.api->task_names[task];
        if(argc==5&&strcmp(argv[4],name)){puf_close(&env);free(config.items);continue;}
        float obs[WFN_LANES][OBS_SIZE],actions[WFN_LANES]={0},rewards[WFN_LANES]={0},terminals[WFN_LANES]={0};
        unsigned char masks[WFN_LANES][WFN_ACTIONS];
        for(unsigned i=0;i<WFN_LANES;i++)env.agents[i]=(Agent){obs[i],actions+i,rewards+i,terminals+i,masks[i],0};
        if(net)memset(net->mingru->state,0,WFN_LANES*64*sizeof(float));
        puf_reset(&env);unsigned games=0,wins=0,steps=0;uint32_t rng=91037;
        while(games<(unsigned)episodes) {
            if(net) {
                mingru_zero_term(net->mingru,terminals);
                linear(net->encoder,&obs[0][0]);mingru(net->mingru,net->encoder->output);
                linear(net->decoder,net->mingru->output);
            }
            for(unsigned lane=0;lane<WFN_LANES;lane++) {
                if(net) {
                    int best=0;float score=-INFINITY;
                    for(unsigned a=0;a<WFN_ACTIONS;a++)if(masks[lane][a]&&net->decoder->output[lane*(WFN_ACTIONS+1)+a]>score){best=a;score=net->decoder->output[lane*(WFN_ACTIONS+1)+a];}
                    actions[lane]=(float)best;
                } else {
                    unsigned legal[WFN_ACTIONS],count=0;
                    for(unsigned a=0;a<WFN_ACTIONS;a++)if(masks[lane][a])legal[count++]=a;
                    rng=rng*1664525u+1013904223u;actions[lane]=(float)legal[(rng>>8)%count];
                }
            }
            puf_step(&env);steps+=WFN_LANES;
            for(unsigned lane=0;lane<WFN_LANES&&games<(unsigned)episodes;lane++)if(terminals[lane]){games++;wins+=rewards[lane]>=0.999f;}
        }
        printf("{\"task\":\"%s\",\"episodes\":%u,\"successes\":%u,\"full_credit_rate\":%.6f,\"vector_steps\":%u,\"policy\":\"%s\"}\n",name,games,wins,(double)wins/games,steps,net?"greedy":"random");fflush(stdout);
        evaluated+=games;puf_close(&env);free(config.items);
    }
    if(net)free_puffernet(net);free(weights);return evaluated?0:2;
}
