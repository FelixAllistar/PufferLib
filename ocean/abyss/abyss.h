#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "pufferenv.h"

#define ABYSS_NAV_ACTIONS (2 + ABYSS_MAX_ENTITIES)
#define ABYSS_TARGET_ACTIONS (1 + 2*ABYSS_MAX_ENTITIES)
#define ABYSS_WEAPON_ACTIONS (2 + ABYSS_MAX_ENTITIES)
#define ABYSS_INTERACTION_ACTIONS (2 + 2*ABYSS_MAX_ENTITIES)
#define ABYSS_ACTION_MASK_SIZE (ABYSS_NAV_ACTIONS + ABYSS_TARGET_ACTIONS + ABYSS_WEAPON_ACTIONS + 3 + 3 + ABYSS_INTERACTION_ACTIONS)
#define ACT_SIZES {ABYSS_NAV_ACTIONS, ABYSS_TARGET_ACTIONS, ABYSS_WEAPON_ACTIONS, 3, 3, ABYSS_INTERACTION_ACTIONS}
#define NUM_ATNS ABYSS_ACTION_LANES

#define ABYSS_MAX_ENTITIES 64
#define ABYSS_ENTITY_FEATURES 18
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
enum { LAYER_SHIELD, LAYER_ARMOR, LAYER_HULL };
enum { EFFECT_CYCLE_START, EFFECT_CYCLE_END };
enum { NAV_HOLD, NAV_STOP, NAV_APPROACH_BASE };
enum { TARGET_HOLD, TARGET_LOCK_BASE };
#define TARGET_FOCUS_BASE (TARGET_LOCK_BASE + ABYSS_MAX_ENTITIES)
enum { WEAPON_HOLD, WEAPON_OFF, WEAPON_FIRE_BASE };
enum { DESIRED_HOLD, DESIRED_ON, DESIRED_OFF };
enum { INTERACT_HOLD, INTERACT_LOOT, INTERACT_OPEN_BASE };
#define INTERACT_ACTIVATE_BASE (INTERACT_OPEN_BASE + ABYSS_MAX_ENTITIES)
enum { INTERACTION_NONE, INTERACTION_OPEN, INTERACTION_ACTIVATE };
enum { POINTER_NONE, POINTER_OPEN, POINTER_ACTIVATE, POINTER_FOCUS, POINTER_LOCK, POINTER_APPROACH };

typedef struct { float x, y, z; } Vec3;
typedef float obs_t;
#include "generated_scenarios.h"
#include "generated_colliders.h"
typedef struct { Vec3 center; float radius; } AbyssObstacle;
typedef struct { Vec3 center, radii; float yaw, pitch; } AbyssCloudLobe;
typedef struct { unsigned char kind, lobe_count; AbyssCloudLobe lobes[4]; } AbyssCloud;
typedef struct {
    unsigned char kind, alive, locking, locked, focused;
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
    float survival_rate, completion_rate, rooms_cleared, caches_looted, env_id, n;
    float death_rate, combat_death_rate, boundary_death_rate, timeout_rate;
    float timeout_threats_rate, timeout_cache_rate, timeout_gate_ready_rate;
    float survived_incomplete_rate;
    float min_cap_fraction, cap_dry_rate, cap_dry_fraction;
    float prop_uptime, rep_uptime, weapon_uptime, weapon_idle_threat_fraction;
    float rep_starved_rate, wasted_rep_fraction;
    float prop_cap_spent, rep_cap_spent, neut_cap_drained;
};

