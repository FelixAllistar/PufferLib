// Gen 3 random battles (2026-11-06; port of ocean/pokemon3): poke-engine (Rust, ocean/pokemon3/engine, built by
// build_engine.sh) with the Gen 3 fixes from the Showdown differential test (984/991 positions agree,
// reports/pokemon_experiments/20261105_gen3_engine). Both seats are agents (simultaneous moves, zero-sum +1/-1, 0 for a
// draw at the turn cap), so mirror self-play trains both. Battles start from Showdown turn-1 positions
// (build_positions.js, one state per line in env.positions). Observations are partial information (engine/src/lib.rs).
//
// Team mode (2026-11-06, the Gen 1 OMNI recipe for Gen 3 OU): env.sets / env.pool (build_sets.js) instead of positions.
// Each seat plays a random pool team, or (env.free_fraction, per seat and game) drafts its own: six times a species of
// the draft catalog (env.draft, its 128 most used species), then one of its sets (the catalog's up to 6 most used),
// the first one the lead; no species twice. Draft steps come before the battle in the same episode (reward at the end
// only); a seat that is not drafting waits (action 0). Forced cores (env.force_core_prob): a drafting learner seat gets
// its first two species from a common pair (env.cores), annealed linearly to 0 at env.force_core_steps env steps.
// Frozen league (vec.num_policies > 1, selfplay): pk3 spreads the frozen banks over the last hist_policy_percent of envs.
#pragma once
#include <assert.h>
// bf16 training (pufferl defines from_float and precision_t first): bf16 observations, a straight device copy into the
// rollouts at half the bytes. Standalone and float builds keep float.
#if defined(from_float) && !defined(PRECISION_FLOAT)
typedef precision_t obs_t;
#else
typedef float obs_t;
#endif
#define PUF_HAS_BOT_POLICY
#include "pufferenv.h"

#define POKEMON3_OBS 5343
#define POKEMON3_ACTIONS 128
#define PK3_BATTLE_ACTIONS 10
#define PK3_DRAFT_SETS 6
#define OBS_SIZE POKEMON3_OBS
#define NUM_ATNS 1
#define ACT_SIZES {POKEMON3_ACTIONS}

// C ABI of engine/src/lib.rs (the trainer compiles this header as CUDA C++).
#ifdef __cplusplus
extern "C" {
#endif
typedef struct Pk3Game Pk3Game;
Pk3Game* pk3_new(const char* state, uint64_t seed);
void pk3_free(Pk3Game* game);
int pk3_mask(const Pk3Game* game, int side, unsigned char* mask);
int pk3_step(Pk3Game* game, int a1, int a2);
void pk3_observe(const Pk3Game* game, int side, float* out);
void pk3_observe_bf16(const Pk3Game* game, int side, uint16_t* out);
int pk3_obs_size(void);
long pk3_sets_load(const char* path);
void pk3_sets_check(unsigned char* valid);
int pk3_maxdmg_action(const Pk3Game* game, int side);
void pk3_set_matchup(int on);
Pk3Game* pk3_new_teams(const int* t1, const int* t2, uint64_t seed);
void pk3_draft_observe(const int* own, int n_own, const int* cand, int n_cand, int picking, void* out, int bf16);
#ifdef __cplusplus
}
#endif

struct Log {
    float perf, score, episode_return, episode_length, n;
    float wins, draws;        // side one (agent 0) wins and draws
    float policy0_games, policy0_score;  // ./puffer match: seat 0 (policy 0) games and score (draws half)
    float engine_errors;      // battles ended by a poke-engine panic (pk3_step returns 4; logged, scored as a draw)
    float draft_games, draft_score;    // seats that drafted their team and their score (draws half)
    float draft_vs_pool_games, draft_vs_pool_score;  // drafted team against a pool team: the drafter's score
    float forced_games;       // drafting seats given a forced core
    float league_games, league_score;  // learner (seat 0) against a frozen league bank: its score
};

typedef struct Client { int unused; } Client;

// Every env shares one copy of the positions file: NUL-terminated lines.
typedef struct {
    char* text;
    char** lines;
    long count;
} Pk3Positions;

static Pk3Positions* pk3_load_positions(const char* path) {
    static Pk3Positions loaded;
    static char loaded_path[1024];
    if (loaded.count && !strcmp(loaded_path, path)) return &loaded;
    FILE* f = fopen(path, "rb");
    assert(f && "env.positions: run ocean/pokemon3/build_positions.js");
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
    printf("Pokemon3 positions: %ld from %s\n", loaded.count, path);
    return &loaded;
}

