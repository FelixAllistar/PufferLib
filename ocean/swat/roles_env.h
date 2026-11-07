// Role task contract v1: officers, suspects and civilians as learned actors in
// one authoritative SwatSim. Selected by env.task=roles (build flag
// SWAT_ROLES_TRAINING). Annex v3 and movement v2 are separate headers and are
// unchanged. Every controlled actor reaches the game through the ordinary
// per-actor SwatInput array (swat_sim_step_inputs); uncontrolled actors keep
// their scripted inputs. See CONTRACT.md "Role task contract v1".
#ifndef SWAT_ROLES_ENV_H
#define SWAT_ROLES_ENV_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim.h"
#ifndef SWAT_ROLES_NO_RENDER
#include "render.h"
#endif
typedef float obs_t;
#include "pufferenv.h"

#define SWAT_ROLES_VERSION 1
#define SWAT_ROLES_SELF 45
#define SWAT_GAZE_RAYS 8
// One fixed reticle ray along the achieved view, then the learned gaze rays.
#define SWAT_ROLES_RAYS (1+SWAT_GAZE_RAYS)
#define SWAT_ROLES_RAY_CHANNELS 11
#define SWAT_ROLES_OBS (SWAT_ROLES_SELF+SWAT_ROLES_RAYS*SWAT_ROLES_RAY_CHANNELS)
#define SWAT_ROLES_RANGE 30.0f
#define SWAT_ROLES_MOTOR_HEADS 16
#define SWAT_GAZE_YAW_BINS 9
#define SWAT_GAZE_PITCH_BINS 5
#define SWAT_ROLES_HEADS (SWAT_ROLES_MOTOR_HEADS+2*SWAT_GAZE_RAYS)
#define SWAT_ROLES_ACTION_SIZES {5,5,3,3,3,2,3,2,2,2,3,2,2,2,2,2, \
    9,5,9,5,9,5,9,5,9,5,9,5,9,5,9,5}
#define SWAT_ROLES_LOGITS 155
#define SWAT_ROLES_MAX_REPEAT 8

#define OBS_SIZE SWAT_ROLES_OBS
#define NUM_ATNS SWAT_ROLES_HEADS
#define ACT_SIZES SWAT_ROLES_ACTION_SIZES
#define PUF_STEPS_PER_SEC 15

enum {
    SWAT_ROLE_HEAD_YAW, SWAT_ROLE_HEAD_PITCH, SWAT_ROLE_HEAD_FORWARD, SWAT_ROLE_HEAD_STRAFE,
    SWAT_ROLE_HEAD_LEAN, SWAT_ROLE_HEAD_CROUCH, SWAT_ROLE_HEAD_GAIT, SWAT_ROLE_HEAD_AIM,
    SWAT_ROLE_HEAD_TRIGGER, SWAT_ROLE_HEAD_RELOAD, SWAT_ROLE_HEAD_WEAPON, SWAT_ROLE_HEAD_INTERACT,
    SWAT_ROLE_HEAD_JUMP, SWAT_ROLE_HEAD_SELECTOR, SWAT_ROLE_HEAD_COMMAND, SWAT_ROLE_HEAD_COMPLY,
    SWAT_ROLE_HEAD_GAZE
};
// Ray channels. 0-1 are the ray's own direction offsets, so the recurrent
// policy can relate what it saw to where it chose to look.
enum {
    SWAT_RAY_YAW, SWAT_RAY_PITCH, SWAT_RAY_DEPTH, SWAT_RAY_SOLID, SWAT_RAY_DESTRUCTIBLE,
    SWAT_RAY_DOOR, SWAT_RAY_OFFICER, SWAT_RAY_SUSPECT, SWAT_RAY_CIVILIAN, SWAT_RAY_COMPLIANT,
    SWAT_RAY_INTEGRITY
};
static const float swat_gaze_yaw_degrees[SWAT_GAZE_YAW_BINS]={-80,-50,-25,-10,0,10,25,50,80};
static const float swat_gaze_pitch_degrees[SWAT_GAZE_PITCH_BINS]={-40,-15,0,15,40};
static const int swat_roles_neutral[SWAT_ROLES_MOTOR_HEADS]={2,2,1,1,1,0,1,0,0,0,0,0,0,0,0,0};

