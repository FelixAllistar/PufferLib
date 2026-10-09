#pragma once
#include "task.h"

// Fixed actor pools make fraction a share of simulation frames, independent of
// the very different lengths of full-game and FPG episodes.
struct SmbGameConfig {
    float fpg_fraction=1,clear_reward=1,time_bonus=.1f,death_penalty=0;
    int max_frames=108000,fixed_stage=-1,terminate_on_clear=0,time_target=1800;
    int checkpoint_distance=128;
    float checkpoint_reward=.025f;
    uint32_t roots[32]={};
};
struct SmbProgressFrontier {uint32_t key;int x;};
struct SmbGameProgress {
    uint32_t rewarded_levels;
    int stage,mode,lives,level_start,clears,deaths,routine;
    int progress_pixels,checkpoints,frontier_count,last_x,tracking;
    uint32_t last_key;
    SmbProgressFrontier frontiers[256];
};
// Keep each loaded area's furthest position for the whole natural-life episode.
// The ROM's area data pointer identifies geometry; destination $0750 can change
// before Mario actually enters a pipe and must not identify the current area.
SMB_HD int smb_game_track_progress(const SmbLogic* s,SmbGameProgress* p,const SmbGameConfig* c) {
    const auto* m=s->ram;
    int stage=m[0x75f]*4+m[0x75c],data=m[0xe7]+256*m[0xe8];
    if(stage>=32||data<0x8000||m[0x770]!=1||m[0x772]!=3||m[0xe]!=8||m[0xb5]>1) {
        p->tracking=0;return 0;
    }
    uint32_t key=((uint32_t)stage<<16)|(uint32_t)data;
    int x=m[0x6d]*256+m[0x86];if(x>3400)x=3400;
    // First arrival (including respawn/pipe exit) establishes a baseline, never
    // rewards teleport distance. Large same-area jumps also establish a baseline.
    bool continuous=p->tracking&&p->last_key==key&&x-p->last_x<=16;
    p->tracking=1;p->last_key=key;p->last_x=x;
    for(int i=0;i<p->frontier_count;i++)if(p->frontiers[i].key==key) {
        int novel=x>p->frontiers[i].x?x-p->frontiers[i].x:0;
        if(x>p->frontiers[i].x)p->frontiers[i].x=x;
        if(!continuous)return 0;
        p->progress_pixels+=novel;
        int total=p->progress_pixels/c->checkpoint_distance,earned=total-p->checkpoints;
        p->checkpoints=total;return earned;
    }
    // If capacity is exhausted, suppress untracked-area rewards instead of
    // evicting old frontiers and allowing repeat collection.
    if(p->frontier_count<256)p->frontiers[p->frontier_count++]={key,x};
    return 0;
}
SMB_HD int smb_fpg_actor(int actor,int count,const SmbGameConfig* c) {
    int fpg=(int)(count*c->fpg_fraction+.5f);
    return actor>=count-fpg;
}
SMB_HD int smb_game_reset(SmbLogic* s,SmbEpisode* e,SmbGameProgress* p,
        const SmbGameConfig* c,const SmbBankEntry* bank) {
    int stage=c->fixed_stage<0?(int)(smb_random(&e->rng)%32):c->fixed_stage;
    e->scene=c->roots[stage];e->world=0;
    int fault=smb_generate(s,&bank[e->scene].scene,&e->rng,0);
    e->frames=0;e->status=SMB_EPISODE_ACTIVE;e->episode_return=0;e->episodes++;
    e->start_world=s->ram[0x75f];e->start_level=s->ram[0x75c];
    *p={};p->stage=stage;p->mode=s->ram[0x770];p->lives=s->ram[0x75a];p->routine=s->ram[0xe];
    smb_game_track_progress(s,p,c);
    return fault;
}
SMB_HD float smb_game_after_frame(const SmbLogic* s,SmbEpisode* e,
        SmbGameProgress* p,const SmbGameConfig* c) {
    const auto* m=s->ram;e->frames++;
    int stage=m[0x75f]*4+m[0x75c];
    bool victory=p->mode!=2&&m[0x770]==2;
    bool flag=(m[0xe]==4||m[0xe]==5)&&(p->routine!=4&&p->routine!=5);
    bool clear=flag||victory||stage>p->stage;
    float reward=c->checkpoint_reward*smb_game_track_progress(s,p,c);
    if(clear&&p->stage>=0&&p->stage<32&&!(p->rewarded_levels&(1u<<p->stage))) {
        p->rewarded_levels|=1u<<p->stage;p->clears++;
        int elapsed=e->frames-p->level_start;
        float speed=elapsed>c->time_target?(float)c->time_target/elapsed:1;
        reward+=c->clear_reward+c->time_bonus*speed;
        if(c->terminate_on_clear)e->status=SMB_EPISODE_SUCCESS;
    }
    if(p->lives!=255&&(m[0x75a]==255||m[0x75a]<p->lives)) {
        p->deaths++;reward-=c->death_penalty;
    }
    // Castle victory mode occurs in every world. Only 8-4 is a game win.
    if(victory&&p->stage==31)e->status=SMB_EPISODE_SUCCESS;
    if(!e->status&&(m[0x75a]==255||m[0x770]==3))e->status=SMB_EPISODE_DEAD;
    if(!e->status&&e->frames>=c->max_frames)e->status=SMB_EPISODE_TIMEOUT;
    if(stage!=p->stage)p->level_start=e->frames;
    p->stage=stage;p->mode=m[0x770];p->lives=m[0x75a];p->routine=m[0xe];
    e->episode_return+=reward;return reward;
}