// Team mode data, shared by every env: the pool (six set indices per team), the draft catalog and the cores.
typedef struct {
    int (*teams)[6];
    long count;
} Pk3Pool;
typedef struct {
    int species;                 // catalog lines
    int nsets[POKEMON3_ACTIONS];
    int sets[POKEMON3_ACTIONS][PK3_DRAFT_SETS];
    int ncores;
    int (*cores)[2];
} Pk3Draft;
static unsigned char* pk3_valid;
static long pk3_nsets;
static Pk3Draft pk3_draft;

static FILE* pk3_open(const char* path, const char* what) {
    FILE* f = fopen(path, "r");
    if (!f) { fprintf(stderr, "pokemon3: cannot open %s %s (ocean/pokemon3/build_sets.js)\n", what, path); exit(1); }
    return f;
}
// Teams with a set the engine cannot load are dropped.
static Pk3Pool* pk3_load_pool(const char* sets, const char* path) {
    static Pk3Pool pools[4];
    static char names[4][1024];
    static int npools;
    for (int i = 0; i < npools; i++) if (!strcmp(names[i], path)) return &pools[i];
    assert(npools < 4);
    if (!pk3_valid) {
        pk3_nsets = pk3_sets_load(sets);
        if (pk3_nsets <= 0) { fprintf(stderr, "pokemon3: cannot read env.sets %s\n", sets); exit(1); }
        pk3_valid = (unsigned char*)malloc(pk3_nsets);
        pk3_sets_check(pk3_valid);
    }
    Pk3Pool* pool = &pools[npools];
    snprintf(names[npools++], sizeof(names[0]), "%s", path);
    FILE* f = pk3_open(path, "pool");
    long cap = 1 << 16, dropped = 0;
    pool->teams = (int (*)[6])malloc(cap * sizeof(*pool->teams));
    int t[6];
    while (fscanf(f, "%d %d %d %d %d %d", t, t + 1, t + 2, t + 3, t + 4, t + 5) == 6) {
        int ok = 1;
        for (int i = 0; i < 6; i++) ok &= t[i] >= 0 && t[i] < pk3_nsets && pk3_valid[t[i]];
        if (!ok) { dropped++; continue; }
        if (pool->count == cap) pool->teams = (int (*)[6])realloc(pool->teams, (cap *= 2) * sizeof(*pool->teams));
        memcpy(pool->teams[pool->count++], t, sizeof(t));
    }
    fclose(f);
    assert(pool->count > 0);
    printf("Pokemon3 team pool: %ld teams from %s (%ld dropped), %ld sets\n", pool->count, path, dropped, pk3_nsets);
    return pool;
}
static void pk3_load_draft(const char* path, const char* cores) {
    if (pk3_draft.species) return;
    FILE* f = pk3_open(path, "draft");
    char line[4096];
    while (fgets(line, sizeof(line), f) && pk3_draft.species < POKEMON3_ACTIONS) {
        int k = pk3_draft.species, n = 0;
        char* p = strchr(line, ' ');  // species name, then its team count, then set indices
        if (!p) continue;
        strtol(p, &p, 10);
        for (long x = strtol(p, &p, 10); n < PK3_DRAFT_SETS && x >= 0 && p && *p != '\n'; x = strtol(p, &p, 10)) {
            if (x < pk3_nsets && pk3_valid[x]) pk3_draft.sets[k][n++] = (int)x;
            if (!*p || *p == '\n') break;
        }
        pk3_draft.nsets[pk3_draft.species++] = n;  // a species with no loadable set keeps its line, never offered
    }
    fclose(f);
    if (strcmp(cores, "None")) {
        f = pk3_open(cores, "cores");
        pk3_draft.cores = (int (*)[2])malloc(4096 * sizeof(*pk3_draft.cores));
        int a, b;
        while (pk3_draft.ncores < 4096 && fscanf(f, "%d %d", &a, &b) == 2)
            if (a < pk3_draft.species && b < pk3_draft.species) {
                pk3_draft.cores[pk3_draft.ncores][0] = a;
                pk3_draft.cores[pk3_draft.ncores++][1] = b;
            }
        fclose(f);
    }
    printf("Pokemon3 draft: %d species, %d cores\n", pk3_draft.species, pk3_draft.ncores);
}