typedef struct SwatRoleRewards {
    float time_cost;
    float officer_success,officer_failure,officer_arrest,officer_neutralize,officer_secure_civilian;
    float officer_civilian_harm,officer_roe,officer_damage,officer_down,officer_shot;
    float suspect_officer_damage,suspect_harm,suspect_damage,suspect_free,suspect_arrested,suspect_down,suspect_escape;
    float civilian_damage,civilian_down,civilian_safe,civilian_comply,civilian_survive;
} SwatRoleRewards;

struct Log {
    float perf,episode_return,episode_length,score;
    float arrests,suspects_down,civilian_damage,protected_damage,officer_deaths;
    float suspects_free,civilians_safe,escapes;
    float officer_return,suspect_return,civilian_return,n;
};

struct Env {
    Log log;
    Agent agents[SWAT_MAX_ACTORS];
    int num_agents,tag,boundary_reached;
    unsigned int rng;
    SwatSim* sim;
    void* view;
    int officers,repeat,decisions;
    bool control_suspects,control_civilians,escape,escaped;
    int actor[SWAT_MAX_ACTORS];
    SwatRole role[SWAT_MAX_ACTORS];
    int gaze[SWAT_MAX_ACTORS][SWAT_GAZE_RAYS][2];
    float taken[SWAT_MAX_ACTORS],returns[SWAT_MAX_ACTORS];
    bool safe[SWAT_MAX_ACTORS],complied[SWAT_MAX_ACTORS];
    float protected_damage;
    SwatRoleRewards reward;
};

static int swat_roles_int(Dict* kwargs,const char* key,int fallback,int minimum,int maximum) {
    DictItem* item=dict_find(kwargs,key); double value=item ? item->value : fallback;
    if(!isfinite(value) || value<minimum || value>maximum || floor(value)!=value) {
        fprintf(stderr,"swat roles: %s must be an integer from %d to %d\n",key,minimum,maximum); exit(1);
    }
    return (int)value;
}
static float swat_roles_float(Dict* kwargs,const char* key,float fallback) {
    DictItem* item=dict_find(kwargs,key); double value=item ? item->value : fallback;
    if(!isfinite(value) || value<0 || value>1000) {
        fprintf(stderr,"swat roles: %s must be finite and in [0,1000]\n",key); exit(1);
    }
    return (float)value;
}

static int swat_roles_logit_offset(int head) {
    static const int sizes[]=SWAT_ROLES_ACTION_SIZES;
    int offset=0; for(int i=0;i<head;i++) offset+=sizes[i];
    return offset;
}
static int swat_roles_bin(const float* action,int head,int bins) {
    float value=isfinite(action[head]) ? action[head] : 0;
    return (int)swat_clamp(value,0,(float)(bins-1));
}

// Officers occupy their stable player slots (actors 0,3,4,5). Spawned again
// after every sim reset; the sim itself never adds officers outside tactical
// squad bots, which this task disables.
static void swat_roles_populate(Env* env) {
    for(int slot=1;slot<env->officers;slot++) swat_sim_set_player(env->sim,slot,true);
}
// Agent order: officers, then suspects, then civilians, each by actor index.
static int swat_roles_roster(const Env* env,int actors[SWAT_MAX_ACTORS],SwatRole roles[SWAT_MAX_ACTORS]) {
    const SwatSim* s=env->sim; int count=0;
    for(int pass=0;pass<3;pass++) for(int i=0;i<s->actor_count;i++) {
        const SwatActor* a=&s->actors[i];
        if(!a->present) continue;
        bool take=pass==0 ? a->role==SWAT_OFFICER :
            pass==1 ? env->control_suspects && a->role==SWAT_SUSPECT :
            env->control_civilians && a->role==SWAT_CIVILIAN;
        if(take) { actors[count]=i; roles[count]=a->role; count++; }
    }
    return count;
}

