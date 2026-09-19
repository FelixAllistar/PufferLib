// CPU-only observation-construction comparison in production host precision.
// No GPU work, training, or checkpoint writes. Uses the same cached palette
// and real native NES frame for the original and SIMD/fallback paths.
#include <cuda_runtime.h>
#include <cuda_bf16.h>
#include <cublas_v2.h>
#include <curand_kernel.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#define ENV_HEADER "ocean/retro/retro.h"
#include "../src/pufferl_preamble.h"
#include "../ocean/retro/tests/observation_reference.h"

using ObservationBuilder=void(*)(const Env*,obs_t*);
static volatile float checksum;
static double measure(ObservationBuilder builder,const Env* env,obs_t* obs,int repeats) {
    for(int i=0;i<100;i++) builder(env,obs);
    auto start=std::chrono::steady_clock::now();
    for(int i=0;i<repeats;i++) {
        builder(env,obs);
        checksum=to_float(obs[(i%RETRO_TILES)+RETRO_EGO_SIZE+RETRO_ENT_SIZE]);
    }
    return std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/repeats;
}
int main(int argc,char** argv) {
    int repeats=argc>1?atoi(argv[1]):10000;
    if(repeats<1||repeats>1000000) return 2;
    Dict cfg={}; dict_set_str(&cfg,"spawn_levels","1-1");
    Env env={}; env.rng=73; puf_init(&env,&cfg);
    std::vector<obs_t> obs(OBS_SIZE);
    float action=0,reward=0,terminal=0;
    env.agents[0]={obs.data(),&action,&reward,&terminal,nullptr,0}; puf_reset(&env);
    for(int i=0;i<32;i++) retro_frame(&env,RETRO_BTN_RIGHT|RETRO_BTN_B);
    retro_sync_from_emu(&env); retro_compute_obs_real(&env,obs.data());
    if(!retro_observation_matches_reference(&env,obs.data())) return 1;
    for(int pass=0;pass<4;pass++) {
        bool reference=pass==0||pass==3;
        double us=measure(reference?retro_observation_reference:retro_compute_obs_real,&env,obs.data(),repeats);
        printf("OBS precision=%s variant=%s us=%.6f\n",USE_BF16?"bf16":"float32",reference?"full-staged":"candidate",us);
        fflush(stdout);
    }
    puf_close(&env); dict_clear(&cfg);
}
