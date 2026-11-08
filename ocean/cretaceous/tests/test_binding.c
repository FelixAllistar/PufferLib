// Binding contract (cretaceous.h, the PufferLib env): sizes against the
// config and README, action decoding, 20k random-action steps with no NaN or
// Inf, every observation feature inside its documented range (with an
// obs-stats table), rewards equal to the sum of their terms, and episode
// termination, reset and respawn.
//   test_binding [--steps N] [--quiet]
#include "test_util.h"
#include "cretaceous.h"
#include "reward_terms.h"

// ------------------------------------------------------------------ obs layout
// One row per feature: name, documented range [lo, hi], and whether the
// value is a 0/1 flag. Patch and neighbour rows repeat (see obs_feature).
// Keep in sync with cret_sim_observe (sim.c); a layout change that is not
// mirrored here shows up as range failures or as a size mismatch.
typedef struct Feat { const char* name; float lo, hi; bool flag; } Feat;
static const Feat SELF[CRET_SELF_OBS] = {
    {"heading cos", -1, 1, false}, {"heading sin", -1, 1, false}, {"vel forward /10", -2, 2, false}, {"vel lateral /10", -2, 2, false}, {"vel up /10", -4, 4, false},
    {"pos x", -1, 1, false}, {"pos z", -1, 1, false}, {"ground /500", -1, 2, false}, {"water depth /5", -1, 2, false}, {"swimming", 0, 1, true},
    {"on ground", 0, 1, true}, {"stamina", 0, 1, false}, {"normal x", -1, 1, false}, {"normal z", -1, 1, false}, {"day sin", -1, 1, false}, {"day cos", -1, 1, false},
    {"sp raptor", 0, 1, true}, {"sp stego", 0, 1, true}, {"sp trex", 0, 1, true}, {"sp pachy", 0, 1, true}, {"sp sauropelta", 0, 1, true}, {"sp galli", 0, 1, true},
    {"alive", 0, 1, true}, {"health", 0, 1, false}, {"food", 0, 1, false}, {"water", 0, 1, false}, {"growth", 0, 1, false}, {"bleeding", 0, 2, false},
    {"attack ready", 0, 1, true}, {"forage here", 0, 1, false}, {"carcass seen", 0, 1, true}, {"carcass fwd", -1, 1, false}, {"carcass side", -1, 1, false},
    {"hit flinch", 0, 2, false}, {"broken bone", 0, 1.5f, false}, {"group size /8", 0, 1, false}, {"rally seen", 0, 1, true}, {"rally fwd", -2, 2, false}, {"rally side", -2, 2, false}};
static const Feat PATCH[2] = {{"patch height", -2, 2, false}, {"patch water", -1, 2, false}};
static const Feat NEIGH[CRET_NEIGHBOR_OBS] = {
    {"nb present", 0, 1, true}, {"nb fwd", -2, 2, false}, {"nb side", -2, 2, false}, {"nb up", -2, 2, false}, {"nb heading cos", -1, 1, false},
    {"nb carnivore", 0, 1, true}, {"nb log mass", -2, 2, false}, {"nb health", 0, 1, false}, {"nb same group", 0, 1, true}, {"nb same species", 0, 1, true}};
// Contract 4, appended after the contract 3 layout: status, abilities, quest, nest; then per neighbour.
static const Feat SELF4[CRET_SELF_OBS4] = {
    {"stunned /2s", 0, 1, false}, {"afraid /6s", 0, 1, false}, {"slowed", 0, 1, false}, {"latchers /3", 0, 1, false}, {"latching", 0, 1, false},
    {"defensive armour", 0, 2, false}, {"marked", 0, 1, true}, {"burst /4s", 0, 1, false}, {"evading", 0, 1, true},
    {"ability 1 cooldown", 0, 1, false}, {"ability 2 cooldown", 0, 1, false}, {"ability 3 cooldown", 0, 1, false},
    {"ability 1 ready", 0, 1, true}, {"ability 2 ready", 0, 1, true}, {"ability 3 ready", 0, 1, true},
    {"quest active", 0, 1, true}, {"quest fwd", -2, 2, false}, {"quest side", -2, 2, false}, {"quest progress", 0, 1, false},
    {"nest", 0, 1, true}, {"nest fwd", -2, 2, false}, {"nest side", -2, 2, false}};
