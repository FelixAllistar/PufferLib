#include "pacing.h"
#include <math.h>
#include <string.h>

void swat_pacer_reset(SwatPacer* p) {
    memset(p,0,sizeof(*p));
}
void swat_pacer_clear_input(SwatPacer* p) {
    int phase=p->phase;
    swat_pacer_reset(p);
    p->phase=phase;
}
int swat_pace_units(float seconds) {
    if(!(seconds>0)) return 0;
    double units=floor((double)seconds*SWAT_PACE_UNITS+.5);
    return units>SWAT_PACE_MAX_FRAME ? SWAT_PACE_MAX_FRAME : (int)units;
}

// Presses the simulation reads as edges or short holds. Any frame that saw
// one since the previous tick reports it on the next tick. Selections keep
// the latest non-zero request for the same reason.
static void latch(SwatInput* l,const SwatInput* s) {
    l->fire|=s->fire; l->reload|=s->reload; l->jump|=s->jump; l->interact|=s->interact;
    l->selector|=s->selector; l->melee|=s->melee; l->command|=s->command; l->taser|=s->taser;
    l->pepper_spray|=s->pepper_spray; l->cancel_reload|=s->cancel_reload;
    if(s->weapon) l->weapon=s->weapon;
    if(s->throwable) l->throwable=s->throwable;
    if(s->door_tool) l->door_tool=s->door_tool;
}
static SwatInput tick_input(const SwatInput* sample,const SwatInput* l,double yaw,double pitch) {
    SwatInput in=*sample;
    in.fire=l->fire; in.reload=l->reload; in.jump=l->jump; in.interact=l->interact;
    in.selector=l->selector; in.melee=l->melee; in.command=l->command; in.taser=l->taser;
    in.pepper_spray=l->pepper_spray; in.cancel_reload=l->cancel_reload;
    in.weapon=l->weapon; in.throwable=l->throwable; in.door_tool=l->door_tool;
    in.yaw_delta=(float)yaw; in.pitch_delta=(float)pitch;
    return in;
}

int swat_pacer_frame(SwatPacer* p,int units,const SwatInput* sample,SwatInput ticks[SWAT_PACE_MAX_TICKS]) {
    if(units<0) units=0;
    if(units>SWAT_PACE_MAX_FRAME) units=SWAT_PACE_MAX_FRAME;
    latch(&p->latched,sample);
    // A zero-length frame covers no tick time; its look waits for the next tick.
    if(!units) { p->look_yaw+=sample->yaw_delta; p->look_pitch+=sample->pitch_delta; return 0; }
    int count=0,start=0;
    while(p->phase+units-start>=SWAT_PACE_TICK) {
        int end=start+SWAT_PACE_TICK-p->phase;
        p->look_yaw+=(double)sample->yaw_delta*(end-start)/units;
        p->look_pitch+=(double)sample->pitch_delta*(end-start)/units;
        ticks[count++]=tick_input(sample,&p->latched,p->look_yaw,p->look_pitch);
        p->look_yaw=p->look_pitch=0; p->phase=0; start=end;
        // Only a frame that continues past this tick also belongs to the next.
        memset(&p->latched,0,sizeof(p->latched));
        if(end<units) latch(&p->latched,sample);
    }
    p->look_yaw+=(double)sample->yaw_delta*(units-start)/units;
    p->look_pitch+=(double)sample->pitch_delta*(units-start)/units;
    p->phase+=units-start;
    return count;
}
