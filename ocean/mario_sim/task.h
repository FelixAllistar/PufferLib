#pragma once
#include "bank.h"
#include "generator.h"

enum {SMB_TASK_FREE=0,SMB_TASK_FPG=1,SMB_TASK_CLEAR=2};
enum {SMB_EPISODE_ACTIVE=0,SMB_EPISODE_SUCCESS=1,SMB_EPISODE_DEAD=2,
      SMB_EPISODE_TIMEOUT=3,SMB_EPISODE_NORMAL_FLAG=4};
typedef struct {
    uint32_t seed,knobs;
    int max_frames,task,fixed_stage,world_count;
} SmbTaskConfig;
typedef struct {
    uint32_t rng,episodes,scene,world;
    int frames,status,start_world,start_level;
    float episode_return;
} SmbEpisode;

SMB_HD int smb_action_buttons(int action) {return (action&3)|((action&60)<<2);}
SMB_HD int smb_task_reset(SmbLogic* s,SmbEpisode* e,const SmbTaskConfig* cfg,
        const SmbBankEntry* bank,const uint32_t* eligible,int eligible_count) {
    e->scene=eligible[smb_random(&e->rng)%(unsigned)eligible_count];
    e->world=smb_random(&e->rng)%(unsigned)cfg->world_count;
    int result=smb_generate(s,&bank[e->scene].scene,&e->rng,cfg->knobs);
    e->frames=0;e->status=SMB_EPISODE_ACTIVE;e->episode_return=0;e->episodes++;
    e->start_world=s->ram[0x75f];e->start_level=s->ram[0x75c];
    return result;
}
SMB_HD float smb_task_after_frame(const SmbLogic* s,SmbEpisode* e,const SmbTaskConfig* cfg) {
    const auto* m=s->ram;e->frames++;
    if(cfg->task==SMB_TASK_FPG) {
        if(m[0xe]==5&&m[0x70f]>=162&&m[0xd4]==48)e->status=SMB_EPISODE_SUCCESS;
        else if(m[0xe]==4||m[0xe]==5)e->status=SMB_EPISODE_NORMAL_FLAG;
    } else if(cfg->task==SMB_TASK_CLEAR) {
        if(m[0xe]==4||m[0xe]==5||m[0x770]==2||m[0x75f]!=e->start_world||m[0x75c]!=e->start_level)
            e->status=SMB_EPISODE_SUCCESS;
    }
    if(!e->status&&(m[0xe]==11||m[0x770]==3))e->status=SMB_EPISODE_DEAD;
    if(!e->status&&e->frames>=cfg->max_frames)e->status=SMB_EPISODE_TIMEOUT;
    float reward=e->status==SMB_EPISODE_SUCCESS?1.0f:0.0f;e->episode_return+=reward;return reward;
}
