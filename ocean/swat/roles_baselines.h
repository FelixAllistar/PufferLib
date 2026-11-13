// Scripted role baselines for the role task contract v3: honest opponents
// that act through the role task's own action space, so learned policies can
// be measured against them (roles_scripted in config/swat.ini; CONTRACT.md
// "Scripted baselines").
//
// Each baseline is a pure function of one agent's 153-float role observation
// and a per-episode trait seed: swat_baseline_act(obs, seed, action). This
// header deliberately includes only roles_layout.h, never the simulation, so
// a baseline cannot read hidden sim state; tests/test_roles_baselines.c
// compiles it without sim.h. Anything a baseline remembers between decisions
// lives in its own previous gaze choice, which the observation reports back
// (ray channels 0-1): gaze ray 0's pitch bin is a five-state memory cell.
//
// Behaviour (degrees are per decision at roles_repeat=4):
//   suspect   holds (standing) or ambushes (crouched, aiming at the nearest
//             visible door), turns onto and fires at visible officers,
//             reloads when empty, turns round when hit by an unseen shooter.
//             A "yielding" suspect (40% of episodes) sometimes surrenders
//             (comply) when it sees two or more officers, or one while hurt.
//             A "runner" (50%) heads for the staging point when
//             suspect_escape=1 and no officer is in sight.
//   civilian  complies (voluntary surrender) when an officer is within the
//             9 m command range; on seeing an armed suspect within 15 m or
//             taking damage it panics for the rest of the episode and either
//             freezes crouched (60%) or sprints for the exit (40%), steering
//             away from the suspect.
//   officer   advances on the role objective (extraction) with simple ray
//             avoidance and door opening, engages non-compliant suspects in
//             view (alternating compliance shout and semi shot while the
//             reticle is on them), never fires with a civilian or compliant
//             person on the reticle, cuffs people whose hands are up but
//             who are not yet cuffed (suspects first), and scans in place
//             once at extraction.
// Actions are always legal under the role mask: held states (dead,
// surrendered, restrained, stunned) give neutral motor heads, and each role
// only uses its own heads.
#ifndef SWAT_ROLES_BASELINES_H
#define SWAT_ROLES_BASELINES_H
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "roles_layout.h"

// Self-observation indices the baselines read (CONTRACT.md observation table).
enum {
    SWAT_SELF_PITCH_SIN=5, SWAT_SELF_HEALTH=13, SWAT_SELF_ALIVE=14,
    SWAT_SELF_OFFICER=15, SWAT_SELF_SUSPECT=16, SWAT_SELF_CIVILIAN=17,
    SWAT_SELF_SURRENDERED=18, SWAT_SELF_RESTRAINED=19, SWAT_SELF_STUNNED=20,
    SWAT_SELF_CUFFING=22, SWAT_SELF_SLOT=23, SWAT_SELF_MAGAZINE=24, SWAT_SELF_CHAMBERED=25,
    SWAT_SELF_RESERVE=26, SWAT_SELF_RELOAD=27, SWAT_SELF_SELECTOR=29,
    SWAT_SELF_PREV_TRIGGER=35, SWAT_SELF_GOAL_FORWARD=38, SWAT_SELF_GOAL_RIGHT=39,
    SWAT_SELF_GOAL_DISTANCE=41, SWAT_SELF_SUSPECTS_HELD=42, SWAT_SELF_CIVILIANS_HELD=43, SWAT_SELF_TAKEN=44
};
enum {
    SWAT_SEEN_NONE, SWAT_SEEN_SOLID, SWAT_SEEN_DESTRUCTIBLE, SWAT_SEEN_DOOR,
    SWAT_SEEN_OFFICER, SWAT_SEEN_SUSPECT, SWAT_SEEN_CIVILIAN
};
// Fixed baseline fan: gaze rays at yaw bins -80,-50,-25,-10,+10,+25,+50,+80
// (the reticle covers 0), level unless noted.
static const int swat_baseline_fan[SWAT_GAZE_RAYS]={0,1,2,3,5,6,7,8};
#define SWAT_BASELINE_IDLE 2             // memory cell value: nothing pending
#define SWAT_BASELINE_COMMAND_RANGE 9.0f // sim command range (metres)
#define SWAT_BASELINE_REACH 1.6f         // cuff/door reach (sim: 1.7 m cuff, 2.2 m door)
#define SWAT_BASELINE_DEGREES (180.0f/3.14159265358979323846f)