static void swat_roles_ray(const SwatSim* s,const SwatActor* a,b3Pos eye,b3Vec3 dir,float* out) {
    SwatHit hit=swat_world_ray(&s->world,eye,dir,SWAT_ROLES_RANGE,a->controller.body.body);
    // Nearest physical depth for movement; clear glass may still expose a
    // person's visual identity, as in annex v3. Opaque cover hides people.
    SwatHit seen=hit;
    bool pane=hit.kind==SWAT_HIT_WORLD && hit.index>=0 && s->world.objects[hit.index].material==SWAT_GLASS;
    if(pane) seen=swat_world_sight_ray(&s->world,eye,dir,SWAT_ROLES_RANGE,a->controller.body.body);
    out[SWAT_RAY_DEPTH]=1;
    if(seen.kind==SWAT_HIT_ACTOR && seen.index>=0 && seen.index<s->actor_count) {
        bool visible=seen.distance<15 || seen.distance<swat_world_visual_range(&s->world,seen.point,SWAT_ROLES_RANGE);
        if(visible) {
            const SwatActor* b=&s->actors[seen.index];
            out[SWAT_RAY_DEPTH]=swat_clamp(hit.distance/SWAT_ROLES_RANGE,0,1);
            out[b->role==SWAT_CIVILIAN ? SWAT_RAY_CIVILIAN : b->role==SWAT_SUSPECT ? SWAT_RAY_SUSPECT : SWAT_RAY_OFFICER]=1;
            out[SWAT_RAY_COMPLIANT]=b->gear.surrendered || b->gear.restrained;
            out[SWAT_RAY_INTEGRITY]=1;
            return;
        }
        if(!pane) return; // No hidden-body identity or depth.
    }
    if(!hit.hit) return;
    out[SWAT_RAY_DEPTH]=swat_clamp(hit.distance/SWAT_ROLES_RANGE,0,1);
    const SwatObject* object=swat_world_hit_object(&s->world,hit);
    if(object) {
        out[object->door ? SWAT_RAY_DOOR : object->max_health>0 ? SWAT_RAY_DESTRUCTIBLE : SWAT_RAY_SOLID]=1;
        out[SWAT_RAY_INTEGRITY]=object->max_health>0 ? object->health/object->max_health : 1;
    }
}

static b3Pos swat_roles_objective(const Env* env,SwatRole role,bool* valid) {
    const SwatSim* s=env->sim;
    *valid=role!=SWAT_SUSPECT || env->escape;
    return role==SWAT_SUSPECT ? s->mission.staging : s->extraction;
}

