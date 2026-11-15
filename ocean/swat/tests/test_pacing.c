// Frame-to-tick pacing (pacing.h). Links pacing.c only: no physics.
// The same human input timeline, drawn at different frame rates, must give
// bit-identical 60 Hz tick inputs whenever the frames resolve every input
// change; otherwise tick timing and total look are still exact.
#include "pacing.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// The timeline changes every 1/30 s (480 units, two ticks). Its look rate is
// a multiple of 2^-20 rad per unit, so splitting a frame's look in halves or
// summing up to eight frames is exact in binary floating point.
#define SEGMENT 480
#define SEGMENTS 180
#define TICKS (SEGMENTS*SEGMENT/SWAT_PACE_TICK)
static SwatInput segment_input[SEGMENTS];
static int segment_yaw[SEGMENTS],segment_pitch[SEGMENTS];

static uint32_t next(uint32_t* s) { *s^=*s<<13; *s^=*s>>17; *s^=*s<<5; return *s; }
static void build_timeline(void) {
    uint32_t s=0x5eed1234u;
    for(int m=0;m<SEGMENTS;m++) {
        SwatInput in={0}; in.gait=SWAT_WALK;
        in.forward=(float)((int)(next(&s)%3)-1); in.strafe=(float)((int)(next(&s)%3)-1);
        in.lean=(float)((int)(next(&s)%3)-1);
        in.gait=(SwatGait)(next(&s)%3); in.crouch=next(&s)%4==0; in.aim=next(&s)%2;
        in.fire=next(&s)%3==0; in.reload=next(&s)%7==0; in.jump=next(&s)%9==0;
        in.interact=next(&s)%8==0; in.selector=next(&s)%11==0; in.cancel_reload=next(&s)%13==0;
        in.weapon=next(&s)%6==0 ? 1+(int)(next(&s)%2) : 0; in.ready=(int)(next(&s)%3);
        segment_input[m]=in;
        segment_yaw[m]=(int)(next(&s)%129)-64; segment_pitch[m]=(int)(next(&s)%33)-16;
    }
}
static const float look_unit=1.0f/1048576.0f; // 2^-20 rad per pace unit
// The input a frame starting at `start` samples, with the look of its duration.
static SwatInput frame_sample(int start,int units) {
    int m=start/SEGMENT; SwatInput in=segment_input[m];
    in.yaw_delta=segment_yaw[m]*look_unit*units; in.pitch_delta=segment_pitch[m]*look_unit*units;
    return in;
}
static void assert_same_input(const SwatInput* a,const SwatInput* b) {
    assert(!memcmp(&a->forward,&b->forward,sizeof(float)) && !memcmp(&a->strafe,&b->strafe,sizeof(float)));
    assert(!memcmp(&a->yaw_delta,&b->yaw_delta,sizeof(float)) && !memcmp(&a->pitch_delta,&b->pitch_delta,sizeof(float)));
    assert(!memcmp(&a->lean,&b->lean,sizeof(float)) && a->gait==b->gait && a->crouch==b->crouch && a->aim==b->aim);
    assert(a->fire==b->fire && a->reload==b->reload && a->jump==b->jump && a->interact==b->interact);
    assert(a->selector==b->selector && a->cancel_reload==b->cancel_reload && a->weapon==b->weapon && a->ready==b->ready);
}
// Ticks emitted by one frame schedule over the whole timeline. Returns the count.
typedef int (*Schedule)(int start,uint32_t* state);
// total_yaw sums the emitted ticks, sampled_yaw the frames' own look.
static int run(Schedule schedule,uint32_t seed,SwatInput out[TICKS+SWAT_PACE_MAX_TICKS],double* total_yaw,double* sampled_yaw) {
    SwatPacer pacer; swat_pacer_reset(&pacer);
    int count=0,time=0; uint32_t state=seed; *total_yaw=*sampled_yaw=0;
    while(time<SEGMENTS*SEGMENT) {
        int units=schedule(time,&state);
        SwatInput sample=frame_sample(time,units),ticks[SWAT_PACE_MAX_TICKS];
        int n=swat_pacer_frame(&pacer,units,&sample,ticks); *sampled_yaw+=sample.yaw_delta;
        // Every tick that ends by the frame's end is emitted, none early.
        assert(count+n==(time+units)/SWAT_PACE_TICK);
        for(int i=0;i<n;i++) { out[count++]=ticks[i]; *total_yaw+=ticks[i].yaw_delta; }
        time+=units;
    }
    return count;
}
static int fixed30(int start,uint32_t* s) { (void)start; (void)s; return 480; }
static int fixed60(int start,uint32_t* s) { (void)start; (void)s; return 240; }
static int fixed120(int start,uint32_t* s) { (void)start; (void)s; return 120; }
static int fixed240(int start,uint32_t* s) { (void)start; (void)s; return 60; }
// Uneven frames of 1/240..1/60 s that never cross a tick boundary.
static int variable(int start,uint32_t* s) {
    int units=60*(1+(int)(next(s)%4)),boundary=(start/SWAT_PACE_TICK+1)*SWAT_PACE_TICK;
    return start+units>boundary ? boundary-start : units;
}
// 144 Hz frames straddle input changes: only timing and look totals are exact.
static int fixed144(int start,uint32_t* s) { (void)start; (void)s; return 100; }

