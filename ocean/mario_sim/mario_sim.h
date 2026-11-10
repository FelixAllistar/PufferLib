#pragma once
typedef float obs_t;
#include "pufferenv.h"
#include "table.h"
#include "pipe_start.h"
#include "curriculum.h"
#include "curriculum_state.h"
#include "logic_cpu.h"
#include "view.h"
#include "observation.h"
#include "game.h"
#include "practice.h"
#include "practice_state.h"
#include <memory>
#include <map>
#include <mutex>
#include <shared_mutex>

#define OBS_SIZE FPT_OBS
#define NUM_ATNS 1
#define ACT_SIZES {64}
struct Log {
    float perf,score,episode_return,episode_length,n;
    float successes,deaths,timeouts,normal_flags,generated,world_variant;
    float time_score,success_frames,target_frames,table_hit;
    float curriculum_level,curriculum_frames,curriculum_start_frames;
    float frontier_episodes,frontier_successes,curriculum_promotions;
    float fpg_episodes,game_episodes,game_clears,game_wins,game_deaths,game_timeouts;
    float game_progress_pixels,game_checkpoints,game_checkpoint_reward;
    float fpg_mastery,curriculum_demotions,game_flag_contacts,game_level_exits;
    float band_episodes[FPT_CURRICULUM_LEVELS],band_successes[FPT_CURRICULUM_LEVELS];
    float practice_episodes,practice_successes,practice_deaths,practice_timeouts,practice_promotions,practice_demotions;
    float practice_stage_episodes[32],practice_stage_successes[32];
    float game_maze_loops;
};
struct Env {
    Log log;Agent agents[1];int tag,boundary_reached,num_agents;uint32_t rng;
    SmbEpisode episode;SmbLogic* state;float target;int table_hit;
    FptCurriculumProgress curriculum;
    int is_fpg;SmbGameProgress game;
    int is_practice;SmbPracticeProgress practice;
    FptObservationHistory observation_history;
    // Curriculum teacher attempt record of a practice actor (POD; kept outside SmbPracticeProgress,
    // whose size the .practice sidecar checks).
    int teacher_group,teacher_pending,teacher_frames,teacher_x0;uint32_t teacher_area;
    // game.deaths at the last tracked frame (0 at every game and practice reset): an area regained after an
    // untracked gap is a route transition only if no life was lost in between.
    int teacher_track_deaths;
    // On-route loop-backs since tracking was last regained, and 1 + the landing x of a passed one (0 = none).
    int teacher_route_loops,teacher_loop_land;
};
// Host side of the practice curriculum teacher. All EMA/decay/ranking runs here at a refresh; the
// environment code only samples from `cdf` and adds to the integer counters.
struct SmbTeacherHost {
    int enabled=0,refresh_steps=256,group_width=256,success_distance=512,attempt_frames=2400;
    TeacherParams params;
    std::vector<TeacherGroup> groups;std::vector<float> cdf;
    std::vector<uint32_t> offsets,area;std::vector<int32_t> section;
    // Counter layout: selected[G] attempts[G] wins[G] death_weights[G] events[4]. `counters` is the live
    // CPU-path storage; `previous` is the host mirror at the last refresh (deltas are taken against it).
    std::vector<uint32_t> counters,previous;
    // Route table from the practice bank's SMB_BANK_ROUTE entries (smb_route_pairs).
    std::vector<uint32_t> pair_from,pair_to;
    // Loop table from the SMB_BANK_ROUTE_LOOP entries: area key and x.
    std::vector<uint32_t> loop_key;std::vector<int32_t> loop_x;
    uint64_t agent_steps=0,since_refresh=0,refreshes=0,death_events=0,cpu_calls=0,loop_events=0,wrong_exit_events=0,route_loops_passed=0;
    std::mutex lock;             // groups, cdf and step counts (CUDA host callbacks, logging, checkpoints)
    std::shared_mutex cpu_cdf;   // CPU path: practice resets read the CDF while a refresh rewrites it
    SmbTeacherDevice device{};
};
struct FpgTimeHost {
    std::unique_ptr<SmbBank> bank;
    SmbTaskConfig task;FpgTimeConfig time;
    FptCurriculumConfig curriculum;
    std::vector<uint32_t> eligible;
    std::vector<FpgTimeEntry> table;
    std::string module;
    SmbGameConfig game;std::vector<SmbBankEntry> runtime_bank;
    SmbPracticeConfig practice;uint64_t practice_bank_hash=0;
    SmbTeacherHost teacher;
};
static std::unique_ptr<FpgTimeHost> fpt_host;
static int fpt_references;
static bool fpt_evaluating;
static int fpt_eval_level=-1;
static uint64_t fpt_eval_bank_hash;
static Dict smb_fpg_options={},smb_game_options={};
static bool smb_configured;
static float smb_fpg_fraction=1;
static int smb_actor_count=1;
static Dict* smb_section(Ini* ini,const char* name) {
    for(int i=0;i<ini->num_sections;i++)if(!strcmp(ini->sections[i].name,name))return &ini->sections[i];
    return nullptr;
}
static Dict* smb_curriculum_options(Ini* ini) {
    Dict* d=smb_section(ini,"fpg");
    return d?d:puf_ini_section(ini,"env",0);
}
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
static double fpt_real(Dict* d,const char* key,double fallback,double low,double high) {
    DictItem* item=dict_find(d,key);double n=item?item->value:fallback;
    if(!isfinite(n)||n<low||n>high)throw std::runtime_error(std::string("invalid FPG timing option: ")+key);
    return n;
}
static const char* fpt_path(Dict* d,const char* key,const char* fallback) {
    DictItem* item=dict_find(d,key);return item&&item->str?item->str:fallback;
}
static void fpt_configure(Ini* ini,const char* mode,const char* checkpoint) {
    fpt_evaluating=!strcmp(mode,"eval");fpt_eval_level=-1;fpt_eval_bank_hash=0;
    Dict* env_options=puf_ini_section(ini,"env",0);
    const char* kind=fpt_path(env_options,"mode","game");
    if(strcmp(kind,"game")&&strcmp(kind,"fpg")&&strcmp(kind,"mixed"))throw std::runtime_error("env.mode must be game, fpg or mixed");
    if(smb_configured){dict_clear(&smb_fpg_options);dict_clear(&smb_game_options);}
    dict_copy(&smb_fpg_options,env_options);
    Dict* section=smb_section(ini,"fpg");
    if(section)for(int i=0;i<section->size;i++) {
        auto* v=&section->items[i];if(v->str)puf_ini_set(&smb_fpg_options,v->key,v->str);else dict_set(&smb_fpg_options,v->key,v->value);
    }
    Dict* game=smb_section(ini,"game");if(game)dict_copy(&smb_game_options,game);else smb_game_options={};
    smb_fpg_fraction=!strcmp(kind,"fpg")?1:!strcmp(kind,"game")?0:fpt_number(&smb_fpg_options,"fraction",.25f,0,1);
    Dict* vec=smb_section(ini,"vec");smb_actor_count=vec?fpt_integer(vec,"total_agents",1,1,10000000):1;
    Dict* base=smb_section(ini,"base");
    if(fpt_evaluating&&base&&dict_find(base,"eval_agents")&&dict_get(base,"eval_agents")>0)
        smb_actor_count=fpt_integer(base,"eval_agents",1,1,10000000);
    smb_configured=true;
    Dict* options=&smb_fpg_options;
    if(!fpt_evaluating||!smb_fpg_fraction||!fpt_integer(options,"curriculum",0,0,1))return;
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
static void fpt_setup_fpg(Dict* options) {
    if(fpt_host)return;auto setup=std::make_unique<FpgTimeHost>();
    auto& curriculum=setup->curriculum;curriculum.enabled=fpt_integer(options,"curriculum",0,0,1);
    curriculum.adaptive=fpt_integer(options,"curriculum_adaptive",1,0,1);
    curriculum.window=fpt_integer(options,"curriculum_window",16,1,4096);
    curriculum.confirmations=fpt_integer(options,"curriculum_confirmations",2,1,128);
    curriculum.threshold=fpt_number(options,"curriculum_threshold",0.8f,0.01f,1);
    curriculum.replay_fraction=fpt_number(options,"curriculum_replay",0.2f,0,0.9f);
    curriculum.retention=fpt_integer(options,"curriculum_retention",1,0,1);
    curriculum.demotion_confirmations=fpt_integer(options,"curriculum_demotion_confirmations",2,1,128);
    curriculum.demotion_threshold=fpt_number(options,"curriculum_demotion_threshold",.25f,0,1);
    if(curriculum.demotion_threshold>=curriculum.threshold)throw std::runtime_error("curriculum demotion threshold must be below promotion threshold");
    int start=fpt_integer(options,"curriculum_start_frames",3,3,425);curriculum.initial_level=-1;
    for(int i=0;i<FPT_CURRICULUM_LEVELS;i++)if(fpt_curriculum_depths[i]==start)curriculum.initial_level=i;
    if(curriculum.initial_level<0)throw std::runtime_error("curriculum_start_frames must be a curriculum depth (3,4,5,6,8,12,16,24,32,48,64,96,128,192,256,320,425)");
    if(fpt_evaluating){curriculum.adaptive=0;curriculum.replay_fraction=0;if(fpt_eval_level>=0)curriculum.initial_level=fpt_eval_level;}
    setup->bank=std::make_unique<SmbBank>(fpt_path(options,"reset_bank",curriculum.enabled?
        "build/mario_sim/fpg/curriculum/bank.bin":"build/mario_sim/fpg/pipe/bank.bin"));
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
        "build/mario_sim/fpg/curriculum_targets.bin":"build/mario_sim/fpg/pipe_targets.bin");
    setup->table.resize(setup->bank->entries.size());
    if(strcmp(path,"None")&&strcmp(path,"none")) {
        FpgTimeTable table(path,*setup->bank,
            fpt_path(options,"engine_cpu_archive","build/mario_sim/runtime/logic_cpu.a"),setup->module.c_str());
        setup->table=std::move(table.entries);
    }
    if(curriculum.enabled)setup->eligible=fpt_curriculum_indices(*setup->bank,setup->table,&curriculum);
    size_t covered=0;for(unsigned i:setup->eligible)covered+=setup->table[i].best_frames!=0;
    fprintf(stderr,"[mario_sim/FPG] %s_templates=%zu worlds=%d knobs=%u timeout=%d cached=%zu fallback=%zu bonus=%.3g (estimated times)\n",
        curriculum.enabled?"curriculum":"pipe_exit",setup->eligible.size(),c.world_count,c.knobs,c.max_frames,covered,setup->eligible.size()-covered,t.bonus);
    if(curriculum.enabled)fprintf(stderr,"[mario_sim/FPG] curriculum start=%d reference_frames adaptive=%d window=%d confirmations=%d threshold=%.3g replay=%.3g\n",
        fpt_curriculum_depths[curriculum.initial_level],curriculum.adaptive,curriculum.window,curriculum.confirmations,curriculum.threshold,curriculum.replay_fraction);
    fpt_host=std::move(setup);
}
// Fold the counter deltas since the previous refresh into the groups and rebuild the sampling CDF.
static void smb_teacher_fold(SmbTeacherHost& t,const uint32_t* now_counters,uint64_t now) {
    size_t n=t.groups.size();const uint32_t* prev=t.previous.data();
    for(size_t g=0;g<n;g++) {
        uint32_t selected=now_counters[g]-prev[g],attempts=now_counters[n+g]-prev[n+g];
        uint32_t wins=now_counters[2*n+g]-prev[2*n+g],weights=now_counters[3*n+g]-prev[3*n+g];
        auto& group=t.groups[g];
        if(attempts)teacher_record(group,attempts,wins,t.params);
        if(weights)teacher_add_mass(group,weights,now,t.params);
        if(selected)group.last_selected=now;
    }
    t.death_events+=now_counters[4*n]-prev[4*n];
    t.loop_events+=now_counters[4*n+1]-prev[4*n+1];t.wrong_exit_events+=now_counters[4*n+2]-prev[4*n+2];
    t.route_loops_passed+=now_counters[4*n+3]-prev[4*n+3];
    std::copy(now_counters,now_counters+4*n+4,t.previous.begin());
    teacher_priorities(t.groups,now,t.params);teacher_cdf(t.groups,&t.cdf);t.refreshes++;
}
// Restore saved group statistics by key (the step counter restarts, so stamps return to zero).
static size_t smb_teacher_restore(SmbTeacherHost& t,const std::vector<TeacherGroup>& saved) {
    std::map<uint64_t,const TeacherGroup*> by_key;for(const auto& g:saved)by_key[g.key]=&g;size_t matched=0;
    for(auto& g:t.groups) {
        auto it=by_key.find(g.key);if(it==by_key.end())continue;
        const auto& s=*it->second;g.fast=s.fast;g.slow=s.slow;g.attempts=s.attempts;g.wins=s.wins;g.deaths=s.deaths;
        g.death_stamp=g.last_selected=0;matched++;
    }
    return matched;
}
// Route table: entries flagged SMB_BANK_ROUTE sorted by (stage, source_frame); within a stage, two consecutive
// entries with different area keys a != b add the pair (a, b) once. area = stage<<16 | area data pointer.
static void smb_route_pairs(const std::vector<SmbBankEntry>& entries,std::vector<uint32_t>* from,std::vector<uint32_t>* to) {
    std::vector<const SmbBankEntry*> route;
    for(const auto& e:entries)if(e.flags&SMB_BANK_ROUTE)route.push_back(&e);
    std::stable_sort(route.begin(),route.end(),[](const SmbBankEntry* a,const SmbBankEntry* b) {
        return a->stage!=b->stage?a->stage<b->stage:a->source_frame<b->source_frame;});
    from->clear();to->clear();
    auto area=[](const SmbBankEntry* e){const auto* m=e->scene.initial.ram;
        return ((m[0x75f]*4u+m[0x75c])<<16)|(m[0xe7]+256u*m[0xe8]);};
    for(size_t i=1;i<route.size();i++) {
        if(route[i]->stage!=route[i-1]->stage)continue;
        uint32_t a=area(route[i-1]),b=area(route[i]);if(a==b)continue;
        bool seen=false;for(size_t k=0;k<from->size();k++)seen|=(*from)[k]==a&&(*to)[k]==b;
        if(!seen){from->push_back(a);to->push_back(b);}
    }
}
static void smb_setup_teacher(FpgTimeHost& h,Dict* d,const SmbBank& practice) {
    auto& t=h.teacher;
    t.group_width=fpt_integer(d,"practice_group_width",256,32,2048);
    t.success_distance=fpt_integer(d,"practice_success_distance",512,1,65536);
    t.attempt_frames=fpt_integer(d,"practice_attempt_frames",2400,1,108000);
    t.refresh_steps=fpt_integer(d,"practice_refresh_steps",256,1,100000000);
    auto& p=t.params;
    p.upstream_groups=fpt_integer(d,"practice_upstream_groups",6,0,1024);
    p.death_half_life=fpt_real(d,"practice_death_half_life",20e6,1,1e18);
    p.lp_fast=fpt_real(d,"practice_lp_fast",.05,0,1);p.lp_slow=fpt_real(d,"practice_lp_slow",.005,0,1);
    p.lp_min_attempts=fpt_integer(d,"practice_lp_min_attempts",5,1,1000000);
    p.weight_deaths=fpt_real(d,"practice_weight_deaths",.5,0,1000);
    p.weight_lp=fpt_real(d,"practice_weight_lp",.35,0,1000);
    p.weight_stale=fpt_real(d,"practice_weight_stale",.15,0,1000);
    p.rank_power=fpt_real(d,"practice_rank_power",1,0,16);
    p.floor=fpt_real(d,"practice_floor",.1,0,1);
    teacher_validate(p);
    // Group = one section of one loaded area: area = stage<<16 | area data pointer (the frontier key of
    // smb_game_track_progress), section = x / group_width. Entries are copied group-major.
    std::map<uint64_t,std::vector<const SmbBankEntry*>> by_key;
    for(const auto& e:practice.entries) {
        const auto* m=e.scene.initial.ram;
        uint32_t stage=m[0x75f]*4u+m[0x75c],data=m[0xe7]+256u*m[0xe8];
        int section=fpt_x(m,0)/t.group_width;
        uint32_t area=(stage<<16)|data;
        by_key[((uint64_t)area<<16)|(uint64_t)section].push_back(&e);
    }
    if(by_key.empty()||by_key.size()>4096)throw std::runtime_error("practice teacher needs 1..4096 groups");
    for(const auto& [key,list]:by_key) {
        const auto* m=list[0]->scene.initial.ram;
        TeacherGroup g;g.key=key;g.area=(uint32_t)(key>>16);g.level=m[0x75f]*4+m[0x75c];
        g.section=fpt_x(m,0)/t.group_width;g.eligible=1;
        t.offsets.push_back((uint32_t)h.runtime_bank.size());t.area.push_back(g.area);t.section.push_back(g.section);
        for(const auto* e:list)h.runtime_bank.push_back(*e);
        t.groups.push_back(g);
    }
    t.offsets.push_back((uint32_t)h.runtime_bank.size());
    size_t n=t.groups.size();t.counters.assign(4*n+4,0);t.previous=t.counters;
    smb_route_pairs(practice.entries,&t.pair_from,&t.pair_to);
    if(t.pair_from.size()>256)throw std::runtime_error("practice route table needs at most 256 area pairs");
    t.loop_key.clear();t.loop_x.clear();
    for(const auto& e:practice.entries)if((e.flags&SMB_BANK_ROUTE)&&(e.flags&SMB_BANK_ROUTE_LOOP)) {
        const auto* m=e.scene.initial.ram;
        t.loop_key.push_back(((m[0x75f]*4u+m[0x75c])<<16)|(m[0xe7]+256u*m[0xe8]));t.loop_x.push_back(fpt_x(m,0));
    }
    if(t.loop_key.size()>16)throw std::runtime_error("practice route table needs at most 16 on-route loop-backs");
    // First refresh before the first reset: no signal yet, so the uniform floor ties everything.
    teacher_priorities(t.groups,0,t.params);teacher_cdf(t.groups,&t.cdf);
    auto& v=t.device;v.enabled=1;v.groups=(int)n;v.group_width=t.group_width;v.success_distance=t.success_distance;
    v.attempt_frames=t.attempt_frames;v.upstream=p.upstream_groups;
    v.cdf=t.cdf.data();v.offsets=t.offsets.data();v.area=t.area.data();v.section=t.section.data();
    uint32_t* c=t.counters.data();v.selected=c;v.attempts=c+n;v.wins=c+2*n;v.death_weights=c+3*n;v.events=c+4*n;
    v.pairs=(int)t.pair_from.size();v.pair_from=t.pair_from.data();v.pair_to=t.pair_to.data();
    v.loops=(int)t.loop_key.size();v.loop_key=t.loop_key.data();v.loop_x=t.loop_x.data();
    t.enabled=1;
    fprintf(stderr,"[mario_sim/practice] teacher groups=%zu group_width=%d success_distance=%d attempt_frames=%d upstream=%d refresh_steps=%d\n",
        n,t.group_width,t.success_distance,t.attempt_frames,p.upstream_groups,t.refresh_steps);
    fprintf(stderr,"[mario_sim/practice] route pairs=%zu loop_terminal=%d:",t.pair_from.size(),h.practice.loop_terminal);
    for(size_t i=0;i<t.pair_from.size();i++)fprintf(stderr," %d-%d:%04x->%04x",(int)(t.pair_from[i]>>16)/4+1,(int)(t.pair_from[i]>>16)%4+1,
        t.pair_from[i]&0xffff,t.pair_to[i]&0xffff);
    fprintf(stderr,"; on-route loops=%zu:",t.loop_key.size());
    for(size_t i=0;i<t.loop_key.size();i++)fprintf(stderr," %d-%d:%04x x>=%d",(int)(t.loop_key[i]>>16)/4+1,(int)(t.loop_key[i]>>16)%4+1,
        t.loop_key[i]&0xffff,t.loop_x[i]);
    fprintf(stderr,"\n");
}
static void fpt_setup(Dict* options) {
    if(fpt_host)return;
    Dict* fpg=smb_configured?&smb_fpg_options:options;
    if(smb_fpg_fraction>0)fpt_setup_fpg(fpg);
    else {
        fpt_host=std::make_unique<FpgTimeHost>();
        fpt_host->task.seed=fpt_integer(options,"seed",73,1,2147483647);
        fpt_host->module=fpt_path(options,"engine_module","build/mario_sim/runtime/cuda_replay.cubin");
    }
    auto& h=*fpt_host;h.game.fpg_fraction=smb_fpg_fraction;
    if(smb_fpg_fraction<1) {
        Dict* d=&smb_game_options;
        SmbBank bank(fpt_path(d,"reset_bank","build/mario_sim/runtime/generated/bank.bin"));
        if(h.bank&&memcmp(h.bank->worlds.data(),bank.worlds.data(),SMB_PRG))throw std::runtime_error("game and FPG banks must share original ROM data");
        if(!h.bank)h.bank=std::make_unique<SmbBank>(std::move(bank));
        const auto& roots=bank.entries.empty()?h.bank->entries:bank.entries;
        h.runtime_bank=h.bank->entries;
        bool found[32]={};
        for(const auto& entry:roots)if((entry.flags&SMB_BANK_STAGE_START)&&!(entry.flags&SMB_BANK_CONSTRUCTED)&&!found[entry.stage]) {
            h.game.roots[entry.stage]=(uint32_t)h.runtime_bank.size();h.runtime_bank.push_back(entry);found[entry.stage]=true;
        }
        for(int stage=0;stage<32;stage++)if(!found[stage])throw std::runtime_error("game bank must cover all 32 natural stage starts");
        h.game.max_frames=fpt_integer(d,"max_frames",108000,1,1000000);
        h.game.fixed_stage=fpt_integer(d,"fixed_stage",-1,-1,31);
        h.game.terminate_on_clear=fpt_integer(d,"terminate_on_clear",0,0,1);
        h.game.reward_on_exit=fpt_integer(d,"reward_on_exit",1,0,1);
        h.game.clear_reward=fpt_number(d,"clear_reward",1,0,10);
        h.game.time_bonus=fpt_number(d,"time_bonus",.1f,0,10);
        h.game.time_target=fpt_integer(d,"time_target_frames",1800,1,1000000);
        h.game.death_penalty=fpt_number(d,"death_penalty",0,0,10);
        h.game.checkpoint_distance=fpt_integer(d,"checkpoint_distance",128,1,3400);
        h.game.checkpoint_reward=fpt_number(d,"checkpoint_reward",.025f,0,10);
        h.practice.fraction=fpt_evaluating?0:fpt_number(d,"practice_fraction",0,0,1);
        if(h.practice.fraction+smb_fpg_fraction>1)throw std::runtime_error("game practice and FPG frame shares exceed one");
        h.practice.max_frames=fpt_integer(d,"practice_max_frames",6000,1,108000);
        h.practice.replay=fpt_number(d,"practice_replay",.2f,0,.9f);
        h.practice.loop_terminal=fpt_integer(d,"practice_loop_terminal",0,0,1);
        if(h.practice.fraction>0) {
            SmbBank practice(fpt_path(d,"practice_bank","build/mario_sim/practice/bank.bin"));
            if(practice.header.worlds!=1||memcmp(practice.worlds.data(),h.bank->worlds.data(),SMB_PRG))
                throw std::runtime_error("practice bank must contain the original ROM world");
            h.practice_bank_hash=practice.header.payload_hash;
            for(const auto& e:practice.entries) {
                const auto* m=e.scene.initial.ram;
                if(!(e.flags&SMB_BANK_PRACTICE)||(e.flags&SMB_BANK_CONSTRUCTED)||e.scene.mask
                    ||e.stage!=m[0x75f]*4u+m[0x75c]||m[0xe]!=8
                    ||((e.flags>>SMB_PRACTICE_BAND_SHIFT)&255)>=SMB_PRACTICE_BANDS)
                    throw std::runtime_error("invalid natural encounter reset");
            }
            size_t copied=0;bool teacher=fpt_integer(d,"practice_teacher",0,0,1);
            if(teacher) {
                for(int stage=0;stage<32;stage++) {
                    for(const auto& e:practice.entries)if((int)e.stage==stage){h.practice.stages[h.practice.stage_count++]=stage;break;}
                }
                size_t first=h.runtime_bank.size();smb_setup_teacher(h,d,practice);copied=h.runtime_bank.size()-first;
            } else for(int stage=0;stage<32;stage++) {
                bool present=false;
                for(const auto& e:practice.entries)if((int)e.stage==stage)present=true;
                if(!present)continue;
                h.practice.stages[h.practice.stage_count++]=stage;
                for(int band=0;band<SMB_PRACTICE_BANDS;band++) {
                    h.practice.offsets[stage][band]=(uint32_t)h.runtime_bank.size();
                    for(const auto& e:practice.entries)if((int)e.stage==stage
                        &&(int)(e.flags>>SMB_PRACTICE_BAND_SHIFT)==band){h.runtime_bank.push_back(e);copied++;}
                    if(h.runtime_bank.size()==h.practice.offsets[stage][band])
                        throw std::runtime_error("practice stage has an empty approach band");
                }
                h.practice.offsets[stage][SMB_PRACTICE_BANDS]=(uint32_t)h.runtime_bank.size();
            }
            if(copied!=practice.entries.size()||!h.practice.stage_count)throw std::runtime_error("empty or incomplete practice bank");
            fprintf(stderr,"[mario_sim/practice] frame_share=%.3f stages=%d states=%zu max_frames=%d\n",
                h.practice.fraction,h.practice.stage_count,copied,h.practice.max_frames);
        }
    } else h.runtime_bank=h.bank->entries;
    if(h.table.empty())h.table.resize(h.runtime_bank.size());
    if(h.eligible.empty())h.eligible.push_back(0);
    fprintf(stderr,"[mario_sim] game=%.1f%% practice=%.1f%% FPG=%.1f%%; original stages=%s; game limit=%d\n",
        100*(1-smb_fpg_fraction-h.practice.fraction),100*h.practice.fraction,100*smb_fpg_fraction,
        h.game.fixed_stage<0?"all 32":"fixed",h.game.max_frames);
}
// Teacher practice reset: same scene/episode/progress initialisation as smb_practice_reset, then the attempt record.
SMB_HD int smb_teacher_practice_reset(Env* env,SmbLogic* state,const SmbTeacherDevice* t,const SmbGameConfig* game,const SmbBankEntry* bank) {
    int g=smb_teacher_choose(&env->episode,t),stage=(int)bank[env->episode.scene].stage;
    env->practice.stage=stage;env->practice.sampled=0;
    int fault=smb_practice_load(state,&env->episode,&env->game,stage,game,bank);
    const auto* m=state->ram;
    env->teacher_group=g;env->teacher_pending=1;env->teacher_frames=0;env->teacher_x0=fpt_x(m,0);
    env->teacher_area=((uint32_t)(m[0x75f]*4+m[0x75c])<<16)|(uint32_t)(m[0xe7]+256*m[0xe8]);
    return fault;
}
SMB_HD int fpt_reset_state(Env* env,SmbLogic* state,const SmbTaskConfig* task,const FpgTimeConfig* time,
        const SmbBankEntry* bank,const uint32_t* eligible,int eligible_count,const FpgTimeEntry* table,
        const FptCurriculumConfig* curriculum=nullptr,const SmbGameConfig* game=nullptr,const SmbPracticeConfig* practice=nullptr,
        const SmbTeacherDevice* teacher=nullptr) {
    if(!game)env->is_fpg=1;
    if(game&&!env->is_fpg) {
        env->observation_history={};env->target=0;env->table_hit=0;env->teacher_track_deaths=0;
        env->teacher_route_loops=0;env->teacher_loop_land=0;
        if(env->is_practice&&teacher&&teacher->enabled)return smb_teacher_practice_reset(env,state,teacher,game,bank);
        if(env->is_practice)return smb_practice_reset(state,&env->episode,&env->game,&env->practice,practice,game,bank);
        return smb_game_reset(state,&env->episode,&env->game,game,bank);
    }
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
    auto* l=&env->log;l->fpg_episodes++;const auto* e=&env->episode;float won=e->status==SMB_EPISODE_SUCCESS;
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
        int sampled=p->sampled_level;
        l->band_episodes[sampled]++;l->band_successes[sampled]+=won;
        l->fpg_mastery+=won*(curriculum->depths[sampled]-curriculum->depths[0])/
            (float)(curriculum->depths[FPT_CURRICULUM_LEVELS-1]-curriculum->depths[0]);
        int changed=fpt_curriculum_result(p,curriculum,(int)won);
        l->curriculum_promotions+=changed>0;l->curriculum_demotions+=changed<0;
    }
}
SMB_HD float smb_after_frame(Env* env,SmbLogic* state,const SmbTaskConfig* task,
        const FpgTimeConfig* time,const FptCurriculumConfig* curriculum,const FpgTimeEntry* table,const SmbGameConfig* game,
        const SmbPracticeConfig* practice=nullptr,const SmbTeacherDevice* teacher=nullptr) {
    SmbGameConfig encounter;
    if(env->is_practice) {
        encounter=*game;encounter.max_frames=practice->max_frames;encounter.terminate_on_clear=1;encounter.reward_on_exit=1;game=&encounter;
    }
    bool teach=teacher&&teacher->enabled;
    // Last tracked gameplay area and uncapped position before this frame; they persist through the death animation.
    int deaths_before=env->game.deaths,was_tracking=env->game.tracking,x0=env->game.raw_x,loops_before=env->game.maze_loops;
    uint32_t k0=env->game.last_key;
    float reward=env->is_fpg?fpg_time_after_frame(state,&env->episode,task,env->target,time):
        smb_game_after_frame(state,&env->episode,&env->game,game);
    // Route events (teacher on, game and practice actors). loop: the ROM's maze loop-back in this area.
    // An area regained after an untracked gap in the same stage without a lost life is a transition: forward
    // along a route pair, or backward (a same-area jump back of 256+ px, or entering a route area off the pairs).
    // The first loop-back the reference route itself takes (8-4's first pipe) is passed: neutral, counted in
    // events[3]. Failures right after a passed loop are credited 1,024 px further (credit_x), at the real place.
    int loop=0,forward=0,backward=0,passed=0,credit_x=x0;
    if(teach&&!env->is_fpg) {
        uint32_t k1=env->game.last_key;int x1=env->game.raw_x;
        loop=env->game.maze_loops>loops_before;
        if(!was_tracking&&env->game.tracking){env->teacher_route_loops=0;env->teacher_loop_land=0;}
        if(loop&&smb_route_loop(teacher,k0,x0)) {
            if(env->teacher_route_loops==0){passed=1;loop=0;env->teacher_loop_land=x1+1;}   // +1 so 0 means "none"
            env->teacher_route_loops++;
        }
        credit_x=(env->teacher_loop_land&&!passed&&x0<env->teacher_loop_land-1+256)?x0+1024:x0;
        int regained=!was_tracking&&env->game.tracking&&k0!=0&&(k0>>16)==(k1>>16)&&env->game.deaths==env->teacher_track_deaths;
        forward=regained&&k1!=k0&&smb_route_pair(teacher,k0,k1);
        backward=regained&&((k1==k0&&x0-x1>=256)
            ||(k1!=k0&&smb_route_known(teacher,k1)&&smb_route_known(teacher,k0)&&!smb_route_pair(teacher,k0,k1)));
        if(env->game.tracking)env->teacher_track_deaths=env->game.deaths;
        if(passed)smb_teacher_add(&teacher->events[3],1);
    }
    if(env->is_practice&&!env->episode.status&&(state->ram[0xe]==11||env->game.deaths))env->episode.status=SMB_EPISODE_DEAD;
    if(teach&&env->is_practice&&practice->loop_terminal&&!env->episode.status&&(loop||backward))env->episode.status=SMB_EPISODE_DEAD;
    if(teach&&env->is_practice&&env->teacher_pending) {
        // Attempt outcome: an exit, success_distance px of progress in the starting area, or a forward route
        // transition is a win; death, timeout, the frame cap, a loop-back or a wrong exit is a loss.
        const auto* m=state->ram;int status=env->episode.status;env->teacher_frames++;
        uint32_t area=((uint32_t)(m[0x75f]*4+m[0x75c])<<16)|(uint32_t)(m[0xe7]+256*m[0xe8]);
        int win=status==SMB_EPISODE_SUCCESS||env->game.level_exits>0
            ||(m[0xe]==8&&area==env->teacher_area&&smb_game_x(m)>=env->teacher_x0+teacher->success_distance)||forward;
        int loss=status==SMB_EPISODE_DEAD||status==SMB_EPISODE_TIMEOUT||env->teacher_frames>=teacher->attempt_frames||loop||backward;
        if(win||loss) {
            int g=env->teacher_group;
            smb_teacher_add(&teacher->attempts[g],1);smb_teacher_add(&teacher->wins[g],(uint32_t)win);env->teacher_pending=0;
        }
    } else if(teach&&!env->is_practice&&!env->is_fpg&&((env->game.deaths>deaths_before&&k0)||loop||backward)) {
        // Natural-play failure (life loss, loop-back or wrong exit; credited once per frame): the groups at and
        // upstream of the last tracked position (credit_x) get integer kernel weights U+1-d.
        int section=(credit_x>0?credit_x:0)/teacher->group_width;
        for(int k=0;k<teacher->groups;k++) {
            int d=section-teacher->section[k];
            if(teacher->area[k]==k0&&d>=0&&d<=teacher->upstream)
                smb_teacher_add(&teacher->death_weights[k],(uint32_t)(teacher->upstream+1-d));
        }
        smb_teacher_add(&teacher->events[0],1);
        if(loop)smb_teacher_add(&teacher->events[1],1);
        if(backward)smb_teacher_add(&teacher->events[2],1);
    }
    if(env->episode.status!=SMB_EPISODE_ACTIVE) {
        if(env->is_fpg)fpt_log_episode(env,task,time,curriculum,table);
        else if(env->is_practice) {
            auto* l=&env->log;auto* e=&env->episode;int won=e->status==SMB_EPISODE_SUCCESS;
            l->n++;l->practice_episodes++;l->practice_successes+=won;l->practice_deaths+=e->status==SMB_EPISODE_DEAD;
            l->practice_timeouts+=e->status==SMB_EPISODE_TIMEOUT;
            l->practice_stage_episodes[env->practice.stage]++;l->practice_stage_successes[env->practice.stage]+=won;
            int changed=teach?0:smb_practice_result(&env->practice,won);l->practice_promotions+=changed>0;l->practice_demotions+=changed<0;
            l->perf+=won;l->score+=won;l->episode_return+=e->episode_return;l->episode_length+=e->frames;
        } else {
            auto* l=&env->log;const auto* e=&env->episode;
            l->n++;l->game_episodes++;l->game_clears+=env->game.clears;l->game_deaths+=env->game.deaths;
            l->game_wins+=e->status==SMB_EPISODE_SUCCESS&&!game->terminate_on_clear;
            l->game_timeouts+=e->status==SMB_EPISODE_TIMEOUT;
            l->game_progress_pixels+=env->game.progress_pixels;l->game_checkpoints+=env->game.checkpoints;
            l->game_checkpoint_reward+=env->game.checkpoints*game->checkpoint_reward;
            l->game_flag_contacts+=env->game.flag_contacts;l->game_level_exits+=env->game.level_exits;
            l->game_maze_loops+=env->game.maze_loops;
            l->perf+=env->game.clears>0;l->score+=env->game.clears;l->episode_return+=e->episode_return;l->episode_length+=e->frames;
        }
    }
    return reward;
}
// Teacher state from the host singleton (as of the last refresh).
static void fpt_teacher_log(Dict* out) {
    if(!fpt_host||!fpt_host->teacher.enabled)return;
    auto& t=fpt_host->teacher;std::lock_guard<std::mutex> guard(t.lock);
    uint64_t attempts=0,wins=0;double progress=0,share[32]={};int eligible=0;size_t top=0;
    for(size_t g=0;g<t.groups.size();g++) {
        const auto& x=t.groups[g];attempts+=x.attempts;wins+=x.wins;
        if(x.eligible){progress+=teacher_progress(x,t.params);eligible++;}
        if(x.level>=0&&x.level<32)share[x.level]+=x.prob;
        if(x.prob>t.groups[top].prob)top=g;
    }
    dict_set(out,"teacher_groups",(double)t.groups.size());dict_set(out,"teacher_entropy",teacher_entropy(t.groups));
    dict_set(out,"teacher_attempts",(double)attempts);dict_set(out,"teacher_attempt_win_rate",attempts?(double)wins/attempts:0);
    dict_set(out,"teacher_death_events",(double)t.death_events);dict_set(out,"teacher_mean_progress",eligible?progress/eligible:0);
    dict_set(out,"teacher_top_stage",t.groups[top].level);dict_set(out,"teacher_top_section",t.groups[top].section);
    dict_set(out,"teacher_top_prob",t.groups[top].prob);
    dict_set(out,"teacher_loop_events",(double)t.loop_events);dict_set(out,"teacher_wrong_exit_events",(double)t.wrong_exit_events);
    dict_set(out,"teacher_route_pairs",(double)t.pair_from.size());
    dict_set(out,"teacher_route_loops_passed",(double)t.route_loops_passed);
    for(int i=0;i<fpt_host->practice.stage_count;i++) {
        int stage=fpt_host->practice.stages[i];char key[64];
        snprintf(key,sizeof(key),"teacher_%d_%d_share",stage/4+1,stage%4+1);dict_set(out,key,share[stage]);
    }
}
void puf_log(Log* l,Dict* out) {
    dict_set(out,"perf",l->perf);dict_set(out,"score",l->score);
    // The shared headless evaluator expects a draw rate. Mario timeouts and
    // deaths are failures, so this single-player task has no draw outcome.
    dict_set(out,"draw_rate",0);
    dict_set(out,"episode_return",l->episode_return);dict_set(out,"episode_length",l->episode_length);
    dict_set(out,"fpg_perf",l->fpg_episodes?l->successes/l->fpg_episodes:0);
    dict_set(out,"game_clears",l->game_episodes?l->game_clears/l->game_episodes:0);
    dict_set(out,"game_wins",l->game_episodes?l->game_wins/l->game_episodes:0);
    dict_set(out,"game_deaths",l->game_episodes?l->game_deaths/l->game_episodes:0);
    dict_set(out,"game_timeouts",l->game_episodes?l->game_timeouts/l->game_episodes:0);
    dict_set(out,"game_flag_contacts",l->game_episodes?l->game_flag_contacts/l->game_episodes:0);
    dict_set(out,"game_level_exits",l->game_episodes?l->game_level_exits/l->game_episodes:0);
    dict_set(out,"game_maze_loops",l->game_episodes?l->game_maze_loops/l->game_episodes:0);
    float game_pixels=l->game_episodes?l->game_progress_pixels/l->game_episodes:0;
    float curriculum_frames=l->fpg_episodes?l->curriculum_frames/l->fpg_episodes:0;
    dict_set(out,"game_progress_pixels",game_pixels);
    // Cheap training-progress objective. FPG credit requires current success
    // at the sampled difficulty; historical frontier alone earns no credit.
    // Keep game progress uncapped so clearing more areas can still improve it.
    float fpg_progress=fmaxf(0.0f,(curriculum_frames-fpt_curriculum_depths[0])/
        (fpt_curriculum_depths[FPT_CURRICULUM_LEVELS-1]-fpt_curriculum_depths[0]));
    float mastery=l->fpg_episodes?l->fpg_mastery/l->fpg_episodes:0;
    dict_set(out,"fpg_mastery",mastery);
    dict_set(out,"sweep_score",0.75f*game_pixels/3400.0f+0.25f*mastery);
    dict_set(out,"legacy_sweep_score",0.75f*game_pixels/3400.0f+0.25f*fpg_progress);
    dict_set(out,"game_checkpoints",l->game_episodes?l->game_checkpoints/l->game_episodes:0);
    dict_set(out,"game_checkpoint_reward",l->game_episodes?l->game_checkpoint_reward/l->game_episodes:0);
#define FPT_LOG(field) dict_set(out,#field,l->fpg_episodes?l->field/l->fpg_episodes:0)
    FPT_LOG(successes);FPT_LOG(deaths);FPT_LOG(timeouts);FPT_LOG(normal_flags);FPT_LOG(generated);FPT_LOG(world_variant);
    FPT_LOG(target_frames);FPT_LOG(table_hit);
    FPT_LOG(curriculum_level);FPT_LOG(curriculum_frames);FPT_LOG(curriculum_start_frames);FPT_LOG(curriculum_promotions);
    FPT_LOG(curriculum_demotions);
#undef FPT_LOG
    dict_set(out,"success_frames",l->successes?l->success_frames/l->successes:0);
    dict_set(out,"success_time_score",l->successes?l->time_score/l->successes:0);
    dict_set(out,"frontier_perf",l->frontier_episodes?l->frontier_successes/l->frontier_episodes:0);
    dict_set(out,"frontier_fraction",l->fpg_episodes?l->frontier_episodes/l->fpg_episodes:0);
    dict_set(out,"practice_perf",l->practice_episodes?l->practice_successes/l->practice_episodes:0);
    dict_set(out,"practice_deaths",l->practice_episodes?l->practice_deaths/l->practice_episodes:0);
    dict_set(out,"practice_timeouts",l->practice_episodes?l->practice_timeouts/l->practice_episodes:0);
    dict_set(out,"practice_promotions",l->practice_episodes?l->practice_promotions/l->practice_episodes:0);
    dict_set(out,"practice_demotions",l->practice_episodes?l->practice_demotions/l->practice_episodes:0);
    for(int stage=0;stage<32;stage++) {
        char key[64];snprintf(key,sizeof(key),"practice_%d_%d_perf",stage/4+1,stage%4+1);
        dict_set(out,key,l->practice_stage_episodes[stage]?l->practice_stage_successes[stage]/l->practice_stage_episodes[stage]:0);
    }
    for(int i=0;i<FPT_CURRICULUM_LEVELS;i++) {
        char key[64];snprintf(key,sizeof(key),"fpg_band_%d_perf",fpt_curriculum_depths[i]);
        dict_set(out,key,l->band_episodes[i]?l->band_successes[i]/l->band_episodes[i]:0);
        snprintf(key,sizeof(key),"fpg_band_%d_fraction",fpt_curriculum_depths[i]);
        dict_set(out,key,l->fpg_episodes?l->band_episodes[i]/l->fpg_episodes:0);
    }
    fpt_teacher_log(out);
}
#ifndef PUFFER_GPU_ENV
static std::vector<Env*> fpt_cpu_envs;
void puf_init(Env* env,Dict* options) {
    fpt_setup(options);fpt_references++;env->num_agents=1;
    env->is_fpg=smb_fpg_actor(env->rng%(unsigned)smb_actor_count,smb_actor_count,&fpt_host->game);
    env->is_practice=smb_practice_actor(env->rng%(unsigned)smb_actor_count,smb_actor_count,&fpt_host->game,&fpt_host->practice);
    env->episode.rng=smb_seed(fpt_host->task.seed,env->rng);env->state=new SmbLogic{};
    fpt_cpu_envs.push_back(env);
}
void puf_reset(Env* env) {
    auto& t=fpt_host->teacher;std::shared_lock<std::shared_mutex> guard;
    if(t.enabled&&env->is_practice)guard=std::shared_lock<std::shared_mutex>(t.cpu_cdf);
    if(fpt_reset_state(env,env->state,&fpt_host->task,&fpt_host->time,fpt_host->runtime_bank.data(),
        fpt_host->eligible.data(),(int)fpt_host->eligible.size(),fpt_host->table.data(),&fpt_host->curriculum,&fpt_host->game,&fpt_host->practice,&t.device))throw std::runtime_error("FPG timing reset fault");
    if(env->agents[0].observations)fpt_observe(env->state,
        fpt_host->bank->worlds.data()+(size_t)env->episode.world*SMB_PRG,
        &env->observation_history,env->agents[0].observations);
}
void puf_step(Env* env) {
    float raw=env->agents[0].actions[0];int action=(int)raw;
    if(!(raw>=0&&raw<64)||raw!=(float)action)throw std::runtime_error("invalid FPG timing action");
    auto* world=fpt_host->bank->worlds.data()+(size_t)env->episode.world*SMB_PRG;
    if(smb_native_frame(env->state,world,smb_action_buttons(action)))throw std::runtime_error("FPG timing simulation fault");
    float reward=smb_after_frame(env,env->state,&fpt_host->task,&fpt_host->time,&fpt_host->curriculum,fpt_host->table.data(),&fpt_host->game,&fpt_host->practice,&fpt_host->teacher.device);
    if(fpt_host->teacher.enabled)__atomic_fetch_add(&fpt_host->teacher.cpu_calls,1,__ATOMIC_RELAXED);
    int done=env->episode.status!=SMB_EPISODE_ACTIVE;
    if(done){env->boundary_reached=1;puf_reset(env);}
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
// CPU refresh (caller holds teacher.lock): snapshot the atomic counters (wins before attempts before
// selections, so no delta can show more wins than attempts), then fold them while no reset reads the CDF.
static void fpt_teacher_cpu_refresh(SmbTeacherHost& t) {
    size_t n=t.groups.size();std::vector<uint32_t> now(4*n+4);const uint32_t* c=t.counters.data();
    for(size_t k=0;k<n;k++)now[2*n+k]=__atomic_load_n(c+2*n+k,__ATOMIC_SEQ_CST);
    for(size_t k=0;k<n;k++)now[n+k]=__atomic_load_n(c+n+k,__ATOMIC_SEQ_CST);
    for(size_t k=0;k<n;k++)now[k]=__atomic_load_n(c+k,__ATOMIC_SEQ_CST);
    for(size_t k=0;k<n;k++)now[3*n+k]=__atomic_load_n(c+3*n+k,__ATOMIC_SEQ_CST);
    for(size_t k=4*n;k<4*n+4;k++)now[k]=__atomic_load_n(c+k,__ATOMIC_SEQ_CST);
    std::unique_lock<std::shared_mutex> write(t.cpu_cdf);smb_teacher_fold(t,now.data(),t.agent_steps);
}
// Forced refresh (tests, resume).
static void fpt_teacher_refresh_now() {
    if(!fpt_host||!fpt_host->teacher.enabled)return;
    std::lock_guard<std::mutex> guard(fpt_host->teacher.lock);fpt_teacher_cpu_refresh(fpt_host->teacher);
}
// Cadence: every practice_refresh_steps puf_step calls per CPU env, checked after each stepped buffer.
static void fpt_cpu_post_step(Env*,int) {
    if(!fpt_host||!fpt_host->teacher.enabled)return;
    auto& t=fpt_host->teacher;uint64_t calls=__atomic_exchange_n(&t.cpu_calls,(uint64_t)0,__ATOMIC_RELAXED);
    std::lock_guard<std::mutex> guard(t.lock);t.agent_steps+=calls;t.since_refresh+=calls;
    if(t.since_refresh>=(uint64_t)t.refresh_steps*std::max<size_t>(1,fpt_cpu_envs.size())){t.since_refresh=0;fpt_teacher_cpu_refresh(t);}
}
static void fpt_cpu_save(const char* checkpoint,Ini*) {
    if(!fpt_host)return;
    std::vector<FptCurriculumProgress> progress;for(auto* env:fpt_cpu_envs)if(env->is_fpg)progress.push_back(env->curriculum);
    fpt_write_progress(checkpoint,fpt_host->bank->header.payload_hash,fpt_host->curriculum,progress);
    if(fpt_host->teacher.enabled) {
        std::vector<TeacherGroup> groups;{std::lock_guard<std::mutex> guard(fpt_host->teacher.lock);groups=fpt_host->teacher.groups;}
        smb_write_teacher(checkpoint,fpt_host->practice_bank_hash,groups);return;
    }
    std::vector<SmbPracticeProgress> practice;for(auto* env:fpt_cpu_envs)if(env->is_practice)practice.push_back(env->practice);
    smb_write_practice(checkpoint,fpt_host->practice_bank_hash,practice);
}
static void fpt_cpu_load(const char* checkpoint,Ini* ini) {
    if(!fpt_host)return;
    if(fpt_host->curriculum.enabled&&fpt_integer(smb_curriculum_options(ini),"curriculum_resume",1,0,1)) {
    auto saved=fpt_read_progress(checkpoint);fpt_check_resume(saved,fpt_host->bank->header.payload_hash);
    if(!saved.progress.empty()) {
    size_t actor=0;for(auto* env:fpt_cpu_envs)if(env->is_fpg){env->curriculum=fpt_restore_progress(saved,actor++,fpt_host->curriculum);puf_reset(env);}
    }}
    if(fpt_host->teacher.enabled&&fpt_integer(&smb_game_options,"practice_resume",1,0,1)) {
        auto saved=smb_read_teacher(checkpoint,fpt_host->practice_bank_hash);
        if(!saved.empty()) {
            std::lock_guard<std::mutex> guard(fpt_host->teacher.lock);size_t matched=smb_teacher_restore(fpt_host->teacher,saved);
            fpt_teacher_cpu_refresh(fpt_host->teacher);
            fprintf(stderr,"[mario_sim/practice] restored teacher statistics for %zu of %zu groups\n",matched,fpt_host->teacher.groups.size());
        }
    } else if(fpt_host->practice.fraction>0&&fpt_integer(&smb_game_options,"practice_resume",1,0,1)) {
        auto saved=smb_read_practice(checkpoint,fpt_host->practice_bank_hash);
        if(!saved.empty()){size_t actor=0;for(auto* env:fpt_cpu_envs)if(env->is_practice){env->practice=saved[actor++%saved.size()];puf_reset(env);}}
    }
}
#define PUF_CHECKPOINT_HOOK(checkpoint,ini) fpt_cpu_save(checkpoint,ini)
#define PUF_LOAD_HOOK(checkpoint,ini) fpt_cpu_load(checkpoint,ini)
#define PUF_CPU_POST_STEP(envs,count) fpt_cpu_post_step(envs,count)
#endif

#include "training_config.h"