static void swat_roles_observe(const Env* env,int agent,float out[SWAT_ROLES_OBS]) {
    memset(out,0,SWAT_ROLES_OBS*sizeof(float));
    const SwatSim* s=env->sim;
    const SwatActor* a=&s->actors[env->actor[agent]];
    SwatRole role=env->role[agent];
    out[15]=role==SWAT_OFFICER; out[16]=role==SWAT_SUSPECT; out[17]=role==SWAT_CIVILIAN;
    out[37]=fmaxf(0,1.0f-(float)s->tick/s->config.max_ticks);
    if(!a->alive) return;
    const SwatController* c=&a->controller;
    b3Vec3 v=b3Body_GetLinearVelocity(c->body.body);
    b3Vec3 f=swat_direction(c->yaw,0),r=swat_controller_right(c);
    out[0]=b3Dot(v,f)/4.6f; out[1]=b3Dot(v,r)/4.6f; out[2]=swat_clamp(v.y/15,-1,1);
    out[3]=sinf(c->yaw); out[4]=cosf(c->yaw); out[5]=sinf(c->pitch); out[6]=cosf(c->pitch);
    out[7]=c->body.onGround; out[8]=c->body.crouched; out[9]=c->lean;
    out[10]=c->ads; out[11]=c->sprinting; out[12]=c->stamina; out[13]=a->health/100;
    out[14]=1;
    out[18]=a->gear.surrendered; out[19]=a->gear.restrained;
    out[20]=swat_clamp((float)a->gear.stunned_ticks/240,0,1);
    out[21]=a->gear.restrained && a->mind.escort_owner>=0;
    out[22]=swat_clamp((float)a->gear.cuff_ticks/72,0,1);
    if(role!=SWAT_CIVILIAN) {
        const SwatWeapon* w=&a->arsenal.slots[a->arsenal.active];
        const SwatWeaponDef* def=swat_arsenal_def(&a->arsenal,a->arsenal.active);
        out[23]=(float)a->arsenal.active; out[24]=(float)w->magazine/def->capacity;
        out[25]=w->chambered; out[26]=(float)w->reserve/(def->capacity*3);
        out[27]=(float)w->reload_remaining/def->empty_reload_ticks;
        out[28]=(float)a->arsenal.equip_remaining/def->equip_ticks;
        out[29]=(float)w->mode/2; out[30]=(float)w->cooldown/def->shot_ticks;
        out[31]=c->recoil_pitch/(12*SWAT_RAD); out[32]=c->recoil_yaw/(6*SWAT_RAD);
        out[33]=c->muzzle_blocked;
        out[35]=a->arsenal.last_fire; out[36]=a->last_shot_tick==s->tick;
    }
    out[34]=fmaxf(0,c->body.jumpCooldown)/c->body.jumpCooldownTime;
    bool objective=false; b3Pos point=swat_roles_objective(env,role,&objective);
    if(objective) {
        b3Vec3 goal=b3SubPos(point,swat_body_feet_position(&c->body));
        out[38]=swat_clamp(b3Dot(goal,f)/30,-1,1); out[39]=swat_clamp(b3Dot(goal,r)/30,-1,1);
        out[40]=swat_clamp(goal.y/10,-1,1); out[41]=swat_clamp(b3Length(goal)/30,0,1);
    }
    if(role==SWAT_OFFICER) {
        // Explicit mission telemetry, as in annex v3 (TOC reports).
        SwatScenarioProgress progress=swat_sim_progress(s);
        out[42]=swat_clamp((float)progress.threats/4,0,1); out[43]=swat_clamp((float)progress.unsecured/4,0,1);
    }
    out[44]=swat_clamp(env->taken[agent]/100,0,1);
    b3Vec3 forward,right,up;
    swat_controller_view(c,&forward,&right,&up);
    b3Pos eye=swat_controller_eye(c);
    float* ray=out+SWAT_ROLES_SELF;
    swat_roles_ray(s,a,eye,forward,ray);
    for(int k=0;k<SWAT_GAZE_RAYS;k++) {
        ray+=SWAT_ROLES_RAY_CHANNELS;
        float yaw=swat_gaze_yaw_degrees[env->gaze[agent][k][0]],pitch=swat_gaze_pitch_degrees[env->gaze[agent][k][1]];
        b3CosSin y=swat_sincos(yaw*SWAT_RAD),p=swat_sincos(pitch*SWAT_RAD);
        b3Vec3 dir=swat_normalize(swat_add(swat_mul(forward,p.cosine*y.cosine),
            swat_add(swat_mul(right,p.cosine*y.sine),swat_mul(up,p.sine))));
        ray[SWAT_RAY_YAW]=yaw/90; ray[SWAT_RAY_PITCH]=pitch/45;
        swat_roles_ray(s,a,eye,dir,ray);
    }
}

static void swat_roles_only(unsigned char* mask,int head,int index) {
    static const int sizes[]=SWAT_ROLES_ACTION_SIZES;
    int offset=swat_roles_logit_offset(head);
    memset(mask+offset,0,(size_t)sizes[head]); mask[offset+index]=1;
}
// Legal actions per role and state. Gaze heads stay free in every state.
static void swat_roles_mask(const Env* env,int agent,unsigned char* mask) {
    if(!mask) return;
    memset(mask,1,SWAT_ROLES_LOGITS);
    const SwatActor* a=&env->sim->actors[env->actor[agent]];
    SwatRole role=env->role[agent];
    bool held=!a->alive || a->gear.restrained || a->gear.surrendered || a->gear.stunned_ticks>0;
    if(held) {
        for(int h=0;h<SWAT_ROLES_MOTOR_HEADS;h++) swat_roles_only(mask,h,swat_roles_neutral[h]);
        return;
    }
    if(role!=SWAT_OFFICER) swat_roles_only(mask,SWAT_ROLE_HEAD_COMMAND,0);
    else swat_roles_only(mask,SWAT_ROLE_HEAD_COMPLY,0);
    if(role==SWAT_CIVILIAN) {
        static const int unarmed[]={SWAT_ROLE_HEAD_AIM,SWAT_ROLE_HEAD_TRIGGER,SWAT_ROLE_HEAD_RELOAD,
            SWAT_ROLE_HEAD_WEAPON,SWAT_ROLE_HEAD_SELECTOR};
        for(int i=0;i<5;i++) swat_roles_only(mask,unarmed[i],0);
    }
}
// The decoder enforces the same role rules, so CPU playback without masks
// cannot issue illegal actions either.
static SwatInput swat_roles_decode(SwatRole role,const float* action,bool* comply) {
    SwatInput in=swat_decode_action(action);
    in.command=role==SWAT_OFFICER && swat_roles_bin(action,SWAT_ROLE_HEAD_COMMAND,2);
    *comply=role!=SWAT_OFFICER && swat_roles_bin(action,SWAT_ROLE_HEAD_COMPLY,2);
    if(role==SWAT_CIVILIAN) { in.aim=in.fire=in.reload=in.selector=false; in.weapon=0; }
    return in;
}

