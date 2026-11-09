// Sim tests: determinism (same seed and inputs give bit-identical state, also
// with sims stepped on threads and with the bake on 1 or 4 OpenMP threads),
// state invariants every tick, physics sanity (falls, hop, mass, turning,
// client prediction, swimming, prop colliders) and combat (aimed strike points, attacking on the move,
// secondary attacks per species, the attack buffer).
//   test_sim                 the fast suite (make test)
//   test_sim --soak N        N ticks of 32 bots with invariants (make asan)
#include "test_util.h"
#include <float.h>
#include <pthread.h>

// ------------------------------------------------------------------ invariants
// Checked on every tick of every run. swim_streak (per agent, may be NULL)
// counts consecutive swimming ticks: the float settles within a few seconds.
typedef struct Counters { long deaths, kills, hits, respawns, drinks, eats, swims, calls, attacks; } Counters;

static void check_invariants(const CretSim* s, int* swim_streak, Counters* c, const bool* was_alive) {
    const CretWorld* w = s->world; float S = w->size;
    for (int i = 0; i < s->cfg.num_agents; i++) {
        const CretAgent* a = &s->agents[i];
        if (c) {
            c->deaths += a->ev_died; c->kills += a->ev_kills; c->hits += a->ev_dealt > 0; c->drinks += a->ev_water > 0; c->eats += a->ev_food > 0;
            c->swims += a->swimming; c->calls += a->act == CRET_ACT_CALL && a->act_ticks == 0; c->attacks += a->act == CRET_ACT_ATTACK && a->act_ticks == 0;
            if (was_alive && !was_alive[i] && a->alive) c->respawns++;
        }
        if (!a->alive) { if (swim_streak) swim_streak[i] = 0; continue; }
        bool finite = isfinite(a->x) && isfinite(a->y) && isfinite(a->z) && isfinite(a->vx) && isfinite(a->vy) && isfinite(a->vz) && isfinite(a->yaw) &&
                      isfinite(a->health) && isfinite(a->food) && isfinite(a->water) && isfinite(a->growth) && isfinite(a->stamina) && isfinite(a->bleed);
        CHECK_CAPPED(finite, 5, "tick %d agent %d: non-finite state", s->tick, i);
        if (!finite) continue;
        float g = cret_height(w, a->x, a->z), wl = cret_water(w, a->x, a->z);
        CHECK_CAPPED(a->y >= g - 1.0f, 5, "tick %d agent %d (%s): y %.2f below ground %.2f - 1", s->tick, i, CRET_SPECIES_TABLE[a->species].name, a->y, g);
        CHECK_CAPPED(a->x >= 0 && a->z >= 0 && a->x <= S && a->z <= S, 5, "tick %d agent %d: off the map (%.1f, %.1f)", s->tick, i, a->x, a->z);
        CHECK_CAPPED(a->health >= 0 && a->health <= a->max_health * 1.0001f + 1e-3f, 5, "tick %d agent %d: health %.3f of %.3f", s->tick, i, a->health, a->max_health);
        CHECK_CAPPED(a->food >= 0 && a->food <= 1.0001f && a->water >= 0 && a->water <= 1.0001f, 5, "tick %d agent %d: food %.3f water %.3f", s->tick, i, a->food, a->water);
        CHECK_CAPPED(a->growth >= 0.35f - 1e-4f && a->growth <= 1.0001f, 5, "tick %d agent %d: growth %.3f", s->tick, i, a->growth);
        CHECK_CAPPED(a->stamina >= 0 && a->stamina <= 1.0001f, 5, "tick %d agent %d: stamina %.3f", s->tick, i, a->stamina);
        CHECK_CAPPED(a->species < CRET_SPECIES, 5, "tick %d agent %d: species %d", s->tick, i, a->species);
        CHECK_CAPPED(a->act <= CRET_ACT_MAX && isfinite(a->aim) && fabsf(a->aim) <= 1, 5, "tick %d agent %d: act %d", s->tick, i, a->act);
        CHECK_CAPPED(a->bleed >= 0 && a->broken >= 0, 5, "tick %d agent %d: bleed %.3f broken %.3f", s->tick, i, a->bleed, a->broken);
        if (a->on_ground && !a->latch) CHECK_CAPPED(fabsf(a->y - g) < 0.4f, 5, "tick %d agent %d: on_ground but %.2f m from the ground", s->tick, i, a->y - g);
        if (swim_streak) {
            swim_streak[i] = a->swimming ? swim_streak[i] + 1 : 0;
            // Floating: the body rides at the surface (target is water - 0.6 h).
            if (swim_streak[i] > 90)
                CHECK_CAPPED(a->y <= wl + 0.3f && a->y >= wl - a->height - 0.5f, 5, "tick %d agent %d (%s): swimming at y %.2f, water %.2f, height %.2f",
                             s->tick, i, CRET_SPECIES_TABLE[a->species].name, a->y, wl, a->height);
        }
        if (a->group) {
            int cap = CRET_SPECIES_TABLE[a->species].group_max > 1 ? CRET_SPECIES_TABLE[a->species].group_max : 4;
            CHECK_CAPPED(cret_group_size(s, a->group) <= cap, 3, "tick %d agent %d (%s): group %d has %d members (cap %d)", s->tick, i,
                         CRET_SPECIES_TABLE[a->species].name, a->group, cret_group_size(s, a->group), cap);
        }
    }
    for (int k = 0; k < CRET_MAX_CARCASSES; k++) {
        const CretCarcass* cc = &s->carcasses[k]; if (!cc->active) continue;
        CHECK_CAPPED(cc->species < CRET_SPECIES && cc->meat <= cc->max_meat + 1e-3f && isfinite(cc->x) && isfinite(cc->z), 3, "carcass %d invalid", k);
    }
}

// ------------------------------------------------------------------ determinism
// A run mixes policies so combat, deaths and respawns happen inside 5000
// ticks: every third agent acts at random, the rest are scripted bots.
// Hunger and thirst are short so the random animals die and respawn.
#define DET_TICKS 5000
static CretConfig det_config(void) {
    CretConfig c = t_config(32);
    c.seed = 7; c.respawn_ticks = 30; c.hunger_seconds = 120; c.thirst_seconds = 90; c.growth_seconds = 300; c.weather = 1;
    return c;
}
static void det_inputs(CretSim* s, uint32_t* rng, CretInput* in) {
    for (int i = 0; i < s->cfg.num_agents; i++) {
        if (i % 3 == 0) { float act[CRET_ACTION_HEADS]; t_random_actions(rng, act); in[i] = cret_decode_action(act); }
        else in[i] = cret_bot_input(s, i);
    }
}
typedef struct DetRun { CretSim sim; uint32_t rng; uint64_t* trace; Counters c; bool alive[CRET_MAX_AGENTS]; int swim[CRET_MAX_AGENTS]; } DetRun;
static void det_start(DetRun* r) {
    memset(&r->c, 0, sizeof r->c); memset(r->swim, 0, sizeof r->swim);
    if (cret_sim_init(&r->sim, det_config()) != 0) { fprintf(stderr, "sim init failed\n"); exit(2); }
    r->rng = 12345; r->trace = calloc(DET_TICKS, sizeof(uint64_t));
}
static void det_tick(DetRun* r, int t, bool check) {
    CretInput in[CRET_MAX_AGENTS];
    for (int i = 0; i < r->sim.cfg.num_agents; i++) r->alive[i] = r->sim.agents[i].alive;
    det_inputs(&r->sim, &r->rng, in); cret_sim_step(&r->sim, in);
    if (check) check_invariants(&r->sim, r->swim, &r->c, r->alive);
    r->trace[t] = t_sim_hash(&r->sim);
}
static void* det_thread(void* p) { DetRun* r = p; for (int t = 0; t < DET_TICKS; t++) det_tick(r, t, false); return NULL; }

