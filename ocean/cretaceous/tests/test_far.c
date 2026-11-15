// Far groups (fargroup.h, docs/ecosystem.md, Far groups): folding a group
// into one record and back.
//   - fold and unfold at once: every member's state comes back unchanged,
//     the members stand on dry, walkable ground, clear of props and of
//     each other, out of the physics world while folded;
//   - a record unfolded on a shore and on a prop: nobody in the water or in
//     the prop (the prop case runs only with assets/props);
//   - a minute folded: the members lived it (aged 30 ticks a second, ate,
//     drank, burned, healed a leg wound at the full sim's rate), then come
//     back with that state;
//   - a person walking up unfolds the groups near it, and a slot a policy
//     drives never folds;
//   - determinism with folding on: two sims in lockstep for 4000 ticks (the
//     state hash covers the records), and the run folded and stepped records.
#include "test_util.h"
#include <stddef.h>

static bool count_hit(b3ShapeId shape, void* c) { (void)shape; ++*(int*)c; return true; }
// Agent i's capsule overlaps a prop, a movable prop or another animal's body.
static bool overlaps_any(CretSim* s, int i) {
    CretAgent* a = &s->agents[i];
    b3Vec3 pts[2] = {{0, a->radius + 0.05f, 0}, {0, fmaxf(a->height - a->radius, a->radius + 0.01f) + 0.05f, 0}};
    b3ShapeProxy proxy = {pts, 2, fmaxf(a->radius - 0.02f, 0.02f)};
    b3QueryFilter f = {.categoryBits = CRET_CAT_AGENT, .maskBits = CRET_CAT_PROP | CRET_CAT_DYN, .id = 9, .name = "cret_test_far"};
    int n = 0; b3World_OverlapShape(s->phys, (b3Pos){a->x, a->y, a->z}, &proxy, f, count_hit, &n);
    for (int j = 0; j < s->cfg.num_agents; j++) {   // other animals: capsules standing upright, so the horizontal gap decides
        const CretAgent* b = &s->agents[j]; if (j == i || !b->alive) continue;
        if (hypotf(b->x - a->x, b->z - a->z) < 0.98f * (a->radius + b->radius)) n++;
    }
    return n > 0;
}
// Checks where record r's former members stand after an unfold.
static void check_placed(CretSim* s, const int* mem, int n, const char* what) {
    for (int k = 0; k < n; k++) {
        CretAgent* a = &s->agents[mem[k]];
        float h = cret_height(s->world, a->x, a->z), wl = cret_water(s->world, a->x, a->z);
        CHECK(a->alive && !cret_far_folded(s, mem[k]), "%s: member %d is not back in the full sim", what, mem[k]);
        CHECK(cret_walkable(s->world, a->x, a->z), "%s: member %d unfolded on ground it cannot walk (%.0f, %.0f)", what, mem[k], a->x, a->z);
        CHECK(wl <= h - 0.2f, "%s: member %d unfolded in water (ground %.2f, water %.2f)", what, mem[k], h, wl);
        CHECK(fabsf(a->y - h) < 0.01f, "%s: member %d not on the ground", what, mem[k]);
        CHECK(!overlaps_any(s, mem[k]), "%s: member %d unfolded inside a prop or another animal", what, mem[k]);
        b3Vec3 p = b3Body_GetPosition(a->body);
        CHECK(fabsf(p.x - a->x) < 0.01f && fabsf(p.z - a->z) < 0.01f, "%s: member %d's body is not where it stands", what, mem[k]);
    }
}

