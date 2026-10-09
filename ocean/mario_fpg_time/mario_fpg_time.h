#pragma once
typedef float obs_t;
#include "pufferenv.h"
#include "table.h"
#include "pipe_start.h"
#include "curriculum.h"
#include "curriculum_state.h"
#include "../mario_sim/logic_cpu.h"
#include "../mario_sim/view.h"
#include "observation.h"
#include <memory>

#define OBS_SIZE FPT_OBS
#define NUM_ATNS 1
#define ACT_SIZES {64}
struct Log {
    float perf,score,episode_return,episode_length,n;
    float successes,deaths,timeouts,normal_flags,generated,world_variant;
    float time_score,success_frames,target_frames,table_hit;
    float curriculum_level,curriculum_frames,curriculum_start_frames;
    float frontier_episodes,frontier_successes,curriculum_promotions;
};
struct Env {
    Log log;Agent agents[1];int tag,boundary_reached,num_agents;uint32_t rng;
    SmbEpisode episode;SmbLogic* state;float target;int table_hit;
    FptCurriculumProgress curriculum;
    FptObservationHistory observation_history;
};
struct FpgTimeHost {
    std::unique_ptr<SmbBank> bank;
    SmbTaskConfig task;FpgTimeConfig time;
    FptCurriculumConfig curriculum;
    std::vector<uint32_t> eligible;
    std::vector<FpgTimeEntry> table;
    std::string module;
};
static std::unique_ptr<FpgTimeHost> fpt_host;
static int fpt_references;
static bool fpt_evaluating;
static int fpt_eval_level=-1;
static uint64_t fpt_eval_bank_hash;
static float fpt_number(Dict* d,const char* key,float fallback,float low,float high,bool integer=false) {
    DictItem* item=dict_find(d,key);double n=item?item->value:fallback;
    if(!isfinite(n)||n<low||n>high||(integer&&floor(n)!=n))
        throw std::runtime_error(std::string("invalid FPG timing option: ")+key);
    return (float)n;
}
static int fpt_integer(Dict* d,const char* key,int fallback,int low,int high) {
    DictItem* item=dict_find(d,key);double n=item?item->value:fallback;
    if(!isfinite(n)||floor(n)!=n||n<low||n>high)
        throw std::runtime_error(std::string("invalid FPG timing option: ")+key);
    return (int)n;
}
static const char* fpt_path(Dict* d,const char* key,const char* fallback) {
    DictItem* item=dict_find(d,key);return item&&item->str?item->str:fallback;
}
static void fpt_configure(Ini* ini,const char* mode,const char* checkpoint) {
    fpt_evaluating=!strcmp(mode,"eval");fpt_eval_level=-1;fpt_eval_bank_hash=0;
    Dict* options=puf_ini_section(ini,"env",0);
    if(!fpt_evaluating||!fpt_integer(options,"curriculum",0,0,1))return;
    if(checkpoint&&fpt_integer(options,"curriculum_resume",1,0,1)) {
        auto saved=fpt_read_progress(checkpoint);
        if(!saved.progress.empty()){fpt_eval_level=fpt_median_level(saved.progress);fpt_eval_bank_hash=saved.header.bank_hash;}
    }
    int frames=fpt_integer(options,"curriculum_eval_frames",0,0,425);
    if(frames) {
        fpt_eval_level=-1;for(int i=0;i<FPT_CURRICULUM_LEVELS;i++)if(fpt_curriculum_depths[i]==frames)fpt_eval_level=i;
        if(fpt_eval_level<0)throw std::runtime_error("curriculum_eval_frames must be a curriculum depth");
    }
}
// These hooks expand after the shared trainer's checkpoint resolver definition.
// Curriculum logic and its persistence remain entirely in this environment.
#define PUF_CONFIGURE(ini,mode) do { \
    char fpt_checkpoint_buffer[4096]; \
    const char* fpt_checkpoint=puf_checkpoint_path_key(ini,"load_model_path",fpt_checkpoint_buffer,sizeof(fpt_checkpoint_buffer)); \
    fpt_configure(ini,mode,fpt_checkpoint); \
} while(0)
static void fpt_setup(Dict* options) {
    if(fpt_host)return;auto setup=std::make_unique<FpgTimeHost>();
    auto& curriculum=setup->curriculum;curriculum.enabled=fpt_integer(options,"curriculum",0,0,1);
    curriculum.adaptive=fpt_integer(options,"curriculum_adaptive",1,0,1);
    curriculum.window=fpt_integer(options,"curriculum_window",16,1,4096);
    curriculum.confirmations=fpt_integer(options,"curriculum_confirmations",2,1,128);
    curriculum.threshold=fpt_number(options,"curriculum_threshold",0.8f,0.01f,1);
    curriculum.replay_fraction=fpt_number(options,"curriculum_replay",0.2f,0,0.9f);
    int start=fpt_integer(options,"curriculum_start_frames",3,3,425);curriculum.initial_level=-1;
    for(int i=0;i<FPT_CURRICULUM_LEVELS;i++)if(fpt_curriculum_depths[i]==start)curriculum.initial_level=i;
    if(curriculum.initial_level<0)throw std::runtime_error("curriculum_start_frames must be a curriculum depth (3,4,5,6,8,12,16,24,32,48,64,96,128,192,256,320,425)");
    if(fpt_evaluating){curriculum.adaptive=0;curriculum.replay_fraction=0;if(fpt_eval_level>=0)curriculum.initial_level=fpt_eval_level;}
    setup->bank=std::make_unique<SmbBank>(fpt_path(options,"reset_bank",curriculum.enabled?
        "build/mario_fpg_time/curriculum/bank.bin":"build/mario_fpg_time/pipe/bank.bin"));
    if(curriculum.enabled)fpt_require_natural_bank(*setup->bank);else fpt_require_pipe_bank(*setup->bank);
    if(curriculum.enabled&&fpt_eval_bank_hash&&fpt_eval_bank_hash!=setup->bank->header.payload_hash)
        throw std::runtime_error("evaluation curriculum checkpoint bank mismatch; use curriculum_resume=0 to choose a new evaluation bank");
    auto& c=setup->task;c.seed=(uint32_t)fpt_integer(options,"seed",73,1,2147483647);
    c.knobs=(uint32_t)fpt_integer(options,"generation_knobs",0,0,0);
    c.max_frames=fpt_integer(options,"max_frames",1800,1,1000000);c.task=SMB_TASK_FPG;
    c.fixed_stage=fpt_integer(options,"fixed_stage",0,0,0);
    c.world_count=fpt_integer(options,"world_variants",(int)setup->bank->header.worlds,1,(int)setup->bank->header.worlds);
    setup->eligible=setup->bank->select(c);
    auto& t=setup->time;
    t.bonus=fpt_number(options,"time_bonus",0.5f,0,10);
    t.scale=fpt_number(options,"time_target_scale",1,0.01f,100);
    t.slack=fpt_number(options,"time_slack_frames",8,0,10000);
    t.power=fpt_number(options,"time_power",1,0.05f,8);
    t.run_speed=fpt_number(options,"time_fallback_speed",2.5f,0.01f,100);
    t.setup_frames=fpt_number(options,"time_fallback_setup",24,0,10000);
    t.goal_x=FPT_PIPE_POLE_X;
    setup->module=fpt_path(options,"engine_module","build/mario_sim/runtime/cuda_replay.cubin");
    const char* path=fpt_path(options,"time_table",curriculum.enabled?
        "build/mario_fpg_time/curriculum_targets.bin":"build/mario_fpg_time/pipe_targets.bin");
    setup->table.resize(setup->bank->entries.size());
    if(strcmp(path,"None")&&strcmp(path,"none")) {
        FpgTimeTable table(path,*setup->bank,
            fpt_path(options,"engine_cpu_archive","build/mario_sim/runtime/logic_cpu.a"),setup->module.c_str());
        setup->table=std::move(table.entries);
    }
    if(curriculum.enabled)setup->eligible=fpt_curriculum_indices(*setup->bank,setup->table,&curriculum);
    size_t covered=0;for(unsigned i:setup->eligible)covered+=setup->table[i].best_frames!=0;
    fprintf(stderr,"[mario_fpg_time] %s_templates=%zu worlds=%d knobs=%u timeout=%d cached=%zu fallback=%zu bonus=%.3g (estimated times)\n",
        curriculum.enabled?"curriculum":"pipe_exit",setup->eligible.size(),c.world_count,c.knobs,c.max_frames,covered,setup->eligible.size()-covered,t.bonus);
    if(curriculum.enabled)fprintf(stderr,"[mario_fpg_time] curriculum start=%d reference_frames adaptive=%d window=%d confirmations=%d threshold=%.3g replay=%.3g\n",
        fpt_curriculum_depths[curriculum.initial_level],curriculum.adaptive,curriculum.window,curriculum.confirmations,curriculum.threshold,curriculum.replay_fraction);
    fpt_host=std::move(setup);
}
SMB_HD int fpt_reset_state(Env* env,SmbLogic* state,const SmbTaskConfig* task,const FpgTimeConfig* time,
        const SmbBankEntry* bank,const uint32_t* eligible,int eligible_count,const FpgTimeEntry* table,
        const FptCurriculumConfig* curriculum=nullptr) {
    if(curriculum&&curriculum->enabled) {
        int level=fpt_curriculum_choose(&env->curriculum,curriculum,&env->episode.rng);
        eligible+=curriculum->offsets[level];eligible_count=curriculum->offsets[level+1]-curriculum->offsets[level];
    }
    int fault=smb_task_reset(state,&env->episode,task,bank,eligible,eligible_count);
    env->observation_history={};
    env->table_hit=table&&table[env->episode.scene].best_frames!=0;
    env->target=fpg_time_target(state,env->episode.scene,table,time);return fault;
}
SMB_HD void fpt_log_episode(Env* env,const SmbTaskConfig* task,const FpgTimeConfig* time,
        const FptCurriculumConfig* curriculum=nullptr,const FpgTimeEntry* table=nullptr) {
    auto* l=&env->log;const auto* e=&env->episode;float won=e->status==SMB_EPISODE_SUCCESS;
    l->perf+=won;l->score+=won;l->episode_return+=e->episode_return;l->episode_length+=e->frames;l->n++;
    l->successes+=won;l->deaths+=e->status==SMB_EPISODE_DEAD;l->timeouts+=e->status==SMB_EPISODE_TIMEOUT;
    l->normal_flags+=e->status==SMB_EPISODE_NORMAL_FLAG;l->generated+=task->knobs!=0||e->world!=0;l->world_variant+=e->world;
    if(won){l->time_score+=fpg_time_score(e->frames,env->target,time);l->success_frames+=e->frames;}
    l->target_frames+=env->target;l->table_hit+=env->table_hit;
    if(curriculum&&curriculum->enabled) {
        auto* p=&env->curriculum;int frontier=p->sampled_level==p->level;
        l->curriculum_level+=p->level;l->curriculum_frames+=curriculum->depths[p->level];
        l->curriculum_start_frames+=table?table[e->scene].best_frames:0;
        l->frontier_episodes+=frontier;l->frontier_successes+=frontier&&won;
        l->curriculum_promotions+=fpt_curriculum_result(p,curriculum,(int)won);
    }
}
void puf_log(Log* l,Dict* out) {
#define FPT_LOG(field) dict_set(out,#field,l->field)
    FPT_LOG(perf);FPT_LOG(score);FPT_LOG(episode_return);FPT_LOG(episode_length);
    FPT_LOG(successes);FPT_LOG(deaths);FPT_LOG(timeouts);FPT_LOG(normal_flags);FPT_LOG(generated);FPT_LOG(world_variant);
    FPT_LOG(target_frames);FPT_LOG(table_hit);
    FPT_LOG(curriculum_level);FPT_LOG(curriculum_frames);FPT_LOG(curriculum_start_frames);FPT_LOG(curriculum_promotions);
#undef FPT_LOG
    dict_set(out,"success_frames",l->successes?l->success_frames/l->successes:0);
    dict_set(out,"success_time_score",l->successes?l->time_score/l->successes:0);
    dict_set(out,"frontier_perf",l->frontier_episodes?l->frontier_successes/l->frontier_episodes:0);
    dict_set(out,"frontier_fraction",l->frontier_episodes);
}
#ifndef PUFFER_GPU_ENV
static std::vector<Env*> fpt_cpu_envs;
void puf_init(Env* env,Dict* options) {
    fpt_setup(options);fpt_references++;env->num_agents=1;
    env->episode.rng=smb_seed(fpt_host->task.seed,env->rng);env->state=new SmbLogic{};
    fpt_cpu_envs.push_back(env);
}
void puf_reset(Env* env) {
    if(fpt_reset_state(env,env->state,&fpt_host->task,&fpt_host->time,fpt_host->bank->entries.data(),
        fpt_host->eligible.data(),(int)fpt_host->eligible.size(),fpt_host->table.data(),&fpt_host->curriculum))throw std::runtime_error("FPG timing reset fault");
    if(env->agents[0].observations)fpt_observe(env->state,
        fpt_host->bank->worlds.data()+(size_t)env->episode.world*SMB_PRG,
        &env->observation_history,env->agents[0].observations);
}
void puf_step(Env* env) {
    float raw=env->agents[0].actions[0];int action=(int)raw;
    if(!(raw>=0&&raw<64)||raw!=(float)action)throw std::runtime_error("invalid FPG timing action");
    auto* world=fpt_host->bank->worlds.data()+(size_t)env->episode.world*SMB_PRG;
    if(smb_native_frame(env->state,world,smb_action_buttons(action)))throw std::runtime_error("FPG timing simulation fault");
    float reward=fpg_time_after_frame(env->state,&env->episode,&fpt_host->task,env->target,&fpt_host->time);
    int done=env->episode.status!=SMB_EPISODE_ACTIVE;
    if(done){fpt_log_episode(env,&fpt_host->task,&fpt_host->time,&fpt_host->curriculum,fpt_host->table.data());env->boundary_reached=1;puf_reset(env);}
    else fpt_observe(env->state,world,&env->observation_history,env->agents[0].observations);
    env->agents[0].rewards[0]=reward;env->agents[0].terminals[0]=(float)done;
}
void puf_render(Env* env) {
#ifndef SMB_HEADLESS
    smb_render_state(env->state,fpt_host->bank->worlds.data());
#endif
}
void puf_close(Env* env) {
    if(env->state){delete env->state;env->state=nullptr;fpt_cpu_envs.erase(std::remove(fpt_cpu_envs.begin(),fpt_cpu_envs.end(),env),fpt_cpu_envs.end());if(!--fpt_references)fpt_host.reset();}
}
static void fpt_cpu_save(const char* checkpoint,Ini*) {
    if(!fpt_host||!fpt_host->curriculum.enabled)return;
    std::vector<FptCurriculumProgress> progress;for(auto* env:fpt_cpu_envs)progress.push_back(env->curriculum);
    fpt_write_progress(checkpoint,fpt_host->bank->header.payload_hash,fpt_host->curriculum,progress);
}
static void fpt_cpu_load(const char* checkpoint,Ini* ini) {
    if(!fpt_host||!fpt_host->curriculum.enabled||!fpt_integer(puf_ini_section(ini,"env",0),"curriculum_resume",1,0,1))return;
    auto saved=fpt_read_progress(checkpoint);if(saved.progress.empty())return;fpt_check_resume(saved,fpt_host->bank->header.payload_hash);
    for(size_t i=0;i<fpt_cpu_envs.size();i++){auto* env=fpt_cpu_envs[i];env->curriculum=fpt_restore_progress(saved,i,fpt_host->curriculum);puf_reset(env);}
}
#define PUF_CHECKPOINT_HOOK(checkpoint,ini) fpt_cpu_save(checkpoint,ini)
#define PUF_LOAD_HOOK(checkpoint,ini) fpt_cpu_load(checkpoint,ini)
#endif