typedef struct SwatBaselineRay {
    float yaw,pitch,depth; // degrees (+ right, + up) and metres
    int kind; bool compliant,restrained;
    // The person's hands as seen (SWAT_RAY_WEAPON): a weapon in hand, or
    // hands that cannot be seen. Neither is hands seen empty.
    bool armed,hands_hidden;
} SwatBaselineRay;
typedef struct SwatBaselineView {
    SwatBaselineRay ray[SWAT_ROLES_RAYS];
    int memory; // previous gaze ray 0 pitch bin, IDLE if not the baseline's fan
} SwatBaselineView;

static uint32_t swat_baseline_mix(uint32_t x) {
    x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; x^=x>>16;
    return x;
}
// Per-episode trait in [0,1) from the seed; trait k is independent of k'.
static float swat_baseline_trait(uint32_t seed,int k) {
    return (float)(swat_baseline_mix(seed^(uint32_t)(k+1)*0x9e3779b9u)>>8)*(1.0f/16777216.0f);
}
// Per-decision coin bits: a hash of the observation itself, so identical
// observations always give identical actions.
static uint32_t swat_baseline_hash(const float* obs,uint32_t seed) {
    uint32_t h=2166136261u^seed;
    for(int i=0;i<SWAT_ROLES_OBS;i++) { uint32_t bits; memcpy(&bits,&obs[i],sizeof(bits)); h=(h^bits)*16777619u; }
    return swat_baseline_mix(h);
}
static float swat_baseline_wrap(float degrees) {
    while(degrees>=180) degrees-=360;
    while(degrees<-180) degrees+=360;
    return degrees;
}
static int swat_baseline_pitch_index(float normalized) {
    for(int b=0;b<SWAT_GAZE_PITCH_BINS;b++) if(fabsf(swat_gaze_pitch_degrees[b]/45-normalized)<1e-3f) return b;
    return -1;
}

static void swat_baseline_read(const float* obs,SwatBaselineView* v) {
    static const int classes[]={SWAT_RAY_SOLID,SWAT_RAY_DESTRUCTIBLE,SWAT_RAY_DOOR,
        SWAT_RAY_OFFICER,SWAT_RAY_SUSPECT,SWAT_RAY_CIVILIAN};
    for(int r=0;r<SWAT_ROLES_RAYS;r++) {
        const float* ray=obs+SWAT_ROLES_SELF+r*SWAT_ROLES_RAY_CHANNELS;
        SwatBaselineRay* out=&v->ray[r];
        out->yaw=ray[SWAT_RAY_YAW]*90; out->pitch=ray[SWAT_RAY_PITCH]*45;
        out->kind=SWAT_SEEN_NONE;
        for(int c=0;c<6;c++) if(ray[classes[c]]>.5f) out->kind=SWAT_SEEN_SOLID+c;
        out->depth=out->kind ? ray[SWAT_RAY_DEPTH]*SWAT_ROLES_RANGE : SWAT_ROLES_RANGE;
        // Surrendered reads 1 and restrained 0.5 (roles_layout.h).
        out->compliant=ray[SWAT_RAY_COMPLIANT]>.25f;
        out->restrained=out->compliant && ray[SWAT_RAY_COMPLIANT]<.75f;
        // A weapon in hand reads 1, hidden hands 0.5, empty hands 0 (v4).
        out->armed=ray[SWAT_RAY_WEAPON]>.75f;
        out->hands_hidden=ray[SWAT_RAY_WEAPON]>.25f && !out->armed;
    }
    const float* first=obs+SWAT_ROLES_SELF+SWAT_ROLES_RAY_CHANNELS;
    int bin=swat_baseline_pitch_index(first[SWAT_RAY_PITCH]);
    bool fan=fabsf(first[SWAT_RAY_YAW]-swat_gaze_yaw_degrees[swat_baseline_fan[0]]/90)<1e-3f;
    v->memory=fan && bin>=0 ? bin : SWAT_BASELINE_IDLE;
}

