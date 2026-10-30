// Gen 9 random battles (2026-10-30): poke-engine (Rust, ocean/pokemon9/engine, built by build_engine.sh) with the
// fixes from the Showdown differential test (997/1000 positions agree). Both seats are agents (simultaneous moves,
// zero-sum +1/-1, 0 for a draw at the turn cap), so mirror self-play trains both. Battles start from Showdown turn-1
// positions (build_positions.js, one state per line in env.positions). Observations are partial information
// (engine/src/lib.rs). v1 pipeline: no species/ability/item ids in the observation yet.
#pragma once
#include <assert.h>
typedef float obs_t;
#define PUF_HAS_BOT_POLICY
#include "pufferenv.h"

#define POKEMON9_OBS 2388
#define POKEMON9_ACTIONS 14
#define OBS_SIZE POKEMON9_OBS
#define NUM_ATNS 1
#define ACT_SIZES {POKEMON9_ACTIONS}

// C ABI of engine/src/lib.rs (the trainer compiles this header as CUDA C++).
#ifdef __cplusplus
extern "C" {
#endif
typedef struct Pk9Game Pk9Game;
Pk9Game* pk9_new(const char* state, uint64_t seed);
void pk9_free(Pk9Game* game);
int pk9_mask(const Pk9Game* game, int side, unsigned char* mask);
int pk9_step(Pk9Game* game, int a1, int a2);
void pk9_observe(const Pk9Game* game, int side, float* out);
int pk9_obs_size(void);
#ifdef __cplusplus
}
#endif

struct Log {
    float perf, score, episode_return, episode_length, n;
    float wins, draws, tera;  // side one (agent 0) wins and draws; battles where agent 0 Terastallized
    float engine_errors;      // battles ended by a poke-engine panic (pk9_step returns 4; logged, scored as a draw)
};

typedef struct Client { int unused; } Client;

// Every env shares one copy of the positions file: NUL-terminated lines.
typedef struct {
    char* text;
    char** lines;
    long count;
} Pk9Positions;

static Pk9Positions* pk9_load_positions(const char* path) {
    static Pk9Positions loaded;
    static char loaded_path[1024];
    if (loaded.count && !strcmp(loaded_path, path)) return &loaded;
    FILE* f = fopen(path, "rb");
    assert(f && "env.positions: run ocean/pokemon9/build_positions.js");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    loaded.text = (char*)malloc(size + 1);
    assert(fread(loaded.text, 1, size, f) == (size_t)size);
    fclose(f);
    loaded.text[size] = 0;
    long lines = 0;
    for (long i = 0; i < size; i++) lines += loaded.text[i] == '\n';
    loaded.lines = (char**)malloc(lines * sizeof(char*));
    char* start = loaded.text;
    for (long i = 0; i < size; i++) {
        if (loaded.text[i] != '\n') continue;
        loaded.text[i] = 0;
        loaded.lines[loaded.count++] = start;
        start = loaded.text + i + 1;
    }
    assert(loaded.count > 0);
    snprintf(loaded_path, sizeof(loaded_path), "%s", path);
    printf("Pokemon9 positions: %ld from %s\n", loaded.count, path);
    return &loaded;
}

struct Env {
    Log log;
    Agent agents[2];
    int num_agents;
    int tag;
    int boundary_reached;
    Client* client;
    Pk9Game* game;
    Pk9Positions* positions;
    uint64_t rng;
    int turns;
    int tera;          // agent 0 Terastallized this battle
    int bot_policy;    // 0: agent 1 plays; 1: side two plays uniformly random legal actions (evaluation ladder)
};

static uint64_t pk9_random(uint64_t* rng) {
    *rng ^= *rng << 13;
    *rng ^= *rng >> 7;
    *rng ^= *rng << 17;
    return *rng;
}

static void pk9_publish(Env* env) {
    for (int s = 0; s < 2; s++) {
        pk9_observe(env->game, s, env->agents[s].observations);
        pk9_mask(env->game, s, env->agents[s].action_mask);
    }
}

static void pk9_new_battle(Env* env) {
    if (env->game) pk9_free(env->game);
    const char* line = env->positions->lines[pk9_random(&env->rng) % env->positions->count];
    env->game = pk9_new(line, pk9_random(&env->rng));
    env->turns = 0;
    env->tera = 0;
}

void puf_init(Env* env, Dict* kwargs) {
    env->num_agents = 2;
    env->agents[0].policy = env->agents[1].policy = 0;
    env->rng = ((uint64_t)(unsigned)dict_get(kwargs, "seed") << 32 | (uint64_t)(uintptr_t)env) * 0x9e3779b97f4a7c15ull | 1;
    assert(pk9_obs_size() == POKEMON9_OBS);
    env->positions = pk9_load_positions(dict_get_str(kwargs, "positions"));
}

void puf_reset(Env* env) {
    pk9_new_battle(env);
    pk9_publish(env);
}

static int pk9_bot_action(Env* env) {
    unsigned char mask[POKEMON9_ACTIONS];
    int legal = pk9_mask(env->game, 1, mask), pick = pk9_random(&env->rng) % legal;
    for (int a = 0; a < POKEMON9_ACTIONS; a++) {
        if (mask[a] && pick-- == 0) return a;
    }
    assert(0);
    return 0;
}

void puf_step(Env* env) {
    int a1 = (int)env->agents[0].actions[0];
    int a2 = env->bot_policy ? pk9_bot_action(env) : (int)env->agents[1].actions[0];
    assert(env->agents[0].action_mask[a1] && (env->bot_policy || env->agents[1].action_mask[a2]));
    env->tera |= a1 >= 4 && a1 < 8;
    int result = pk9_step(env->game, a1, a2);
    env->turns++;
    float r = result == 1 ? 1 : result == 2 ? -1 : 0;
    env->agents[0].rewards[0] = r;
    env->agents[1].rewards[0] = -r;
    env->agents[0].terminals[0] = env->agents[1].terminals[0] = result != 0;
    if (result) {
        Log* log = &env->log;
        log->perf += result == 1;
        log->score += r;
        log->episode_return += r;
        log->episode_length += env->turns;
        log->wins += result == 1;
        log->draws += result == 3;
        log->engine_errors += result == 4;
        log->tera += env->tera;
        log->n += 1;
        pk9_new_battle(env);
    }
    pk9_publish(env);
}

void puf_set_bot_policy(Env* env, int policy) {
    assert(policy == 0 || policy == 1);
    env->bot_policy = policy;
}

void puf_log(Log* log, Dict* out) {
    dict_set(out, "perf", log->perf);
    dict_set(out, "score", log->score);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "n", log->n);
    dict_set(out, "win_rate", log->wins);
    dict_set(out, "draw_rate", log->draws);
    dict_set(out, "tera_rate", log->tera);
    dict_set(out, "engine_errors", log->engine_errors);
}

void puf_close(Env* env) {
    if (env->game) pk9_free(env->game);
}

void puf_render(Env* env) {
}