// A world of bots with folding on and nobody watching: after a few seconds the calm units fold.
static CretSim* far_world(int agents, uint32_t seed) {
    CretSim* s = calloc(1, sizeof *s); CretConfig c = t_config(agents);
    c.seed = seed; c.respawn_ticks = 300; c.far_range = CRET_FAR_RANGE;
    if (cret_sim_init(s, c) != 0) { fprintf(stderr, "sim init failed\n"); exit(2); }
    return s;
}
static void step_bots(CretSim* s, int ticks) {
    CretInput in[CRET_MAX_AGENTS];
    for (int t = 0; t < ticks; t++) { for (int i = 0; i < s->cfg.num_agents; i++) in[i] = cret_bot_input(s, i); cret_sim_step(s, in); }
}
// The largest record (-1: none).
static int largest(const CretSim* s) {
    int best = -1;
    for (int r = 0; r < CRET_MAX_AGENTS; r++) if (s->far.g[r].active && (best < 0 || s->far.g[r].n > s->far.g[best].n)) best = r;
    return best;
}

// The fields a member must keep across a fold: everything the full sim needs back.
typedef struct Keep { uint8_t species, group; float growth, health, max_health, food, water, fat, fatigue, broken; int ticks_alive; float water_x, wound[CRET_WOUND_PLACES]; } Keep;
static Keep keep_of(const CretAgent* a) {
    Keep k = {a->species, a->group, a->growth, a->health, a->max_health, a->food, a->water, a->fat, a->fatigue, a->broken, a->ticks_alive, a->water_x, {0}};
    for (int p = 0; p < CRET_WOUND_PLACES; p++) k.wound[p] = a->wound[p].sev;
    return k;
}
// The same state, group aside (an unfolded group takes a new id if its own was taken meanwhile).
static bool keep_eq(const Keep* a, const Keep* b) {
    return a->species == b->species && a->growth == b->growth && a->health == b->health && a->max_health == b->max_health && a->food == b->food &&
           a->water == b->water && a->fat == b->fat && a->fatigue == b->fatigue && a->broken == b->broken && a->ticks_alive == b->ticks_alive && a->water_x == b->water_x &&
           !memcmp(a->wound, b->wound, sizeof a->wound);
}

