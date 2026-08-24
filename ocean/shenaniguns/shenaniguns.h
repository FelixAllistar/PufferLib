// Shenanigans — top-down arena gunfight gym for FPS NPC training.
//
// This env IS the training half of the shenanigans deployment contract:
//   gym (this file, CUDA/C portable)  <->  real game (box3d FPS server)
// Both sides share the constants below (movement model matches
// puffertank/pd64/src/movement.h) and the obs/action layout documented here.
// Bots trained here deploy as regular players: policy inputs <- obs layout,
// policy outputs -> discrete actions -> game input struct.
//
// UNITS CONTRACT (deployment must match):
//   meters, seconds, dt = 1/60 fixed. yaw in radians, forward = (+cos,+sin)
//   in XZ. Top-down: x -> screen x, z -> screen y.
//
// OBSERVATIONS (float32, OBS_SIZE = 62 per agent):
//   [0]  x / width                       absolute position (map memory)
//   [1]  y / height
//   [2]  ego-frame vel x / MAX_SPEED     own velocity rotated into body frame
//   [3]  ego-frame vel z / MAX_SPEED
//   [4]  sin(yaw)                        own heading (unwrapped encoding)
//   [5]  cos(yaw)
//   [6]  hp / 100
//   [7]  ammo / MAG_SIZE
//   [8]  reload progress (0 ready .. 1 just-started)
//   [9]  fire cooldown progress
//   [10..25] 16 view rays over FOV_DEG cone, value = distance fraction
//            (0 = touching, 1 = clear to VIEW_RANGE). Walls and players both
//            shorten rays; identity disambiguation comes from target block.
//   [26..61] up to MAX_TARGETS nearest VISIBLE enemies, sorted by distance,
//            9 floats each, zero-padded:
//       +0 dx ego / VIEW_RANGE     +1 dy ego / VIEW_RANGE
//       +2 rel vel x / MAX_SPEED   +3 rel vel z / MAX_SPEED
//       +4 hp / 100                +5 aim error (rad, signed)
//       +6 distance / VIEW_RANGE   +7 visible flag (1)
//       +8 reserved (0)
//
// ACTIONS (NUM_ATNS = 4, discrete indices):
//   [0] turn    5: {-12, -6, 0, +6, +12} deg/tick
//   [1] forward 3: back / none / forward
//   [2] strafe  3: left / none / right
//   [3] fire    2: no / yes  (hitscan, spread-limited)
//
// REWARDS (all coefficients configurable via puf_init kwargs):
//   damage dealt, damage taken (negative-shape), kill bonus, death penalty,
//   small per-tick aim reward while crosshair is on a visible enemy.

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include "raylib.h"
#include "pufferenv.h"

// ---------------------------------------------------------------------------
// Tunables — DEPLOYMENT CONTRACT, keep in sync with the box3d game side.
// ---------------------------------------------------------------------------
#define DT 60.0f                 // sim rate (Hz)
#define MAX_SPEED 6.0f           // m/s run speed (matches pd64 movement.h)
#define GROUND_ACCEL 60.0f       // Quake-style ground accel
#define AIR_ACCEL 12.0f          // unused in v0 (always grounded) kept for parity
#define FRICTION 8.0f
#define STOP_SPEED 2.0f

#define TURN_VALUES_LUT {-12.0f, -6.0f, 0.0f, 6.0f, 12.0f} // deg/tick
#define PLAYER_RADIUS 0.4f       // m
#define START_HP 100.0f
#define DAMAGE_PER_SHOT 20.0f
#define FIRE_COOLDOWN 6          // ticks between shots
#define MAG_SIZE 12
#define RELOAD_TICKS 48          // 0.8s
#define AGENT_SPREAD_DEG 1.5f    // hitscan cone half-angle

#define RAYS 16
#define FOV_DEG 120.0f
#define VIEW_RANGE 24.0f         // m
#define AIM_REWARD_DEG 5.0f      // crosshair tolerance for aim reward

#define NUM_ATNS 4
#define ACT_SIZES {5, 3, 3, 2}
#define EGO_FEATURES 10
#define TARGET_FEATURES 9
#define MAX_TARGETS 4
#define OBS_SIZE (EGO_FEATURES + RAYS + MAX_TARGETS * TARGET_FEATURES)

#define MAX_AGENTS 2             // duel v0 (selfplay slots), FFA later
#define MAX_BOTS 6
#define MAX_PLAYERS (MAX_AGENTS + MAX_BOTS)

#define NUM_OBSTACLES 6
#define TRACER_CAP 64

static const float TURN_VALUES[5] = TURN_VALUES_LUT;

const Color PUFF_RED = (Color){187, 0, 0, 255};
const Color PUFF_CYAN = (Color){0, 187, 187, 255};
const Color PUFF_WHITE = (Color){241, 241, 241, 255};
const Color PUFF_YELLOW = (Color){245, 197, 66, 255};

typedef struct Log Log;
struct Log {
    float perf;              // kills this episode
    float episode_return;
    float episode_length;
    float score;             // damage dealt
    float kills;
    float deaths;
    float damage_taken;
    float accuracy;          // shots_hit / max(1, shots_fired)
    float slot_0_score;      // selfplay match credit, see robocode.h notes
    float slot_1_score;
    float draw_rate;
    float n;
};