struct Env {
    Log log;
    Agent agents[2];
    int num_agents;
    int tag;
    int boundary_reached;
    Client* client;
    Pk3Game* game;
    Pk3Positions* positions;
    uint64_t rng;
    int turns;
    int bot_policy;    // 0: agent 1 plays; side two plays 1: uniformly random legal actions, 2: max damage (evaluation)
    // Team mode
    Pk3Pool* pool;
    float free_fraction, force_core_prob;
    double force_core_steps;
    long steps;
    int drafting;      // still in the draft
    int drafts[2];     // seat drafts this game
    int team[2][6];
    int ndrafted[2];
    int pending[2];    // catalog species picked, set not yet (-1 none)
    int forced[2][2];  // forced core species for the first two picks (-1 none)
    unsigned char used[2][POKEMON3_ACTIONS];
    FILE* draft_log;   // env.draft_log: one line per finished team-mode game (diagnostics; shared by every env)
};

static uint64_t pk3_random(uint64_t* rng) {
    *rng ^= *rng << 13;
    *rng ^= *rng >> 7;
    *rng ^= *rng << 17;
    return *rng;
}

static int pk3_learner(Env* env, int s) { return env->agents[s].policy == 0 || env->tag == 0; }

static void pk3_draft_publish(Env* env) {
    for (int s = 0; s < 2; s++) {
        unsigned char* mask = env->agents[s].action_mask;
        memset(mask, 0, POKEMON3_ACTIONS);
        int done = !env->drafts[s] || env->ndrafted[s] == 6;
        int picking = done ? 0 : env->pending[s] < 0 ? 1 : 2;
        int k = env->pending[s];
        pk3_draft_observe(env->team[s], env->ndrafted[s], k >= 0 ? pk3_draft.sets[k] : env->team[s],
            k >= 0 ? pk3_draft.nsets[k] : 0, picking, env->agents[s].observations, sizeof(obs_t) == 2);
        if (picking == 0) mask[0] = 1;
        else if (picking == 2) memset(mask, 1, pk3_draft.nsets[k]);
        else {
            int n = env->ndrafted[s];
            if (n < 2 && env->forced[s][n] >= 0) mask[env->forced[s][n]] = 1;
            else for (int a = 0; a < pk3_draft.species; a++) mask[a] = !env->used[s][a] && pk3_draft.nsets[a] > 0;
        }
    }
}

static void pk3_publish(Env* env) {
    if (env->drafting) { pk3_draft_publish(env); return; }
    for (int s = 0; s < 2; s++) {
        if (sizeof(obs_t) == 2) pk3_observe_bf16(env->game, s, (uint16_t*)env->agents[s].observations);
        else pk3_observe(env->game, s, (float*)env->agents[s].observations);
        pk3_mask(env->game, s, env->agents[s].action_mask);
    }
}

static double pk3_uniform(uint64_t* rng) { return (pk3_random(rng) >> 11) * 0x1.0p-53; }

static void pk3_new_battle(Env* env) {
    if (env->game) pk3_free(env->game);
    env->game = NULL;
    env->turns = 0;
    if (!env->pool) {
        const char* line = env->positions->lines[pk3_random(&env->rng) % env->positions->count];
        env->game = pk3_new(line, pk3_random(&env->rng));
        return;
    }
    double core = env->force_core_prob;
    if (env->force_core_steps > 0) core *= fmax(0.0, 1.0 - env->steps / env->force_core_steps);
    env->drafting = 0;
    for (int s = 0; s < 2; s++) {
        env->drafts[s] = env->free_fraction > 0 && pk3_uniform(&env->rng) < env->free_fraction;
        env->ndrafted[s] = 0;
        env->pending[s] = -1;
        env->forced[s][0] = env->forced[s][1] = -1;
        memset(env->used[s], 0, POKEMON3_ACTIONS);
        if (!env->drafts[s]) {
            memcpy(env->team[s], env->pool->teams[pk3_random(&env->rng) % env->pool->count], sizeof(env->team[s]));
            continue;
        }
        env->drafting = 1;
        if (pk3_learner(env, s) && pk3_draft.ncores && core > 0 && pk3_uniform(&env->rng) < core) {
            int* c = pk3_draft.cores[pk3_random(&env->rng) % pk3_draft.ncores];
            int first = pk3_random(&env->rng) & 1;  // either species may lead
            env->forced[s][0] = c[first];
            env->forced[s][1] = c[1 - first];
            env->log.forced_games += 1;
        }
    }
    if (!env->drafting) env->game = pk3_new_teams(env->team[0], env->team[1], pk3_random(&env->rng));
}

