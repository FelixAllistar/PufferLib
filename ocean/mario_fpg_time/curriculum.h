#pragma once
#include "table.h"
#include "pipe_start.h"
#include <algorithm>

// Reference frames remaining along a ROM-verified successful trajectory.
// The first useful control is three frames from success; the last one-frame
// state is already committed to the outcome and is deliberately excluded.
enum {FPT_CURRICULUM_LEVELS=17,FPT_CURRICULUM_ROUTE_END=1610};
static const int fpt_curriculum_depths[FPT_CURRICULUM_LEVELS]={
    3,4,5,6,8,12,16,24,32,48,64,96,128,192,256,320,425
};
struct FptCurriculumConfig {
    int enabled,adaptive,initial_level,window,confirmations;
    float threshold,replay_fraction;
    int offsets[FPT_CURRICULUM_LEVELS+1],depths[FPT_CURRICULUM_LEVELS];
};
struct FptCurriculumProgress {
    int initialized,level,sampled_level,attempts,wins,streak;
    uint32_t promotions;
    float last_rate;
};

static void fpt_require_natural_bank(const SmbBank& bank) {
    if(bank.header.worlds!=1||smb_bank_hash(1469598103934665603ull,bank.worlds.data(),bank.worlds.size())!=0x0df85300a23f25f8ull)
        throw std::runtime_error("FPG curriculum requires the original unmodified 1-1 world data");
    for(const auto& e:bank.entries) {
        const auto* m=e.scene.initial.ram;int x=m[0x6d]*256+m[0x86];
        if(e.stage||e.scene.mask||(e.flags&SMB_BANK_CONSTRUCTED)||!(e.flags&SMB_BANK_FLAGPOLE)
            ||m[0x75f]||m[0x75c]||m[0x74e]!=1||m[0x754]!=1||m[0xe]!=8
            ||x<FPT_PIPE_MIN_X||x>=FPT_PIPE_POLE_X)
            throw std::runtime_error("FPG curriculum reset is outside the natural 1-1 successful approach");
    }
}
static std::vector<uint32_t> fpt_curriculum_indices(const SmbBank& bank,const std::vector<FpgTimeEntry>& table,
        FptCurriculumConfig* cfg) {
    if(table.size()!=bank.entries.size())throw std::runtime_error("curriculum timing table size mismatch");
    for(unsigned i=0;i<table.size();i++)if(bank.entries[i].source_id!=0
        ||bank.entries[i].source_frame+table[i].best_frames!=FPT_CURRICULUM_ROUTE_END)
        throw std::runtime_error("curriculum requires the recorded suffix lengths; rebuild its teacher-only timing table");
    std::vector<uint32_t> ordered;int low=3;
    for(int level=0;level<FPT_CURRICULUM_LEVELS;level++) {
        cfg->depths[level]=fpt_curriculum_depths[level];cfg->offsets[level]=(int)ordered.size();
        for(unsigned i=0;i<table.size();i++)if((int)table[i].best_frames>=low&&(int)table[i].best_frames<=cfg->depths[level]) {
            if(!(bank.entries[i].flags&SMB_BANK_FPG_TEACHER))throw std::runtime_error("curriculum reset lacks a recorded FPG continuation");
            ordered.push_back(i);
        }
        if((int)ordered.size()==cfg->offsets[level])throw std::runtime_error("curriculum has an empty difficulty band");
        low=cfg->depths[level]+1;
    }
    cfg->offsets[FPT_CURRICULUM_LEVELS]=(int)ordered.size();
    if(ordered.size()!=bank.entries.size())throw std::runtime_error("curriculum bank contains an unqualified or out-of-range start");
    return ordered;
}
SMB_HD void fpt_curriculum_init(FptCurriculumProgress* p,const FptCurriculumConfig* cfg) {
    *p={};p->initialized=1;p->level=cfg->initial_level;p->sampled_level=p->level;
}
SMB_HD int fpt_curriculum_choose(FptCurriculumProgress* p,const FptCurriculumConfig* cfg,uint32_t* rng) {
    if(!p->initialized)fpt_curriculum_init(p,cfg);
    int level=p->level;
    if(level>0&&cfg->replay_fraction>0) {
        float draw=(float)(smb_random(rng)>>8)*(1.0f/16777216.0f);
        if(draw<cfg->replay_fraction)level=(int)(smb_random(rng)%(unsigned)level);
    }
    p->sampled_level=level;return level;
}
SMB_HD int fpt_curriculum_result(FptCurriculumProgress* p,const FptCurriculumConfig* cfg,int won) {
    if(!cfg->enabled||!cfg->adaptive||p->sampled_level!=p->level)return 0;
    p->attempts++;p->wins+=won;
    if(p->attempts<cfg->window)return 0;
    p->last_rate=(float)p->wins/(float)p->attempts;
    if(p->last_rate>=cfg->threshold){if(p->streak<cfg->confirmations)p->streak++;}else p->streak=0;
    p->attempts=p->wins=0;
    if(p->streak<cfg->confirmations||p->level+1>=FPT_CURRICULUM_LEVELS)return 0;
    p->level++;p->promotions++;p->streak=0;return 1;
}
