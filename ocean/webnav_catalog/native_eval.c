#include <sys/prctl.h>
#include "../../src/puffercpu.c"
#include "webnav_catalog.h"

static size_t align8(size_t n){return (n+7u)&~7u;}
static size_t expected_weights(void){
    size_t n=align8(64u*OBS_SIZE);
    n=align8(n+64u*(WFC_ACTIONS+1u));
    return align8(n+3u*64u*64u);
}
int main(int argc,char **argv){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    if(argc<3||argc>4){
        fprintf(stderr,"usage: %s CHECKPOINT|random EPISODES_PER_TASK [TASK_NAME]\n",argv[0]);return 2;
    }
    char *end=NULL;unsigned long parsed=strtoul(argv[2],&end,10);
    if(!end||*end||parsed<1||parsed>100000)return 2;
    unsigned episodes=(unsigned)parsed,random_policy=!strcmp(argv[1],"random");
    Weights *weights=NULL;PufferNet *net=NULL;
    if(!random_policy){
        weights=load_weights(argv[1]);
        if(!weights||weights->size<7||(size_t)(weights->size-7)!=expected_weights()){
            fprintf(stderr,"expected H64/L1 catalog checkpoint with %zu floats\n",expected_weights());
            free(weights);return 2;
        }
        int sizes[]={WFC_ACTIONS};
        net=make_puffernet(weights,WFC_LANES,OBS_SIZE,64,1,sizes,1);
        if(!net||(size_t)weights->idx!=expected_weights())return 2;
    }
    unsigned evaluated=0;
    for(unsigned task=0;task<3;task++){
        Dict config={0};dict_set(&config,"task_mask",1u<<task);
        Env env={0};env.rng=4000000u+task*10000u;puf_init(&env,&config);
        const char *name=env.family.api->task_names[task];
        if(argc==4&&strcmp(argv[3],name)){puf_close(&env);free(config.items);continue;}
        float obs[WFC_LANES][OBS_SIZE],actions[WFC_LANES]={0};
        float rewards[WFC_LANES]={0},terminals[WFC_LANES]={0};
        unsigned char masks[WFC_LANES][WFC_ACTIONS];
        for(unsigned lane=0;lane<WFC_LANES;lane++)
            env.agents[lane]=(Agent){obs[lane],actions+lane,rewards+lane,terminals+lane,masks[lane],0};
        if(net)memset(net->mingru->state,0,WFC_LANES*64*sizeof(float));
        puf_reset(&env);
        unsigned games=0,wins=0,partials=0,steps=0;double score=0;
        uint32_t rng=91037u+task;
        while(games<episodes){
            if(net){
                mingru_zero_term(net->mingru,terminals);
                linear(net->encoder,&obs[0][0]);
                mingru(net->mingru,net->encoder->output);
                linear(net->decoder,net->mingru->output);
            }
            for(unsigned lane=0;lane<WFC_LANES;lane++){
                if(net){
                    int best=0;float best_score=-INFINITY;
                    for(unsigned a=0;a<WFC_ACTIONS;a++)if(masks[lane][a]&&
                       net->decoder->output[lane*(WFC_ACTIONS+1u)+a]>best_score){
                        best=(int)a;best_score=net->decoder->output[lane*(WFC_ACTIONS+1u)+a];
                    }
                    actions[lane]=(float)best;
                }else{
                    unsigned legal[WFC_ACTIONS],count=0;
                    for(unsigned a=0;a<WFC_ACTIONS;a++)if(masks[lane][a])legal[count++]=a;
                    rng=rng*1664525u+1013904223u;
                    actions[lane]=(float)legal[(rng>>8)%count];
                }
            }
            puf_step(&env);steps+=WFC_LANES;
            for(unsigned lane=0;lane<WFC_LANES&&games<episodes;lane++)if(terminals[lane]){
                games++;score+=rewards[lane];wins+=rewards[lane]>=0.999f;
                partials+=rewards[lane]>0&&rewards[lane]<0.999f;
            }
        }
        printf("{\"task\":\"%s\",\"episodes\":%u,\"successes\":%u,\"partial_positive\":%u,\"full_credit_rate\":%.6f,\"mean_raw_score\":%.6f,\"vector_steps\":%u,\"policy\":\"%s\"}\n",
               name,games,wins,partials,(double)wins/games,score/games,steps,
               random_policy?"random":"greedy");fflush(stdout);
        evaluated+=games;puf_close(&env);free(config.items);
    }
    if(net)free_puffernet(net);free(weights);
    return evaluated?0:2;
}
