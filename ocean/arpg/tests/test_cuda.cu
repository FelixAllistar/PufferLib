#define PUFFER_GPU_ENV
#include "../arpg.h"
#include "../arpg.cu"
#include <cassert>
#include <vector>

int main(void) {
    Ini ini={};puf_ini_load_env(&ini,"arpg",0,nullptr);
    Dict* cfg=puf_ini_section(&ini,"env",0);
    const int count=17;
    dict_set(cfg,"enemy_cap",31); // odd pool dimensions exercise SoA alignment
    Env* envs=puf_envs_create(count,cfg);
    ARCudaSim* sim=ar_native_find(envs);
    ar_cuda_reset_all(sim,42);
    std::vector<float> actions(count*NUM_ATNS,0),obs(count*AR_OBS_SIZE);
    AR_CUDA_CHECK(cudaMemcpy(sim->actions,actions.data(),actions.size()*sizeof(float),cudaMemcpyHostToDevice));
    for(int t=0;t<3600;t++)ar_cuda_step_range(sim,0,count);
    AR_CUDA_CHECK(cudaDeviceSynchronize());
    float hp[count],produced[count];int enemies[count],pets[count];
    AR_CUDA_CHECK(cudaMemcpy(hp,sim->hp,sizeof(hp),cudaMemcpyDeviceToHost));
    AR_CUDA_CHECK(cudaMemcpy(produced,sim->harvested,sizeof(produced),cudaMemcpyDeviceToHost));
    AR_CUDA_CHECK(cudaMemcpy(enemies,sim->enemy_count,sizeof(enemies),cudaMemcpyDeviceToHost));
    AR_CUDA_CHECK(cudaMemcpy(pets,sim->pets_alive,sizeof(pets),cudaMemcpyDeviceToHost));
    AR_CUDA_CHECK(cudaMemcpy(obs.data(),sim->observations,obs.size()*sizeof(float),cudaMemcpyDeviceToHost));
    for(int i=0;i<count;i++) {
        assert(hp[i]==sim->cfg.player_health && enemies[i]==0 && pets[i]==2);
        assert(produced[i]>=4);
    }
    for(float value:obs)assert(std::isfinite(value));
    for(int i=0;i<count;i++) {actions[i*NUM_ATNS+5]=AR_TASK_HOLD;actions[i*NUM_ATNS+6]=AR_TASK_HOME;}
    AR_CUDA_CHECK(cudaMemcpy(sim->actions,actions.data(),actions.size()*sizeof(float),cudaMemcpyHostToDevice));
    ar_cuda_step_range(sim,0,count);AR_CUDA_CHECK(cudaDeviceSynchronize());
    int tasks[count*AR_MAX_PETS];
    AR_CUDA_CHECK(cudaMemcpy(tasks,sim->pet_task,sizeof(tasks),cudaMemcpyDeviceToHost));
    for(int i=0;i<count;i++)assert(tasks[i]==AR_TASK_HOLD && tasks[count+i]==AR_TASK_HOME);
    sim->cfg.max_steps=3602;ar_cuda_step_range(sim,0,count);
    AR_CUDA_CHECK(cudaDeviceSynchronize());
    int ticks[count];float terminal[count];
    AR_CUDA_CHECK(cudaMemcpy(ticks,sim->tick,sizeof(ticks),cudaMemcpyDeviceToHost));
    AR_CUDA_CHECK(cudaMemcpy(terminal,sim->terminals,sizeof(terminal),cudaMemcpyDeviceToHost));
    for(int i=0;i<count;i++)assert(ticks[i]==0 && terminal[i]==1);
    sim->cfg.max_steps=900;
    int sizes[]=ACT_SIZES;uint32_t rng=17;
    for(int t=0;t<3600;t++) {
        if(t%30==0) {
            for(int i=0;i<count;i++)for(int h=0;h<NUM_ATNS;h++) {
                rng=rng*1664525u+1013904223u;actions[i*NUM_ATNS+h]=(float)((rng>>8)%sizes[h]);
            }
            AR_CUDA_CHECK(cudaMemcpy(sim->actions,actions.data(),actions.size()*sizeof(float),cudaMemcpyHostToDevice));
        }
        ar_cuda_step_range(sim,0,count);
        if(t%120==0) {
            AR_CUDA_CHECK(cudaMemcpy(obs.data(),sim->observations,obs.size()*sizeof(float),cudaMemcpyDeviceToHost));
            for(float value:obs)assert(std::isfinite(value));
        }
    }
    AR_CUDA_CHECK(cudaDeviceSynchronize());
    puf_envs_close(envs);puf_ini_free(&ini);
    puts("ARPG CUDA idle economy, 237-float observations, pet task heads, terminal reset: PASS");
}
