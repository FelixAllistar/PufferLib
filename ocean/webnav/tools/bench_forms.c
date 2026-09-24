#define _POSIX_C_SOURCE 200809L
#include "bridge.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+1e-9*t.tv_nsec;}
static void reset(uint32_t *w,unsigned seed){for(unsigned i=0;i<32;i++){uint32_t *r=w+256*i;r[0]=i%2?3:1;r[7]=3;r[13]=seed+i;}webnav_batch(w);}
int main(void){
    uint32_t w[8192]={0};reset(w,0); /* Runtime warmup excluded. */
    unsigned batches=0;double start=now();do{reset(w,batches*32);batches++;}while(now()-start<3.0);double generation=now()-start;
    reset(w,17);start=now();
    /* Repeated active control clicks, no terminal submissions: isolate widget
       transition/transport cost. This is not an episode or RL benchmark. */
    unsigned step=0;do{
        for(unsigned i=0;i<32;i++){uint32_t *r=w+256*i;r[7]=1;r[8]=step%(r[1]-1);r[9]=0;}
        webnav_batch(w);step++;
    }while(now()-start<3.0);
    double transition=now()-start;
    printf("{\"scope\":\"warm single-thread CPU Bend plus C transport; no browser, encoder, observation packing or policy; active click-only transitions\",\"generated_forms\":%u,\"generation_seconds\":%.6f,\"forms_per_second\":%.3f,\"transitions\":%u,\"transition_seconds\":%.6f,\"transitions_per_second\":%.3f}\n",batches*32,generation,batches*32/generation,step*32,transition,step*32/transition);
}
