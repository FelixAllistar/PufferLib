// Shared helpers for the Cretaceous test suite (tests/*.c, tools/cret_eval.c).
// Header-only: checks, timers, the tiny test world and sim state hashes.
//
// Every binary runs from the repository root, because world.c caches bakes
// under build/cretaceous/worlds/ and props load from ocean/cretaceous/assets/.
// The test world is small so a fresh checkout bakes it in a few seconds:
//   CRET_TEST_N (default 512), CRET_TEST_CELL (4), CRET_TEST_SEED (1).
#ifndef CRET_TEST_UTIL_H
#define CRET_TEST_UTIL_H
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../sim.h"

static int t_fail, t_checks;
static FILE* t_errf;   // where failures go (stderr unless a test silences stderr)
#define T_ERR (t_errf ? t_errf : stderr)
#define CHECK(cond, ...) do { t_checks++; if (!(cond)) { t_fail++; \
    fprintf(T_ERR, "  FAIL %s:%d: ", __FILE__, __LINE__); fprintf(T_ERR, __VA_ARGS__); fputc('\n', T_ERR); } } while (0)
// Stops reporting the same failing check after `cap` hits (per call site).
#define CHECK_CAPPED(cond, cap, ...) do { static int _hits; t_checks++; if (!(cond)) { t_fail++; \
    if (_hits++ < (cap)) { fprintf(T_ERR, "  FAIL %s:%d: ", __FILE__, __LINE__); fprintf(T_ERR, __VA_ARGS__); fputc('\n', T_ERR); } } } while (0)

static double t_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static int t_env_int(const char* k, int d) { const char* v = getenv(k); return v && *v ? atoi(v) : d; }
static float t_env_float(const char* k, float d) { const char* v = getenv(k); return v && *v ? (float)atof(v) : d; }

static int t_finish(const char* name, double t0) {
    fprintf(T_ERR, "%s: %s (%d checks, %d failed, %.1f s)\n", name, t_fail ? "FAILED" : "ok", t_checks, t_fail, t_now() - t0);
    return t_fail ? 1 : 0;
}

// Deterministic test RNG (independent of the sim's streams).
static inline uint32_t t_rand(uint32_t* s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x ? x : 0x9e3779b9u; }
static inline float t_frand(uint32_t* s) { return (t_rand(s) >> 8) * (1.0f / 16777216.0f); }

// The tiny world plus training-like settings (no weather, quick respawn).
static CretConfig t_config(int num_agents) {
    CretConfig c = cret_default_config();
    c.world_n = t_env_int("CRET_TEST_N", 512); c.world_cell = t_env_float("CRET_TEST_CELL", 4.0f);
    c.world_seed = (uint32_t)t_env_int("CRET_TEST_SEED", 1);
    c.num_agents = num_agents; c.player_growth = -1; c.respawn_ticks = 1; c.weather = 0;
    return c;
}

// Keeps the test world loaded for the whole process: worlds are freed when
// their last sim closes, and reloading rebuilds every prop collider.
static void t_hold_world(void) {
    CretConfig c = t_config(1);
    if (!cret_world_acquire(c.world_seed, c.world_n, c.world_cell, 1)) { fprintf(stderr, "test world failed to load\n"); exit(2); }
}

// Random policy over the binding's action heads (CRET_ACTION_SIZES).
static void t_random_actions(uint32_t* rng, float* act) {
    static const int sizes[CRET_ACTION_HEADS] = CRET_ACTION_SIZES;
    for (int h = 0; h < CRET_ACTION_HEADS; h++) act[h] = (float)(t_rand(rng) % sizes[h]);
}