// Nearest ray showing `kind`; compliant: 0 only non-compliant, 1 only
// compliant, -1 either. The reticle wins ties, so a target already on the
// reticle stays the target.
static int swat_baseline_pick(const SwatBaselineView* v,int kind,int compliant) {
    int best=-1;
    for(int r=0;r<SWAT_ROLES_RAYS;r++) {
        const SwatBaselineRay* ray=&v->ray[r];
        if(ray->kind!=kind || (compliant>=0 && ray->compliant!=(compliant!=0))) continue;
        if(best<0 || ray->depth<v->ray[best].depth-.25f) best=r;
    }
    return best;
}
// Nearest ray showing a person of `kind` with hands up and no cuffs.
static int swat_baseline_pick_uncuffed(const SwatBaselineView* v,int kind) {
    int best=-1;
    for(int r=0;r<SWAT_ROLES_RAYS;r++) {
        const SwatBaselineRay* ray=&v->ray[r];
        if(ray->kind!=kind || !ray->compliant || ray->restrained) continue;
        if(best<0 || ray->depth<v->ray[best].depth-.25f) best=r;
    }
    return best;
}
// Distinct people of `kind` in view: rays adjacent in yaw whose depths agree
// within 1 m are taken to be the same person.
static int swat_baseline_count(const SwatBaselineView* v,int kind) {
    int order[SWAT_ROLES_RAYS];
    for(int r=0;r<SWAT_ROLES_RAYS;r++) {
        int i=r; while(i>0 && v->ray[order[i-1]].yaw>v->ray[r].yaw) { order[i]=order[i-1]; i--; }
        order[i]=r;
    }
    int count=0; const SwatBaselineRay* previous=NULL;
    for(int i=0;i<SWAT_ROLES_RAYS;i++) {
        const SwatBaselineRay* ray=&v->ray[order[i]];
        if(ray->kind!=kind) { previous=NULL; continue; }
        if(!previous || fabsf(ray->depth-previous->depth)>=1.0f) count++;
        previous=ray;
    }
    return count;
}
// Depth along the gaze ray nearest `yaw` (open space ahead in that direction).
static float swat_baseline_open(const SwatBaselineView* v,float yaw) {
    int best=0;
    for(int r=1;r<SWAT_ROLES_RAYS;r++) if(fabsf(v->ray[r].yaw-yaw)<fabsf(v->ray[best].yaw-yaw)) best=r;
    return v->ray[best].depth;
}

// Yaw head for a turn toward an offset: 24 deg or 4 deg per decision.
static int swat_baseline_yaw_bin(float degrees) {
    if(degrees>=15) return 4;
    if(degrees<=-15) return 0;
    if(degrees>=2) return 3;
    if(degrees<=-2) return 1;
    return 2;
}
// Pitch head for an offset: 16 deg or 2 deg per decision.
static int swat_baseline_pitch_bin(float degrees) {
    if(degrees>=8) return 4;
    if(degrees<=-8) return 0;
    if(degrees>=1) return 3;
    if(degrees<=-1) return 1;
    return 2;
}
// Pitch head that levels the view.
static int swat_baseline_level(const float* obs) {
    float sine=fmaxf(-1,fminf(1,obs[SWAT_SELF_PITCH_SIN]));
    return swat_baseline_pitch_bin(-asinf(sine)*SWAT_BASELINE_DEGREES);
}
// Heading of the role objective (degrees, + right).
static float swat_baseline_goal(const float* obs) {
    return atan2f(obs[SWAT_SELF_GOAL_RIGHT],obs[SWAT_SELF_GOAL_FORWARD])*SWAT_BASELINE_DEGREES;
}

// Walk toward a heading (degrees, + right) with door opening and simple
// avoidance of anything closer than 1 m straight ahead.
static void swat_baseline_walk(const SwatBaselineView* v,float heading,int gait,uint32_t coin,float* action) {
    const SwatBaselineRay* ahead=&v->ray[0];
    if(ahead->kind==SWAT_SEEN_DOOR && ahead->depth<SWAT_BASELINE_REACH) action[SWAT_ROLE_HEAD_INTERACT]=(float)(coin&1);
    bool blocked=ahead->kind!=SWAT_SEEN_NONE && ahead->depth<1.0f;
    if(blocked) {
        float left=swat_baseline_open(v,-25)+swat_baseline_open(v,-50);
        float right=swat_baseline_open(v,25)+swat_baseline_open(v,50);
        heading=right>=left ? 40 : -40;
        action[SWAT_ROLE_HEAD_STRAFE]=heading>0 ? 2 : 0;
    }
    action[SWAT_ROLE_HEAD_YAW]=(float)swat_baseline_yaw_bin(heading);
    if(!blocked && fabsf(heading)<45) { action[SWAT_ROLE_HEAD_FORWARD]=2; action[SWAT_ROLE_HEAD_GAIT]=(float)gait; }
}

