#pragma once
#include "retro.h"

// Viewer-only state. Training and ordinary viewing both use natural lives;
// --single-life remains an explicit viewer override.
struct RetroPlayback {
    bool single_life=false;
    bool waiting_respawn=false;
};

// Returns whether the viewer should clear recurrent policy state at an episode
// reset. Midpoint respawns keep the policy state, as in training.
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
    }
    return false;
}