// ------------------------------------------------------------------ state hash
// FNV-1a over the sim state. Agents hash their raw bytes except the Box3D ids
// (those carry the physics world index, which differs between two live sims),
// so a new CretAgent field is covered without touching this file. Padding is
// zero because cret_sim_respawn memsets the agent; a struct assignment of a
// whole CretAgent could break that, and t_agent_diff then names the offset.
static inline uint64_t t_fnv(uint64_t h, const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}
#define T_FNV0 1469598103934665603ull
static inline uint64_t t_agent_hash(uint64_t h, const CretAgent* a) {
    const uint8_t* p = (const uint8_t*)a;
    size_t s0 = offsetof(CretAgent, shape), s1 = s0 + sizeof(a->shape), b0 = offsetof(CretAgent, body), b1 = b0 + sizeof(a->body);
    h = t_fnv(h, p, s0);
    if (b0 > s1) h = t_fnv(h, p + s1, b0 - s1);
    if (sizeof(CretAgent) > b1) h = t_fnv(h, p + b1, sizeof(CretAgent) - b1);
    return h;
}
static inline uint64_t t_carcass_hash(uint64_t h, const CretCarcass* k) {
    // Field by field: carcasses are written with compound literals, whose padding is unspecified.
    h = t_fnv(h, &k->active, sizeof k->active); if (!k->active) return h;
    h = t_fnv(h, &k->species, 1); h = t_fnv(h, &k->x, 4); h = t_fnv(h, &k->y, 4); h = t_fnv(h, &k->z, 4); h = t_fnv(h, &k->yaw, 4);
    h = t_fnv(h, &k->growth, 4); h = t_fnv(h, &k->meat, 4); h = t_fnv(h, &k->max_meat, 4); h = t_fnv(h, &k->age, 4); h = t_fnv(h, &k->agent, 4);
    return h;
}
static uint64_t t_sim_hash(const CretSim* s) {
    uint64_t h = T_FNV0;
    h = t_fnv(h, &s->tick, sizeof s->tick); h = t_fnv(h, &s->time_of_day, 4); h = t_fnv(h, &s->rng, 4);
    h = t_fnv(h, &s->cloud_cover, 4); h = t_fnv(h, &s->cloud_target, 4); h = t_fnv(h, &s->weather_ticks, 4); h = t_fnv(h, &s->weather_rng, 4);
    h = t_fnv(h, s->species_pick, sizeof s->species_pick); h = t_fnv(h, s->human, sizeof s->human);
    h = t_fnv(h, &s->props.ns, sizeof s->props.ns);
    for (int i = 0; i < s->cfg.num_agents; i++) h = t_agent_hash(h, &s->agents[i]);
    for (int c = 0; c < CRET_MAX_CARCASSES; c++) h = t_carcass_hash(h, &s->carcasses[c]);
    return h;
}

// Names the first CretAgent field that differs between two agents.
static const char* t_agent_diff(const CretAgent* a, const CretAgent* b, char* buf, size_t n) {
#define F(f) {#f, offsetof(CretAgent, f), sizeof(((CretAgent*)0)->f)}
    static const struct { const char* name; size_t off, size; } fields[] = {
        F(alive), F(on_ground), F(swimming), F(sprinting), F(species), F(act), F(x), F(y), F(z), F(yaw), F(vx), F(vy), F(vz),
        F(speed), F(radius), F(height), F(stamina), F(distance), F(ticks_alive), F(act_ticks), F(health), F(max_health),
        F(food), F(water), F(growth), F(bleed), F(attack_cd), F(hurt_ticks), F(dead_ticks), F(last_attacker), F(broken),
        F(group), F(rally_ticks), F(ev_dealt), F(ev_taken), F(ev_died), F(ev_kills), F(bot_state), F(bot_timer), F(bot_target),
        F(goal_x), F(goal_z)};
#undef F
    for (size_t q = 0; q < sizeof fields / sizeof fields[0]; q++)
        if (memcmp((const char*)a + fields[q].off, (const char*)b + fields[q].off, fields[q].size)) { snprintf(buf, n, "%s", fields[q].name); return buf; }
    const uint8_t *pa = (const uint8_t*)a, *pb = (const uint8_t*)b;
    for (size_t o = 0; o < offsetof(CretAgent, shape); o++) if (pa[o] != pb[o]) { snprintf(buf, n, "byte offset %zu (unlisted field or padding)", o); return buf; }
    snprintf(buf, n, "a field after `shape`"); return buf;
}
#endif
