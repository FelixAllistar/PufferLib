#pragma once
#define PUF_BACKEND PUF_GPU
#define PUFFER_GPU_ENV 1
typedef float obs_t;
#include <cuda_runtime.h>
#include <assert.h>
#include "mario_fpg_time.h"
#include "../mario_sim/runtime_cuda.h"

static SmbLogic* fpt_states;
static SmbBankEntry* fpt_bank_device;
static FpgTimeEntry* fpt_table_device;
static uint8_t* fpt_worlds_device;
static uint32_t *fpt_eligible_device,*fpt_world_ids;
static float *fpt_observations,*fpt_actions,*fpt_rewards,*fpt_terminals;
static int fpt_count;
static cudaStream_t fpt_stream;
static SmbCudaRuntime fpt_engine;
static Env* fpt_device_envs;

__device__ static void fpt_require_valid(const SmbLogic* s,int env) {
    if(s->fault){printf("FPG timing simulation fault: env=%d pc=%04x fault=%x\n",env,s->pc,s->fault);assert(!s->fault);}
}
__global__ void fpt_reset_kernel(Env* envs,SmbLogic* states,const SmbBankEntry* bank,const uint32_t* eligible,
        int eligible_count,const FpgTimeEntry* table,uint32_t* worlds,float* rewards,float* terminals,
        SmbTaskConfig task,FpgTimeConfig time,FptCurriculumConfig curriculum,int count) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;if(i>=count)return;
    envs[i].num_agents=1;envs[i].episode={};envs[i].episode.rng=smb_seed(task.seed,(unsigned)i);envs[i].curriculum={};
    fpt_reset_state(&envs[i],&states[i],&task,&time,bank,eligible,eligible_count,table,&curriculum);fpt_require_valid(&states[i],i);
    worlds[i]=envs[i].episode.world;rewards[i]=terminals[i]=0;
}
__global__ void fpt_after_kernel(Env* envs,SmbLogic* states,const SmbBankEntry* bank,const uint32_t* eligible,
        int eligible_count,const FpgTimeEntry* table,uint32_t* worlds,float* rewards,float* terminals,
        SmbTaskConfig task,FpgTimeConfig time,FptCurriculumConfig curriculum,int count) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;if(i>=count)return;
    auto* e=&envs[i];fpt_require_valid(&states[i],i);
    rewards[i]=fpg_time_after_frame(&states[i],&e->episode,&task,e->target,&time);
    int done=e->episode.status!=SMB_EPISODE_ACTIVE;terminals[i]=(float)done;
    if(done) {
        fpt_log_episode(e,&task,&time,&curriculum,table);
        fpt_reset_state(e,&states[i],&task,&time,bank,eligible,eligible_count,table,&curriculum);fpt_require_valid(&states[i],i);
        worlds[i]=e->episode.world;
    }
}
__global__ void fpt_observe_kernel(Env* envs,const SmbLogic* states,const uint8_t* worlds,float* observations,int count) {
    int env=blockIdx.x;if(env>=count)return;
    const SmbLogic* s=&states[env];const uint8_t* world=worlds+(size_t)envs[env].episode.world*SMB_PRG;
    float* o=observations+(size_t)env*FPT_OBS;
    if(threadIdx.x==0)fpt_observe_player(s,world,o);
    for(int cell=threadIdx.x;cell<FPT_GRID_CELLS;cell+=blockDim.x)
        o[FPT_TERRAIN_OFFSET+cell]=(float)fpt_terrain_tile(s,cell);
    for(int k=threadIdx.x;k<FPT_ENTITY_COUNT;k+=blockDim.x)
        fpt_observe_entity(s,world,&envs[env].observation_history,k,o+FPT_ENTITY_OFFSET+k*FPT_ENTITY_FEATURES);
}
void puf_init(Env* env,Dict*) {env->num_agents=1;}
Env* puf_vec_create(int count,Dict* options,obs_t* observations,float* actions,float* rewards,float* terminals) {
    if(fpt_states||count<1)throw std::runtime_error("invalid FPG timing batch creation");
    fpt_setup(options);fpt_count=count;fpt_observations=observations;fpt_actions=actions;fpt_rewards=rewards;fpt_terminals=terminals;
    Env* envs=nullptr;smb_cuda_check(cudaMalloc(&envs,(size_t)count*sizeof(Env)));smb_cuda_check(cudaMemset(envs,0,(size_t)count*sizeof(Env)));
    smb_cuda_check(cudaMalloc(&fpt_states,(size_t)count*sizeof(SmbLogic)));smb_cuda_check(cudaMalloc(&fpt_world_ids,(size_t)count*sizeof(uint32_t)));
    smb_cuda_check(cudaMalloc(&fpt_worlds_device,fpt_host->bank->worlds.size()));
    smb_cuda_check(cudaMemcpy(fpt_worlds_device,fpt_host->bank->worlds.data(),fpt_host->bank->worlds.size(),cudaMemcpyHostToDevice));
    size_t bank_bytes=fpt_host->bank->entries.size()*sizeof(SmbBankEntry),indices=fpt_host->eligible.size()*sizeof(uint32_t);
    smb_cuda_check(cudaMalloc(&fpt_bank_device,bank_bytes));
    smb_cuda_check(cudaMemcpy(fpt_bank_device,fpt_host->bank->entries.data(),bank_bytes,cudaMemcpyHostToDevice));
    smb_cuda_check(cudaMalloc(&fpt_eligible_device,indices));
    smb_cuda_check(cudaMemcpy(fpt_eligible_device,fpt_host->eligible.data(),indices,cudaMemcpyHostToDevice));
    size_t table_bytes=fpt_host->table.size()*sizeof(FpgTimeEntry);smb_cuda_check(cudaMalloc(&fpt_table_device,table_bytes));
    smb_cuda_check(cudaMemcpy(fpt_table_device,fpt_host->table.data(),table_bytes,cudaMemcpyHostToDevice));
    fpt_engine.open(fpt_host->module.c_str());smb_cuda_check(cudaDeviceSynchronize());fpt_device_envs=envs;return envs;
}
void puf_bind_stream(cudaStream_t stream) {fpt_stream=stream;}
void puf_reset(Env* envs) {
    fpt_reset_kernel<<<(fpt_count+127)/128,128,0,fpt_stream>>>(envs,fpt_states,fpt_bank_device,fpt_eligible_device,
        (int)fpt_host->eligible.size(),fpt_table_device,fpt_world_ids,fpt_rewards,fpt_terminals,fpt_host->task,fpt_host->time,fpt_host->curriculum,fpt_count);
    smb_cuda_check(cudaGetLastError());
    fpt_observe_kernel<<<fpt_count,128,0,fpt_stream>>>(envs,fpt_states,fpt_worlds_device,fpt_observations,fpt_count);smb_cuda_check(cudaGetLastError());
}
void puf_step(Env* envs) {
    fpt_engine.step(fpt_states,fpt_worlds_device,fpt_world_ids,fpt_actions,fpt_count,fpt_stream);
    fpt_after_kernel<<<(fpt_count+127)/128,128,0,fpt_stream>>>(envs,fpt_states,fpt_bank_device,fpt_eligible_device,
        (int)fpt_host->eligible.size(),fpt_table_device,fpt_world_ids,fpt_rewards,fpt_terminals,fpt_host->task,fpt_host->time,fpt_host->curriculum,fpt_count);
    smb_cuda_check(cudaGetLastError());
    fpt_observe_kernel<<<fpt_count,128,0,fpt_stream>>>(envs,fpt_states,fpt_worlds_device,fpt_observations,fpt_count);smb_cuda_check(cudaGetLastError());
}
void puf_render(Env*) {
#ifndef SMB_HEADLESS
    SmbLogic state;smb_cuda_check(cudaStreamSynchronize(fpt_stream));
    smb_cuda_check(cudaMemcpy(&state,fpt_states,sizeof(state),cudaMemcpyDeviceToHost));smb_render_state(&state,fpt_host->bank->worlds.data());
#endif
}
void puf_close(Env* envs) {
    if(!fpt_states)return;smb_cuda_check(cudaStreamSynchronize(fpt_stream));fpt_engine.close();
    smb_cuda_check(cudaFree(fpt_states));smb_cuda_check(cudaFree(fpt_world_ids));smb_cuda_check(cudaFree(fpt_worlds_device));
    smb_cuda_check(cudaFree(fpt_bank_device));smb_cuda_check(cudaFree(fpt_eligible_device));smb_cuda_check(cudaFree(fpt_table_device));smb_cuda_check(cudaFree(envs));
    fpt_states=nullptr;fpt_bank_device=nullptr;fpt_eligible_device=nullptr;fpt_worlds_device=nullptr;fpt_world_ids=nullptr;fpt_table_device=nullptr;
    fpt_stream=0;fpt_count=0;fpt_device_envs=nullptr;fpt_host.reset();
}
static void fpt_gpu_save(const char* checkpoint,Ini*) {
    if(!fpt_host||!fpt_host->curriculum.enabled)return;
    smb_cuda_check(cudaStreamSynchronize(fpt_stream));std::vector<Env> shells(fpt_count);
    smb_cuda_check(cudaMemcpy(shells.data(),fpt_device_envs,shells.size()*sizeof(Env),cudaMemcpyDeviceToHost));
    std::vector<FptCurriculumProgress> progress;for(const auto& env:shells)progress.push_back(env.curriculum);
    fpt_write_progress(checkpoint,fpt_host->bank->header.payload_hash,fpt_host->curriculum,progress);
}
__global__ void fpt_resume_kernel(Env* envs,SmbLogic* states,const FptCurriculumProgress* saved,
        const SmbBankEntry* bank,const uint32_t* eligible,int eligible_count,const FpgTimeEntry* table,
        uint32_t* worlds,float* rewards,float* terminals,SmbTaskConfig task,FpgTimeConfig time,FptCurriculumConfig curriculum,int count) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;if(i>=count)return;
    envs[i].curriculum=saved[i];fpt_reset_state(&envs[i],&states[i],&task,&time,bank,eligible,eligible_count,table,&curriculum);
    fpt_require_valid(&states[i],i);worlds[i]=envs[i].episode.world;rewards[i]=terminals[i]=0;
}
static void fpt_gpu_load(const char* checkpoint,Ini* ini) {
    if(!fpt_host||!fpt_host->curriculum.enabled||!fpt_integer(puf_ini_section(ini,"env",0),"curriculum_resume",1,0,1))return;
    auto saved=fpt_read_progress(checkpoint);if(saved.progress.empty())return;fpt_check_resume(saved,fpt_host->bank->header.payload_hash);
    std::vector<FptCurriculumProgress> progress;for(int i=0;i<fpt_count;i++)progress.push_back(fpt_restore_progress(saved,i,fpt_host->curriculum));
    FptCurriculumProgress* device;smb_cuda_check(cudaMalloc(&device,progress.size()*sizeof(FptCurriculumProgress)));
    smb_cuda_check(cudaMemcpyAsync(device,progress.data(),progress.size()*sizeof(FptCurriculumProgress),cudaMemcpyHostToDevice,fpt_stream));
    fpt_resume_kernel<<<(fpt_count+127)/128,128,0,fpt_stream>>>(fpt_device_envs,fpt_states,device,fpt_bank_device,fpt_eligible_device,
        (int)fpt_host->eligible.size(),fpt_table_device,fpt_world_ids,fpt_rewards,fpt_terminals,fpt_host->task,fpt_host->time,fpt_host->curriculum,fpt_count);
    smb_cuda_check(cudaGetLastError());fpt_observe_kernel<<<fpt_count,128,0,fpt_stream>>>(fpt_device_envs,fpt_states,fpt_worlds_device,fpt_observations,fpt_count);
    smb_cuda_check(cudaGetLastError());smb_cuda_check(cudaStreamSynchronize(fpt_stream));smb_cuda_check(cudaFree(device));
    fprintf(stderr,"[mario_fpg_time] restored curriculum progress for %d agents\n",fpt_count);
}
#define PUF_CHECKPOINT_HOOK(checkpoint,ini) fpt_gpu_save(checkpoint,ini)
#define PUF_LOAD_HOOK(checkpoint,ini) fpt_gpu_load(checkpoint,ini)