typedef struct Tracer Tracer;
struct Tracer {
    float x0, y0, x1, y1;
    int ttl;
};

typedef struct Player Player;
struct Player {
    float x, y;              // XZ position (y IS the z axis, top-down)
    float vx, vz;
    float yaw;               // radians, forward = (cos, sin)
    float hp;
    bool alive;
    int ammo;
    int reload_ticks;        // >0 while reloading
    int fire_cooldown;       // ticks until next shot allowed
    int kills, deaths;
    int shots_fired, shots_hit;

    // per-tick vision cache (filled by update_vision)
    int target_idx;          // nearest visible enemy, -1 none
    float target_dist;
    float aim_err;           // signed rad to target bearing
};

typedef struct Client Client;
typedef float obs_t;
typedef Env Shenanigans;

struct Env {
    Client* client;
    Agent agents[MAX_AGENTS];

    int num_agents;
    int num_bots;
    int tick;
    int max_ticks;
    float width, height;     // meters

    Player players[MAX_PLAYERS];
    // obstacles as center/half-extent AABBs (meters)
    float obs_cx[NUM_OBSTACLES];
    float obs_cy[NUM_OBSTACLES];
    float obs_hx[NUM_OBSTACLES];
    float obs_hz[NUM_OBSTACLES];

    // bot waypoints
    float bot_wp_x[MAX_BOTS];
    float bot_wp_y[MAX_BOTS];
    int bot_wp_timer[MAX_BOTS];

    Tracer tracers[TRACER_CAP];
    int tracer_head;

    Log log;
    Log* logs;               // per-agent running logs

    // reward shaping coefficients (kwargs)
    float reward_damage_dealt;
    float reward_damage_taken;
    float reward_kill;
    float reward_die;
    float reward_aim;
    float bot_spread_deg;    // bots fire wider than agents
    float bot_reaction_deg;  // bots only open fire inside this aim error

    // selfplay-pool tagging (mirrors robocode.h semantics)
    int tag;
    int boundary_reached;

    unsigned int rng;
};

void init(Shenanigans* env);
void puf_reset(Shenanigans* env);

static inline float sg_get_float(Dict* kwargs, const char* key, float default_value) {
    for (int i = 0; i < kwargs->size; i++) {
        if (strcmp(kwargs->items[i].key, key) == 0) {
            return (float)kwargs->items[i].value;
        }
    }
    return default_value;
}

static inline float rand_unit(Shenanigans* env) {
    return (float)rand_r(&env->rng) / ((float)RAND_MAX + 1.0f);
}

static inline float rand_range(Shenanigans* env, float lo, float hi) {
    return lo + (hi - lo) * rand_unit(env);
}

// deterministic-ish gaussian via central limit, spread in degrees -> radians
static inline float spread_rad(Shenanigans* env, float spread_deg) {
    float g = (rand_unit(env) + rand_unit(env) + rand_unit(env)) / 1.5f - 1.0f; // [-1,1]
    return g * spread_deg * (float)M_PI / 180.0f;
}

static inline float wrap_angle(float a) {
    while (a > (float)M_PI) a -= 2.0f * (float)M_PI;
    while (a < -(float)M_PI) a += 2.0f * (float)M_PI;
    return a;
}

// ---------------------------------------------------------------------------
// Geometry: rays vs AABBs / circles / arena border
// ---------------------------------------------------------------------------

// Slab test. Returns hit fraction t along (dx,dy), or INFINITY.
static inline float ray_vs_aabb(float ox, float oy, float dx, float dy,
                                float left, float right, float bottom, float top) {
    float tmin = 0.0f, tmax = INFINITY;
    if (fabsf(dx) < 1e-8f) {
        if (ox < left || ox > right) return INFINITY;
    } else {
        float t1 = (left - ox) / dx, t2 = (right - ox) / dx;
        if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
        tmin = t1 > tmin ? t1 : tmin;
        tmax = t2 < tmax ? t2 : tmax;
        if (tmin > tmax) return INFINITY;
    }
    if (fabsf(dy) < 1e-8f) {
        if (oy < bottom || oy > top) return INFINITY;
    } else {
        float t1 = (bottom - oy) / dy, t2 = (top - oy) / dy;
        if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
        tmin = t1 > tmin ? t1 : tmin;
        tmax = t2 < tmax ? t2 : tmax;
        if (tmin > tmax) return INFINITY;
    }
    return tmin > 0.0f ? tmin : INFINITY; // start-inside treated as no hit
}

static inline float ray_vs_circle(float ox, float oy, float dx, float dy,
                                  float cx, float cy, float r) {
    float fx = ox - cx, fy = oy - cy;
    float b = fx * dx + fy * dy;
    float c = fx * fx + fy * fy - r * r;
    float disc = b * b - c;
    if (disc < 0.0f) return INFINITY;
    float t = -b - sqrtf(disc);
    return t > 0.0f ? t : INFINITY;
}

