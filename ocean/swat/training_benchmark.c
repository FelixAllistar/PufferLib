// Actual movement adapter, including observations and resets. Rollout order
// visits every environment each decision, matching the trainer's cache pressure.
// --schedule env retains the earlier benchmark's cache-hot per-world ordering.
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
typedef struct Course {
    void* env;
    float obs[SWAT_LOCOMOTION_OBS];
    uint32_t hash,random;
    int resets,successes;
} Course;
static void step_course(Course* c,int i,int envs,int stage,int step,FILE* trace,bool observations_only) {
    float reward,action[SWAT_LOCOMOTION_HEADS];
    // Alternate goal-directed traversal and randomized controls.
    float error=atan2f(c->obs[13],c->obs[12]);
    action[0]=error<-.07f ? 0 : error<-.015f ? 1 : error>.07f ? 4 : error>.015f ? 3 : 2;
    action[1]=fabsf(error)<.9f ? 2 : 1;
    action[2]=1; action[3]=c->obs[26]<.65f && c->obs[21]>c->obs[26]+.08f; action[4]=0; action[5]=1;
    if((step/300)%2) {
        const int sizes[]=SWAT_LOCOMOTION_ACTION_SIZES;
        for(int j=0;j<SWAT_LOCOMOTION_HEADS;j++) action[j]=swat_random(&c->random)%sizes[j];
    }
    int terminal=swat_training_step(c->env,action,c->obs,&reward);
    c->hash=hash_bytes(c->hash,c->obs,sizeof(c->obs)); c->hash=hash_bytes(c->hash,&reward,sizeof(reward)); c->hash=hash_bytes(c->hash,&terminal,sizeof(terminal));
    if(trace) {
        if(fwrite(c->obs,sizeof(c->obs),1,trace)!=1 || fwrite(&reward,sizeof(reward),1,trace)!=1 || fwrite(&terminal,sizeof(terminal),1,trace)!=1) abort();
        if(!observations_only) {
            SwatSnapshot snapshot; unsigned char bytes[SWAT_NET_PACKET_MAX];
            swat_capture_snapshot(swat_training_sim(c->env),1,&snapshot);
            uint32_t count=(uint32_t)swat_encode_snapshot(bytes,sizeof(bytes),&snapshot);
            if(!count || fwrite(&count,sizeof(count),1,trace)!=1 || fwrite(bytes,count,1,trace)!=1) abort();
        }
    }
    if(terminal) {
        c->successes+=terminal==1; c->resets++;
        swat_training_reset(c->env,2718+i+envs*c->resets,c->resets%2,stage);
        swat_training_observe(c->env,c->obs);
    }
}
int main(int argc,char** argv) {
    int steps=3000,envs=32,threads=4; const char* trace_path=NULL;
    bool rollout=true,observations_only=false;
    for(int i=1;i<argc;i++) {
        if(i+1<argc && !strcmp(argv[i],"--steps")) steps=atoi(argv[++i]);
        else if(i+1<argc && !strcmp(argv[i],"--envs")) envs=atoi(argv[++i]);
        else if(i+1<argc && !strcmp(argv[i],"--threads")) threads=atoi(argv[++i]);
        else if(i+1<argc && !strcmp(argv[i],"--trace")) trace_path=argv[++i];
        else if(!strcmp(argv[i],"--observations-only")) observations_only=true;
        else if(i+1<argc && !strcmp(argv[i],"--schedule")) {
            const char* schedule=argv[++i];
            if(strcmp(schedule,"rollout") && strcmp(schedule,"env")) return 2;
            rollout=!strcmp(schedule,"rollout");
        } else { fprintf(stderr,"usage: %s [--steps N] [--envs N] [--threads N] [--schedule rollout|env] [--trace FILE] [--observations-only]\n",argv[0]); return 2; }
    }
    if(steps<1 || envs<1 || envs>4096 || threads<1 || threads>128 || (trace_path && (envs!=1 || threads!=1))) return 2;
    FILE* trace=trace_path ? fopen(trace_path,"wb") : NULL;
    if(trace_path && !trace) { perror(trace_path); return 2; }
    Course* courses=calloc(envs,sizeof(*courses)); if(!courses) return 2;
    for(int stage=0;stage<5;stage++) {
        for(int i=0;i<envs;i++) {
            courses[i]=(Course){.env=swat_training_create(2718+i,i%2,stage),.hash=2166136261u,.random=73+i};
            if(!courses[i].env) return 2;
            swat_training_observe(courses[i].env,courses[i].obs);
        }
        double start=now(),cpu_start=cpu_now();
        if(rollout) {
#pragma omp parallel num_threads(threads)
            for(int step=0;step<steps;step++) {
#pragma omp for schedule(static)
                for(int i=0;i<envs;i++) step_course(&courses[i],i,envs,stage,step,trace,observations_only);
            }
        } else {
#pragma omp parallel for num_threads(threads) schedule(static)
            for(int i=0;i<envs;i++) for(int step=0;step<steps;step++) step_course(&courses[i],i,envs,stage,step,trace,observations_only);
        }
        double elapsed=now()-start,cpu_seconds=cpu_now()-cpu_start;
        int resets=0,successes=0; uint32_t checksum=0;
        for(int i=0;i<envs;i++) { resets+=courses[i].resets; successes+=courses[i].successes; checksum^=courses[i].hash; }
        printf("stage=%d envs=%d threads=%d decisions=%lld resets=%d successes=%d seconds=%.6f cpu_seconds=%.6f decisions_per_second=%.0f checksum=%08x schedule=%s sim_bytes=%zu\n",
               stage,envs,threads,(long long)steps*envs,resets,successes,elapsed,cpu_seconds,(double)steps*envs/elapsed,checksum,rollout ? "rollout" : "env",sizeof(SwatSim));
        fflush(stdout);
        for(int i=0;i<envs;i++) swat_training_close(courses[i].env);
    }
    free(courses); if(trace && fclose(trace)) return 2;
    return 0;
}
