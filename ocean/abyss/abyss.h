#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "pufferenv.h"

#define ACT_SIZES {5, 3, 3, 3, 3, 3}
#define NUM_ATNS ABYSS_ACTION_LANES

#define ABYSS_MAX_ENTITIES 16
#define ABYSS_ENTITY_FEATURES 16
#define ABYSS_GLOBAL_FEATURES 32
#define ABYSS_MAX_OBSTACLES 60
#define ABYSS_OBS_OBSTACLES 8
#define ABYSS_OBSTACLE_FEATURES 5
#define ABYSS_OBS_SIZE (ABYSS_GLOBAL_FEATURES + ABYSS_MAX_ENTITIES*ABYSS_ENTITY_FEATURES + ABYSS_OBS_OBSTACLES*ABYSS_OBSTACLE_FEATURES)
#define ABYSS_ACTION_LANES 6
#define ABYSS_MAX_CLOUDS 4
#define OBS_SIZE ABYSS_OBS_SIZE

enum { ENTITY_NONE, ENTITY_HOSTILE, ENTITY_CACHE, ENTITY_CONDUIT, ENTITY_CLOUD,
    ENTITY_SUPPRESSOR, ENTITY_TRACKING_PYLON };
enum { WEATHER_DARK, WEATHER_ELECTRICAL, WEATHER_EXOTIC, WEATHER_FIRESTORM, WEATHER_GAMMA };
enum { CLOUD_FILAMENT, CLOUD_BIOLUMINESCENCE, CLOUD_TACHYON };
enum { NAV_HOLD, NAV_CACHE, NAV_FOCUS, NAV_CONDUIT, NAV_STOP };
enum { FOCUS_HOLD, FOCUS_NEAREST_HOSTILE, FOCUS_CACHE };
enum { DESIRED_HOLD, DESIRED_ON, DESIRED_OFF };
enum { INTERACT_HOLD, INTERACT_LOOT, INTERACT_GATE };

typedef struct { float x, y, z; } Vec3;
typedef float obs_t;
#include "generated_scenarios.h"
#include "generated_colliders.h"
typedef struct { Vec3 center; float radius; } AbyssObstacle;
typedef struct { Vec3 center, radii; float yaw, pitch; } AbyssCloudLobe;
typedef struct { unsigned char kind, lobe_count; AbyssCloudLobe lobes[4]; } AbyssCloud;
typedef struct {
    unsigned char kind, alive, locked, focused;
    unsigned char suppressor_vulnerable, gate_required;
    unsigned short type_index;
    Vec3 pos, vel;
    float radius, signature, max_speed, orbit_speed, orbit_range;
    float shield, armor, hull;
    float shield_max, armor_max, hull_max;
    float resist[3][4];
    float damage_mix[4];
    float dps, optimal, falloff, tracking, neutralizer;
    float effect_range, effect_strength, effect_cycle, effect_cooldown;
    float lock_progress, lock_time;
} AbyssEntity;

struct Log {
    float perf, score, episode_return, episode_length;
    float survival_rate, completion_rate, rooms_cleared, env_id, n;
};

struct Env {
    Log log;
    Agent agents[1];
    int num_agents;
    int tag, boundary_reached;
    unsigned int rng;
    int tick, max_steps, room, rooms_cleared, entity_count, focus_index, cloud_count, obstacle_count;
    int scenario_episode;
    int filament_tier, weather_type;
    int weapon_on, prop_on, rep_on, cache_looted, cargo_open, reset_pending;
    float episode_return;
    Vec3 ship_pos, ship_vel;
    float shield, armor, hull, capacitor;
    float boundary_radius, boundary_damage;
    float weather_penalty, weather_range_multiplier, weather_velocity_multiplier;
    float ship_shield_hp, ship_armor_hp, ship_hull_hp;
    float ship_resist[3][4], base_ship_resist[3][4];
    float cap_capacity, cap_recharge_time;
    float base_speed, prop_speed, signature, scan_resolution, lock_range;
    float weapon_volley, weapon_cycle, weapon_cooldown, weapon_optimal, weapon_falloff, weapon_tracking;
    float weapon_damage_mix[4];
    float rep_amount, rep_cycle, rep_cooldown, rep_cap_cost;
    float prop_cap_per_s;
    float reward_hostile_kill, reward_cache_kill, reward_loot, reward_room_clear;
    float reward_success, reward_completion_speed, reward_failure;
    float reward_wasted_rep, reward_invalid_loot, reward_cargo_open, reward_step;
    AbyssEntity entities[ABYSS_MAX_ENTITIES];
    AbyssCloud clouds[ABYSS_MAX_CLOUDS];
    AbyssObstacle obstacles[ABYSS_MAX_OBSTACLES];
};