static const Feat NEIGH4[CRET_NEIGHBOR_OBS4] = {{"nb stunned", 0, 1, true}, {"nb afraid", 0, 1, true}, {"nb latched", 0, 1, true}, {"nb marked", 0, 1, true}};
#define R_PATCH CRET_SELF_OBS
#define R_NEIGH (R_PATCH + 2)
#define R_SELF4 (R_NEIGH + CRET_NEIGHBOR_OBS)
#define R_NEIGH4 (R_SELF4 + CRET_SELF_OBS4)
#define N_ROWS (R_NEIGH4 + CRET_NEIGHBOR_OBS4)
// Feature row of obs index k.
static int obs_row(int k) {
    if (k < CRET_SELF_OBS) return k;
    k -= CRET_SELF_OBS;
    if (k < CRET_PATCH * CRET_PATCH * 2) return R_PATCH + (k & 1);
    k -= CRET_PATCH * CRET_PATCH * 2;
    if (k < CRET_NEIGHBORS * CRET_NEIGHBOR_OBS) return R_NEIGH + k % CRET_NEIGHBOR_OBS;
    k -= CRET_NEIGHBORS * CRET_NEIGHBOR_OBS;
    if (k < CRET_SELF_OBS4) return R_SELF4 + k;
    k -= CRET_SELF_OBS4;
    if (k < CRET_NEIGHBORS * CRET_NEIGHBOR_OBS4) return R_NEIGH4 + k % CRET_NEIGHBOR_OBS4;
    return -1;
}
static const Feat* row_feat(int r) {
    return r < R_PATCH ? &SELF[r] : r < R_NEIGH ? &PATCH[r - R_PATCH] : r < R_SELF4 ? &NEIGH[r - R_NEIGH] : r < R_NEIGH4 ? &SELF4[r - R_SELF4] : &NEIGH4[r - R_NEIGH4];
}
typedef struct Stat { double sum; float lo, hi; long n, out; float worst; } Stat;

// ------------------------------------------------------------------ env setup
#define GUARD 16
#define GUARD_VAL -12345.0f
typedef struct Bufs { float *obs, *act, *rew, *term; unsigned char* mask; } Bufs;
static Env* make_env(Dict* kw, Bufs* b, unsigned int seed) {
    Env* env = calloc(1, sizeof(Env)); env->rng = seed;
    puf_init(env, kw);
    int n = env->num_agents;
    // Each agent's obs is followed by guard floats: an overrun shows up as a changed guard.
    b->obs = malloc(sizeof(float) * n * (OBS_SIZE + GUARD)); b->act = calloc(n * NUM_ATNS, sizeof(float));
    b->rew = calloc(n, sizeof(float)); b->term = calloc(n, sizeof(float));
    for (int i = 0; i < n * (OBS_SIZE + GUARD); i++) b->obs[i] = GUARD_VAL;
    for (int i = 0; i < n; i++) {
        env->agents[i].observations = b->obs + i * (OBS_SIZE + GUARD); env->agents[i].actions = b->act + i * NUM_ATNS;
        env->agents[i].rewards = b->rew + i; env->agents[i].terminals = b->term + i;
    }
    puf_reset(env);
    return env;
}
static void free_env(Env* env, Bufs* b) { puf_close(env); free(env); free(b->obs); free(b->act); free(b->rew); free(b->term); }

// [env] from config/cretaceous.ini with the tiny test world.
static Ini g_ini;
static Dict* test_kwargs(int max_ticks) {
    Dict* kw = puf_ini_section(&g_ini, "env", 0);
    CretConfig c = t_config(16);
    dict_set(kw, "world_n", c.world_n); dict_set(kw, "world_cell", c.world_cell); dict_set(kw, "world_seed", c.world_seed);
    dict_set(kw, "max_ticks", max_ticks);
    return kw;
}

// ------------------------------------------------------------------ checks
static bool file_has(const char* path, const char* needle) {
    FILE* f = fopen(path, "rb"); if (!f) return false;
    static char buf[1 << 20]; size_t n = fread(buf, 1, sizeof buf - 1, f); fclose(f); buf[n] = 0;
    return strstr(buf, needle) != NULL;
}

