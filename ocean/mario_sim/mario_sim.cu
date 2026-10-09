#pragma once
#define PUF_BACKEND PUF_GPU
#define PUFFER_GPU_ENV 1
typedef float obs_t;
#include <cuda_runtime.h>
#include <assert.h>
#include "mario_sim.h"
#include "runtime_cuda.h"

static SmbLogic* smb_states;
static SmbBankEntry* smb_bank_device;
static uint8_t* smb_worlds_device;
static uint32_t *smb_eligible_device,*smb_world_ids;
static float *smb_observations,*smb_actions,*smb_rewards,*smb_terminals;
static int smb_count;
static cudaStream_t smb_stream;
static SmbCudaRuntime smb_engine;

__device__ static void smb_require_valid(const SmbLogic* s,int env) {
    if(s->fault){printf("Mario simulation fault: env=%d pc=%04x fault=%x\n",env,s->pc,s->fault);assert(!s->fault);}
}
__global__ void smb_reset_kernel(Env* envs,SmbLogic* states,const SmbBankEntry* bank,const uint32_t* eligible,
        int eligible_count,uint32_t* worlds,float* rewards,float* terminals,SmbTaskConfig cfg,int count) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;if(i>=count)return;
    envs[i].num_agents=1;envs[i].episode={};envs[i].episode.rng=smb_seed(cfg.seed,(unsigned)i);
    smb_task_reset(&states[i],&envs[i].episode,&cfg,bank,eligible,eligible_count);smb_require_valid(&states[i],i);
    worlds[i]=envs[i].episode.world;rewards[i]=terminals[i]=0;
}
__global__ void smb_after_kernel(Env* envs,SmbLogic* states,const SmbBankEntry* bank,const uint32_t* eligible,
        int eligible_count,uint32_t* worlds,float* rewards,float* terminals,SmbTaskConfig cfg,int count) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;if(i>=count)return;
    auto* e=&envs[i];smb_require_valid(&states[i],i);
    rewards[i]=smb_task_after_frame(&states[i],&e->episode,&cfg);
    int done=e->episode.status!=SMB_EPISODE_ACTIVE;terminals[i]=(float)done;
    if(done) {
        smb_log_episode(&e->log,&e->episode,&cfg);
        smb_task_reset(&states[i],&e->episode,&cfg,bank,eligible,eligible_count);smb_require_valid(&states[i],i);
        worlds[i]=e->episode.world;
    }
}
__global__ void smb_observe_kernel(const SmbLogic* states,float* observations,int count) {
    int env=blockIdx.x;if(env>=count)return;
    for(int k=threadIdx.x;k<SMB_DEBUG_OBS;k+=blockDim.x)
        observations[(size_t)env*SMB_DEBUG_OBS+k]=(float)states[env].ram[k]*(1.0f/256.0f);
}
void puf_init(Env* env,Dict*) {env->num_agents=1;}
Env* puf_vec_create(int count,Dict* kwargs,obs_t* observations,float* actions,float* rewards,float* terminals) {
    if(smb_states||count<1)throw std::runtime_error("invalid Mario batch creation");
    smb_host_setup(kwargs);smb_count=count;smb_observations=observations;smb_actions=actions;
    smb_rewards=rewards;smb_terminals=terminals;
    Env* envs=nullptr;
    smb_cuda_check(cudaMalloc(&envs,(size_t)count*sizeof(Env)));smb_cuda_check(cudaMemset(envs,0,(size_t)count*sizeof(Env)));
    smb_cuda_check(cudaMalloc(&smb_states,(size_t)count*sizeof(SmbLogic)));
    smb_cuda_check(cudaMalloc(&smb_world_ids,(size_t)count*sizeof(uint32_t)));
    smb_cuda_check(cudaMalloc(&smb_worlds_device,smb_host->bank->worlds.size()));
    smb_cuda_check(cudaMemcpy(smb_worlds_device,smb_host->bank->worlds.data(),smb_host->bank->worlds.size(),cudaMemcpyHostToDevice));
    size_t bank_bytes=smb_host->bank->entries.size()*sizeof(SmbBankEntry);
    smb_cuda_check(cudaMalloc(&smb_bank_device,bank_bytes));
    smb_cuda_check(cudaMemcpy(smb_bank_device,smb_host->bank->entries.data(),bank_bytes,cudaMemcpyHostToDevice));
    size_t indices=smb_host->eligible.size()*sizeof(uint32_t);
    smb_cuda_check(cudaMalloc(&smb_eligible_device,indices));
    smb_cuda_check(cudaMemcpy(smb_eligible_device,smb_host->eligible.data(),indices,cudaMemcpyHostToDevice));
    smb_engine.open(smb_host->module);smb_cuda_check(cudaDeviceSynchronize());return envs;
}
void puf_bind_stream(cudaStream_t stream) {smb_stream=stream;}
void puf_reset(Env* envs) {
    smb_reset_kernel<<<(smb_count+127)/128,128,0,smb_stream>>>(envs,smb_states,smb_bank_device,smb_eligible_device,
        (int)smb_host->eligible.size(),smb_world_ids,smb_rewards,smb_terminals,smb_host->cfg,smb_count);
    smb_cuda_check(cudaGetLastError());
    smb_observe_kernel<<<smb_count,128,0,smb_stream>>>(smb_states,smb_observations,smb_count);smb_cuda_check(cudaGetLastError());
}
void puf_step(Env* envs) {
    smb_engine.step(smb_states,smb_worlds_device,smb_world_ids,smb_actions,smb_count,smb_stream);
    smb_after_kernel<<<(smb_count+127)/128,128,0,smb_stream>>>(envs,smb_states,smb_bank_device,smb_eligible_device,
        (int)smb_host->eligible.size(),smb_world_ids,smb_rewards,smb_terminals,smb_host->cfg,smb_count);
    smb_cuda_check(cudaGetLastError());
    smb_observe_kernel<<<smb_count,128,0,smb_stream>>>(smb_states,smb_observations,smb_count);smb_cuda_check(cudaGetLastError());
}
void puf_render(Env*) {
#ifndef SMB_HEADLESS
    SmbLogic state;smb_cuda_check(cudaStreamSynchronize(smb_stream));
    smb_cuda_check(cudaMemcpy(&state,smb_states,sizeof(state),cudaMemcpyDeviceToHost));smb_render_state(&state,smb_host->bank->worlds.data());
#endif
}
void puf_close(Env* envs) {
    if(!smb_states)return;
    smb_cuda_check(cudaStreamSynchronize(smb_stream));smb_engine.close();
    smb_cuda_check(cudaFree(smb_states));smb_cuda_check(cudaFree(smb_world_ids));smb_cuda_check(cudaFree(smb_worlds_device));
    smb_cuda_check(cudaFree(smb_bank_device));smb_cuda_check(cudaFree(smb_eligible_device));smb_cuda_check(cudaFree(envs));
    smb_states=nullptr;smb_bank_device=nullptr;smb_eligible_device=nullptr;smb_worlds_device=nullptr;smb_world_ids=nullptr;
    smb_stream=0;smb_count=0;smb_host.reset();
}
