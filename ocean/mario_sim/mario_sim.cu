#pragma once
#define PUF_BACKEND PUF_GPU
#define PUFFER_GPU_ENV 1
typedef float obs_t;
#include <cuda_runtime.h>
#include <assert.h>
#include <deque>
#define TEACHER_HD __host__ __device__
#include "mario_sim.h"
#include "runtime_cuda.h"

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
// Curriculum teacher buffers. Device: cdf[G], offsets[G+1], area[G], section[G], counters[4G+4]
// (selected|attempts|wins|death_weights|events[SMB_TEACHER_EVENTS]), route pairs from[P], to[P], loop table key[L], x[L]. Pinned
// host mirrors carry the counters out and the CDF back.
static float *fpt_teacher_cdf,*fpt_teacher_cdf_pinned;
static uint32_t *fpt_teacher_offsets,*fpt_teacher_area,*fpt_teacher_counters,*fpt_teacher_counters_pinned;
static uint32_t *fpt_teacher_pair_from,*fpt_teacher_pair_to,*fpt_teacher_loop_key;
static int32_t* fpt_teacher_loop_x;
static int32_t* fpt_teacher_section;
// The refresh is stream-ordered (counters D2H -> host callback -> CDF H2D) so it also works inside the
// trainer's captured CUDA graph, where puf_step's host code runs only while capturing. Each captured
// rollout gets one refresh node after its first step; the callback reads how many steps that capture held.
static std::deque<int> fpt_teacher_capture_steps;
static unsigned long long fpt_teacher_capture_id;
static int fpt_teacher_direct_steps;

