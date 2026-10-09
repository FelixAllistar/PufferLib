#pragma once
typedef float obs_t;
#include "pufferenv.h"
#include "bank_io.h"
#include "logic_cpu.h"
#include <memory>

// Provisional lossless RAM interface. No prior encoder/training conclusion is
// carried over; every observation is the exact native RAM byte divided by 256.
#define OBS_SIZE SMB_DEBUG_OBS
#define NUM_ATNS 1
#define ACT_SIZES {64}
struct Log {
    float perf,score,episode_return,episode_length,n;
    float successes,deaths,timeouts,normal_flags,generated,world_variant;
};
struct Env {
    Log log;Agent agents[1];int tag,boundary_reached,num_agents;uint32_t rng;
    SmbEpisode episode;SmbLogic* state;
};
struct SmbHostSetup {
    std::unique_ptr<SmbBank> bank;
    SmbTaskConfig cfg;
    std::vector<uint32_t> eligible;
    const char* module;
};
static std::unique_ptr<SmbHostSetup> smb_host;
static int smb_host_references;
static int smb_option(Dict* d,const char* key,int fallback,int low,int high) {
    DictItem* item=dict_find(d,key);double value=item?item->value:fallback;
    if(!isfinite(value)||value!=floor(value)||value<low||value>high)
        throw std::runtime_error(std::string("invalid Mario option: ")+key);
    return (int)value;
}
static const char* smb_path_option(Dict* d,const char* key,const char* fallback) {
    DictItem* item=dict_find(d,key);return item&&item->str?item->str:fallback;
}
static void smb_host_setup(Dict* d) {
    if(smb_host)return;
    auto setup=std::make_unique<SmbHostSetup>();
    const char* path=smb_path_option(d,"reset_bank","build/mario_sim/runtime/generated/bank.bin");
    setup->bank=std::make_unique<SmbBank>(path);
    auto& c=setup->cfg;
    c.seed=(uint32_t)smb_option(d,"seed",73,1,2147483647);
    c.knobs=(unsigned)smb_option(d,"generation_knobs",SMB_GEN_CLOCK|SMB_GEN_FRACTIONS,0,SMB_GEN_ALL);
    c.max_frames=smb_option(d,"max_frames",1800,1,1000000);
    c.task=smb_option(d,"task",SMB_TASK_FPG,SMB_TASK_FREE,SMB_TASK_CLEAR);
    c.fixed_stage=smb_option(d,"fixed_stage",-1,-1,31);
    c.world_count=smb_option(d,"world_variants",(int)setup->bank->header.worlds,1,(int)setup->bank->header.worlds);
    setup->eligible=setup->bank->select(c);
    setup->module=smb_path_option(d,"engine_module","build/mario_sim/runtime/cuda_replay.cubin");
    fprintf(stderr,"[mario_sim] task=%d templates=%zu/%zu worlds=%d knobs=%u bank=%016llx\n",
            c.task,setup->eligible.size(),setup->bank->entries.size(),c.world_count,c.knobs,
            (unsigned long long)setup->bank->header.payload_hash);
    smb_host=std::move(setup);
}
SMB_HD void smb_log_episode(Log* log,const SmbEpisode* e,const SmbTaskConfig* cfg) {
    float won=e->status==SMB_EPISODE_SUCCESS;
    log->perf+=won;log->score+=won;log->episode_return+=e->episode_return;
    log->episode_length+=e->frames;log->n++;
    log->successes+=won;log->deaths+=e->status==SMB_EPISODE_DEAD;
    log->timeouts+=e->status==SMB_EPISODE_TIMEOUT;log->normal_flags+=e->status==SMB_EPISODE_NORMAL_FLAG;
    log->generated+=cfg->knobs!=0||e->world!=0;log->world_variant+=e->world;
}
void puf_log(Log* l,Dict* out) {
#define SMB_LOG(field) dict_set(out,#field,l->field)
    SMB_LOG(perf);SMB_LOG(score);SMB_LOG(episode_return);SMB_LOG(episode_length);
    SMB_LOG(successes);SMB_LOG(deaths);SMB_LOG(timeouts);SMB_LOG(normal_flags);SMB_LOG(generated);SMB_LOG(world_variant);
#undef SMB_LOG
}
#include "view.h"
#ifndef PUFFER_GPU_ENV
void puf_init(Env* env,Dict* kwargs) {
    smb_host_setup(kwargs);smb_host_references++;env->num_agents=1;
    env->episode.rng=smb_seed(smb_host->cfg.seed,env->rng);env->state=new SmbLogic{};
}
void puf_reset(Env* env) {
    int fault=smb_task_reset(env->state,&env->episode,&smb_host->cfg,smb_host->bank->entries.data(),
                             smb_host->eligible.data(),(int)smb_host->eligible.size());
    if(fault)throw std::runtime_error("Mario generated reset failed");
    if(env->agents[0].observations)smb_debug_observe(env->state,env->agents[0].observations);
}
void puf_step(Env* env) {
    float raw=env->agents[0].actions[0];int action=(int)raw;
    if(!(raw>=0&&raw<64)||raw!=(float)action)throw std::runtime_error("invalid Mario action");
    auto* data=smb_host->bank->worlds.data()+(size_t)env->episode.world*SMB_PRG;
    if(smb_native_frame(env->state,data,smb_action_buttons(action)))throw std::runtime_error("Mario simulation fault");
    float reward=smb_task_after_frame(env->state,&env->episode,&smb_host->cfg);
    int done=env->episode.status!=SMB_EPISODE_ACTIVE;
    if(done){smb_log_episode(&env->log,&env->episode,&smb_host->cfg);env->boundary_reached=1;puf_reset(env);}
    else smb_debug_observe(env->state,env->agents[0].observations);
    env->agents[0].rewards[0]=reward;env->agents[0].terminals[0]=(float)done;
}
void puf_render(Env* env) {
#ifndef SMB_HEADLESS
    smb_render_state(env->state,smb_host->bank->worlds.data());
#endif
}
void puf_close(Env* env) {
    if(env->state){delete env->state;env->state=nullptr;if(!--smb_host_references)smb_host.reset();}
}
#endif
