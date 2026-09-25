#define _POSIX_C_SOURCE 200809L
#include "webnav.h"
#include <assert.h>
#include <math.h>
#include <time.h>

static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
int main(void) {
    DictItem items[] = {
        {.key = "total_agents", .value = 128},
        {.key = "num_buffers", .value = 2},
    };
    Dict vk = {.items = items, .size = 2, .cap = 2};
    int count, starts[2], counts[2];
    Env* batch = my_vec_init(&count, starts, counts, &vk, NULL);
    assert(count == 4 && starts[0] == 0 && starts[1] == 2);
    assert(counts[0] == 2 && counts[1] == 2);
    for (int e = 0; e < count; e++) {
        assert(batch[e].num_agents == WEBNAV_BATCH && batch[e].rng == (unsigned)e);
        for (int a = 0; a < WEBNAV_BATCH; a++) {
            assert(batch[e].agents[a].policy == 0);
        }
    }
    free(batch);
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