// Distance to arena boundary along ray (walls close the map).
static inline float ray_vs_border(float ox, float oy, float dx, float dy,
                                  float w, float h) {
    float t = INFINITY;
    if (dx < -1e-8f) { float tt = -ox / dx;           if (tt < t) t = tt; }
    if (dx >  1e-8f) { float tt = (w - ox) / dx;      if (tt < t) t = tt; }
    if (dy < -1e-8f) { float tt = -oy / dy;           if (tt < t) t = tt; }
    if (dy >  1e-8f) { float tt = (h - oy) / dy;      if (tt < t) t = tt; }
    return t;
}

static inline bool circle_vs_aabb(float cx, float cy, float r,
                                  float bx, float by, float hx, float hy) {
    float nx = fmaxf(bx - hx, fminf(cx, bx + hx));
    float ny = fmaxf(by - hy, fminf(cy, by + hy));
    float dx = cx - nx, dy = cy - ny;
    return dx * dx + dy * dy < r * r;
}

static inline bool position_blocked(Shenanigans* env, float x, float y, float r) {
    if (x < r || y < r || x > env->width - r || y > env->height - r) return true;
    for (int i = 0; i < NUM_OBSTACLES; i++) {
        if (circle_vs_aabb(x, y, r, env->obs_cx[i], env->obs_cy[i],
                           env->obs_hx[i], env->obs_hz[i])) return true;
    }
    return false;
}

// First wall (obstacle or border) hit distance along unit-length dir.
static inline float cast_wall(Shenanigans* env, float ox, float oy,
                              float dx, float dy, float max_t) {
    float best = ray_vs_border(ox, oy, dx, dy, env->width, env->height);
    for (int i = 0; i < NUM_OBSTACLES; i++) {
        float t = ray_vs_aabb(ox, oy, dx, dy,
                              env->obs_cx[i] - env->obs_hx[i],
                              env->obs_cx[i] + env->obs_hx[i],
                              env->obs_cy[i] - env->obs_hz[i],
                              env->obs_cy[i] + env->obs_hz[i]);
        if (t < best) best = t;
    }
    return best < max_t ? best : max_t;
}

// ---------------------------------------------------------------------------
// Rewards / logging
// ---------------------------------------------------------------------------

static inline void add_agent_reward(Shenanigans* env, int agent_idx, float reward) {
    *env->agents[agent_idx].rewards += reward;
    env->logs[agent_idx].episode_return += reward;
}

static inline void record_damage(Shenanigans* env, int shooter, int victim, float dmg) {
    if (shooter >= 0 && shooter < env->num_agents) {
        env->logs[shooter].score += dmg;
        add_agent_reward(env, shooter, dmg * env->reward_damage_dealt);
    }
    if (victim >= 0 && victim < env->num_agents) {
        env->logs[victim].damage_taken += dmg;
        add_agent_reward(env, victim, dmg * env->reward_damage_taken);
    }
}

