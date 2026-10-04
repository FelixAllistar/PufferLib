#ifndef SWAT_SIM_H
#define SWAT_SIM_H
#include "controller.h"
#include "weapons.h"
#include "world.h"
#include "acoustics.h"
#include "equipment.h"
#include "mission.h"
#include "tactical.h"
#include "overwatch.h"
#include "generation.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SWAT_CONTRACT_VERSION 1
#define SWAT_MAX_ACTORS 12
#define SWAT_MAX_PLAYERS 4
#define SWAT_PROPRIO_SIZE 32
#define SWAT_SENSOR_ROWS 5
#define SWAT_SENSOR_COLS 9
#define SWAT_SENSOR_CHANNELS 3
#define SWAT_OBS_SIZE (SWAT_PROPRIO_SIZE + SWAT_SENSOR_ROWS*SWAT_SENSOR_COLS*SWAT_SENSOR_CHANNELS)
#define SWAT_ACTION_HEADS 14
#define SWAT_ACTION_SIZES {5,5,3,3,3,2,3,2,2,2,3,2,2,2}
#define SWAT_PHYSICS_SUBSTEPS 4

typedef enum SwatRole { SWAT_OFFICER, SWAT_SUSPECT, SWAT_CIVILIAN, SWAT_SNIPER } SwatRole;
typedef enum SwatEnd { SWAT_RUNNING, SWAT_SUCCESS, SWAT_OFFICER_DOWN,
                       SWAT_CIVILIAN_HARMED, SWAT_TIMEOUT, SWAT_FALL } SwatEnd;
typedef struct SwatActor {
    SwatTag tag;
    SwatController controller;
    SwatArsenal arsenal;
    SwatEquipment gear;
    SwatRole role;
    float health;
    bool present, alive, last_interact;
    int visible_ticks, last_shot_tick;
    int target_actor, hearing_ticks;
    float heard_yaw, foot_distance;
    SwatHearingMemory hearing;
    b3Pos last_foot_position;
    b3Pos last_seen, tracer_start, tracer_end;
} SwatActor;

typedef struct SwatConfig {
    int max_ticks;
    bool randomize, hostile_fire;
    int mission;
    uint32_t layout_seed;
    int generator,difficulty;
} SwatConfig;

typedef struct SwatEvents {
    float hostile_damage, civilian_damage, officer_damage;
    int shots, destroyed, hostile_down;
} SwatEvents;

typedef struct SwatSim {
    SwatWorld world;
    SwatActor actors[SWAT_MAX_ACTORS];
    int actor_count, tick, episode;
    uint32_t rng;
    uint32_t reset_seed;
    SwatConfig config;
    SwatEnd end;
    SwatEvents events, totals;
    SwatSoundLog sounds;
    b3Pos extraction;
    SwatProjectile projectiles[SWAT_MAX_PROJECTILES];
    SwatSniper snipers[SWAT_SNIPERS];
    int commander_actor,last_sniper_order[SWAT_MAX_ACTORS];
    SwatMissionDef mission;
    SwatLayout layout;
} SwatSim;

SwatConfig swat_default_config(void);
void swat_sim_init(SwatSim* sim, SwatConfig config, uint32_t seed);
void swat_sim_reset(SwatSim* sim);
void swat_sim_close(SwatSim* sim);
int swat_player_actor(int slot);
// Stable officer slots: slot zero is actor zero; NPC indices remain unchanged.
bool swat_sim_set_player(SwatSim* sim, int slot, bool present);
void swat_sim_spawn_actor(SwatSim* sim, int index, SwatRole role, b3Pos feet, float yaw);
void swat_sim_bot_inputs(SwatSim* sim, SwatInput inputs[SWAT_MAX_ACTORS]);
void swat_sim_step_inputs(SwatSim* sim, const SwatInput inputs[SWAT_MAX_ACTORS]);
void swat_sim_step(SwatSim* sim, const SwatInput* player);
void swat_sim_observe(const SwatSim* sim, int actor, float out[SWAT_OBS_SIZE]);
SwatInput swat_decode_action(const float actions[SWAT_ACTION_HEADS]);
int swat_sim_hostiles(const SwatSim* sim);
void swat_sim_shoot(SwatSim* sim, int actor, b3Pos origin, b3Vec3 direction, SwatShot shot);
void swat_sim_damage_actor(SwatSim* sim, int victim, int shooter, float damage);
void swat_sim_damage_region(SwatSim* sim,int victim,int shooter,float damage,SwatHitRegion region,bool less_lethal);
SwatHitRegion swat_sim_hit_region(const SwatActor* actor,b3Pos point);
b3Pos swat_sim_inspection_camera(const SwatSim* sim,int actor);
b3Vec3 swat_sim_inspection_direction(const SwatSim* sim,int actor);
int swat_sim_unsecured(const SwatSim* sim);
const char* swat_end_name(SwatEnd end);
const SwatMissionDef* swat_sim_mission(const SwatSim* sim);

#ifdef __cplusplus
}
#endif
#endif