static int first_mismatch(const uint64_t* a, const uint64_t* b, int n) { for (int t = 0; t < n; t++) if (a[t] != b[t]) return t; return -1; }

static void test_determinism(void) {
    fprintf(stderr, "determinism: 2 sims stepped in lockstep, %d ticks, 32 agents\n", DET_TICKS);
    DetRun* A = calloc(1, sizeof *A); DetRun* B = calloc(1, sizeof *B);
    det_start(A); det_start(B);
    CHECK(t_sim_hash(&A->sim) == t_sim_hash(&B->sim), "initial states differ");
    int bad = -1;
    for (int t = 0; t < DET_TICKS; t++) {
        det_tick(A, t, true); det_tick(B, t, false);
        if (bad < 0 && A->trace[t] != B->trace[t]) {
            bad = t;
            for (int i = 0; i < A->sim.cfg.num_agents; i++)
                if (t_agent_hash(T_FNV0, &A->sim.agents[i]) != t_agent_hash(T_FNV0, &B->sim.agents[i])) {
                    char buf[96]; fprintf(stderr, "  first divergence at tick %d, agent %d, field %s\n", t, i, t_agent_diff(&A->sim.agents[i], &B->sim.agents[i], buf, sizeof buf));
                    break;
                }
        }
    }
    CHECK(bad < 0, "lockstep sims diverged at tick %d", bad);
    Counters* c = &A->c;
    fprintf(stderr, "  coverage: %ld deaths, %ld respawns, %ld kills, %ld hits, %ld attacks, %ld calls, %ld drink ticks, %ld eat ticks, %ld swim ticks\n",
            c->deaths, c->respawns, c->kills, c->hits, c->attacks, c->calls, c->drinks, c->eats, c->swims);
    CHECK(c->deaths > 0 && c->respawns > 0, "no deaths or respawns in the determinism run (%ld, %ld)", c->deaths, c->respawns);
    CHECK(c->hits > 0, "no damage dealt in the determinism run");
    CHECK(c->eats > 0, "nobody ate");   // drinking: test_drinking

    // Same run alone after the others (state shared through the world or
    // statics would show here), then four copies stepped on threads at once
    // (sims share one CretWorld, as trainer threads do).
    DetRun* C = calloc(1, sizeof *C); det_start(C);
    for (int t = 0; t < DET_TICKS; t++) det_tick(C, t, false);
    int m = first_mismatch(A->trace, C->trace, DET_TICKS);
    CHECK(m < 0, "a later sequential run diverged from the first at tick %d", m);
    enum { NT = 4 };
    DetRun* T[NT]; pthread_t th[NT];
    for (int k = 0; k < NT; k++) { T[k] = calloc(1, sizeof *T[k]); det_start(T[k]); }   // physics worlds are created on one thread
    double t0 = t_now();
    for (int k = 0; k < NT; k++) pthread_create(&th[k], NULL, det_thread, T[k]);
    for (int k = 0; k < NT; k++) pthread_join(th[k], NULL);
    for (int k = 0; k < NT; k++) {
        m = first_mismatch(A->trace, T[k]->trace, DET_TICKS);
        CHECK(m < 0, "threaded run %d diverged at tick %d", k, m);
    }
    fprintf(stderr, "  sequential and %d threaded copies match (threads took %.1f s)\n", NT, t_now() - t0);
    DetRun* all[] = {A, B, C, T[0], T[1], T[2], T[3]};
    for (int k = 0; k < 7; k++) { cret_sim_close(&all[k]->sim); free(all[k]->trace); free(all[k]); }
}

// The bake is OpenMP-parallel: 1 and N threads must give the same world.
static uint64_t map_hash(const TerraMap* m) {
    size_t n = (size_t)m->n * m->n; uint64_t h = T_FNV0;
    h = t_fnv(h, m->height, n * 4); h = t_fnv(h, m->water, n * 4); h = t_fnv(h, m->flow, n * 4); h = t_fnv(h, m->soil, n * 4);
    h = t_fnv(h, m->water_kind, n); h = t_fnv(h, m->splat, n * TERRA_MAT_COUNT); h = t_fnv(h, m->veg, n * TERRA_VEG_COUNT);
    return h;
}
static void test_bake_threads(void) {
    TerraParams p = terra_default_params(3); p.n = 256; p.cell = 8;
    // 1 against 2 threads (CRET_TEST_BAKE_THREADS): enough to change every loop's split; more
    // threads stall at the bake's many barriers when the machine is oversubscribed.
    uint64_t h[2]; int threads[2] = {1, t_env_int("CRET_TEST_BAKE_THREADS", 2)};
    for (int k = 0; k < 2; k++) {
        TerraMap m; p.threads = threads[k];
        CHECK(terra_generate(&p, &m, NULL, NULL) == 0, "bake failed");
        h[k] = map_hash(&m); terra_free(&m);
    }
    fprintf(stderr, "bake 256^2 on 1 and %d OpenMP threads: %016llx %016llx\n", threads[1], (unsigned long long)h[0], (unsigned long long)h[1]);
    CHECK(h[0] == h[1], "the bake depends on the OpenMP thread count");
}

