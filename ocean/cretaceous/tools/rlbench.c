// End-to-end RL throughput: steps the PufferLib binding (cretaceous.h) the way
// the trainer's CPU workers do, with random actions, and reports agent-steps
// per second per core and in total. Every step is the trainer's puf_step:
// decode actions, cret_sim_step (physics, statuses, survival, quests, nests,
// critters, carcasses), rewards, terminals, episode logs and cret_sim_observe
// for every agent. No GPU, window or ENet.
//
//   make -C ocean/cretaceous rlbench && ./build/cretaceous/rlbench
//   ./build/cretaceous/rlbench --threads 1,2,4 --envs 8 --seconds 10
//   ./build/cretaceous/rlbench --critters          every slot a person (critters around each)
//   ./build/cretaceous/rlbench --hash --threads 1  fingerprint of every obs, reward and terminal
//
// Options:
//   --threads LIST   comma-separated OpenMP thread counts (default 1,2,4)
//   --envs N         envs per thread (default 8); each env has [env] num_agents agents
//   --agents N       agents per env (default: config)
//   --seconds S      timed seconds per thread count (default 10), after --warmup ticks (default 300)
//   --ticks T        time exactly T ticks per env instead of --seconds (needed for --hash)
//   --world-n N --world-cell C   another world (default: config, the training island)
//   --max-ticks T    episode length (default: config)
//   --bots           drive every agent with the scripted bots (cret_bot_input, the server's cost)
//                    instead of random actions: bot input, cret_sim_step and cret_sim_observe
//   --critters       make every slot a person spawning as a hatchling, so the critter layer runs around
//                    each (the trainer has no people and never runs it)
//   --hash           print an FNV-1a hash of all observations, rewards and terminals, per env in env order,
//                    so it does not depend on the thread count; an optimisation must leave it unchanged
//   --check          count non-finite observations and rewards and observations outside [-5, 5]
//
// Envs are spread over the threads with `omp parallel for schedule(static)`,
// as src/pufferl.cu does (vec.num_threads / vec.num_buffers workers per
// buffer). Times are wall clock over the parallel region; "per core" divides
// the total by the thread count. Run from the repository root (the world cache
// is build/cretaceous/worlds, the config config/cretaceous.ini).
#include <omp.h>
#include <time.h>
#include "cretaceous.h"

