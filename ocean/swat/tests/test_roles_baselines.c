// Scripted role baselines on synthetic observations. This file includes only
// roles_baselines.h (which includes only roles_layout.h): no sim.h, no
// world, no actors. It compiling at all is the check that the baselines use
// nothing but the role observation. Covers legality under the role masks,
// determinism/purity, and each role's behaviour rules.
#include "../roles_baselines.h"
#include <assert.h>
#include <stdio.h>

#ifdef SWAT_ROLES_ENV_H
#error "baseline tests must not include the role env or the simulation"
#endif

static const int sizes[]=SWAT_ROLES_ACTION_SIZES;
enum { OFFICER, SUSPECT, CIVILIAN };

static int offset(int head) { int o=0; for(int h=0;h<head;h++) o+=sizes[h]; return o; }
static int head_of(int kind) {
    return kind==SWAT_SEEN_SOLID ? SWAT_RAY_SOLID : kind==SWAT_SEEN_DESTRUCTIBLE ? SWAT_RAY_DESTRUCTIBLE :
        kind==SWAT_SEEN_DOOR ? SWAT_RAY_DOOR : kind==SWAT_SEEN_OFFICER ? SWAT_RAY_OFFICER :
        kind==SWAT_SEEN_SUSPECT ? SWAT_RAY_SUSPECT : SWAT_RAY_CIVILIAN;
}

// An alive, unhurt agent of `role` with the baseline's own fan from the
// previous decision, every ray empty, a full semi-auto weapon.
static void make(int role,float* obs) {
    memset(obs,0,SWAT_ROLES_OBS*sizeof(float));
    obs[4]=1; obs[6]=1; obs[7]=1; obs[12]=1; obs[13]=1; obs[SWAT_SELF_ALIVE]=1;
    obs[SWAT_SELF_OFFICER+role]=1; obs[37]=.5f;
    if(role!=CIVILIAN) { obs[SWAT_SELF_MAGAZINE]=1; obs[SWAT_SELF_CHAMBERED]=1; obs[SWAT_SELF_RESERVE]=1; obs[SWAT_SELF_SELECTOR]=.5f; }
    if(role==OFFICER) { obs[SWAT_SELF_GOAL_FORWARD]=.5f; obs[SWAT_SELF_GOAL_DISTANCE]=.5f; }
    if(role==CIVILIAN) { obs[SWAT_SELF_GOAL_FORWARD]=-.3f; obs[SWAT_SELF_GOAL_DISTANCE]=.3f; }
    for(int r=0;r<SWAT_ROLES_RAYS;r++) {
        float* ray=obs+SWAT_ROLES_SELF+r*SWAT_ROLES_RAY_CHANNELS;
        if(r) { ray[SWAT_RAY_YAW]=swat_gaze_yaw_degrees[swat_baseline_fan[r-1]]/90; ray[SWAT_RAY_PITCH]=0; }
        ray[SWAT_RAY_DEPTH]=1;
    }
}
// Ray r (0 reticle, 1-8 the fan rays at -80,-50,-25,-10,10,25,50,80) sees `kind`.
// compliant: hands up (surrendered) when true, free when false.
static void see(float* obs,int r,int kind,float metres,bool compliant) {
    float* ray=obs+SWAT_ROLES_SELF+r*SWAT_ROLES_RAY_CHANNELS;
    for(int c=SWAT_RAY_SOLID;c<=SWAT_RAY_CIVILIAN;c++) ray[c]=0;
    ray[head_of(kind)]=1; ray[SWAT_RAY_DEPTH]=metres/SWAT_ROLES_RANGE;
    ray[SWAT_RAY_COMPLIANT]=compliant ? SWAT_RAY_SURRENDERED_VALUE : 0; ray[SWAT_RAY_INTEGRITY]=1;
}
static void cuffed(float* obs,int r) { obs[SWAT_ROLES_SELF+r*SWAT_ROLES_RAY_CHANNELS+SWAT_RAY_COMPLIANT]=SWAT_RAY_RESTRAINED_VALUE; }
static void memory(float* obs,int bin) {
    obs[SWAT_ROLES_SELF+SWAT_ROLES_RAY_CHANNELS+SWAT_RAY_PITCH]=swat_gaze_pitch_degrees[bin]/45;
}
static int act(const float* obs,uint32_t seed,float* action) {
    swat_baseline_act(obs,seed,action);
    return (int)action[SWAT_ROLE_HEAD_GAZE+1];
}