static void swat_roles_emit(Env* env) {
    for(int i=0;i<env->num_agents;i++) {
        if(env->agents[i].observations) swat_roles_observe(env,i,env->agents[i].observations);
        swat_roles_mask(env,i,env->agents[i].action_mask);
    }
}

void puf_init(Env* env,Dict* kwargs) {
#ifdef SWAT_NATIVE_TRAINER
    DictItem* task=dict_find(kwargs,"task");
    if(!task || !task->str || strcmp(task->str,"roles")) {
        fprintf(stderr,"swat: this puffer was built for env.task=roles; rebuild after changing task\n"); exit(1);
    }
#endif
    SwatConfig config=swat_default_config();
    DictItem* mission=dict_find(kwargs,"roles_mission");
    if(mission && mission->str && !strcmp(mission->str,"motel")) config.mission=SWAT_MOTEL;
    else if(mission && (!mission->str || strcmp(mission->str,"annex"))) {
        fprintf(stderr,"swat roles: roles_mission must be annex or motel\n"); exit(1);
    }
    config.max_ticks=swat_roles_int(kwargs,"max_ticks",1800,1,1000000);
    config.randomize=swat_roles_int(kwargs,"randomize",1,0,1)!=0;
    config.hostile_fire=swat_roles_int(kwargs,"hostile_fire",1,0,1)!=0;
    config.motel_occupancy=swat_roles_int(kwargs,"motel_occupancy",0,0,SWAT_MOTEL_OCCUPANCIES-1);
    // The motel needs its tactical rules (securing, evacuation, evidence);
    // vacant officer slots stay empty instead of becoming scripted squad bots.
    config.tactical_rules=config.mission==SWAT_MOTEL; config.squad_bots=0;
    env->officers=swat_roles_int(kwargs,"roles_officers",2,1,SWAT_MAX_PLAYERS);
    env->control_suspects=swat_roles_int(kwargs,"roles_suspects",1,0,1)!=0;
    env->control_civilians=swat_roles_int(kwargs,"roles_civilians",1,0,1)!=0;
    env->escape=swat_roles_int(kwargs,"suspect_escape",0,0,1)!=0;
    env->repeat=swat_roles_int(kwargs,"roles_repeat",4,1,SWAT_ROLES_MAX_REPEAT);
    env->rng^=(unsigned int)swat_roles_int(kwargs,"seed",2718,0,2147483647);
    SwatRoleRewards* w=&env->reward;
    w->time_cost=swat_roles_float(kwargs,"roles_time_cost",.001f);
    w->officer_success=swat_roles_float(kwargs,"officer_success",5);
    w->officer_failure=swat_roles_float(kwargs,"officer_failure",5);
    w->officer_arrest=swat_roles_float(kwargs,"officer_arrest",1.5f);
    w->officer_neutralize=swat_roles_float(kwargs,"officer_neutralize",1);
    w->officer_secure_civilian=swat_roles_float(kwargs,"officer_secure_civilian",.25f);
    w->officer_civilian_harm=swat_roles_float(kwargs,"officer_civilian_harm",.1f);
    w->officer_roe=swat_roles_float(kwargs,"officer_roe",.1f);
    w->officer_damage=swat_roles_float(kwargs,"officer_damage",.01f);
    w->officer_down=swat_roles_float(kwargs,"officer_down",1);
    w->officer_shot=swat_roles_float(kwargs,"officer_shot",.0005f);
    w->suspect_officer_damage=swat_roles_float(kwargs,"suspect_officer_damage",.01f);
    w->suspect_harm=swat_roles_float(kwargs,"suspect_harm",.05f);
    w->suspect_damage=swat_roles_float(kwargs,"suspect_damage",.01f);
    w->suspect_free=swat_roles_float(kwargs,"suspect_free",1);
    w->suspect_arrested=swat_roles_float(kwargs,"suspect_arrested",1);
    w->suspect_down=swat_roles_float(kwargs,"suspect_down",2);
    w->suspect_escape=swat_roles_float(kwargs,"suspect_escape_reward",2);
    w->civilian_damage=swat_roles_float(kwargs,"civilian_damage_cost",.02f);
    w->civilian_down=swat_roles_float(kwargs,"civilian_down",2);
    w->civilian_safe=swat_roles_float(kwargs,"civilian_safe",1);
    w->civilian_comply=swat_roles_float(kwargs,"civilian_comply",.1f);
    w->civilian_survive=swat_roles_float(kwargs,"civilian_survive",1);
    env->sim=(SwatSim*)calloc(1,sizeof(SwatSim));
    if(!env->sim) { fprintf(stderr,"swat roles: allocation failed\n"); exit(1); }
    // Simulation owns stable storage: native vector setup may realloc Env.
    swat_sim_init(env->sim,config,env->rng);
    swat_roles_populate(env);
    env->num_agents=swat_roles_roster(env,env->actor,env->role);
    for(int i=0;i<env->num_agents;i++) { env->agents[i].policy=0; env->agents[i].action_mask=NULL; }
}