static void test_fold_unfold(void) {
    CretSim* s = far_world(24, 7);
    step_bots(s, 20 * CRET_TICK_HZ);
    int members, recs = cret_far_count(s, &members);
    fprintf(stderr, "far: after 20 s, %d of 24 animals folded into %d records\n", members, recs);
    CHECK(recs > 0, "nothing folded in 20 s with nobody about");
    int r = largest(s); if (r < 0) { cret_sim_close(s); free(s); return; }
    // Folded: not alive, not dead, out of the physics world.
    const CretFarGroup* g = &s->far.g[r]; int n = g->n, mem[CRET_FAR_MEMBERS]; Keep before[CRET_FAR_MEMBERS];
    for (int k = 0; k < n; k++) {
        mem[k] = g->member[k]; const CretAgent* a = &s->agents[mem[k]]; before[k] = keep_of(a);
        CHECK(!a->alive && cret_far_folded(s, mem[k]) && cret_far_present(s, mem[k]), "member %d is not folded", mem[k]);
        CHECK(b3Body_GetPosition(a->body).y < -500.0f, "member %d's body is still in the physics world", mem[k]);
    }
    CHECK(cret_far_unfold(s, r) == n, "unfold placed fewer than the %d members", n);
    check_placed(s, mem, n, "unfold");
    for (int k = 0; k < n; k++) {
        Keep after = keep_of(&s->agents[mem[k]]);
        CHECK(keep_eq(&before[k], &after), "member %d's state changed across fold and unfold", mem[k]);
        CHECK(after.group == before[k].group, "member %d lost its group", mem[k]);
    }
    int g0 = s->agents[mem[0]].group;
    if (g0) for (int k = 0; k < n; k++) CHECK(s->agents[mem[k]].group == g0, "the herd split up on unfolding");

    // A minute folded: the members live it by the full sim's rules, then come
    // back with that state. The unit just unfolded may not fold again at once
    // (a predator of the full sim may be near it now), so the largest record
    // folded within the next 30 s is the one followed.
    int r2 = cret_far_fold(s, mem[0]);
    for (int t = 0; t < 30 && r2 < 0; t++) { step_bots(s, CRET_TICK_HZ); r2 = largest(s); }
    CHECK(r2 >= 0, "nothing folded again within 30 s");
    if (r2 >= 0) {
        n = s->far.g[r2].n;
        // Each member carries a leg wound into the minute: it heals at the full sim's rate a second (wounds.c).
        for (int k = 0; k < n; k++) { mem[k] = s->far.g[r2].member[k]; s->agents[mem[k]].wound[CRET_WOUND_LEG].sev = 0.5f; before[k] = keep_of(&s->agents[mem[k]]); }
        double eaten0 = 0, burned0 = 0; for (int sp = 0; sp < CRET_SPECIES; sp++) { eaten0 += s->stats.eaten_kg[sp]; burned0 += s->stats.burned_kg[sp]; }
        step_bots(s, 60 * CRET_TICK_HZ);
        double eaten = -eaten0, burned = -burned0; for (int sp = 0; sp < CRET_SPECIES; sp++) { eaten += s->stats.eaten_kg[sp]; burned += s->stats.burned_kg[sp]; }
        int lived = 0;
        for (int k = 0; k < n; k++) {
            const CretAgent* a = &s->agents[mem[k]]; if (!cret_far_present(s, mem[k])) continue;   // it died, or the sim took it back
            if (!cret_far_folded(s, mem[k])) continue;
            lived++;
            CHECK(a->ticks_alive >= before[k].ticks_alive + 59 * CRET_TICK_HZ, "member %d did not age while folded (%d -> %d ticks)", mem[k], before[k].ticks_alive, a->ticks_alive);
            CHECK(a->growth >= before[k].growth && a->species == before[k].species, "member %d shrank or changed species", mem[k]);
            CHECK(a->food != before[k].food || a->water != before[k].water, "member %d's needs stood still for a minute", mem[k]);
            // Healed by 60 s at a rate between hungry and resting, or worsened at most as a starving one does.
            float healed = 0.5f - a->wound[CRET_WOUND_LEG].sev, day = s->cfg.day_seconds;
            CHECK((healed > 0.9f * CRET_WOUND_HEAL_HUNGRY * 59 / day && healed < 1.1f * CRET_WOUND_HEAL_REST * 61 / day) || (healed < 0 && -healed < 1.1f * CRET_WOUND_WORSEN * 61 / day),
                  "member %d's leg wound went 0.5 -> %.4f in a minute folded (the full sim heals %.4f-%.4f)", mem[k], a->wound[CRET_WOUND_LEG].sev, CRET_WOUND_HEAL_HUNGRY * 60 / day, CRET_WOUND_HEAL_REST * 60 / day);
        }
        fprintf(stderr, "far: a minute folded: %d members lived it; the world ate %.1f kg and burned %.1f kg\n", lived, eaten, burned);
        CHECK(burned > 0, "nothing burned food in a minute");
        int rr = s->far.of[mem[0]] - 1;
        if (rr >= 0) {
            int m2[CRET_FAR_MEMBERS], n2 = s->far.g[rr].n; Keep k2[CRET_FAR_MEMBERS];
            for (int k = 0; k < n2; k++) { m2[k] = s->far.g[rr].member[k]; k2[k] = keep_of(&s->agents[m2[k]]); }
            cret_far_unfold(s, rr);
            check_placed(s, m2, n2, "unfold after a minute");
            for (int k = 0; k < n2; k++) { Keep a2 = keep_of(&s->agents[m2[k]]); CHECK(keep_eq(&k2[k], &a2), "member %d's state changed on unfolding", m2[k]); }
        }
    }
    cret_sim_close(s); free(s);
}