// ---------------------------------------------------------------------------
// Vision — fills per-player target cache. Called once per tick (and on reset).
// A player is visible to viewer when within FOV cone, range, and LOS unblocked.
// ---------------------------------------------------------------------------
static void update_vision(Shenanigans* env) {
    int total = env->num_agents + env->num_bots;
    for (int i = 0; i < total; i++) {
        Player* p = &env->players[i];
        p->target_idx = -1;
        p->target_dist = INFINITY;
        p->aim_err = 0.0f;
        if (!p->alive) continue;

        float fov_half = FOV_DEG * (float)M_PI / 360.0f;
        for (int j = 0; j < total; j++) {
            if (j == i) continue;
            Player* q = &env->players[j];
            if (!q->alive) continue;
            float dx = q->x - p->x, dy = q->y - p->y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist > VIEW_RANGE) continue;
            float bearing = atan2f(dy, dx);
            if (fabsf(wrap_angle(bearing - p->yaw)) > fov_half) continue;
            float inv = 1.0f / (dist > 1e-6f ? dist : 1e-6f);
            float wall_t = cast_wall(env, p->x, p->y, dx * inv, dy * inv, dist);
            if (wall_t < dist) continue; // LOS blocked
            if (dist < p->target_dist) {
                p->target_idx = j;
                p->target_dist = dist;
                p->aim_err = wrap_angle(bearing - p->yaw);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Movement — Quake accel identical in spirit to pd64/src/movement.h
// ---------------------------------------------------------------------------
static inline void apply_friction(Player* p) {
    float speed = sqrtf(p->vx * p->vx + p->vz * p->vz);
    if (speed < 1e-4f) { p->vx = 0.0f; p->vz = 0.0f; return; }
    float control = speed < STOP_SPEED ? STOP_SPEED : speed;
    float drop = control * FRICTION / DT;
    float ns = speed - drop > 0.0f ? speed - drop : 0.0f;
    float scale = ns / speed;
    p->vx *= scale;
    p->vz *= scale;
}

static inline void accelerate(Player* p, float wishX, float wishZ,
                              float wishSpeed, float accel) {
    float cur = p->vx * wishX + p->vz * wishZ;
    float add = wishSpeed - cur;
    if (add <= 0.0f) return;
    float as = accel * wishSpeed / DT;
    if (as > add) as = add;
    p->vx += wishX * as;
    p->vz += wishZ * as;
}

// Axis-separated slide so players hug walls instead of sticking.
static void try_move(Shenanigans* env, Player* p, float dx, float dz) {
    float nx = p->x + dx;
    if (!position_blocked(env, nx, p->y, PLAYER_RADIUS)) p->x = nx;
    else p->vx = 0.0f;
    float ny = p->y + dz;
    if (!position_blocked(env, p->x, ny, PLAYER_RADIUS)) p->y = ny;
    else p->vz = 0.0f;
}

static void move_player(Shenanigans* env, Player* p, int fwd, int strafe) {
    float fx = cosf(p->yaw), fz = sinf(p->yaw);          // forward
    float rx = -sinf(p->yaw), rz = cosf(p->yaw);         // right = fwd + 90deg
    float wishX = fx * fwd + rx * strafe;
    float wishZ = fz * fwd + rz * strafe;
    float len = sqrtf(wishX * wishX + wishZ * wishZ);
    if (len > 1e-6f) { wishX /= len; wishZ /= len; }

    apply_friction(p);
    if (len > 1e-6f || fwd != 0 || strafe != 0) {
        accelerate(p, wishX, wishZ, len > 1e-6f ? MAX_SPEED : 0.0f, GROUND_ACCEL);
    }
    try_move(env, p, p->vx / DT, p->vz / DT);
}

// ---------------------------------------------------------------------------
// Weapons — instant hitscan with tracer bookkeeping
// ---------------------------------------------------------------------------
static void spawn_tracer(Shenanigans* env, float x0, float y0, float x1, float y1) {
    Tracer* t = &env->tracers[env->tracer_head];
    env->tracer_head = (env->tracer_head + 1) % TRACER_CAP;
    t->x0 = x0; t->y0 = y0; t->x1 = x1; t->y1 = y1; t->ttl = 3;
}

static void fire_hitscan(Shenanigans* env, int shooter_idx, float spread_deg) {
    Player* s = &env->players[shooter_idx];
    if (!s->alive || s->reload_ticks > 0 || s->fire_cooldown > 0 || s->ammo <= 0) {
        return;
    }

    s->fire_cooldown = FIRE_COOLDOWN;
    s->ammo--;
    s->shots_fired++;

    float ang = s->yaw + spread_rad(env, spread_deg);
    float dx = cosf(ang), dy = sinf(ang);
    float max_t = ray_vs_border(s->x, s->y, dx, dy, env->width, env->height);
    for (int i = 0; i < NUM_OBSTACLES; i++) {
        float t = ray_vs_aabb(s->x, s->y, dx, dy,
                              env->obs_cx[i] - env->obs_hx[i],
                              env->obs_cx[i] + env->obs_hx[i],
                              env->obs_cy[i] - env->obs_hz[i],
                              env->obs_cy[i] + env->obs_hz[i]);
        if (t < max_t) max_t = t;
    }

    int total = env->num_agents + env->num_bots;
    int hit_idx = -1;
    float hit_t = max_t;
    for (int j = 0; j < total; j++) {
        if (j == shooter_idx || !env->players[j].alive) continue;
        float t = ray_vs_circle(s->x, s->y, dx, dy,
                                env->players[j].x, env->players[j].y, PLAYER_RADIUS);
        if (t < hit_t) { hit_t = t; hit_idx = j; }
    }

    float ex = s->x + dx * hit_t, ey = s->y + dy * hit_t;
    spawn_tracer(env, s->x, s->y, ex, ey);

    if (hit_idx >= 0) {
        Player* v = &env->players[hit_idx];
        v->hp -= DAMAGE_PER_SHOT;
        s->shots_hit++;
        record_damage(env, shooter_idx < env->num_agents ? shooter_idx : -1,
                      hit_idx < env->num_agents ? hit_idx : -1, DAMAGE_PER_SHOT);
        if (v->hp <= 0.0f) {
            v->alive = false;
            v->hp = 0.0f;
            s->kills++;
            if (shooter_idx < env->num_agents) {
                env->logs[shooter_idx].kills += 1.0f;
                env->logs[shooter_idx].perf += 1.0f;
                add_agent_reward(env, shooter_idx, env->reward_kill);
            }
            v->deaths++;
            if (hit_idx < env->num_agents) {
                env->logs[hit_idx].deaths += 1.0f;
                add_agent_reward(env, hit_idx, env->reward_die);
            }
        }
    }

    if (s->ammo <= 0) s->reload_ticks = RELOAD_TICKS;
}

// ---------------------------------------------------------------------------
// Scripted bots — wander + engage nearest visible agent
// ---------------------------------------------------------------------------
static void bot_step(Shenanigans* env, int idx) {
    Player* b = &env->players[idx];
    if (!b->alive) return;

    int bi = idx - env->num_agents;
    int target = b->target_idx;

    if (target >= 0) {
        Player* t = &env->players[target];
        float dx = t->x - b->x, dy = t->y - b->y;
        float bearing = atan2f(dy, dx);
        float err = wrap_angle(bearing - b->yaw);
        int turn = err > 0.05f ? 4 : (err < -0.05f ? 0 : 2);
        b->yaw += TURN_VALUES[turn] * (float)M_PI / 180.0f;
        b->yaw = wrap_angle(b->yaw);

        // strafe-orbit while engaged, close distance when far
        int fwd = b->target_dist > VIEW_RANGE * 0.5f ? 1 : ((env->tick / 24) % 2);
        int strafe = (env->tick / 12 + bi) % 2 == 0 ? 1 : -1;
        if (b->target_dist < 3.0f) fwd = -1; // back off when crowded
        move_player(env, b, fwd, strafe);

        if (fabsf(err) < env->bot_reaction_deg * (float)M_PI / 180.0f) {
            fire_hitscan(env, idx, env->bot_spread_deg);
        }
    } else {
        float dx = env->bot_wp_x[bi] - b->x, dy = env->bot_wp_y[bi] - b->y;
        if (env->bot_wp_timer[bi] <= 0 || (dx * dx + dy * dy) < 4.0f) {
            for (int tries = 0; tries < 32; tries++) {
                float wx = rand_range(env, PLAYER_RADIUS + 1.0f, env->width - PLAYER_RADIUS - 1.0f);
                float wy = rand_range(env, PLAYER_RADIUS + 1.0f, env->height - PLAYER_RADIUS - 1.0f);
                if (!position_blocked(env, wx, wy, PLAYER_RADIUS + 0.5f)) {
                    env->bot_wp_x[bi] = wx;
                    env->bot_wp_y[bi] = wy;
                    break;
                }
            }
            env->bot_wp_timer[bi] = 240;
        }
        env->bot_wp_timer[bi]--;
        float bearing = atan2f(dy, dx);
        float err = wrap_angle(bearing - b->yaw);
        int turn = err > 0.1f ? 4 : (err < -0.1f ? 0 : 2);
        b->yaw += TURN_VALUES[turn] * (float)M_PI / 180.0f;
        b->yaw = wrap_angle(b->yaw);
        move_player(env, b, 1, 0);
    }
}

// ---------------------------------------------------------------------------
// Observations
// ---------------------------------------------------------------------------
static void compute_observations(Shenanigans* env) {
    int total = env->num_agents + env->num_bots;
    float fov_half = FOV_DEG * (float)M_PI / 360.0f;

    for (int i = 0; i < env->num_agents; i++) {
        Player* p = &env->players[i];
        obs_t* obs = (obs_t*)env->agents[i].observations;
        memset(obs, 0, OBS_SIZE * sizeof(obs_t));

        obs[0] = p->x / env->width;
        obs[1] = p->y / env->height;
        float c = cosf(p->yaw), s = sinf(p->yaw);
        obs[2] = (c * p->vx + s * p->vz) / MAX_SPEED;    // ego forward speed
        obs[3] = (-s * p->vx + c * p->vz) / MAX_SPEED;   // ego right speed
        obs[4] = s;
        obs[5] = c;
        obs[6] = p->hp / START_HP;
        obs[7] = (float)p->ammo / (float)MAG_SIZE;
        obs[8] = p->reload_ticks > 0
            ? 1.0f - (float)p->reload_ticks / (float)RELOAD_TICKS : 0.0f;
        obs[9] = p->fire_cooldown > 0
            ? (float)p->fire_cooldown / (float)FIRE_COOLDOWN : 0.0f;

        float base = p->yaw - fov_half;
        float* rays_out = obs + EGO_FEATURES;
        for (int r = 0; r < RAYS; r++) {
            float ang = base + (FOV_DEG * (float)M_PI / 180.0f) * (float)r / (float)(RAYS - 1);
            float dx = cosf(ang), dy = sinf(ang);
            float wall_t = cast_wall(env, p->x, p->y, dx, dy, VIEW_RANGE);
            float best = wall_t;
            for (int j = 0; j < total; j++) {
                if (j == i || !env->players[j].alive) continue;
                float t = ray_vs_circle(p->x, p->y, dx, dy,
                                        env->players[j].x, env->players[j].y,
                                        PLAYER_RADIUS);
                if (t < best) best = t;
            }
            rays_out[r] = best / VIEW_RANGE;
        }

        // nearest visible enemies sorted by distance (vision cache + scan)
        int picked[MAX_TARGETS];
        float picked_d[MAX_TARGETS];
        int n_picked = 0;
        for (int j = 0; j < total && n_picked < MAX_TARGETS; j++) {
            if (j == i || !env->players[j].alive) continue;
            float dxw = env->players[j].x - p->x, dyw = env->players[j].y - p->y;
            float d2 = dxw * dxw + dyw * dyw;
            if (d2 > VIEW_RANGE * VIEW_RANGE) continue;
            float bearing = atan2f(dyw, dxw);
            if (fabsf(wrap_angle(bearing - p->yaw)) > fov_half) continue;
            float inv = 1.0f / (sqrtf(d2) > 1e-6f ? sqrtf(d2) : 1e-6f);
            if (cast_wall(env, p->x, p->y, dxw * inv, dyw * inv, sqrtf(d2)) < sqrtf(d2))
                continue;
            // insertion sort by distance
            int pos = n_picked;
            while (pos > 0 && picked_d[pos - 1] > d2) {
                picked[pos] = picked[pos - 1];
                picked_d[pos] = picked_d[pos - 1];
                pos--;
            }
            picked[pos] = j;
            picked_d[pos] = d2;
            n_picked++;
        }

        float* tgt_out = obs + EGO_FEATURES + RAYS;
        for (int k = 0; k < MAX_TARGETS; k++) {
            if (k >= n_picked) break;
            Player* q = &env->players[picked[k]];
            float dxw = q->x - p->x, dyw = q->y - p->y;
            float dist = sqrtf(picked_d[k]);
            float cI = cosf(-p->yaw), sI = sinf(-p->yaw);
            float ex = cI * dxw - sI * dyw;   // world -> ego rotation
            float ey = sI * dxw + cI * dyw;
            float* o = tgt_out + k * TARGET_FEATURES;
            o[0] = ex / VIEW_RANGE;
            o[1] = ey / VIEW_RANGE;
            o[2] = (cI * q->vx - sI * q->vz) / MAX_SPEED;
            o[3] = (sI * q->vx + cI * q->vz) / MAX_SPEED;
            o[4] = q->hp / START_HP;
            o[5] = wrap_angle(atan2f(dyw, dxw) - p->yaw);
            o[6] = dist / VIEW_RANGE;
            o[7] = 1.0f;
        }
    }
}

// ---------------------------------------------------------------------------
// Episode lifecycle
// ---------------------------------------------------------------------------
void add_log(Shenanigans* env) {
    for (int i = 0; i < env->num_agents; i++) {
        env->log.perf            += env->logs[i].perf;
        env->log.episode_return  += env->logs[i].episode_return;
        env->log.episode_length  += env->logs[i].episode_length;
        env->log.score           += env->logs[i].score;
        env->log.kills           += env->logs[i].kills;
        env->log.deaths          += env->logs[i].deaths;
        env->log.damage_taken    += env->logs[i].damage_taken;
        env->log.accuracy        +=
            (float)env->players[i].shots_hit / (float)(env->players[i].shots_fired + 1);
        env->log.n               += 1.0f;
    }
}

// Mirrors robocode agent_terminal_outcome: 2 = continue, +1/-1 slot result, 0 draw
static inline int agent_terminal_outcome(Shenanigans* env) {
    if (env->num_agents <= 0) return 2;
    bool slot0_dead = !env->players[0].alive;
    if (env->num_agents == 1) return slot0_dead ? -1 : 2;

    bool any_other_alive = false;
    bool any_other_dead = false;
    for (int a = 1; a < env->num_agents; a++) {
        if (!env->players[a].alive) any_other_dead = true;
        else any_other_alive = true;
    }
    if (slot0_dead && !any_other_alive) return 0;
    if (slot0_dead) return -1;
    if (any_other_dead) return 1;
    return 2;
}

static inline void end_episode(Shenanigans* env, int outcome) {
    float s0 = outcome > 0 ? 1.0f : (outcome < 0 ? 0.0f : 0.5f);
    env->log.slot_0_score += s0 * env->num_agents;
    env->log.slot_1_score += (1.0f - s0) * env->num_agents;
    if (outcome == 0) env->log.draw_rate += env->num_agents;
    if (env->tag > 0) env->boundary_reached = 1;
    for (int a = 0; a < env->num_agents; a++) {
        *env->agents[a].terminals = 1.0f;
    }
    add_log(env);
    puf_reset(env);
}

void puf_reset(Shenanigans* env) {
    env->tick = 0;
    env->tracer_head = 0;
    memset(env->tracers, 0, sizeof(env->tracers));

    int total = env->num_agents + env->num_bots;
    int placed = 0;
    while (placed < total) {
        float x = rand_range(env, PLAYER_RADIUS + 0.5f, env->width - PLAYER_RADIUS - 0.5f);
        float y = rand_range(env, PLAYER_RADIUS + 0.5f, env->height - PLAYER_RADIUS - 0.5f);
        bool collide = false;
        for (int j = 0; j < placed; j++) {
            float dx = x - env->players[j].x, dy = y - env->players[j].y;
            if (dx * dx + dy * dy < 9.0f) { collide = true; break; }
        }
        if (!collide && position_blocked(env, x, y, PLAYER_RADIUS + 0.25f)) collide = true;
        if (collide) continue;

        Player* p = &env->players[placed];
        p->x = x; p->y = y;
        p->vx = 0.0f; p->vz = 0.0f;
        p->yaw = rand_range(env, 0.0f, 2.0f * (float)M_PI);
        p->hp = START_HP;
        p->alive = true;
        p->ammo = MAG_SIZE;
        p->reload_ticks = 0;
        p->fire_cooldown = 0;
        p->target_idx = -1;
        p->target_dist = INFINITY;
        p->aim_err = 0.0f;
        if (placed >= env->num_agents) {
            int bi = placed - env->num_agents;
            env->bot_wp_timer[bi] = 0;
            env->bot_wp_x[bi] = x;
            env->bot_wp_y[bi] = y;
        }
        placed++;
    }
    for (int i = 0; i < env->num_agents; i++) {
        env->logs[i] = (Log){0};
        *env->agents[i].terminals = 0.0f;
        *env->agents[i].rewards = 0.0f;
    }
    update_vision(env);
    compute_observations(env);
}

// ---------------------------------------------------------------------------
// Core interface
// ---------------------------------------------------------------------------
void init(Shenanigans* env) {
    int total = env->num_agents + env->num_bots;
    assert(total <= MAX_PLAYERS);
    assert(env->num_agents <= MAX_AGENTS);
    env->logs = (Log*)calloc(env->num_agents, sizeof(Log));
    // symmetric cover layout, scales with arena size (designed around 32m)
    float sc = env->width / 32.0f;
    float scy = env->height / 32.0f;
    float layouts[NUM_OBSTACLES][4] = {
        // cx, cy, hx, hz (fractions of 32m design space)
        { 9.0f,  9.0f, 1.5f, 1.5f},
        {-9.0f,  9.0f, 1.5f, 1.5f},
        { 9.0f, -9.0f, 1.5f, 1.5f},
        {-9.0f, -9.0f, 1.5f, 1.5f},
        { 0.0f,  0.0f, 3.0f, 1.0f},
        { 0.0f,  0.0f, 1.0f, 3.0f},
    };
    for (int i = 0; i < NUM_OBSTACLES; i++) {
        env->obs_cx[i] = layouts[i][0] * sc + env->width * 0.5f;
        env->obs_cy[i] = layouts[i][1] * scy + env->height * 0.5f;
        env->obs_hx[i] = layouts[i][2] * sc;
        env->obs_hz[i] = layouts[i][3] * scy;
    }
}

void allocate_env(Shenanigans* env) {
    init(env);
}

void puf_init(Env* env, Dict* kwargs) {
    env->num_agents = (int)sg_get_float(kwargs, "num_agents", 2.0f);
    env->num_bots = (int)sg_get_float(kwargs, "num_bots", 0.0f);
    env->max_ticks = (int)sg_get_float(kwargs, "max_ticks", 1200.0f);
    env->width = sg_get_float(kwargs, "width", 32.0f);
    env->height = sg_get_float(kwargs, "height", 32.0f);
    env->reward_damage_dealt = sg_get_float(kwargs, "reward_damage_dealt", 0.01f);
    env->reward_damage_taken = sg_get_float(kwargs, "reward_damage_taken", -0.01f);
    env->reward_kill = sg_get_float(kwargs, "reward_kill", 1.0f);
    env->reward_die = sg_get_float(kwargs, "reward_die", -1.0f);
    env->reward_aim = sg_get_float(kwargs, "reward_aim", 0.001f);
    env->bot_spread_deg = sg_get_float(kwargs, "bot_spread_deg", 4.0f);
    env->bot_reaction_deg = sg_get_float(kwargs, "bot_reaction_deg", 6.0f);
    env->tag = (int)sg_get_float(kwargs, "tag", 0.0f);
    for (int i = 0; i < env->num_agents; i++) env->agents[i].policy = (unsigned char)i;
    init(env);
}

void puf_log(Log* log, Dict* out) {
    dict_set(out, "perf", log->perf);
    dict_set(out, "score", log->score);
    dict_set(out, "kills", log->kills);
    dict_set(out, "deaths", log->deaths);
    dict_set(out, "damage_taken", log->damage_taken);
    dict_set(out, "accuracy", log->accuracy);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "slot_0_score", log->slot_0_score);
    dict_set(out, "slot_1_score", log->slot_1_score);
    dict_set(out, "draw_rate", log->draw_rate);
    dict_set(out, "n", log->n);
}

void puf_close(Shenanigans* env) {
    free(env->logs);
    env->logs = NULL;
}

void puf_step(Shenanigans* env) {
    env->tick++;
    if (env->tick > env->max_ticks) {
        end_episode(env, 0); // timeout draw
        return;
    }
    for (int a = 0; a < env->num_agents; a++) {
        *env->agents[a].rewards = 0.0f;
        *env->agents[a].terminals = 0.0f;
    }

    update_vision(env);

    // aim shaping: small reward while crosshair rests on a visible enemy
    for (int a = 0; a < env->num_agents; a++) {
        Player* p = &env->players[a];
        if (p->alive && p->target_idx >= 0 &&
            fabsf(p->aim_err) < AIM_REWARD_DEG * (float)M_PI / 180.0f) {
            add_agent_reward(env, a, env->reward_aim);
        }
    }

    // agent action phase
    for (int a = 0; a < env->num_agents; a++) {
        Player* p = &env->players[a];
        if (!p->alive) continue;
        float* atn = env->agents[a].actions;
        int turn = (int)atn[0]; if (turn < 0) turn = 0; if (turn > 4) turn = 4;
        int fwd = (int)atn[1]; if (fwd < 0) fwd = 0; if (fwd > 2) fwd = 2;
        int strafe = (int)atn[2]; if (strafe < 0) strafe = 0; if (strafe > 2) strafe = 2;
        int fire = (int)atn[3]; if (fire < 0) fire = 0; if (fire > 1) fire = 1;

        p->yaw += TURN_VALUES[turn] * (float)M_PI / 180.0f;
        p->yaw = wrap_angle(p->yaw);
        move_player(env, p, fwd - 1, strafe - 1);

        if (p->reload_ticks > 0) {
            p->reload_ticks--;
            if (p->reload_ticks == 0) p->ammo = MAG_SIZE;
        }
        if (p->fire_cooldown > 0) p->fire_cooldown--;
        if (fire) fire_hitscan(env, a, AGENT_SPREAD_DEG);
    }

    // bot phase
    for (int b = env->num_agents; b < env->num_agents + env->num_bots; b++) {
        Player* p = &env->players[b];
        if (!p->alive) continue;
        if (p->reload_ticks > 0) {
            p->reload_ticks--;
            if (p->reload_ticks == 0) p->ammo = MAG_SIZE;
        }
        if (p->fire_cooldown > 0) p->fire_cooldown--;
        bot_step(env, b);
    }

    // tracer decay
    for (int i = 0; i < TRACER_CAP; i++) {
        if (env->tracers[i].ttl > 0) env->tracers[i].ttl--;
    }

    for (int i = 0; i < env->num_agents; i++) {
        env->logs[i].episode_length += 1.0f;
    }

    int outcome = agent_terminal_outcome(env);
    if (outcome == 2 && env->num_bots > 0) {
        // slot0 alive + all bots dead => win
        bool any_bot_alive = false;
        for (int b = env->num_agents; b < env->num_agents + env->num_bots; b++) {
            if (env->players[b].alive) { any_bot_alive = true; break; }
        }
        if (!any_bot_alive) outcome = 1;
    }
    if (outcome != 2) {
        end_episode(env, outcome);
        return;
    }
    compute_observations(env);
}

// ---------------------------------------------------------------------------
// Rendering (raylib shapes only — no texture assets required)
// ---------------------------------------------------------------------------
struct Client {
    float pad;
};

static Client* make_client(Shenanigans* env) {
    InitWindow(900, 900, "PufferLib Shenanigans");
    SetTargetFPS(60);
    Client* client = (Client*)calloc(1, sizeof(Client));
    (void)env;
    return client;
}

void puf_render(Shenanigans* env) {
    if (env->client == NULL) {
        env->client = make_client(env);
    }
    float scale = fminf(GetScreenWidth() / env->width, GetScreenHeight() / env->height);
    float ox = (GetScreenWidth() - env->width * scale) * 0.5f;
    float oy = (GetScreenHeight() - env->height * scale) * 0.5f;

    BeginDrawing();
    ClearBackground((Color){6, 24, 24, 255});

    DrawRectangleLinesEx((Rectangle){ox, oy, env->width * scale, env->height * scale},
                         2, PUFF_CYAN);
    for (int i = 0; i < NUM_OBSTACLES; i++) {
        Rectangle r = {
            ox + (env->obs_cx[i] - env->obs_hx[i]) * scale,
            oy + (env->obs_cy[i] - env->obs_hz[i]) * scale,
            env->obs_hx[i] * 2.0f * scale,
            env->obs_hz[i] * 2.0f * scale,
        };
        DrawRectangleRec(r, (Color){0, 110, 110, 255});
        DrawRectangleLinesEx(r, 1, PUFF_CYAN);
    }

    // tracers
    for (int i = 0; i < TRACER_CAP; i++) {
        Tracer* t = &env->tracers[i];
        if (t->ttl <= 0) continue;
        DrawLineEx(
            (Vector2){ox + t->x0 * scale, oy + t->y0 * scale},
            (Vector2){ox + t->x1 * scale, oy + t->y1 * scale},
            2, (Color){245, 197, 66, (unsigned char)(85 * t->ttl)});
    }

    int total = env->num_agents + env->num_bots;
    for (int i = 0; i < total; i++) {
        Player* p = &env->players[i];
        if (!p->alive) continue;
        bool is_agent = i < env->num_agents;
        Color body = is_agent ? PUFF_CYAN : PUFF_RED;
        Vector2 pos = {ox + p->x * scale, oy + p->y * scale};
        float px = cosf(p->yaw), py = sinf(p->yaw);
        float rx = -py, ry = px;
        Vector2 tip = {pos.x + px * 14, pos.y + py * 14};
        Vector2 l = {pos.x - px * 10 + rx * 9, pos.y - py * 10 + ry * 9};
        Vector2 rr = {pos.x - px * 10 - rx * 9, pos.y - py * 10 - ry * 9};
        DrawTriangle(tip, l, rr, body);

        // hp pip
        DrawRectangle(pos.x - 14, pos.y - 22, 28 * (p->hp / START_HP), 3,
                      p->hp > 30 ? GREEN : RED);
        if (is_agent) {
            DrawText(TextFormat("%d", i + 1), pos.x - 4, pos.y + 10, 10, WHITE);
        }
    }

    DrawText(TextFormat("tick %d/%d", env->tick, env->max_ticks), 10, 10, 16, WHITE);
    EndDrawing();
}