// ------------------------------------------------------------------ physics
// A flat, dry spot at least `margin` metres from water and steep ground.
static bool find_flat(const CretWorld* w, uint32_t* rng, float margin, float* x, float* z) {
    for (int t = 0; t < 20000; t++) {
        float px = w->size * (0.1f + 0.8f * t_frand(rng)), pz = w->size * (0.1f + 0.8f * t_frand(rng));
        bool ok = true;
        for (int q = 0; q < 16 && ok; q++) {
            float ang = q * 0.3927f, r = margin * (q % 2 ? 1.0f : 0.5f), qx = px + cosf(ang) * r, qz = pz + sinf(ang) * r;
            float n[3]; terra_normal(&w->map, qx, qz, n);
            ok = cret_height(w, qx, qz) > 3 && cret_water(w, qx, qz) < cret_height(w, qx, qz) - 0.5f && n[1] > 0.95f;
        }
        if (ok) { *x = px; *z = pz; return true; }
    }
    return false;
}
// A dry strip `len` metres long from (x, z) along yaw, flatter than n.y >= flat
// (slopes change gait speeds, which the movement checks measure).
static bool find_strip(const CretWorld* w, uint32_t* rng, float len, float flat, float* x, float* z, float* yaw) {
    for (int t = 0; t < 4000; t++) {
        if (!find_flat(w, rng, 6, x, z)) return false;
        *yaw = t_frand(rng) * 6.2832f; bool ok = true;
        for (int k = -1; k <= (int)(len / 5) && ok; k++) {
            float px = *x + cosf(*yaw) * k * 5, pz = *z + sinf(*yaw) * k * 5, n[3]; terra_normal(&w->map, px, pz, n);
            ok = n[1] >= flat && cret_water(w, px, pz) < cret_height(w, px, pz) - 0.5f;
        }
        if (ok) return true;
    }
    return false;
}
static void place(CretSim* s, int i, float x, float y, float z, float yaw) {
    CretAgent* a = &s->agents[i]; a->x = x; a->y = y; a->z = z; a->yaw = yaw; a->vx = a->vy = a->vz = 0; a->on_ground = false;
    b3Body_SetTransform(a->body, (b3Pos){x, y + a->height * 0.5f, z}, b3Quat_identity);
}
// One agent of a species in its own sim, full health, never hungry.
static void solo(CretSim* s, int species) {
    CretConfig c = t_config(1); c.player_species = species; c.player_growth = 1.0f; c.hunger_seconds = c.thirst_seconds = 1e6f;
    if (cret_sim_init(s, c) != 0) exit(2);
}

static void test_falls(void) {
    uint32_t rng = 99; int species[] = {CRET_SP_RAPTOR, CRET_SP_TREX, CRET_SP_GALLI};
    for (int q = 0; q < 3; q++) {
        CretSim* s = calloc(1, sizeof *s); solo(s, species[q]); CretAgent* a = &s->agents[0];
        float x, z; CHECK(find_flat(s->world, &rng, 6, &x, &z), "no flat land in the test world");
        float g = cret_height(s->world, x, z);
        place(s, 0, x, g + 40, z, 0);
        CretInput in = {0}; int landed = -1;
        for (int t = 0; t < 300 && a->alive; t++) {
            cret_sim_step(s, &in); check_invariants(s, NULL, NULL, NULL);
            if (landed < 0 && a->on_ground) landed = t;
        }
        CHECK(landed > 0 && landed < 120, "%s dropped from 40 m: landed at tick %d", CRET_SPECIES_TABLE[a->species].name, landed);
        CHECK(!a->alive || fabsf(a->y - cret_height(s->world, a->x, a->z)) < 0.05f, "%s did not settle on the ground", CRET_SPECIES_TABLE[a->species].name);
        CHECK(!a->alive || a->health < a->max_health * 0.9f, "%s took no fall damage from 40 m (%.0f of %.0f)", CRET_SPECIES_TABLE[a->species].name, a->health, a->max_health);
        // A huge downward speed (10 m in one tick) must still stop at the ground.
        if (!a->alive) cret_sim_respawn(s, 0);
        place(s, 0, x, g + 5, z, 0); a->vy = -300; a->health = a->max_health;
        cret_sim_step(s, &in);
        CHECK(!a->alive || a->y >= cret_height(s->world, a->x, a->z) - 0.01f, "fast fall went through the ground (y %.2f, ground %.2f)", a->y, cret_height(s->world, a->x, a->z));
        cret_sim_close(s); free(s);
    }
}

// Movement feel: the raptor's hop (height and air time), only raptors hop,
// mass (time to reach the sprint, stopping distance) and turning circles that
// widen with speed.
static void test_hop(void) {
    uint32_t rng = 41;
    for (int sp = 0; sp < CRET_SPECIES; sp++) {
        CretSim* s = calloc(1, sizeof *s); solo(s, sp); CretAgent* a = &s->agents[0];
        float x, z; CHECK(find_flat(s->world, &rng, 6, &x, &z), "no flat land in the test world");
        place(s, 0, x, cret_height(s->world, x, z), z, 0);
        CretInput in = {0};
        for (int t = 0; t < 10; t++) cret_sim_step(s, &in);
        float apex = 0; int air = 0;
        in.jump = 1; cret_sim_step(s, &in); in.jump = 0;
        if (!a->on_ground) air = 1;
        for (int t = 0; t < 90 && !a->on_ground; t++) {
            apex = fmaxf(apex, a->y - cret_height(s->world, a->x, a->z));
            cret_sim_step(s, &in); check_invariants(s, NULL, NULL, NULL); air++;
        }
        const char* name = CRET_SPECIES_TABLE[sp].name;
        if (CRET_SPECIES_TABLE[sp].jump > 0) {
            float h = CRET_SPECIES_TABLE[sp].jump, tair = air * CRET_DT;
            fprintf(stderr, "hop: %s %.2f m, %.2f s in the air\n", name, apex, tair);
            CHECK(fabsf(apex - h) < 0.06f, "%s hop peaked at %.2f m, expected %.2f", name, apex, h);
            CHECK(tair > 0.5f && tair < 0.72f, "%s hop lasted %.2f s", name, tair);
            CHECK(a->alive && a->health >= a->max_health * 0.999f, "%s hurt itself hopping", name);
        } else CHECK(air == 0 && apex == 0, "%s hopped (%.2f m)", name, apex);
        cret_sim_close(s); free(s);
    }
}

