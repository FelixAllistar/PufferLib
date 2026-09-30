#include <sys/prctl.h>
#include "../../src/puffercpu.c"
#include "webnav_numeric.h"

static size_t align8(size_t n){return (n+7u)&~7u;}
static size_t expected_weights(void){
    size_t n=align8(64u*OBS_SIZE);
    n=align8(n+64u*(WNN_ACTIONS+1u));
    return align8(n+3u*64u*64u);
}
int main(int argc,char **argv) {
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    if(argc<3||argc>4){
        fprintf(stderr,"usage: %s CHECKPOINT|random EPISODES [TASK_NAME]\n",argv[0]);return 2;
    }
    char *end=NULL;unsigned long count=strtoul(argv[2],&end,10);
    if(!end||*end||count<1||count>100000)return 2;
    int random_policy=!strcmp(argv[1],"random");
    Weights *weights=NULL;PufferNet *net=NULL;
    if(!random_policy){
        weights=load_weights(argv[1]);
        if(!weights||weights->size<7||(size_t)(weights->size-7)!=expected_weights()){
            fprintf(stderr,"expected H64/L1 numeric checkpoint with %zu floats\n",expected_weights());
            free(weights);return 2;
        }
        int sizes[]={WNN_ACTIONS};
        net=make_puffernet(weights,WNN_LANES,OBS_SIZE,64,1,sizes,1);
        if(!net||(size_t)weights->idx!=expected_weights()){
            fputs("checkpoint layout mismatch\n",stderr);return 2;
        }
    }
    unsigned evaluated=0;
    for(unsigned task=0;task<9;task++){
        Dict config={0};dict_set(&config,"task_mask",1u<<task);
        Env env={0};env.rng=4000000u+task*10000u;puf_init(&env,&config);
        const char *name=env.family.api->task_names[task];
        if(argc==4&&strcmp(argv[3],name)){puf_close(&env);free(config.items);continue;}
        float obs[WNN_LANES][OBS_SIZE],actions[WNN_LANES]={0};
        float rewards[WNN_LANES]={0},terminals[WNN_LANES]={0};
        unsigned char masks[WNN_LANES][WNN_ACTIONS];
        for(unsigned i=0;i<WNN_LANES;i++)env.agents[i]=(Agent){obs[i],actions+i,rewards+i,terminals+i,masks[i],0};
        if(net)memset(net->mingru->state,0,WNN_LANES*64*sizeof(float));
        puf_reset(&env);unsigned games=0,wins=0,steps=0;uint32_t rng=91037;
        while(games<(unsigned)count){
            if(net){
                mingru_zero_term(net->mingru,terminals);
                linear(net->encoder,&obs[0][0]);mingru(net->mingru,net->encoder->output);
                linear(net->decoder,net->mingru->output);
            }
            for(unsigned lane=0;lane<WNN_LANES;lane++){
                if(net){
                    int best=0;float score=-INFINITY;
                    for(unsigned a=0;a<WNN_ACTIONS;a++)if(masks[lane][a]&&
                        net->decoder->output[lane*(WNN_ACTIONS+1)+a]>score){
                        best=(int)a;score=net->decoder->output[lane*(WNN_ACTIONS+1)+a];
                    }
                    actions[lane]=(float)best;
                }else{
                    unsigned legal[WNN_ACTIONS],n=0;
                    for(unsigned a=0;a<WNN_ACTIONS;a++)if(masks[lane][a])legal[n++]=a;
                    rng=rng*1664525u+1013904223u;actions[lane]=(float)legal[(rng>>8)%n];
                }
            }
            puf_step(&env);steps+=WNN_LANES;
            for(unsigned lane=0;lane<WNN_LANES&&games<(unsigned)count;lane++)if(terminals[lane]){
                games++;wins+=rewards[lane]>=0.999f;
            }
        }
        printf("{\"task\":\"%s\",\"episodes\":%u,\"successes\":%u,\"full_credit_rate\":%.6f,\"vector_steps\":%u,\"policy\":\"%s\"}\n",
            name,games,wins,(double)wins/games,steps,random_policy?"random":"greedy");
        fflush(stdout);evaluated+=games;puf_close(&env);free(config.items);
    }
    if(net)free_puffernet(net);free(weights);return evaluated?0:2;
}
