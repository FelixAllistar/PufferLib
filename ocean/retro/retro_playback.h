#pragma once
#include "retro.h"

// Viewer-only state. Training always calls puf_step(), whose single-life
// episode boundary is unchanged. No emulator state is restored on a respawn.
struct RetroPlayback {
    bool single_life=false;
    bool waiting_respawn=false;
};

// Returns whether the viewer should clear its recurrent policy state before
// choosing the next action. Clear at the new life's first controllable frame,
// not repeatedly during the ROM's death/entrance animations.
static bool retro_playback_step(Env* e,RetroPlayback* playback) {
    int previous_life=e->life;
    retro_step(e,!playback->single_life);
    if(e->agents[0].terminals[0]) {
        playback->waiting_respawn=false;
        return true;
    }
    const unsigned char* m=e->emu->low_mem();
    if(robs_dead(m)||e->life<previous_life) playback->waiting_respawn=true;
    bool ready=m[0x770]==1&&m[0x772]==3&&m[0xe]==8&&!robs_dying(m);
    if(playback->waiting_respawn&&ready) {
        playback->waiting_respawn=false;
        return true;
    }
    return false;
}
