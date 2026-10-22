#pragma once
#include "game.h"

enum {SMB_PRACTICE_BANDS=4,SMB_BANK_PRACTICE=16,SMB_PRACTICE_BAND_SHIFT=8};
struct SmbPracticeConfig {
    float fraction=0,replay=.2f;
    int max_frames=6000,stage_count=0;
    uint32_t stages[32]={},offsets[32][SMB_PRACTICE_BANDS+1]={};
};
struct SmbPracticeProgress {
    uint8_t level[32],attempts[32],wins[32],bad_windows[32];
    int stage,sampled;
};
SMB_HD int smb_practice_actor(int actor,int count,const SmbGameConfig* game,const SmbPracticeConfig* c) {
    int fpg=(int)(count*game->fpg_fraction+.5f),practice=(int)(count*c->fraction+.5f);
    return actor>=count-fpg-practice&&actor<count-fpg;
}
SMB_HD int smb_practice_reset(SmbLogic* s,SmbEpisode* e,SmbGameProgress* p,SmbPracticeProgress* progress,
        const SmbPracticeConfig* c,const SmbGameConfig* game,const SmbBankEntry* bank) {
    int stage=c->stages[smb_random(&e->rng)%(unsigned)c->stage_count];
    int band=progress->level[stage];
    if(band&&(smb_random(&e->rng)>>8)*(1.0f/16777216.0f)<c->replay)
        band=(int)(smb_random(&e->rng)%(unsigned)band);
    progress->stage=stage;progress->sampled=band;
    uint32_t first=c->offsets[stage][band],count=c->offsets[stage][band+1]-first;
    e->scene=first+smb_random(&e->rng)%count;e->world=0;
    int fault=smb_scene_reset(s,&bank[e->scene].scene);
    e->frames=0;e->status=SMB_EPISODE_ACTIVE;e->episode_return=0;e->episodes++;
    e->start_world=s->ram[0x75f];e->start_level=s->ram[0x75c];
    *p={};p->stage=stage;p->mode=s->ram[0x770];p->lives=s->ram[0x75a];p->routine=s->ram[0xe];
    smb_game_track_progress(s,p,game);return fault;
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
