#pragma once
#include "game.h"
// The curriculum teacher's math is shared with ocean/retro; only teacher_sample runs on the device.
#if defined(__CUDACC__)&&!defined(TEACHER_HD)
#define TEACHER_HD __host__ __device__
#endif
#include "../retro/curriculum_teacher.h"

// SMB_BANK_ROUTE: the entry comes from the reference movie (build_practice_bank --route). SMB_BANK_EDITED: the
// snapshot was taken as small Mario ($0754=1, $0756=0) instead of the movie's form. SMB_BANK_ROUTE_LOOP: the last
// route entry before a loop-back the reference movie itself takes (8-4's first pipe). All sit below the band bits.
enum {SMB_PRACTICE_BANDS=4,SMB_BANK_PRACTICE=16,SMB_BANK_ROUTE=32,SMB_BANK_EDITED=64,SMB_BANK_ROUTE_LOOP=128,SMB_PRACTICE_BAND_SHIFT=8};
struct SmbPracticeConfig {
    float fraction=0,replay=.2f;
    int max_frames=6000,stage_count=0;
    uint32_t stages[32]={},offsets[32][SMB_PRACTICE_BANDS+1]={};
    int loop_terminal=0;   // teacher on: a maze loop-back or wrong exit ends a practice episode as a death
};
struct SmbPracticeProgress {
    uint8_t level[32],attempts[32],wins[32],bad_windows[32];
    int stage,sampled;
};
SMB_HD int smb_practice_actor(int actor,int count,const SmbGameConfig* game,const SmbPracticeConfig* c) {
    int fpg=(int)(count*game->fpg_fraction+.5f),practice=(int)(count*c->fraction+.5f);
    return actor>=count-fpg-practice&&actor<count-fpg;
}
// Curriculum teacher over groups of practice states (practice_teacher = 1). Passed by value to the kernels;
// pointers are device memory under CUDA and host memory on the CPU path. Counters only ever grow; the host
// takes deltas at each refresh. Group g's entries are runtime_bank[offsets[g] .. offsets[g+1]).
// Route table: pairs (pair_from[i], pair_to[i]) of area keys that follow each other on the reference route.
// Loop table: (loop_key[i], loop_x[i]) = area key and x of each SMB_BANK_ROUTE_LOOP entry.
enum {SMB_TEACHER_EVENTS=5};   // counter block: 4 per-group arrays + SMB_TEACHER_EVENTS event counters
struct SmbTeacherDevice {
    int enabled,groups,group_width,success_distance,attempt_frames,upstream;
    const float* cdf;const uint32_t* offsets;const uint32_t* area;const int32_t* section;
    // events[0] = natural-play failures credited, events[1] = maze loop-backs, events[2] = wrong exits,
    // events[3] = on-route loop-backs passed, events[4] = forward exits onto new ground (game and practice actors)
    uint32_t *selected,*attempts,*wins,*death_weights,*events;
    int pairs;const uint32_t *pair_from,*pair_to;
    int loops;const uint32_t* loop_key;const int32_t* loop_x;
};
// (a, b) is a route pair: area b follows area a on the reference route.
SMB_HD bool smb_route_pair(const SmbTeacherDevice* t,uint32_t a,uint32_t b) {
    for(int i=0;i<t->pairs;i++)if(t->pair_from[i]==a&&t->pair_to[i]==b)return true;
    return false;
}
// A loop-back from (area k, pre-loop x) is one the reference route takes: within 512 px after a flagged entry.
SMB_HD bool smb_route_loop(const SmbTeacherDevice* t,uint32_t k,int x) {
    for(int i=0;i<t->loops;i++)if(t->loop_key[i]==k&&x>=t->loop_x[i]&&x-t->loop_x[i]<=512)return true;
    return false;
}
// Area k is on the reference route (appears in any pair).
SMB_HD bool smb_route_known(const SmbTeacherDevice* t,uint32_t k) {
    for(int i=0;i<t->pairs;i++)if(t->pair_from[i]==k||t->pair_to[i]==k)return true;
    return false;
}
// Integer counter add shared by both paths. CPU envs step under OpenMP, so the host add is atomic as well
// (sequentially consistent, so a refresh that reads wins before attempts never sees more wins than attempts).
SMB_HD void smb_teacher_add(uint32_t* p,uint32_t v) {
#ifdef __CUDA_ARCH__
    atomicAdd(p,v);
#else
    __atomic_fetch_add(p,v,__ATOMIC_SEQ_CST);
#endif
}
// Teacher draw: a group from the float CDF, then a uniform entry inside it. Returns the group.
SMB_HD int smb_teacher_choose(SmbEpisode* e,const SmbTeacherDevice* t) {
    float u=(float)(smb_random(&e->rng)>>8)*(1.0f/16777216.0f);
    int g=teacher_sample(t->cdf,t->groups,u);
    uint32_t first=t->offsets[g],count=t->offsets[g+1]-first;
    e->scene=first+smb_random(&e->rng)%count;
    smb_teacher_add(&t->selected[g],1);
    return g;
}
// Scene load and episode/progress initialisation shared by the band and teacher resets.
SMB_HD int smb_practice_load(SmbLogic* s,SmbEpisode* e,SmbGameProgress* p,int stage,const SmbGameConfig* game,const SmbBankEntry* bank) {
    e->world=0;
    int fault=smb_scene_reset(s,&bank[e->scene].scene);
    e->frames=0;e->status=SMB_EPISODE_ACTIVE;e->episode_return=0;e->episodes++;
    e->start_world=s->ram[0x75f];e->start_level=s->ram[0x75c];
    *p={};p->stage=stage;p->mode=s->ram[0x770];p->lives=s->ram[0x75a];p->routine=s->ram[0xe];
    smb_game_track_progress(s,p,game);return fault;
}
SMB_HD int smb_practice_reset(SmbLogic* s,SmbEpisode* e,SmbGameProgress* p,SmbPracticeProgress* progress,
        const SmbPracticeConfig* c,const SmbGameConfig* game,const SmbBankEntry* bank) {
    int stage=c->stages[smb_random(&e->rng)%(unsigned)c->stage_count];
    int band=progress->level[stage];
    if(band&&(smb_random(&e->rng)>>8)*(1.0f/16777216.0f)<c->replay)
        band=(int)(smb_random(&e->rng)%(unsigned)band);
    progress->stage=stage;progress->sampled=band;
    uint32_t first=c->offsets[stage][band],count=c->offsets[stage][band+1]-first;
    e->scene=first+smb_random(&e->rng)%count;
    return smb_practice_load(s,e,p,stage,game,bank);
}
SMB_HD int smb_practice_result(SmbPracticeProgress* p,int won) {
    int stage=p->stage;if(p->sampled!=p->level[stage])return 0;
    p->attempts[stage]++;p->wins[stage]+=won!=0;
    if(p->attempts[stage]<8)return 0;
    int wins=p->wins[stage];p->attempts[stage]=p->wins[stage]=0;
    p->bad_windows[stage]=wins<2?p->bad_windows[stage]+1:0;
    if(wins>=6&&p->level[stage]+1<SMB_PRACTICE_BANDS){p->level[stage]++;return 1;}
    if(p->bad_windows[stage]>=2) {
        p->bad_windows[stage]=0;
        if(p->level[stage]){p->level[stage]--;return -1;}
    }
    return 0;
}
