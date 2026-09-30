#include "webnav_family.h"
#include <stdio.h>

int main(void) {
    Dict ek={0};
    dict_set(&ek,"task_mask",1023);
    dict_set(&ek,"seed_offset",1000000);
    dict_set(&ek,"data_mode",1);
    dict_set(&ek,"semantic",0);
    enum {ENVS=64, TARGET_EPISODES=10000};
    Env *envs=(Env*)calloc(ENVS,sizeof *envs);
    float *obs=(float*)calloc((size_t)ENVS*WFC_LANES*OBS_SIZE,sizeof *obs);
    unsigned char *masks=(unsigned char*)calloc((size_t)ENVS*WFC_LANES*WFC_ACTIONS,1);
    float *actions=(float*)calloc(ENVS*WFC_LANES,sizeof *actions);
    float *rewards=(float*)calloc(ENVS*WFC_LANES,sizeof *rewards);
    float *terminals=(float*)calloc(ENVS*WFC_LANES,sizeof *terminals);
    if(!envs||!obs||!masks||!actions||!rewards||!terminals)abort();
    unsigned games[10]={0},wins[10]={0},total=0;
    uint32_t rng=3017;
    for(unsigned e=0;e<ENVS;e++) {
        Env *env=&envs[e];env->rng=1000000u+e*2654435761u;
        puf_init(env,&ek);
        for(unsigned lane=0;lane<WFC_LANES;lane++) {
            unsigned i=e*WFC_LANES+lane;
            env->agents[lane].observations=obs+(size_t)i*OBS_SIZE;
            env->agents[lane].action_mask=masks+(size_t)i*WFC_ACTIONS;
            env->agents[lane].actions=actions+i;
            env->agents[lane].rewards=rewards+i;
            env->agents[lane].terminals=terminals+i;
        }
        puf_reset(env);
    }
    while(total<TARGET_EPISODES)for(unsigned e=0;e<ENVS;e++) {
        Env *env=&envs[e];unsigned tasks[WFC_LANES];
        for(unsigned lane=0;lane<WFC_LANES;lane++) {
            unsigned i=e*WFC_LANES+lane,legal[WFC_NODES],count=0;
            tasks[lane]=env->family.words[(size_t)lane*env->family.api->row_words+WF_TASK];
            for(unsigned a=1;a<WFC_ACTIONS;a++)if(masks[(size_t)i*WFC_ACTIONS+a])legal[count++]=a;
            rng=rng*1664525u+1013904223u;
            actions[i]=count?(float)legal[rng%count]:0;
        }
        puf_step(env);
        for(unsigned lane=0;lane<WFC_LANES;lane++) {
            unsigned i=e*WFC_LANES+lane;
            if(terminals[i]){games[tasks[lane]]++;wins[tasks[lane]]+=rewards[i]>=0.999f;total++;}
        }
    }
    for(unsigned i=0;i<10;i++)printf("{\"task\":\"%s\",\"episodes\":%u,\"successes\":%u,\"full_credit_rate\":%.6f}\n",
        envs[0].family.api->task_names[i],games[i],wins[i],games[i]?(double)wins[i]/games[i]:0);
    for(unsigned e=0;e<ENVS;e++)puf_close(&envs[e]);
    free(envs);free(obs);free(masks);free(actions);free(rewards);free(terminals);
    return 0;
}
