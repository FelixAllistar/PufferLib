#pragma once
// Experimental FPG timing reward. General Mario tasks do not include this file.
#include "../mario_sim/task.h"
#include <math.h>

struct FpgTimeEntry {
    uint32_t best_frames,successes,trials;
};
struct FpgTimeConfig {
    float bonus,scale,slack,power,run_speed,setup_frames;
    int goal_x;
};

SMB_HD float fpg_time_fallback(const SmbLogic* s,const FpgTimeConfig* cfg) {
    const auto* m=s->ram;int player=m[0x6d]*256+m[0x86],pole=-1;
    for(int k=0;k<6;k++)if(m[0xf+k]&&m[0x16+k]==0x30)
        pole=m[0x6e + k]*256+m[0x87+k];
    if(pole<0) {
        int last=m[0x725]*16+m[0x726]-((m[0x71f]&3)==0);
        for(int col=last-31;col<=last;col++)for(int row=2;row<=14;row++) {
            int tile=m[0x500+((col&16)?0xd0:0)+(row-2)*16+(col&15)];
            if((tile==0x24||tile==0x25)&&col*16>=player)pole=col*16;
        }
    }
    // The original pipe exit is more than two screens from the pole, so its
    // actor/tiles are not loaded yet. Use the experiment's known level endpoint.
    if(pole<0&&cfg->goal_x>0)pole=cfg->goal_x;
    // A rough target, not a physical lower bound. It never changes a reset.
    float distance=pole>player?(float)(pole-player):0.0f;
    return fmaxf(1.0f,distance/cfg->run_speed+cfg->setup_frames);
}
SMB_HD float fpg_time_target(const SmbLogic* s,uint32_t scene,
        const FpgTimeEntry* table,const FpgTimeConfig* cfg) {
    float estimate=table&&table[scene].best_frames?(float)table[scene].best_frames:fpg_time_fallback(s,cfg);
    return fmaxf(1.0f,cfg->scale*estimate+cfg->slack);
}
SMB_HD float fpg_time_score(int frames,float target,const FpgTimeConfig* cfg) {
    return powf(fminf(1.0f,target/(float)(frames>0?frames:1)),cfg->power);
}
SMB_HD float fpg_time_after_frame(const SmbLogic* s,SmbEpisode* e,const SmbTaskConfig* task,
        float target,const FpgTimeConfig* cfg) {
    float reward=smb_task_after_frame(s,e,task);
    if(e->status==SMB_EPISODE_SUCCESS) {
        float bonus=cfg->bonus*fpg_time_score(e->frames,target,cfg);
        e->episode_return+=bonus;reward+=bonus;
    }
    return reward;
}