void puf_reset(Env* env) {
    swat_sim_reset(env->sim);
    swat_roles_populate(env);
    int actors[SWAT_MAX_ACTORS]; SwatRole roles[SWAT_MAX_ACTORS];
    int count=swat_roles_roster(env,actors,roles);
    if(count!=env->num_agents || memcmp(actors,env->actor,sizeof(int)*(size_t)count) ||
       memcmp(roles,env->role,sizeof(SwatRole)*(size_t)count)) {
        fprintf(stderr,"swat roles: controlled roster changed across resets\n"); exit(1);
    }
    env->decisions=0; env->escaped=false; env->protected_damage=0;
    for(int i=0;i<env->num_agents;i++) {
        // Default fan until the policy chooses: spread across the yaw bins.
        for(int k=0;k<SWAT_GAZE_RAYS;k++) {
            env->gaze[i][k][0]=k*SWAT_GAZE_YAW_BINS/SWAT_GAZE_RAYS; env->gaze[i][k][1]=SWAT_GAZE_PITCH_BINS/2;
        }
        env->taken[i]=env->returns[i]=0; env->safe[i]=env->complied[i]=false;
    }
    swat_roles_emit(env);
}

typedef struct SwatRolesBefore {
    bool alive[SWAT_MAX_ACTORS],restrained[SWAT_MAX_ACTORS];
    int shots[SWAT_MAX_ACTORS];
} SwatRolesBefore;

static void swat_roles_tick(Env* env,const SwatRolesBefore* before,float rewards[SWAT_MAX_ACTORS]) {
    const SwatSim* s=env->sim; const SwatRoleRewards* w=&env->reward;
    int arrests=0,downs=0,secured=0; float civilian_harm=0,unlawful=0;
    for(int i=0;i<s->actor_count;i++) {
        const SwatActor* a=&s->actors[i];
        if(a->role==SWAT_SUSPECT) {
            arrests+=!before->restrained[i] && a->gear.restrained;
            downs+=before->alive[i] && !a->alive;
        }
        if(a->role==SWAT_CIVILIAN) {
            secured+=!before->restrained[i] && a->gear.restrained && a->alive;
            civilian_harm+=s->force[i].taken;
        }
        if(a->role==SWAT_OFFICER || a->role==SWAT_SNIPER) unlawful+=s->force[i].protected_damage+s->force[i].officer_damage;
    }
    env->protected_damage+=unlawful;
    for(int n=0;n<env->num_agents;n++) {
        int i=env->actor[n]; const SwatActor* a=&s->actors[i]; const SwatForce* f=&s->force[i];
        bool died=before->alive[i] && !a->alive;
        float r=-w->time_cost-f->taken*(env->role[n]==SWAT_OFFICER ? w->officer_damage :
            env->role[n]==SWAT_SUSPECT ? w->suspect_damage : w->civilian_damage);
        env->taken[n]+=f->taken;
        if(env->role[n]==SWAT_OFFICER) {
            r+=w->officer_arrest*arrests+w->officer_neutralize*downs+w->officer_secure_civilian*secured;
            r-=w->officer_civilian_harm*civilian_harm+w->officer_roe*unlawful;
            r-=w->officer_shot*(float)(a->arsenal.shots-before->shots[i])+w->officer_down*died;
        } else if(env->role[n]==SWAT_SUSPECT) {
            r+=w->suspect_officer_damage*f->officer_damage-w->suspect_harm*f->protected_damage;
            r-=w->suspect_arrested*(!before->restrained[i] && a->gear.restrained)+w->suspect_down*died;
        } else {
            r-=w->civilian_down*died;
            bool safe=a->alive && (a->rescued || b3Distance(swat_body_feet_position(&a->controller.body),s->extraction)<SWAT_STAGING_RADIUS);
            if(safe && !env->safe[n]) { env->safe[n]=true; r+=w->civilian_safe; }
        }
        rewards[n]+=r;
    }
}