// One draft decision for both seats (a seat that is not drafting waits). Returns 1 when both teams are complete.
static int pk3_draft_step(Env* env, const int* actions) {
    for (int s = 0; s < 2; s++) {
        if (!env->drafts[s] || env->ndrafted[s] == 6) continue;
        int a = actions[s];
        if (env->pending[s] < 0) {
            assert(a >= 0 && a < pk3_draft.species && !env->used[s][a]);
            env->pending[s] = a;
            env->used[s][a] = 1;
        } else {
            assert(a >= 0 && a < pk3_draft.nsets[env->pending[s]]);
            env->team[s][env->ndrafted[s]++] = pk3_draft.sets[env->pending[s]][a];
            env->pending[s] = -1;
        }
    }
    for (int s = 0; s < 2; s++) if (env->drafts[s] && env->ndrafted[s] < 6) return 0;
    env->drafting = 0;
    env->game = pk3_new_teams(env->team[0], env->team[1], pk3_random(&env->rng));
    return 1;
}

void puf_init(Env* env, Dict* kwargs) {
    env->num_agents = 2;
    // Seat 1 is policy 1 for ./puffer match (frozen envs use these); with one policy (training) every seat is the learner.
    env->agents[0].policy = 0;
    env->agents[1].policy = 1;
    env->rng = ((uint64_t)(unsigned)dict_get(kwargs, "seed") << 32 | (uint64_t)(uintptr_t)env) * 0x9e3779b97f4a7c15ull | 1;
    assert(pk3_obs_size() == POKEMON3_OBS);
    const char* pool = dict_find(kwargs, "pool") ? dict_get_str(kwargs, "pool") : "None";
    if (strcmp(pool, "None")) {
        const char* sets = dict_get_str(kwargs, "sets");
        env->pool = pk3_load_pool(sets, pool);
        DictItem* f = dict_find(kwargs, "free_fraction");
        env->free_fraction = f ? f->value : 0;
        DictItem* c = dict_find(kwargs, "force_core_prob");
        env->force_core_prob = c ? c->value : 0;
        DictItem* cs = dict_find(kwargs, "force_core_steps");
        env->force_core_steps = cs ? cs->value : 0;
        assert(env->free_fraction >= 0 && env->free_fraction <= 1 && env->force_core_prob >= 0 && env->force_core_prob <= 1);
        if (env->free_fraction > 0) {
            DictItem* cores = dict_find(kwargs, "cores");
            pk3_load_draft(dict_get_str(kwargs, "draft"), cores ? dict_get_str(kwargs, "cores") : "None");
        }
        // env.draft_log = PATH: append "result drafted0 drafted1 turns team0[6] team1[6]" per game (set indices, lead
        // first; result 1/2 = seat 0/1 won, 3 draw, 4 engine error). stdio locks the FILE, so lines stay whole.
        static FILE* draft_log;
        DictItem* dl = dict_find(kwargs, "draft_log");
        if (dl && strcmp(dict_get_str(kwargs, "draft_log"), "None")) {
            if (!draft_log) draft_log = fopen(dict_get_str(kwargs, "draft_log"), "a");
            assert(draft_log);
            env->draft_log = draft_log;
        }
    } else env->positions = pk3_load_positions(dict_get_str(kwargs, "positions"));
    // env.bot_policy = 1: side two plays random legal moves; 2: the max-damage baseline (pk3_maxdmg_action, full
    // information; drafts at random). ./puffer eval MODEL --env.bot_policy=N; perf = win rate.
    DictItem* bot = dict_find(kwargs, "bot_policy");
    env->bot_policy = bot ? (int)bot->value : 0;
    assert(env->bot_policy >= 0 && env->bot_policy <= 2);
    // env.matchup = 0 zeroes the matchup features (the A/B control; same observation size). Process wide.
    DictItem* mu = dict_find(kwargs, "matchup");
    pk3_set_matchup(mu ? (int)mu->value : 1);
}

void puf_reset(Env* env) {
    pk3_new_battle(env);
    pk3_publish(env);
}

static int pk3_bot_action(Env* env) {
    unsigned char mask[POKEMON3_ACTIONS];
    if (env->drafting) {  // the random bot drafts at random too
        memcpy(mask, env->agents[1].action_mask, POKEMON3_ACTIONS);
        int legal = 0;
        for (int a = 0; a < POKEMON3_ACTIONS; a++) legal += mask[a];
        int pick = pk3_random(&env->rng) % legal;
        for (int a = 0; a < POKEMON3_ACTIONS; a++) if (mask[a] && pick-- == 0) return a;
    }
    if (env->bot_policy == 2) return pk3_maxdmg_action(env->game, 1);
    int legal = pk3_mask(env->game, 1, mask), pick = pk3_random(&env->rng) % legal;
    for (int a = 0; a < POKEMON3_ACTIONS; a++) {
        if (mask[a] && pick-- == 0) return a;
    }
    assert(0);
    return 0;
}

