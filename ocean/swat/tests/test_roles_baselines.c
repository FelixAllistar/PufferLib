// Scripted role baselines on synthetic observations. This file includes only
// roles_baselines.h (which includes only roles_layout.h): no sim.h, no
// world, no actors. It compiling at all is the check that the baselines use
// nothing but the role observation. Covers legality under the role masks,
// determinism/purity, and each role's behaviour rules. The motel officer
// (roles_motel_officer.h) is checked on a synthetic plan the same way.
#include "../roles_baselines.h"
#include "../roles_motel_officer.h"
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
// compliant: hands up (surrendered) when true, free when false. The weapon
// channel shows what the env shows: officers and free suspects hold one.
static void see(float* obs,int r,int kind,float metres,bool compliant) {
    float* ray=obs+SWAT_ROLES_SELF+r*SWAT_ROLES_RAY_CHANNELS;
    for(int c=SWAT_RAY_SOLID;c<=SWAT_RAY_CIVILIAN;c++) ray[c]=0;
    ray[head_of(kind)]=1; ray[SWAT_RAY_DEPTH]=metres/SWAT_ROLES_RANGE;
    ray[SWAT_RAY_COMPLIANT]=compliant ? SWAT_RAY_SURRENDERED_VALUE : 0; ray[SWAT_RAY_INTEGRITY]=1;
    ray[SWAT_RAY_WEAPON]=kind==SWAT_SEEN_OFFICER || (kind==SWAT_SEEN_SUSPECT && !compliant);
}
static void cuffed(float* obs,int r) {
    obs[SWAT_ROLES_SELF+r*SWAT_ROLES_RAY_CHANNELS+SWAT_RAY_COMPLIANT]=SWAT_RAY_RESTRAINED_VALUE;
    obs[SWAT_ROLES_SELF+r*SWAT_ROLES_RAY_CHANNELS+SWAT_RAY_WEAPON]=0;
}
// The person on ray r has dropped the weapon (empty hands, not yet up).
static void empty_handed(float* obs,int r) { obs[SWAT_ROLES_SELF+r*SWAT_ROLES_RAY_CHANNELS+SWAT_RAY_WEAPON]=0; }
// The person on ray r is seen, but their hands are not (contract v4).
static void hands_hidden(float* obs,int r) { obs[SWAT_ROLES_SELF+r*SWAT_ROLES_RAY_CHANNELS+SWAT_RAY_WEAPON]=.5f; }
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