static void test_contract(void) {
    int sizes[] = CRET_ACTION_SIZES, nheads = sizeof sizes / sizeof sizes[0];
    CHECK(OBS_SIZE == CRET_OBS_SIZE, "OBS_SIZE %d != CRET_OBS_SIZE %d", OBS_SIZE, CRET_OBS_SIZE);
    CHECK(CRET_OBS_SIZE == CRET_SELF_OBS + CRET_PATCH * CRET_PATCH * 2 + CRET_NEIGHBORS * CRET_NEIGHBOR_OBS + CRET_SELF_OBS4 + CRET_NEIGHBORS * CRET_NEIGHBOR_OBS4, "obs size formula");
    CHECK(NUM_ATNS == CRET_ACTION_HEADS && nheads == CRET_ACTION_HEADS, "action heads: NUM_ATNS %d, CRET_ACTION_HEADS %d, sizes %d", NUM_ATNS, CRET_ACTION_HEADS, nheads);
    // The config and README state the contract; they must not drift from sim.h.
    char want[128] = "{", num[16];
    for (int h = 0; h < nheads; h++) { snprintf(num, sizeof num, "%s%d", h ? ", " : "", sizes[h]); strcat(want, num); }
    strcat(want, "}");
    CHECK(file_has("config/cretaceous.ini", want), "config/cretaceous.ini does not state action heads %s", want);
    snprintf(num, sizeof num, "%d obs floats", CRET_OBS_SIZE);
    CHECK(file_has("config/cretaceous.ini", num), "config/cretaceous.ini does not state '%s'", num);
    snprintf(num, sizeof num, "%d floats", CRET_OBS_SIZE);
    CHECK(file_has("ocean/cretaceous/README.md", num), "README.md does not state '%s'", num);
    char contract[48]; snprintf(contract, sizeof contract, "contract %d", CRET_CONTRACT_VERSION);
    CHECK(file_has("ocean/cretaceous/README.md", contract) && file_has("config/cretaceous.ini", contract), "README/config do not name policy %s", contract);
    int nfeat = 0; for (int k = 0; k < CRET_OBS_SIZE; k++) nfeat += obs_row(k) >= 0;
    CHECK(nfeat == CRET_OBS_SIZE, "obs layout table");

    // Every action combination decodes to a bounded input; each head matters.
    int combos = 1; for (int h = 0; h < nheads; h++) combos *= sizes[h];
    for (int c = 0; c < combos; c++) {
        float act[CRET_ACTION_HEADS]; int r = c;
        for (int h = 0; h < nheads; h++) { act[h] = (float)(r % sizes[h]); r /= sizes[h]; }
        CretInput in = cret_decode_action(act);
        CHECK_CAPPED(fabsf(in.forward) <= 1 && fabsf(in.turn) <= 1 && fabsf(in.strafe) <= 1 && in.action <= CRET_ACT_MAX && in.ability <= CRET_AB_SLOTS && in.sprint <= 1 && in.jump <= 1 && fabsf(in.aim) <= 1,
                     3, "action combo %d decodes out of range", c);
    }
    for (int h = 0; h < nheads; h++) {
        float a0[CRET_ACTION_HEADS] = {1, 2, 1, 0, 0, 2}, a1[CRET_ACTION_HEADS] = {1, 2, 1, 0, 0, 2};
        a1[h] = (float)(sizes[h] - 1);
        CretInput i0 = cret_decode_action(a0), i1 = cret_decode_action(a1);
        CHECK(memcmp(&i0, &i1, sizeof i0) != 0, "action head %d has no effect", h);
    }
    // Out-of-range indices clamp instead of reading past the tables.
    float bad[CRET_ACTION_HEADS] = {-3, 99, -1, 7, 1000, -40};
    CretInput ib = cret_decode_action(bad);
    CHECK(ib.action <= CRET_ACT_MAX && ib.ability <= CRET_AB_SLOTS && fabsf(ib.forward) <= 1 && ib.aim == -1, "out-of-range action indices");
    // Contract 5: action 7 is the secondary attack, the aim head maps to -1, -0.5, 0, 0.5, 1.
    float a2[CRET_ACTION_HEADS] = {3, 2, 1, 0, 7, 4}; CretInput i2 = cret_decode_action(a2);
    CHECK(i2.action == CRET_ACT_ATTACK2 && i2.ability == 0 && i2.aim == 1 && i2.forward == 1, "action 7 / aim 4 decode to %d / %g", i2.action, i2.aim);
    for (int b = 0; b < 5; b++) { float am[CRET_ACTION_HEADS] = {1, 2, 1, 0, 1, (float)b}; CHECK(cret_decode_action(am).aim == -1 + 0.5f * b, "aim bin %d", b); }
    // The no-op action (throttle 0, straight, no strafe, no jump, no action) stands still.
    float noop[CRET_ACTION_HEADS] = {1, 2, 1, 0, 0, 2}; CretInput in0 = cret_decode_action(noop);
    CHECK(in0.forward == 0 && in0.turn == 0 && in0.strafe == 0 && !in0.sprint && !in0.jump && in0.action == CRET_ACT_NONE && in0.ability == 0 && in0.aim == 0, "no-op action is not a no-op");
}

