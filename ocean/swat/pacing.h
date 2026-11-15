#ifndef SWAT_PACING_H
#define SWAT_PACING_H
#include "controller.h"

#ifdef __cplusplus
extern "C" {
#endif

// Converts rendered frames into fixed 60 Hz simulation ticks. Time is kept in
// integer units of 1/14400 s, so 30, 48, 60, 72, 75, 90, 120, 144 and 240 Hz
// frames are exact and tick boundaries never drift with float rounding.
//
// Each frame supplies one sampled input. A tick that ends inside a frame takes
// that frame's held state (axes, stance, modes). Momentary presses seen in any
// frame since the previous tick are latched into the next tick, so a tap
// shorter than a tick is not lost at high frame rates. Look deltas are split
// between ticks in proportion to the frame time that falls inside each tick,
// so a tick's yaw and pitch depend on the time covered, not on how many
// frames were drawn.
#define SWAT_PACE_UNITS 14400
#define SWAT_PACE_TICK (SWAT_PACE_UNITS/60)
// Frames longer than this are clamped, as the viewer always has: a stall
// produces at most six catch-up ticks instead of a burst.
#define SWAT_PACE_MAX_FRAME (SWAT_PACE_UNITS/10)
#define SWAT_PACE_MAX_TICKS (SWAT_PACE_MAX_FRAME/SWAT_PACE_TICK)

typedef struct SwatPacer {
    int phase;                 // units already elapsed in the current tick
    double look_yaw,look_pitch; // look from frames inside the current tick
    SwatInput latched;         // momentary presses since the previous tick
} SwatPacer;

void swat_pacer_reset(SwatPacer* pacer);
// Drops pending look and latched presses but keeps the tick phase. Used when
// gameplay input pauses (menus, focus loss) while networked time continues.
void swat_pacer_clear_input(SwatPacer* pacer);
// Rounds a frame duration in seconds to pace units, clamped to
// [0, SWAT_PACE_MAX_FRAME].
int swat_pace_units(float seconds);
// Adds one frame of `units` duration (clamped as above) with its sampled
// input. Writes the inputs of the ticks that end within the frame to `ticks`
// and returns their count, at most SWAT_PACE_MAX_TICKS.
int swat_pacer_frame(SwatPacer* pacer,int units,const SwatInput* sample,SwatInput ticks[SWAT_PACE_MAX_TICKS]);

// Presentation only: places a per-tick position (the first-person eye) at the
// frame's time inside the current tick instead of at the latest tick. Without
// it a frame shows 0, 1 or 2 whole ticks of motion depending on where its end
// falls against the 60 Hz grid, so camera translation judders at any frame
// rate that is not exactly 60 Hz in phase (120 FPS limit, timer jitter,
// GPU-bound frames). The shown position trails the latest tick by 1-alpha
// ticks (at most one tick of display delay). `alpha` is pacer.phase divided
// by SWAT_PACE_TICK; a negative alpha (paused, networked client with its own
// prediction) shows `position` and restarts the blend from it. A new actor, a
// tick reset or gap beyond SWAT_PACE_MAX_TICKS, or a jump longer than `snap`
// metres per tick also restarts it.
typedef struct SwatTickBlend {
    b3Pos previous,current;
    int tick,span,actor;
    bool valid;
} SwatTickBlend;
static inline b3Pos swat_tick_blend(SwatTickBlend* b,int actor,int tick,b3Pos position,float alpha,float snap) {
    if(!(alpha>=0) || !b->valid || actor!=b->actor || tick<b->tick || tick-b->tick>SWAT_PACE_MAX_TICKS) {
        b->previous=b->current=position; b->tick=tick; b->span=1; b->actor=actor; b->valid=true;
        return position;
    }
    if(tick>b->tick) { b->previous=b->current; b->span=tick-b->tick; b->tick=tick; }
    b->current=position;
    if(b3Length(b3SubPos(position,b->previous))>snap*b->span) b->previous=position;
    if(alpha>1) alpha=1;
    // A frame that ran several catch-up ticks shows the time one tick before
    // the latest, between the last shown tick and the latest.
    return b3LerpPosition(b->previous,b->current,(b->span-1+alpha)/b->span);
}

#ifdef __cplusplus
}
#endif
#endif