// The role task's legal-action rules (CONTRACT.md), rebuilt from the
// observation alone, as the env's mask builds them from the same state.
static void mask_for(const float* obs,unsigned char* mask) {
    memset(mask,1,SWAT_ROLES_LOGITS);
    bool held=obs[SWAT_SELF_ALIVE]<.5f || obs[SWAT_SELF_SURRENDERED]>.5f || obs[SWAT_SELF_RESTRAINED]>.5f || obs[SWAT_SELF_STUNNED]>0;
    for(int h=0;h<SWAT_ROLES_MOTOR_HEADS;h++) {
        int only=-1;
        if(held) only=swat_roles_neutral[h];
        else if(h==SWAT_ROLE_HEAD_COMMAND && obs[SWAT_SELF_OFFICER]<.5f) only=0;
        else if(h==SWAT_ROLE_HEAD_COMPLY && obs[SWAT_SELF_OFFICER]>.5f) only=0;
        else if(obs[SWAT_SELF_CIVILIAN]>.5f && (h==SWAT_ROLE_HEAD_AIM || h==SWAT_ROLE_HEAD_TRIGGER ||
            h==SWAT_ROLE_HEAD_RELOAD || h==SWAT_ROLE_HEAD_WEAPON || h==SWAT_ROLE_HEAD_SELECTOR)) only=0;
        if(only>=0) { memset(mask+offset(h),0,(size_t)sizes[h]); mask[offset(h)+only]=1; }
    }
}
static void assert_legal(const float* obs,const float* action) {
    unsigned char mask[SWAT_ROLES_LOGITS]; mask_for(obs,mask);
    for(int h=0;h<SWAT_ROLES_HEADS;h++) {
        float v=action[h]; int i=(int)v;
        assert(isfinite(v) && (float)i==v && i>=0 && i<sizes[h]);
        assert(mask[offset(h)+i]);
    }
}

static uint32_t rng_state=12345;
static uint32_t next(void) { rng_state^=rng_state<<13; rng_state^=rng_state>>17; rng_state^=rng_state<<5; return rng_state; }
static float unit(void) { return (float)(next()>>8)*(1.0f/16777216.0f); }

static void legality(void) {
    float obs[SWAT_ROLES_OBS],action[SWAT_ROLES_HEADS];
    int counts[3]={0};
    for(int trial=0;trial<20000;trial++) {
        int role=(int)(next()%3); make(role,obs);
        for(int i=0;i<SWAT_ROLES_SELF;i++) if(i<15 || i>17) obs[i]=unit()*2-1;
        obs[SWAT_SELF_ALIVE]=next()%8!=0;
        obs[SWAT_SELF_SURRENDERED]=next()%6==0; obs[SWAT_SELF_RESTRAINED]=next()%8==0;
        obs[SWAT_SELF_STUNNED]=next()%8==0 ? unit() : 0;
        for(int r=0;r<SWAT_ROLES_RAYS;r++) {
            float* ray=obs+SWAT_ROLES_SELF+r*SWAT_ROLES_RAY_CHANNELS;
            ray[SWAT_RAY_YAW]=r ? swat_gaze_yaw_degrees[next()%SWAT_GAZE_YAW_BINS]/90 : 0;
            ray[SWAT_RAY_PITCH]=r ? swat_gaze_pitch_degrees[next()%SWAT_GAZE_PITCH_BINS]/45 : 0;
            ray[SWAT_RAY_DEPTH]=1;
            int kind=(int)(next()%7);
            if(kind) see(obs,r,kind,unit()*30,next()%3==0);
        }
        swat_baseline_act(obs,next(),action);
        assert_legal(obs,action);
        counts[role]++;
    }
    // Inactive padding and dead agents observe (mostly) zeros: neutral, legal.
    memset(obs,0,sizeof(obs)); swat_baseline_act(obs,7,action); assert_legal(obs,action);
    for(int h=0;h<SWAT_ROLES_MOTOR_HEADS;h++) assert(action[h]==swat_roles_neutral[h]);
    printf("PASS baselines legality: 20000 random observations (%d officer, %d suspect, %d civilian) legal under role masks\n",
        counts[0],counts[1],counts[2]);
}