// Random actions for `steps` env steps; checks values, ranges, rewards and episode ends.
static void test_rollout(int steps, bool quiet) {
    const int max_ticks = 2000;
    Bufs b; Env* env = make_env(test_kwargs(max_ticks), &b, 5);
    int n = env->num_agents; CretSim* s = env->sim;
    CHECK(n == (int)dict_get(puf_ini_section(&g_ini, "env", 0), "num_agents"), "num_agents %d != config", n);
    static Stat st[N_ROWS]; for (int r = 0; r < N_ROWS; r++) st[r] = (Stat){0, 1e30f, -1e30f, 0, 0, 0};
    uint32_t rng = 2024; long deaths = 0, terms = 0, episodes = 0; double rsum = 0, rmax = 0;
    float growth0[CRET_MAX_AGENTS]; float last_tick = 0;
    for (int t = 0; t < steps; t++) {
        for (int i = 0; i < n; i++) t_random_actions(&rng, env->agents[i].actions);
        for (int i = 0; i < n; i++) growth0[i] = s->agents[i].growth;
        int tick0 = s->tick;
        puf_step(env);
        bool ended = s->tick == 0;   // the binding reset after max_ticks
        if (ended) {
            episodes++;
            CHECK(tick0 + 1 == max_ticks, "episode ended at tick %d, max_ticks %d", tick0 + 1, max_ticks);
        }
        for (int i = 0; i < n; i++) {
            float r = b.rew[i], term = b.term[i];
            CHECK_CAPPED(isfinite(r) && fabsf(r) < 20, 5, "step %d agent %d: reward %g", t, i, r);
            CHECK_CAPPED(term == 0 || term == 1, 5, "step %d agent %d: terminal %g", t, i, term);
            terms += term > 0; rsum += r; if (fabsf(r) > rmax) rmax = fabsf(r);
            if (!ended) {
                const CretAgent* a = &s->agents[i];
                deaths += a->ev_died;
                CHECK_CAPPED((term > 0) == (a->ev_died != 0), 5, "step %d agent %d: terminal %g but died %d", t, i, term, a->ev_died);
                // The reward is exactly the sum of its terms (tests/reward_terms.h, used by the eval).
                float tt[RT_COUNT]; cret_reward_terms(&env->w, a, growth0[i], tt);
                float sum = cret_reward_sum(tt);
                bool same = fabsf(sum - r) <= 1e-5f * fmaxf(1, fabsf(r));
                CHECK_CAPPED(same, 3, "step %d agent %d: reward %.7f but its terms sum to %.7f (reward_terms.h out of date?)", t, i, r, sum);
            } else {
                CHECK_CAPPED(term == 1, 3, "step %d agent %d: episode end is not terminal", t, i);
            }
            const float* o = env->agents[i].observations;
            for (int g = 0; g < GUARD; g++) CHECK_CAPPED(o[OBS_SIZE + g] == GUARD_VAL, 3, "agent %d: obs write past OBS_SIZE (float %d)", i, OBS_SIZE + g);
            float onehot = 0;
            for (int k = 0; k < OBS_SIZE; k++) {
                float v = o[k]; int rr = obs_row(k); const Feat* f = row_feat(rr); Stat* q = &st[rr];
                CHECK_CAPPED(isfinite(v), 5, "step %d agent %d: obs[%d] (%s) = %g", t, i, k, f->name, v);
                if (!isfinite(v)) continue;
                if (k >= 16 && k < 22) onehot += v;
                q->sum += v; q->n++; if (v < q->lo) q->lo = v; if (v > q->hi) q->hi = v;
                bool in = v >= f->lo - 1e-4f && v <= f->hi + 1e-4f && (!f->flag || v == 0 || v == 1);
                if (!in) { q->out++; if (fabsf(v) > fabsf(q->worst)) q->worst = v; }
            }
            CHECK_CAPPED(fabsf(onehot - 1) < 1e-6f, 3, "step %d agent %d: species one-hot sums to %g", t, i, onehot);
        }
        last_tick = s->tick;
    }
    (void)last_tick;
    // Obs-stats table.
    if (!quiet) {
        fprintf(stderr, "\nobs stats over %d steps x %d agents (range = documented range; * = outside it; const = never changed)\n", steps, n);
        fprintf(stderr, "  %-18s %9s %9s %9s   %-12s %s\n", "feature", "min", "max", "mean", "range", "");
    }
    for (int r = 0; r < N_ROWS; r++) {
        const Feat* f = row_feat(r); Stat* q = &st[r];
        if (!quiet || q->out)
            fprintf(stderr, "  %-18s %9.3f %9.3f %9.3f   [%g, %g]%s%s\n", f->name, q->lo, q->hi, q->n ? q->sum / q->n : 0, f->lo, f->hi,
                    q->out ? " *" : "", q->lo == q->hi ? " const" : "");
        CHECK(q->out == 0, "obs feature '%s' left its range [%g, %g] %ld times (worst %g)", f->name, f->lo, f->hi, q->out, q->worst);
    }
    fprintf(stderr, "rollout: %d steps, %ld episodes, %ld deaths, %ld terminals, mean reward %.4f, max |reward| %.3f\n", steps, episodes, deaths, terms, rsum / ((double)steps * n), rmax);
    CHECK(episodes == steps / max_ticks, "%ld episodes in %d steps of max_ticks %d", episodes, steps, max_ticks);
    CHECK(env->log.n == (float)(episodes * n), "log.n %g after %ld episodes of %d agents", env->log.n, episodes, n);
    CHECK(episodes == 0 || fabsf(env->log.episode_length / env->log.n - max_ticks) < 1e-3f, "log episode_length %g", env->log.episode_length / fmaxf(env->log.n, 1));
    CHECK(env->log.perf >= 0 && env->log.perf <= env->log.n, "log perf %g of n %g", env->log.perf, env->log.n);
    Dict out = {0}; puf_log(&env->log, &out);
    const char* keys[] = {"perf", "score", "episode_return", "episode_length", "n", "deaths", "kills", "food", "water"};
    for (size_t k = 0; k < sizeof keys / sizeof keys[0]; k++) CHECK(dict_find(&out, keys[k]) != NULL, "puf_log lacks '%s'", keys[k]);
    dict_clear(&out);
    free_env(env, &b);
}

