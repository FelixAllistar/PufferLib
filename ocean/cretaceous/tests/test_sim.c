// Sim tests: determinism (same seed and inputs give bit-identical state, also
// with sims stepped on threads and with the bake on 1 or 4 OpenMP threads),
// state invariants every tick, physics sanity (falls, swimming, prop
// colliders) and combat (aimed strike points, attacking on the move,
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
// No tree trunk within r metres of the segment (x0, z0)-(x1, z1).
static bool path_clear(const CretWorld* w, float x0, float z0, float x1, float z1, float r) {
    float tr[5 * 256], dx = x1 - x0, dz = z1 - z0, L2 = fmaxf(dx * dx + dz * dz, 1e-6f);
    for (int tz = (int)floorf((fminf(z0, z1) - r) / PROP_TILE); tz <= (int)floorf((fmaxf(z0, z1) + r) / PROP_TILE); tz++)
        for (int tx = (int)floorf((fminf(x0, x1) - r) / PROP_TILE); tx <= (int)floorf((fmaxf(x0, x1) + r) / PROP_TILE); tx++) {
            int n = cret_tree_trunks(w, tx, tz, tr, 256);
            for (int k = 0; k < n; k++) {
                float u = fminf(fmaxf(((tr[5 * k] - x0) * dx + (tr[5 * k + 2] - z0) * dz) / L2, 0), 1);
                if (hypotf(x0 + u * dx - tr[5 * k], z0 + u * dz - tr[5 * k + 2]) < r + tr[5 * k + 3]) return false;
            }
        }
    return true;
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
        if (pc->dyn && cret_prop_mass(w, k) < cret_mass(a)) continue;   // stepped over or kicked aside (test_dynprops)
        float ang = t_frand(&rng) * 6.2831853f, sx = pc->x - cosf(ang) * 10, sz = pc->z - sinf(ang) * 10;
        float n[3]; terra_normal(&w->map, sx, sz, n);
        if (cret_height(w, sx, sz) < 1 || cret_water(w, sx, sz) > cret_height(w, sx, sz) || n[1] < 0.85f) continue;
        cret_sim_respawn(s, 0); place(s, 0, sx, cret_height(w, sx, sz), sz, ang);
        CretInput in = {.forward = 1}; int sunk = 0; float closest = 1e9f, deepest = 0, dy = 0;
        for (int t = 0; t < 150 && a->alive; t++) {
            cret_sim_step(s, &in);
            // A movable prop the raptor set rolling may roll back onto it (the
            // mover pushes it out next tick): sinking counts while all is at rest.
            if (s->dyn_awake) continue;
            sunk += prop_overlap(s, 0, PEN_TOL);
            float d = prop_depth(s, 0); if (d > deepest) { deepest = d; dy = a->y - pc->y; }
            int r = cret_dyn_find(s, k); float px = r >= 0 ? s->dyn[r].p[0] : pc->x, pz = r >= 0 ? s->dyn[r].p[2] : pc->z;
            closest = fminf(closest, hypotf(a->x - px, a->z - pz));
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
            float x, z, yaw; int tries = 0;   // start at a trot a few metres back, through the flat spot (no tree in the way)
            do { CHECK(find_flat(s->world, &rng, 9, &x, &z), "no flat land"); yaw = t_frand(&rng) * 6.2831853f; }
            while (!path_clear(s->world, x - cosf(yaw) * 5, z - sinf(yaw) * 5, x + cosf(yaw) * 6, z + sinf(yaw) * 6, 2.0f) && ++tries < 50);
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

// ------------------------------------------------------------------ movable props
// Colliders match their meshes: each variant's collider bounds (hull, or the
// union of a log's slabs) against the LOD0 mesh's, and every seated prop's
// bottom rim lies under the ground (no floating, no open bottom showing).
static void test_prop_colliders(void) {
    CretSim* s = calloc(1, sizeof *s); solo(s, CRET_SP_RAPTOR); const CretWorld* w = s->world; const PropCatalog* c = &w->prop_cat;
    if (!w->nprops) { fprintf(stderr, "prop colliders: none loaded (assets/props missing)\n"); cret_sim_close(s); free(s); return; }
    int checked = 0;
    for (int v = 0; v < c->nvar; v++) {
        if (!w->prop_hull[v]) continue;
        char path[512]; snprintf(path, sizeof path, PROP_DIR "%s.prop", c->name[v]);
        float* pos; int32_t* idx; int ni, nv = prop_read_lod(path, 0, &pos, &idx, &ni);
        if (!nv) { free(pos); free(idx); continue; }
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (int i = 0; i < nv; i++) for (int k = 0; k < 3; k++) { lo[k] = fminf(lo[k], pos[3 * i + k]); hi[k] = fmaxf(hi[k], pos[3 * i + k]); }
        free(pos); free(idx);
        b3AABB b = b3ComputeHullAABB(w->prop_hull[v], b3Transform_identity);
        if (w->prop_nparts[v]) {
            b = b3ComputeHullAABB(w->prop_part[v][0], b3Transform_identity);
            for (int p = 1; p < w->prop_nparts[v]; p++) b = b3AABB_Union(b, b3ComputeHullAABB(w->prop_part[v][p], b3Transform_identity));
        }
        float blo[3] = {b.lowerBound.x, b.lowerBound.y, b.lowerBound.z}, bhi[3] = {b.upperBound.x, b.upperBound.y, b.upperBound.z};
        for (int k = 0; k < 3; k++) {
            float tol = 0.06f * (hi[k] - lo[k]) + 0.03f;
            CHECK(fabsf(blo[k] - lo[k]) < tol && fabsf(bhi[k] - hi[k]) < tol, "%s: collider axis %d spans %.2f..%.2f, mesh %.2f..%.2f",
                  c->name[v], k, blo[k], bhi[k], lo[k], hi[k]);
        }
        CHECK(w->prop_vol[v] > 0, "%s: collider has no volume", c->name[v]);
        checked++;
    }
    // Seating: rim points of every non-set-piece collider under the ground.
    int seated = 0, floating = 0;
    for (int k = 0; k < w->nprops; k++) {
        const CretPropCollider* pc = &w->props[k]; int v = pc->variant;
        if (pc->kind == PROP_COAST || pc->kind == PROP_CLIFF || !c->nbase[v]) continue;
        float worst = -1e9f; const float* q = pc->q;
        for (int r = 0; r < c->nbase[v]; r++) {
            float b[3] = {c->base[v][r][0] * pc->scale, c->base[v][r][1] * pc->scale, c->base[v][r][2] * pc->scale};
            float tx = 2 * (q[1] * b[2] - q[2] * b[1]), ty = 2 * (q[2] * b[0] - q[0] * b[2]), tz = 2 * (q[0] * b[1] - q[1] * b[0]);
            float o[3] = {b[0] + q[3] * tx + (q[1] * tz - q[2] * ty), b[1] + q[3] * ty + (q[2] * tx - q[0] * tz), b[2] + q[3] * tz + (q[0] * ty - q[1] * tx)};
            worst = fmaxf(worst, pc->y + o[1] - terra_height(&w->map, pc->x + o[0], pc->z + o[2]));
        }
        seated++; floating += worst > 0.01f;
        CHECK_CAPPED(worst <= 0.01f, 5, "prop %d (kind %d, %s, scale %.2f): bottom rim %.2f m above the ground", k, pc->kind, c->name[v], pc->scale, worst);
    }
    // What moves, and how heavy it is.
    static const char* NAME[PROP_KINDS] = {"boulder", "outcrop", "stone", "log", "stump", "roots", "debris", "driftwood", "coast", "cliff"};
    for (int kd = 0; kd < PROP_KINDS; kd++) {
        int n = 0; float lo = 1e30f, hi = 0, sum = 0;
        for (int k = 0; k < w->nprops; k++) if (w->props[k].kind == kd && w->props[k].dyn) { float m = cret_prop_mass(w, k); n++; lo = fminf(lo, m); hi = fmaxf(hi, m); sum += m; }
        if (n) fprintf(stderr, "  movable %-9s %5d, mass %6.0f .. %6.0f kg (mean %.0f)\n", NAME[kd], n, lo, hi, sum / n);
    }
    float tr[5 * 256]; int trunks = 0, T = (int)ceilf(w->size / PROP_TILE);
    for (int t = 0; t < T * T; t++) trunks += cret_tree_trunks(w, t % T, t / T, tr, 256);
    fprintf(stderr, "prop colliders: %d variants match their meshes; %d props seated, %d float; %d movable; %d tree trunks\n", checked, seated, floating, w->ndyn, trunks);
    CHECK(checked > 0, "no prop collider to check");
    CHECK(trunks > 0, "no tree trunks in the test world");
    cret_sim_close(s); free(s);
}

// Flat dry ground, cleared of props and trees within `r` metres (the test
// world's flat land is strewn with stones): their tiles are loaded and the
// bodies near the spot destroyed (whole trunk bodies of the tiles it spans).
static bool find_clear(CretSim* s, uint32_t* rng, float r, float* x, float* z) {
    const CretWorld* w = s->world;
    if (!find_flat(w, rng, r, x, z)) return false;
    cret_prop_stream_touch(&s->props, w, s->phys, *x, *z, r + 40, s->tick);
    for (int t = 0; t < s->props.ns; t++) {
        CretPropSlot* sl = &s->props.s[t];
        float cx = (sl->tile % s->props.tiles) * PROP_TILE, cz = (sl->tile / s->props.tiles) * PROP_TILE;
        bool spans = *x + r > cx && *x - r < cx + PROP_TILE && *z + r > cz && *z - r < cz + PROP_TILE;
        for (int k = 0; k < sl->n; k++) {
            if (!b3Body_IsValid(sl->bodies[k])) continue;
            b3Pos p = b3Body_GetPosition(sl->bodies[k]); int id = sl->ids[k];
            bool trunks = id < 0 && p.x == cx && p.y == 0 && p.z == cz;   // the tile's trunk body
            float R = id >= 0 ? w->prop_cat.radius[w->props[id].variant] * w->props[id].scale : 8.0f;
            if (trunks ? spans : hypotf(p.x - *x, p.z - *z) < r + R) b3DestroyBody(sl->bodies[k]);
        }
    }
    return true;
}
// The movable collider of `kind` whose mass is nearest `kg`.
static int dyn_of_mass(const CretWorld* w, int kind, float kg) {
    int best = -1; float bd = 1e30f;
    for (int k = 0; k < w->nprops; k++) {
        if (!w->props[k].dyn || w->props[k].kind != kind) continue;
        float d = fabsf(logf(cret_prop_mass(w, k) / kg)); if (d < bd) { bd = d; best = k; }
    }
    return best;
}
// Drops prop id upright at (x, z), turned by yaw, and lets it settle; returns its record.
static CretDynProp* drop_prop(CretSim* s, int id, float x, float z, float yaw) {
    float p[3] = {x, cret_height(s->world, x, z) + 0.15f, z}, q[4] = {0, sinf(0.5f * yaw), 0, cosf(0.5f * yaw)};
    cret_dyn_set(s, id, p, q, true);
    CretInput in[CRET_MAX_AGENTS] = {0};
    for (int t = 0; t < 90; t++) cret_sim_step(s, in);
    int r = cret_dyn_find(s, id);
    return r >= 0 ? &s->dyn[r] : NULL;
}

// Pushing: the animal walks into a prop for 4 s; how far did it go?
static float push_run(int species, int kind, float kg, float* mass, int* pusher) {
    uint32_t rng = 515; CretSim* s = calloc(1, sizeof *s); solo(s, species); CretAgent* a = &s->agents[0];
    float x, z; if (!find_clear(s, &rng, 14, &x, &z)) { cret_sim_close(s); free(s); return -1; }
    int id = dyn_of_mass(s->world, kind, kg); *mass = id >= 0 ? cret_prop_mass(s->world, id) : 0;
    if (id < 0) { cret_sim_close(s); free(s); return -1; }
    place(s, 0, x - 6, cret_height(s->world, x - 6, z), z, 0);
    CretDynProp* r = drop_prop(s, id, x, z, kind == PROP_LOG ? 1.5708f : 0);   // logs broadside on
    float x0 = r ? r->p[0] : x, z0 = r ? r->p[2] : z;
    place(s, 0, x - 4 - a->radius - s->world->prop_cat.radius[s->world->props[id].variant] * s->world->props[id].scale, cret_height(s->world, x - 6, z), z, 0);
    CretInput in = {.forward = 1};
    for (int t = 0; t < 120; t++) cret_sim_step(s, &in);
    for (int t = 0; t < 60; t++) { CretInput none = {0}; cret_sim_step(s, &none); }
    int k = cret_dyn_find(s, id); float d = k >= 0 ? hypotf(s->dyn[k].p[0] - x0, s->dyn[k].p[2] - z0) : 0;
    *pusher = k >= 0 ? s->dyn[k].pusher : -1;
    cret_sim_close(s); free(s);
    return d;
}

static void test_pushing(void) {
    struct { int species, kind; float kg; bool moves; } cases[] = {
        {CRET_SP_STEGO, PROP_BOULDER, 2000, true}, {CRET_SP_TREX, PROP_BOULDER, 2000, true}, {CRET_SP_RAPTOR, PROP_BOULDER, 2000, false},
        {CRET_SP_RAPTOR, PROP_LOG, 150, true}, {CRET_SP_RAPTOR, PROP_STONE, 60, true}, {CRET_SP_PACHY, PROP_BOULDER, 400, true}};
    for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        float mass; int pusher; float d = push_run(cases[c].species, cases[c].kind, cases[c].kg, &mass, &pusher);
        if (d < 0) { fprintf(stderr, "  push: no clear ground or prop for case %zu\n", c); continue; }
        fprintf(stderr, "  push: %-18s into a %5.0f kg %s moved it %.2f m\n", CRET_SPECIES_TABLE[cases[c].species].name, mass,
                cases[c].kind == PROP_BOULDER ? "boulder" : cases[c].kind == PROP_LOG ? "log" : "stone", d);
        if (cases[c].moves) CHECK(d > 0.3f && pusher == 0, "%s did not push a %.0f kg prop (%.2f m)", CRET_SPECIES_TABLE[cases[c].species].name, mass, d);
        else CHECK(d < 0.3f, "%s pushed a %.0f kg prop %.2f m", CRET_SPECIES_TABLE[cases[c].species].name, mass, d);
    }
}

// Crushing: a boulder rolled into a raptor hurts it and credits the last
// pusher; one at rest against it never does.
static void test_crushing(void) {
    uint32_t rng = 616; CretSim* s = calloc(1, sizeof *s);
    CretConfig c = t_config(2); c.player_species = CRET_SP_RAPTOR; c.player_growth = 1.0f; c.hunger_seconds = c.thirst_seconds = 1e6f; c.respawn_ticks = 100000;
    if (cret_sim_init(s, c) != 0) exit(2);
    cret_sim_set_species(s, 1, CRET_SP_TREX);
    float x, z; if (!find_clear(s, &rng, 16, &x, &z)) { CHECK(0, "no clear ground for the crush test"); cret_sim_close(s); free(s); return; }
    int id = dyn_of_mass(s->world, PROP_BOULDER, 2000);
    CretAgent* a = &s->agents[0];
    place(s, 0, x + 3, cret_height(s->world, x + 3, z), z, 0); place(s, 1, x - 12, cret_height(s->world, x - 12, z), z + 12, 0);
    CretDynProp* r = drop_prop(s, id, x, z, 0); CHECK(r != NULL, "no record for the dropped boulder"); if (!r) { cret_sim_close(s); free(s); return; }
    // At rest against the raptor: nothing.
    float rr = s->world->prop_cat.radius[s->world->props[id].variant] * s->world->props[id].scale;
    place(s, 0, r->p[0] + rr + a->radius + 0.05f, cret_height(s->world, r->p[0] + rr + a->radius, z), z, 3.14159f);
    float h0 = a->health; CretInput in[2] = {0};
    for (int t = 0; t < 60; t++) cret_sim_step(s, in);
    CHECK(a->health >= h0 - 0.01f, "a boulder at rest hurt the raptor (%.1f -> %.1f)", h0, a->health);
    // Sent at 10 m/s, pushed by the tyrannosaur a moment ago.
    place(s, 0, r->p[0] + rr + a->radius + 2.0f, cret_height(s->world, r->p[0] + rr + a->radius + 2.0f, z), z, 3.14159f);
    r = &s->dyn[cret_dyn_find(s, id)];
    r->pusher = 1; r->push_tick = s->tick;
    b3Body_SetLinearVelocity(r->body, (b3Vec3){10, 0, 0}); b3Body_SetAwake(r->body, true); r->awake = 1; s->dyn_awake = 1;
    uint32_t hits0 = s->hit_count; h0 = a->health; bool crushed = false;
    for (int t = 0; t < 45 && a->alive; t++) { cret_sim_step(s, in); crushed |= a->health < h0 - 1; }
    bool logged = false;
    for (uint32_t k = hits0; k < s->hit_count; k++) { const CretHit* h = &s->hits[k % CRET_HIT_LOG]; logged |= h->kind == CRET_HIT_CRUSH && h->attacker == 1 && h->victim == 0; }
    fprintf(stderr, "crush: a %.0f kg boulder at 10 m/s took the raptor from %.0f to %.0f hp (%s)\n", cret_prop_mass(s->world, id), h0, fmaxf(a->health, 0), a->alive ? "alive" : "dead");
    CHECK(crushed || !a->alive, "the rolling boulder did not hurt the raptor");
    CHECK(logged, "the crush was not credited to the pusher in the hit log");
    CHECK(a->last_attacker == 1, "the raptor's last attacker is %d, not the pusher", a->last_attacker);
    if (!a->alive) CHECK(s->stats.kills[CRET_SP_TREX][CRET_SP_RAPTOR] == 1, "the kill was not credited");
    cret_sim_close(s); free(s);
}

// Same pushes, same rolls: two sims bit for bit.
static void test_dyn_determinism(void) {
    CretSim* S[2]; uint64_t h[2][400]; int moved = 0;
    for (int q = 0; q < 2; q++) {
        uint32_t rng = 717; CretSim* s = S[q] = calloc(1, sizeof *s);
        CretConfig c = t_config(3); c.player_species = CRET_SP_STEGO; c.player_growth = 1.0f; c.hunger_seconds = c.thirst_seconds = 1e6f;
        if (cret_sim_init(s, c) != 0) exit(2);
        cret_sim_set_species(s, 1, CRET_SP_TREX); cret_sim_set_species(s, 2, CRET_SP_RAPTOR);
        float x, z; if (!find_clear(s, &rng, 16, &x, &z)) { CHECK(0, "no clear ground"); return; }
        int ids[3] = {dyn_of_mass(s->world, PROP_BOULDER, 2000), dyn_of_mass(s->world, PROP_LOG, 300), dyn_of_mass(s->world, PROP_BOULDER, 600)};
        for (int k = 0; k < 3; k++) { float p[3] = {x, cret_height(s->world, x, z + 5 * (k - 1)) + 0.15f, z + 5 * (k - 1)}, qq[4] = {0, 0, 0, 1}; cret_dyn_set(s, ids[k], p, qq, true); }
        for (int i = 0; i < 3; i++) place(s, i, x - 7, cret_height(s->world, x - 7, z + 5 * (i - 1)), z + 5 * (i - 1), 0);
        for (int t = 0; t < 400; t++) {
            CretInput in[3]; for (int i = 0; i < 3; i++) in[i] = (CretInput){.forward = t < 200 ? 1.0f : 0, .turn = t > 120 && t < 160 ? 0.3f : 0, .sprint = t > 60};
            cret_sim_step(s, in); h[q][t] = t_sim_hash(s);
        }
        if (q == 0) for (int k = 0; k < s->ndyn; k++) moved += hypotf(s->dyn[k].p[0] - x, 0) > 0.5f;
    }
    int bad = first_mismatch(h[0], h[1], 400);
    fprintf(stderr, "dyn determinism: 2 sims, 3 animals pushing 3 props for 400 ticks: %s (%d props moved)\n", bad < 0 ? "identical" : "DIVERGED", moved);
    CHECK(bad < 0, "pushed props diverged at tick %d", bad);
    CHECK(moved > 0, "nothing moved in the determinism run");
    for (int q = 0; q < 2; q++) { cret_sim_close(S[q]); free(S[q]); }
}

// Trees: an animal cannot walk through a trunk.
static void test_trunks(void) {
    CretSim* s = calloc(1, sizeof *s); solo(s, CRET_SP_GALLI); CretAgent* a = &s->agents[0]; const CretWorld* w = s->world;
    int T = (int)ceilf(w->size / PROP_TILE), tried = 0, through = 0; float tr[5 * 256];
    for (int t = 0; t < T * T && tried < 12; t += 7) {
        int n = cret_tree_trunks(w, t % T, t / T, tr, 256);
        for (int k = 0; k < n && tried < 12; k += 5) {
            float* p = tr + 5 * k; if (p[3] < 0.2f) continue;
            float sx = p[0] - 8, sz = p[2], n3[3]; terra_normal(&w->map, sx, sz, n3);
            if (n3[1] < 0.9f || cret_water(w, sx, sz) > cret_height(w, sx, sz) || cret_height(w, sx, sz) < 1) continue;
            cret_sim_respawn(s, 0); place(s, 0, sx, cret_height(w, sx, sz), sz, 0);
            CretInput in = {.forward = 1}; float closest = 1e9f;
            for (int q = 0; q < 90 && a->alive; q++) { cret_sim_step(s, &in); if (fabsf(a->z - p[2]) < 0.2f) closest = fminf(closest, fabsf(a->x - p[0])); }
            tried++; through += closest < p[3] + a->radius - 0.15f;
        }
    }
    fprintf(stderr, "trunks: walked into %d trees, passed through %d\n", tried, through);
    CHECK(tried > 0, "no tree to walk into");
    CHECK(through == 0, "walked through %d of %d trunks", through, tried);
    cret_sim_close(s); free(s);
}

int main(int argc, char** argv) {
    double t0 = t_now(); t_hold_world();
    if (argc > 2 && !strcmp(argv[1], "--soak")) { soak(atoi(argv[2])); return t_finish("test_sim --soak", t0); }
#define RUN_TEST(f) do { double _t = t_now(); f; fprintf(stderr, "  [%s: %.1f s]\n", #f, t_now() - _t); } while (0)
    RUN_TEST(test_bake_threads());
    RUN_TEST(test_determinism());
    RUN_TEST(test_falls());
    RUN_TEST(test_swimming());
    RUN_TEST(test_drinking());
    RUN_TEST(test_props());
    RUN_TEST(test_prop_colliders());
    RUN_TEST(test_pushing());
    RUN_TEST(test_crushing());
    RUN_TEST(test_dyn_determinism());
    RUN_TEST(test_trunks());
    RUN_TEST(test_garbage_inputs());
    RUN_TEST(test_aimed_attacks());
    RUN_TEST(test_attack_while_moving());
    RUN_TEST(test_secondary_attacks());
    RUN_TEST(test_attack_buffer());
    RUN_TEST(soak(3000));
    return t_finish("test_sim", t0);
}