__device__ static void fpt_require_valid(const SmbLogic* s,int env) {
    if(s->fault){printf("FPG timing simulation fault: env=%d pc=%04x fault=%x\n",env,s->pc,s->fault);assert(!s->fault);}
}
__global__ void fpt_reset_kernel(Env* envs,SmbLogic* states,const SmbBankEntry* bank,const uint32_t* eligible,
        int eligible_count,const FpgTimeEntry* table,uint32_t* worlds,float* rewards,float* terminals,
        SmbTaskConfig task,FpgTimeConfig time,FptCurriculumConfig curriculum,SmbGameConfig game,SmbPracticeConfig practice,SmbTeacherDevice teacher,int count) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;if(i>=count)return;
    envs[i].num_agents=1;envs[i].is_fpg=smb_fpg_actor(i,count,&game);envs[i].episode={};envs[i].episode.rng=smb_seed(task.seed,(unsigned)i);envs[i].curriculum={};envs[i].practice={};envs[i].is_practice=smb_practice_actor(i,count,&game,&practice);
    fpt_reset_state(&envs[i],&states[i],&task,&time,bank,eligible,eligible_count,table,&curriculum,&game,&practice,&teacher);fpt_require_valid(&states[i],i);
    worlds[i]=envs[i].episode.world;rewards[i]=terminals[i]=0;
}
__global__ void fpt_after_kernel(Env* envs,SmbLogic* states,const SmbBankEntry* bank,const uint32_t* eligible,
        int eligible_count,const FpgTimeEntry* table,uint32_t* worlds,float* rewards,float* terminals,
        SmbTaskConfig task,FpgTimeConfig time,FptCurriculumConfig curriculum,SmbGameConfig game,SmbPracticeConfig practice,SmbTeacherDevice teacher,int count) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;if(i>=count)return;
    auto* e=&envs[i];fpt_require_valid(&states[i],i);
    rewards[i]=smb_after_frame(e,&states[i],&task,&time,&curriculum,table,&game,&practice,&teacher);
    int done=e->episode.status!=SMB_EPISODE_ACTIVE;terminals[i]=(float)done;
    if(done) {
        fpt_reset_state(e,&states[i],&task,&time,bank,eligible,eligible_count,table,&curriculum,&game,&practice,&teacher);fpt_require_valid(&states[i],i);
        worlds[i]=e->episode.world;
    }
}
__global__ void fpt_observe_kernel(Env* envs,const SmbLogic* states,const uint8_t* worlds,float* observations,int count,bool fpg_only=false) {
    int env=blockIdx.x;if(env>=count||(fpg_only&&!envs[env].is_fpg))return;
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
    size_t bank_bytes=fpt_host->runtime_bank.size()*sizeof(SmbBankEntry),indices=fpt_host->eligible.size()*sizeof(uint32_t);
    smb_cuda_check(cudaMalloc(&fpt_bank_device,bank_bytes));
    smb_cuda_check(cudaMemcpy(fpt_bank_device,fpt_host->runtime_bank.data(),bank_bytes,cudaMemcpyHostToDevice));
    smb_cuda_check(cudaMalloc(&fpt_eligible_device,indices));
    smb_cuda_check(cudaMemcpy(fpt_eligible_device,fpt_host->eligible.data(),indices,cudaMemcpyHostToDevice));
    size_t table_bytes=fpt_host->table.size()*sizeof(FpgTimeEntry);smb_cuda_check(cudaMalloc(&fpt_table_device,table_bytes));
    smb_cuda_check(cudaMemcpy(fpt_table_device,fpt_host->table.data(),table_bytes,cudaMemcpyHostToDevice));
    auto& t=fpt_host->teacher;
    if(t.enabled) {
        size_t n=t.groups.size(),counters=(4*n+SMB_TEACHER_EVENTS)*sizeof(uint32_t),pairs=t.pair_from.size()*sizeof(uint32_t),
            loops=t.loop_key.size()*sizeof(uint32_t);
        smb_cuda_check(cudaMalloc(&fpt_teacher_cdf,n*sizeof(float)));smb_cuda_check(cudaMalloc(&fpt_teacher_offsets,(n+1)*sizeof(uint32_t)));
        smb_cuda_check(cudaMalloc(&fpt_teacher_area,n*sizeof(uint32_t)));smb_cuda_check(cudaMalloc(&fpt_teacher_section,n*sizeof(int32_t)));
        smb_cuda_check(cudaMalloc(&fpt_teacher_counters,counters));smb_cuda_check(cudaMemset(fpt_teacher_counters,0,counters));
        smb_cuda_check(cudaMallocHost(&fpt_teacher_cdf_pinned,n*sizeof(float)));smb_cuda_check(cudaMallocHost(&fpt_teacher_counters_pinned,counters));
        memcpy(fpt_teacher_cdf_pinned,t.cdf.data(),n*sizeof(float));memset(fpt_teacher_counters_pinned,0,counters);
        smb_cuda_check(cudaMemcpy(fpt_teacher_cdf,t.cdf.data(),n*sizeof(float),cudaMemcpyHostToDevice));
        smb_cuda_check(cudaMemcpy(fpt_teacher_offsets,t.offsets.data(),(n+1)*sizeof(uint32_t),cudaMemcpyHostToDevice));
        smb_cuda_check(cudaMemcpy(fpt_teacher_area,t.area.data(),n*sizeof(uint32_t),cudaMemcpyHostToDevice));
        smb_cuda_check(cudaMemcpy(fpt_teacher_section,t.section.data(),n*sizeof(int32_t),cudaMemcpyHostToDevice));
        if(pairs) {
            smb_cuda_check(cudaMalloc(&fpt_teacher_pair_from,pairs));smb_cuda_check(cudaMalloc(&fpt_teacher_pair_to,pairs));
            smb_cuda_check(cudaMemcpy(fpt_teacher_pair_from,t.pair_from.data(),pairs,cudaMemcpyHostToDevice));
            smb_cuda_check(cudaMemcpy(fpt_teacher_pair_to,t.pair_to.data(),pairs,cudaMemcpyHostToDevice));
        }
        if(loops) {
            smb_cuda_check(cudaMalloc(&fpt_teacher_loop_key,loops));smb_cuda_check(cudaMalloc(&fpt_teacher_loop_x,loops));
            smb_cuda_check(cudaMemcpy(fpt_teacher_loop_key,t.loop_key.data(),loops,cudaMemcpyHostToDevice));
            smb_cuda_check(cudaMemcpy(fpt_teacher_loop_x,t.loop_x.data(),loops,cudaMemcpyHostToDevice));
        }
        auto& v=t.device;v.cdf=fpt_teacher_cdf;v.offsets=fpt_teacher_offsets;v.area=fpt_teacher_area;v.section=fpt_teacher_section;
        v.pairs=(int)t.pair_from.size();v.pair_from=fpt_teacher_pair_from;v.pair_to=fpt_teacher_pair_to;
        v.loops=(int)t.loop_key.size();v.loop_key=fpt_teacher_loop_key;v.loop_x=fpt_teacher_loop_x;
        uint32_t* c=fpt_teacher_counters;v.selected=c;v.attempts=c+n;v.wins=c+2*n;v.death_weights=c+3*n;v.events=c+4*n;
    }
    fpt_engine.open(fpt_host->module.c_str());smb_cuda_check(cudaDeviceSynchronize());fpt_device_envs=envs;return envs;
}
// Host callback on the env stream: account the steps, and at the cadence fold the counter deltas and write the
// new CDF to the pinned buffer that the following stream copy uploads. No CUDA calls here.
static void CUDART_CB fpt_teacher_callback(void* data) {
    if(!fpt_host)return;
    auto& t=fpt_host->teacher;int steps=*(const int*)data;std::lock_guard<std::mutex> guard(t.lock);
    t.agent_steps+=(uint64_t)steps*(uint64_t)fpt_count;t.since_refresh+=(uint64_t)steps;
    if(t.since_refresh<(uint64_t)t.refresh_steps)return;
    t.since_refresh=0;smb_teacher_fold(t,fpt_teacher_counters_pinned,t.agent_steps);
    memcpy(fpt_teacher_cdf_pinned,t.cdf.data(),t.cdf.size()*sizeof(float));
}
static void fpt_teacher_enqueue(int* steps) {
    size_t n=fpt_host->teacher.groups.size();
    smb_cuda_check(cudaMemcpyAsync(fpt_teacher_counters_pinned,fpt_teacher_counters,(4*n+SMB_TEACHER_EVENTS)*sizeof(uint32_t),cudaMemcpyDeviceToHost,fpt_stream));
    smb_cuda_check(cudaLaunchHostFunc(fpt_stream,fpt_teacher_callback,steps));
    smb_cuda_check(cudaMemcpyAsync(fpt_teacher_cdf,fpt_teacher_cdf_pinned,n*sizeof(float),cudaMemcpyHostToDevice,fpt_stream));
}
static void fpt_teacher_after_step() {
    auto& t=fpt_host->teacher;cudaStreamCaptureStatus status;unsigned long long id=0;
    smb_cuda_check(cudaStreamGetCaptureInfo(fpt_stream,&status,&id));
    if(status==cudaStreamCaptureStatusActive) {
        if(fpt_teacher_capture_steps.empty()||id!=fpt_teacher_capture_id) {
            fpt_teacher_capture_id=id;fpt_teacher_capture_steps.push_back(0);fpt_teacher_enqueue(&fpt_teacher_capture_steps.back());
        }
        fpt_teacher_capture_steps.back()++;
    } else if(++fpt_teacher_direct_steps>=t.refresh_steps) {
        fpt_teacher_direct_steps=0;fpt_teacher_enqueue(&t.refresh_steps);
    }
}
// Forced synchronous refresh (resume, tests). Never call while the stream is being captured.
static void fpt_teacher_refresh_now() {
    if(!fpt_host||!fpt_host->teacher.enabled)return;
    auto& t=fpt_host->teacher;size_t n=t.groups.size();
    smb_cuda_check(cudaStreamSynchronize(fpt_stream));
    smb_cuda_check(cudaMemcpy(fpt_teacher_counters_pinned,fpt_teacher_counters,(4*n+SMB_TEACHER_EVENTS)*sizeof(uint32_t),cudaMemcpyDeviceToHost));
    {std::lock_guard<std::mutex> guard(t.lock);smb_teacher_fold(t,fpt_teacher_counters_pinned,t.agent_steps);
     memcpy(fpt_teacher_cdf_pinned,t.cdf.data(),n*sizeof(float));}
    smb_cuda_check(cudaMemcpy(fpt_teacher_cdf,fpt_teacher_cdf_pinned,n*sizeof(float),cudaMemcpyHostToDevice));
}
void puf_bind_stream(cudaStream_t stream) {fpt_stream=stream;}
void puf_reset(Env* envs) {
    fpt_reset_kernel<<<(fpt_count+127)/128,128,0,fpt_stream>>>(envs,fpt_states,fpt_bank_device,fpt_eligible_device,
        (int)fpt_host->eligible.size(),fpt_table_device,fpt_world_ids,fpt_rewards,fpt_terminals,fpt_host->task,fpt_host->time,fpt_host->curriculum,fpt_host->game,fpt_host->practice,fpt_host->teacher.device,fpt_count);
    smb_cuda_check(cudaGetLastError());
    fpt_observe_kernel<<<fpt_count,128,0,fpt_stream>>>(envs,fpt_states,fpt_worlds_device,fpt_observations,fpt_count);smb_cuda_check(cudaGetLastError());
}
void puf_step(Env* envs) {
    fpt_engine.step(fpt_states,fpt_worlds_device,fpt_world_ids,fpt_actions,fpt_count,fpt_stream);
    fpt_after_kernel<<<(fpt_count+127)/128,128,0,fpt_stream>>>(envs,fpt_states,fpt_bank_device,fpt_eligible_device,
        (int)fpt_host->eligible.size(),fpt_table_device,fpt_world_ids,fpt_rewards,fpt_terminals,fpt_host->task,fpt_host->time,fpt_host->curriculum,fpt_host->game,fpt_host->practice,fpt_host->teacher.device,fpt_count);
    smb_cuda_check(cudaGetLastError());
    fpt_observe_kernel<<<fpt_count,128,0,fpt_stream>>>(envs,fpt_states,fpt_worlds_device,fpt_observations,fpt_count);smb_cuda_check(cudaGetLastError());
    if(fpt_host->teacher.enabled)fpt_teacher_after_step();
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
    if(fpt_teacher_counters) {
        smb_cuda_check(cudaFree(fpt_teacher_cdf));smb_cuda_check(cudaFree(fpt_teacher_offsets));smb_cuda_check(cudaFree(fpt_teacher_area));
        smb_cuda_check(cudaFree(fpt_teacher_section));smb_cuda_check(cudaFree(fpt_teacher_counters));
        smb_cuda_check(cudaFreeHost(fpt_teacher_cdf_pinned));smb_cuda_check(cudaFreeHost(fpt_teacher_counters_pinned));
        fpt_teacher_cdf=fpt_teacher_cdf_pinned=nullptr;fpt_teacher_offsets=fpt_teacher_area=fpt_teacher_counters=fpt_teacher_counters_pinned=nullptr;
        fpt_teacher_section=nullptr;
        if(fpt_teacher_pair_from){smb_cuda_check(cudaFree(fpt_teacher_pair_from));smb_cuda_check(cudaFree(fpt_teacher_pair_to));}
        fpt_teacher_pair_from=fpt_teacher_pair_to=nullptr;
        if(fpt_teacher_loop_key){smb_cuda_check(cudaFree(fpt_teacher_loop_key));smb_cuda_check(cudaFree(fpt_teacher_loop_x));}
        fpt_teacher_loop_key=nullptr;fpt_teacher_loop_x=nullptr;
    }
    fpt_teacher_capture_steps.clear();fpt_teacher_capture_id=0;fpt_teacher_direct_steps=0;
    fpt_states=nullptr;fpt_bank_device=nullptr;fpt_eligible_device=nullptr;fpt_worlds_device=nullptr;fpt_world_ids=nullptr;fpt_table_device=nullptr;
    fpt_stream=0;fpt_count=0;fpt_device_envs=nullptr;fpt_host.reset();
}
static void fpt_gpu_save(const char* checkpoint,Ini*) {
    if(!fpt_host)return;
    smb_cuda_check(cudaStreamSynchronize(fpt_stream));std::vector<Env> shells(fpt_count);
    smb_cuda_check(cudaMemcpy(shells.data(),fpt_device_envs,shells.size()*sizeof(Env),cudaMemcpyDeviceToHost));
    std::vector<FptCurriculumProgress> progress;for(const auto& env:shells)if(env.is_fpg)progress.push_back(env.curriculum);
    fpt_write_progress(checkpoint,fpt_host->bank->header.payload_hash,fpt_host->curriculum,progress);
    if(fpt_host->teacher.enabled) {
        std::vector<TeacherGroup> groups;{std::lock_guard<std::mutex> guard(fpt_host->teacher.lock);groups=fpt_host->teacher.groups;}
        smb_write_teacher(checkpoint,fpt_host->practice_bank_hash,groups);return;
    }
    std::vector<SmbPracticeProgress> practice;for(const auto& env:shells)if(env.is_practice)practice.push_back(env.practice);
    smb_write_practice(checkpoint,fpt_host->practice_bank_hash,practice);
}
__global__ void fpt_resume_kernel(Env* envs,SmbLogic* states,const FptCurriculumProgress* saved,
        const SmbBankEntry* bank,const uint32_t* eligible,int eligible_count,const FpgTimeEntry* table,
        uint32_t* worlds,float* rewards,float* terminals,SmbTaskConfig task,FpgTimeConfig time,FptCurriculumConfig curriculum,SmbGameConfig game,SmbPracticeConfig practice,int count) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;if(i>=count)return;
    if(!envs[i].is_fpg)return;
    envs[i].curriculum=saved[i];fpt_reset_state(&envs[i],&states[i],&task,&time,bank,eligible,eligible_count,table,&curriculum,&game,&practice);
    fpt_require_valid(&states[i],i);worlds[i]=envs[i].episode.world;rewards[i]=terminals[i]=0;
}
static void fpt_gpu_load_curriculum(const char* checkpoint,Ini* ini) {
    if(!fpt_host||!fpt_host->curriculum.enabled||!fpt_integer(smb_curriculum_options(ini),"curriculum_resume",1,0,1))return;
    auto saved=fpt_read_progress(checkpoint);if(saved.progress.empty())return;fpt_check_resume(saved,fpt_host->bank->header.payload_hash);
    std::vector<FptCurriculumProgress> progress(fpt_count);size_t actor=0;
    for(int i=0;i<fpt_count;i++)if(smb_fpg_actor(i,fpt_count,&fpt_host->game))progress[i]=fpt_restore_progress(saved,actor++,fpt_host->curriculum);
    FptCurriculumProgress* device;smb_cuda_check(cudaMalloc(&device,progress.size()*sizeof(FptCurriculumProgress)));
    smb_cuda_check(cudaMemcpyAsync(device,progress.data(),progress.size()*sizeof(FptCurriculumProgress),cudaMemcpyHostToDevice,fpt_stream));
    fpt_resume_kernel<<<(fpt_count+127)/128,128,0,fpt_stream>>>(fpt_device_envs,fpt_states,device,fpt_bank_device,fpt_eligible_device,
        (int)fpt_host->eligible.size(),fpt_table_device,fpt_world_ids,fpt_rewards,fpt_terminals,fpt_host->task,fpt_host->time,fpt_host->curriculum,fpt_host->game,fpt_host->practice,fpt_count);
    smb_cuda_check(cudaGetLastError());fpt_observe_kernel<<<fpt_count,128,0,fpt_stream>>>(fpt_device_envs,fpt_states,fpt_worlds_device,fpt_observations,fpt_count,true);
    smb_cuda_check(cudaGetLastError());smb_cuda_check(cudaStreamSynchronize(fpt_stream));smb_cuda_check(cudaFree(device));
    fprintf(stderr,"[mario_sim/FPG] restored curriculum progress for %zu FPG agents\n",actor);
}
__global__ void smb_resume_practice_kernel(Env* envs,SmbLogic* states,const SmbPracticeProgress* progress,
        const SmbBankEntry* bank,uint32_t* worlds,float* rewards,float* terminals,
        SmbGameConfig game,SmbPracticeConfig practice,int count) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;if(i>=count||!envs[i].is_practice)return;
    envs[i].practice=progress[i];envs[i].observation_history={};
    smb_practice_reset(&states[i],&envs[i].episode,&envs[i].game,&envs[i].practice,&practice,&game,bank);
    fpt_require_valid(&states[i],i);worlds[i]=envs[i].episode.world;rewards[i]=terminals[i]=0;
}
static void fpt_gpu_load(const char* checkpoint,Ini* ini) {
    fpt_gpu_load_curriculum(checkpoint,ini);
    if(!fpt_host||fpt_host->practice.fraction<=0||!fpt_integer(&smb_game_options,"practice_resume",1,0,1))return;
    if(fpt_host->teacher.enabled) {
        auto saved=smb_read_teacher(checkpoint,fpt_host->practice_bank_hash);if(saved.empty())return;
        smb_cuda_check(cudaStreamSynchronize(fpt_stream));size_t matched;
        {std::lock_guard<std::mutex> guard(fpt_host->teacher.lock);matched=smb_teacher_restore(fpt_host->teacher,saved);}
        fpt_teacher_refresh_now();
        fprintf(stderr,"[mario_sim/practice] restored teacher statistics for %zu of %zu groups\n",matched,fpt_host->teacher.groups.size());
        return;
    }
    auto saved=smb_read_practice(checkpoint,fpt_host->practice_bank_hash);if(saved.empty())return;
    std::vector<SmbPracticeProgress> progress(fpt_count);size_t actor=0;
    for(int i=0;i<fpt_count;i++)if(smb_practice_actor(i,fpt_count,&fpt_host->game,&fpt_host->practice))
        progress[i]=saved[actor++%saved.size()];
    SmbPracticeProgress* device;smb_cuda_check(cudaMalloc(&device,progress.size()*sizeof(progress[0])));
    smb_cuda_check(cudaMemcpyAsync(device,progress.data(),progress.size()*sizeof(progress[0]),cudaMemcpyHostToDevice,fpt_stream));
    smb_resume_practice_kernel<<<(fpt_count+127)/128,128,0,fpt_stream>>>(fpt_device_envs,fpt_states,device,fpt_bank_device,
        fpt_world_ids,fpt_rewards,fpt_terminals,fpt_host->game,fpt_host->practice,fpt_count);
    smb_cuda_check(cudaGetLastError());
    fpt_observe_kernel<<<fpt_count,128,0,fpt_stream>>>(fpt_device_envs,fpt_states,fpt_worlds_device,fpt_observations,fpt_count);
    smb_cuda_check(cudaGetLastError());smb_cuda_check(cudaStreamSynchronize(fpt_stream));smb_cuda_check(cudaFree(device));
}
#define PUF_CHECKPOINT_HOOK(checkpoint,ini) fpt_gpu_save(checkpoint,ini)
#define PUF_LOAD_HOOK(checkpoint,ini) fpt_gpu_load(checkpoint,ini)