static SwatInput reference[TICKS],paced[TICKS+SWAT_PACE_MAX_TICKS];
static void frame_rates(void) {
    build_timeline();
    double expected_yaw=0;
    for(int k=0;k<TICKS;k++) {
        int m=k*SWAT_PACE_TICK/SEGMENT;
        reference[k]=segment_input[m];
        reference[k].yaw_delta=segment_yaw[m]*look_unit*SWAT_PACE_TICK;
        reference[k].pitch_delta=segment_pitch[m]*look_unit*SWAT_PACE_TICK;
        expected_yaw+=reference[k].yaw_delta;
    }
    Schedule exact[]={fixed30,fixed60,fixed120,fixed240,variable};
    const char* names[]={"30","60","120","240","variable 60-240"};
    for(int r=0;r<5;r++) for(uint32_t seed=1;seed<=(r==4 ? 8u : 1u);seed++) {
        double yaw,sampled; int count=run(exact[r],seed,paced,&yaw,&sampled);
        assert(count==TICKS);
        for(int k=0;k<TICKS;k++) assert_same_input(&paced[k],&reference[k]);
        assert(yaw==expected_yaw && sampled==expected_yaw);
        if(seed==1) printf("  %s Hz: %d ticks identical to the 60 Hz timeline\n",names[r],count);
    }
    // The timeline ends on a tick boundary, so no look is left pending.
    double yaw,sampled; int count=run(fixed144,1,paced,&yaw,&sampled);
    assert(count==TICKS && fabs(yaw-sampled)<1e-5);
    puts("PASS pacing frame rates: 30/60/120/240 Hz and uneven frames give bit-identical tick inputs; 144 Hz keeps tick timing and total look");
}

static void presses(void) {
    SwatPacer p; swat_pacer_reset(&p);
    SwatInput idle={0},tap={0},out[SWAT_PACE_MAX_TICKS];
    tap.fire=true; tap.weapon=2; tap.reload=true;
    // A 1/240 s tap between two ticks reaches the next tick, then releases.
    assert(swat_pacer_frame(&p,60,&idle,out)==0);
    assert(swat_pacer_frame(&p,60,&tap,out)==0);
    assert(swat_pacer_frame(&p,60,&idle,out)==0);
    assert(swat_pacer_frame(&p,60,&idle,out)==1 && out[0].fire && out[0].reload && out[0].weapon==2);
    for(int f=0;f<4;f++) { int n=swat_pacer_frame(&p,60,&idle,out); assert(n==(f==3)); if(n) assert(!out[0].fire && !out[0].weapon); }
    // A frame that straddles a tick boundary belongs to both ticks.
    swat_pacer_reset(&p);
    assert(swat_pacer_frame(&p,300,&tap,out)==1 && out[0].fire);
    assert(swat_pacer_frame(&p,180,&idle,out)==1 && out[0].fire && out[0].weapon==2);
    assert(swat_pacer_frame(&p,240,&idle,out)==1 && !out[0].fire);
    // Held state is the frame's own: a release is seen on the tick it ends in.
    swat_pacer_reset(&p);
    SwatInput held={0}; held.forward=1; held.aim=true;
    assert(swat_pacer_frame(&p,240,&held,out)==1 && out[0].forward==1 && out[0].aim);
    assert(swat_pacer_frame(&p,240,&idle,out)==1 && out[0].forward==0 && !out[0].aim);
    puts("PASS pacing presses: sub-tick taps latch into exactly one tick; straddling frames reach both ticks; held state follows the tick's frame");
}

static void limits(void) {
    assert(swat_pace_units(1.0f/60)==SWAT_PACE_TICK && swat_pace_units(1.0f/144)==100 && swat_pace_units(1.0f/240)==60);
    assert(swat_pace_units(0)==0 && swat_pace_units(-1)==0 && swat_pace_units(NAN)==0 && swat_pace_units(5)==SWAT_PACE_MAX_FRAME);
    SwatPacer p; swat_pacer_reset(&p);
    SwatInput look={0},out[SWAT_PACE_MAX_TICKS]; look.yaw_delta=.25f;
    // A stall is clamped to six ticks; the look of a zero-length frame waits.
    assert(swat_pacer_frame(&p,100000,&look,out)==SWAT_PACE_MAX_TICKS);
    for(int i=0;i<SWAT_PACE_MAX_TICKS;i++) assert(out[i].yaw_delta==.25f/6);
    assert(swat_pacer_frame(&p,0,&look,out)==0 && p.look_yaw==.25);
    SwatInput idle={0};
    assert(swat_pacer_frame(&p,240,&idle,out)==1 && out[0].yaw_delta==.25f);
    // Clearing input keeps the tick phase, for networked play behind a menu.
    assert(swat_pacer_frame(&p,100,&look,out)==0);
    swat_pacer_clear_input(&p); assert(p.phase==100 && p.look_yaw==0);
    assert(swat_pacer_frame(&p,140,&idle,out)==1 && out[0].yaw_delta==0);
    puts("PASS pacing limits: exact unit rounding, stall clamp, zero-length frames and input clearing");
}

int main(void) {
    frame_rates();
    presses();
    limits();
    return 0;
}