void puf_step(Env* env) {
    int a1 = (int)env->agents[0].actions[0];
    int a2 = env->bot_policy ? pk3_bot_action(env) : (int)env->agents[1].actions[0];
    assert(env->agents[0].action_mask[a1] && (env->bot_policy || env->agents[1].action_mask[a2]));
    env->steps++;
    if (env->drafting) {
        int actions[2] = {a1, a2};
        pk3_draft_step(env, actions);
        env->agents[0].rewards[0] = env->agents[1].rewards[0] = 0;
        env->agents[0].terminals[0] = env->agents[1].terminals[0] = 0;
        pk3_publish(env);
        return;
    }
    int result = pk3_step(env->game, a1, a2);
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
        log->policy0_games += 1;
        log->policy0_score += result == 1 ? 1.0f : result == 2 ? 0.0f : 0.5f;
        log->n += 1;
        for (int seat = 0; seat < 2; seat++) {
            if (!env->pool || !env->drafts[seat]) continue;
            float score = result == 3 || result == 4 ? 0.5f : (result == 1) == (seat == 0);
            log->draft_games += 1;
            log->draft_score += score;
            if (!env->drafts[1 - seat]) { log->draft_vs_pool_games += 1; log->draft_vs_pool_score += score; }
        }
        if (env->draft_log) {
            const int* t = env->team[0];
            const int* u = env->team[1];
            fprintf(env->draft_log, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n", result, env->drafts[0],
                env->drafts[1], env->turns, t[0], t[1], t[2], t[3], t[4], t[5], u[0], u[1], u[2], u[3], u[4], u[5]);
            fflush(env->draft_log);
        }
        if (env->tag > 0) {
            log->league_games += 1;
            log->league_score += result == 1 ? 1.0f : result == 2 ? 0.0f : 0.5f;
        }
        pk3_new_battle(env);
    }
    pk3_publish(env);
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
    dict_set(out, "engine_errors", log->engine_errors);
    dict_set(out, "policy_0_score", log->policy0_games ? log->policy0_score / log->policy0_games : 0);
    dict_set(out, "draft_share", log->n ? log->draft_games / (2 * log->n) : 0);
    dict_set(out, "draft_score", log->draft_games ? log->draft_score / log->draft_games : 0);
    dict_set(out, "draft_vs_pool", log->draft_vs_pool_games ? log->draft_vs_pool_score / log->draft_vs_pool_games : 0);
    dict_set(out, "forced_core_share", log->draft_games ? log->forced_games / log->draft_games : 0);
    dict_set(out, "vs_league", log->league_games ? log->league_score / log->league_games : 0);
}

// Frozen league (selfplay with vec.num_policies > 1): the last hist_policy_percent of each buffer's envs put a frozen
// bank on seat 1, banks spread round robin; the rest are learner self-play (both seats policy 0). With one policy
// (training without a league) or two (./puffer match) seat 1 is policy 1 as before.
#define MY_VEC_INIT
Env* my_vec_init(int* size, int* starts, int* counts, Dict* vk, Dict* ek) {
    int total = dict_get(vk, "total_agents"), buffers = dict_get(vk, "num_buffers"), policies = dict_get(vk, "num_policies");
    assert(total > 0 && buffers > 0 && total % (2 * buffers) == 0);
    int games = total / 2, per_buffer = games / buffers;
    int frozen_start = per_buffer - (int)(per_buffer * dict_get(vk, "hist_policy_percent"));
    Env* envs = (Env*)calloc(games, sizeof(Env));
    for (int buf = 0; buf < buffers; buf++) {
        starts[buf] = buf * per_buffer;
        counts[buf] = per_buffer;
        for (int e = 0; e < per_buffer; e++) {
            Env* env = envs + starts[buf] + e;
            env->rng = starts[buf] + e;
            puf_init(env, ek);
            int bank = policies > 2 && e >= frozen_start ? 1 + (e - frozen_start) % (policies - 1) : 0;
            env->tag = policies == 2 ? (e >= frozen_start) : bank;
            env->agents[1].policy = policies == 2 ? 1 : bank;
        }
    }
    *size = games;
    return envs;
}

void puf_close(Env* env) {
    if (env->game) pk3_free(env->game);
}

void puf_render(Env* env) {
}
