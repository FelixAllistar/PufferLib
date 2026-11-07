// Authoritative fixed-step simulation. Humans, network peers, scripted bots
// and policies all drive agents through CretInput; rendering never changes it.
#ifndef CRET_SIM_H
#define CRET_SIM_H
#include <stdbool.h>
#include <stdint.h>
#include "world.h"
#ifdef __cplusplus
extern "C" {
#endif

#define CRET_TICK_HZ 30
#define CRET_DT (1.0f / CRET_TICK_HZ)
#define CRET_MAX_AGENTS 64

// Policy contract (bump CRET_CONTRACT_VERSION when obs/actions change).
// 2: species, health, hunger, thirst, growth, bleed, forage and carcass in
//    the self obs; diet, size and health per neighbour; an action head
//    (none, attack, eat or drink).
#define CRET_CONTRACT_VERSION 2
#define CRET_PATCH 7                          // egocentric terrain patch (PATCH x PATCH)
#define CRET_PATCH_SPACING 6.0f               // metres between patch samples
#define CRET_NEIGHBORS 6
#define CRET_NEIGHBOR_OBS 8
#define CRET_SELF_OBS 31
#define CRET_OBS_SIZE (CRET_SELF_OBS + CRET_PATCH * CRET_PATCH * 2 + CRET_NEIGHBORS * CRET_NEIGHBOR_OBS)
#define CRET_ACTION_HEADS 5
#define CRET_ACTION_SIZES {5, 5, 3, 2, 3}     // throttle, turn, strafe, jump, action (none, attack, eat/drink)
#define CRET_MAX_CARCASSES 32

// Species: adult body size, gaits, stamina, health and attack. Juveniles
// scale these with growth (see cret_growth_scale in sim.c). Assets
// (render/dino.c) are looked up by `asset`; a missing model draws as a
// capsule, so the sim never depends on the art.
enum { CRET_SP_RAPTOR, CRET_SP_STEGO, CRET_SP_TREX, CRET_SPECIES };
enum { CRET_DIET_HERBIVORE, CRET_DIET_CARNIVORE };
typedef struct CretSpecies {
    const char* name; const char* asset; int diet;
    float length, height, radius, mass;          // m, m, capsule radius m, kg
    float walk, trot, sprint, swim;              // m/s
    float turn_rate, jump;                       // rad/s at rest, take-off m/s (0: cannot jump)
    float stamina_drain, stamina_regen;          // per second while sprinting / otherwise
    float health;                                // adult hit points
    float damage, reach, cooldown;               // attack: hp, m, s between attacks
    float hit_time, attack_time;                 // s into the attack when it lands (matches the clip), length of the attack
    int tail;                                    // 1: the attack hits behind (tail swing), 0: a bite in front
} CretSpecies;
extern const CretSpecies CRET_SPECIES_TABLE[CRET_SPECIES];

// Actions. Eat grazes (herbivores) or eats a carcass in reach (carnivores),
// and turns into drink when facing water; attack bites or swings the tail.
enum { CRET_ACT_NONE, CRET_ACT_EAT, CRET_ACT_DRINK, CRET_ACT_CALL, CRET_ACT_ATTACK, CRET_ACT_REST };

typedef struct CretInput {
    float forward;   // -1 back .. 1 forward (0.5 walk, 1 trot; sprint flag for the top gait)
    float strafe;    // -1 left .. 1 right
    float turn;      // -1 left .. 1 right (yaw rate scale)
    uint8_t sprint, jump;
    uint8_t action;  // CRET_ACT_*: started when standing (eat, drink, call, attack)
} CretInput;

typedef struct CretAgent {
    bool alive, on_ground, swimming, sprinting;
    uint8_t species;       // CRET_SP_*
    uint8_t act;           // CRET_ACT_* in progress (0: none)
    float x, y, z;         // feet position (m)
    float yaw;             // radians, 0 = +x
    float vx, vy, vz;
    float speed;           // horizontal speed (m/s)
    float radius, height;  // collision capsule
    float walk_speed, trot_speed, sprint_speed, swim_speed, turn_rate, jump_speed;
    float stamina;         // 0..1
    float distance;        // metres travelled this life
    int ticks_alive, act_ticks;
    // Survival.
    float health, max_health;   // hp
    float food, water;          // 0 starving / parched .. 1 full
    float growth;               // 0.35 hatchling .. 1 adult; scales size, speed, health and damage
    float shape_growth;         // growth the collision capsule was built for
    float bleed;                // hp per second, decays
    int attack_cd;              // ticks until the next attack
    int hurt_ticks;             // > 0 just after a hit (flinch)
    int dead_ticks;             // ticks since death (alive == false)
    int last_attacker;          // agent that hit last (-1)
    // Events this tick (cleared at the start of cret_sim_step), for rewards.
    float ev_food, ev_water, ev_dealt, ev_taken;   // fullness gained; damage as a fraction of the victim's / own max health
    int ev_died, ev_kills;
    b3ShapeId shape;
    // Scripted bot state (cret_bot_input).
    int bot_state, bot_timer, bot_target; float goal_x, goal_z;
    b3BodyId body;
} CretAgent;

typedef struct CretConfig {
    uint32_t world_seed;
    int world_n;
    float world_cell;
    int num_agents;
    int max_ticks;
    float day_seconds;     // real seconds per in-game day
    uint32_t seed;
    int player_species;    // slot 0 (CRET_SP_*); other slots follow a fixed mix
    int respawn_ticks;     // after death (the RL binding uses 1)
    float growth_seconds;  // fed hatchling to adult
    float spawn_growth;    // growth at spawn; < 0: random adult-ish (0.55..1)
    float player_growth;   // slot 0 growth at spawn (< 0: as spawn_growth)
    float hunger_seconds, thirst_seconds;   // full to empty at rest
    float carcass_seconds; // a carcass lasts at most this long
} CretConfig;

// Remains of a dead animal: carnivores eat the meat; it rots away.
typedef struct CretCarcass {
    bool active; uint8_t species;
    float x, y, z, yaw, growth;
    float meat, max_meat;  // kg
    int age;               // ticks since death
    int agent;             // who died (for the death animation), -1 unknown
} CretCarcass;

typedef struct CretSim {
    CretConfig cfg;
    CretWorld* world;
    b3WorldId phys;
    CretAgent agents[CRET_MAX_AGENTS];
    int tick;
    float time_of_day;     // 0..1, 0.25 sunrise, 0.5 noon
    uint32_t rng;
    CretPropStream props;  // prop colliders near agents (world.c)
    CretCarcass carcasses[CRET_MAX_CARCASSES];
} CretSim;

CretConfig cret_default_config(void);
// Species of an agent slot (slot 0 is the player's choice).
int cret_slot_species(const CretConfig* cfg, int slot);
int cret_sim_init(CretSim* s, CretConfig cfg);
void cret_sim_reset(CretSim* s);
void cret_sim_step(CretSim* s, const CretInput* inputs);
void cret_sim_close(CretSim* s);
void cret_sim_respawn(CretSim* s, int i);
void cret_sim_observe(const CretSim* s, int i, float* obs);
// Switches an agent's species (the player's choice) and respawns it.
void cret_sim_set_species(CretSim* s, int i, int species);
// Size factor of a growth stage (1 at adult).
float cret_growth_scale(float growth);
// Sets size, speeds, turning, jump and max health from species and growth
// (keeps the health fraction).
void cret_apply_growth(CretAgent* a);
// Edible ground plants at a point, 0..1 (herbivore grazing).
float cret_forage(const CretWorld* w, float x, float z);
CretInput cret_decode_action(const float* actions);
// Simple scripted wanderer used for bots until policies take over.
CretInput cret_bot_input(CretSim* s, int i);
#ifdef __cplusplus
}
#endif
#endif