// Sprint from a standstill in a straight line, then let go: seconds to 90%
// of the top speed reached and metres to stop. Returns false when the run
// left flat ground (slopes change the speeds).
static bool run_and_stop(int species, uint32_t* rng, float* t90, float* stop, float* top) {
    CretSim* s = calloc(1, sizeof *s); solo(s, species); CretAgent* a = &s->agents[0]; const CretWorld* w = s->world;
    float x = 0, z = 0, yaw = 0;
    if (!find_strip(w, rng, 80, 0.97f, &x, &z, &yaw)) { cret_sim_close(s); free(s); return false; }
    place(s, 0, x, cret_height(w, x, z), z, yaw);
    CretInput in = {0};
    for (int t = 0; t < 5; t++) cret_sim_step(s, &in);
    in.forward = 1; in.sprint = 1; *t90 = -1; *top = 0;
    float speeds[150];
    for (int t = 0; t < 150; t++) { cret_sim_step(s, &in); speeds[t] = sqrtf(a->vx * a->vx + a->vz * a->vz); }
    for (int t = 120; t < 150; t++) *top = fmaxf(*top, speeds[t]);
    for (int t = 0; t < 150 && *t90 < 0; t++) if (speeds[t] >= 0.9f * *top) *t90 = (t + 1) * CRET_DT;
    in.forward = 0; in.sprint = 0;
    float x0 = a->x, z0 = a->z; int t = 0;
    for (; t < 150 && a->vx * a->vx + a->vz * a->vz > 0.01f; t++) cret_sim_step(s, &in);
    *stop = sqrtf((a->x - x0) * (a->x - x0) + (a->z - z0) * (a->z - z0));
    bool ok = a->alive && t < 150;
    cret_sim_close(s); free(s);
    return ok;
}
static void test_mass(void) {
    uint32_t rng = 77; float t90[CRET_SPECIES], stop[CRET_SPECIES], top[CRET_SPECIES];
    for (int sp = 0; sp < CRET_SPECIES; sp++) {
        const CretSpecies* S = &CRET_SPECIES_TABLE[sp];
        bool ok = run_and_stop(sp, &rng, &t90[sp], &stop[sp], &top[sp]);
        CHECK(ok, "%s: no flat strip to sprint on, or it never stopped", S->name);
        if (!ok) return;
        fprintf(stderr, "mass: %-12s top %.2f m/s (sprint %.2f), 90%% in %.2f s, stops in %.2f m\n", S->name, top[sp], S->sprint, t90[sp], stop[sp]);
        CHECK(fabsf(top[sp] - S->sprint) < 0.08f * S->sprint, "%s: top speed %.2f, sprint %.2f", S->name, top[sp], S->sprint);
        CHECK(t90[sp] > 0.6f && t90[sp] < 4.0f, "%s: %.2f s to 90%% of the sprint", S->name, t90[sp]);
        // Braking: within half a second for the light animals, about a second for the heavy ones.
        float tstop = 2 * stop[sp] / fmaxf(top[sp], 0.1f);
        CHECK(tstop > 0.3f && tstop < 2.5f, "%s: stops from the sprint in %.2f m (%.2f s)", S->name, stop[sp], tstop);
    }
    CHECK(t90[CRET_SP_RAPTOR] < t90[CRET_SP_TREX] && t90[CRET_SP_GALLI] < t90[CRET_SP_STEGO] && t90[CRET_SP_PACHY] < t90[CRET_SP_TREX],
          "light animals should reach their sprint sooner (raptor %.2f trex %.2f galli %.2f stego %.2f pachy %.2f)",
          t90[CRET_SP_RAPTOR], t90[CRET_SP_TREX], t90[CRET_SP_GALLI], t90[CRET_SP_STEGO], t90[CRET_SP_PACHY]);
}

// Holding a full turn: the turning circle (speed over yaw rate) widens with
// speed, and the body's motion follows the heading (no sideways skid).
static float turn_radius(int species, bool sprint, float* skid) {
    CretSim* s = calloc(1, sizeof *s); solo(s, species); CretAgent* a = &s->agents[0];
    uint32_t rng = 3; float x, z; CHECK(find_flat(s->world, &rng, 6, &x, &z), "no flat land in the test world");
    place(s, 0, x, cret_height(s->world, x, z), z, 0);
    CretInput in = {.forward = sprint ? 1.0f : 0.25f, .sprint = sprint};
    for (int t = 0; t < 90; t++) cret_sim_step(s, &in);
    in.turn = 1;
    for (int t = 0; t < 15; t++) cret_sim_step(s, &in);
    float y0 = a->yaw, d = 0, sk = 0; int n = 20;
    for (int t = 0; t < n; t++) {
        float sp = sqrtf(a->vx * a->vx + a->vz * a->vz) + 1e-6f;
        sk = fmaxf(sk, fabsf(-a->vx * sinf(a->yaw) + a->vz * cosf(a->yaw)) / sp);
        cret_sim_step(s, &in); d += a->speed * CRET_DT;
    }
    float dyaw = fabsf(a->yaw - y0);
    *skid = sk;
    cret_sim_close(s); free(s);
    return d / fmaxf(dyaw, 1e-4f);
}
static void test_turning(void) {
    int sps[] = {CRET_SP_RAPTOR, CRET_SP_TREX, CRET_SP_STEGO};
    for (int q = 0; q < 3; q++) {
        const char* name = CRET_SPECIES_TABLE[sps[q]].name; float sk0, sk1;
        float r0 = turn_radius(sps[q], false, &sk0), r1 = turn_radius(sps[q], true, &sk1);
        fprintf(stderr, "turning: %-12s radius %.1f m walking, %.1f m sprinting (skid %.2f)\n", name, r0, r1, sk1);
        CHECK(r1 > 2.5f * r0, "%s: sprint turning radius %.1f m not much wider than walking %.1f m", name, r1, r0);
        CHECK(sk1 < 0.35f, "%s skids sideways while turning at a sprint (%.2f)", name, sk1);
    }
}

// Client prediction replays the same movement code: cret_sim_move(predict)
// must follow cret_sim_step exactly for an animal on its own (no statuses,
// nothing else in the world to touch it).
static void test_prediction(void) {
    int sps[] = {CRET_SP_RAPTOR, CRET_SP_TREX, CRET_SP_SAUROPELTA};
    for (int q = 0; q < 3; q++) {
        CretSim* A = calloc(1, sizeof *A); CretSim* B = calloc(1, sizeof *B); solo(A, sps[q]); solo(B, sps[q]);
        uint32_t rng = 11; float x, z; CHECK(find_flat(A->world, &rng, 6, &x, &z), "no flat land in the test world");
        place(A, 0, x, cret_height(A->world, x, z), z, 1); place(B, 0, x, cret_height(B->world, x, z), z, 1);
        int bad = 0;
        for (int t = 0; t < 240; t++) {
            int ph = t / 30;
            CretInput in = {.forward = ph % 4 == 3 ? 0.0f : 1.0f, .sprint = ph % 2 == 1, .turn = ph == 2 ? 1.0f : ph == 5 ? -0.4f : 0,
                            .strafe = ph == 6 ? 1.0f : 0, .jump = t % 37 == 5};
            cret_sim_step(A, &in); cret_sim_move(B, 0, &in, true); cret_sim_place(B, 0);
            const CretAgent *a = &A->agents[0], *b = &B->agents[0];
            bool same = a->x == b->x && a->y == b->y && a->z == b->z && a->vx == b->vx && a->vy == b->vy && a->vz == b->vz &&
                        a->yaw == b->yaw && a->stamina == b->stamina && a->on_ground == b->on_ground;
            if (!same && bad++ < 3)
                CHECK(0, "%s tick %d: prediction (%.4f %.4f %.4f v %.3f %.3f %.3f) differs from the sim (%.4f %.4f %.4f v %.3f %.3f %.3f)",
                      CRET_SPECIES_TABLE[sps[q]].name, t, b->x, b->y, b->z, b->vx, b->vy, b->vz, a->x, a->y, a->z, a->vx, a->vy, a->vz);
        }
        cret_sim_close(A); cret_sim_close(B); free(A); free(B);
    }
}

