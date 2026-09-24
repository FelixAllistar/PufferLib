#define _POSIX_C_SOURCE 200809L
#include "webnav.h"
#include <assert.h>
#include <math.h>
#include <time.h>

static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
int main(void) {
    Env env={0};env.rng=12345;
    float obs[32][OBS_SIZE],actions[32],rewards[32],terminals[32];
    unsigned char masks[32][13];
    puf_init(&env,NULL);
    for(int i=0;i<32;i++)env.agents[i]=(Agent){.observations=obs[i],.actions=actions+i,.rewards=rewards+i,.terminals=terminals+i,.action_mask=masks[i]};
    puf_reset(&env);
    long steps=0,episodes=0;double start=now();float checksum=0;
    while(now()-start<3) {
        for(int i=0;i<32;i++)actions[i]=(steps/32+i)%13;
        puf_step(&env);steps+=32;
        for(int i=0;i<32;i++) {
            episodes+=terminals[i]>0;
            assert(env.words[i*256+9]==0);
            assert(terminals[i]==0||env.words[i*256+8]==0);
            assert(terminals[i] ? fabsf(rewards[i])==1 : rewards[i]==0);
            assert(masks[i][0]);
            checksum+=obs[i][0]+rewards[i];
        }
    }
    printf("{\"puffer_env\":\"PASS\",\"batch\":32,\"steps\":%ld,\"episodes\":%ld,\"seconds\":%.6f,\"steps_per_second\":%.0f,\"checksum\":%g}\n",steps,episodes,now()-start,steps/(now()-start),checksum);
}
