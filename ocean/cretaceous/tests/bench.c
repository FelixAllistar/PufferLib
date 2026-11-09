// Sim tick microbenchmark: what one trainer step costs (decode random
// actions, cret_sim_step, cret_sim_observe for every agent) at 16, 64 and 256
// agents, plus the scripted bots (a server's cost). CRET_MAX_AGENTS is 64,
// so 256 agents are four 64-agent envs stepped on one thread, the way a
// trainer thread steps its envs.
//   bench                    print the table
//   bench --write FILE       store it as the baseline
//   bench --check FILE       fail if a case regressed past the baseline x tolerance
// Times are per-thread CPU time, the best of several short repeats (the
// repeat least disturbed by other work). Tolerance: the file's `tolerance`
// line, or CRET_BENCH_TOL. On a machine whose load average is above 1.5x
// its cores, a slow case is reported but does not fail (CRET_BENCH_STRICT=1
// fails anyway); CRET_BENCH_SKIP=1 never fails. A normalised score against a
// calibration loop was tried and was noisier than raw CPU time on a loaded box.
#include "test_util.h"
#include <unistd.h>

static double cpu_now(void) { struct timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

typedef struct Case { const char* name; int envs, agents; int bots; double ms; } Case;
static Case CASES[] = {
    {"step_16", 1, 16, 0, 0}, {"step_64", 1, 64, 0, 0}, {"step_256", 4, 64, 0, 0},
    {"bots_16", 1, 16, 1, 0}, {"bots_64", 1, 64, 1, 0}, {"step_128", 1, 128, 0, 0}, {"bots_128", 1, 128, 1, 0},
};
#define NCASES (int)(sizeof CASES / sizeof CASES[0])

static void run_case(Case* c, int warmup, int ticks, int reps) {
    CretSim* sims[4]; static float obs[CRET_OBS_SIZE]; uint32_t rng = 31;
    for (int e = 0; e < c->envs; e++) {
        sims[e] = calloc(1, sizeof(CretSim)); CretConfig cfg = t_config(c->agents); cfg.seed = 100 + e;
        if (cret_sim_init(sims[e], cfg) != 0) exit(2);
    }
    double best = 1e30;
    for (int r = 0; r < reps; r++) {
        double t0 = 0;
        for (int t = 0; t < warmup + ticks; t++) {
            if (t == warmup) t0 = cpu_now();
            for (int e = 0; e < c->envs; e++) {
                CretSim* s = sims[e]; CretInput in[CRET_MAX_AGENTS];
                for (int i = 0; i < c->agents; i++) {
                    if (c->bots) in[i] = cret_bot_input(s, i);
                    else { float act[CRET_ACTION_HEADS]; t_random_actions(&rng, act); in[i] = cret_decode_action(act); }
                }
                cret_sim_step(s, in);
                if (!c->bots) for (int i = 0; i < c->agents; i++) cret_sim_observe(s, i, obs);
            }
        }
        double ms = (cpu_now() - t0) * 1000.0 / ticks;
        if (ms < best) best = ms;
        warmup = 0;   // later repeats continue the same run
    }
    for (int e = 0; e < c->envs; e++) { cret_sim_close(sims[e]); free(sims[e]); }
    c->ms = best;
}

static int load_baseline(const char* path, double* base, double* tol) {
    FILE* f = fopen(path, "r"); if (!f) return -1;
    char line[256], name[64]; double v;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || sscanf(line, "%63s %lf", name, &v) != 2) continue;
        if (!strcmp(name, "tolerance")) *tol = v;
        for (int k = 0; k < NCASES; k++) if (!strcmp(name, CASES[k].name)) base[k] = v;
    }
    fclose(f); return 0;
}
static double load_avg(void) { double l = 0; FILE* f = fopen("/proc/loadavg", "r"); if (f) { if (fscanf(f, "%lf", &l) != 1) l = 0; fclose(f); } return l; }

int main(int argc, char** argv) {
    double t0 = t_now(); const char *write = NULL, *check = NULL;
    for (int i = 1; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "--write")) write = argv[++i];
        else if (!strcmp(argv[i], "--check")) check = argv[++i];
    }
    t_hold_world();
    int ticks = t_env_int("CRET_BENCH_TICKS", 250), reps = t_env_int("CRET_BENCH_REPS", 6);
    double base[NCASES] = {0}, tol = 2.5;
    if (check && load_baseline(check, base, &tol) != 0) { fprintf(stderr, "bench: no baseline at %s (make bench-baseline)\n", check); check = NULL; }
    if (getenv("CRET_BENCH_TOL")) tol = atof(getenv("CRET_BENCH_TOL"));
    long cores = sysconf(_SC_NPROCESSORS_ONLN); double load = load_avg();
    bool busy = load > 1.5 * cores && !(getenv("CRET_BENCH_STRICT") && atoi(getenv("CRET_BENCH_STRICT")));
    bool skip = (getenv("CRET_BENCH_SKIP") && atoi(getenv("CRET_BENCH_SKIP"))) || busy;
    fprintf(stderr, "bench: CPU ms per tick, best of %d x %d ticks (step = random actions + sim + observe; bots = cret_bot_input + sim), tolerance %.1fx\n", reps, ticks, tol);
    if (busy) fprintf(stderr, "  load average %.1f on %ld cores: slow cases are reported, not failed (CRET_BENCH_STRICT=1 to fail)\n", load, cores);
    fprintf(stderr, "  %-10s %8s %8s %9s\n", "case", "ms/tick", "baseline", "us/agent");
    for (int k = 0; k < NCASES; k++) {
        Case* c = &CASES[k];
        run_case(c, 300, ticks, reps);
        const char* verdict = "";
        if (check && base[k] > 0) {
            if (c->ms > base[k] * tol) verdict = skip ? "SLOW (not failed)" : "SLOW";
            else if (c->ms < base[k] / tol) verdict = "faster (make bench-baseline?)";
            if (!skip) CHECK(c->ms <= base[k] * tol, "%s: %.3f ms/tick is over %.1fx the baseline %.3f", c->name, c->ms, tol, base[k]);
        }
        fprintf(stderr, "  %-10s %8.3f %8.3f %9.2f  %s\n", c->name, c->ms, base[k], 1000.0 * c->ms / (c->envs * c->agents), verdict);
    }
    if (write) {
        FILE* f = fopen(write, "w");
        if (!f) { fprintf(stderr, "bench: cannot write %s\n", write); return 1; }
        CretConfig tc = t_config(1);
        fprintf(f, "# make bench-baseline (tests/bench.c): CPU ms per tick, best of %d x %d ticks; load average %.1f on %ld cores.\n", reps, ticks, load, cores);
        fprintf(f, "# make test fails above baseline x tolerance (not on a busy machine). World %d^2, cell %g, seed %u.\n", tc.world_n, tc.world_cell, tc.world_seed);
        fprintf(f, "tolerance %.1f\n", tol);
        for (int k = 0; k < NCASES; k++) fprintf(f, "%s %.4f\n", CASES[k].name, CASES[k].ms);
        fclose(f); fprintf(stderr, "bench: wrote %s\n", write);
    }
    return t_finish("bench", t0);
}