static void purity(void) {
    float a[SWAT_ROLES_OBS],b[SWAT_ROLES_OBS],first[SWAT_ROLES_HEADS],other[SWAT_ROLES_HEADS],again[SWAT_ROLES_HEADS];
    for(int role=0;role<3;role++) for(int trial=0;trial<200;trial++) {
        make(role,a); make((role+1)%3,b);
        see(a,(int)(next()%9),1+(int)(next()%6),unit()*20,false); a[37]=unit();
        uint32_t seed=next();
        float copy[SWAT_ROLES_OBS]; memcpy(copy,a,sizeof(a));
        swat_baseline_act(a,seed,first);
        assert(!memcmp(a,copy,sizeof(a)));        // the observation is read-only
        swat_baseline_act(b,seed^1,other);        // no hidden state carried between calls
        swat_baseline_act(a,seed,again);
        assert(!memcmp(first,again,sizeof(first)));
    }
    puts("PASS baselines purity: identical (obs, seed) give identical actions; obs untouched; no state between calls");
}

static void suspect(void) {
    float obs[SWAT_ROLES_OBS],action[SWAT_ROLES_HEADS];
    uint32_t stubborn=0,yielding=0;
    for(uint32_t s=1;!stubborn || !yielding;s++) {
        if(swat_baseline_trait(s,1)>=.4f && !stubborn) stubborn=s;
        if(swat_baseline_trait(s,1)<.4f && !yielding) yielding=s;
    }
    // Officer on the reticle: aim and fire on a fresh press, rest after one.
    make(SUSPECT,obs); see(obs,0,SWAT_SEEN_OFFICER,8,false);
    act(obs,stubborn,action); assert_legal(obs,action);
    assert(action[SWAT_ROLE_HEAD_AIM]==1 && action[SWAT_ROLE_HEAD_TRIGGER]==1 && action[SWAT_ROLE_HEAD_YAW]==2);
    assert(action[SWAT_ROLE_HEAD_COMPLY]==0 && action[SWAT_ROLE_HEAD_COMMAND]==0);
    obs[SWAT_SELF_PREV_TRIGGER]=1; act(obs,stubborn,action); assert(action[SWAT_ROLE_HEAD_TRIGGER]==0);
    // Officer at +25: big right turn, no shot yet.
    make(SUSPECT,obs); see(obs,6,SWAT_SEEN_OFFICER,8,false);
    act(obs,stubborn,action); assert(action[SWAT_ROLE_HEAD_YAW]==4 && action[SWAT_ROLE_HEAD_TRIGGER]==0);
    // Officer at -10: small left turn, and the memory cell keeps turning left
    // for two more decisions once the officer falls between rays.
    make(SUSPECT,obs); see(obs,4,SWAT_SEEN_OFFICER,8,false);
    assert(act(obs,stubborn,action)==0 && action[SWAT_ROLE_HEAD_YAW]==1);
    make(SUSPECT,obs); memory(obs,0);
    assert(act(obs,stubborn,action)==1 && action[SWAT_ROLE_HEAD_YAW]==1);
    make(SUSPECT,obs); memory(obs,1);
    assert(act(obs,stubborn,action)==SWAT_BASELINE_IDLE && action[SWAT_ROLE_HEAD_YAW]==1);
    make(SUSPECT,obs);
    assert(act(obs,stubborn,action)==SWAT_BASELINE_IDLE && action[SWAT_ROLE_HEAD_FORWARD]==1);
    // Empty magazine: reload (on a coin) and never pull the trigger with it.
    int reloads=0;
    for(int t=0;t<64;t++) {
        make(SUSPECT,obs); obs[SWAT_SELF_MAGAZINE]=0; obs[SWAT_SELF_CHAMBERED]=0; obs[37]=(float)t/64;
        act(obs,stubborn,action); reloads+=action[SWAT_ROLE_HEAD_RELOAD]==1;
        assert(!(action[SWAT_ROLE_HEAD_RELOAD]==1 && action[SWAT_ROLE_HEAD_TRIGGER]==1));
    }
    assert(reloads>8 && reloads<56);
    // Outnumbered by two officers: a yielding suspect sometimes complies, a
    // stubborn one never does; nobody complies facing one healthy officer.
    int yields=0,stubborn_yields=0,single=0;
    for(int t=0;t<128;t++) {
        make(SUSPECT,obs); obs[37]=(float)t/128;
        see(obs,3,SWAT_SEEN_OFFICER,5,false); see(obs,6,SWAT_SEEN_OFFICER,9,false);
        act(obs,yielding,action); yields+=action[SWAT_ROLE_HEAD_COMPLY]==1; assert_legal(obs,action);
        if(action[SWAT_ROLE_HEAD_COMPLY]==1) for(int h=0;h<SWAT_ROLE_HEAD_COMPLY;h++) assert(action[h]==swat_roles_neutral[h]);
        act(obs,stubborn,action); stubborn_yields+=action[SWAT_ROLE_HEAD_COMPLY]==1;
        make(SUSPECT,obs); obs[37]=(float)t/128; see(obs,3,SWAT_SEEN_OFFICER,5,false); see(obs,4,SWAT_SEEN_OFFICER,5.2f,false);
        act(obs,yielding,action); single+=action[SWAT_ROLE_HEAD_COMPLY]==1;  // one officer on two adjacent rays
    }
    assert(yields>8 && yields<80 && stubborn_yields==0 && single==0);
    // Surrendered: motor heads neutral.
    make(SUSPECT,obs); obs[SWAT_SELF_SURRENDERED]=1; see(obs,0,SWAT_SEEN_OFFICER,3,false);
    act(obs,stubborn,action); for(int h=0;h<SWAT_ROLES_MOTOR_HEADS;h++) assert(action[h]==swat_roles_neutral[h]);
    printf("PASS baselines suspect: fires on reticle, turns onto officers, memory turn, reload, yields %d/128 outnumbered\n",yields);
}

