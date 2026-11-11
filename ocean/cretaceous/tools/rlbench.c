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
//   ./build/cretaceous/rlbench --hash --threads 1 --ticks 4000   fingerprint of every obs, reward and terminal
//   ./build/cretaceous/rlbench --stats --threads 4 --envs 4 --ticks 36000   observation and episode statistics
//
// Options:
//   --threads LIST   comma-separated OpenMP thread counts (default 1,2,4)
//   --envs N         envs per thread (default 8); each env has [env] num_agents agents
//   --agents N       agents per env (default: config)
//   --seconds S      timed seconds per thread count (default 10), after --warmup ticks (default 300)
//   --ticks T        time exactly T ticks per env instead of --seconds (needed for --hash)
//   --world-n N --world-cell C   another world (default: config, the training island)
//   --max-ticks T    episode length (default: config)
//   --critters       make every slot a person spawning as a hatchling, so the critter layer runs around
//                    each (the trainer has no people and never runs it)
//   --hash           print an FNV-1a hash of all observations, rewards and terminals, per env in env order:
//                    the same --threads, --envs and --ticks give the same hash however the threads are
//                    scheduled, and a change that keeps the env's outputs must keep it
//   --stats          what a learner sees: per observation feature its min, max, mean, the share of
//                    steps it is nonzero, constant features and values outside [-5, 5] (patch cells and
//                    neighbours pooled, as in tests/test_binding.c, whose tables name the features);
//                    non-finite observations and rewards; and per species the agents' RL episodes (to a
//                    death or the episode end): count, deaths, mean length and return, and the largest |reward|
//
// Envs are spread over the threads with `omp parallel for schedule(static)`,
// as src/pufferl.cu does (vec.num_threads / vec.num_buffers workers per
// buffer). Rates are wall clock over the timed ticks; "per core" divides the
// total by the thread count; "us/step" is wall time x threads per agent-step,
// and "cpu us" the process CPU time per agent-step (steadier than wall time
// on a shared machine; it includes OpenMP threads spinning at the barrier). Run from the repository root (the world cache
// is build/cretaceous/worlds, the config config/cretaceous.ini).
#include <omp.h>
#include <time.h>
#include "cretaceous.h"