// A record moved onto a shore (dry ground within 10-30 m of water), and
// onto a prop: its members come back on dry ground, clear of the prop.
static void test_unfold_hard_ground(void) {
    CretSim* s = far_world(24, 9);
    step_bots(s, 20 * CRET_TICK_HZ);
    const CretWorld* w = s->world; int shores = 0, spots = 0;
    for (float z = 60; z < w->size - 60 && shores < 6; z += 13) for (float x = 60; x < w->size - 60 && shores < 6; x += 17) {
        if (!cret_walkable(w, x, z) || cret_water(w, x, z) > cret_height(w, x, z) - 0.2f) continue;
        bool wet = false;
        for (int k = 0; k < 24 && !wet; k++) { float d = 10.0f * (1 + k / 8), wx = x + d * cosf(k * 0.785f), wz = z + d * sinf(k * 0.785f); wet = cret_water(w, wx, wz) > cret_height(w, wx, wz) + 0.1f; }
        if (!wet) continue;
        spots++;
        int r = largest(s);
        for (int t = 0; t < 10 && r < 0; t++) { step_bots(s, CRET_TICK_HZ); r = largest(s); }
        if (r < 0) continue;
        CretFarGroup* g = &s->far.g[r]; g->x = x; g->z = z;
        for (int k = 0; k < g->n; k++) { g->fx[k] = 3.0f * g->fx[k] + 4.0f; g->fz[k] *= 3.0f; }   // a spread formation, some of it over the water
        int mem[CRET_FAR_MEMBERS], n = g->n; for (int k = 0; k < n; k++) mem[k] = g->member[k];
        cret_far_unfold(s, r); check_placed(s, mem, n, "shore");
        shores++;
        z += 150;   // the next shore elsewhere
    }
    fprintf(stderr, "far: unfolded %d records on shores (%d shore spots looked at)\n", shores, spots);
    CHECK(shores >= 3, "found only %d shores with a record to unfold there", shores);
    if (!w->nprops) fprintf(stderr, "far: no prop colliders loaded (assets/props missing); the prop case did not run\n");
    int on_props = 0;
    for (int p = 0; p < w->nprops && on_props < 6; p += 1 + w->nprops / 50) {
        const CretPropCollider* pc = &w->props[p];
        if (!cret_walkable(w, pc->x, pc->z)) continue;
        step_bots(s, CRET_TICK_HZ);
        int r = largest(s); if (r < 0) continue;
        CretFarGroup* g = &s->far.g[r]; g->x = pc->x; g->z = pc->z;
        for (int k = 0; k < g->n; k++) g->fx[k] = g->fz[k] = 0;   // every member onto the prop itself
        int mem[CRET_FAR_MEMBERS], n = g->n; for (int k = 0; k < n; k++) mem[k] = g->member[k];
        cret_far_unfold(s, r); check_placed(s, mem, n, "prop");
        on_props++;
    }
    if (w->nprops) { fprintf(stderr, "far: unfolded %d records on props\n", on_props); CHECK(on_props > 0, "no prop to unfold a record on"); }
    cret_sim_close(s); free(s);
}

// A person in the far corner: the groups fold. Walked up to a record, the
// person unfolds it within a second. A slot marked as a policy's never folds.
static void test_person_and_learned(void) {
    CretSim* s = calloc(1, sizeof *s); CretConfig c = t_config(24);
    c.seed = 7; c.respawn_ticks = 300; c.far_range = CRET_FAR_RANGE; c.player_growth = 1.0f; c.hunger_seconds = c.thirst_seconds = 1e5f;
    if (cret_sim_init(s, c) != 0) exit(2);
    s->far.learned[5] = 1;
    CretAgent* p = &s->agents[0];
    p->x = 40; p->z = 40; p->y = cret_height(s->world, 40, 40); cret_sim_place(s, 0);
    CretInput in[CRET_MAX_AGENTS]; int folded5 = 0;
    for (int t = 0; t < 20 * CRET_TICK_HZ; t++) {
        in[0] = (CretInput){0}; for (int i = 1; i < s->cfg.num_agents; i++) in[i] = cret_bot_input(s, i);
        cret_sim_step(s, in); folded5 += cret_far_folded(s, 5);
        p->x = 40; p->z = 40; cret_sim_place(s, 0);   // the person stays in the corner
    }
    int members, recs = cret_far_count(s, &members);
    CHECK(recs > 0, "nothing folded with the only person in the corner");
    CHECK(folded5 == 0, "the policy's slot was folded");
    CHECK(!cret_far_folded(s, 0), "the person was folded");
    int r = largest(s);
    if (r >= 0) {
        int m0 = s->far.g[r].member[0]; float gx = s->far.g[r].x, gz = s->far.g[r].z;
        p->x = gx + 120; p->z = gz; if (!cret_walkable(s->world, p->x, p->z)) { p->x = gx; p->z = gz + 120; }
        p->y = cret_height(s->world, p->x, p->z); cret_sim_place(s, 0);
        int took = -1;
        for (int t = 0; t < 2 * CRET_TICK_HZ && took < 0; t++) {
            in[0] = (CretInput){0}; for (int i = 1; i < s->cfg.num_agents; i++) in[i] = cret_bot_input(s, i);
            cret_sim_step(s, in);
            if (!cret_far_folded(s, m0)) took = t;
        }
        CHECK(took >= 0 && took < CRET_TICK_HZ + 1, "a record 120 m from a person was not unfolded within a second (%d ticks)", took);
        CHECK(s->agents[m0].alive, "the unfolded member is not alive");
        fprintf(stderr, "far: %d animals in %d records with the person in the corner; walked up to, a record unfolded after %d ticks\n", members, recs, took);
    }
    cret_sim_close(s); free(s);
}