static void officer(void) {
    float obs[SWAT_ROLES_OBS],action[SWAT_ROLES_HEADS];
    // Threat on the reticle: shot and compliance shout alternate.
    make(OFFICER,obs); see(obs,0,SWAT_SEEN_SUSPECT,6,false);
    act(obs,1,action); assert_legal(obs,action);
    assert(action[SWAT_ROLE_HEAD_TRIGGER]==1 && action[SWAT_ROLE_HEAD_COMMAND]==0 && action[SWAT_ROLE_HEAD_COMPLY]==0);
    obs[SWAT_SELF_PREV_TRIGGER]=1; act(obs,1,action);
    assert(action[SWAT_ROLE_HEAD_TRIGGER]==0 && action[SWAT_ROLE_HEAD_COMMAND]==1);
    // Civilian on the reticle and a suspect at +25: turn, never fire.
    make(OFFICER,obs); see(obs,0,SWAT_SEEN_CIVILIAN,4,false); see(obs,6,SWAT_SEEN_SUSPECT,8,false);
    act(obs,1,action); assert(action[SWAT_ROLE_HEAD_TRIGGER]==0 && action[SWAT_ROLE_HEAD_YAW]==4);
    // Compliant suspect on the reticle: no shot.
    make(OFFICER,obs); see(obs,0,SWAT_SEEN_SUSPECT,4,true);
    act(obs,1,action); assert(action[SWAT_ROLE_HEAD_TRIGGER]==0);
    // ... within reach with hands up: cuff, holding once it progresses.
    make(OFFICER,obs); see(obs,0,SWAT_SEEN_SUSPECT,1.2f,true); obs[SWAT_SELF_CUFFING]=.3f;
    act(obs,1,action); assert(action[SWAT_ROLE_HEAD_INTERACT]==1 && action[SWAT_ROLE_HEAD_FORWARD]==1);
    // ... but never once the ray shows cuffs, on any coin.
    for(uint32_t seed=1;seed<64;seed++) {
        make(OFFICER,obs); see(obs,0,SWAT_SEEN_SUSPECT,1.2f,true); cuffed(obs,0); obs[37]=(float)seed/64;
        act(obs,seed,action); assert(action[SWAT_ROLE_HEAD_INTERACT]==0 && action[SWAT_ROLE_HEAD_TRIGGER]==0);
    }
    // A cuffed suspect beside one with hands up: the uncuffed one is cuffed.
    make(OFFICER,obs); see(obs,0,SWAT_SEEN_SUSPECT,1.2f,true); cuffed(obs,0); see(obs,6,SWAT_SEEN_SUSPECT,2,true);
    act(obs,1,action); assert(action[SWAT_ROLE_HEAD_YAW]==4 && action[SWAT_ROLE_HEAD_INTERACT]==0);
    // Nothing in view: walk toward the objective (straight ahead).
    make(OFFICER,obs); act(obs,1,action);
    assert(action[SWAT_ROLE_HEAD_FORWARD]==2 && action[SWAT_ROLE_HEAD_GAIT]==1 && action[SWAT_ROLE_HEAD_YAW]==2);
    // Objective behind and to the right: turn right in place.
    make(OFFICER,obs); obs[SWAT_SELF_GOAL_FORWARD]=-.2f; obs[SWAT_SELF_GOAL_RIGHT]=.1f;
    act(obs,1,action); assert(action[SWAT_ROLE_HEAD_YAW]==4 && action[SWAT_ROLE_HEAD_FORWARD]==1);
    // Wall straight ahead: no walking into it; turn toward the open side.
    make(OFFICER,obs); see(obs,0,SWAT_SEEN_SOLID,.6f,false); see(obs,2,SWAT_SEEN_SOLID,1,false); see(obs,3,SWAT_SEEN_SOLID,1,false);
    act(obs,1,action); assert(action[SWAT_ROLE_HEAD_FORWARD]!=2 && action[SWAT_ROLE_HEAD_YAW]==4);
    // At extraction: stay there and scan.
    for(uint32_t seed=1;seed<32;seed++) {
        make(OFFICER,obs); obs[SWAT_SELF_GOAL_DISTANCE]=.01f; obs[37]=(float)seed/32;
        act(obs,seed,action); assert(action[SWAT_ROLE_HEAD_FORWARD]==1 && (action[SWAT_ROLE_HEAD_YAW]==2 || action[SWAT_ROLE_HEAD_YAW]==4));
    }
    puts("PASS baselines officer: fire/shout alternation, civilian and compliant holds, cuffing only uncuffed people, objective walk, avoidance");
}

