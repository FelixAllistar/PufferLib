// Sim tests: determinism (same seed and inputs give bit-identical state, also
// with sims stepped on threads and with the bake on 1 or 4 OpenMP threads),
// state invariants every tick, and physics sanity (falls, swimming, prop
// colliders).
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
        CHECK_CAPPED(a->act <= CRET_ACT_NEST, 5, "tick %d agent %d: act %d", s->tick, i, a->act);
        CHECK_CAPPED(a->bleed >= 0 && a->broken >= 0, 5, "tick %d agent %d: bleed %.3f broken %.3f", s->tick, i, a->bleed, a->broken);
        if (a->on_ground) CHECK_CAPPED(fabsf(a->y - g) < 0.4f, 5, "tick %d agent %d: on_ground but %.2f m from the ground", s->tick, i, a->y - g);
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
    RUN_TEST(test_garbage_inputs());
    RUN_TEST(soak(3000));
    return t_finish("test_sim", t0);
}