// Officer/suspect weapon upkeep: selector off safe, reload when empty (or
// low with nothing in view), sidearm when the primary is dry. A reload in
// progress is held to the end. Holding reload keeps the agent from
// sprinting, which would interrupt it, and when a wound interrupts it the
// held input counts as a new press, so it restarts on the next tick.
static void swat_baseline_weapon(const float* obs,bool engaged,uint32_t coin,float* action) {
    bool empty=obs[SWAT_SELF_MAGAZINE]<=0 && obs[SWAT_SELF_CHAMBERED]<.5f;
    if(obs[SWAT_SELF_SELECTOR]<=0) action[SWAT_ROLE_HEAD_SELECTOR]=(float)((coin>>1)&1);
    if(obs[SWAT_SELF_RELOAD]>0) { action[SWAT_ROLE_HEAD_RELOAD]=1; action[SWAT_ROLE_HEAD_TRIGGER]=0; }
    else if(obs[SWAT_SELF_RESERVE]>0 && (empty || (!engaged && obs[SWAT_SELF_MAGAZINE]<.34f))) {
        action[SWAT_ROLE_HEAD_RELOAD]=(float)((coin>>2)&1);
        if(action[SWAT_ROLE_HEAD_RELOAD]>0) action[SWAT_ROLE_HEAD_TRIGGER]=0;
    } else if(empty && obs[SWAT_SELF_RESERVE]<=0 && obs[SWAT_SELF_SLOT]<.5f) action[SWAT_ROLE_HEAD_WEAPON]=2;
}

// Turn onto a person and fire (semi: one press every other decision) while
// the reticle is on a non-compliant person of `kind`. A target seen on a
// +-10 deg ray falls between rays after a 4 deg turn, so the memory cell
// keeps turning that way for two more decisions. Returns true when the
// reticle is on the target.
static bool swat_baseline_engage(const float* obs,const SwatBaselineView* v,int target,int kind,
                                 float* action,int* memory) {
    action[SWAT_ROLE_HEAD_AIM]=1;
    if(v->ray[0].kind==kind && !v->ray[0].compliant) {
        action[SWAT_ROLE_HEAD_TRIGGER]=obs[SWAT_SELF_PREV_TRIGGER]<.5f;
        return true;
    }
    const SwatBaselineRay* t=&v->ray[target];
    action[SWAT_ROLE_HEAD_YAW]=(float)swat_baseline_yaw_bin(t->yaw);
    action[SWAT_ROLE_HEAD_PITCH]=(float)swat_baseline_pitch_bin(t->pitch);
    if(t->yaw>=2 && t->yaw<15) *memory=4;
    else if(t->yaw<=-2 && t->yaw>-15) *memory=0;
    return false;
}
// No target in view: finish a pending small turn from the memory cell
// (0,1 left; 3,4 right; each decision steps one closer to idle). Returns
// true while a turn is pending.
static bool swat_baseline_follow(const SwatBaselineView* v,float* action,int* memory) {
    if(v->memory==SWAT_BASELINE_IDLE) return false;
    bool right=v->memory>SWAT_BASELINE_IDLE;
    action[SWAT_ROLE_HEAD_YAW]=right ? 3 : 1;
    *memory=v->memory+(right ? -1 : 1);
    return true;
}