static void civilian(void) {
    float obs[SWAT_ROLES_OBS],action[SWAT_ROLES_HEADS];
    uint32_t freezer=0,fleer=0;
    for(uint32_t s=1;!freezer || !fleer;s++) {
        if(swat_baseline_trait(s,4)>=.4f && !freezer) freezer=s;
        if(swat_baseline_trait(s,4)<.4f && !fleer) fleer=s;
    }
    // Calm: stays put.
    make(CIVILIAN,obs); assert(act(obs,freezer,action)==SWAT_BASELINE_IDLE);
    for(int h=0;h<SWAT_ROLES_MOTOR_HEADS;h++) assert(action[h]==swat_roles_neutral[h]);
    // Officer within command range: comply.
    make(CIVILIAN,obs); see(obs,7,SWAT_SEEN_OFFICER,6,false);
    act(obs,freezer,action); assert(action[SWAT_ROLE_HEAD_COMPLY]==1); assert_legal(obs,action);
    // Armed suspect close by: the freezer crouches, the fleer runs away from it.
    make(CIVILIAN,obs); see(obs,0,SWAT_SEEN_SUSPECT,5,false);
    assert(act(obs,freezer,action)==0 && action[SWAT_ROLE_HEAD_CROUCH]==1 && action[SWAT_ROLE_HEAD_FORWARD]==1);
    assert_legal(obs,action);
    // The exit lies past the suspect, so the fleer turns about (heading -180).
    make(CIVILIAN,obs); obs[SWAT_SELF_GOAL_FORWARD]=.3f; see(obs,0,SWAT_SEEN_SUSPECT,5,false);
    assert(act(obs,fleer,action)==0 && action[SWAT_ROLE_HEAD_YAW]==0 && action[SWAT_ROLE_HEAD_FORWARD]!=2);
    assert_legal(obs,action);
    // Panic persists through the memory cell once the suspect is out of view:
    // the fleer sprints for the exit straight ahead.
    make(CIVILIAN,obs); obs[SWAT_SELF_GOAL_FORWARD]=.3f; memory(obs,0);
    assert(act(obs,fleer,action)==0 && action[SWAT_ROLE_HEAD_FORWARD]==2 && action[SWAT_ROLE_HEAD_GAIT]==2);
    make(CIVILIAN,obs); memory(obs,0); assert(act(obs,freezer,action)==0 && action[SWAT_ROLE_HEAD_CROUCH]==1);
    // Damage from an unseen shooter also panics.
    make(CIVILIAN,obs); obs[SWAT_SELF_TAKEN]=.1f; assert(act(obs,freezer,action)==0);
    puts("PASS baselines civilian: calm hold, comply near officers, freeze or flee, persistent panic");
}

int main(void) {
    legality();
    purity();
    suspect();
    officer();
    civilian();
    return 0;
}