static bool swat_roles_escape(const Env* env) {
    if(!env->escape) return false;
    const SwatSim* s=env->sim;
    for(int i=0;i<s->actor_count;i++) {
        const SwatActor* a=&s->actors[i];
        if(a->present && a->alive && a->role==SWAT_SUSPECT && !a->gear.restrained && !a->gear.surrendered &&
           b3Distance(swat_body_feet_position(&a->controller.body),s->mission.staging)<1.4f) return true;
    }
    return false;
}

static void swat_roles_finish(Env* env,float rewards[SWAT_MAX_ACTORS]) {
    const SwatSim* s=env->sim; const SwatRoleRewards* w=&env->reward;
    bool success=s->end==SWAT_SUCCESS && !env->escaped;
    bool failure=s->end==SWAT_CIVILIAN_HARMED || env->escaped;
    float role_return[3]={0}; int role_count[3]={0}; float total=0;
    for(int n=0;n<env->num_agents;n++) {
        const SwatActor* a=&s->actors[env->actor[n]];
        bool at_large=a->alive && !a->gear.restrained && !a->gear.surrendered;
        if(env->role[n]==SWAT_OFFICER) rewards[n]+=success*w->officer_success-failure*w->officer_failure;
        else if(env->role[n]==SWAT_SUSPECT) {
            bool escaped=at_large && env->escaped && b3Distance(swat_body_feet_position(&a->controller.body),s->mission.staging)<1.4f;
            rewards[n]+=escaped ? w->suspect_escape : at_large*w->suspect_free;
        } else rewards[n]+=a->alive*w->civilian_survive;
        env->returns[n]+=rewards[n];
        int slot=env->role[n]==SWAT_OFFICER ? 0 : env->role[n]==SWAT_SUSPECT ? 1 : 2;
        role_return[slot]+=env->returns[n]; role_count[slot]++; total+=env->returns[n];
    }
    int arrests=0,suspects_down=0,suspects_free=0,officer_deaths=0,civilians_safe=0;
    for(int i=0;i<s->actor_count;i++) {
        const SwatActor* a=&s->actors[i]; if(!a->present) continue;
        if(a->role==SWAT_SUSPECT) {
            arrests+=a->alive && a->gear.restrained; suspects_down+=!a->alive;
            suspects_free+=a->alive && !a->gear.restrained && !a->gear.surrendered;
        }
        if(a->role==SWAT_OFFICER) officer_deaths+=!a->alive;
        if(a->role==SWAT_CIVILIAN) civilians_safe+=a->alive && (a->rescued ||
            b3Distance(swat_body_feet_position(&a->controller.body),s->extraction)<SWAT_STAGING_RADIUS);
    }
    Log* log=&env->log;
    log->perf+=success; log->score+=arrests; log->arrests+=arrests; log->suspects_down+=suspects_down;
    log->episode_return+=total/env->num_agents; log->episode_length+=env->decisions;
    log->civilian_damage+=s->totals.civilian_damage; log->protected_damage+=env->protected_damage;
    log->officer_deaths+=officer_deaths; log->suspects_free+=suspects_free; log->civilians_safe+=civilians_safe;
    log->escapes+=env->escaped;
    log->officer_return+=role_count[0] ? role_return[0]/role_count[0] : 0;
    log->suspect_return+=role_count[1] ? role_return[1]/role_count[1] : 0;
    log->civilian_return+=role_count[2] ? role_return[2]/role_count[2] : 0;
    log->n+=1;
}