static double wall(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static uint32_t xs(uint32_t* s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

typedef struct Slot {
    Env* env;
    float *obs, *act, *rew, *term;
    uint32_t rng;     // random actions
    uint64_t hash;    // FNV-1a over obs, rewards, terminals
    long nonfinite, outside;
} Slot;

static uint64_t fnv(uint64_t h, const void* p, size_t n) {
    const unsigned char* b = p;
    for (size_t k = 0; k < n; k++) { h ^= b[k]; h *= 1099511628211ull; }
    return h;
}

static void make_slot(Slot* sl, Dict* kw, unsigned seed, bool critters) {
    Env* env = calloc(1, sizeof(Env)); env->rng = seed;
    puf_init(env, kw);
    if (critters) {   // every slot a person: re-init the sim with player growth set, mark all human
        CretConfig cfg = env->sim->cfg; cfg.player_growth = 0.35f;
        cret_sim_close(env->sim); memset(env->sim, 0, sizeof *env->sim);
        if (cret_sim_init(env->sim, cfg) != 0) { fprintf(stderr, "rlbench: sim init failed\n"); exit(1); }
        for (int i = 0; i < cfg.num_agents; i++) env->sim->human[i] = 1;
    }
    int n = env->num_agents;
    sl->env = env; sl->rng = seed * 2654435761u | 1; sl->hash = 14695981039346656037ull;
    sl->obs = calloc((size_t)n * OBS_SIZE, sizeof(float)); sl->act = calloc((size_t)n * NUM_ATNS, sizeof(float));
    sl->rew = calloc(n, sizeof(float)); sl->term = calloc(n, sizeof(float));
    for (int i = 0; i < n; i++) {
        env->agents[i].observations = sl->obs + (size_t)i * OBS_SIZE; env->agents[i].actions = sl->act + i * NUM_ATNS;
        env->agents[i].rewards = sl->rew + i; env->agents[i].terminals = sl->term + i;
    }
    puf_reset(env);
}

static void free_slot(Slot* sl) { puf_close(sl->env); free(sl->env); free(sl->obs); free(sl->act); free(sl->rew); free(sl->term); }

// One trainer step of one env: random actions, then puf_step. With bots,
// the scripted bots drive every agent and the env steps and observes as the
// trainer would.
static bool g_bots;
static void step_slot(Slot* sl, bool hash, bool check) {
    static const int sizes[NUM_ATNS] = ACT_SIZES;
    Env* env = sl->env; int n = env->num_agents;
    if (g_bots) {
        CretInput in[CRET_MAX_AGENTS]; CretSim* s = env->sim;
        for (int i = 0; i < n; i++) in[i] = cret_bot_input(s, i);
        cret_sim_step(s, in);
        if (s->tick >= s->cfg.max_ticks) cret_sim_reset(s);
        for (int i = 0; i < n; i++) cret_sim_observe(s, i, env->agents[i].observations);
        if (hash) sl->hash = fnv(sl->hash, sl->obs, (size_t)n * OBS_SIZE * sizeof(float));
        return;
    }
    for (int i = 0; i < n * NUM_ATNS; i++) sl->act[i] = (float)(xs(&sl->rng) % sizes[i % NUM_ATNS]);
    memset(sl->rew, 0, n * sizeof(float)); memset(sl->term, 0, n * sizeof(float));   // as the trainer clears them
    puf_step(env);
    if (hash) {
        sl->hash = fnv(sl->hash, sl->obs, (size_t)n * OBS_SIZE * sizeof(float));
        sl->hash = fnv(sl->hash, sl->rew, n * sizeof(float)); sl->hash = fnv(sl->hash, sl->term, n * sizeof(float));
    }
    if (check) {
        for (int k = 0; k < n * OBS_SIZE; k++) { float v = sl->obs[k]; if (!isfinite(v)) sl->nonfinite++; else if (fabsf(v) > 5) sl->outside++; }
        for (int i = 0; i < n; i++) if (!isfinite(sl->rew[i])) sl->nonfinite++;
    }
}

int main(int argc, char** argv) {
    int threads[16] = {1, 2, 4}, nthr = 3, envs_per = 8, agents = 0, warmup = 300, ticks = 0, world_n = 0, max_ticks = 0;
    double seconds = 10; float world_cell = 0; bool critters = false, hash = false, check = false;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i]; bool more = i + 1 < argc;
        if (!strcmp(a, "--threads") && more) {
            nthr = 0; for (char* t = strtok(argv[++i], ","); t && nthr < 16; t = strtok(NULL, ",")) threads[nthr++] = atoi(t);
        }
        else if (!strcmp(a, "--envs") && more) envs_per = atoi(argv[++i]);
        else if (!strcmp(a, "--agents") && more) agents = atoi(argv[++i]);
        else if (!strcmp(a, "--seconds") && more) seconds = atof(argv[++i]);
        else if (!strcmp(a, "--warmup") && more) warmup = atoi(argv[++i]);
        else if (!strcmp(a, "--ticks") && more) ticks = atoi(argv[++i]);
        else if (!strcmp(a, "--world-n") && more) world_n = atoi(argv[++i]);
        else if (!strcmp(a, "--world-cell") && more) world_cell = (float)atof(argv[++i]);
        else if (!strcmp(a, "--max-ticks") && more) max_ticks = atoi(argv[++i]);
        else if (!strcmp(a, "--critters")) critters = true;
        else if (!strcmp(a, "--bots")) g_bots = true;
        else if (!strcmp(a, "--hash")) hash = true;
        else if (!strcmp(a, "--check")) check = true;
        else {
            fprintf(stderr, "usage: rlbench [--threads 1,2,4] [--envs N] [--agents N] [--seconds S | --ticks T] [--warmup T]\n"
                            "               [--world-n N --world-cell C] [--max-ticks T] [--critters] [--bots] [--hash] [--check]\n");
            return 2;
        }
    }
    if (hash && !ticks) { fprintf(stderr, "rlbench: --hash needs --ticks (a fixed amount of work)\n"); return 2; }
    FILE* cf = fopen("config/cretaceous.ini", "r");
    if (!cf) { fprintf(stderr, "rlbench: run from the repository root (config/cretaceous.ini)\n"); return 2; }
    fclose(cf);
    Ini ini = {0}; puf_ini_load_file(&ini, "config/cretaceous.ini");
    Dict* kw = puf_ini_section(&ini, "env", 0);
    if (agents) dict_set(kw, "num_agents", agents);
    if (world_n) dict_set(kw, "world_n", world_n);
    if (world_cell > 0) dict_set(kw, "world_cell", world_cell);
    if (max_ticks) dict_set(kw, "max_ticks", max_ticks);
    int na = (int)dict_get(kw, "num_agents");
    fprintf(stderr, "rlbench: world %d^2 at %g m, %d agents per env, %d envs per thread, episodes of %d ticks%s%s\n",
            (int)dict_get(kw, "world_n"), dict_get(kw, "world_cell"), na, envs_per, (int)dict_get(kw, "max_ticks"),
            critters ? ", every slot a person (critters on)" : "", g_bots ? ", scripted bots" : "");
    fprintf(stderr, "  %7s %5s %12s %14s %14s %10s\n", "threads", "envs", "ticks/env", "steps/s total", "steps/s/core", "us/step");
    for (int t = 0; t < nthr; t++) {
        int nt = threads[t], ne = nt * envs_per;
        Slot* sl = calloc(ne, sizeof(Slot));
        for (int e = 0; e < ne; e++) make_slot(&sl[e], kw, 1000 + e, critters);
        #pragma omp parallel for schedule(static) num_threads(nt)
        for (int e = 0; e < ne; e++) for (int k = 0; k < warmup; k++) step_slot(&sl[e], hash, check);
        // Timed: the trainer steps every env once per horizon step, all threads in step.
        long done = 0; double t0 = wall(), el = 0;
        for (;;) {
            #pragma omp parallel for schedule(static) num_threads(nt)
            for (int e = 0; e < ne; e++) step_slot(&sl[e], hash, check);
            done++; el = wall() - t0;
            if (ticks ? done >= ticks : el >= seconds) break;
        }
        double sps = (double)done * ne * na / el;
        fprintf(stderr, "  %7d %5d %12ld %14.0f %14.0f %10.2f\n", nt, ne, done, sps, sps / nt, 1e6 * nt / sps);
        if (hash) { uint64_t h = 14695981039346656037ull; for (int e = 0; e < ne; e++) h = fnv(h, &sl[e].hash, sizeof sl[e].hash); fprintf(stderr, "  hash %016llx\n", (unsigned long long)h); }
        if (check) {
            long nf = 0, out = 0; for (int e = 0; e < ne; e++) { nf += sl[e].nonfinite; out += sl[e].outside; }
            fprintf(stderr, "  check: %ld non-finite obs or rewards, %ld obs outside [-5, 5]\n", nf, out);
        }
        for (int e = 0; e < ne; e++) free_slot(&sl[e]);
        free(sl);
    }
    return 0;
}