// Deep water: agents float near the surface and keep floating while swimming.
static void test_swimming(void) {
    CretSim* s = calloc(1, sizeof *s); solo(s, CRET_SP_STEGO); CretAgent* a = &s->agents[0]; const CretWorld* w = s->world;
    uint32_t rng = 5; float x = 0, z = 0; bool found = false;
    for (int t = 0; t < 20000 && !found; t++) {
        x = w->size * (0.15f + 0.7f * t_frand(&rng)); z = w->size * (0.15f + 0.7f * t_frand(&rng));
        found = cret_water(w, x, z) - cret_height(w, x, z) > 8;
    }
    CHECK(found, "no deep water in the test world");
    float wl = cret_water(w, x, z);
    place(s, 0, x, wl - 1, z, 0);
    CretInput in = {0}; int streak[1] = {0};
    for (int t = 0; t < 600; t++) {
        in.forward = t < 200 ? 0 : 1; in.turn = t > 400 ? 0.3f : 0;
        cret_sim_step(s, &in); check_invariants(s, streak, NULL, NULL);
        if (t == 199) {
            CHECK(a->swimming, "stegosaur in 8+ m of water is not swimming");
            float target = cret_water(w, a->x, a->z) - a->height * 0.6f;
            CHECK(fabsf(a->y - target) < 0.3f, "floating stegosaur at y %.2f, expected about %.2f", a->y, target);
        }
    }
    CHECK(a->alive, "the swimmer died");
    cret_sim_close(s); free(s);
}

// Drinking: an animal standing on a shore, facing water and holding drink,
// gains water. (The determinism run's random animals used to cover this by
// wading in; with the ability heads they reach water too rarely to count on.)
static void test_drinking(void) {
    CretSim* s = calloc(1, sizeof *s); solo(s, CRET_SP_GALLI); CretAgent* a = &s->agents[0]; const CretWorld* w = s->world;
    uint32_t rng = 17; int shores = 0; bool drank = false;
    for (int t = 0; t < 200000 && shores < 8 && !drank; t++) {
        float x = w->size * (0.1f + 0.8f * t_frand(&rng)), z = w->size * (0.1f + 0.8f * t_frand(&rng)), yaw = 6.2831853f * t_frand(&rng);
        if (cret_water(w, x, z) > cret_height(w, x, z) - 0.2f) continue;   // dry land only
        place(s, 0, x, cret_height(w, x, z) + 0.3f, z, yaw);
        if (!cret_water_ahead(s, a)) continue;
        CretInput in = {0};
        for (int k = 0; k < 20; k++) cret_sim_step(s, &in);
        if (!a->alive || !a->on_ground || a->swimming || a->speed > 0.5f || !cret_water_ahead(s, a)) continue;   // slid or fell in
        shores++;
        a->water = 0.3f; in.action = CRET_ACT_DRINK; bool acted = false;
        for (int k = 0; k < 60; k++) { cret_sim_step(s, &in); check_invariants(s, NULL, NULL, NULL); acted |= a->act == CRET_ACT_DRINK; }
        CHECK(acted && a->water > 0.4f, "shore %d: animal facing water did not drink (act %d, water %.3f)", shores, a->act, a->water);
        drank = acted && a->water > 0.4f;
    }
    CHECK(shores > 0, "no shore found in the test world");
    cret_sim_close(s); free(s);
}

// Does the agent's capsule, shrunk by `shrink` metres, overlap a prop shape?
// (A capsule sunk deeper than `shrink` into a prop still overlaps.)
static bool count_overlap(b3ShapeId shape, void* c) { (void)shape; ++*(int*)c; return true; }
static bool prop_overlap(CretSim* s, int i, float shrink) {
    CretAgent* a = &s->agents[i]; float r = fmaxf(a->radius - shrink, 0.02f);
    b3Vec3 pts[2] = {{0, a->radius + 0.05f, 0}, {0, a->height - a->radius + 0.05f, 0}};
    b3ShapeProxy proxy = {pts, 2, r};
    b3QueryFilter f = {.categoryBits = CRET_CAT_AGENT, .maskBits = CRET_CAT_PROP, .id = 2, .name = "cret_test"};
    int n = 0; b3World_OverlapShape(s->phys, (b3Pos){a->x, a->y, a->z}, &proxy, f, count_overlap, &n);
    return n > 0;
}
#define PEN_TOL 0.15f
// How deep (m, to 1 cm) the capsule is inside props: the largest shrink that still overlaps.
static float prop_depth(CretSim* s, int i) {
    if (!prop_overlap(s, i, 0.0f)) return 0;
    float lo = 0, hi = s->agents[i].radius;
    while (hi - lo > 0.01f) { float m = 0.5f * (lo + hi); if (prop_overlap(s, i, m)) lo = m; else hi = m; }
    return lo;
}