static double clock_s(clockid_t c) { struct timespec t; clock_gettime(c, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static double wall(void) { return clock_s(CLOCK_MONOTONIC); }
static uint32_t xs(uint32_t* s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

// Observation rows: the self features, the patch's height and water (pooled
// over its cells), the neighbour features (pooled over the six), the contract
// 4 self block, then the contract 4 neighbour features (pooled).
enum { R_PATCH = CRET_SELF_OBS, R_NEIGH = R_PATCH + 2, R_SELF4 = R_NEIGH + CRET_NEIGHBOR_OBS, R_NEIGH4 = R_SELF4 + CRET_SELF_OBS4,
       NROWS = R_NEIGH4 + CRET_NEIGHBOR_OBS4 };
static int obs_row(int k) {
    if (k < CRET_OBS_PATCH) return k;
    if (k < CRET_OBS_NEIGHBOR) return R_PATCH + (k - CRET_OBS_PATCH) % 2;
    if (k < CRET_OBS_V3) return R_NEIGH + (k - CRET_OBS_NEIGHBOR) % CRET_NEIGHBOR_OBS;
    if (k < CRET_OBS_V3 + CRET_SELF_OBS4) return R_SELF4 + k - CRET_OBS_V3;
    return R_NEIGH4 + (k - CRET_OBS_V3 - CRET_SELF_OBS4) % CRET_NEIGHBOR_OBS4;
}
static void row_name(int r, char* out, size_t n) {
    if (r < R_PATCH) snprintf(out, n, "self %d", r);
    else if (r < R_NEIGH) snprintf(out, n, "patch %s", r == R_PATCH ? "height" : "water");
    else if (r < R_SELF4) snprintf(out, n, "neighbour %d", r - R_NEIGH);
    else if (r < R_NEIGH4) snprintf(out, n, "self4 %d", r - R_SELF4);
    else snprintf(out, n, "neighbour4 %d", r - R_NEIGH4);
}

typedef struct Stats {
    double sum[NROWS]; float lo[NROWS], hi[NROWS]; long nz[NROWS], n[NROWS], big[NROWS];
    long nonfinite; float rmax;
    // RL episodes per species: to a death or the episode end.
    long eps[CRET_SPECIES], deaths[CRET_SPECIES]; double len[CRET_SPECIES], ret[CRET_SPECIES]; float rlo[CRET_SPECIES], rhi[CRET_SPECIES];
    double run_ret[CRET_MAX_AGENTS]; long run_len[CRET_MAX_AGENTS];
} Stats;

static void stats_init(Stats* st) {
    memset(st, 0, sizeof *st);
    for (int r = 0; r < NROWS; r++) { st->lo[r] = INFINITY; st->hi[r] = -INFINITY; }
    for (int q = 0; q < CRET_SPECIES; q++) { st->rlo[q] = INFINITY; st->rhi[q] = -INFINITY; }
}

typedef struct Slot {
    Env* env;
    float *obs, *act, *rew, *term;
    uint32_t rng;     // random actions
    uint64_t hash;    // FNV-1a over obs, rewards, terminals
    Stats* st;        // --stats
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

static void free_slot(Slot* sl) { puf_close(sl->env); free(sl->env); free(sl->obs); free(sl->act); free(sl->rew); free(sl->term); free(sl->st); }

static void stats_step(Stats* st, const Env* env, const float* obs, const float* rew, const float* term) {
    const CretSim* s = env->sim; int n = env->num_agents;
    for (int i = 0; i < n; i++) {
        const float* o = obs + (size_t)i * OBS_SIZE;
        for (int k = 0; k < OBS_SIZE; k++) {
            float v = o[k]; int r = obs_row(k);
            if (!isfinite(v)) { st->nonfinite++; continue; }
            st->sum[r] += v; st->n[r]++; st->nz[r] += v != 0; st->big[r] += fabsf(v) > 5;
            if (v < st->lo[r]) st->lo[r] = v;
            if (v > st->hi[r]) st->hi[r] = v;
        }
        if (!isfinite(rew[i])) { st->nonfinite++; continue; }
        st->rmax = fmaxf(st->rmax, fabsf(rew[i]));
        st->run_ret[i] += rew[i]; st->run_len[i]++;
        if (term[i] > 0) {   // a death, or the episode end (the sim has just been reset: tick 0)
            int q = s->agents[i].species;
            st->eps[q]++; st->deaths[q] += s->tick > 0; st->len[q] += st->run_len[i]; st->ret[q] += st->run_ret[i];
            st->rlo[q] = fminf(st->rlo[q], (float)st->run_ret[i]); st->rhi[q] = fmaxf(st->rhi[q], (float)st->run_ret[i]);
            st->run_ret[i] = 0; st->run_len[i] = 0;
        }
    }
}

static void stats_print(Slot* sl, int ne) {
    Stats t; stats_init(&t);
    for (int e = 0; e < ne; e++) {
        const Stats* st = sl[e].st;
        for (int r = 0; r < NROWS; r++) {
            t.sum[r] += st->sum[r]; t.n[r] += st->n[r]; t.nz[r] += st->nz[r]; t.big[r] += st->big[r];
            t.lo[r] = fminf(t.lo[r], st->lo[r]); t.hi[r] = fmaxf(t.hi[r], st->hi[r]);
        }
        t.nonfinite += st->nonfinite; t.rmax = fmaxf(t.rmax, st->rmax);
        for (int q = 0; q < CRET_SPECIES; q++) {
            t.eps[q] += st->eps[q]; t.deaths[q] += st->deaths[q]; t.len[q] += st->len[q]; t.ret[q] += st->ret[q];
            t.rlo[q] = fminf(t.rlo[q], st->rlo[q]); t.rhi[q] = fmaxf(t.rhi[q], st->rhi[q]);
        }
    }
    fprintf(stderr, "  observations: %ld non-finite values (obs and rewards); largest |reward| %.3f\n", t.nonfinite, t.rmax);
    fprintf(stderr, "  %-14s %9s %9s %9s %8s  %s\n", "feature", "min", "max", "mean", "nonzero", "flags");
    for (int r = 0; r < NROWS; r++) {
        char name[32]; row_name(r, name, sizeof name);
        fprintf(stderr, "  %-14s %9.3f %9.3f %9.3f %7.1f%%  %s%s\n", name, t.lo[r], t.hi[r], t.n[r] ? t.sum[r] / t.n[r] : 0,
                t.n[r] ? 100.0 * t.nz[r] / t.n[r] : 0, t.lo[r] == t.hi[r] ? "const " : "", t.big[r] ? "outside [-5, 5]" : "");
    }
    fprintf(stderr, "  RL episodes (to a death or the episode end), random actions:\n");
    fprintf(stderr, "  %-19s %6s %7s %12s %9s %9s %9s\n", "species", "eps", "deaths", "mean ticks", "return", "min", "max");
    for (int q = 0; q < CRET_SPECIES; q++) if (t.eps[q])
        fprintf(stderr, "  %-19s %6ld %7ld %12.0f %9.3f %9.3f %9.3f\n", CRET_SPECIES_TABLE[q].name, t.eps[q], t.deaths[q],
                t.len[q] / t.eps[q], t.ret[q] / t.eps[q], t.rlo[q], t.rhi[q]);
}

// One trainer step of one env: random actions, then puf_step.
static void step_slot(Slot* sl, bool hash) {
    static const int sizes[NUM_ATNS] = ACT_SIZES;
    Env* env = sl->env; int n = env->num_agents;
    for (int i = 0; i < n * NUM_ATNS; i++) sl->act[i] = (float)(xs(&sl->rng) % sizes[i % NUM_ATNS]);
    memset(sl->rew, 0, n * sizeof(float)); memset(sl->term, 0, n * sizeof(float));   // as the trainer clears them
    puf_step(env);
    if (hash) {
        sl->hash = fnv(sl->hash, sl->obs, (size_t)n * OBS_SIZE * sizeof(float));
        sl->hash = fnv(sl->hash, sl->rew, n * sizeof(float)); sl->hash = fnv(sl->hash, sl->term, n * sizeof(float));
    }
    if (sl->st) stats_step(sl->st, env, sl->obs, sl->rew, sl->term);
}

int main(int argc, char** argv) {
    int threads[16] = {1, 2, 4}, nthr = 3, envs_per = 8, agents = 0, warmup = 300, ticks = 0, world_n = 0, max_ticks = 0;
    double seconds = 10; float world_cell = 0; bool critters = false, hash = false, stats = false;
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
        else if (!strcmp(a, "--hash")) hash = true;
        else if (!strcmp(a, "--stats")) stats = true;
        else {
            fprintf(stderr, "usage: rlbench [--threads 1,2,4] [--envs N] [--agents N] [--seconds S | --ticks T] [--warmup T]\n"
                            "               [--world-n N --world-cell C] [--max-ticks T] [--critters] [--hash] [--stats]\n");
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
    fprintf(stderr, "rlbench: world %d^2 at %g m, %d agents per env, %d envs per thread, episodes of %d ticks%s\n",
            (int)dict_get(kw, "world_n"), dict_get(kw, "world_cell"), na, envs_per, (int)dict_get(kw, "max_ticks"),
            critters ? ", every slot a person (critters on)" : "");
    fprintf(stderr, "  %7s %5s %12s %14s %14s %10s %10s\n", "threads", "envs", "ticks/env", "steps/s total", "steps/s/core", "us/step", "cpu us");
    for (int t = 0; t < nthr; t++) {
        int nt = threads[t], ne = nt * envs_per;
        Slot* sl = calloc(ne, sizeof(Slot));
        for (int e = 0; e < ne; e++) {
            make_slot(&sl[e], kw, 1000 + e, critters);
            if (stats) { sl[e].st = malloc(sizeof(Stats)); stats_init(sl[e].st); }
        }
        #pragma omp parallel for schedule(static) num_threads(nt)
        for (int e = 0; e < ne; e++) for (int k = 0; k < warmup; k++) step_slot(&sl[e], hash);
        // Timed: the trainer steps every env once per horizon step, all threads in step.
        long done = 0; double t0 = wall(), c0 = clock_s(CLOCK_PROCESS_CPUTIME_ID), el = 0;
        for (;;) {
            #pragma omp parallel for schedule(static) num_threads(nt)
            for (int e = 0; e < ne; e++) step_slot(&sl[e], hash);
            done++; el = wall() - t0;
            if (ticks ? done >= ticks : el >= seconds) break;
        }
        double sps = (double)done * ne * na / el, cpu = clock_s(CLOCK_PROCESS_CPUTIME_ID) - c0;
        fprintf(stderr, "  %7d %5d %12ld %14.0f %14.0f %10.2f %10.2f\n", nt, ne, done, sps, sps / nt, 1e6 * nt / sps, 1e6 * cpu / ((double)done * ne * na));
        if (hash) { uint64_t h = 14695981039346656037ull; for (int e = 0; e < ne; e++) h = fnv(h, &sl[e].hash, sizeof sl[e].hash); fprintf(stderr, "  hash %016llx\n", (unsigned long long)h); }
        if (stats) stats_print(sl, ne);
        for (int e = 0; e < ne; e++) free_slot(&sl[e]);
        free(sl);
    }
    return 0;
}