static int swat_baseline_officer(const float* obs,const SwatBaselineView* v,uint32_t seed,uint32_t coin,float* action) {
    int memory=SWAT_BASELINE_IDLE;
    int threat=swat_baseline_pick(v,SWAT_SEEN_SUSPECT,0);
    if(threat>=0) {
        bool on=swat_baseline_engage(obs,v,threat,SWAT_SEEN_SUSPECT,action,&memory);
        // Compliance shout on the decisions the trigger rests (both need a
        // rising edge, so they alternate), within command range.
        if(on && v->ray[0].depth<SWAT_BASELINE_COMMAND_RANGE && action[SWAT_ROLE_HEAD_TRIGGER]<.5f)
            action[SWAT_ROLE_HEAD_COMMAND]=1;
        swat_baseline_weapon(obs,true,coin,action);
        return memory;
    }
    // Shout at a non-compliant civilian on the reticle (the sim's command
    // rule makes commanded civilians surrender).
    if(v->ray[0].kind==SWAT_SEEN_CIVILIAN && !v->ray[0].compliant && v->ray[0].depth<SWAT_BASELINE_COMMAND_RANGE)
        action[SWAT_ROLE_HEAD_COMMAND]=(float)((coin>>3)&1);
    // Cuff people whose hands are up but who are not cuffed yet, suspects
    // first; the ray shows cuffs, so nobody is cuffed twice.
    int cuff=swat_baseline_pick_uncuffed(v,SWAT_SEEN_SUSPECT);
    if(cuff<0) cuff=swat_baseline_pick_uncuffed(v,SWAT_SEEN_CIVILIAN);
    if(cuff>=0) {
        const SwatBaselineRay* c=&v->ray[cuff];
        if(cuff==0 && c->depth<SWAT_BASELINE_REACH) {
            // Keep holding while the cuff progresses; otherwise re-press.
            action[SWAT_ROLE_HEAD_INTERACT]=obs[SWAT_SELF_CUFFING]>0 ? 1 : (float)(coin&1);
            return memory;
        }
        if(cuff!=0) {
            action[SWAT_ROLE_HEAD_PITCH]=(float)swat_baseline_pitch_bin(c->pitch);
            action[SWAT_ROLE_HEAD_YAW]=(float)swat_baseline_yaw_bin(c->yaw);
        }
        if(fabsf(c->yaw)<15) { action[SWAT_ROLE_HEAD_FORWARD]=2; action[SWAT_ROLE_HEAD_GAIT]=0; }
        return memory;
    }
    swat_baseline_weapon(obs,false,coin,action);
    action[SWAT_ROLE_HEAD_PITCH]=(float)swat_baseline_level(obs);
    // Hit by an unseen shooter: turn round to find them.
    if(obs[SWAT_SELF_TAKEN]>0) { action[SWAT_ROLE_HEAD_YAW]=4; return memory; }
    if(swat_baseline_follow(v,action,&memory)) return memory;
    float distance=obs[SWAT_SELF_GOAL_DISTANCE]*SWAT_ROLES_RANGE;
    if(distance<1.5f) {                              // at extraction: scan
        action[SWAT_ROLE_HEAD_YAW]=(float)(((coin>>4)&1) ? 4 : 2);
        return memory;
    }
    swat_baseline_walk(v,swat_baseline_goal(obs),1,coin,action);
    return memory;
}

static int swat_baseline_suspect(const float* obs,const SwatBaselineView* v,uint32_t seed,uint32_t coin,float* action) {
    int memory=SWAT_BASELINE_IDLE;
    bool yielding=swat_baseline_trait(seed,1)<.4f,ambusher=swat_baseline_trait(seed,2)<.5f;
    bool runner=swat_baseline_trait(seed,3)<.5f;
    int officers=swat_baseline_count(v,SWAT_SEEN_OFFICER);
    bool hurt=obs[SWAT_SELF_HEALTH]<.5f;
    // Sometimes surrender when outnumbered (about one decision in four).
    if(yielding && (officers>=2 || (hurt && officers>=1)) && ((coin>>8)&3)==0) {
        action[SWAT_ROLE_HEAD_COMPLY]=1;
        return memory;
    }
    if(ambusher) action[SWAT_ROLE_HEAD_CROUCH]=1;
    int target=swat_baseline_pick(v,SWAT_SEEN_OFFICER,0);
    if(target>=0) {
        swat_baseline_engage(obs,v,target,SWAT_SEEN_OFFICER,action,&memory);
        swat_baseline_weapon(obs,true,coin,action);
        return memory;
    }
    swat_baseline_weapon(obs,false,coin,action);
    action[SWAT_ROLE_HEAD_PITCH]=(float)swat_baseline_level(obs);
    if(obs[SWAT_SELF_TAKEN]>0) { action[SWAT_ROLE_HEAD_YAW]=4; return memory; }
    if(swat_baseline_follow(v,action,&memory)) return memory;
    // The objective is the staging point only with suspect_escape=1.
    if(runner && obs[SWAT_SELF_GOAL_DISTANCE]>0) {
        action[SWAT_ROLE_HEAD_CROUCH]=0;
        swat_baseline_walk(v,swat_baseline_goal(obs),2,coin,action);
        return memory;
    }
    if(ambusher) {
        // Ambush: aim at the nearest visible door within 12 m.
        action[SWAT_ROLE_HEAD_AIM]=1;
        int door=swat_baseline_pick(v,SWAT_SEEN_DOOR,-1);
        if(door>0 && v->ray[door].depth<12) action[SWAT_ROLE_HEAD_YAW]=(float)swat_baseline_yaw_bin(v->ray[door].yaw);
    }
    return memory;
}