void puf_step(Env* env) {
    SwatSim* s=env->sim;
    float rewards[SWAT_MAX_ACTORS]={0};
    SwatInput chosen[SWAT_MAX_ACTORS];
    for(int n=0;n<env->num_agents;n++) {
        const float* action=env->agents[n].actions;
        bool comply=false;
        chosen[n]=swat_roles_decode(env->role[n],action,&comply);
        for(int k=0;k<SWAT_GAZE_RAYS;k++) {
            env->gaze[n][k][0]=swat_roles_bin(action,SWAT_ROLE_HEAD_GAZE+2*k,SWAT_GAZE_YAW_BINS);
            env->gaze[n][k][1]=swat_roles_bin(action,SWAT_ROLE_HEAD_GAZE+2*k+1,SWAT_GAZE_PITCH_BINS);
        }
        env->taken[n]=0;
        SwatActor* a=&s->actors[env->actor[n]];
        // Voluntary surrender: the only role action without a SwatInput field.
        // Irreversible, like a commanded surrender; officers must still cuff.
        if(comply && a->alive && !a->gear.restrained && !a->gear.surrendered) {
            a->gear.surrendered=true;
            if(env->role[n]==SWAT_CIVILIAN && !env->complied[n]) { env->complied[n]=true; rewards[n]+=env->reward.civilian_comply; }
        }
    }
    bool done=false;
    for(int t=0;t<env->repeat && !done;t++) {
        SwatRolesBefore before;
        for(int i=0;i<SWAT_MAX_ACTORS;i++) {
            const SwatActor* a=&s->actors[i];
            before.alive[i]=a->present && a->alive; before.restrained[i]=a->gear.restrained; before.shots[i]=a->arsenal.shots;
        }
        SwatInput inputs[SWAT_MAX_ACTORS];
        swat_sim_bot_inputs(s,inputs);
        // Restrained people are led, not in control: keep the scripted escort input.
        for(int n=0;n<env->num_agents;n++) {
            const SwatActor* a=&s->actors[env->actor[n]];
            if(a->alive && !a->gear.restrained) inputs[env->actor[n]]=chosen[n];
        }
        swat_sim_step_inputs(s,inputs);
        swat_roles_tick(env,&before,rewards);
        env->escaped=swat_roles_escape(env);
        done=s->end!=SWAT_RUNNING || env->escaped;
    }
    env->decisions++;
    if(done) swat_roles_finish(env,rewards);
    for(int n=0;n<env->num_agents;n++) {
        if(!done) env->returns[n]+=rewards[n];
        *env->agents[n].rewards=rewards[n];
        *env->agents[n].terminals=done;
    }
    if(done) puf_reset(env);
    else swat_roles_emit(env);
}

void puf_log(Log* log,Dict* out) {
    dict_set(out,"perf",log->perf); dict_set(out,"episode_return",log->episode_return);
    dict_set(out,"episode_length",log->episode_length); dict_set(out,"score",log->score);
    dict_set(out,"arrests",log->arrests); dict_set(out,"suspects_down",log->suspects_down);
    dict_set(out,"civilian_damage",log->civilian_damage); dict_set(out,"protected_damage",log->protected_damage);
    dict_set(out,"officer_deaths",log->officer_deaths); dict_set(out,"suspects_free",log->suspects_free);
    dict_set(out,"civilians_safe",log->civilians_safe); dict_set(out,"escapes",log->escapes);
    dict_set(out,"officer_return",log->officer_return); dict_set(out,"suspect_return",log->suspect_return);
    dict_set(out,"civilian_return",log->civilian_return); dict_set(out,"n",log->n);
}

void puf_render(Env* env) {
#ifndef SWAT_ROLES_NO_RENDER
    if(!env->view) {
        env->view=calloc(1,sizeof(SwatView));
        if(!env->view) { fprintf(stderr,"swat roles: view allocation failed\n"); exit(1); }
        swat_view_init((SwatView*)env->view,false);
    }
    SwatView* view=(SwatView*)env->view;
    view->actor=env->num_agents ? env->actor[0] : 0;
    BeginDrawing(); swat_view_draw(view,env->sim,true,70); EndDrawing();
#endif
}

void puf_close(Env* env) {
#ifndef SWAT_ROLES_NO_RENDER
    if(env->view) { swat_view_close((SwatView*)env->view); free(env->view); env->view=NULL; }
#endif
    if(env->sim) { swat_sim_close(env->sim); free(env->sim); env->sim=NULL; }
}
#endif