// A synthetic wall (box hull, prop category) across an agent's path, then
// the real prop colliders when the assets are present.
static void test_props(void) {
    uint32_t rng = 21; int species[] = {CRET_SP_RAPTOR, CRET_SP_TREX, CRET_SP_SAUROPELTA};
    for (int q = 0; q < 3; q++) {
        CretSim* s = calloc(1, sizeof *s); solo(s, species[q]); CretAgent* a = &s->agents[0];
        float x = 0, z = 0; bool flat = find_flat(s->world, &rng, 16, &x, &z);
        CHECK(flat, "no flat land for the wall test"); if (!flat) { cret_sim_close(s); free(s); continue; }
        float wall_x = x + 12, g = cret_height(s->world, wall_x, z);
        b3BodyDef bd = b3DefaultBodyDef(); bd.type = b3_staticBody; bd.position = (b3Pos){wall_x, g + 3, z};
        b3BodyId body = b3CreateBody(s->phys, &bd);
        b3ShapeDef sd = b3DefaultShapeDef(); sd.filter.categoryBits = CRET_CAT_PROP; sd.filter.maskBits = CRET_CAT_AGENT;
        b3BoxHull box = b3MakeBoxHull(0.5f, 6.0f, 12.0f); b3CreateHullShape(body, &sd, &box.base);
        place(s, 0, x, cret_height(s->world, x, z), z, 0);
        // The probe itself: a capsule placed inside the wall must report the overlap.
        place(s, 0, wall_x, cret_height(s->world, wall_x, z), z, 0);
        CHECK(prop_overlap(s, 0, PEN_TOL), "the overlap probe misses a capsule inside the wall");
        place(s, 0, x, cret_height(s->world, x, z), z, 0);
        float far = 0; int sunk = 0;
        CretInput in = {.forward = 1, .sprint = 1};
        for (int t = 0; t < 240; t++) {
            cret_sim_step(s, &in); check_invariants(s, NULL, NULL, NULL);
            far = fmaxf(far, a->x + a->radius); sunk += prop_overlap(s, 0, PEN_TOL);
        }
        CHECK(far > wall_x - 1.5f, "%s never reached the wall (front at %.2f, wall at %.2f)", CRET_SPECIES_TABLE[a->species].name, far, wall_x - 0.5f);
        CHECK(far < wall_x - 0.5f + 0.1f, "%s went into the wall: front %.2f past the face %.2f", CRET_SPECIES_TABLE[a->species].name, far, wall_x - 0.5f);
        CHECK(sunk == 0, "%s was more than %.2f m inside the wall on %d ticks", CRET_SPECIES_TABLE[a->species].name, PEN_TOL, sunk);
        cret_sim_close(s); free(s);
    }
    // Real props: walk into the large blockers and never sink into them.
    CretSim* s = calloc(1, sizeof *s); solo(s, CRET_SP_RAPTOR); CretAgent* a = &s->agents[0]; const CretWorld* w = s->world;
    if (!w->nprops) { fprintf(stderr, "props: no prop colliders loaded (assets/props missing); only the synthetic wall ran\n"); cret_sim_close(s); free(s); return; }
    int tried = 0, blocked = 0, sunk_props = 0;
    for (int k = 0; k < w->nprops && tried < 24; k += 1 + w->nprops / 97) {
        const CretPropCollider* pc = &w->props[k];
        if (pc->kind != PROP_BOULDER && pc->kind != PROP_LOG && pc->kind != PROP_STUMP) continue;
        float ang = t_frand(&rng) * 6.2831853f, sx = pc->x - cosf(ang) * 10, sz = pc->z - sinf(ang) * 10;
        float n[3]; terra_normal(&w->map, sx, sz, n);
        if (cret_height(w, sx, sz) < 1 || cret_water(w, sx, sz) > cret_height(w, sx, sz) || n[1] < 0.85f) continue;
        cret_sim_respawn(s, 0); place(s, 0, sx, cret_height(w, sx, sz), sz, ang);
        CretInput in = {.forward = 1}; int sunk = 0; float closest = 1e9f, deepest = 0, dy = 0;
        for (int t = 0; t < 150 && a->alive; t++) {
            cret_sim_step(s, &in);
            sunk += prop_overlap(s, 0, PEN_TOL);
            float d = prop_depth(s, 0); if (d > deepest) { deepest = d; dy = a->y - pc->y; }
            closest = fminf(closest, hypotf(a->x - pc->x, a->z - pc->z));
        }
        tried++; blocked += closest > 0.3f; sunk_props += sunk > 0;
        CHECK_CAPPED(sunk == 0, 5, "raptor was more than %.2f m inside prop %d (kind %d, scale %.2f) on %d ticks (deepest %.2f m, feet %.2f m above its base)",
                     PEN_TOL, k, pc->kind, pc->scale, sunk, deepest, dy);
    }
    fprintf(stderr, "props: %d colliders in the world; walked into %d, kept off the centre of %d, sank into %d\n", w->nprops, tried, blocked, sunk_props);
    CHECK(tried > 0, "no prop to walk into");
    cret_sim_close(s); free(s);
}

// Out-of-range inputs (a buggy client or policy) keep the state finite.
static void test_garbage_inputs(void) {
    CretSim* s = calloc(1, sizeof *s); CretConfig c = t_config(16); c.seed = 3;
    if (cret_sim_init(s, c) != 0) exit(2);
    uint32_t rng = 77; CretInput in[CRET_MAX_AGENTS];
    for (int t = 0; t < 1500; t++) {
        for (int i = 0; i < 16; i++) {
            in[i] = (CretInput){.forward = (t_frand(&rng) - 0.5f) * 8, .strafe = (t_frand(&rng) - 0.5f) * 8, .turn = (t_frand(&rng) - 0.5f) * 8,
                                .sprint = (uint8_t)t_rand(&rng), .jump = (uint8_t)t_rand(&rng), .action = (uint8_t)t_rand(&rng), .ability = (uint8_t)t_rand(&rng)};   // every action and ability byte, in range or not
        }
        cret_sim_step(s, in); check_invariants(s, NULL, NULL, NULL);
    }
    cret_sim_close(s); free(s);
}

// ------------------------------------------------------------------ soak
static void soak(int ticks) {
    CretSim* s = calloc(1, sizeof *s); CretConfig c = t_config(32); c.seed = 11; c.respawn_ticks = 30; c.weather = 1;
    if (cret_sim_init(s, c) != 0) exit(2);
    Counters k = {0}; int streak[CRET_MAX_AGENTS] = {0}; bool alive[CRET_MAX_AGENTS]; CretInput in[CRET_MAX_AGENTS];
    double t0 = t_now(), tb = 0, ts = 0; bool trace = getenv("CRET_SOAK_TRACE") != NULL;
    for (int t = 0; t < ticks; t++) {
        double q0 = t_now();
        for (int i = 0; i < c.num_agents; i++) { alive[i] = s->agents[i].alive; in[i] = cret_bot_input(s, i); }
        double q1 = t_now(); cret_sim_step(s, in); tb += q1 - q0; ts += t_now() - q1;
        if (trace && t % 1000 == 999) {
            int sw = 0, th = 0; for (int i = 0; i < c.num_agents; i++) { sw += s->agents[i].alive && s->agents[i].swimming; th += s->agents[i].alive && s->agents[i].water < 0.35f; }
            fprintf(stderr, "  tick %d: bots %.3f ms/tick, sim %.3f ms/tick, %d prop tiles, %d swimming, %d thirsty\n", t + 1, tb, ts, s->props.ns, sw, th); tb = ts = 0;
        }
        check_invariants(s, streak, &k, alive);
        if (t % 2000 == 1999) {   // the trainer resets every episode: exercise it too
            float obs[CRET_OBS_SIZE]; for (int i = 0; i < c.num_agents; i++) cret_sim_observe(s, i, obs);
        }
    }
    cret_sim_reset(s);
    fprintf(stderr, "soak: %d ticks of %d bots in %.1f s: %ld deaths, %ld kills, %ld hits, %ld attacks, %ld calls, %ld drink, %ld eat, %ld swim ticks\n",
            ticks, c.num_agents, t_now() - t0, k.deaths, k.kills, k.hits, k.attacks, k.calls, k.drinks, k.eats, k.swims);
    cret_sim_close(s); free(s);
}