static int swat_baseline_civilian(const float* obs,const SwatBaselineView* v,uint32_t seed,uint32_t coin,float* action) {
    bool fleer=swat_baseline_trait(seed,4)<.4f;
    int officer=swat_baseline_pick(v,SWAT_SEEN_OFFICER,-1);
    if(officer>=0 && v->ray[officer].depth<SWAT_BASELINE_COMMAND_RANGE) {
        action[SWAT_ROLE_HEAD_COMPLY]=1;
        return SWAT_BASELINE_IDLE;
    }
    int threat=swat_baseline_pick(v,SWAT_SEEN_SUSPECT,0);
    bool panic=v->memory==0 || obs[SWAT_SELF_TAKEN]>0 || (threat>=0 && v->ray[threat].depth<15);
    if(!panic) return SWAT_BASELINE_IDLE;
    // Memory cell 0: panicked for the rest of the episode.
    if(!fleer || obs[SWAT_SELF_GOAL_DISTANCE]*SWAT_ROLES_RANGE<1.0f) {
        action[SWAT_ROLE_HEAD_CROUCH]=1;
        return 0;
    }
    float heading=swat_baseline_goal(obs);
    if(threat>=0 && fabsf(swat_baseline_wrap(heading-v->ray[threat].yaw))<40)
        heading=swat_baseline_wrap(v->ray[threat].yaw+180);
    swat_baseline_walk(v,heading,2,coin,action);
    action[SWAT_ROLE_HEAD_PITCH]=(float)swat_baseline_level(obs);
    return 0;
}

// The baseline action for one agent: a pure function of its role
// observation and per-episode trait seed. Writes all SWAT_ROLES_HEADS heads.
static void swat_baseline_act(const float obs[SWAT_ROLES_OBS],uint32_t seed,float action[SWAT_ROLES_HEADS]) {
    for(int h=0;h<SWAT_ROLES_MOTOR_HEADS;h++) action[h]=(float)swat_roles_neutral[h];
    int memory=SWAT_BASELINE_IDLE;
    uint32_t coin=swat_baseline_hash(obs,seed);
    bool held=obs[SWAT_SELF_ALIVE]<.5f || obs[SWAT_SELF_SURRENDERED]>.5f ||
        obs[SWAT_SELF_RESTRAINED]>.5f || obs[SWAT_SELF_STUNNED]>0;
    if(!held) {
        SwatBaselineView v; swat_baseline_read(obs,&v);
        if(obs[SWAT_SELF_OFFICER]>.5f) memory=swat_baseline_officer(obs,&v,seed,coin,action);
        else if(obs[SWAT_SELF_SUSPECT]>.5f) memory=swat_baseline_suspect(obs,&v,seed,coin,action);
        else if(obs[SWAT_SELF_CIVILIAN]>.5f) memory=swat_baseline_civilian(obs,&v,seed,coin,action);
    }
    // Gaze: the fixed fan; the +-10 rays dip to -15 deg on about half the
    // decisions to catch crouched people; ray 0's pitch is the memory cell.
    bool low=(coin>>12)&1;
    for(int k=0;k<SWAT_GAZE_RAYS;k++) {
        action[SWAT_ROLE_HEAD_GAZE+2*k]=(float)swat_baseline_fan[k];
        action[SWAT_ROLE_HEAD_GAZE+2*k+1]=(float)(low && (k==3 || k==4) ? 1 : SWAT_GAZE_PITCH_BINS/2);
    }
    action[SWAT_ROLE_HEAD_GAZE+1]=(float)memory;
}
#endif
