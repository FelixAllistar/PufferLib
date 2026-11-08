#pragma once
// Exact ROM snapshots collected during ordinary play. Nes_State owns internal
// pointers and MUST NEVER be copied; snapshots are immutable shared objects.
#include <atomic>
#include <unordered_map>
// zlib's legacy Byte typedef conflicts with the native trainer's Byte enum.
#define Byte RetroZlibByte
#include <zlib.h>
#undef Byte
#include "nes_emu/abstract_file.h"
#include "curriculum_teacher.h"
#define RETRO_ARCHIVE_AVAILABLE 1
struct RetroArchiveLedger {
    RetroFrontier frontiers[256];
    unsigned int areas[256],rewarded,last_area;
    int frontier_count,area_count,pipe_phase;
};
struct RetroArchiveSnapshot {
    RetroStart start;
    RetroArchiveLedger ledger;
};
// A cell holds 1..archive_variants snapshots of one (area, x band, height, power, motion) key (2026-11-07; one
// snapshot per cell before). `group` indexes bank.groups: the curriculum teacher's section of the cell's area.
struct RetroArchiveCell {
    uint64_t key=0,visits=0,selected=0,failures=0,successes=0,last=0;
    int level=0;
    float depth=0;  // progress into its own level when captured (retro_archive_depth)
    int group=-1;
    std::vector<std::shared_ptr<RetroArchiveSnapshot>> states;
    // Read-only mirror of states.front() for the single-state diagnostic tools (retro.c --save-states, tas_states,
    // keypoint_search, keypoint_eval, seed_maze, pipe_probe, archive_frames). The environment never reads it; a cell a
    // tool builds with only `state` set is written as a one-state cell by retro_archive_file.
    std::shared_ptr<RetroArchiveSnapshot> state;
};
// sizeof(RetroArchiveCell) before cells held a vector (2026-11-07): the teacher-off capacity stays exactly as it was.
#define RETRO_ARCHIVE_V1_CELL_BYTES 72
#define RETRO_ARCHIVE_V1_MAGIC 0x524554524f415201ull
#define RETRO_ARCHIVE_V2_MAGIC 0x524554524f415202ull
#define RETRO_ARCHIVE_MAX_VARIANTS 64
// Curriculum teacher refresh period in global agent steps (decisions).
#define RETRO_TEACHER_REFRESH_STEPS 2048
struct RetroArchiveBank {
    std::mutex mutex;
    std::vector<RetroArchiveCell> cells;
    std::unordered_map<uint64_t,size_t> index;
    std::atomic<uint64_t> steps{0},mode_steps[3]{};
    uint64_t budget=1000000000,resets[3]{};
    size_t capacity=1;  // maximum number of stored snapshots (with one per cell when the teacher is off: cells)
    double resumed_phase=0,anneal=.8;
    // Curriculum teacher (env.archive_teacher, 2026-11-07; curriculum_teacher.h).
    std::vector<TeacherGroup> groups;           // never deleted; `eligible` recomputed before each refresh
    std::unordered_map<uint64_t,size_t> group_index;
    std::vector<float> cdf;                     // sampling distribution from the last refresh (empty = stale)
    uint64_t cdf_steps=0;
    int cdf_eligible=0;                         // eligible groups at the last refresh
    size_t snapshots=0;                         // stored snapshots over all cells
    TeacherParams teacher;
    bool teacher_enabled=false,death_chain=true;
    int group_width=256,variants=1,success_distance=512,attempt_decisions=600;
    int chain_interval=32,chain_depth=4,chain_skip=16;
    double full_game_share=.3;
    uint32_t spawn_levels=0xffffffffu;          // env.spawn_levels as a level bit mask (eligibility after a load)
    uint64_t death_events=0,chain_deposits=0,attempt_wins=0,attempt_losses=0;
};
static RetroArchiveBank& retro_archive_bank(){static RetroArchiveBank b;return b;}
// Summed frontier x of the snapshot's own level, across that level's areas.
static float retro_archive_depth(const RetroArchiveSnapshot& s,int level) {
    float depth=0;
    for(int i=0;i<s.ledger.frontier_count;i++)
        if((int)(s.ledger.frontiers[i].key>>16)==level) depth+=retro_progress_x(s.ledger.frontiers[i].x);
    return depth;
}
// One entry of the death chain: a recent snapshot of a natural-play episode, deposited if a failure follows.
struct RetroChainEntry {
    std::shared_ptr<RetroArchiveSnapshot> state;
    uint64_t key=0;
    int level=0,x=0,decision=0;
    unsigned area=0;
};
struct RetroArchiveEpisode {
    std::shared_ptr<RetroArchiveSnapshot> state;
    double start=.6,end=.1,anneal=.8,priority=.5,level_focus=0,depth=0,backward=0,lane=0;
    int interval=128,width=128,mode=0,feedback_step=0,feedback_progress=0;
    bool practice=false;  // env.archive_practice (retro.h)
    double practice_focus=0;  // env.archive_practice_focus
    uint64_t feedback_key=0;
    bool feedback=false;
    // Teacher attempt record (mode 2): one outcome per archive start.
    bool attempt_pending=false;
    int attempt_group=-1,attempt_decisions=0,attempt_x0=0;
    unsigned attempt_area=0;
    // Death chain ring (modes 0 and 1).
    std::vector<RetroChainEntry> chain;
    int chain_next=0,chain_count=0;
    // Death onset (modes 0 and 1): the life counter drops ~50 decisions after the hit, so a failure is referenced
    // to the first decision Mario was dying and to his position then. A maze loop is referenced to its pre-rewind x.
    int dying_decision=-1,dying_x=0,last_failure_decision=-1,last_failure_x=0;
    unsigned dying_area=0xffffffffu;
};
static double retro_archive_phase(const RetroArchiveEpisode& a) {
    auto& b=retro_archive_bank();
    return std::min(1.0,std::max(b.resumed_phase,(double)b.steps.load(std::memory_order_relaxed)/std::max(1.0,b.budget*a.anneal)));
}
static void retro_archive_file(const char* path,bool write,double phase=0);
static void retro_archive_sync(RetroArchiveCell& c){c.state=c.states.empty()?nullptr:c.states.front();}
// Area identity of a stored snapshot, equal to retro_area_key() at capture time (robs_area is $0760+1).
static unsigned retro_archive_snapshot_area(const RetroStart& st) {
    int level=retro_level_id(st.world,st.stage);
    if(level<0)return 0xffffffffu;
    return ((unsigned)level<<24)|((unsigned)(st.area-1)<<16)|(unsigned)st.data;
}
static int retro_archive_section(const RetroArchiveBank& b,int x){return retro_progress_x(x)/b.group_width;}
static uint64_t retro_archive_group_key(unsigned area,int section){return ((uint64_t)area<<16)|(uint64_t)(section&0xffff);}
// Index of the group for (area, section), created on first use (bank mutex held).
static int retro_archive_group(RetroArchiveBank& b,unsigned area,int section) {
    uint64_t key=retro_archive_group_key(area,section);
    auto it=b.group_index.find(key);
    if(it!=b.group_index.end())return (int)it->second;
    TeacherGroup g;g.key=key;g.area=area;g.level=(int32_t)(area>>24);g.section=section;
    b.group_index[key]=b.groups.size();b.groups.push_back(g);
    return (int)b.groups.size()-1;
}
static int retro_archive_cell_group(RetroArchiveBank& b,const RetroArchiveSnapshot& s) {
    return retro_archive_group(b,retro_archive_snapshot_area(s.start),retro_archive_section(b,s.start.x));
}
// env.spawn_levels as a bit mask; the practice branch of retro_archive_choose uses the same levels.
static uint32_t retro_archive_spawn_mask(const Env* e) {
    uint32_t mask=0;
    for(int i=0;i<e->spawn_n;i++){int l=retro_level_id(e->spawn_w[i],e->spawn_l[i]);if(l>=0)mask|=1u<<l;}
    return mask;
}
// Eligible: at least one cell with at least one state, in a spawn level (bank mutex held).
static int retro_archive_eligibility(RetroArchiveBank& b,uint32_t spawn) {
    std::vector<char> has(b.groups.size(),0);
    for(auto& c:b.cells)if(c.group>=0&&(size_t)c.group<has.size()&&!c.states.empty())has[c.group]=1;
    int n=0;
    for(size_t i=0;i<b.groups.size();i++) {
        int level=b.groups[i].level;
        b.groups[i].eligible=has[i]&&level>=0&&level<32&&(spawn>>level&1);
        n+=b.groups[i].eligible;
    }
    return n;
}
static void retro_archive_init(Env* e,Dict* cfg) {
    if(!retro_option(cfg,"archive_enabled",0))return;
    if(e->practice||(e->terminate_on_clear&&!e->archive_practice))
        throw std::runtime_error("retro archive requires natural-life episodes");
    std::unique_ptr<RetroArchiveEpisode> a(new RetroArchiveEpisode);
    a->start=retro_option(cfg,"archive_start",.6);a->end=retro_option(cfg,"archive_end",.1);
    a->anneal=retro_option(cfg,"archive_anneal_fraction",.8);a->priority=retro_option(cfg,"archive_priority",.5);
    a->level_focus=retro_option(cfg,"archive_level_focus",0);
    a->depth=retro_option(cfg,"archive_depth_priority",0);
    a->backward=retro_option(cfg,"archive_backward",0);
    double interval=retro_option(cfg,"archive_capture_interval",128),width=retro_option(cfg,"archive_cell_width",128);
    double mb=retro_option(cfg,"archive_capacity_mb",128);
    if(!std::isfinite(a->start+a->end+a->anneal+a->priority+a->level_focus+a->depth+a->backward+interval+width+mb)
        ||a->level_focus<0||a->level_focus>1||a->depth<0||a->depth>10||a->backward<0||a->backward>10
        ||a->end<0||a->start<a->end||a->start>1||a->anneal<=0||a->anneal>1
        ||a->priority<0||a->priority>1||interval<1||interval>4096||interval!=floor(interval)
        ||width<16||width>1024||width!=floor(width)||mb<1||mb>1024)
        throw std::runtime_error("retro: invalid archive configuration");
    a->interval=interval;a->width=width;
    a->practice=e->archive_practice;
    a->practice_focus=retro_option(cfg,"archive_practice_focus",0);
    if(!std::isfinite(a->practice_focus)||a->practice_focus<0||a->practice_focus>1)
        throw std::runtime_error("retro: archive_practice_focus must be in [0, 1]");
    if(a->practice){a->end=a->start;a->priority=a->level_focus=a->depth=a->backward=0;}
    // Curriculum teacher (2026-11-07). Defaults keep the Go-Explore weighting; all keys are validated either way.
    double teacher=retro_option(cfg,"archive_teacher",0);
    double group_width=retro_option(cfg,"archive_group_width",256),variants=retro_option(cfg,"archive_variants",8);
    double share=retro_option(cfg,"archive_full_game_share",.3);
    double success=retro_option(cfg,"archive_success_distance",512);
    double attempt=retro_option(cfg,"archive_attempt_decisions",600);
    double chain=retro_option(cfg,"archive_death_chain",1),chain_interval=retro_option(cfg,"archive_chain_interval",32);
    double chain_depth=retro_option(cfg,"archive_chain_depth",4),chain_skip=retro_option(cfg,"archive_chain_skip",16);
    double upstream=retro_option(cfg,"archive_upstream_groups",6),min_attempts=retro_option(cfg,"archive_lp_min_attempts",5);
    TeacherParams tp;
    tp.death_half_life=retro_option(cfg,"archive_death_half_life",20000000);
    tp.lp_fast=retro_option(cfg,"archive_lp_fast",.05);tp.lp_slow=retro_option(cfg,"archive_lp_slow",.005);
    tp.weight_deaths=retro_option(cfg,"archive_weight_deaths",.5);tp.weight_lp=retro_option(cfg,"archive_weight_lp",.35);
    tp.weight_stale=retro_option(cfg,"archive_weight_stale",.15);tp.rank_power=retro_option(cfg,"archive_rank_power",1);
    tp.floor=retro_option(cfg,"archive_floor",.1);
    auto integer=[](double x,double lo,double hi){return std::isfinite(x)&&x>=lo&&x<=hi&&x==floor(x);};
    if((teacher!=0&&teacher!=1)||(chain!=0&&chain!=1)||!integer(group_width,32,2048)
        ||!integer(variants,1,RETRO_ARCHIVE_MAX_VARIANTS)||!std::isfinite(share)||share<0||share>1
        ||!integer(success,1,65535)||!integer(attempt,1,10000000)||!integer(chain_interval,1,4096)
        ||!integer(chain_depth,1,256)||!integer(chain_skip,0,10000000)||!integer(upstream,0,1024)
        ||!integer(min_attempts,1,1000000))
        throw std::runtime_error("retro: invalid archive teacher configuration");
    tp.upstream_groups=(int)upstream;tp.lp_min_attempts=(int)min_attempts;
    try{teacher_validate(tp);}catch(const std::exception&){throw std::runtime_error("retro: invalid archive teacher configuration");}
    if(teacher&&e->archive_practice)throw std::runtime_error("retro: archive_teacher requires archive_practice = 0");
    if(teacher&&(a->start+share>1||a->end+share>1))
        throw std::runtime_error("retro: archive_teacher requires archive_start + archive_full_game_share <= 1 and archive_end + archive_full_game_share <= 1");
    if(teacher) {
        static std::once_flag ignored;
        std::call_once(ignored,[]{fprintf(stderr,"[retro] archive_teacher=1: archive_priority, archive_level_focus, archive_depth_priority and archive_backward are ignored\n");});
    }
    a->chain.resize(teacher&&chain?(size_t)chain_depth:0);
    a->lane=(retro_random(&e->rng)+.5)/4294967296.0;
    auto& b=retro_archive_bank();
    {
        std::lock_guard<std::mutex> lock(b.mutex);
        b.capacity=std::max(size_t(1),size_t(mb*1024*1024/(sizeof(RetroArchiveSnapshot)+(teacher?0:RETRO_ARCHIVE_V1_CELL_BYTES)+64)));
        b.anneal=a->anneal;
        b.teacher_enabled=teacher!=0;b.teacher=tp;b.death_chain=chain!=0;
        b.group_width=(int)group_width;b.variants=teacher?(int)variants:1;b.full_game_share=share;
        b.success_distance=(int)success;b.attempt_decisions=(int)attempt;
        b.chain_interval=(int)chain_interval;b.chain_depth=(int)chain_depth;b.chain_skip=(int)chain_skip;
        b.spawn_levels=retro_archive_spawn_mask(e);
    }
    e->archive=a.release();
    // Optional starting states for a fresh policy (states do not depend on the policy).
    // Loaded once into an empty bank; its saved schedule phase is ignored.
    DictItem* init=dict_find(cfg,"archive_init_path");
    if(init&&init->str&&strcmp(init->str,"None")) {
        static std::once_flag once;
        std::call_once(once,[&]{
            if(!b.cells.empty())return;
            retro_archive_file(init->str,false);b.resumed_phase=0;
            fprintf(stderr,"[retro] archive_init_path: %zu starting cells from %s\n",b.cells.size(),init->str);
        });
    }
}
static void retro_archive_chain_clear(RetroArchiveEpisode& a) {
    for(auto& c:a.chain)c.state.reset();
    a.chain_next=0;a.chain_count=0;
    a.dying_decision=-1;a.dying_area=0xffffffffu;
}
// Teacher priority refresh: eligibility, teacher_priorities and the float CDF (bank mutex held).
static void retro_archive_refresh(Env* e) {
    auto& b=retro_archive_bank();uint64_t now=b.steps.load();
    retro_archive_eligibility(b,retro_archive_spawn_mask(e));
    b.cdf_eligible=teacher_priorities(b.groups,now,b.teacher);
    teacher_cdf(b.groups,&b.cdf);b.cdf_steps=now;
}
// Teacher mode-2 start (bank mutex held). False when no group is eligible: the caller falls back like legacy.
static bool retro_archive_teacher_pick(Env* e,int* id) {
    auto& a=*e->archive;auto& b=retro_archive_bank();uint64_t now=b.steps.load();
    if(b.cdf.empty()||now-b.cdf_steps>=RETRO_TEACHER_REFRESH_STEPS)retro_archive_refresh(e);
    if(!b.cdf_eligible||b.cdf.empty())return false;
    float u=(float)((retro_random(&e->rng)+.5)/4294967296.0);
    int n=(int)b.cdf.size(),g=teacher_sample(b.cdf.data(),n,u);
    // u can round to 1.0f, and the forced final CDF entry then lands on the last group even with probability 0.
    while(g>0&&b.groups[g].prob<=0)g--;
    if(b.groups[g].prob<=0)return false;
    std::vector<size_t> members;
    for(size_t i=0;i<b.cells.size();i++)if(b.cells[i].group==g&&!b.cells[i].states.empty())members.push_back(i);
    if(members.empty())return false;
    auto& c=b.cells[members[retro_random(&e->rng)%members.size()]];
    a.state=c.states[retro_random(&e->rng)%c.states.size()];
    c.selected++;c.last=now;
    a.feedback=true;a.feedback_key=c.key;a.feedback_step=0;a.feedback_progress=0;
    e->start=&a.state->start;*id=c.level;
    a.attempt_pending=true;a.attempt_group=g;a.attempt_x0=retro_progress_x(a.state->start.x);
    a.attempt_area=retro_archive_snapshot_area(a.state->start);a.attempt_decisions=0;
    b.groups[g].last_selected=now;
    return true;
}
static void retro_archive_choose(Env* e,int* id) {
    if(!e->archive)return;
    auto& a=*e->archive;auto& b=retro_archive_bank();double phase=retro_archive_phase(a);
    double archived=a.start+(a.end-a.start)*phase;
    double full=b.teacher_enabled?b.full_game_share:a.practice?0:(1-archived)*(.5+.5*phase);
    a.state.reset();a.feedback=false;a.attempt_pending=false;retro_archive_chain_clear(a);
    a.mode=a.lane<archived?2:(a.lane<archived+full?1:0);
    std::lock_guard<std::mutex> lock(b.mutex);
    if(a.mode==2&&b.teacher_enabled&&!b.cells.empty()&&retro_archive_teacher_pick(e,id)) {
    } else if(a.mode==2&&!b.teacher_enabled&&!b.cells.empty()) {
        // Uniform floor plus failure/uncertainty and staleness. Impossible cells
        // cannot monopolize resets; no hand-authored ordering or mastery gate.
        float deepest[32]={};
        if(a.backward>0)for(auto& c:b.cells)deepest[c.level]=std::max(deepest[c.level],c.depth);
        // Practice bank: key bits 52-59 name the key point; each point gets the same share whatever its state count.
        // env.archive_practice_focus (2026-10-31): that share of key-point picks goes to points by their failure rate
        // (the cells' feedback pooled per point, one-success/one-failure prior), the rest stays uniform.
        int per_point[256]={};
        double point_fail[256]={},point_trials[256]={},fail_sum=0,points=0;
        // practice draws only key points in the spawn levels (env.spawn_levels), so a run on 2 levels skips the rest
        bool spawn_level[32]={};
        for(int i=0;i<e->spawn_n;i++){int l=retro_level_id(e->spawn_w[i],e->spawn_l[i]);if(l>=0)spawn_level[l]=true;}
        if(a.practice)for(auto& c:b.cells){
            if(!spawn_level[c.level])continue;
            int p=(c.key>>52)&255;
            if(!per_point[p])points++;
            per_point[p]++;point_fail[p]+=c.failures;point_trials[p]+=c.failures+c.successes;
        }
        if(a.practice&&a.practice_focus>0)for(int p=0;p<256;p++)if(per_point[p])fail_sum+=(point_fail[p]+1)/(point_trials[p]+2);
        auto weight=[&](const RetroArchiveCell& c){
            if(a.practice) {
                if(!spawn_level[c.level])return 0.0;
                int p=(c.key>>52)&255;
                double share=(1-a.practice_focus)/points;
                if(a.practice_focus>0)share+=a.practice_focus*((point_fail[p]+1)/(point_trials[p]+2))/fail_sum;
                return share/per_point[p];
            }
            double difficulty=(c.failures+1.0)/(c.failures+c.successes+2.0);
            double novel=1/std::sqrt(c.selected+1.0);
            double stale=std::min(1.0,(double)(b.steps.load()-c.last)/1000000.0);
            // Optional end-first curriculum: early on, cells deep into their level are picked
            // far more often; the bias fades to neutral as the archive phase reaches 1.
            double back=a.backward>0&&deepest[c.level]>0
                ?std::exp(a.backward*(1-phase)*(c.depth/deepest[c.level]-1)):1;
            // Optional depth priority: lower cells (key bits 42-45 hold y/32, 0-7)
            // are picked more often, so drops to lower floors get explored.
            double depth=1+a.depth*std::min(7.0,(double)((c.key>>42)&15))/7;
            return back*depth*(1+a.priority*(3*difficulty+2*novel+stale));
        };
        // Optional level focus: per-cell sampling gives every archived level about
        // the same share, however often it fails. With probability level_focus,
        // first choose a level by its measured failure rate (cell feedback with a
        // one-success/one-failure prior, plus a 0.1 floor), then a cell within it.
        int level=-1;
        if(a.level_focus>0&&(retro_random(&e->rng)+.5)/4294967296.0<a.level_focus) {
            double failures[32]={},trials[32]={},levels[32]={},total=0;
            for(auto& c:b.cells){failures[c.level]+=c.failures;trials[c.level]+=c.failures+c.successes;levels[c.level]=1;}
            for(int l=0;l<32;l++)if(levels[l])total+=levels[l]=.1+(failures[l]+1)/(trials[l]+2);
            double pick=(retro_random(&e->rng)+.5)/4294967296.0*total;
            for(int l=0;l<32;l++)if(levels[l]){level=l;if((pick-=levels[l])<=0)break;}
        }
        double sum=0;for(auto& c:b.cells)if(level<0||c.level==level)sum+=weight(c);
        double pick=(retro_random(&e->rng)+.5)/4294967296.0*sum;
        size_t chosen=b.cells.size();
        for(size_t i=0;i<b.cells.size();i++)if(level<0||b.cells[i].level==level){chosen=i;if((pick-=weight(b.cells[i]))<=0)break;}
        auto& c=b.cells[chosen];c.selected++;c.last=b.steps.load();
        // One state per cell unless a multi-variant bank was loaded: no extra random draw in the ordinary case.
        a.state=c.states.size()>1?c.states[retro_random(&e->rng)%c.states.size()]:c.states.front();
        a.feedback=true;a.feedback_key=c.key;
        a.feedback_step=0;a.feedback_progress=0;e->start=&a.state->start;*id=c.level;
    } else {
        if(a.mode==2)a.mode=0;
        if(a.mode==1){*id=0;e->start=retro_rom().starts[0].get();}
    }
    b.resets[a.mode]++;
}
// A practice-bank episode that started at a key point (not at a level start).
static bool retro_archive_keypoint(const Env* e) {
    return e->archive&&e->archive->practice&&e->archive->state;
}
static void retro_archive_restore(Env* e) {
    if(!e->archive||!e->archive->state)return;
    const auto& l=e->archive->state->ledger;
    memcpy(e->frontiers,l.frontiers,sizeof(l.frontiers));e->frontier_count=l.frontier_count;
    memcpy(e->area_transition_keys,l.areas,sizeof(l.areas));e->area_transition_key_count=l.area_count;
    e->rewarded_levels=l.rewarded;e->last_area_key=l.last_area;e->pipe_phase=l.pipe_phase;
}
static uint64_t retro_archive_key(Env* e) {
    const auto* m=e->emu->low_mem();
    // Physical room, spatial cell, power and vertical motion. Exact route and
    // enemy state remain in the snapshot; reservoir replacement adds variety.
    return uint64_t(retro_area_key(m)) | (uint64_t(robs_x(m)/e->archive->width)<<32)
        | (uint64_t(m[0xce]/32)<<42) | (uint64_t(m[0x756]&3)<<46)
        | (uint64_t(m[0x1d]&3)<<48);
}
// Exact snapshot of the current emulator state, screen and reward ledger (shared by captures, the death chain,
// keypoint_states and the tests).
static std::shared_ptr<RetroArchiveSnapshot> retro_archive_snapshot(Env* e) {
    auto s=std::make_shared<RetroArchiveSnapshot>();auto& st=s->start;auto& l=s->ledger;
    const auto* m=e->emu->low_mem();
    e->emu->save_state(&st.state);const auto& f=e->emu->frame();
    for(int y=0;y<240;y++)memcpy(st.pixels+y*256,f.pixels+y*f.pitch,256);
    memcpy(st.palette,f.palette,sizeof(st.palette));
    st.world=e->world;st.stage=e->stage;st.area=e->area;st.data=m[0xe7]+256*m[0xe8];st.x=e->x_pos;st.rta_offset_frames=0;
    memcpy(l.frontiers,e->frontiers,sizeof(l.frontiers));l.frontier_count=e->frontier_count;
    memcpy(l.areas,e->area_transition_keys,sizeof(l.areas));l.area_count=e->area_transition_key_count;
    l.rewarded=e->rewarded_levels;l.last_area=e->last_area_key;l.pipe_phase=e->pipe_phase;
    return s;
}
// Capture filter: playable, alive, on screen and standing in a level. Returns the level id or -1.
static int retro_archive_capturable(Env* e) {
    const auto* m=e->emu->low_mem();
    if(!retro_playable_area_state(m)||robs_dead(m)||m[0xb5]!=1||m[0xce]>224)return -1;
    return retro_level_id(e->world,e->stage);
}
static void retro_archive_remove_cell(RetroArchiveBank& b,size_t i) {
    b.index.erase(b.cells[i].key);b.snapshots-=b.cells[i].states.size();
    if(i+1!=b.cells.size()){b.cells[i]=std::move(b.cells.back());b.index[b.cells[i].key]=i;}
    b.cells.pop_back();
}
// Free one snapshot slot, never from the cell with key `keep` (bank mutex held; teacher on). Rules:
//  a. among cells with more than one state, the one whose group has the smallest prob (ties: more states, then
//     random) drops one random state;
//  b. else, among groups with more than one cell, the smallest-prob group loses one random cell;
//  c. else the only cell of the smallest-prob group goes and that group becomes ineligible (its stats stay).
static bool retro_archive_evict(Env* e,bool protect,uint64_t keep) {
    auto& b=retro_archive_bank();
    auto prob=[&](int g){return g>=0&&(size_t)g<b.groups.size()?b.groups[g].prob:0.0;};
    long best=-1;double best_prob=0;size_t best_states=0;unsigned ties=0;
    for(size_t i=0;i<b.cells.size();i++) {
        auto& c=b.cells[i];
        if((protect&&c.key==keep)||c.states.size()<=1)continue;
        double p=prob(c.group);
        bool better=best<0||p<best_prob||(p==best_prob&&c.states.size()>best_states);
        bool tie=best>=0&&p==best_prob&&c.states.size()==best_states;
        if(better){best=i;best_prob=p;best_states=c.states.size();ties=1;}
        else if(tie&&retro_random(&e->rng)%++ties==0)best=i;
    }
    if(best>=0) {
        auto& c=b.cells[best];
        c.states.erase(c.states.begin()+retro_random(&e->rng)%c.states.size());
        b.snapshots--;retro_archive_sync(c);
        return true;
    }
    std::vector<size_t> count(b.groups.size(),0);
    for(auto& c:b.cells)if(c.group>=0&&(size_t)c.group<count.size())count[c.group]++;
    int keep_group=-1;
    if(protect){auto it=b.index.find(keep);if(it!=b.index.end())keep_group=b.cells[it->second].group;}
    for(int rule=0;rule<2;rule++) {
        // rule 0 = b (groups with more than one cell), rule 1 = c (single-cell groups other than the inserting cell's)
        long group=-1;double group_prob=0;ties=0;
        for(size_t g=0;g<b.groups.size();g++) {
            if(rule==0?count[g]<=1:(count[g]!=1||(int)g==keep_group))continue;
            double p=b.groups[g].prob;
            if(group<0||p<group_prob){group=g;group_prob=p;ties=1;}
            else if(p==group_prob&&retro_random(&e->rng)%++ties==0)group=g;
        }
        if(group<0)continue;
        std::vector<size_t> members;
        for(size_t i=0;i<b.cells.size();i++)if(b.cells[i].group==group&&!(protect&&b.cells[i].key==keep))members.push_back(i);
        if(members.empty())continue;
        retro_archive_remove_cell(b,members[retro_random(&e->rng)%members.size()]);
        if(rule==1){b.groups[group].eligible=0;b.cdf.clear();}  // the CDF still gives it mass: refresh at the next pick
        return true;
    }
    return false;
}
// Store snapshot s under `key` (bank mutex held). Returns whether it was stored. Teacher off: the single-state
// archive exactly as before 2026-11-07 (replace 1 in 16, evict a random cell of the most represented level).
static bool retro_archive_insert(Env* e,std::shared_ptr<RetroArchiveSnapshot> s,uint64_t key,int level,unsigned area) {
    auto& b=retro_archive_bank();
    int section=retro_archive_section(b,s->start.x);
    auto it=b.index.find(key);
    if(!b.teacher_enabled) {
        size_t slot;
        if(it!=b.index.end()) {
            slot=it->second;
            // Replace occasionally, preserving coverage without freezing one enemy phase.
            auto& c=b.cells[slot];c.visits++;
            if(retro_random(&e->rng)%16)return false;
            b.snapshots-=c.states.size();c.states.clear();
        } else {
            if(b.cells.size()<b.capacity){slot=b.cells.size();b.cells.emplace_back();}
            else {
                // Evict from the most represented level, retaining broad coverage.
                int counts[32]={};for(auto& c:b.cells)counts[c.level]++;
                int victim=std::max_element(counts,counts+32)-counts;
                std::vector<size_t> candidates;
                for(size_t i=0;i<b.cells.size();i++)if(b.cells[i].level==victim)candidates.push_back(i);
                slot=candidates[retro_random(&e->rng)%candidates.size()];b.index.erase(b.cells[slot].key);
                b.snapshots-=b.cells[slot].states.size();
            }
            b.cells[slot]=RetroArchiveCell{};b.cells[slot].key=key;b.cells[slot].level=level;
            b.cells[slot].group=retro_archive_group(b,area,section);
            b.index[key]=slot;
        }
        auto& c=b.cells[slot];
        c.depth=retro_archive_depth(*s,level);c.states.push_back(std::move(s));b.snapshots++;retro_archive_sync(c);
        return true;
    }
    if(it==b.index.end()) {
        if(b.snapshots>=b.capacity&&!retro_archive_evict(e,false,0))return false;
        RetroArchiveCell c;c.key=key;c.level=level;c.group=retro_archive_group(b,area,section);
        c.depth=retro_archive_depth(*s,level);c.states.push_back(std::move(s));retro_archive_sync(c);
        b.index[key]=b.cells.size();b.cells.push_back(std::move(c));b.snapshots++;
        return true;
    }
    b.cells[it->second].visits++;
    if(b.cells[it->second].states.size()<(size_t)b.variants
        &&(b.snapshots<b.capacity||retro_archive_evict(e,true,key))) {
        auto& c=b.cells[b.index[key]];  // eviction may have moved the cell
        c.depth=retro_archive_depth(*s,level);c.states.push_back(std::move(s));b.snapshots++;retro_archive_sync(c);
        return true;
    }
    // Full cell: reservoir replacement with a floor, so the bank keeps refreshing.
    auto& c=b.cells[b.index[key]];
    double p=std::max((double)b.variants/(double)c.visits,1.0/32);
    if((retro_random(&e->rng)+.5)/4294967296.0>=p)return false;
    c.depth=retro_archive_depth(*s,level);
    c.states[retro_random(&e->rng)%c.states.size()]=std::move(s);retro_archive_sync(c);
    return true;
}
static void retro_archive_capture(Env* e) {
    auto& a=*e->archive;auto& b=retro_archive_bank();const auto* m=e->emu->low_mem();
    int level=retro_archive_capturable(e);if(level<0)return;
    uint64_t key=retro_archive_key(e);unsigned area=retro_area_key(m);
    auto s=retro_archive_snapshot(e);
    std::lock_guard<std::mutex> lock(b.mutex);
    if(!retro_archive_insert(e,std::move(s),key,level,area))return;
    a.feedback=true;a.feedback_key=key;a.feedback_step=e->episode_decisions;a.feedback_progress=e->progress_pixels;
}
// Teacher bookkeeping for one decision (modes 0 and 1: death chain and failure crediting; mode 2: the attempt).
static void retro_archive_teacher_step(Env* e,bool done,bool death,bool progress,int maze_loops,int old_x) {
    auto& a=*e->archive;auto& b=retro_archive_bank();
    bool failure=death||maze_loops>0;
    if(a.mode==2) {
        if(!a.attempt_pending)return;
        a.attempt_decisions++;
        bool win=progress||(e->last_area_key==a.attempt_area
            &&retro_progress_x(e->x_pos)>=a.attempt_x0+b.success_distance);
        bool loss=!win&&(failure||a.attempt_decisions>=b.attempt_decisions||done);
        if(!win&&!loss)return;
        std::lock_guard<std::mutex> lock(b.mutex);
        if(a.attempt_group>=0&&(size_t)a.attempt_group<b.groups.size())
            teacher_record(b.groups[a.attempt_group],1,win?1:0,b.teacher);
        (win?b.attempt_wins:b.attempt_losses)++;
        a.attempt_pending=false;
        return;
    }
    // Track the onset of dying before any failure handling.
    unsigned now_area=e->last_area_key;int now_x=old_x;
    {unsigned frontier=((now_area>>24)<<16)|(now_area&0xffff);
     for(int i=0;i<e->frontier_count;i++)if(e->frontiers[i].key==frontier){now_x=e->frontiers[i].last_x;break;}}
    if(e->is_dead&&a.dying_decision<0){a.dying_decision=e->episode_decisions;a.dying_x=now_x;a.dying_area=now_area;}
    if(failure) {
        bool onset=death&&a.dying_decision>=0;
        int reference=onset?a.dying_decision:e->episode_decisions;
        unsigned area=onset?a.dying_area:now_area;
        int x=onset?a.dying_x:(maze_loops>0?old_x:now_x);
        a.last_failure_decision=reference;a.last_failure_x=x;
        std::lock_guard<std::mutex> lock(b.mutex);
        // Deposit the chain first, so groups it creates already receive this failure's mass.
        if(b.death_chain&&a.chain_count) {
            int size=(int)a.chain.size();
            for(int k=0;k<a.chain_count;k++) {
                auto& c=a.chain[((a.chain_next-a.chain_count+k)%size+size)%size];
                if(c.decision<=reference-b.chain_skip) {
                    retro_archive_insert(e,c.state,c.key,c.level,c.area);b.chain_deposits++;
                }
            }
        }
        retro_archive_chain_clear(a);
        if(death)a.dying_decision=-1;
        if(area!=0xffffffffu) {
            teacher_credit_failure(b.groups,area,retro_archive_section(b,x),b.steps.load(),b.teacher);
            b.death_events++;
        }
    }
    if(b.death_chain&&!done&&!a.chain.empty()&&e->episode_decisions%b.chain_interval==0) {
        int level=retro_archive_capturable(e);if(level<0)return;
        auto& c=a.chain[a.chain_next];
        c.state=retro_archive_snapshot(e);c.key=retro_archive_key(e);c.level=level;c.x=e->x_pos;
        c.decision=e->episode_decisions;c.area=retro_area_key(e->emu->low_mem());
        a.chain_next=(a.chain_next+1)%(int)a.chain.size();a.chain_count=std::min(a.chain_count+1,(int)a.chain.size());
    }
}
static void retro_archive_step(Env* e,bool done,bool death,bool progress,int maze_loops,int old_x) {
    if(!e->archive)return;
    auto& a=*e->archive;auto& b=retro_archive_bank();
    b.steps.fetch_add(1,std::memory_order_relaxed);b.mode_steps[a.mode].fetch_add(1,std::memory_order_relaxed);
    if(a.feedback&&(done||death||progress||e->progress_pixels-a.feedback_progress>=128||e->episode_decisions-a.feedback_step>=std::min(256,a.interval))) {
        std::lock_guard<std::mutex> lock(b.mutex);auto it=b.index.find(a.feedback_key);
        if(it!=b.index.end()) {
            auto& c=b.cells[it->second];
            bool good=progress||e->progress_pixels-a.feedback_progress>=128;
            c.successes+=good;c.failures+=!good;
        }
        a.feedback=false;
    }
    if(b.teacher_enabled)retro_archive_teacher_step(e,done,death,progress,maze_loops,old_x);
    // Last frame of a nonterminal decision is always rendered in retro_step.
    if(!done&&!a.practice&&e->episode_decisions%a.interval==0)retro_archive_capture(e);
}
static void retro_archive_io(gzFile f,void* p,size_t n,bool write) {
    if(n>1000000||(write?gzwrite(f,p,(unsigned)n):gzread(f,p,(unsigned)n))!=(int)n)
        throw std::runtime_error("retro: truncated/corrupt archive");
}
// One snapshot in the per-snapshot layout shared by v1 and v2: ledger, pixels, palette, metadata[7], core size, core.
static void retro_archive_snapshot_io(gzFile f,RetroArchiveSnapshot& s,int level,bool write) {
    auto& st=s.start;
    retro_archive_io(f,&s.ledger,sizeof(s.ledger),write);
    retro_archive_io(f,st.pixels,sizeof(st.pixels),write);retro_archive_io(f,st.palette,sizeof(st.palette),write);
    int metadata[7]={st.world,st.stage,st.area,st.data,st.x,st.rta_offset_frames,level};
    retro_archive_io(f,metadata,sizeof(metadata),write);
    if(!write){st.world=metadata[0];st.stage=metadata[1];st.area=metadata[2];st.data=metadata[3];st.x=metadata[4];st.rta_offset_frames=metadata[5];
        if(level<0||level>=32||retro_level_id(st.world,st.stage)!=level
            ||s.ledger.frontier_count<0||s.ledger.frontier_count>256||s.ledger.area_count<0||s.ledger.area_count>256)
            throw std::runtime_error("retro: invalid archive metadata");}
    Mem_Writer writer;if(write)retro_check(st.state.write(writer));
    uint32_t size=write?writer.size():0;retro_archive_io(f,&size,sizeof(size),write);
    if(!size||size>100000)throw std::runtime_error("retro: invalid core state size");
    std::vector<char> bytes(size);if(write)memcpy(bytes.data(),writer.data(),size);
    retro_archive_io(f,bytes.data(),size,write);
    if(!write){Mem_File_Reader reader(bytes.data(),size);retro_check(st.state.read(reader));}
}
static void retro_archive_check_groups(const std::vector<TeacherGroup>& groups) {
    std::unordered_map<uint64_t,size_t> seen;
    for(size_t k=0;k<groups.size();k++) {
        auto& g=groups[k];
        if(g.key!=retro_archive_group_key(g.area,g.section)||g.level!=(int32_t)(g.area>>24)||g.level<0||g.level>=32
            ||g.section<0||g.section>0xffff||g.wins>g.attempts||!std::isfinite(g.fast)||!std::isfinite(g.slow)
            ||!std::isfinite(g.deaths)||g.deaths<0||!std::isfinite(g.prob)||!seen.emplace(g.key,k).second)
            throw std::runtime_error("retro: invalid archive group");
    }
}
// After any load: group index, cell groups, snapshot count, restarted step stamps and eligibility (mutex held).
static void retro_archive_rebuild(RetroArchiveBank& b) {
    b.group_index.clear();
    for(size_t k=0;k<b.groups.size();k++) {
        auto& g=b.groups[k];
        b.group_index[g.key]=k;g.death_stamp=0;g.last_selected=0;
    }
    b.snapshots=0;
    for(auto& c:b.cells){c.group=retro_archive_cell_group(b,*c.states.front());b.snapshots+=c.states.size();retro_archive_sync(c);}
    retro_archive_eligibility(b,b.spawn_levels);
    b.cdf.clear();b.cdf_steps=0;b.cdf_eligible=0;
}
// Versioned native-endian POD metadata, portable core state serialization. Never
// serialize Nes_State's internal pointers. CRC and length checks reject damage.
// v1: one snapshot per cell. v2 (2026-11-07): stats[5] = state count, that many snapshots, then the teacher groups.
static void retro_archive_file(const char* path,bool write,double phase) {
    auto& b=retro_archive_bank();std::lock_guard<std::mutex> lock(b.mutex);
    std::string temp=std::string(path)+(write?".tmp":"");gzFile f=gzopen(temp.c_str(),write?"wb1":"rb");
    if(!f)throw std::runtime_error(std::string("retro: cannot open archive ")+path);
    try {
        uint64_t header[5]={RETRO_ARCHIVE_V2_MAGIC,retro_rom().fingerprint,b.cells.size(),sizeof(RetroArchiveLedger),0x01020304};
        retro_archive_io(f,header,sizeof(header),write);
        bool v2=header[0]==RETRO_ARCHIVE_V2_MAGIC;
        if((header[0]!=RETRO_ARCHIVE_V1_MAGIC&&!v2)||header[1]!=retro_rom().fingerprint||header[2]>b.capacity
            ||header[3]!=sizeof(RetroArchiveLedger)||header[4]!=0x01020304)
            throw std::runtime_error("retro: incompatible archive version, ROM, size or byte order");
        retro_archive_io(f,&phase,sizeof(phase),write);
        if(!std::isfinite(phase)||phase<0||phase>1)throw std::runtime_error("retro: invalid archive phase");
        std::vector<RetroArchiveCell> loaded;size_t total=0;
        for(size_t i=0;i<header[2];i++) {
            RetroArchiveCell c;auto& cell=write?b.cells[i]:c;
            // Diagnostic tools may build a cell with only the legacy `state` member set.
            std::vector<std::shared_ptr<RetroArchiveSnapshot>> single;
            if(write&&cell.states.empty()&&cell.state)single.push_back(cell.state);
            const auto& states=single.empty()?cell.states:single;
            if(write&&states.empty())throw std::runtime_error("retro: archive cell without a state");
            uint64_t stats[7]={cell.key,cell.visits,cell.selected,cell.failures,cell.successes,(uint64_t)states.size(),(uint64_t)cell.level};
            retro_archive_io(f,stats,sizeof(stats),write);
            uint64_t count=write||v2?stats[5]:1;
            if(!write) {
                c.key=stats[0];c.visits=stats[1];c.selected=stats[2];c.failures=stats[3];c.successes=stats[4];c.level=(int)stats[6];
                if(count<1||count>RETRO_ARCHIVE_MAX_VARIANTS||stats[6]>=32)throw std::runtime_error("retro: invalid archive cell");
            }
            for(uint64_t k=0;k<count;k++) {
                auto s=write?states[k]:std::make_shared<RetroArchiveSnapshot>();
                retro_archive_snapshot_io(f,*s,cell.level,write);
                if(!write)c.states.push_back(std::move(s));
            }
            total+=count;
            if(!write) {
                if(total>b.capacity)throw std::runtime_error("retro: incompatible archive version, ROM, size or byte order");
                c.depth=retro_archive_depth(*c.states.front(),c.level);loaded.push_back(std::move(c));
            }
        }
        std::vector<TeacherGroup> groups;
        if(v2) {
            uint64_t shape[2]={b.groups.size(),sizeof(TeacherGroup)};
            retro_archive_io(f,shape,sizeof(shape),write);
            if(shape[1]!=sizeof(TeacherGroup)||shape[0]>(1u<<24))throw std::runtime_error("retro: invalid archive groups");
            if(!write)groups.resize(shape[0]);
            for(uint64_t k=0;k<shape[0];k++)retro_archive_io(f,write?&b.groups[k]:&groups[k],sizeof(TeacherGroup),write);
        }
        if(!write){char extra;if(gzread(f,&extra,1)!=0)throw std::runtime_error("retro: archive trailing data or CRC failure");}
        if(gzclose(f)!=Z_OK){f=nullptr;throw std::runtime_error("retro: archive close failed");}f=nullptr;
        if(write){if(rename(temp.c_str(),path))throw std::runtime_error("retro: archive publish failed");}
        else {
            std::unordered_map<uint64_t,size_t> index;
            for(size_t i=0;i<loaded.size();i++)if(!index.emplace(loaded[i].key,i).second)throw std::runtime_error("retro: duplicate archive cell");
            retro_archive_check_groups(groups);
            b.cells=std::move(loaded);b.index=std::move(index);b.groups=std::move(groups);
            retro_archive_rebuild(b);b.resumed_phase=phase;
        }
    }catch(...){if(f)gzclose(f);throw;}
}