// A synthetic static plan: open floor (every cell linked to its neighbours),
// two 4x6 m rooms side by side with front doors on z=0 facing +z, and
// extraction 10 m out in front.
static void plan(SwatMotelMap* m) {
    memset(m,0,sizeof(*m));
    m->x0=-15; m->z0=-12;
    for(int c=0;c<SWAT_MOTEL_MAP_CELLS;c++) for(int d=0;d<4;d++) if(swat_motel_neighbour(c,d)>=0) m->links[c]|=(unsigned char)(1u<<d);
    m->extraction[0]=0; m->extraction[1]=10;
    m->rooms=2;
    m->room[0][0]=-3; m->room[0][1]=-3; m->room[0][2]=1.9f; m->room[0][3]=2.9f;
    m->room[1][0]=3;  m->room[1][1]=-3; m->room[1][2]=1.9f; m->room[1][3]=2.9f;
    m->doors=2;
    m->door[0][0]=-3; m->door[0][1]=0; m->door[0][2]=0; m->door[0][3]=1;
    m->door[1][0]=3;  m->door[1][1]=0; m->door[1][2]=0; m->door[1][3]=1;
}
// An officer observation at (x,z) with heading yaw (radians): the objective
// vector to extraction in its own frame, as the env writes obs 38-41.
static void place_officer(float* obs,const SwatMotelMap* m,float x,float z,float yaw) {
    make(OFFICER,obs);
    float c=cosf(yaw),s=sinf(yaw),dx=m->extraction[0]-x,dz=m->extraction[1]-z;
    obs[3]=s; obs[4]=c;
    obs[SWAT_SELF_GOAL_FORWARD]=(dx*c+dz*s)/SWAT_ROLES_RANGE; obs[SWAT_SELF_GOAL_RIGHT]=(-dx*s+dz*c)/SWAT_ROLES_RANGE;
    obs[SWAT_SELF_GOAL_DISTANCE]=sqrtf(dx*dx+dz*dz)/SWAT_ROLES_RANGE;
}
static void motel_officer(void) {
    SwatMotelMap m; plan(&m);
    float obs[SWAT_ROLES_OBS],action[SWAT_ROLES_HEADS];
    // Localization: position and heading come back from the observation.
    for(int k=0;k<64;k++) {
        float x=unit()*20-10,z=unit()*16-10,yaw=unit()*6.2f-3.1f,lx,lz,lyaw;
        place_officer(obs,&m,x,z,yaw); swat_motel_locate(obs,&m,&lx,&lz,&lyaw);
        assert(fabsf(lx-x)<1e-3f && fabsf(lz-z)<1e-3f && fabsf(swat_baseline_wrap((lyaw-yaw)*SWAT_BASELINE_DEGREES))<1e-2f);
    }
    // Routes: two officers split the rooms east to west; each room is a
    // stack point outside its door facing it, then the room centre; every
    // route ends at extraction. A full sweep covers both rooms.
    SwatMotelOfficer o[2];
    for(int n=0;n<2;n++) swat_motel_officer_begin(&o[n],&m,n,2,4);
    for(int n=0;n<2;n++) {
        assert(o[n].route_length==3 && o[n].route[0].kind==SWAT_MOTEL_STACK && o[n].route[1].kind==SWAT_MOTEL_CLEAR &&
               o[n].route[2].kind==SWAT_MOTEL_EXTRACT);
        float room=n==0 ? 3 : -3;
        assert(fabsf(o[n].route[0].x-room)<.31f && fabsf(o[n].route[0].z-1.3f)<.31f && fabsf(o[n].route[0].face+90)<1e-3f);
        assert(fabsf(o[n].route[1].x-room)<.31f && fabsf(o[n].route[1].z+3)<.31f);
    }
    SwatMotelOfficer full=o[0]; swat_motel_route(&full,&m,true); assert(full.route_length==5);
    // Legality: random observations, state carried across decisions.
    SwatMotelOfficer state; swat_motel_officer_begin(&state,&m,0,2,4);
    for(int trial=0;trial<20000;trial++) {
        place_officer(obs,&m,unit()*20-10,unit()*16-10,unit()*6.2f-3.1f);
        for(int i=0;i<SWAT_ROLES_SELF;i++) if((i<15 || i>17) && (i<3 || i>4) && (i<38 || i>41)) obs[i]=unit()*2-1;
        obs[SWAT_SELF_ALIVE]=next()%8!=0; obs[SWAT_SELF_SURRENDERED]=next()%16==0;
        obs[SWAT_SELF_STUNNED]=next()%16==0 ? unit() : 0;
        for(int r=0;r<SWAT_ROLES_RAYS;r++) {
            int kind=(int)(next()%7);
            if(kind) see(obs,r,kind,unit()*30,next()%3==0);
            if(kind && next()%4==0) cuffed(obs,r);
        }
        swat_motel_officer_act(&state,&m,obs,next(),action);
        assert_legal(obs,action);
    }
    // Engagement under the rules of engagement (roe.h). A free suspect on
    // the reticle within command range is challenged first: a command, no
    // shot. The officer then holds fire, and calls nothing more (a new
    // command would restart the suspect's clock), until the challenge has
    // gone unanswered for longer than SWAT_ROE_CHALLENGE_TICKS at 4 ticks
    // per decision. Officers call only on odd decisions and fire only on
    // even ones, so no command (its own or a teammate's) lands in a
    // decision that fires. Every shot follows a confirming call at the
    // target in the decision before: a suspect who has dropped the weapon
    // always complies with it. Nobody compliant or protected on the reticle
    // is ever fired on.
    swat_motel_officer_begin(&state,&m,0,2,4);
    place_officer(obs,&m,3,1.3f,-1.5708f); see(obs,0,SWAT_SEEN_SUSPECT,4,false); obs[SWAT_SELF_PREV_TRIGGER]=0;
    int challenged=-1,first=-1,shots=0,previous=0;
    for(int t=0;t<60;t++) {
        swat_motel_officer_act(&state,&m,obs,(uint32_t)t,action);
        obs[SWAT_SELF_PREV_TRIGGER]=action[SWAT_ROLE_HEAD_TRIGGER];
        assert(action[SWAT_ROLE_HEAD_AIM]==1);
        bool call=action[SWAT_ROLE_HEAD_COMMAND]==1,shot=action[SWAT_ROLE_HEAD_TRIGGER]==1;
        assert(!(call && shot) && (!call || (t&1)) && (!shot || !(t&1)));
        if(call && challenged<0) challenged=t;
        else if(call) assert(first<0 ? (t-challenged)*4>SWAT_ROE_CHALLENGE_TICKS : 1);
        if(shot) { assert(previous==1); shots++; if(first<0) first=t; }
        previous=call;
    }
    assert(challenged==1 && first>0 && (first-challenged)*4>SWAT_ROE_CHALLENGE_TICKS &&
           (first-challenged)*4<=SWAT_ROE_CHALLENGE_TICKS+16 && shots>=5);
    // A free suspect beyond command range cannot be challenged: no shot
    // while the officer closes in, however long it calls out.
    swat_motel_officer_begin(&state,&m,0,2,4);
    place_officer(obs,&m,3,1.3f,-1.5708f); see(obs,0,SWAT_SEEN_SUSPECT,12,false);
    for(int t=0;t<40;t++) {
        swat_motel_officer_act(&state,&m,obs,(uint32_t)t,action);
        assert(action[SWAT_ROLE_HEAD_TRIGGER]==0 && state.challenge==0);
    }
    // Hurt, it returns fire on the free suspect on its reticle without
    // waiting out a challenge (a suspect seen firing is a lethal threat):
    // a confirming call on the next odd decision, then the shot.
    swat_motel_officer_begin(&state,&m,0,2,4);
    place_officer(obs,&m,3,1.3f,-1.5708f); see(obs,0,SWAT_SEEN_SUSPECT,4,false);
    obs[SWAT_SELF_PREV_TRIGGER]=0; obs[SWAT_SELF_TAKEN]=.3f;
    swat_motel_officer_act(&state,&m,obs,1,action);
    assert(action[SWAT_ROLE_HEAD_TRIGGER]==0 && action[SWAT_ROLE_HEAD_COMMAND]==0);
    obs[SWAT_SELF_TAKEN]=0;
    swat_motel_officer_act(&state,&m,obs,2,action);
    assert(action[SWAT_ROLE_HEAD_TRIGGER]==0 && action[SWAT_ROLE_HEAD_COMMAND]==1);
    swat_motel_officer_act(&state,&m,obs,3,action);
    assert(action[SWAT_ROLE_HEAD_TRIGGER]==1 && action[SWAT_ROLE_HEAD_COMMAND]==0);
    // A challenged suspect who puts their hands up is not fired on, even
    // after the wait; a different free suspect seen elsewhere must be
    // challenged anew.
    swat_motel_officer_begin(&state,&m,0,2,4);
    place_officer(obs,&m,3,1.3f,-1.5708f); see(obs,0,SWAT_SEEN_SUSPECT,4,false);
    for(int t=0;t<2;t++) swat_motel_officer_act(&state,&m,obs,(uint32_t)t,action);
    assert(state.challenge==1);
    see(obs,0,SWAT_SEEN_SUSPECT,4,true);
    for(int t=0;t<40;t++) { swat_motel_officer_act(&state,&m,obs,(uint32_t)t,action); assert(action[SWAT_ROLE_HEAD_TRIGGER]==0); }
    swat_motel_officer_begin(&state,&m,0,2,4);
    place_officer(obs,&m,3,1.3f,-1.5708f); see(obs,0,SWAT_SEEN_SUSPECT,4,false);
    for(int t=0;t<30;t++) swat_motel_officer_act(&state,&m,obs,(uint32_t)t,action);
    assert(state.challenge>1);
    see(obs,0,SWAT_SEEN_SUSPECT,7,false); obs[SWAT_SELF_PREV_TRIGGER]=0;
    for(int t=0;t<2;t++) {
        swat_motel_officer_act(&state,&m,obs,(uint32_t)t,action);
        assert(action[SWAT_ROLE_HEAD_TRIGGER]==0);
    }
    assert(state.challenge==1);
    // Nobody whose ray shows no weapon in hand is ever fired on: an
    // empty-handed free suspect is challenged but never shot, after the
    // wait or when the officer is hurt.
    swat_motel_officer_begin(&state,&m,0,2,4);
    place_officer(obs,&m,3,1.3f,-1.5708f); see(obs,0,SWAT_SEEN_SUSPECT,4,false); empty_handed(obs,0);
    obs[SWAT_SELF_PREV_TRIGGER]=0; challenged=-1;
    for(int t=0;t<60;t++) {
        obs[SWAT_SELF_TAKEN]=t%10==0 ? .2f : 0;
        swat_motel_officer_act(&state,&m,obs,(uint32_t)t,action);
        assert(action[SWAT_ROLE_HEAD_TRIGGER]==0);
        if(action[SWAT_ROLE_HEAD_COMMAND]==1 && challenged<0) challenged=t;
    }
    obs[SWAT_SELF_TAKEN]=0;
    assert(challenged==1 && state.challenge>1);
    // A suspect holding a weapon is the one challenged, even with an
    // empty-handed suspect nearer on the reticle: the officer turns onto
    // the armed one (25 deg right) and calls only once it is on the reticle.
    swat_motel_officer_begin(&state,&m,0,2,4);
    place_officer(obs,&m,3,1.3f,-1.5708f);
    see(obs,0,SWAT_SEEN_SUSPECT,3,false); empty_handed(obs,0); see(obs,6,SWAT_SEEN_SUSPECT,5,false);
    for(int t=0;t<4;t++) {
        swat_motel_officer_act(&state,&m,obs,(uint32_t)t,action);
        assert(action[SWAT_ROLE_HEAD_YAW]==4 && action[SWAT_ROLE_HEAD_TRIGGER]==0 && state.challenge==0);
    }
    see(obs,0,SWAT_SEEN_SUSPECT,5,false); see(obs,6,SWAT_SEEN_SUSPECT,3,false); empty_handed(obs,6);
    swat_motel_officer_act(&state,&m,obs,4,action);
    assert(action[SWAT_ROLE_HEAD_COMMAND]==0 && state.challenge==0);
    swat_motel_officer_act(&state,&m,obs,5,action);
    assert(action[SWAT_ROLE_HEAD_COMMAND]==1 && state.challenge==1);
    // A weapon dropped after the challenge: the wait runs out, but no shot.
    for(int t=6;t<40;t++) {
        if(t==20) empty_handed(obs,0);
        swat_motel_officer_act(&state,&m,obs,(uint32_t)t,action);
        if(t>=20) assert(action[SWAT_ROLE_HEAD_TRIGGER]==0);
    }
    float* side=obs+SWAT_ROLES_SELF+6*SWAT_ROLES_RAY_CHANNELS;
    for(int c=SWAT_RAY_DEPTH;c<SWAT_ROLES_RAY_CHANNELS;c++) side[c]=c==SWAT_RAY_DEPTH;
    // Hands that cannot be seen (weapon channel 0.5, contract v4): the
    // suspect is challenged and covered like an armed one, preferred over an
    // empty-handed one nearer on the reticle, but never fired on, after the
    // wait or when the officer is hurt, and the challenge is not repeated.
    // Once the reticle shows the weapon in hand, fire follows.
    swat_motel_officer_begin(&state,&m,0,2,4);
    place_officer(obs,&m,3,1.3f,-1.5708f);
    see(obs,0,SWAT_SEEN_SUSPECT,3,false); empty_handed(obs,0); see(obs,6,SWAT_SEEN_SUSPECT,5,false); hands_hidden(obs,6);
    swat_motel_officer_act(&state,&m,obs,0,action);
    assert(action[SWAT_ROLE_HEAD_YAW]==4 && action[SWAT_ROLE_HEAD_TRIGGER]==0 && state.challenge==0);
    for(int c=SWAT_RAY_DEPTH;c<SWAT_ROLES_RAY_CHANNELS;c++) side[c]=c==SWAT_RAY_DEPTH;
    swat_motel_officer_begin(&state,&m,0,2,4);
    place_officer(obs,&m,3,1.3f,-1.5708f); see(obs,0,SWAT_SEEN_SUSPECT,3,false); hands_hidden(obs,0);
    obs[SWAT_SELF_PREV_TRIGGER]=0; challenged=-1;
    int armed_fire=-1,repeats=0;
    for(int t=0;t<70;t++) {
        obs[SWAT_SELF_TAKEN]=t<50 && t%10==0 ? .2f : 0;
        if(t==50) see(obs,0,SWAT_SEEN_SUSPECT,3,false);
        swat_motel_officer_act(&state,&m,obs,(uint32_t)t,action);
        if(t<50) assert(action[SWAT_ROLE_HEAD_TRIGGER]==0 && action[SWAT_ROLE_HEAD_AIM]==1);
        else if(armed_fire<0 && action[SWAT_ROLE_HEAD_TRIGGER]==1) armed_fire=t;
        if(action[SWAT_ROLE_HEAD_COMMAND]==1 && challenged<0) challenged=t;
        repeats+=t<50 && action[SWAT_ROLE_HEAD_COMMAND]==1;
    }
    obs[SWAT_SELF_TAKEN]=0;
    // One challenge, no further call to restart its clock, and the shot
    // follows a confirming call once the weapon shows.
    assert(challenged==1 && repeats==1 && armed_fire>50 && armed_fire<=54);
    for(int kind=SWAT_SEEN_OFFICER;kind<=SWAT_SEEN_CIVILIAN;kind++) for(int up=0;up<2;up++) {
        if(kind==SWAT_SEEN_SUSPECT && !up) continue;
        swat_motel_officer_begin(&state,&m,0,2,4);
        place_officer(obs,&m,3,1.3f,-1.5708f); see(obs,0,kind,3,up);
        for(int t=0;t<4;t++) { swat_motel_officer_act(&state,&m,obs,(uint32_t)t,action); assert(action[SWAT_ROLE_HEAD_TRIGGER]==0); }
    }
    // Cuffing: hands up within reach on the reticle: interact is held while
    // the cuff progresses. Already cuffed: no cuffing, the route goes on.
    swat_motel_officer_begin(&state,&m,0,2,4);
    place_officer(obs,&m,3,-2,-1.5708f); see(obs,0,SWAT_SEEN_CIVILIAN,1.2f,true); obs[SWAT_SELF_CUFFING]=.3f;
    swat_motel_officer_act(&state,&m,obs,3,action);
    assert(action[SWAT_ROLE_HEAD_INTERACT]==1 && action[SWAT_ROLE_HEAD_FORWARD]==1 && state.cuffing==SWAT_SEEN_CIVILIAN);
    // The cuff closes: the reticle shows them cuffed. The officer releases
    // the hold, then presses once to take their arm.
    obs[SWAT_SELF_CUFFING]=0; cuffed(obs,0);
    swat_motel_officer_act(&state,&m,obs,4,action); assert(action[SWAT_ROLE_HEAD_INTERACT]==0 && state.escort==SWAT_MOTEL_ESCORT_TAKE);
    swat_motel_officer_act(&state,&m,obs,5,action); assert(action[SWAT_ROLE_HEAD_INTERACT]==1 && state.escort==SWAT_MOTEL_ESCORT_NONE);
    // Route: from the lot, with nothing in view, the officer walks to its
    // first stack point and calls out on alternate decisions.
    swat_motel_officer_begin(&state,&m,0,2,4);
    place_officer(obs,&m,8,4,1.5708f);
    int moved=0,calls=0;
    for(int t=0;t<8;t++) {
        swat_motel_officer_act(&state,&m,obs,(uint32_t)t,action);
        moved+=action[SWAT_ROLE_HEAD_FORWARD]!=1 || action[SWAT_ROLE_HEAD_STRAFE]!=1; calls+=action[SWAT_ROLE_HEAD_COMMAND]==1;
    }
    assert(moved==8 && calls==4);
    // Reloads. A reload in progress is held on every decision, on the route
    // and in a fight, hurt or not, so the officer never sprints out of it
    // and never fires. When a wound interrupts it (the observed timer is 0
    // again with the magazine still empty), the officer presses reload
    // again within a few decisions instead of stalling.
    float before[SWAT_ROLES_OBS]; memcpy(before,obs,sizeof(before));
    swat_motel_officer_begin(&state,&m,0,2,4);
    place_officer(obs,&m,8,4,1.5708f);
    obs[SWAT_SELF_MAGAZINE]=0; obs[SWAT_SELF_CHAMBERED]=0;
    for(int t=0;t<16;t++) {
        obs[SWAT_SELF_RELOAD]=.5f; obs[SWAT_SELF_TAKEN]=t%3==0 ? .2f : 0;
        if(t==8) { see(obs,0,SWAT_SEEN_SUSPECT,4,false); obs[SWAT_SELF_PREV_TRIGGER]=0; }
        swat_motel_officer_act(&state,&m,obs,(uint32_t)t,action);
        assert(action[SWAT_ROLE_HEAD_RELOAD]==1 && action[SWAT_ROLE_HEAD_TRIGGER]==0);
    }
    int restart=-1;
    for(int t=16;t<32 && restart<0;t++) {
        obs[SWAT_SELF_RELOAD]=0; obs[SWAT_SELF_TAKEN]=t==16 ? .2f : 0;
        swat_motel_officer_act(&state,&m,obs,(uint32_t)t,action);
        assert(action[SWAT_ROLE_HEAD_TRIGGER]==0);
        if(action[SWAT_ROLE_HEAD_RELOAD]==1) restart=t;
    }
    assert(restart>=16 && restart<24);
    memcpy(obs,before,sizeof(before));
    // Determinism: two officers fed the same observations act identically.
    SwatMotelOfficer a,b; swat_motel_officer_begin(&a,&m,1,2,4); swat_motel_officer_begin(&b,&m,1,2,4);
    for(int t=0;t<200;t++) {
        float x[SWAT_ROLES_HEADS],y[SWAT_ROLES_HEADS];
        place_officer(obs,&m,unit()*20-10,unit()*16-10,unit()*6.2f-3.1f);
        if(t%3==0) see(obs,(int)(next()%SWAT_ROLES_RAYS),(int)(1+next()%6),unit()*10,next()%2);
        swat_motel_officer_act(&a,&m,obs,7,x); swat_motel_officer_act(&b,&m,obs,7,y);
        assert(!memcmp(x,y,sizeof(x)));
    }
    puts("PASS baselines motel officer: localization, route split, 20000 legal decisions, lawful fire, no shot without a visible weapon, the armed suspect challenged first, a suspect with hidden hands challenged and covered but never fired on until the weapon shows, no call on a firing decision, cuff and escort, route walk, reloads held and restarted after a wound, determinism");
}

int main(void) {
    legality();
    purity();
    suspect();
    officer();
    civilian();
    motel_officer();
    return 0;
}
