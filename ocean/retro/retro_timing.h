#pragma once
#include <cmath>
#include <cstddef>
#include <cstdio>

// NTSC ROM + NTSC core, with a strict image allowlist in retro_rom_identity.h.
// Frame-derived console time, independent of inference, rendering or pauses.
// This is not a claim of human-run/leaderboard eligibility.
static constexpr bool RETRO_RTA_COMPARABLE=true;
static constexpr double RETRO_NTSC_FPS=60.0988138974405;
static double retro_frame_seconds(double frames) { return frames/RETRO_NTSC_FPS; }
static void retro_clock_text(char* out,size_t size,double frames) {
    long long ms=llround(retro_frame_seconds(frames)*1000.0);
    snprintf(out,size,"%lld:%02lld.%03lld",ms/60000,(ms/1000)%60,ms%1000);
}

// RAM event convention: periwinkle's SMB1 LiveSplit autosplitter, default
// black-screen splits with artificial reaction delay disabled.
// https://github.com/periwinkle9/smb-autosplitter/blob/main/SuperMarioBros.asl
// Start is entry into player control. Intermediate warp splits use next-area
// entrance; the full-game finish is the 8-4 victory-mode edge (axe contact).
struct RetroRtaClock {
    int world,stage,start_offset,previous_split_tick;
    int split_frames,split_total_frames,split_level,first_split_frames;
    int final_tick;
};
static void retro_rta_reset(RetroRtaClock* clock,int world,int stage,int offset) {
    *clock={}; clock->world=world; clock->stage=stage;
    clock->start_offset=offset; clock->previous_split_tick=-offset;
    clock->split_level=-1; clock->final_tick=-1;
}
static int retro_rta_elapsed(const RetroRtaClock& clock,int tick) {
    return (clock.final_tick>=0?clock.final_tick:tick)+clock.start_offset;
}
static bool retro_rta_update(RetroRtaClock* clock,int tick,const unsigned char* m,
        int previous_routine,int previous_screen_timer,int previous_mode) {
    if(clock->final_tick>=0) return false;
    int world=m[0x75f]+1,stage=m[0x75c]+1;
    bool advanced=world>clock->world||(world==clock->world&&stage>clock->stage);
    bool finished=clock->world==8&&clock->stage==4&&m[0x770]==2&&previous_mode!=2;
    bool split=finished;
    if(advanced) {
        bool warp=world>clock->world+1||(world==clock->world+1&&clock->stage<4);
        split=warp?((m[0xe]==7||m[0xe]==8)&&previous_routine<m[0xe])
                  :(previous_screen_timer==0&&m[0x7a0]>=6);
    }
    if(!split) return false;
    clock->split_frames=tick-clock->previous_split_tick;
    clock->split_total_frames=tick+clock->start_offset;
    clock->split_level=(clock->world-1)*4+clock->stage-1;
    if(!clock->first_split_frames) clock->first_split_frames=clock->split_total_frames;
    clock->previous_split_tick=tick; clock->world=world; clock->stage=stage;
    if(finished) clock->final_tick=tick;
    return true;
}
