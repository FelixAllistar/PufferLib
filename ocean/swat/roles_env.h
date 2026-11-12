// Role task contract v3: officers, suspects and civilians as learned actors in
// one authoritative SwatSim. Selected by env.task=roles (build flag
// SWAT_ROLES_TRAINING). Annex v4 and movement v2 are separate contracts.
// Every controlled actor reaches the game through the ordinary
// per-actor SwatInput array (swat_sim_step_inputs); uncontrolled actors keep
// their scripted inputs. See CONTRACT.md "Role task contract v3". Episode
// seeds come from the training stream of roles_split.h; the held-out
// evaluation suite (roles_eval.h) drives the same env on the other stream.
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

#include "roles_layout.h"
#include "roles_baselines.h"
#include "roles_motel_officer.h"
#include "roles_split.h"
#include "roles_curriculum.h"

#define SWAT_ROLES_MAX_REPEAT 8
// Agents per env including inactive padding (roles_agents). The largest
// roster is SWAT_MAX_ACTORS, so the next power of two always fits.
#define SWAT_ROLES_MAX_AGENTS 16

#define OBS_SIZE SWAT_ROLES_OBS
#define NUM_ATNS SWAT_ROLES_HEADS
#define ACT_SIZES SWAT_ROLES_ACTION_SIZES
#define PUF_STEPS_PER_SEC 15

typedef struct SwatRoleRewards {
    float time_cost;
    float officer_success,officer_failure,officer_arrest,officer_neutralize,officer_secure_civilian;
    float officer_civilian_harm,officer_roe,officer_damage,officer_down,officer_shot;
    float suspect_officer_damage,suspect_harm,suspect_damage,suspect_free,suspect_arrested,suspect_down,suspect_escape;
    float civilian_damage,civilian_down,civilian_safe,civilian_comply,civilian_survive;
} SwatRoleRewards;

// Outcome of one finished episode, kept for the evaluation suite. Ground
// truth for measurement only: never observed by any agent or baseline.
typedef struct SwatRolesEpisode {
    uint32_t seed,layout_seed;
    int end,ticks,decisions;
    bool success,escaped;
    int civilians,civilians_harmed,civilians_killed;
    int suspects,suspects_killed,suspects_arrested,suspects_free;
    int officers,officers_down;
    // ROE violations (suite v2): officer engagements that dealt force to
    // protected people or officers, or lethal force to a person who was not a
    // perceived lethal threat (roe.h), merged per officer while unlawful hits
    // follow within SWAT_ROE_ENGAGEMENT_TICKS. protected_violations counts the
    // first kind alone, merged the same way (suite v1's roe_violations);
    // lethal_violations is the sim's debrief count of the second kind, merged
    // per officer and victim.
    int roe_violations,protected_violations,lethal_violations; float unlawful_damage;
    int officer_rounds,suspect_rounds;
} SwatRolesEpisode;

struct Log {
    float perf,episode_return,episode_length,score;
    float arrests,suspects_down,civilian_damage,protected_damage,officer_deaths;
    float suspects_free,civilians_safe,escapes;
    float officer_return,suspect_return,civilian_return;
    // Episodes whose roles_scripted roles ran the baselines, and officer
    // successes among them (puf_log reports the conditional win rate).
    float scripted_episodes,scripted_wins,n;
};