// ------------------------------------------------------------------ combat
// Two adults on flat ground: slot 0 attacks, slot 1 (a stegosaur unless
// asked otherwise, it stands still and has health to spare) takes the blows.
static CretSim* duel(int sp, int victim, uint32_t* rng, float* x, float* z) {
    CretSim* s = calloc(1, sizeof *s);
    CretConfig c = t_config(2); c.player_species = sp; c.player_growth = 1.0f; c.hunger_seconds = c.thirst_seconds = 1e6f;
    if (cret_sim_init(s, c) != 0) exit(2);
    cret_sim_set_species(s, 1, victim);
    for (int i = 0; i < 2; i++) { CretAgent* a = &s->agents[i]; a->growth = 1; cret_apply_growth(a); a->health = a->max_health; a->group = 0; cret_sim_place(s, i); }
    CHECK(find_flat(s->world, rng, 9, x, z), "no flat land in the test world");
    return s;
}
// Stand both still for a moment so they settle on the ground, then hold them in place.
static void settle(CretSim* s, int ticks) {
    CretInput in[2] = {0};
    for (int t = 0; t < ticks; t++) cret_sim_step(s, in);
}
static void put(CretSim* s, int i, float x, float z, float yaw) { place(s, i, x, cret_height(s->world, x, z) + 0.2f, z, yaw); }
// Where the primary blow lands for an aim (mirrors strike_point in sim.c).
static void t_strike(const CretAgent* a, float aim, float* px, float* pz) {
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; float L = S->length * cret_growth_scale(a->growth);
    float base = S->tail ? -0.18f * L : 0.18f * L, arm = (S->tail ? 0.27f : 0.24f) * L, dir = a->yaw + (S->tail ? 3.14159265f : 0) + aim * S->neck_arc;
    *px = a->x + cosf(a->yaw) * base + cosf(dir) * arm; *pz = a->z + sinf(a->yaw) * base + sinf(dir) * arm;
}
// Presses `act` with `aim` once (then no input) and runs `ticks`; true if the victim lost health.
static bool swing(CretSim* s, int act, float aim, int ticks) {
    CretAgent* b = &s->agents[1]; float h0 = b->health;
    for (int t = 0; t < ticks && b->alive; t++) {
        CretInput in[2] = {{.aim = aim, .action = (uint8_t)(t == 0 ? act : 0)}, {0}};
        cret_sim_step(s, in);
        if (t == 0) CHECK(s->agents[0].act == act && s->agents[0].act_ticks == 0, "%s: action %d did not start on the press (act %d)",
                          CRET_SPECIES_TABLE[s->agents[0].species].name, act, s->agents[0].act);
    }
    return !b->alive || b->health < h0 - 0.5f;
}

// Aimed primary attacks: the strike point turns with the aim across the neck
// arc (the stegosaur's tail arc behind it); a victim off to one side is hit
// when aimed at and missed when aimed away.
static void test_aimed_attacks(void) {
    uint32_t rng = 404; int checked_miss = 0;
    for (int sp = 0; sp < CRET_SPECIES; sp++) {
        float x, z; CretSim* s = duel(sp, CRET_SP_STEGO, &rng, &x, &z);
        CretAgent* a = &s->agents[0]; CretAgent* b = &s->agents[1]; const CretSpecies* S = &CRET_SPECIES_TABLE[sp];
        // cret_attack_dir follows the aim; the rear attack points behind.
        a->yaw = 0.3f;
        for (float aim = -1; aim <= 1.01f; aim += 0.5f) {
            a->aim = aim; float want = 0.3f + (S->tail ? 3.14159265f : 0) + aim * S->neck_arc, d = cret_attack_dir(a, S->tail);
            CHECK(fabsf(atan2f(sinf(d - want), cosf(d - want))) < 1e-4f, "%s: attack dir %.3f for aim %.1f, want %.3f", S->name, d, aim, want);
        }
        CHECK(S->neck_arc >= 0.6f && S->neck_arc <= 1.23f, "%s: neck arc %.2f outside 35..70 degrees", S->name, S->neck_arc);
        for (int pass = 0; pass < 2; pass++) {   // 0: aimed at the victim, 1: aimed away
            put(s, 0, x, z, 0); settle(s, 15); a->aim = 1;
            float px, pz; t_strike(a, 1, &px, &pz);
            float dir = (S->tail ? 3.14159265f : 0) + S->neck_arc;   // yaw 0
            float rb = b->radius + 0.25f * S->reach;
            put(s, 1, px + cosf(dir) * rb, pz + sinf(dir) * rb, dir + 1.5708f);
            settle(s, 2);
            float qx, qz; t_strike(a, -1, &qx, &qz);
            bool far = cret_body_dist(b, qx, qz) > S->reach * 1.1f;
            if (pass == 1 && !far) continue;   // the arc is too narrow for this body to be out of reach on the other side
            a->attack_cd = 0; a->act = 0; b->health = b->max_health; b->bleed = 0; b->hurt_ticks = 0;
            bool hit = swing(s, CRET_ACT_ATTACK, pass ? -1.0f : 1.0f, (int)(S->hit_time * CRET_TICK_HZ) + 4);
            CHECK(hit == !pass, "%s: aimed %s the victim, %s", S->name, pass ? "away from" : "at", hit ? "hit" : "missed");
            checked_miss += pass;
        }
        cret_sim_close(s); free(s);
    }
    CHECK(checked_miss >= 4, "only %d species had a meaningful aim-away check", checked_miss);
}

// Attacks never root: a trotting animal that attacks keeps at least the
// species' swing speed fraction of its pace, and the attack starts on the
// tick it is pressed.
static void test_attack_while_moving(void) {
    uint32_t rng = 505;
    for (int sp = 0; sp < CRET_SPECIES; sp++) {
        const CretSpecies* S = &CRET_SPECIES_TABLE[sp];
        for (int act = CRET_ACT_ATTACK; act <= CRET_ACT_ATTACK2; act += CRET_ACT_ATTACK2 - CRET_ACT_ATTACK) {
            if (act == CRET_ACT_ATTACK2 && S->alt.kind == CRET_ALT_NONE) continue;
            CretSim* s = calloc(1, sizeof *s); solo(s, sp); CretAgent* a = &s->agents[0];
            float x, z, yaw;   // start at a trot a few metres back, along a flat strip
            CHECK(find_strip(s->world, &rng, 20, 0.985f, &x, &z, &yaw), "no flat land");
            put(s, 0, x - cosf(yaw) * 4, z - sinf(yaw) * 4, yaw); settle(s, 5);
            a->vx = cosf(yaw) * a->trot_speed; a->vz = sinf(yaw) * a->trot_speed;
            CretInput in = {.forward = 1};
            for (int t = 0; t < 12; t++) cret_sim_step(s, &in);
            float v0 = a->speed, vmin = v0;
            int hit = (int)((act == CRET_ACT_ATTACK ? S->hit_time : S->alt.hit_time) * CRET_TICK_HZ);
            float keep = act == CRET_ACT_ATTACK ? S->swing_speed : S->alt.speed;
            in.action = (uint8_t)act; cret_sim_step(s, &in); in.action = 0;
            CHECK(a->act == act && a->act_ticks == 0, "%s: action %d did not start at a trot (act %d)", S->name, act, a->act);
            for (int t = 0; t < hit + 2; t++) { cret_sim_step(s, &in); vmin = fminf(vmin, a->speed); }
            CHECK(v0 > a->trot_speed * 0.8f, "%s: trotted at only %.2f m/s (trot %.2f)", S->name, v0, a->trot_speed);
            CHECK(vmin >= keep * v0 * 0.93f && vmin >= 0.65f * v0, "%s: action %d slowed %.2f -> %.2f m/s (keeps %.2f)", S->name, act, v0, vmin, keep);
            cret_sim_close(s); free(s);
        }
        CHECK(S->swing_speed >= 0.7f && S->alt.speed >= 0.7f, "%s: swing speeds %.2f / %.2f", S->name, S->swing_speed, S->alt.speed);
        if (S->alt.kind == CRET_ALT_SLASH) CHECK(S->alt.speed >= 1.0f, "%s: the slash slows", S->name);
    }
}

