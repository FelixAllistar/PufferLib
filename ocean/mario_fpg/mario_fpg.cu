#pragma once
#define PUF_BACKEND PUF_GPU
#define PUFFER_GPU_ENV 1
typedef float obs_t;
#include <cuda_runtime.h>
#include "mario_fpg.h"
#define FPG_CUDA(call) do {cudaError_t err=(call);if(err!=cudaSuccess) {fprintf(stderr,"mario_fpg CUDA: %s\n",cudaGetErrorString(err));abort();}} while(0)
static FpgConfig fpg_host_config;
static __constant__ FpgConfig fpg_device_config;
static FpgCase* fpg_device_bank;
static FpgState* fpg_states;
static int fpg_count;
static float *fpg_obs,*fpg_actions,*fpg_rewards,*fpg_terminals;
static cudaStream_t fpg_stream;
__global__ void fpg_reset_kernel(Env* envs,FpgState* states,const FpgCase* bank,int bank_count,float* rewards,float* terminals,int n) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;if(i>=n)return;
    envs[i].num_agents=1;envs[i].rng=fpg_hash((uint32_t)fpg_device_config.seed^(unsigned)i)|1u;
    fpg_reset_task(&states[i],&fpg_device_config,bank,bank_count,&envs[i].curriculum,&envs[i].rng);
    rewards[i]=terminals[i]=0;
}
__global__ void fpg_step_kernel(Env* envs,FpgState* states,const FpgCase* bank,int bank_count,const float* actions,float* rewards,float* terminals,int n) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;if(i>=n)return;
    FpgState* s=&states[i];rewards[i]=fpg_step_task(s,&fpg_device_config,(int)actions[i]);
    int done=s->status!=FPG_ACTIVE;terminals[i]=(float)done;
    if(done) {fpg_log_episode(&envs[i].log,s);fpg_record(&envs[i].curriculum,s);
        fpg_reset_task(s,&fpg_device_config,bank,bank_count,&envs[i].curriculum,&envs[i].rng);}
}
__global__ void fpg_observe_kernel(const FpgState* states,float* observations,int n) {
    int env=blockIdx.x;if(env>=n)return;
    for(int i=threadIdx.x;i<FPG_OBS;i+=blockDim.x) observations[(size_t)env*FPG_OBS+i]=fpg_observation(&states[env],&fpg_device_config,i);
}
void puf_init(Env* e,Dict*) {e->num_agents=1;}
Env* puf_vec_create(int total_agents,Dict* kwargs,obs_t* observations,float* actions,float* rewards,float* terminals) {
    if(fpg_states||total_agents<1)abort();fpg_host_config=fpg_config(kwargs);fpg_load_bank(kwargs);fpg_count=total_agents;
    fpg_obs=observations;fpg_actions=actions;fpg_rewards=rewards;fpg_terminals=terminals;
    FPG_CUDA(cudaMemcpyToSymbol(fpg_device_config,&fpg_host_config,sizeof(FpgConfig)));
    Env* envs=NULL;FPG_CUDA(cudaMalloc((void**)&envs,(size_t)fpg_count*sizeof(Env)));
    FPG_CUDA(cudaMemset(envs,0,(size_t)fpg_count*sizeof(Env)));
    FPG_CUDA(cudaMalloc((void**)&fpg_states,(size_t)fpg_count*sizeof(FpgState)));
    FPG_CUDA(cudaMalloc((void**)&fpg_device_bank,(size_t)fpg_bank_count*sizeof(FpgCase)));
    FPG_CUDA(cudaMemcpy(fpg_device_bank,fpg_bank,(size_t)fpg_bank_count*sizeof(FpgCase),cudaMemcpyHostToDevice));
    // reset/step can run on a nonblocking trainer stream after this returns.
    FPG_CUDA(cudaDeviceSynchronize());
    return envs;
}
void puf_bind_stream(cudaStream_t stream) {fpg_stream=stream;}
void puf_reset(Env* envs) {
    fpg_reset_kernel<<<(fpg_count+127)/128,128,0,fpg_stream>>>(envs,fpg_states,fpg_device_bank,fpg_bank_count,fpg_rewards,fpg_terminals,fpg_count);
    FPG_CUDA(cudaGetLastError());fpg_observe_kernel<<<fpg_count,128,0,fpg_stream>>>(fpg_states,fpg_obs,fpg_count);FPG_CUDA(cudaGetLastError());
}
void puf_step(Env* envs) {
    fpg_step_kernel<<<(fpg_count+127)/128,128,0,fpg_stream>>>(envs,fpg_states,fpg_device_bank,fpg_bank_count,fpg_actions,fpg_rewards,fpg_terminals,fpg_count);
    FPG_CUDA(cudaGetLastError());fpg_observe_kernel<<<fpg_count,128,0,fpg_stream>>>(fpg_states,fpg_obs,fpg_count);FPG_CUDA(cudaGetLastError());
}
void puf_render(Env*) {
#ifndef FPG_HEADLESS
    FpgState s;FPG_CUDA(cudaStreamSynchronize(fpg_stream));FPG_CUDA(cudaMemcpy(&s,fpg_states,sizeof(s),cudaMemcpyDeviceToHost));fpg_render_state(&s);
#endif
}
void puf_close(Env* envs) {
    if(!fpg_states)return;FPG_CUDA(cudaStreamSynchronize(fpg_stream));
    FPG_CUDA(cudaFree(fpg_states));FPG_CUDA(cudaFree(fpg_device_bank));FPG_CUDA(cudaFree(envs));fpg_states=NULL;fpg_device_bank=NULL;fpg_stream=0;
}