// Deaths are terminal, the next step respawns a fresh animal; episodes reset.
static void test_death_and_respawn(void) {
    Bufs b; Env* env = make_env(test_kwargs(300), &b, 9); CretSim* s = env->sim; int n = env->num_agents;
    float noop[CRET_ACTION_HEADS] = {1, 2, 1, 0, 0};
    for (int i = 0; i < n; i++) memcpy(env->agents[i].actions, noop, sizeof noop);
    for (int t = 0; t < 10; t++) puf_step(env);
    CretAgent* a = &s->agents[3]; int sp = a->species;
    a->health = 0.001f; a->bleed = a->max_health;   // bleeds out this tick
    puf_step(env);
    CHECK(!a->alive && a->ev_died && b.term[3] == 1, "a bled-out agent is not dead and terminal (alive %d, term %g)", a->alive, b.term[3]);
    CHECK(b.rew[3] <= -env->w.death + 1e-4f, "death reward %g, expected about -%g", b.rew[3], env->w.death);
    CHECK(env->agents[3].observations[22] == 0, "dead agent observes alive = %g", env->agents[3].observations[22]);
    for (int i = 0; i < n; i++) if (i != 3) CHECK_CAPPED(b.term[i] == 0, 3, "agent %d terminal without dying", i);
    puf_step(env);   // respawn_ticks = 1
    CHECK(a->alive && b.term[3] == 0, "agent did not respawn the next step (alive %d)", a->alive);
    CHECK(a->health == a->max_health && a->ticks_alive == 1 && a->species == sp, "respawned agent is not fresh (health %.1f/%.1f, ticks %d)", a->health, a->max_health, a->ticks_alive);
    CHECK(env->agents[3].observations[22] == 1, "respawned agent observes alive = %g", env->agents[3].observations[22]);
    // Episode end: every agent terminal on the last tick, the sim resets.
    while (s->tick != 299) puf_step(env);
    puf_step(env);
    int all = 0; for (int i = 0; i < n; i++) all += b.term[i] == 1;
    CHECK(all == n && s->tick == 0, "episode end: %d of %d terminal, tick %d", all, n, s->tick);
    for (int i = 0; i < n; i++) CHECK_CAPPED(s->agents[i].alive && s->agents[i].ticks_alive == 0, 3, "agent %d not fresh after the reset", i);
    CHECK(env->log.n == n, "log.n %g after one episode", env->log.n);
    free_env(env, &b);
}

int main(int argc, char** argv) {
    double t0 = t_now(); int steps = 20000; bool quiet = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--steps") && i + 1 < argc) steps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--quiet")) quiet = true;
    }
    t_hold_world();
    puf_ini_load_file(&g_ini, "config/cretaceous.ini");
    test_contract();
    test_death_and_respawn();
    test_rollout(steps, quiet);
    return t_finish("test_binding", t0);
}