// Secondary attacks per species: tail swipes hit behind (inside the arc) and
// throw the victim, not in front; slashes and kicks hit at the snout; each
// sets its own cooldown and leaves the primary ready.
static void test_secondary_attacks(void) {
    uint32_t rng = 606;
    static const int kinds[CRET_SPECIES] = {[CRET_SP_RAPTOR] = CRET_ALT_SLASH, [CRET_SP_STEGO] = CRET_ALT_TAIL, [CRET_SP_TREX] = CRET_ALT_TAIL,
                                            [CRET_SP_PACHY] = CRET_ALT_TAIL, [CRET_SP_SAUROPELTA] = CRET_ALT_TAIL, [CRET_SP_GALLI] = CRET_ALT_SLASH};
    for (int sp = 0; sp < CRET_SPECIES; sp++) {
        const CretSpecies* S = &CRET_SPECIES_TABLE[sp]; const CretAltAttack* A = &S->alt;
        CHECK(A->kind == kinds[sp], "%s: secondary kind %d, want %d", S->name, A->kind, kinds[sp]);
        CHECK(A->cooldown > A->attack_time && A->hit_time < A->attack_time && A->damage > 0 && A->reach > 0, "%s: secondary timings", S->name);
        float x, z; CretSim* s = duel(sp, CRET_SP_STEGO, &rng, &x, &z); CretAgent* a = &s->agents[0]; CretAgent* b = &s->agents[1];
        int len = (int)(A->hit_time * CRET_TICK_HZ) + 4;
        for (int pass = 0; pass < 2; pass++) {   // 0: where it should land, 1: where it must not
            put(s, 0, x, z, 0); settle(s, 15); a->attack_cd = a->attack2_cd = 0; a->act = 0; b->health = b->max_health; b->bleed = 0; b->hurt_ticks = 0;
            float bx, bz, by;
            if (A->kind == CRET_ALT_TAIL) {   // behind, or in front
                float d = 0.5f * A->reach + b->radius, dir = pass ? 0 : 3.14159265f;
                bx = x + cosf(dir) * d; bz = z + sinf(dir) * d; by = dir + 1.5708f;
            } else {   // at the snout, or behind
                float px, pz; t_strike(a, 0, &px, &pz);
                float d = b->radius + 0.3f * A->reach;
                bx = pass ? x - (S->length * 0.5f + A->reach + b->radius + 2) : px + d; bz = pass ? z : pz; by = 1.5708f;
            }
            put(s, 1, bx, bz, by); settle(s, 2);
            float v0 = sqrtf(b->vx * b->vx + b->vz * b->vz);
            bool hit = swing(s, CRET_ACT_ATTACK2, 0, len);
            CHECK(hit == !pass, "%s: secondary %s", S->name, pass ? "hit where it must not" : "missed");
            CHECK(a->attack_cd == 0, "%s: the secondary used the primary's cooldown", S->name);
            CHECK(a->attack2_cd > 0 && a->attack2_cd <= (int)(A->cooldown * CRET_TICK_HZ), "%s: secondary cooldown %d", S->name, a->attack2_cd);
            if (!pass && A->kind == CRET_ALT_TAIL && !S->tail)
                CHECK(b->hurt_ticks > 12 || sqrtf(b->vx * b->vx + b->vz * b->vz) > v0 + 0.3f, "%s: the tail swipe did not throw or stagger", S->name);
        }
        cret_sim_close(s); free(s);
    }
}

// An attack pressed up to CRET_ATTACK_BUFFER ticks before the cooldown ends
// starts the moment it can; one pressed earlier is dropped.
static void test_attack_buffer(void) {
    for (int sp = 0; sp < CRET_SPECIES; sp++) {
        CretSim* s = calloc(1, sizeof *s); solo(s, sp); CretAgent* a = &s->agents[0]; const CretSpecies* S = &CRET_SPECIES_TABLE[sp];
        for (int early = 0; early < 2; early++) {
            CretInput in = {.action = CRET_ACT_ATTACK};
            a->attack_cd = 0; a->act = 0; a->buf_ticks = 0; cret_sim_step(s, &in);
            CHECK(a->act == CRET_ACT_ATTACK, "%s: no attack", S->name);
            in.action = 0;
            int lead = early ? CRET_ATTACK_BUFFER + 6 : CRET_ATTACK_BUFFER - 1;
            while (a->attack_cd > lead) cret_sim_step(s, &in);
            in.action = CRET_ACT_ATTACK; cret_sim_step(s, &in); in.action = 0;
            int started = -1;
            for (int t = 0; t < lead + 4; t++) { cret_sim_step(s, &in); if (a->act == CRET_ACT_ATTACK && a->act_ticks == 0) { started = t; break; } }
            if (early) CHECK(started < 0, "%s: a press %d ticks early was not dropped", S->name, lead);
            else CHECK(started >= 0 && started <= lead, "%s: a press %d ticks early started at %d", S->name, lead, started);
        }
        cret_sim_close(s); free(s);
    }
}

int main(int argc, char** argv) {
    double t0 = t_now(); t_hold_world();
    if (argc > 2 && !strcmp(argv[1], "--soak")) { soak(atoi(argv[2])); return t_finish("test_sim --soak", t0); }
#define RUN_TEST(f) do { double _t = t_now(); f; fprintf(stderr, "  [%s: %.1f s]\n", #f, t_now() - _t); } while (0)
    RUN_TEST(test_bake_threads());
    RUN_TEST(test_determinism());
    RUN_TEST(test_falls());
    RUN_TEST(test_hop());
    RUN_TEST(test_mass());
    RUN_TEST(test_turning());
    RUN_TEST(test_prediction());
    RUN_TEST(test_swimming());
    RUN_TEST(test_drinking());
    RUN_TEST(test_props());
    RUN_TEST(test_garbage_inputs());
    RUN_TEST(test_aimed_attacks());
    RUN_TEST(test_attack_while_moving());
    RUN_TEST(test_secondary_attacks());
    RUN_TEST(test_attack_buffer());
    RUN_TEST(soak(3000));
    return t_finish("test_sim", t0);
}