struct Env {
    Log log;
    Agent agents[SWAT_ROLES_MAX_AGENTS];
    // num_agents = roster (controlled actors, agents 0..roster-1) plus
    // inactive padding agents roster..num_agents-1.
    int num_agents,tag,boundary_reached;
    unsigned int rng;
    SwatSim* sim;
    void* view;
    int officers,repeat,decisions,roster;
    bool control_suspects,control_civilians,escape,escaped;
    int actor[SWAT_ROLES_MAX_AGENTS];
    SwatRole role[SWAT_ROLES_MAX_AGENTS];
    int gaze[SWAT_ROLES_MAX_AGENTS][SWAT_GAZE_RAYS][2];
    float taken[SWAT_ROLES_MAX_AGENTS],returns[SWAT_ROLES_MAX_AGENTS];
    bool safe[SWAT_ROLES_MAX_AGENTS],complied[SWAT_ROLES_MAX_AGENTS];
    float protected_damage;
    SwatRoleRewards reward;
    // Scripted baselines (roles_scripted, roles_scripted_rate). Roles are bit
    // 1<<SwatRole. The choice is per episode; a scripted agent keeps its slot
    // but is presented to the trainer exactly like a padding agent.
    unsigned int scripted_roles; float scripted_rate; int episodes,learned; bool scripted_episode;
    bool scripted[SWAT_ROLES_MAX_AGENTS];
    uint32_t script_seed[SWAT_ROLES_MAX_AGENTS];
    // Seed streams (roles_split.h). Training draws per-episode sim seeds from
    // the env seed; evaluation from the suite case. layout_seed is the fixed
    // training layout and layouts the optional per-episode training pool.
    SwatRolesSplit split; uint32_t eval_suite,eval_case,layout_seed; int layouts;
    // Health at the start of the decision: own damage taken (obs 44) is the
    // drop the agent feels, not the reward ledger.
    float health_before[SWAT_ROLES_MAX_AGENTS];
    // Evaluation bookkeeping for the episode in progress and the last one
    // that finished (finished counts completed episodes).
    SwatRolesEpisode current,last; int finished;
    // Tick of each officer's latest unlawful hit, and of its latest hit on
    // protected people or officers (-1 for none), for the merged counts.
    int roe_last[SWAT_MAX_ACTORS],protected_last[SWAT_MAX_ACTORS];
    // Scripted motel officers (roles_motel_officer.h): the static plan they
    // are briefed with, and each one's own episode state.
    SwatMotelMap* motel_map;
    SwatMotelOfficer motel_officer[SWAT_ROLES_MAX_AGENTS];
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

// roles_scripted: none, or officers/suspects/civilians (or all), separated by
// '+', ',' or spaces. Returns a mask of 1<<SwatRole.
static unsigned int swat_roles_scripted(Dict* kwargs) {
    DictItem* item=dict_find(kwargs,"roles_scripted");
    if(!item) return 0;
    const char* p=item->str;
    if(!p) { fprintf(stderr,"swat roles: roles_scripted must be none, officers, suspects, civilians or all\n"); exit(1); }
    unsigned int mask=0;
    while(*p) {
        while(*p=='+' || *p==',' || *p==' ' || *p=='\t') p++;
        size_t n=strcspn(p,"+, \t");
        if(!n) break;
        if(n==4 && !strncmp(p,"none",n)) {}
        else if(n==8 && !strncmp(p,"officers",n)) mask|=1u<<SWAT_OFFICER;
        else if(n==8 && !strncmp(p,"suspects",n)) mask|=1u<<SWAT_SUSPECT;
        else if(n==9 && !strncmp(p,"civilians",n)) mask|=1u<<SWAT_CIVILIAN;
        else if(n==3 && !strncmp(p,"all",n)) mask|=(1u<<SWAT_OFFICER)|(1u<<SWAT_SUSPECT)|(1u<<SWAT_CIVILIAN);
        else { fprintf(stderr,"swat roles: unknown roles_scripted role '%.*s'\n",(int)n,p); exit(1); }
        p+=n;
    }
    return mask;
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
// Roster actors take their inputs from the trainer (learned or scripted
// baseline): the sim's own surrender decisions must not act for them.
static void swat_roles_external(Env* env) {
    for(int n=0;n<env->roster;n++) env->sim->actors[env->actor[n]].mind.external=true;
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
            out[SWAT_RAY_COMPLIANT]=b->gear.restrained ? SWAT_RAY_RESTRAINED_VALUE :
                b->gear.surrendered ? SWAT_RAY_SURRENDERED_VALUE : 0;
            out[SWAT_RAY_INTEGRITY]=1;
            // The weapon in hand is drawn on the body, so whoever sees the
            // person sees it (contract v3).
            out[SWAT_RAY_WEAPON]=swat_actor_shows_weapon(b);
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
        // Team custody, as reported over the radio by the officer who cuffed
        // each person. Counts of people still at large would reveal unseen
        // suspects and civilians, so they are not observed (since contract v2).
        int suspects=0,civilians=0;
        for(int i=0;i<s->actor_count;i++) {
            const SwatActor* b=&s->actors[i];
            if(!b->present || !b->alive || !b->gear.restrained) continue;
            suspects+=b->role==SWAT_SUSPECT; civilians+=b->role==SWAT_CIVILIAN;
        }
        out[42]=swat_clamp((float)suspects/4,0,1); out[43]=swat_clamp((float)civilians/4,0,1);
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
// Inactive padding agents: every head, gaze included, has one legal value,
// so the sampled action is fixed and carries no policy gradient.
static void swat_roles_pad_mask(unsigned char* mask) {
    if(!mask) return;
    for(int h=0;h<SWAT_ROLES_MOTOR_HEADS;h++) swat_roles_only(mask,h,swat_roles_neutral[h]);
    for(int k=0;k<SWAT_GAZE_RAYS;k++) {
        swat_roles_only(mask,SWAT_ROLE_HEAD_GAZE+2*k,SWAT_GAZE_YAW_BINS/2);
        swat_roles_only(mask,SWAT_ROLE_HEAD_GAZE+2*k+1,SWAT_GAZE_PITCH_BINS/2);
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
        if(i>=env->roster || env->scripted[i]) {
            // Padding observes zeros: no role bit, unlike every real agent.
            // Scripted agents look the same to the trainer; their baseline
            // observes the real role observation inside puf_step.
            if(env->agents[i].observations) memset(env->agents[i].observations,0,SWAT_ROLES_OBS*sizeof(float));
            swat_roles_pad_mask(env->agents[i].action_mask);
            continue;
        }
        if(env->agents[i].observations) swat_roles_observe(env,i,env->agents[i].observations);
        swat_roles_mask(env,i,env->agents[i].action_mask);
    }
}

// The static plan scripted motel officers are briefed with: the floor of
// the game's own navigation grid (built here with every door closed, before
// any episode), the room boxes, the closed door leaves and the extraction
// point. It is the same for every motel episode: occupancy and layout seeds
// only place people, who are not on the plan.
static void swat_roles_motel_map(SwatSim* s,SwatMotelMap* m) {
    memset(m,0,sizeof(*m));
    const SwatWorld* w=&s->world;
    b3Pos next;
    swat_navigation_next(s,s->mission.staging,s->extraction,&next);
    const SwatNavigation* nav=s->navigation;
    if(!nav || !nav->built) { fprintf(stderr,"swat roles: motel navigation unavailable\n"); exit(1); }
    // Crop the grid around the rooms, staging and extraction.
    float lo[2]={(float)s->extraction.x,(float)s->extraction.z},hi[2]={lo[0],lo[1]};
    float staging[2]={(float)s->mission.staging.x,(float)s->mission.staging.z};
    for(int k=0;k<2;k++) { lo[k]=fminf(lo[k],staging[k]); hi[k]=fmaxf(hi[k],staging[k]); }
    for(int r=0;r<w->room_count && r<SWAT_MOTEL_MAP_ROOMS;r++) {
        const SwatRoom* room=&w->rooms[r];
        lo[0]=fminf(lo[0],(float)(room->center.x-room->half.x)); hi[0]=fmaxf(hi[0],(float)(room->center.x+room->half.x));
        lo[1]=fminf(lo[1],(float)(room->center.z-room->half.z)); hi[1]=fmaxf(hi[1],(float)(room->center.z+room->half.z));
        m->room[m->rooms][0]=(float)room->center.x; m->room[m->rooms][1]=(float)room->center.z;
        m->room[m->rooms][2]=room->half.x; m->room[m->rooms][3]=room->half.z; m->rooms++;
    }
    int ox=(int)lroundf(((lo[0]+hi[0])*.5f-SWAT_MOTEL_MAP_WIDTH*SWAT_MOTEL_MAP_CELL*.5f-nav->min_x)/SWAT_MOTEL_MAP_CELL);
    int oz=(int)lroundf(((lo[1]+hi[1])*.5f-SWAT_MOTEL_MAP_DEPTH*SWAT_MOTEL_MAP_CELL*.5f-nav->min_z)/SWAT_MOTEL_MAP_CELL);
    if(hi[0]-lo[0]>SWAT_MOTEL_MAP_WIDTH*SWAT_MOTEL_MAP_CELL-4 || hi[1]-lo[1]>SWAT_MOTEL_MAP_DEPTH*SWAT_MOTEL_MAP_CELL-4 ||
       ox<0 || oz<0 || ox+SWAT_MOTEL_MAP_WIDTH>nav->width || oz+SWAT_MOTEL_MAP_DEPTH>nav->depth) {
        fprintf(stderr,"swat roles: motel plan does not fit the officer map\n"); exit(1);
    }
    m->x0=nav->min_x+ox*SWAT_MOTEL_MAP_CELL; m->z0=nav->min_z+oz*SWAT_MOTEL_MAP_CELL;
    m->extraction[0]=(float)s->extraction.x; m->extraction[1]=(float)s->extraction.z;
    // Ground floor: the lowest walkable sample of each cell (not the roof).
    int layer[SWAT_MOTEL_MAP_CELLS];
    for(int c=0;c<SWAT_MOTEL_MAP_CELLS;c++) {
        int cell=(oz+c/SWAT_MOTEL_MAP_WIDTH)*nav->width+ox+c%SWAT_MOTEL_MAP_WIDTH; layer[c]=-1;
        for(int l=0;l<SWAT_NAV_LAYERS;l++) {
            int at=l*nav->cells+cell;
            if(nav->walkable[at] && (layer[c]<0 || nav->height[at]<nav->height[layer[c]*nav->cells+cell])) layer[c]=l;
        }
    }
    for(int c=0;c<SWAT_MOTEL_MAP_CELLS;c++) if(layer[c]>=0) {
        int at=layer[c]*nav->cells+(oz+c/SWAT_MOTEL_MAP_WIDTH)*nav->width+ox+c%SWAT_MOTEL_MAP_WIDTH;
        for(int d=0;d<4;d++) {
            int n=swat_motel_neighbour(c,d);
            if(n>=0 && layer[n]>=0 && (nav->links[at][d]&(1u<<layer[n]))) m->links[c]|=(unsigned char)(1u<<d);
        }
    }
    // Keep only the floor connected to extraction.
    int16_t distance[SWAT_MOTEL_MAP_CELLS];
    swat_motel_distances(m,swat_motel_cell(m,m->extraction[0],m->extraction[1]),distance);
    for(int c=0;c<SWAT_MOTEL_MAP_CELLS;c++) if(distance[c]<0) m->links[c]=0;
    for(int i=0;i<w->count && m->doors<SWAT_MOTEL_MAP_DOORS;i++) {
        const SwatObject* o=&w->objects[i];
        if(!o->door || !o->active) continue;
        b3Pos centre=swat_door_center(o,o->closed_yaw);
        m->door[m->doors][0]=(float)centre.x; m->door[m->doors][1]=(float)centre.z;
        m->door[m->doors][2]=cosf(o->closed_yaw); m->door[m->doors][3]=sinf(o->closed_yaw); m->doors++;
    }
}

// Builds the env for one split. puf_init, the trainer's entry, always builds
// a training env; only the evaluation suite (roles_eval.h) builds
// SWAT_ROLES_EVAL envs, so no config key can select held-out episodes.
static void swat_roles_init(Env* env,Dict* kwargs,SwatRolesSplit split) {
    // roles_stage=N applies curriculum stage N's difficulty keys over kwargs.
    Dict staged={0};
    int stage=swat_roles_int(kwargs,"roles_stage",-1,-1,SWAT_ROLES_MAX_STAGES-1);
    if(stage>=0) {
        if(split!=SWAT_ROLES_TRAIN) { fprintf(stderr,"swat roles: curriculum stages are training-only\n"); exit(1); }
        DictItem* path=dict_find(kwargs,"roles_curriculum");
        Ini curriculum;
        swat_roles_curriculum_load(&curriculum,path && path->str && *path->str ? path->str : SWAT_ROLES_CURRICULUM_PATH);
        if(stage>=curriculum.num_sections) {
            fprintf(stderr,"swat roles: roles_stage=%d but the curriculum has %d stages\n",stage,curriculum.num_sections); exit(1);
        }
        swat_roles_apply_stage(kwargs,&staged,&curriculum,stage);
        puf_ini_free(&curriculum);
        kwargs=&staged;
    }
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
    if(split==SWAT_ROLES_TRAIN && config.motel_occupancy==SWAT_ROLES_EVAL_OCCUPANCY) {
        fprintf(stderr,"swat roles: motel_occupancy=%d is reserved for the held-out evaluation suite\n",SWAT_ROLES_EVAL_OCCUPANCY); exit(1);
    }
    // Training layout seeds stay below 2^31: the upper half is held out.
    env->layout_seed=(uint32_t)swat_roles_int(kwargs,"layout_seed",1,1,(int)(SWAT_ROLES_EVAL_BIT-1));
    env->layouts=swat_roles_int(kwargs,"roles_layouts",0,0,SWAT_ROLES_MAX_LAYOUTS);
    env->split=split;
    config.layout_seed=env->layout_seed;
    // The motel needs its tactical rules (securing, evacuation, evidence);
    // vacant officer slots stay empty instead of becoming scripted squad bots.
    config.tactical_rules=config.mission==SWAT_MOTEL; config.squad_bots=0;
    env->officers=swat_roles_int(kwargs,"roles_officers",2,1,SWAT_MAX_PLAYERS);
    env->control_suspects=swat_roles_int(kwargs,"roles_suspects",1,0,1)!=0;
    env->control_civilians=swat_roles_int(kwargs,"roles_civilians",1,0,1)!=0;
    env->escape=swat_roles_int(kwargs,"suspect_escape",0,0,1)!=0;
    int agents=swat_roles_int(kwargs,"roles_agents",0,0,SWAT_ROLES_MAX_AGENTS);
    env->repeat=swat_roles_int(kwargs,"roles_repeat",4,1,SWAT_ROLES_MAX_REPEAT);
    env->scripted_roles=swat_roles_scripted(kwargs);
    env->scripted_rate=swat_roles_float(kwargs,"roles_scripted_rate",1);
    if(env->scripted_rate>1) { fprintf(stderr,"swat roles: roles_scripted_rate must be in [0,1]\n"); exit(1); }
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
    // This first world only fixes the roster; puf_reset reseeds every episode.
    swat_sim_init(env->sim,config,swat_roles_train_seed(env->rng,0));
    swat_roles_populate(env);
    env->roster=swat_roles_roster(env,env->actor,env->role);
    if(env->roster<1) { fprintf(stderr,"swat roles: no controlled actors\n"); exit(1); }
    swat_roles_external(env);
    // roles_agents=0 pads the roster to the next power of two (4 stays 4,
    // the 7-agent motel becomes 8), so any power-of-two vec.total_agents per
    // buffer divides evenly. A positive value is the exact count.
    if(!agents) for(agents=1;agents<env->roster;agents*=2) {}
    if(agents<env->roster) {
        fprintf(stderr,"swat roles: roles_agents=%d is below the %d controlled actors\n",agents,env->roster); exit(1);
    }
    env->num_agents=agents;
    for(int i=env->roster;i<env->num_agents;i++) env->actor[i]=-1;
    env->episodes=0; env->learned=env->roster; env->scripted_episode=false;
    memset(env->scripted,0,sizeof(env->scripted));
    for(int i=0;i<env->num_agents;i++) { env->agents[i].policy=0; env->agents[i].action_mask=NULL; }
    env->finished=0;
    env->motel_map=NULL;
    if(config.mission==SWAT_MOTEL && (env->scripted_roles>>SWAT_OFFICER)&1u) {
        env->motel_map=calloc(1,sizeof(SwatMotelMap));
        if(!env->motel_map) { fprintf(stderr,"swat roles: allocation failed\n"); exit(1); }
        swat_roles_motel_map(env->sim,env->motel_map);
    }
    if(kwargs==&staged) dict_clear(&staged);
}

void puf_init(Env* env,Dict* kwargs) {
#ifdef SWAT_NATIVE_TRAINER
    DictItem* task=dict_find(kwargs,"task");
    if(!task || !task->str || strcmp(task->str,"roles")) {
        fprintf(stderr,"swat: this puffer was built for env.task=roles; rebuild after changing task\n"); exit(1);
    }
#endif
    swat_roles_init(env,kwargs,SWAT_ROLES_TRAIN);
}

void puf_reset(Env* env) {
    // Episode seeds: the training stream from the env seed, or the held-out
    // stream from the evaluation case (roles_split.h).
    uint32_t index=(uint32_t)(++env->episodes),seed,layout;
    if(env->split==SWAT_ROLES_EVAL) {
        seed=swat_roles_eval_seed(env->eval_suite,env->eval_case,index-1);
        layout=swat_roles_eval_layout(env->eval_suite,env->eval_case,index-1);
    } else {
        seed=swat_roles_train_seed(env->rng,index);
        layout=swat_roles_train_layout(seed,env->layout_seed,env->layouts);
    }
    env->sim->rng=seed; env->sim->config.layout_seed=layout;
    swat_sim_reset(env->sim);
    swat_roles_populate(env);
    int actors[SWAT_MAX_ACTORS]; SwatRole roles[SWAT_MAX_ACTORS];
    int count=swat_roles_roster(env,actors,roles);
    if(count!=env->roster || memcmp(actors,env->actor,sizeof(int)*(size_t)count) ||
       memcmp(roles,env->role,sizeof(SwatRole)*(size_t)count)) {
        fprintf(stderr,"swat roles: controlled roster changed across resets\n"); exit(1);
    }
    swat_roles_external(env);
    env->decisions=0; env->escaped=false; env->protected_damage=0;
    // Scripted baselines: drawn per episode from the episode seed only, so
    // the sim's RNG stream is untouched and roles_scripted=none behaves
    // exactly like the unscripted env.
    uint32_t episode=swat_baseline_mix(seed^0x5bd1e995u);
    env->scripted_episode=env->scripted_roles && (env->scripted_rate>=1 ||
        (float)(episode>>8)*(1.0f/16777216.0f)<env->scripted_rate);
    env->learned=0;
    for(int i=0;i<env->num_agents;i++) {
        env->scripted[i]=env->scripted_episode && i<env->roster && ((env->scripted_roles>>env->role[i])&1u);
        env->script_seed[i]=swat_baseline_mix(episode^(uint32_t)(i+1)*0x85ebca6bu);
        env->learned+=i<env->roster && !env->scripted[i];
    }
    // Officers come first in the roster, so agent n is officer slot n.
    if(env->motel_map) for(int i=0;i<env->roster;i++) if(env->scripted[i] && env->role[i]==SWAT_OFFICER)
        swat_motel_officer_begin(&env->motel_officer[i],env->motel_map,i,env->officers,env->repeat);
    for(int i=0;i<env->num_agents;i++) {
        // Default fan until the policy chooses: spread across the yaw bins.
        for(int k=0;k<SWAT_GAZE_RAYS;k++) {
            env->gaze[i][k][0]=k*SWAT_GAZE_YAW_BINS/SWAT_GAZE_RAYS; env->gaze[i][k][1]=SWAT_GAZE_PITCH_BINS/2;
        }
        env->taken[i]=env->returns[i]=0; env->safe[i]=env->complied[i]=false;
    }
    memset(&env->current,0,sizeof(env->current));
    env->current.seed=seed; env->current.layout_seed=layout;
    for(int i=0;i<SWAT_MAX_ACTORS;i++) env->roe_last[i]=env->protected_last[i]=-1;
    swat_roles_emit(env);
}

typedef struct SwatRolesBefore {
    bool alive[SWAT_MAX_ACTORS],restrained[SWAT_MAX_ACTORS];
    int shots[SWAT_MAX_ACTORS];
} SwatRolesBefore;

static void swat_roles_snapshot(const SwatSim* s,SwatRolesBefore* before) {
    for(int i=0;i<SWAT_MAX_ACTORS;i++) {
        const SwatActor* a=&s->actors[i];
        before->alive[i]=a->present && a->alive; before->restrained[i]=a->gear.restrained; before->shots[i]=a->arsenal.shots;
    }
}

// Rewards are indexed by agent; padding agents (>= roster) are never touched.
static void swat_roles_tick(Env* env,const SwatRolesBefore* before,float rewards[SWAT_ROLES_MAX_AGENTS]) {
    const SwatSim* s=env->sim; const SwatRoleRewards* w=&env->reward;
    int arrests=0,downs=0,secured=0; float civilian_harm=0,unlawful=0;
    for(int i=0;i<s->actor_count;i++) {
        const SwatActor* a=&s->actors[i];
        if(a->role==SWAT_SUSPECT) {
            arrests+=!before->restrained[i] && a->gear.restrained;
            // Only threats count: killing a surrendered or restrained suspect
            // is never rewarded; its damage is charged as protected force.
            downs+=before->alive[i] && !a->alive && !s->force[i].downed_protected;
        }
        if(a->role==SWAT_CIVILIAN) {
            secured+=!before->restrained[i] && a->gear.restrained && a->alive;
            civilian_harm+=s->force[i].taken;
        }
        if(a->role==SWAT_OFFICER || a->role==SWAT_SNIPER) {
            float force=s->force[i].protected_damage+s->force[i].officer_damage;
            unlawful+=force;
            if(force>0) {
                if(env->protected_last[i]<0 || s->tick-env->protected_last[i]>=SWAT_ROE_ENGAGEMENT_TICKS) env->current.protected_violations++;
                env->protected_last[i]=s->tick;
            }
            if(force>0 || s->force[i].lethal>0) {
                if(env->roe_last[i]<0 || s->tick-env->roe_last[i]>=SWAT_ROE_ENGAGEMENT_TICKS) env->current.roe_violations++;
                env->roe_last[i]=s->tick;
            }
        }
    }
    env->protected_damage+=unlawful;
    for(int n=0;n<env->roster;n++) {
        int i=env->actor[n]; const SwatActor* a=&s->actors[i]; const SwatForce* f=&s->force[i];
        bool died=before->alive[i] && !a->alive;
        float r=-w->time_cost-f->taken*(env->role[n]==SWAT_OFFICER ? w->officer_damage :
            env->role[n]==SWAT_SUSPECT ? w->suspect_damage : w->civilian_damage);
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

static void swat_roles_finish(Env* env,float rewards[SWAT_ROLES_MAX_AGENTS]) {
    const SwatSim* s=env->sim; const SwatRoleRewards* w=&env->reward;
    bool success=s->end==SWAT_SUCCESS && !env->escaped;
    bool failure=s->end==SWAT_CIVILIAN_HARMED || env->escaped;
    float role_return[3]={0}; int role_count[3]={0}; float total=0;
    for(int n=0;n<env->roster;n++) {
        const SwatActor* a=&s->actors[env->actor[n]];
        bool at_large=a->alive && !a->gear.restrained && !a->gear.surrendered;
        if(env->role[n]==SWAT_OFFICER) rewards[n]+=success*w->officer_success-failure*w->officer_failure;
        else if(env->role[n]==SWAT_SUSPECT) {
            bool escaped=at_large && env->escaped && b3Distance(swat_body_feet_position(&a->controller.body),s->mission.staging)<1.4f;
            rewards[n]+=escaped ? w->suspect_escape : at_large*w->suspect_free;
        } else rewards[n]+=a->alive*w->civilian_survive;
        env->returns[n]+=rewards[n];
        if(env->scripted[n]) continue; // returns are logged for learned agents only
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
    SwatRolesEpisode* e=&env->current;
    e->end=s->end; e->ticks=s->tick; e->decisions=env->decisions; e->success=success; e->escaped=env->escaped;
    e->unlawful_damage=env->protected_damage; e->lethal_violations=s->debrief.lethal_violations;
    for(int i=0;i<s->actor_count;i++) {
        const SwatActor* a=&s->actors[i]; if(!a->present) continue;
        if(a->role==SWAT_CIVILIAN) {
            e->civilians++; e->civilians_killed+=!a->alive; e->civilians_harmed+=!a->alive || a->health<100;
        } else if(a->role==SWAT_SUSPECT) {
            e->suspects++; e->suspects_killed+=!a->alive; e->suspects_arrested+=a->alive && a->gear.restrained;
            e->suspects_free+=a->alive && !a->gear.restrained && !a->gear.surrendered;
            e->suspect_rounds+=a->arsenal.shots;
        } else if(a->role==SWAT_OFFICER) {
            e->officers++; e->officers_down+=!a->alive; e->officer_rounds+=a->arsenal.shots;
        }
    }
    env->last=*e; env->finished++;
    Log* log=&env->log;
    log->perf+=success; log->score+=arrests; log->arrests+=arrests; log->suspects_down+=suspects_down;
    log->episode_return+=env->learned ? total/env->learned : 0; log->episode_length+=env->decisions;
    log->civilian_damage+=s->totals.civilian_damage; log->protected_damage+=env->protected_damage;
    log->officer_deaths+=officer_deaths; log->suspects_free+=suspects_free; log->civilians_safe+=civilians_safe;
    log->escapes+=env->escaped;
    log->officer_return+=role_count[0] ? role_return[0]/role_count[0] : 0;
    log->suspect_return+=role_count[1] ? role_return[1]/role_count[1] : 0;
    log->civilian_return+=role_count[2] ? role_return[2]/role_count[2] : 0;
    log->scripted_episodes+=env->scripted_episode; log->scripted_wins+=env->scripted_episode && success;
    log->n+=1;
}

void puf_step(Env* env) {
    SwatSim* s=env->sim;
    float rewards[SWAT_ROLES_MAX_AGENTS]={0};
    SwatInput chosen[SWAT_ROLES_MAX_AGENTS];
    // Scripted agents ignore the trainer's action: their baseline acts on the
    // role observation of this decision, computed before any agent's comply
    // changes the state, so it sees exactly what a learned agent would.
    float script[SWAT_ROLES_MAX_AGENTS][SWAT_ROLES_HEADS]={{0}};
    for(int n=0;n<env->roster;n++) env->health_before[n]=s->actors[env->actor[n]].health;
    for(int n=0;n<env->roster;n++) if(env->scripted[n]) {
        float obs[SWAT_ROLES_OBS];
        swat_roles_observe(env,n,obs);
        if(env->motel_map && env->role[n]==SWAT_OFFICER)
            swat_motel_officer_act(&env->motel_officer[n],env->motel_map,obs,env->script_seed[n],script[n]);
        else swat_baseline_act(obs,env->script_seed[n],script[n]);
    }
    // Padding agents' actions are ignored; their reward stays zero.
    for(int n=0;n<env->roster;n++) {
        const float* action=env->scripted[n] ? script[n] : env->agents[n].actions;
        bool comply=false;
        chosen[n]=swat_roles_decode(env->role[n],action,&comply);
        for(int k=0;k<SWAT_GAZE_RAYS;k++) {
            env->gaze[n][k][0]=swat_roles_bin(action,SWAT_ROLE_HEAD_GAZE+2*k,SWAT_GAZE_YAW_BINS);
            env->gaze[n][k][1]=swat_roles_bin(action,SWAT_ROLE_HEAD_GAZE+2*k+1,SWAT_GAZE_PITCH_BINS);
        }
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
        SwatRolesBefore before; swat_roles_snapshot(s,&before);
        SwatInput inputs[SWAT_MAX_ACTORS];
        swat_sim_bot_inputs(s,inputs);
        // Restrained people are led, not in control: keep the scripted escort input.
        for(int n=0;n<env->roster;n++) {
            const SwatActor* a=&s->actors[env->actor[n]];
            if(a->alive && !a->gear.restrained) inputs[env->actor[n]]=chosen[n];
        }
        swat_sim_step_inputs(s,inputs);
        swat_roles_tick(env,&before,rewards);
        env->escaped=swat_roles_escape(env);
        done=s->end!=SWAT_RUNNING || env->escaped;
    }
    env->decisions++;
    for(int n=0;n<env->roster;n++) env->taken[n]=fmaxf(0,env->health_before[n]-s->actors[env->actor[n]].health);
    if(done) swat_roles_finish(env,rewards);
    for(int n=0;n<env->num_agents;n++) {
        if(!done) env->returns[n]+=rewards[n];
        // Scripted agents are not trained: the trainer sees padding.
        *env->agents[n].rewards=n<env->roster && env->scripted[n] ? 0 : rewards[n];
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
    dict_set(out,"civilian_return",log->civilian_return);
    // Eval against the scripted baselines: share of episodes that ran them and
    // the officer success rate within those episodes (0 when none ran).
    dict_set(out,"scripted_episodes",log->scripted_episodes);
    dict_set(out,"scripted_perf",log->scripted_episodes>0 ? log->scripted_wins/log->scripted_episodes : 0);
    dict_set(out,"n",log->n);
}

void puf_render(Env* env) {
#ifndef SWAT_ROLES_NO_RENDER
    if(!env->view) {
        env->view=calloc(1,sizeof(SwatView));
        if(!env->view) { fprintf(stderr,"swat roles: view allocation failed\n"); exit(1); }
        swat_view_init((SwatView*)env->view,false);
    }
    SwatView* view=(SwatView*)env->view;
    view->actor=env->roster ? env->actor[0] : 0;
    BeginDrawing(); swat_view_draw(view,env->sim,true,70); EndDrawing();
#endif
}

void puf_close(Env* env) {
#ifndef SWAT_ROLES_NO_RENDER
    if(env->view) { swat_view_close((SwatView*)env->view); free(env->view); env->view=NULL; }
#endif
    if(env->sim) { swat_sim_close(env->sim); free(env->sim); env->sim=NULL; }
    free(env->motel_map); env->motel_map=NULL;
}
#endif
