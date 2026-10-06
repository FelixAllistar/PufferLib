// Native benchmark of the actual movement adapter, including observations and
// episode resets. --trace writes exact observations/rewards and game snapshots
// for comparing simulator changes. It is not a trainer or an alternate env.
#include "locomotion.h"
#include "sim.h"
#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _OPENMP
#include <omp.h>
#endif

static double now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return t.tv_sec+t.tv_nsec*1e-9;
}
static double cpu_now(void) {
    struct timespec t; clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&t);
    return t.tv_sec+t.tv_nsec*1e-9;
}
static uint32_t hash_bytes(uint32_t hash,const void* bytes,size_t count) {
    const unsigned char* p=bytes;
    for(size_t i=0;i<count;i++) { hash^=p[i]; hash*=16777619u; }
    return hash;
}
int main(int argc,char** argv) {
    int steps=3000,envs=32,threads=4; const char* trace_path=NULL;
    for(int i=1;i<argc;i++) {
        if(i+1<argc && !strcmp(argv[i],"--steps")) steps=atoi(argv[++i]);
        else if(i+1<argc && !strcmp(argv[i],"--envs")) envs=atoi(argv[++i]);
        else if(i+1<argc && !strcmp(argv[i],"--threads")) threads=atoi(argv[++i]);
        else if(i+1<argc && !strcmp(argv[i],"--trace")) trace_path=argv[++i];
        else { fprintf(stderr,"usage: %s [--steps N] [--envs N] [--threads N] [--trace FILE]\n",argv[0]); return 2; }
    }
    if(steps<1 || envs<1 || envs>1024 || threads<1 || threads>128 || (trace_path && (envs!=1 || threads!=1))) return 2;
    FILE* trace=trace_path ? fopen(trace_path,"wb") : NULL;
    if(trace_path && !trace) { perror(trace_path); return 2; }
    void** courses=calloc(envs,sizeof(*courses));
    if(!courses) return 2;
    for(int stage=0;stage<5;stage++) {
        for(int i=0;i<envs;i++) courses[i]=swat_training_create(2718+i,i%2,stage);
        int resets=0,successes=0; uint32_t checksum=0; double start=now(),cpu_start=cpu_now();
#pragma omp parallel for num_threads(threads) reduction(+:resets,successes) reduction(^:checksum)
        for(int i=0;i<envs;i++) {
            float obs[32],reward,action[6]={2,2,1,0,0,0}; uint32_t hash=2166136261u,random=73+i;
            int episodes=0;
            swat_training_observe(courses[i],obs);
            for(int step=0;step<steps;step++) {
                // Alternate goal-directed traversal and randomized controls.
                float error=atan2f(obs[13],obs[12]);
                action[0]=error<-.07f ? 0 : error<-.015f ? 1 : error>.07f ? 4 : error>.015f ? 3 : 2;
                action[1]=fabsf(error)<.9f ? 2 : 1;
                action[2]=1; action[3]=obs[26]<.65f && obs[21]>obs[26]+.08f; action[4]=0; action[5]=1;
                if((step/300)%2) {
                    const int sizes[]={5,3,3,2,2,3};
                    for(int j=0;j<6;j++) action[j]=swat_random(&random)%sizes[j];
                }
                int terminal=swat_training_step(courses[i],action,obs,&reward);
                hash=hash_bytes(hash,obs,sizeof(obs)); hash=hash_bytes(hash,&reward,sizeof(reward)); hash=hash_bytes(hash,&terminal,sizeof(terminal));
                if(trace) {
                    SwatSnapshot snapshot; unsigned char bytes[SWAT_NET_PACKET_MAX];
                    swat_capture_snapshot(swat_training_sim(courses[i]),1,&snapshot);
                    uint32_t count=(uint32_t)swat_encode_snapshot(bytes,sizeof(bytes),&snapshot);
                    if(!count || fwrite(obs,sizeof(obs),1,trace)!=1 || fwrite(&reward,sizeof(reward),1,trace)!=1 ||
                       fwrite(&terminal,sizeof(terminal),1,trace)!=1 || fwrite(&count,sizeof(count),1,trace)!=1 || fwrite(bytes,count,1,trace)!=1) abort();
                }
                if(terminal) {
                    successes+=terminal==1; resets++; episodes++;
                    swat_training_reset(courses[i],2718+i+envs*episodes,episodes%2,stage);
                    swat_training_observe(courses[i],obs);
                }
            }
            checksum^=hash;
        }
        double elapsed=now()-start,cpu_seconds=cpu_now()-cpu_start;
        printf("stage=%d envs=%d threads=%d decisions=%d resets=%d successes=%d seconds=%.6f cpu_seconds=%.6f decisions_per_second=%.0f checksum=%08x\n",
               stage,envs,threads,steps*envs,resets,successes,elapsed,cpu_seconds,steps*envs/elapsed,checksum);
        fflush(stdout);
        for(int i=0;i<envs;i++) swat_training_close(courses[i]);
    }
    free(courses); if(trace && fclose(trace)) return 2;
    return 0;
}
