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
#define CRET_CONTRACT_VERSION 1
#define CRET_PATCH 7                          // egocentric terrain patch (PATCH x PATCH)
#define CRET_PATCH_SPACING 6.0f               // metres between patch samples
#define CRET_NEIGHBORS 6
#define CRET_SELF_OBS 16
#define CRET_OBS_SIZE (CRET_SELF_OBS + CRET_PATCH * CRET_PATCH * 2 + CRET_NEIGHBORS * 5)
#define CRET_ACTION_HEADS 4
#define CRET_ACTION_SIZES {5, 5, 3, 2}        // throttle, turn, strafe, jump

typedef struct CretInput {
    float forward;   // -1 back .. 1 forward (scaled: 0.5 walk, 1 trot)
    float strafe;    // -1 left .. 1 right
    float turn;      // -1 left .. 1 right (yaw rate scale)
    uint8_t sprint, jump;
} CretInput;

typedef struct CretAgent {
    bool alive, on_ground, swimming;
    float x, y, z;         // feet position (m)
    float yaw;             // radians, 0 = +x
    float vx, vy, vz;
    float radius, height;  // collision capsule
    float walk_speed, sprint_speed, turn_rate, jump_speed;
    float stamina;         // 0..1
    float distance;        // metres travelled this life
    int ticks_alive;
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
} CretConfig;

typedef struct CretSim {
    CretConfig cfg;
    CretWorld* world;
    b3WorldId phys;
    CretAgent agents[CRET_MAX_AGENTS];
    int tick;
    float time_of_day;     // 0..1, 0.25 sunrise, 0.5 noon
    uint32_t rng;
} CretSim;

CretConfig cret_default_config(void);
int cret_sim_init(CretSim* s, CretConfig cfg);
void cret_sim_reset(CretSim* s);
void cret_sim_step(CretSim* s, const CretInput* inputs);
void cret_sim_close(CretSim* s);
void cret_sim_respawn(CretSim* s, int i);
void cret_sim_observe(const CretSim* s, int i, float* obs);
CretInput cret_decode_action(const float* actions);
// Simple scripted wanderer used for bots until policies take over.
CretInput cret_bot_input(CretSim* s, int i);
#ifdef __cplusplus
}
#endif
#endif