static inline float ab_clip(float x, float lo, float hi) { return fmaxf(lo, fminf(x, hi)); }
static inline unsigned int ab_rand_u32(Env* e) {
    unsigned int x=e->rng?e->rng:1u;
    x^=x<<13;x^=x>>17;x^=x<<5;
    e->rng=x?x:1u;
    return e->rng;
}
static inline float ab_rand(Env* e) { return (ab_rand_u32(e)&0x00ffffffu)/16777216.0f; }
static inline Vec3 ab_sub(Vec3 a, Vec3 b) { return (Vec3){a.x-b.x,a.y-b.y,a.z-b.z}; }
static inline Vec3 ab_add(Vec3 a, Vec3 b) { return (Vec3){a.x+b.x,a.y+b.y,a.z+b.z}; }
static inline Vec3 ab_mul(Vec3 a, float s) { return (Vec3){a.x*s,a.y*s,a.z*s}; }
static inline float ab_len(Vec3 a) { return sqrtf(a.x*a.x+a.y*a.y+a.z*a.z); }
static inline Vec3 ab_unit(Vec3 a) { float n=ab_len(a); return n>0.001f?ab_mul(a,1.0f/n):(Vec3){1,0,0}; }
static inline Vec3 ab_cross(Vec3 a, Vec3 b) { return (Vec3){a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }

static void ab_resolve_obstacles(Env* e, Vec3* position, Vec3* velocity, float body_radius) {
    for(int i=0;i<e->obstacle_count;i++) {
        AbyssObstacle* obstacle=&e->obstacles[i];
        Vec3 delta=ab_sub(*position,obstacle->center);float distance=ab_len(delta);
        float minimum=obstacle->radius+body_radius;
        if(distance>=minimum)continue;
        Vec3 normal=ab_unit(delta);*position=ab_add(obstacle->center,ab_mul(normal,minimum));
        float inward=velocity->x*normal.x+velocity->y*normal.y+velocity->z*normal.z;
        if(inward<0)*velocity=ab_sub(*velocity,ab_mul(normal,inward));
    }
}

static int ab_inside_cloud(AbyssCloud* cloud, Vec3 point) {
    for(int i=0;i<cloud->lobe_count;i++) {
        AbyssCloudLobe* lobe=&cloud->lobes[i];Vec3 d=ab_sub(point,lobe->center);
        float cy=cosf(lobe->yaw),sy=sinf(lobe->yaw),cp=cosf(lobe->pitch),sp=sinf(lobe->pitch);
        float x=cy*d.x+sy*d.y,y=-sy*d.x+cy*d.y;
        float xr=cp*x-sp*d.z,zr=sp*x+cp*d.z;
        float q=xr*xr/(lobe->radii.x*lobe->radii.x)
            +y*y/(lobe->radii.y*lobe->radii.y)
            +zr*zr/(lobe->radii.z*lobe->radii.z);
        if(q<=1.0f)return 1;
    }
    return 0;
}

static float ab_cloud_multiplier(Env* e, Vec3 point, int kind, float multiplier) {
    for(int i=0;i<e->cloud_count;i++) {
        if(e->clouds[i].kind==kind&&ab_inside_cloud(&e->clouds[i],point))return multiplier;
    }
    return 1.0f;
}

// EVE's modern turret equation uses a normalized tracking attribute and a
// 40,000 m reference signature. Angular velocity is in radians/second.
static float ab_turret_hit_chance(float angular, float tracking, float signature,
        float distance, float optimal, float falloff) {
    float tracking_term = angular*40000.0f/fmaxf(tracking*signature, 0.0001f);
    float range_term = fmaxf(0.0f, distance-optimal)/fmaxf(falloff, 0.0001f);
    return powf(0.5f, tracking_term*tracking_term + range_term*range_term);
}

static float ab_lock_time(float scan_resolution, float signature) {
    float a = asinhf(sqrtf(fmaxf(signature, 0.001f)));
    return 40000.0f/fmaxf(scan_resolution*a*a, 0.0001f);
}

static void ab_damage_layers(float amount, const float mix[4], float* shield, float* armor,
        float* hull, const float resist[3][4]) {
    float* layers[3] = {shield, armor, hull};
    for (int layer=0; layer<3 && amount>0; layer++) {
        float multiplier=0;
        for (int d=0; d<4; d++) multiplier += mix[d]*(1.0f-resist[layer][d]);
        multiplier = fmaxf(multiplier, 0.01f);
        float raw_to_break = *layers[layer]/multiplier;
        float raw_used = fminf(amount, raw_to_break);
        *layers[layer] = fmaxf(0, *layers[layer]-raw_used*multiplier);
        amount -= raw_used;
    }
}

static void ab_add_generated_hostile(Env* e, GeneratedSpawn spawn) {
    if(e->entity_count>=ABYSS_MAX_ENTITIES||spawn.npc>=GENERATED_NPC_COUNT)return;
    const GeneratedNpcDef* def=&GENERATED_NPCS[spawn.npc];
    AbyssEntity* n=&e->entities[e->entity_count++];memset(n,0,sizeof(*n));
    n->kind=ENTITY_HOSTILE;n->alive=1;n->type_index=spawn.npc;
    n->gate_required=def->gate_required;n->suppressor_vulnerable=def->suppressor_vulnerable;
    n->pos=(Vec3){spawn.position[0],spawn.position[1],spawn.position[2]};
    n->signature=def->signature;n->shield=n->shield_max=def->shield;
    n->armor=n->armor_max=def->armor;n->hull=n->hull_max=def->hull;
    n->max_speed=def->max_speed;n->orbit_speed=def->orbit_speed;n->orbit_range=def->orbit_range;
    n->optimal=def->optimal;n->falloff=def->falloff;n->tracking=def->tracking;
    n->dps=def->dps;n->neutralizer=def->neutralizer;
    memcpy(n->resist[0],def->shield_resist,sizeof(def->shield_resist));
    memcpy(n->resist[1],def->armor_resist,sizeof(def->armor_resist));
    memcpy(n->resist[2],def->hull_resist,sizeof(def->hull_resist));
    memcpy(n->damage_mix,def->damage_mix,sizeof(def->damage_mix));
}

static void ab_add_tower(Env* e, int kind, float range, float strength, Vec3 position) {
    if(e->entity_count>=ABYSS_MAX_ENTITIES)return;
    AbyssEntity* t=&e->entities[e->entity_count++];memset(t,0,sizeof(*t));
    t->kind=kind;t->alive=1;t->radius=7500;t->effect_range=range;
    t->effect_strength=strength;t->effect_cycle=2;
    t->pos=position;
}

static void ab_add_recorded_tower(Env* e, GeneratedTower tower) {
    Vec3 position={tower.position[0],tower.position[1],tower.position[2]};
    if(tower.kind==1)ab_add_tower(e,ENTITY_SUPPRESSOR,15000,30,position);
    else if(tower.kind==2)ab_add_tower(e,ENTITY_SUPPRESSOR,40000,10,position);
    else if(tower.kind==3)ab_add_tower(e,ENTITY_TRACKING_PYLON,15000,.80f,position);
    else if(tower.kind==4)ab_add_tower(e,ENTITY_TRACKING_PYLON,40000,.60f,position);
}

static void ab_spawn_local_effects(Env* e) {
    e->cloud_count=0;
    if(ab_rand(e)<.25f) {
        AbyssCloud* c=&e->clouds[e->cloud_count++];c->kind=ab_rand_u32(e)%3;c->lobe_count=2+ab_rand_u32(e)%3;
        Vec3 center={(ab_rand(e)-.5f)*60000,(ab_rand(e)-.5f)*60000,(ab_rand(e)-.5f)*12000};
        float yaw=ab_rand(e)*6.2831853f,pitch=(ab_rand(e)-.5f)*1.2f;
        for(int i=0;i<c->lobe_count;i++){AbyssCloudLobe*l=&c->lobes[i];float along=(i-(c->lobe_count-1)*.5f)*7000;l->center=ab_add(center,(Vec3){cosf(yaw)*along,sinf(yaw)*along,sinf(pitch)*along});l->radii=(Vec3){10000+12000*ab_rand(e),7000+9000*ab_rand(e),5000+7000*ab_rand(e)};l->yaw=yaw+(ab_rand(e)-.5f)*.5f;l->pitch=pitch+(ab_rand(e)-.5f)*.35f;}
    }
}

static void ab_spawn_obstacles(Env* e) {
    e->obstacle_count=0;
    // The capture came from a giant-rock room. Keep empty rooms common while
    // sampling one of its two measured 30-sphere unions in rock rooms.
    if(ab_rand(e)>=.35f)return;
    const GeneratedColliderTemplate* shape=&GENERATED_COLLIDER_TEMPLATES[ab_rand_u32(e)%GENERATED_COLLIDER_TEMPLATE_COUNT];
    float angle=ab_rand(e)*6.2831853f, ca=cosf(angle), sa=sinf(angle);
    float bearing=ab_rand(e)*6.2831853f, distance=45000+20000*ab_rand(e);
    Vec3 placement={cosf(bearing)*distance,sinf(bearing)*distance,(ab_rand(e)-.5f)*30000};
    for(int i=0;i<shape->count&&e->obstacle_count<ABYSS_MAX_OBSTACLES;i++) {
        const GeneratedColliderSphere* source=&GENERATED_COLLIDER_SPHERES[shape->offset+i];
        float x=source->center[0],y=source->center[1];
        Vec3 rotated={ca*x-sa*y,sa*x+ca*y,source->center[2]};
        AbyssObstacle* target=&e->obstacles[e->obstacle_count++];
        target->center=ab_add(placement,rotated);target->radius=source->radius;
    }
}

static void ab_spawn_room(Env* e) {
    memset(e->entities,0,sizeof(e->entities)); e->entity_count=0; e->focus_index=-1; e->cache_looted=0; e->cargo_open=0;
    int room_offset=e->scenario_episode*3+(e->room-1);
    const GeneratedRoom* recorded=&GENERATED_ROOMS[room_offset];
    AbyssEntity* cache=&e->entities[e->entity_count++]; cache->kind=ENTITY_CACHE; cache->alive=1;
    cache->pos=(Vec3){recorded->cache_position[0],recorded->cache_position[1],recorded->cache_position[2]}; cache->radius=1200; cache->signature=500;
    cache->shield=cache->shield_max=250; cache->armor=cache->armor_max=500; cache->hull=cache->hull_max=450;
    AbyssEntity* gate=&e->entities[e->entity_count++]; gate->kind=ENTITY_CONDUIT; gate->alive=1;
    gate->pos=(Vec3){recorded->gate_position[0],recorded->gate_position[1],recorded->gate_position[2]}; gate->radius=5000;
    for(int i=0;i<recorded->hostile_count;i++)ab_add_generated_hostile(e,recorded->hostiles[i]);
    for(int i=0;i<recorded->tower_count;i++)ab_add_recorded_tower(e,recorded->towers[i]);
    ab_spawn_local_effects(e);
    ab_spawn_obstacles(e);
    e->ship_pos=(Vec3){0,0,0};
    e->ship_vel=(Vec3){0,0,0};
    ab_resolve_obstacles(e,&e->ship_pos,&e->ship_vel,60.0f);
}

static void ab_roll_weather(Env* e) {
    if(e->filament_tier<=3)e->weather_penalty=ab_rand(e)<.90f?.30f:.50f;
    else e->weather_penalty=ab_rand(e)<.50f?.50f:.70f;
    e->weather_range_multiplier=e->weather_type==WEATHER_DARK?1.0f-e->weather_penalty:1.0f;
    e->weather_velocity_multiplier=e->weather_type==WEATHER_DARK?1.50f:1.0f;
}

static float ab_weather_resist(float resist, float penalty) {
    return 1.0f-(1.0f-resist)*(1.0f+penalty);
}

static void ab_apply_weather_entity(Env* e, AbyssEntity* n) {
    if(n->kind!=ENTITY_HOSTILE)return;
    if(e->weather_type==WEATHER_ELECTRICAL)for(int l=0;l<3;l++)n->resist[l][0]=ab_weather_resist(n->resist[l][0],e->weather_penalty);
    if(e->weather_type==WEATHER_EXOTIC)for(int l=0;l<3;l++)n->resist[l][2]=ab_weather_resist(n->resist[l][2],e->weather_penalty);
    if(e->weather_type==WEATHER_FIRESTORM){n->armor*=1.5f;n->armor_max*=1.5f;}
    if(e->weather_type==WEATHER_GAMMA){n->shield*=1.5f;n->shield_max*=1.5f;}
}

static void ab_apply_weather_room(Env* e) {
    for(int i=0;i<e->entity_count;i++)ab_apply_weather_entity(e,&e->entities[i]);
}

static int ab_hostiles_alive(Env* e) { int n=0; for(int i=0;i<e->entity_count;i++) n+=e->entities[i].alive&&e->entities[i].kind==ENTITY_HOSTILE; return n; }
static int ab_gate_targets_alive(Env* e) { int n=0; for(int i=0;i<e->entity_count;i++) n+=e->entities[i].alive&&e->entities[i].gate_required; return n; }
static int ab_find_kind(Env* e,int kind) { for(int i=0;i<e->entity_count;i++) if(e->entities[i].alive&&e->entities[i].kind==kind)return i; return -1; }
static int ab_nearest_hostile(Env* e) { int best=-1; float bd=INFINITY; for(int i=0;i<e->entity_count;i++){AbyssEntity*n=&e->entities[i];float d=ab_len(ab_sub(n->pos,e->ship_pos));if(n->alive&&n->kind==ENTITY_HOSTILE&&d<bd){best=i;bd=d;}}return best; }

static void compute_observations(Env* e) {
    obs_t* o=(obs_t*)e->agents[0].observations;
    memset(o,0,ABYSS_OBS_SIZE*sizeof(obs_t)); int k=0;
    float shield_max=e->ship_shield_hp*(e->weather_type==WEATHER_GAMMA?1.5f:1.0f);
    float armor_max=e->ship_armor_hp*(e->weather_type==WEATHER_FIRESTORM?1.5f:1.0f);
    o[k++]=e->room/3.0f; o[k++]=e->tick/(float)e->max_steps; o[k++]=e->shield/shield_max;
    o[k++]=e->armor/armor_max; o[k++]=e->hull/e->ship_hull_hp; o[k++]=e->capacitor/e->cap_capacity;
    o[k++]=e->ship_vel.x/e->prop_speed; o[k++]=e->ship_vel.y/e->prop_speed; o[k++]=e->ship_vel.z/e->prop_speed;
    o[k++]=e->weapon_on; o[k++]=e->prop_on; o[k++]=e->rep_on; o[k++]=e->cache_looted;
    o[k++]=ab_hostiles_alive(e)/(float)ABYSS_MAX_ENTITIES; o[k++]=(e->focus_index+1)/(float)(ABYSS_MAX_ENTITIES+1);
    o[k++]=e->weather_penalty; o[k++]=e->weather_range_multiplier; o[k++]=e->weather_velocity_multiplier/2.0f;
    o[k++]=e->weather_type/4.0f;
    for(int cloud=0;cloud<3;cloud++)o[k++]=ab_cloud_multiplier(e,e->ship_pos,cloud,2.0f)>1.0f;
    o[k++]=e->cargo_open;
    k=ABYSS_GLOBAL_FEATURES;
    for(int i=0;i<ABYSS_MAX_ENTITIES;i++) {
        AbyssEntity*n=&e->entities[i]; Vec3 r=ab_sub(n->pos,e->ship_pos);
        o[k++]=n->kind/6.0f; o[k++]=n->alive; o[k++]=n->locked; o[k++]=n->focused;
        o[k++]=r.x/100000; o[k++]=r.y/100000; o[k++]=r.z/100000;
        o[k++]=n->vel.x/2500; o[k++]=n->vel.y/2500; o[k++]=n->vel.z/2500;
        o[k++]=ab_len(r)/100000; o[k++]=n->shield_max? n->shield/n->shield_max:0;
        o[k++]=n->armor_max? n->armor/n->armor_max:0; o[k++]=n->hull_max? n->hull/n->hull_max:0;
        o[k++]=n->signature/500; o[k++]=n->type_index/128.0f;
    }
    int used[ABYSS_MAX_OBSTACLES]={0};
    for(int slot=0;slot<ABYSS_OBS_OBSTACLES;slot++) {
        int best=-1;float clearance=INFINITY;
        for(int i=0;i<e->obstacle_count;i++)if(!used[i]){float value=ab_len(ab_sub(e->obstacles[i].center,e->ship_pos))-e->obstacles[i].radius;if(value<clearance){clearance=value;best=i;}}
        if(best<0){k+=ABYSS_OBSTACLE_FEATURES;continue;}used[best]=1;
        AbyssObstacle* obstacle=&e->obstacles[best];Vec3 relative=ab_sub(obstacle->center,e->ship_pos);
        o[k++]=relative.x/100000;o[k++]=relative.y/100000;o[k++]=relative.z/100000;
        o[k++]=obstacle->radius/25000;o[k++]=ab_clip(clearance/100000,-1,1);
    }
}

void puf_init(Env* e, Dict* kwargs) {
    e->num_agents=1;
    e->agents[0].policy=0;
    e->agents[0].action_mask=NULL;
    e->max_steps=(int)dict_get(kwargs,"max_steps");
    e->filament_tier=(int)dict_get(kwargs,"filament_tier");
    e->weather_type=(int)dict_get(kwargs,"weather_type");
    e->boundary_radius=(float)dict_get(kwargs,"boundary_radius");
    e->boundary_damage=(float)dict_get(kwargs,"boundary_damage");
    e->ship_shield_hp=(float)dict_get(kwargs,"ship_shield_hp");
    e->ship_armor_hp=(float)dict_get(kwargs,"ship_armor_hp");
    e->ship_hull_hp=(float)dict_get(kwargs,"ship_hull_hp");
    e->cap_capacity=(float)dict_get(kwargs,"cap_capacity");
    e->cap_recharge_time=(float)dict_get(kwargs,"cap_recharge_time");
    e->base_speed=(float)dict_get(kwargs,"base_speed");
    e->prop_speed=(float)dict_get(kwargs,"prop_speed");
    e->signature=(float)dict_get(kwargs,"signature");
    e->scan_resolution=(float)dict_get(kwargs,"scan_resolution");
    e->lock_range=(float)dict_get(kwargs,"lock_range");
    e->weapon_volley=(float)dict_get(kwargs,"weapon_volley");
    e->weapon_cycle=(float)dict_get(kwargs,"weapon_cycle");
    e->weapon_optimal=(float)dict_get(kwargs,"weapon_optimal");
    e->weapon_falloff=(float)dict_get(kwargs,"weapon_falloff");
    e->weapon_tracking=(float)dict_get(kwargs,"weapon_tracking");
    e->rep_amount=(float)dict_get(kwargs,"rep_amount");
    e->rep_cycle=(float)dict_get(kwargs,"rep_cycle");
    e->rep_cap_cost=(float)dict_get(kwargs,"rep_cap_cost");
    e->prop_cap_per_s=(float)dict_get(kwargs,"prop_cap_per_s");
    e->reward_hostile_kill=(float)dict_get(kwargs,"reward_hostile_kill");
    e->reward_cache_kill=(float)dict_get(kwargs,"reward_cache_kill");
    e->reward_loot=(float)dict_get(kwargs,"reward_loot");
    e->reward_room_clear=(float)dict_get(kwargs,"reward_room_clear");
    e->reward_success=(float)dict_get(kwargs,"reward_success");
    e->reward_completion_speed=(float)dict_get(kwargs,"reward_completion_speed");
    e->reward_failure=(float)dict_get(kwargs,"reward_failure");
    e->reward_wasted_rep=(float)dict_get(kwargs,"reward_wasted_rep");
    e->reward_invalid_loot=(float)dict_get(kwargs,"reward_invalid_loot");
    e->reward_cargo_open=(float)dict_get(kwargs,"reward_cargo_open");
    e->reward_step=(float)dict_get(kwargs,"reward_step");
    float sh[4]={0,.2f,.4f,.5f}, ar[4]={.5f,.35f,.25f,.2f}, hu[4]={.33f,.33f,.33f,.33f};
    memcpy(e->ship_resist[0],sh,sizeof(sh));
    memcpy(e->ship_resist[1],ar,sizeof(ar));
    memcpy(e->ship_resist[2],hu,sizeof(hu));
    memcpy(e->base_ship_resist,e->ship_resist,sizeof(e->ship_resist));
    e->weapon_damage_mix[0]=9.0f/11.0f;
    e->weapon_damage_mix[1]=2.0f/11.0f;
}

void puf_log(Log* log, Dict* out) {
    dict_set(out,"perf",log->perf);
    dict_set(out,"score",log->score);
    dict_set(out,"episode_return",log->episode_return);
    dict_set(out,"episode_length",log->episode_length);
    dict_set(out,"survival_rate",log->survival_rate);
    dict_set(out,"completion_rate",log->completion_rate);
    dict_set(out,"rooms_cleared",log->rooms_cleared);
}

void puf_reset(Env* e) {
    e->tick=0;e->room=1;e->rooms_cleared=0;e->episode_return=0;e->weapon_on=e->prop_on=e->rep_on=0;e->cargo_open=0;
    e->scenario_episode=ab_rand_u32(e)%GENERATED_EPISODE_COUNT;
    memcpy(e->ship_resist,e->base_ship_resist,sizeof(e->ship_resist));
    e->shield=e->ship_shield_hp;e->armor=e->ship_armor_hp;e->hull=e->ship_hull_hp;e->capacitor=e->cap_capacity;
    e->weapon_cooldown=e->rep_cooldown=0;e->reset_pending=0;ab_roll_weather(e);
    if(e->weather_type==WEATHER_ELECTRICAL)for(int l=0;l<3;l++)e->ship_resist[l][0]=ab_weather_resist(e->ship_resist[l][0],e->weather_penalty);
    if(e->weather_type==WEATHER_EXOTIC)for(int l=0;l<3;l++)e->ship_resist[l][2]=ab_weather_resist(e->ship_resist[l][2],e->weather_penalty);
    if(e->weather_type==WEATHER_FIRESTORM)e->armor*=1.5f;
    if(e->weather_type==WEATHER_GAMMA)e->shield*=1.5f;
    ab_spawn_room(e);ab_apply_weather_room(e);compute_observations(e);
}

static void ab_finish(Env*e,int success){float* rewards=e->agents[0].rewards;float* terminals=e->agents[0].terminals;terminals[0]=1;float speed=success?(1.0f-fminf(1.0f,e->tick/(float)e->max_steps)):0;int survived=e->hull>0;if(success)rewards[0]+=e->reward_success+e->reward_completion_speed*speed;else rewards[0]+=e->reward_failure;e->episode_return+=rewards[0];float perf=100.0f*success+10.0f*survived+5.0f*(e->rooms_cleared/3.0f)+5.0f*speed;e->log.perf+=perf;e->log.score+=perf;e->log.episode_return+=e->episode_return;e->log.episode_length+=e->tick;e->log.completion_rate+=success;e->log.survival_rate+=survived;e->log.rooms_cleared+=e->rooms_cleared;e->log.n++;e->reset_pending=1;}

void puf_step(Env* e) {
    if(e->reset_pending)puf_reset(e);
    float* actions=e->agents[0].actions;float* rewards=e->agents[0].rewards;float* terminals=e->agents[0].terminals;
    rewards[0]=0;terminals[0]=0;e->tick++;
    int nav=(int)actions[0], focus=(int)actions[1], weapon=(int)actions[2];
    int prop=(int)actions[3], rep=(int)actions[4], interact=(int)actions[5];
    if(prop==DESIRED_ON)e->prop_on=1;else if(prop==DESIRED_OFF)e->prop_on=0;
    if(rep==DESIRED_ON)e->rep_on=1;else if(rep==DESIRED_OFF)e->rep_on=0;
    if(weapon==DESIRED_ON)e->weapon_on=1;else if(weapon==DESIRED_OFF)e->weapon_on=0;
    int requested_focus=-1;
    if(focus==FOCUS_NEAREST_HOSTILE)requested_focus=ab_nearest_hostile(e);else if(focus==FOCUS_CACHE)requested_focus=ab_find_kind(e,ENTITY_CACHE);
    if(requested_focus>=0) {
        AbyssEntity* requested=&e->entities[requested_focus];
        float distance=ab_len(ab_sub(requested->pos,e->ship_pos));
        if(distance<=e->lock_range) {
            float scan=e->scan_resolution*(e->weather_type==WEATHER_EXOTIC?1.5f:1.0f);
            if(requested->lock_time<=0) requested->lock_time=ab_lock_time(scan,requested->signature);
            requested->lock_progress+=1.0f;
            if(requested->lock_progress>=requested->lock_time) { requested->locked=1; e->focus_index=requested_focus; }
        }
    }
    for(int i=0;i<e->entity_count;i++)e->entities[i].focused=i==e->focus_index;
    int target=-1;if(nav==NAV_CACHE)target=ab_find_kind(e,ENTITY_CACHE);else if(nav==NAV_FOCUS)target=e->focus_index;else if(nav==NAV_CONDUIT)target=ab_find_kind(e,ENTITY_CONDUIT);
    float tachyon_velocity=ab_cloud_multiplier(e,e->ship_pos,CLOUD_TACHYON,4.0f);
    float max_speed=(e->prop_on?e->prop_speed:e->base_speed)*e->weather_velocity_multiplier*tachyon_velocity;
    if(target>=0){Vec3 desired=ab_mul(ab_unit(ab_sub(e->entities[target].pos,e->ship_pos)),max_speed);float response=tachyon_velocity>1?0.90f:0.65f;e->ship_vel=ab_add(ab_mul(e->ship_vel,1-response),ab_mul(desired,response));}
    else if(nav==NAV_STOP)e->ship_vel=ab_mul(e->ship_vel,0.25f);
    e->ship_pos=ab_add(e->ship_pos,e->ship_vel);
    ab_resolve_obstacles(e,&e->ship_pos,&e->ship_vel,60.0f);
    float player_signature=e->signature*ab_cloud_multiplier(e,e->ship_pos,CLOUD_BIOLUMINESCENCE,4.0f);
    for(int i=0;i<e->entity_count;i++){AbyssEntity*n=&e->entities[i];if(!n->alive||n->kind!=ENTITY_HOSTILE)continue;Vec3 radial=ab_sub(e->ship_pos,n->pos);float d=ab_len(radial);Vec3 u=ab_unit(radial), tangent=ab_unit(ab_cross(u,(Vec3){0,0,1}));float local_tachyon=ab_cloud_multiplier(e,n->pos,CLOUD_TACHYON,4.0f);float chase_max=n->max_speed*e->weather_velocity_multiplier*local_tachyon;const GeneratedNpcDef* movement=&GENERATED_NPCS[n->type_index];float radial_speed=ab_clip((d-n->orbit_range)*movement->radial_gain,-chase_max,chase_max);n->vel=ab_add(ab_mul(u,radial_speed),ab_mul(tangent,fminf(n->orbit_speed*movement->orbit_speed_scale,n->max_speed)*local_tachyon));if(ab_len(n->vel)>chase_max)n->vel=ab_mul(ab_unit(n->vel),chase_max);n->pos=ab_add(n->pos,n->vel);ab_resolve_obstacles(e,&n->pos,&n->vel,fmaxf(20,n->signature*.25f));Vec3 relative_velocity=ab_sub(e->ship_vel,n->vel);float angular=ab_len(ab_cross(radial,relative_velocity))/fmaxf(d*d,1);float tracking=n->tracking;for(int t=0;t<e->entity_count;t++){AbyssEntity*p=&e->entities[t];if(p->kind==ENTITY_TRACKING_PYLON&&ab_len(ab_sub(p->pos,n->pos))<=p->effect_range)tracking*=1+p->effect_strength;}float hit=n->tracking>0?ab_turret_hit_chance(angular,tracking,player_signature,d,n->optimal*e->weather_range_multiplier,n->falloff*e->weather_range_multiplier):1.0f;ab_damage_layers(n->dps*hit,n->damage_mix,&e->shield,&e->armor,&e->hull,e->ship_resist);e->capacitor=fmaxf(0,e->capacitor-n->neutralizer);}

    // Suppressors only affect explicitly eligible drone/missile entities. They
    // cannot damage the player ship or ordinary NPC ships.
    for(int t=0;t<e->entity_count;t++){AbyssEntity*p=&e->entities[t];if(p->kind!=ENTITY_SUPPRESSOR)continue;p->effect_cooldown=fmaxf(0,p->effect_cooldown-1);if(p->effect_cooldown>0)continue;for(int i=0;i<e->entity_count;i++){AbyssEntity*n=&e->entities[i];if(!n->alive||!n->suppressor_vulnerable)continue;if(ab_len(ab_sub(p->pos,n->pos))>p->effect_range)continue;float mix[4]={0,.5f,0,.5f};ab_damage_layers(2*p->effect_strength,mix,&n->shield,&n->armor,&n->hull,n->resist);if(n->hull<=0)n->alive=0;}p->effect_cooldown=p->effect_cycle;}
    float recharge=e->cap_recharge_time*(e->weather_type==WEATHER_ELECTRICAL?.5f:1.0f);
    e->capacitor=fminf(e->cap_capacity,e->capacitor+e->cap_capacity/recharge);
    if(e->prop_on)e->capacitor=fmaxf(0,e->capacitor-e->prop_cap_per_s);
    e->rep_cooldown=fmaxf(0,e->rep_cooldown-1);if(e->rep_on&&e->rep_cooldown<=0&&e->capacitor>=e->rep_cap_cost){float armor_max=e->ship_armor_hp*(e->weather_type==WEATHER_FIRESTORM?1.5f:1.0f);float before=e->armor;e->armor=fminf(armor_max,e->armor+e->rep_amount);float effective=e->armor-before;float wasted=fmaxf(0,e->rep_amount-effective)/fmaxf(e->rep_amount,1);rewards[0]+=e->reward_wasted_rep*wasted;e->capacitor-=e->rep_cap_cost;e->rep_cooldown=e->rep_cycle;}
    e->weapon_cooldown=fmaxf(0,e->weapon_cooldown-1);if(e->weapon_on&&e->focus_index>=0&&e->weapon_cooldown<=0){AbyssEntity*n=&e->entities[e->focus_index];Vec3 radial=ab_sub(n->pos,e->ship_pos);float d=ab_len(radial);Vec3 relative_velocity=ab_sub(n->vel,e->ship_vel);float angular=ab_len(ab_cross(radial,relative_velocity))/fmaxf(d*d,1);float tracking=e->weapon_tracking;for(int t=0;t<e->entity_count;t++){AbyssEntity*p=&e->entities[t];if(p->kind==ENTITY_TRACKING_PYLON&&ab_len(ab_sub(p->pos,e->ship_pos))<=p->effect_range)tracking*=1+p->effect_strength;}float target_signature=n->signature*ab_cloud_multiplier(e,n->pos,CLOUD_BIOLUMINESCENCE,4.0f);float hit=ab_turret_hit_chance(angular,tracking,target_signature,d,e->weapon_optimal*e->weather_range_multiplier,e->weapon_falloff*e->weather_range_multiplier);if(n->alive&&n->locked){if(ab_rand(e)<=hit){float quality=0.5f+ab_rand(e);if(ab_rand(e)<0.01f)quality=3.0f;ab_damage_layers(e->weapon_volley*quality,e->weapon_damage_mix,&n->shield,&n->armor,&n->hull,n->resist);if(n->hull<=0){n->alive=0;rewards[0]+=n->kind==ENTITY_HOSTILE?e->reward_hostile_kill:e->reward_cache_kill;e->weapon_on=0;}}e->weapon_cooldown=e->weapon_cycle;}}
    int cache=ab_find_kind(e,ENTITY_CACHE),gate=ab_find_kind(e,ENTITY_CONDUIT);
    int cache_entity=-1;for(int i=0;i<e->entity_count;i++)if(e->entities[i].kind==ENTITY_CACHE){cache_entity=i;break;}
    if(interact==INTERACT_LOOT&&!e->cache_looted&&cache<0&&cache_entity>=0){float cargo_distance=ab_len(ab_sub(e->entities[cache_entity].pos,e->ship_pos));if(cargo_distance<=2500){if(e->cargo_open){e->cache_looted=1;e->cargo_open=0;rewards[0]+=e->reward_loot;}else e->cargo_open=1;}else rewards[0]+=e->reward_invalid_loot;}
    else if(interact==INTERACT_LOOT)rewards[0]+=e->reward_invalid_loot;
    if(e->cargo_open)rewards[0]+=e->reward_cargo_open;
    if(interact==INTERACT_GATE&&e->cache_looted&&ab_gate_targets_alive(e)==0&&gate>=0&&ab_len(ab_sub(e->entities[gate].pos,e->ship_pos))<6000){e->rooms_cleared++;rewards[0]+=e->reward_room_clear;if(e->room==3){ab_finish(e,1);compute_observations(e);return;}e->room++;ab_spawn_room(e);ab_apply_weather_room(e);}
    if(ab_len(e->ship_pos)>e->boundary_radius){float mix[4]={.25f,.25f,.25f,.25f};ab_damage_layers(e->boundary_damage,mix,&e->shield,&e->armor,&e->hull,e->ship_resist);}
    rewards[0]+=e->reward_step;e->episode_return+=rewards[0];if(e->hull<=0||e->tick>=e->max_steps)ab_finish(e,0);compute_observations(e);
}

void puf_render(Env* e) { (void)e; }
void puf_close(Env* e) { (void)e; }