struct Env {
    Log log;
    Agent agents[1];
    int num_agents;
    int tag, boundary_reached;
    unsigned int rng;
    int tick, max_steps, room, rooms_cleared, caches_looted, entity_count, focus_index;
    int weapon_target_index, weapon_desired_target_index, navigation_target_index, cargo_index;
    int interaction_kind, interaction_target_index;
    int cloud_count, obstacle_count;
    int policy_to_entity[ABYSS_MAX_ENTITIES], entity_to_policy[ABYSS_MAX_ENTITIES];
    int scenario_episode;
    int filament_tier, weather_type;
    int weapon_on, prop_on, prop_desired_on, rep_on, cache_looted, cargo_open, reset_pending;
    float episode_return;
    int boundary_kill, cap_dry_ticks, prop_ticks, rep_ticks, weapon_ticks;
    int threat_ticks, weapon_idle_threat_ticks, rep_starved_ticks;
    float min_cap_fraction, prop_cap_spent, rep_cap_spent, neut_cap_drained;
    float wasted_rep_amount, total_rep_amount;
    Vec3 ship_pos, ship_vel;
    float shield, armor, hull, capacitor;
    float boundary_radius, boundary_damage;
    float weather_penalty, weather_range_multiplier, weather_velocity_multiplier;
    float ship_shield_hp, ship_armor_hp, ship_hull_hp;
    float ship_resist[3][4], base_ship_resist[3][4];
    float cap_capacity, cap_recharge_time;
    float ship_mass_kg, prop_mass_addition_kg, inertia_modifier;
    int distance_observation_lag_min_ticks, distance_observation_lag_max_ticks;
    int distance_observation_lag_ticks;
    float base_speed, prop_speed, signature, prop_signature_multiplier;
    float scan_resolution, lock_range;
    float weapon_volley, weapon_cycle, weapon_cooldown, weapon_optimal, weapon_falloff, weapon_tracking;
    int weapon_count;
    float weapon_cap_cost_each;
    float weapon_damage_mix[4];
    int rep_layer, rep_effect_timing, rep_cycle_active;
    float rep_amount, rep_cycle, rep_cooldown, rep_cap_cost;
    float prop_cycle, prop_cooldown, prop_cap_cost;
    float reward_hostile_kill, reward_cache_kill, reward_loot, reward_room_clear;
    float reward_success, reward_completion_speed, reward_failure;
    float reward_wasted_rep, reward_invalid_loot, reward_cargo_open, reward_step;
    float reward_pointer_action, reward_cap_spent, reward_low_cap;
    float cap_reserve_fraction;
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

// EVE's recharge curve is easiest to integrate in sqrt(cap fraction) space:
// y(t+dt) = 1 - (1-y(t))*exp(-5*dt/recharge_time), cap = capacity*y^2.
// This is equivalent to dC/dt = 10*Cmax/T*(sqrt(C/Cmax)-C/Cmax)
// and reaches the familiar 2.5*Cmax/T peak recharge at 25% capacitor.
static float ab_capacitor_after_recharge(float capacitor, float capacity,
        float recharge_time, float seconds) {
    if(capacity<=0||recharge_time<=0||seconds<=0)return ab_clip(capacitor,0,capacity);
    float fraction=ab_clip(capacitor/capacity,0,1);
    float y=sqrtf(fraction);
    float next_y=1.0f-(1.0f-y)*expf(-5.0f*seconds/recharge_time);
    return capacity*next_y*next_y;
}

// Capacitor is the common opportunity cost of propulsion, repair, and weapons.
// Charge the cost when a cycle is actually paid, not when a toggle is requested.
static void ab_spend_capacitor(Env* e, float amount, float* rewards, float* metric) {
    e->capacitor-=amount;
    if(metric!=NULL)*metric+=amount;
    if(e->cap_capacity>0)
        rewards[0]+=e->reward_cap_spent*amount/e->cap_capacity;
}

static void ab_apply_cap_reserve_reward(Env* e, float* rewards) {
    if(e->cap_capacity<=0||e->cap_reserve_fraction<=0)return;
    float fraction=ab_clip(e->capacitor/e->cap_capacity,0,1);
    if(fraction>=e->cap_reserve_fraction)return;
    float shortfall=(e->cap_reserve_fraction-fraction)/e->cap_reserve_fraction;
    rewards[0]+=e->reward_low_cap*shortfall*shortfall;
}

static float ab_layer_max_hp(Env* e, int layer) {
    if(layer==LAYER_SHIELD)
        return e->ship_shield_hp*(e->weather_type==WEATHER_GAMMA?1.5f:1.0f);
    if(layer==LAYER_ARMOR)
        return e->ship_armor_hp*(e->weather_type==WEATHER_FIRESTORM?1.5f:1.0f);
    return e->ship_hull_hp;
}

static float* ab_layer_hp(Env* e, int layer) {
    if(layer==LAYER_SHIELD)return &e->shield;
    if(layer==LAYER_ARMOR)return &e->armor;
    return &e->hull;
}

static void ab_apply_local_repair(Env* e, float* rewards) {
    float* hp=ab_layer_hp(e,e->rep_layer);
    float before=*hp;
    *hp=fminf(ab_layer_max_hp(e,e->rep_layer),*hp+e->rep_amount);
    float effective=*hp-before;
    float wasted=fmaxf(0,e->rep_amount-effective);
    rewards[0]+=e->reward_wasted_rep*wasted/fmaxf(e->rep_amount,1);
    e->wasted_rep_amount+=wasted;
    e->total_rep_amount+=e->rep_amount;
}

static void ab_step_repair_module(Env* e, float* rewards, float seconds) {
    if(e->rep_cycle_active){
        e->rep_cooldown=fmaxf(0,e->rep_cooldown-seconds);
        if(e->rep_cooldown<=0){
            if(e->rep_effect_timing==EFFECT_CYCLE_END)
                ab_apply_local_repair(e,rewards);
            e->rep_cycle_active=0;
        }
    }
    if(!e->rep_on||e->rep_cycle_active)return;
    if(e->capacitor<e->rep_cap_cost){
        e->rep_starved_ticks++;
        return;
    }
    ab_spend_capacitor(e,e->rep_cap_cost,rewards,&e->rep_cap_spent);
    e->rep_cycle_active=1;
    e->rep_cooldown=e->rep_cycle;
    if(e->rep_effect_timing==EFFECT_CYCLE_START)
        ab_apply_local_repair(e,rewards);
}

static float ab_ship_time_constant(Env* e, float local_tachyon) {
    float mass=e->ship_mass_kg+(e->prop_on?e->prop_mass_addition_kg:0);
    float inertia=e->inertia_modifier*(local_tachyon>1.0f?.5f:1.0f);
    return fmaxf(.01f,mass*inertia/1000000.0f);
}

static Vec3 ab_observed_relative(Env* e, Vec3 position, Vec3 velocity) {
    Vec3 current=ab_sub(position,e->ship_pos);
    Vec3 relative_velocity=ab_sub(velocity,e->ship_vel);
    return ab_sub(current,ab_mul(relative_velocity,(float)e->distance_observation_lag_ticks));
}

static int ab_entity_for_slot(Env* e, int slot) {
    if(slot<0||slot>=ABYSS_MAX_ENTITIES)return -1;
    int entity=e->policy_to_entity[slot];
    return entity>=0&&entity<e->entity_count?entity:-1;
}

static int ab_slot_for_entity(Env* e, int entity) {
    return entity>=0&&entity<ABYSS_MAX_ENTITIES?e->entity_to_policy[entity]:-1;
}

static void ab_assign_policy_slots(Env* e) {
    for(int i=0;i<ABYSS_MAX_ENTITIES;i++)e->policy_to_entity[i]=e->entity_to_policy[i]=-1;
    for(int i=0;i<e->entity_count;i++)e->policy_to_entity[i]=i;
    // Random room-local permutation prevents the actor from learning that cache/gate
    // always occupy magic indices. Slot identity remains stable until the next room.
    for(int i=e->entity_count-1;i>0;i--){int j=(int)(ab_rand_u32(e)%(unsigned int)(i+1));int t=e->policy_to_entity[i];e->policy_to_entity[i]=e->policy_to_entity[j];e->policy_to_entity[j]=t;}
    for(int slot=0;slot<e->entity_count;slot++)e->entity_to_policy[e->policy_to_entity[slot]]=slot;
}

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
    t->kind=kind;t->alive=1;t->radius=7500;t->signature=500;t->effect_range=range;
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
    memset(e->entities,0,sizeof(e->entities)); e->entity_count=0; e->focus_index=-1;
    e->weapon_target_index=-1;e->weapon_desired_target_index=-1;
    e->navigation_target_index=-1;e->cargo_index=-1;e->cache_looted=0;e->cargo_open=0;
    e->interaction_kind=INTERACTION_NONE;e->interaction_target_index=-1;
    int room_offset=e->scenario_episode*3+(e->room-1);
    const GeneratedRoom* recorded=&GENERATED_ROOMS[room_offset];
    AbyssEntity* cache=&e->entities[e->entity_count++]; cache->kind=ENTITY_CACHE; cache->alive=1;
    cache->pos=(Vec3){recorded->cache_position[0],recorded->cache_position[1],recorded->cache_position[2]}; cache->radius=1200; cache->signature=500;
    cache->shield=cache->shield_max=250; cache->armor=cache->armor_max=500; cache->hull=cache->hull_max=450;
    AbyssEntity* gate=&e->entities[e->entity_count++]; gate->kind=ENTITY_CONDUIT; gate->alive=1; gate->signature=1000;
    gate->pos=(Vec3){recorded->gate_position[0],recorded->gate_position[1],recorded->gate_position[2]}; gate->radius=5000;
    for(int i=0;i<recorded->hostile_count;i++)ab_add_generated_hostile(e,recorded->hostiles[i]);
    for(int i=0;i<recorded->tower_count;i++)ab_add_recorded_tower(e,recorded->towers[i]);
    ab_assign_policy_slots(e);
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
static int ab_damageable(AbyssEntity* n) { return n->alive&&(n->kind==ENTITY_HOSTILE||n->kind==ENTITY_CACHE); }
static int ab_room_cache_requirement_met(Env* e) {
    return e->cache_looted&&(e->room<3||e->caches_looted==3);
}
static float ab_surface_distance(Env* e,AbyssEntity* n) {
    return fmaxf(0,ab_len(ab_sub(n->pos,e->ship_pos))-n->radius);
}
static int ab_conduit_in_activation_range(Env* e,AbyssEntity* n) {
    return ab_surface_distance(e,n)<=2500;
}

static void ab_compute_action_mask(Env* e) {
    unsigned char* mask=e->agents[0].action_mask;if(mask==NULL)return;
    memset(mask,0,ABYSS_ACTION_MASK_SIZE*sizeof(unsigned char));
    int nav_offset=0;
    int target_offset=nav_offset+ABYSS_NAV_ACTIONS;
    int weapon_offset=target_offset+ABYSS_TARGET_ACTIONS;
    int prop_offset=weapon_offset+ABYSS_WEAPON_ACTIONS;
    int rep_offset=prop_offset+3;
    int interaction_offset=rep_offset+3;

    mask[nav_offset+NAV_HOLD]=1;
    if(e->interaction_kind==INTERACTION_NONE)mask[nav_offset+NAV_STOP]=1;
    mask[target_offset+TARGET_HOLD]=1;
    mask[weapon_offset+WEAPON_HOLD]=1;
    mask[prop_offset+DESIRED_HOLD]=1;
    mask[rep_offset+DESIRED_HOLD]=1;
    mask[interaction_offset+INTERACT_HOLD]=1;

    if(e->weapon_on||e->weapon_desired_target_index>=0)mask[weapon_offset+WEAPON_OFF]=1;
    mask[prop_offset+(e->prop_desired_on?DESIRED_OFF:DESIRED_ON)]=1;
    mask[rep_offset+(e->rep_on?DESIRED_OFF:DESIRED_ON)]=1;
    if(e->cargo_open)mask[interaction_offset+INTERACT_LOOT]=1;

    for(int slot=0;slot<ABYSS_MAX_ENTITIES;slot++) {
        int index=ab_entity_for_slot(e,slot);if(index<0)continue;
        AbyssEntity* n=&e->entities[index];float distance=ab_len(ab_observed_relative(e,n->pos,n->vel));
        // A dead cache remains an addressable wreck. All other dead slots are retained
        // for identity stability but cannot receive new actions.
        int addressable=n->alive||(n->kind==ENTITY_CACHE&&!e->cache_looted);
        if(e->interaction_kind==INTERACTION_NONE&&addressable&&e->navigation_target_index!=index)
            mask[nav_offset+NAV_APPROACH_BASE+slot]=1;
        if(ab_damageable(n)&&!n->locked&&!n->locking&&distance<=e->lock_range)
            mask[target_offset+TARGET_LOCK_BASE+slot]=1;
        if(ab_damageable(n)&&n->locked&&!n->focused)
            mask[target_offset+TARGET_FOCUS_BASE+slot]=1;
        if(ab_damageable(n)&&n->locked&&e->weapon_desired_target_index!=index)
            mask[weapon_offset+WEAPON_FIRE_BASE+slot]=1;
        if(e->interaction_kind==INTERACTION_NONE&&n->kind==ENTITY_CACHE&&!n->alive&&!e->cache_looted&&!e->cargo_open)
            mask[interaction_offset+INTERACT_OPEN_BASE+slot]=1;
        if(e->interaction_kind==INTERACTION_NONE&&n->kind==ENTITY_CONDUIT&&ab_room_cache_requirement_met(e)&&ab_gate_targets_alive(e)==0)
            mask[interaction_offset+INTERACT_ACTIVATE_BASE+slot]=1;
    }
}

static void compute_observations(Env* e) {
    obs_t* o=(obs_t*)e->agents[0].observations;
    memset(o,0,ABYSS_OBS_SIZE*sizeof(obs_t)); int k=0;
    float shield_max=e->ship_shield_hp*(e->weather_type==WEATHER_GAMMA?1.5f:1.0f);
    float armor_max=e->ship_armor_hp*(e->weather_type==WEATHER_FIRESTORM?1.5f:1.0f);
    o[k++]=(e->room-1)/3.0f; o[k++]=e->tick/(float)e->max_steps; o[k++]=e->shield/shield_max;
    o[k++]=e->armor/armor_max; o[k++]=e->hull/e->ship_hull_hp; o[k++]=e->capacitor/e->cap_capacity;
    o[k++]=e->ship_vel.x/e->prop_speed; o[k++]=e->ship_vel.y/e->prop_speed; o[k++]=e->ship_vel.z/e->prop_speed;
    o[k++]=e->weapon_on; o[k++]=e->prop_on; o[k++]=e->rep_cycle_active; o[k++]=e->cache_looted;
    o[k++]=ab_hostiles_alive(e)/(float)ABYSS_MAX_ENTITIES; o[k++]=(ab_slot_for_entity(e,e->focus_index)+1)/(float)(ABYSS_MAX_ENTITIES+1);
    o[k++]=e->weather_penalty; o[k++]=e->weather_range_multiplier; o[k++]=e->weather_velocity_multiplier/2.0f;
    o[k++]=e->weather_type/4.0f;
    for(int cloud=0;cloud<3;cloud++)o[k++]=ab_cloud_multiplier(e,e->ship_pos,cloud,2.0f)>1.0f;
    o[k++]=e->cargo_open;
    o[23]=e->weapon_desired_target_index>=0&&
        !(e->weapon_on&&e->weapon_target_index==e->weapon_desired_target_index);
    o[24]=(ab_slot_for_entity(e,e->weapon_desired_target_index)+1)/(float)(ABYSS_MAX_ENTITIES+1);
    o[25]=e->caches_looted/3.0f;
    k=ABYSS_GLOBAL_FEATURES;
    for(int slot=0;slot<ABYSS_MAX_ENTITIES;slot++) {
        int index=ab_entity_for_slot(e,slot);
        if(index<0){k+=ABYSS_ENTITY_FEATURES;continue;}
        AbyssEntity*n=&e->entities[index]; Vec3 r=ab_observed_relative(e,n->pos,n->vel);
        // Live sensors lose destroyed NPC rows/cards. Keep the slot reserved but
        // expose an absent vector; the dead cache remains visible as its wreck.
        if(!n->alive&&n->kind!=ENTITY_CACHE){k+=ABYSS_ENTITY_FEATURES;continue;}
        float lock_state=n->focused?1.0f:n->locked?.66f:n->locking?.33f:0.0f;
        o[k++]=n->kind/6.0f; o[k++]=n->alive; o[k++]=lock_state; o[k++]=n->focused;
        o[k++]=e->weapon_on&&e->weapon_target_index==index;
        o[k++]=1.0f;
        o[k++]=r.x/100000; o[k++]=r.y/100000; o[k++]=r.z/100000;
        o[k++]=n->vel.x/2500; o[k++]=n->vel.y/2500; o[k++]=n->vel.z/2500;
        o[k++]=ab_len(r)/100000; o[k++]=n->shield_max? n->shield/n->shield_max:0;
        o[k++]=n->armor_max? n->armor/n->armor_max:0; o[k++]=n->hull_max? n->hull/n->hull_max:0;
        o[k++]=n->signature/500; o[k++]=n->type_index/128.0f;
    }
    int used[ABYSS_MAX_OBSTACLES]={0};
    for(int slot=0;slot<ABYSS_OBS_OBSTACLES;slot++) {
        int best=-1;float clearance=INFINITY;
        for(int i=0;i<e->obstacle_count;i++)if(!used[i]){float value=ab_len(ab_observed_relative(e,e->obstacles[i].center,(Vec3){0,0,0}))-e->obstacles[i].radius;if(value<clearance){clearance=value;best=i;}}
        if(best<0){k+=ABYSS_OBSTACLE_FEATURES;continue;}used[best]=1;
        AbyssObstacle* obstacle=&e->obstacles[best];Vec3 relative=ab_observed_relative(e,obstacle->center,(Vec3){0,0,0});
        o[k++]=relative.x/100000;o[k++]=relative.y/100000;o[k++]=relative.z/100000;
        o[k++]=obstacle->radius/25000;o[k++]=ab_clip(clearance/100000,-1,1);
    }
    ab_compute_action_mask(e);
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
    e->ship_mass_kg=(float)dict_get(kwargs,"ship_mass_kg");
    e->prop_mass_addition_kg=(float)dict_get(kwargs,"prop_mass_addition_kg");
    e->inertia_modifier=(float)dict_get(kwargs,"inertia_modifier");
    e->distance_observation_lag_min_ticks=(int)dict_get(kwargs,"distance_observation_lag_min_ticks");
    e->distance_observation_lag_max_ticks=(int)dict_get(kwargs,"distance_observation_lag_max_ticks");
    assert(e->ship_mass_kg>0&&e->inertia_modifier>0);
    assert(e->distance_observation_lag_min_ticks>=0&&
        e->distance_observation_lag_max_ticks>=e->distance_observation_lag_min_ticks);
    e->base_speed=(float)dict_get(kwargs,"base_speed");
    e->prop_speed=(float)dict_get(kwargs,"prop_speed");
    e->signature=(float)dict_get(kwargs,"signature");
    e->prop_signature_multiplier=(float)dict_get(kwargs,"prop_signature_multiplier");
    e->scan_resolution=(float)dict_get(kwargs,"scan_resolution");
    e->lock_range=(float)dict_get(kwargs,"lock_range");
    e->weapon_volley=(float)dict_get(kwargs,"weapon_volley");
    e->weapon_cycle=(float)dict_get(kwargs,"weapon_cycle");
    e->weapon_count=(int)dict_get(kwargs,"weapon_count");
    e->weapon_cap_cost_each=(float)dict_get(kwargs,"weapon_cap_cost_each");
    e->weapon_optimal=(float)dict_get(kwargs,"weapon_optimal");
    e->weapon_falloff=(float)dict_get(kwargs,"weapon_falloff");
    e->weapon_tracking=(float)dict_get(kwargs,"weapon_tracking");
    e->rep_layer=(int)dict_get(kwargs,"rep_layer");
    e->rep_effect_timing=(int)dict_get(kwargs,"rep_effect_timing");
    e->rep_amount=(float)dict_get(kwargs,"rep_amount");
    e->rep_cycle=(float)dict_get(kwargs,"rep_cycle");
    e->rep_cap_cost=(float)dict_get(kwargs,"rep_cap_cost");
    e->prop_cycle=(float)dict_get(kwargs,"prop_cycle");
    e->prop_cap_cost=(float)dict_get(kwargs,"prop_cap_cost");
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
    e->reward_pointer_action=(float)dict_get(kwargs,"reward_pointer_action");
    e->reward_cap_spent=(float)dict_get(kwargs,"reward_cap_spent");
    e->reward_low_cap=(float)dict_get(kwargs,"reward_low_cap");
    e->cap_reserve_fraction=(float)dict_get(kwargs,"cap_reserve_fraction");
    assert(e->cap_capacity>0&&e->cap_recharge_time>0);
    assert(e->signature>0&&e->prop_signature_multiplier>=1);
    assert(e->weapon_count>0&&e->weapon_cycle>0&&e->weapon_cap_cost_each>=0);
    assert(e->rep_layer>=LAYER_SHIELD&&e->rep_layer<=LAYER_HULL);
    assert(e->rep_effect_timing==EFFECT_CYCLE_START||
        e->rep_effect_timing==EFFECT_CYCLE_END);
    assert(e->rep_cycle>0&&e->rep_cap_cost>=0);
    assert(e->prop_cycle>0&&e->prop_cap_cost>=0);
    assert(e->cap_reserve_fraction>=0&&e->cap_reserve_fraction<=1);
    float sh[4]={
        (float)dict_get(kwargs,"shield_resist_em"),
        (float)dict_get(kwargs,"shield_resist_thermal"),
        (float)dict_get(kwargs,"shield_resist_kinetic"),
        (float)dict_get(kwargs,"shield_resist_explosive")};
    float ar[4]={
        (float)dict_get(kwargs,"armor_resist_em"),
        (float)dict_get(kwargs,"armor_resist_thermal"),
        (float)dict_get(kwargs,"armor_resist_kinetic"),
        (float)dict_get(kwargs,"armor_resist_explosive")};
    float hu[4]={
        (float)dict_get(kwargs,"hull_resist_em"),
        (float)dict_get(kwargs,"hull_resist_thermal"),
        (float)dict_get(kwargs,"hull_resist_kinetic"),
        (float)dict_get(kwargs,"hull_resist_explosive")};
    for(int damage_type=0;damage_type<4;damage_type++){
        assert(sh[damage_type]>=0&&sh[damage_type]<1);
        assert(ar[damage_type]>=0&&ar[damage_type]<1);
        assert(hu[damage_type]>=0&&hu[damage_type]<1);
    }
    memcpy(e->ship_resist[0],sh,sizeof(sh));
    memcpy(e->ship_resist[1],ar,sizeof(ar));
    memcpy(e->ship_resist[2],hu,sizeof(hu));
    memcpy(e->base_ship_resist,e->ship_resist,sizeof(e->ship_resist));
    e->weapon_damage_mix[0]=(float)dict_get(kwargs,"weapon_damage_em");
    e->weapon_damage_mix[1]=(float)dict_get(kwargs,"weapon_damage_thermal");
    e->weapon_damage_mix[2]=(float)dict_get(kwargs,"weapon_damage_kinetic");
    e->weapon_damage_mix[3]=(float)dict_get(kwargs,"weapon_damage_explosive");
    float damage_total=0;
    for(int damage_type=0;damage_type<4;damage_type++)
        damage_total+=e->weapon_damage_mix[damage_type];
    assert(fabsf(damage_total-1.0f)<0.001f);
}

void puf_log(Log* log, Dict* out) {
    dict_set(out,"perf",log->perf);
    dict_set(out,"score",log->score);
    dict_set(out,"episode_return",log->episode_return);
    dict_set(out,"episode_length",log->episode_length);
    dict_set(out,"survival_rate",log->survival_rate);
    dict_set(out,"completion_rate",log->completion_rate);
    dict_set(out,"rooms_cleared",log->rooms_cleared);
    dict_set(out,"caches_looted",log->caches_looted);
    dict_set(out,"death_rate",log->death_rate);
    dict_set(out,"combat_death_rate",log->combat_death_rate);
    dict_set(out,"boundary_death_rate",log->boundary_death_rate);
    dict_set(out,"timeout_rate",log->timeout_rate);
    dict_set(out,"timeout_threats_rate",log->timeout_threats_rate);
    dict_set(out,"timeout_cache_rate",log->timeout_cache_rate);
    dict_set(out,"timeout_gate_ready_rate",log->timeout_gate_ready_rate);
    dict_set(out,"survived_incomplete_rate",log->survived_incomplete_rate);
    dict_set(out,"min_cap_fraction",log->min_cap_fraction);
    dict_set(out,"cap_dry_rate",log->cap_dry_rate);
    dict_set(out,"cap_dry_fraction",log->cap_dry_fraction);
    dict_set(out,"prop_uptime",log->prop_uptime);
    dict_set(out,"rep_uptime",log->rep_uptime);
    dict_set(out,"weapon_uptime",log->weapon_uptime);
    dict_set(out,"weapon_idle_threat_fraction",log->weapon_idle_threat_fraction);
    dict_set(out,"rep_starved_rate",log->rep_starved_rate);
    dict_set(out,"wasted_rep_fraction",log->wasted_rep_fraction);
    dict_set(out,"prop_cap_spent",log->prop_cap_spent);
    dict_set(out,"rep_cap_spent",log->rep_cap_spent);
    dict_set(out,"neut_cap_drained",log->neut_cap_drained);
}

void puf_reset(Env* e) {
    e->tick=0;e->room=1;e->rooms_cleared=0;e->caches_looted=0;e->episode_return=0;
    e->weapon_on=e->prop_on=e->prop_desired_on=e->rep_on=0;e->cargo_open=0;
    e->weapon_target_index=e->weapon_desired_target_index=e->navigation_target_index=-1;
    e->interaction_kind=INTERACTION_NONE;e->interaction_target_index=-1;
    e->boundary_kill=e->cap_dry_ticks=e->prop_ticks=e->rep_ticks=e->weapon_ticks=0;
    e->threat_ticks=e->weapon_idle_threat_ticks=e->rep_starved_ticks=0;
    e->min_cap_fraction=1.0f;
    e->prop_cap_spent=e->rep_cap_spent=e->neut_cap_drained=0;
    e->wasted_rep_amount=e->total_rep_amount=0;
    int lag_span=e->distance_observation_lag_max_ticks-e->distance_observation_lag_min_ticks+1;
    e->distance_observation_lag_ticks=e->distance_observation_lag_min_ticks+
        (int)(ab_rand_u32(e)%(unsigned int)lag_span);
    e->scenario_episode=ab_rand_u32(e)%GENERATED_EPISODE_COUNT;
    memcpy(e->ship_resist,e->base_ship_resist,sizeof(e->ship_resist));
    e->shield=e->ship_shield_hp;e->armor=e->ship_armor_hp;e->hull=e->ship_hull_hp;e->capacitor=e->cap_capacity;
    e->weapon_cooldown=e->rep_cooldown=e->prop_cooldown=0;
    e->rep_cycle_active=0;e->reset_pending=0;ab_roll_weather(e);
    if(e->weather_type==WEATHER_ELECTRICAL)for(int l=0;l<3;l++)e->ship_resist[l][0]=ab_weather_resist(e->ship_resist[l][0],e->weather_penalty);
    if(e->weather_type==WEATHER_EXOTIC)for(int l=0;l<3;l++)e->ship_resist[l][2]=ab_weather_resist(e->ship_resist[l][2],e->weather_penalty);
    if(e->weather_type==WEATHER_FIRESTORM)e->armor*=1.5f;
    if(e->weather_type==WEATHER_GAMMA)e->shield*=1.5f;
    ab_spawn_room(e);ab_apply_weather_room(e);compute_observations(e);
}

static void ab_finish(Env*e,int success){
    float* rewards=e->agents[0].rewards;float* terminals=e->agents[0].terminals;terminals[0]=1;
    success=success&&e->rooms_cleared==3&&e->caches_looted==3;
    float speed=success?(1.0f-fminf(1.0f,e->tick/(float)e->max_steps)):0;
    int survived=e->hull>0;
    if(success)rewards[0]+=e->reward_success+e->reward_completion_speed*speed;
    else rewards[0]+=e->reward_failure;
    e->episode_return+=rewards[0];
    // Completion is the primary objective. At the default 10k-eval sample size,
    // one additional completed episode outweighs the full speed term.
    float cache_fraction=e->caches_looted/3.0f;
    float perf=1000.0f*success+100.0f*cache_fraction+10.0f*survived+
        e->rooms_cleared/3.0f+0.001f*speed;
    e->log.perf+=perf;e->log.score+=perf;e->log.episode_return+=e->episode_return;
    e->log.episode_length+=e->tick;e->log.completion_rate+=success;
    e->log.survival_rate+=survived;e->log.rooms_cleared+=e->rooms_cleared;
    e->log.caches_looted+=e->caches_looted;

    int dead=!success&&e->hull<=0;
    int timed_out=!success&&!dead&&e->tick>=e->max_steps;
    int threats_alive=ab_gate_targets_alive(e)>0;
    int cache_missing=!ab_room_cache_requirement_met(e);
    int gate_ready=!threats_alive&&!cache_missing;
    e->log.death_rate+=dead;
    e->log.combat_death_rate+=dead&&!e->boundary_kill;
    e->log.boundary_death_rate+=dead&&e->boundary_kill;
    e->log.timeout_rate+=timed_out;
    e->log.timeout_threats_rate+=timed_out&&threats_alive;
    e->log.timeout_cache_rate+=timed_out&&!threats_alive&&cache_missing;
    e->log.timeout_gate_ready_rate+=timed_out&&gate_ready;
    e->log.survived_incomplete_rate+=!success&&survived;

    float ticks=fmaxf(1.0f,(float)e->tick);
    e->log.min_cap_fraction+=e->min_cap_fraction;
    e->log.cap_dry_rate+=e->cap_dry_ticks>0;
    e->log.cap_dry_fraction+=e->cap_dry_ticks/ticks;
    e->log.prop_uptime+=e->prop_ticks/ticks;
    e->log.rep_uptime+=e->rep_ticks/ticks;
    e->log.weapon_uptime+=e->weapon_ticks/ticks;
    e->log.weapon_idle_threat_fraction+=e->threat_ticks>0?
        e->weapon_idle_threat_ticks/(float)e->threat_ticks:0;
    e->log.rep_starved_rate+=e->rep_starved_ticks>0;
    e->log.wasted_rep_fraction+=e->total_rep_amount>0?
        e->wasted_rep_amount/e->total_rep_amount:0;
    e->log.prop_cap_spent+=e->prop_cap_spent;
    e->log.rep_cap_spent+=e->rep_cap_spent;
    e->log.neut_cap_drained+=e->neut_cap_drained;
    e->log.n++;e->reset_pending=1;
}

void puf_step(Env* e) {
    if(e->reset_pending)puf_reset(e);
    float* actions=e->agents[0].actions;float* rewards=e->agents[0].rewards;float* terminals=e->agents[0].terminals;
    rewards[0]=0;terminals[0]=0;e->tick++;
    int threats_at_tick_start=ab_gate_targets_alive(e)>0;
    int nav=(int)actions[0], target_action=(int)actions[1], weapon=(int)actions[2];
    int prop=(int)actions[3], rep=(int)actions[4], interact=(int)actions[5];
    if(prop==DESIRED_ON&&!e->prop_desired_on){
        e->prop_desired_on=1;
    }else if(prop==DESIRED_OFF&&e->prop_desired_on){
        e->prop_desired_on=0;
    }
    if(rep==DESIRED_ON&&!e->rep_on)e->rep_on=1;
    else if(rep==DESIRED_OFF&&e->rep_on)e->rep_on=0;

    int gate=ab_find_kind(e,ENTITY_CONDUIT);

    // Weapon actions are persistent desired assignments. The environment performs
    // EVE's required stop-old -> focus-new -> start-new transaction over later ticks.
    if(weapon==WEAPON_OFF)e->weapon_desired_target_index=-1;
    else if(weapon>=WEAPON_FIRE_BASE&&weapon<ABYSS_WEAPON_ACTIONS){
        int requested=ab_entity_for_slot(e,weapon-WEAPON_FIRE_BASE);
        if(requested>=0&&ab_damageable(&e->entities[requested])&&e->entities[requested].locked)
            e->weapon_desired_target_index=requested;
    }
    if(e->weapon_desired_target_index>=0&&
        !ab_damageable(&e->entities[e->weapon_desired_target_index]))
        e->weapon_desired_target_index=-1;
    int weapon_stopped=0;
    if(e->weapon_on&&e->weapon_target_index!=e->weapon_desired_target_index){
        e->weapon_on=0;e->weapon_target_index=-1;
        weapon_stopped=1;
    }

    // EVE accepts one pointer operation at a time. Select it now, but commit it
    // after this second of world simulation: live geometry revalidation and cursor
    // landing take about one second before the click is accepted.
    // Priority matches the live scheduler:
    // interaction > weapon-transaction focus > explicit target > navigation.
    int pointer_kind=POINTER_NONE, pointer_index=-1;
    if(e->interaction_kind==INTERACTION_NONE&&
        interact>=INTERACT_OPEN_BASE&&interact<INTERACT_ACTIVATE_BASE){
        pointer_kind=POINTER_OPEN;
        pointer_index=ab_entity_for_slot(e,interact-INTERACT_OPEN_BASE);
    }else if(e->interaction_kind==INTERACTION_NONE&&
        interact>=INTERACT_ACTIVATE_BASE&&interact<ABYSS_INTERACTION_ACTIONS){
        pointer_kind=POINTER_ACTIVATE;
        pointer_index=ab_entity_for_slot(e,interact-INTERACT_ACTIVATE_BASE);
    }
    int focus_changed=0;
    if(pointer_kind==POINTER_NONE&&e->weapon_desired_target_index>=0&&!e->weapon_on&&
        e->focus_index!=e->weapon_desired_target_index&&
        e->entities[e->weapon_desired_target_index].locked){
        pointer_kind=POINTER_FOCUS;
        pointer_index=e->weapon_desired_target_index;
        focus_changed=1;
    } else if(pointer_kind==POINTER_NONE&&target_action>=TARGET_LOCK_BASE&&target_action<TARGET_FOCUS_BASE) {
        int index=ab_entity_for_slot(e,target_action-TARGET_LOCK_BASE);
        if(index>=0){AbyssEntity*n=&e->entities[index];float distance=ab_len(ab_sub(n->pos,e->ship_pos));if(ab_damageable(n)&&!n->locked&&!n->locking&&distance<=e->lock_range){pointer_kind=POINTER_LOCK;pointer_index=index;}}
    } else if(pointer_kind==POINTER_NONE&&target_action>=TARGET_FOCUS_BASE&&target_action<ABYSS_TARGET_ACTIONS) {
        int index=ab_entity_for_slot(e,target_action-TARGET_FOCUS_BASE);
        if(index>=0&&ab_damageable(&e->entities[index])&&e->entities[index].locked&&e->focus_index!=index){pointer_kind=POINTER_FOCUS;pointer_index=index;focus_changed=1;}
    }
    // A lock command is a transaction: once requested, acquisition continues without
    // requiring the policy to repeat Ctrl+click every tick.
    for(int i=0;i<e->entity_count;i++){AbyssEntity*n=&e->entities[i];if(!n->alive||!n->locking||n->locked)continue;n->lock_progress+=1.0f;if(n->lock_progress>=n->lock_time){n->locking=0;n->locked=1;}}
    for(int i=0;i<e->entity_count;i++)e->entities[i].focused=i==e->focus_index;
    if(!e->weapon_on&&!weapon_stopped&&!focus_changed&&e->weapon_desired_target_index>=0&&
        e->focus_index==e->weapon_desired_target_index&&
        ab_damageable(&e->entities[e->weapon_desired_target_index])&&
        e->entities[e->weapon_desired_target_index].locked){
        e->weapon_on=1;e->weapon_target_index=e->weapon_desired_target_index;
    }
    if(e->interaction_kind==INTERACTION_NONE){
        if(nav==NAV_STOP)e->navigation_target_index=-1;
        else if(pointer_kind==POINTER_NONE&&nav>=NAV_APPROACH_BASE&&nav<ABYSS_NAV_ACTIONS){
            int requested=ab_entity_for_slot(e,nav-NAV_APPROACH_BASE);
            if(requested>=0&&requested!=e->navigation_target_index){pointer_kind=POINTER_APPROACH;pointer_index=requested;}
        }
    }
    if(e->navigation_target_index>=0){AbyssEntity*n=&e->entities[e->navigation_target_index];if(!n->alive&&n->kind!=ENTITY_CACHE)e->navigation_target_index=-1;}

    // Recharge and propulsion activation happen before motion. Propulsion effects
    // persist through a paid cycle after OFF is requested, matching module cycles.
    float recharge_time=e->cap_recharge_time*
        (e->weather_type==WEATHER_ELECTRICAL?.5f:1.0f);
    e->capacitor=ab_capacitor_after_recharge(
        e->capacitor,e->cap_capacity,recharge_time,1.0f);
    if(e->prop_on){
        e->prop_cooldown=fmaxf(0,e->prop_cooldown-1.0f);
        if(e->prop_cooldown<=0)e->prop_on=0;
    }
    if(e->prop_desired_on&&!e->prop_on){
        if(e->capacitor>=e->prop_cap_cost){
            ab_spend_capacitor(e,e->prop_cap_cost,rewards,&e->prop_cap_spent);
            e->prop_on=1;
            e->prop_cooldown=e->prop_cycle;
        }else{
            e->prop_desired_on=0;
        }
    }

    int target=e->navigation_target_index;
    float tachyon_velocity=ab_cloud_multiplier(e,e->ship_pos,CLOUD_TACHYON,4.0f);
    float max_speed=(e->prop_on?e->prop_speed:e->base_speed)*e->weather_velocity_multiplier*tachyon_velocity;
    Vec3 desired={0,0,0};
    if(target>=0)desired=ab_mul(ab_unit(ab_sub(e->entities[target].pos,e->ship_pos)),max_speed);
    float tau=ab_ship_time_constant(e,tachyon_velocity);
    float decay=expf(-1.0f/tau);
    Vec3 old_velocity=e->ship_vel;
    // Exact one-second solution of dv/dt=(desired-v)/tau. With no active
    // navigation command desired=0, producing EVE's exponential coast-down.
    e->ship_vel=ab_add(desired,ab_mul(ab_sub(old_velocity,desired),decay));
    Vec3 displacement=ab_add(desired,
        ab_mul(ab_sub(old_velocity,desired),tau*(1.0f-decay)));
    e->ship_pos=ab_add(e->ship_pos,displacement);
    ab_resolve_obstacles(e,&e->ship_pos,&e->ship_vel,60.0f);
    float player_signature=e->signature*(e->prop_on?e->prop_signature_multiplier:1.0f)*
        ab_cloud_multiplier(e,e->ship_pos,CLOUD_BIOLUMINESCENCE,4.0f);
    for(int i=0;i<e->entity_count;i++){
        AbyssEntity*n=&e->entities[i];
        if(!n->alive||n->kind!=ENTITY_HOSTILE)continue;
        Vec3 radial=ab_sub(e->ship_pos,n->pos);
        float d=ab_len(radial);
        Vec3 u=ab_unit(radial), tangent=ab_unit(ab_cross(u,(Vec3){0,0,1}));
        float local_tachyon=ab_cloud_multiplier(e,n->pos,CLOUD_TACHYON,4.0f);
        float chase_max=n->max_speed*e->weather_velocity_multiplier*local_tachyon;
        const GeneratedNpcDef* movement=&GENERATED_NPCS[n->type_index];
        float radial_speed=ab_clip((d-n->orbit_range)*movement->radial_gain,-chase_max,chase_max);
        n->vel=ab_add(ab_mul(u,radial_speed),
            ab_mul(tangent,fminf(n->orbit_speed*movement->orbit_speed_scale,n->max_speed)*local_tachyon));
        if(ab_len(n->vel)>chase_max)n->vel=ab_mul(ab_unit(n->vel),chase_max);
        n->pos=ab_add(n->pos,n->vel);
        ab_resolve_obstacles(e,&n->pos,&n->vel,fmaxf(20,n->signature*.25f));
        Vec3 relative_velocity=ab_sub(e->ship_vel,n->vel);
        float angular=ab_len(ab_cross(radial,relative_velocity))/fmaxf(d*d,1);
        float tracking=n->tracking;
        for(int t=0;t<e->entity_count;t++){
            AbyssEntity*p=&e->entities[t];
            if(p->kind==ENTITY_TRACKING_PYLON&&
                ab_len(ab_sub(p->pos,n->pos))<=p->effect_range)
                tracking*=1+p->effect_strength;
        }
        float hit=n->tracking>0?ab_turret_hit_chance(angular,tracking,player_signature,d,
            n->optimal*e->weather_range_multiplier,n->falloff*e->weather_range_multiplier):1.0f;
        ab_damage_layers(n->dps*hit,n->damage_mix,&e->shield,&e->armor,&e->hull,e->ship_resist);
        float before_neut=e->capacitor;
        e->capacitor=fmaxf(0,e->capacitor-n->neutralizer);
        e->neut_cap_drained+=before_neut-e->capacitor;
    }

    // Suppressors only affect explicitly eligible drone/missile entities. They
    // cannot damage the player ship or ordinary NPC ships.
    for(int t=0;t<e->entity_count;t++){AbyssEntity*p=&e->entities[t];if(p->kind!=ENTITY_SUPPRESSOR)continue;p->effect_cooldown=fmaxf(0,p->effect_cooldown-1);if(p->effect_cooldown>0)continue;for(int i=0;i<e->entity_count;i++){AbyssEntity*n=&e->entities[i];if(!n->alive||!n->suppressor_vulnerable)continue;if(ab_len(ab_sub(p->pos,n->pos))>p->effect_range)continue;float mix[4]={0,.5f,0,.5f};ab_damage_layers(2*p->effect_strength,mix,&n->shield,&n->armor,&n->hull,n->resist);if(n->hull<=0)n->alive=0;}p->effect_cooldown=p->effect_cycle;}
    // Armor lands at cycle end; shield lands at cycle start. A paid armor cycle
    // completes even if auto-repeat was turned off after activation.
    ab_step_repair_module(e,rewards,1.0f);
    int weapon_active_this_tick=e->weapon_on;
    e->weapon_cooldown=fmaxf(0,e->weapon_cooldown-1);
    if(e->weapon_on&&e->weapon_target_index>=0&&
        e->weapon_target_index<e->entity_count&&e->weapon_cooldown<=0){
        int fired_index=e->weapon_target_index;
        AbyssEntity*n=&e->entities[fired_index];
        float activation_cost=e->weapon_count*e->weapon_cap_cost_each;
        if(!n->alive||!n->locked){
            e->weapon_on=0;e->weapon_target_index=-1;
            e->weapon_desired_target_index=-1;
        }else if(e->capacitor<activation_cost){
            e->weapon_on=0;e->weapon_target_index=-1;
        }else{
            ab_spend_capacitor(e,activation_cost,rewards,NULL);
            Vec3 radial=ab_sub(n->pos,e->ship_pos);
            float d=ab_len(radial);
            Vec3 relative_velocity=ab_sub(n->vel,e->ship_vel);
            float angular=ab_len(ab_cross(radial,relative_velocity))/fmaxf(d*d,1);
            float tracking=e->weapon_tracking;
            for(int t=0;t<e->entity_count;t++){
                AbyssEntity*p=&e->entities[t];
                if(p->kind==ENTITY_TRACKING_PYLON&&
                    ab_len(ab_sub(p->pos,e->ship_pos))<=p->effect_range)
                    tracking*=1+p->effect_strength;
            }
            float target_signature=n->signature*
                ab_cloud_multiplier(e,n->pos,CLOUD_BIOLUMINESCENCE,4.0f);
            float hit=ab_turret_hit_chance(angular,tracking,target_signature,d,
                e->weapon_optimal*e->weather_range_multiplier,
                e->weapon_falloff*e->weather_range_multiplier);
            if(ab_rand(e)<=hit){
                float quality=0.5f+ab_rand(e);
                if(ab_rand(e)<0.01f)quality=3.0f;
                ab_damage_layers(e->weapon_volley*quality,e->weapon_damage_mix,
                    &n->shield,&n->armor,&n->hull,n->resist);
                if(n->hull<=0){
                    n->alive=0;n->locking=n->locked=n->focused=0;
                    if(e->focus_index==fired_index)e->focus_index=-1;
                    if(e->navigation_target_index==fired_index)
                        e->navigation_target_index=-1;
                    rewards[0]+=n->kind==ENTITY_HOSTILE?
                        e->reward_hostile_kill:e->reward_cache_kill;
                    e->weapon_on=0;e->weapon_target_index=-1;
                    e->weapon_desired_target_index=-1;
                }
            }
            e->weapon_cooldown=e->weapon_cycle;
        }
    }
    e->prop_ticks+=e->prop_on;
    e->rep_ticks+=e->rep_cycle_active;
    e->weapon_ticks+=weapon_active_this_tick;
    e->threat_ticks+=threats_at_tick_start;
    e->weapon_idle_threat_ticks+=threats_at_tick_start&&!weapon_active_this_tick;
    e->cap_dry_ticks+=e->capacitor<=0.05f*e->cap_capacity;
    e->min_cap_fraction=fminf(e->min_cap_fraction,e->capacitor/e->cap_capacity);
    // Commit the one selected pointer command after world advancement. This is the
    // same place the live executor has finished moving and freshly revalidated.
    if(pointer_kind!=POINTER_NONE){
        rewards[0]+=e->reward_pointer_action;
        if(pointer_kind==POINTER_OPEN){
            if(pointer_index>=0&&e->entities[pointer_index].kind==ENTITY_CACHE&&
                !e->entities[pointer_index].alive&&!e->cache_looted&&!e->cargo_open){
                e->interaction_kind=INTERACTION_OPEN;
                e->interaction_target_index=pointer_index;
                e->navigation_target_index=pointer_index;
            }else rewards[0]+=e->reward_invalid_loot;
        }else if(pointer_kind==POINTER_ACTIVATE){
            if(pointer_index==gate&&gate>=0&&ab_room_cache_requirement_met(e)&&ab_gate_targets_alive(e)==0){
                e->interaction_kind=INTERACTION_ACTIVATE;
                e->interaction_target_index=pointer_index;
                e->navigation_target_index=pointer_index;
            }
        }else if(pointer_kind==POINTER_FOCUS){
            if(pointer_index>=0&&ab_damageable(&e->entities[pointer_index])&&e->entities[pointer_index].locked)
                e->focus_index=pointer_index;
        }else if(pointer_kind==POINTER_LOCK){
            if(pointer_index>=0){
                AbyssEntity*n=&e->entities[pointer_index];
                float distance=ab_len(ab_sub(n->pos,e->ship_pos));
                if(ab_damageable(n)&&!n->locked&&!n->locking&&distance<=e->lock_range){
                    n->locking=1;
                    if(n->lock_time<=0){
                        float scan=e->scan_resolution*(e->weather_type==WEATHER_EXOTIC?1.5f:1.0f);
                        n->lock_time=ab_lock_time(scan,n->signature);
                    }
                }
            }
        }else if(pointer_kind==POINTER_APPROACH&&pointer_index>=0){
            e->navigation_target_index=pointer_index;
        }
        for(int i=0;i<e->entity_count;i++)e->entities[i].focused=i==e->focus_index;
    }
    // Loot All is Enter in the live client. It is a key action and does not consume
    // the pointer lane or pay pointer latency.
    if(interact==INTERACT_LOOT){if(e->cargo_open&&e->cargo_index>=0&&!e->cache_looted){e->cache_looted=1;e->caches_looted++;e->cargo_open=0;e->cargo_index=-1;rewards[0]+=e->reward_loot;}else rewards[0]+=e->reward_invalid_loot;}
    if(e->interaction_kind==INTERACTION_OPEN&&e->interaction_target_index>=0){
        AbyssEntity* requested=&e->entities[e->interaction_target_index];
        if(requested->kind==ENTITY_CACHE&&!requested->alive&&!e->cache_looted&&
            ab_surface_distance(e,requested)<=2500){
            e->cargo_open=1;e->cargo_index=e->interaction_target_index;
            e->interaction_kind=INTERACTION_NONE;e->interaction_target_index=-1;
            e->navigation_target_index=-1;
        }
    }
    if(e->cargo_open)rewards[0]+=e->reward_cargo_open;
    if(e->interaction_kind==INTERACTION_ACTIVATE&&e->interaction_target_index==gate&&gate>=0&&
        ab_room_cache_requirement_met(e)&&ab_gate_targets_alive(e)==0&&
        ab_conduit_in_activation_range(e,&e->entities[gate])){
        e->rooms_cleared++;rewards[0]+=e->reward_room_clear;
        if(e->room==3){ab_finish(e,1);compute_observations(e);return;}
        e->room++;ab_spawn_room(e);ab_apply_weather_room(e);
    }
    if(ab_len(e->ship_pos)>e->boundary_radius){
        float hull_before_boundary=e->hull;
        float mix[4]={.25f,.25f,.25f,.25f};
        ab_damage_layers(e->boundary_damage,mix,&e->shield,&e->armor,&e->hull,e->ship_resist);
        if(hull_before_boundary>0&&e->hull<=0)e->boundary_kill=1;
    }
    ab_apply_cap_reserve_reward(e,rewards);
    rewards[0]+=e->reward_step;e->episode_return+=rewards[0];if(e->hull<=0||e->tick>=e->max_steps)ab_finish(e,0);compute_observations(e);
}

void puf_render(Env* e) { (void)e; }
void puf_close(Env* e) { (void)e; }