// Two sims with folding on, the same seed and inputs, in lockstep, while a
// person walks across the island at 10 m/s from one corner to the other
// (records ahead of it unfold, those behind it fold again): the state hash
// (records included, tests/test_util.h) matches on every tick.
static void test_far_determinism(void) {
    enum { TICKS = 6000, N = 40 };
    CretSim* sims[2]; int bad = -1, most = 0;
    for (int k = 0; k < 2; k++) {
        CretSim* s = sims[k] = calloc(1, sizeof *s); CretConfig c = t_config(N);
        c.seed = 5; c.respawn_ticks = 300; c.far_range = CRET_FAR_RANGE; c.player_growth = 1.0f; c.hunger_seconds = 240; c.thirst_seconds = 180;
        if (cret_sim_init(s, c) != 0) exit(2);
    }
    CretInput in[2][CRET_MAX_AGENTS];
    for (int t = 0; t < TICKS; t++) {
        for (int k = 0; k < 2; k++) {
            CretSim* s = sims[k]; float d = 40 + t * 10.0f / CRET_TICK_HZ * 0.7071f;
            CretAgent* p = &s->agents[0]; p->x = fminf(d, s->world->size - 40); p->z = p->x; p->y = cret_height(s->world, p->x, p->z); cret_sim_place(s, 0);
            in[k][0] = (CretInput){0}; for (int i = 1; i < N; i++) in[k][i] = cret_bot_input(s, i);
            cret_sim_step(s, in[k]);
        }
        if (bad < 0 && t_sim_hash(sims[0]) != t_sim_hash(sims[1])) bad = t;
        int m; cret_far_count(sims[0], &m); if (m > most) most = m;
    }
    const CretFar* F = &sims[0]->far;
    fprintf(stderr, "far determinism: 2 sims, %d ticks, a person crossing, up to %d of %d folded; %d folds, %d unfolds, %d far hunts, %d far deaths\n",
            TICKS, most, N, F->folds, F->unfolds, F->hunts, F->deaths);
    CHECK(bad < 0, "lockstep sims with folding on diverged at tick %d", bad);
    CHECK(F->folds > 0 && F->unfolds > 0 && most > 0, "the determinism run did not both fold and unfold");
    for (int k = 0; k < 2; k++) { cret_sim_close(sims[k]); free(sims[k]); }
}

int main(void) {
    double t0 = t_now(); t_hold_world();
#define RUN_TEST(f) do { double _t = t_now(); f; fprintf(stderr, "  [%s: %.1f s]\n", #f, t_now() - _t); } while (0)
    RUN_TEST(test_fold_unfold());
    RUN_TEST(test_unfold_hard_ground());
    RUN_TEST(test_person_and_learned());
    RUN_TEST(test_far_determinism());
    return t_finish("test_far", t0);
}
