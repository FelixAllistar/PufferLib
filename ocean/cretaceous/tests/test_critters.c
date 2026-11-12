// Critter tests (critters.c): spawn density by kind and habitat, food from a
// catch scaling with the eater's size, fleeing from big animals, the large
// mammal biting back, determinism with critters on (scent trails included),
// scent (scent.c) and stalking (the watch cycle, smell on the wind, the
// snap), and a hatchling's first quest landing within 150 m (progress.c).
//   test_critters            the suite (make test, make asan)
#include "test_util.h"
#include "../progress.h"

// A sim whose slot 0 is a person (critters live around people), the rest bots.
static CretSim* crit_sim(int agents, int species, float growth, uint32_t seed) {
    CretConfig c = t_config(agents);
    c.player_growth = growth; c.player_species = species; c.seed = seed;
    CretSim* s = calloc(1, sizeof *s);
    if (cret_sim_init(s, c) != 0) { fprintf(stderr, "sim init failed\n"); exit(2); }
    return s;
}
static void crit_free(CretSim* s) { cret_sim_close(s); free(s); }
static void step_n(CretSim* s, int n, const CretInput* in0) {
    CretInput in[CRET_MAX_AGENTS];
    for (int t = 0; t < n; t++) {
        for (int i = 0; i < s->cfg.num_agents; i++) in[i] = i == 0 && in0 ? *in0 : (CretInput){0};
        cret_sim_step(s, in);
    }
}
// Puts agent i on dry walkable ground near (x, z), facing yaw, at rest.
static bool place(CretSim* s, int i, float x, float z, float yaw) {
    for (float r = 0; r < 400; r += 8)
        for (int q = 0; q < 16; q++) {
            float px = x + cosf(q * 0.39f) * r, pz = z + sinf(q * 0.39f) * r;
            if (!cret_walkable(s->world, px, pz) || cret_water(s->world, px, pz) > cret_height(s->world, px, pz)) continue;
            CretAgent* a = &s->agents[i];
            a->x = px; a->z = pz; a->y = cret_height(s->world, px, pz); a->yaw = yaw; a->vx = a->vy = a->vz = a->speed = 0; a->on_ground = true;
            cret_sim_place(s, i);
            return true;
        }
    return false;
}
// Puts a critter of `kind` (kg) at (x, z), standing; returns its slot.
static int put_critter(CretSim* s, int kind, float kg, float x, float z) {
    for (int k = 0; k < CRET_MAX_CRITTERS; k++) {
        CretCritter* c = &s->critters[k]; if (c->active) continue;
        *c = (CretCritter){.active = 1, .kind = (uint8_t)kind, .state = CRET_CRS_IDLE, .owner = 0, .threat = -1, .x = x, .z = z,
                           .y = cret_height(s->world, x, z), .kg = kg, .hp = CRET_CRITTER_KINDS[kind].hp, .timer = 3000};
        s->ncrit++;
        return k;
    }
    return -1;
}
// No spawning: the test places its own critters.
static void no_spawns(CretSim* s) { cret_critters_clear(s); s->crit_cfg.per_person = s->crit_cfg.fresh_per_person = 0; }

// ------------------------------------------------------------------ density
// Rings filled around a person at 40 spots: every critter on ground of its
// kind (fish in fresh shallows, the rest dry and walkable); mammals and lizards
// where plants are thicker than the ring's average, birds on open ground or
// among trees; small kinds far commoner than the large mammal (Damuth's
// law); a new hatchling's ring fuller and closer than an adult's.
static void test_density(void) {
    CretSim* s = crit_sim(1, CRET_SP_RAPTOR, 0.8f, 3);
    uint32_t rng = 99; double hab_at[CRET_CR_KINDS] = {0}, hab_ring[CRET_CR_KINDS] = {0}; int n_at[CRET_CR_KINDS] = {0}, n_ring = 0, bad = 0, total = 0;
    for (int spot = 0; spot < 40; spot++) {
        float W = s->world->size;
        place(s, 0, W * (0.15f + 0.7f * t_frand(&rng)), W * (0.15f + 0.7f * t_frand(&rng)), 0);
        cret_critters_clear(s); s->agents[0].food = s->agents[0].water = 1;
        step_n(s, 8, NULL);
        const CretAgent* a = &s->agents[0];
        for (int k = 0; k < CRET_MAX_CRITTERS; k++) {
            const CretCritter* c = &s->critters[k]; if (!c->active) continue;
            total++;
            float h = cret_critter_habitat(s, c->kind, c->x, c->z);
            if (h <= 0) bad++;
            hab_at[c->kind] += cret_forage(s->world, c->x, c->z); n_at[c->kind]++;
            float d = hypotf(c->x - a->x, c->z - a->z);
            CHECK_CAPPED(d >= s->crit_cfg.ring_min * 0.85f && d <= s->crit_cfg.despawn, 3, "critter %d m from the person", (int)d);
        }
        for (int t = 0; t < 64; t++) {   // the ring's own plant cover, for comparison
            float ang = t_frand(&rng) * 6.2831853f, d = 25 + 95 * t_frand(&rng), x = a->x + cosf(ang) * d, z = a->z + sinf(ang) * d;
            if (cret_critter_habitat(s, CRET_CR_LIZARD, x, z) <= 0) continue;
            hab_ring[0] += cret_forage(s->world, x, z); n_ring++;
        }
    }
    const CretCritterStats* st = &s->stats.critters;
    fprintf(stderr, "density: %d critters at 40 spots; spawned mouse %d, Repenomamus %d, lizard %d, bird %d, fish %d\n", total,
            st->spawned[0], st->spawned[1], st->spawned[2], st->spawned[3], st->spawned[4]);
    double ring = n_ring ? hab_ring[0] / n_ring : 0;
    fprintf(stderr, "  plant cover where they stand: ring %.2f, mouse %.2f, Repenomamus %.2f, lizard %.2f, bird %.2f\n", ring,
            n_at[0] ? hab_at[0] / n_at[0] : 0, n_at[1] ? hab_at[1] / n_at[1] : 0, n_at[2] ? hab_at[2] / n_at[2] : 0, n_at[3] ? hab_at[3] / n_at[3] : 0);
    CHECK(total > 400, "only %d critters at 40 spots", total);
    CHECK(bad == 0, "%d critters on ground their kind cannot live on", bad);
    CHECK(st->spawned[CRET_CR_MOUSE] > 3 * st->spawned[CRET_CR_BADGER], "small mammals (%d) not much commoner than large (%d)", st->spawned[0], st->spawned[1]);
    CHECK(st->spawned[CRET_CR_LIZARD] > 3 * st->spawned[CRET_CR_BADGER], "lizards (%d) vs large mammals (%d)", st->spawned[2], st->spawned[1]);
    CHECK(st->spawned[CRET_CR_BIRD] > 0 && st->spawned[CRET_CR_BADGER] > 0, "no birds or large mammals");
    CHECK(n_at[0] && hab_at[0] / n_at[0] > ring + 0.02, "mice not in thicker plants (%.2f vs %.2f)", n_at[0] ? hab_at[0] / n_at[0] : 0, ring);
    CHECK(n_at[3] && hab_at[3] / n_at[3] < hab_at[0] / fmax(n_at[0], 1), "birds not on more open ground than mice");
    // Habitat by construction: fish only in fresh shallows, land kinds never in water.
    int wet = 0, dry = 0;
    for (int t = 0; t < 20000; t++) {
        float x = s->world->size * t_frand(&rng), z = s->world->size * t_frand(&rng);
        bool fish = cret_critter_habitat(s, CRET_CR_FISH, x, z) > 0, land = cret_critter_habitat(s, CRET_CR_MOUSE, x, z) > 0;
        CHECK_CAPPED(!(fish && land), 3, "fish and mice both at %.0f %.0f", x, z);
        float d = cret_water(s->world, x, z) - cret_height(s->world, x, z);
        if (fish) { wet++; CHECK_CAPPED(d >= 0.15f && d <= 2.0f, 3, "fish habitat at depth %.2f", d); }
        if (land) { dry++; CHECK_CAPPED(d < 0.02f, 3, "land habitat under %.2f m of water", d); }
    }
    fprintf(stderr, "  habitat samples: %d fresh shallows, %d dry ground of 20000%s\n", wet, dry,
            wet ? "" : " (the test island has sea but no rivers or lakes: fish are checked on the full world by tools/critprobe.c)");
    CHECK(dry > 0, "no dry ground on the test island");
    CHECK(wet > 0 || st->spawned[CRET_CR_FISH] == 0, "%d fish spawned on an island without fresh shallows", st->spawned[CRET_CR_FISH]);
    // A fresh hatchling's ring is fuller and closer.
    int near_adult = 0, near_fresh = 0;
    for (int pass = 0; pass < 2; pass++) {
        CretSim* f = crit_sim(1, CRET_SP_RAPTOR, pass ? 0.35f : 0.8f, 5);
        place(f, 0, f->world->size * 0.5f, f->world->size * 0.45f, 0); cret_critters_clear(f);
        step_n(f, 90, NULL);
        for (int k = 0; k < CRET_MAX_CRITTERS; k++)
            if (f->critters[k].active && hypotf(f->critters[k].x - f->agents[0].x, f->critters[k].z - f->agents[0].z) < 60) { if (pass) near_fresh++; else near_adult++; }
        crit_free(f);
    }
    fprintf(stderr, "  within 60 m after 3 s: adult %d, fresh hatchling %d\n", near_adult, near_fresh);
    CHECK(near_fresh > 2 * near_adult && near_fresh >= 20, "a fresh hatchling has %d critters within 60 m (adult %d)", near_fresh, near_adult);
    crit_free(s);
}

// ------------------------------------------------------------------ food
// A real bite (sim.c resolve_attack) on a 2 kg mouse at the strike point:
// food rises by min(kg / (0.25 x body mass), 1 - food), credited to ev_food;
// a herbivore's bite kills it but feeds nothing.
static float bite_food(int species, float growth, float kg, float food0, bool* caught, float* room) {
    CretSim* s = crit_sim(1, species, growth, 11); no_spawns(s);
    place(s, 0, s->world->size * 0.5f, s->world->size * 0.5f, 0);
    CretAgent* a = &s->agents[0]; a->food = food0; a->water = 1; a->aim = 0;
    float px, pz; cret_strike_point(a, &px, &pz);
    int k = put_critter(s, CRET_CR_MOUSE, kg, px, pz);
    s->critters[k].react = 255; s->critters[k].state = CRET_CRS_HIDE;   // stays put
    CretInput in = {.action = CRET_ACT_ATTACK}; float got = 0;
    CretInput in1[CRET_MAX_AGENTS] = {in}; cret_sim_step(s, in1); got += a->ev_food;
    *room = 1 - a->food;
    for (int t = 0; t < 60 && s->critters[k].active; t++) { *room = 1 - a->food; step_n(s, 1, NULL); got += a->ev_food; }   // room: what the stomach held just before the bite
    *caught = !s->critters[k].active;
    crit_free(s);
    return got;
}
static void test_food_scaling(void) {
    struct { int sp; float g; const char* name; } eaters[] = {{CRET_SP_RAPTOR, 0.35f, "hatchling raptor"}, {CRET_SP_RAPTOR, 1.0f, "adult raptor"},
                                                              {CRET_SP_TREX, 0.35f, "hatchling tyrannosaur"}, {CRET_SP_TREX, 1.0f, "adult tyrannosaur"}};
    for (int e = 0; e < 4; e++) {
        bool caught; float room; float got = bite_food(eaters[e].sp, eaters[e].g, 2.0f, 0.1f, &caught, &room);
        float mass = CRET_SPECIES_TABLE[eaters[e].sp].mass * powf(cret_growth_scale(eaters[e].g), 3);
        float want = fminf(2.0f / (0.25f * mass), room);   // capped at a full stomach
        fprintf(stderr, "food: %-21s (%6.0f kg) catches a 2 kg mouse: +%.4f food (formula %.4f)\n", eaters[e].name, mass, got, want);
        CHECK(caught, "%s missed a mouse at its strike point", eaters[e].name);
        CHECK(fabsf(got - want) < 1e-4f, "%s gained %.5f, formula %.5f", eaters[e].name, got, want);
    }
    bool caught; float room; float got = bite_food(CRET_SP_PACHY, 0.35f, 2.0f, 0.1f, &caught, &room);
    CHECK(caught && got == 0, "a herbivore's bite: caught %d, food %.4f (wants caught, no food)", caught, got);
    // The HUD prompt: prey in reach of a bite, then prey a few metres off.
    CretSim* s = crit_sim(1, CRET_SP_RAPTOR, 0.35f, 12); no_spawns(s);
    place(s, 0, s->world->size * 0.4f, s->world->size * 0.5f, 0);
    CretAgent* a = &s->agents[0]; float px, pz, d = 0; cret_strike_point(a, &px, &pz);
    int k = put_critter(s, CRET_CR_LIZARD, 0.5f, px, pz);
    CHECK(cret_food_state(s, 0, &d) == 3, "food state %d with a lizard at the jaws (wants 3)", cret_food_state(s, 0, &d));
    s->critters[k].x = a->x + 9; s->critters[k].z = a->z;
    CHECK(cret_food_state(s, 0, &d) == 4 && fabsf(d - 9) < 0.5f, "food state %d (%.1f m) with a lizard 9 m off (wants 4)", cret_food_state(s, 0, &d), d);
    // Fish: only an animal standing in the water catches one (the test island
    // has no fresh shallows, so the fish is put at the jaws by hand).
    s->critters[k].active = 0; s->ncrit--;
    int f = put_critter(s, CRET_CR_FISH, 4.0f, px, pz);
    CHECK(cret_critter_in_reach(s, 0, px, pz, 1.0f, NULL) < 0, "a fish was in reach of an animal on dry land");
    a->swimming = true;
    CHECK(cret_critter_in_reach(s, 0, px, pz, 1.0f, NULL) == f, "a fish was out of reach of an animal in the water");
    a->swimming = false;
    crit_free(s);
}

// ------------------------------------------------------------------ fleeing
// An adult raptor trotting at a mouse, a lizard and a bird: each notices it
// within its range, reacts after its delay and gets away (the bird flies off
// and is gone). A juvenile walking up gets nearer before it is noticed than
// one trotting.
static float approach(int kind, float growth, float forward, int* state_seen, float* gap_end) {
    CretSim* s = crit_sim(1, CRET_SP_RAPTOR, growth, 21); no_spawns(s);
    *state_seen = 0; *gap_end = 0;
    // An open run: dry ground all the way to 25 m ahead.
    uint32_t rng = 5; int k = -1; CretAgent* a = &s->agents[0];
    for (int t = 0; t < 400 && k < 0; t++) {
        float W = s->world->size, x = W * (0.2f + 0.6f * t_frand(&rng)), z = W * (0.2f + 0.6f * t_frand(&rng)), yaw = t_frand(&rng) * 6.2831853f;
        bool clear = true;
        for (float r = 0; r <= 25 && clear; r += 1) clear = cret_critter_habitat(s, CRET_CR_MOUSE, x + cosf(yaw) * r, z + sinf(yaw) * r) > 0;
        if (!clear || !place(s, 0, x, z, yaw) || hypotf(a->x - x, a->z - z) > 0.1f) continue;
        k = put_critter(s, kind, 1.0f, x + cosf(yaw) * 22, z + sinf(yaw) * 22);
    }
    a->food = a->water = 1;
    if (k < 0) { crit_free(s); return -1; }
    s->critters[k].timer = 3000;
    CretInput in = {.forward = forward}; float noticed = -1; *state_seen = 0;
    for (int t = 0; t < 360; t++) {
        step_n(s, 1, &in);
        const CretCritter* c = &s->critters[k];
        if (!c->active) { *state_seen |= 1 << CRET_CRS_FLY; break; }
        if (c->state != CRET_CRS_IDLE && c->state != CRET_CRS_WANDER && noticed < 0) noticed = hypotf(c->x - a->x, c->z - a->z);
        *state_seen |= 1 << c->state;
    }
    const CretCritter* c = &s->critters[k];
    *gap_end = c->active ? hypotf(c->x - a->x, c->z - a->z) : 99;
    crit_free(s);
    return noticed;
}
static void test_flee(void) {
    const char* names[] = {"mouse", "", "lizard", "bird"};
    int kinds[] = {CRET_CR_MOUSE, CRET_CR_LIZARD, CRET_CR_BIRD};
    for (int q = 0; q < 3; q++) {
        int seen; float gap, d = approach(kinds[q], 1.0f, 1.0f, &seen, &gap);
        fprintf(stderr, "flee: an adult raptor trots at a %s: noticed at %.1f m, states seen 0x%x, %.1f m apart after 12 s\n", names[kinds[q]], d, seen, gap);
        CHECK(d > 2.0f, "%s noticed the raptor only at %.1f m", names[kinds[q]], d);
        if (kinds[q] == CRET_CR_BIRD) CHECK(seen & (1 << CRET_CRS_FLY), "the bird never flew off");
        else CHECK(seen & (1 << CRET_CRS_FLEE), "%s never fled", names[kinds[q]]);
        if (kinds[q] == CRET_CR_MOUSE) CHECK(seen & (1 << CRET_CRS_HIDE), "the mouse never hid");
    }
    int seen; float gap, run = approach(CRET_CR_MOUSE, 0.6f, 1.0f, &seen, &gap), walk = approach(CRET_CR_MOUSE, 0.6f, 0.5f, &seen, &gap);
    fprintf(stderr, "  a juvenile noticed at %.1f m trotting, %.1f m walking\n", run, walk);
    CHECK(walk > 0 && walk < run, "walking up (%.1f m) is not quieter than trotting (%.1f m)", walk, run);
}

// ------------------------------------------------------------------ biting back
// A Repenomamus next to a hatchling raptor stands and bites: a few percent
// of its health a bite plus a little bleeding; next to an adult tyrannosaur
// a bite is nothing. It takes three bites from a hatchling raptor and two from
// an adult raptor.
static void badger_vs(int species, float growth, float* frac, int* bites, int* to_kill) {
    CretSim* s = crit_sim(1, species, growth, 31); no_spawns(s);
    place(s, 0, s->world->size * 0.5f, s->world->size * 0.55f, 0);
    CretAgent* a = &s->agents[0]; a->food = a->water = 1;
    float px, pz; cret_strike_point(a, &px, &pz);
    int k = put_critter(s, CRET_CR_BADGER, 12.0f, px + 0.3f, pz);
    float h0 = a->health;
    step_n(s, 150, NULL);   // 5 s of standing next to it
    *frac = (h0 - a->health) / a->max_health; *bites = s->stats.critters.bites_taken;
    if (a->bleed <= 0 && *bites) *frac = -1;   // bites must bleed
    // Bite it until it dies.
    *to_kill = 0; a->health = a->max_health;
    for (int t = 0; t < 600 && s->critters[k].active; t++) {
        const CretCritter* c = &s->critters[k];
        a->aim = cret_aim_at(a, c->x, c->z); a->health = a->max_health; a->bleed = 0;
        CretInput in = {.action = a->attack_cd == 0 ? CRET_ACT_ATTACK : 0, .aim = a->aim, .turn = 0};
        float hp0 = c->hp; step_n(s, 1, &in);
        if (!s->critters[k].active || s->critters[k].hp < hp0) (*to_kill)++;
        if (s->critters[k].active) { cret_strike_point(a, &px, &pz); s->critters[k].x = px; s->critters[k].z = pz; }   // keep it at the jaws
    }
    crit_free(s);
}
static void test_bites_back(void) {
    float f; int bites, kill;
    badger_vs(CRET_SP_RAPTOR, 0.35f, &f, &bites, &kill);
    fprintf(stderr, "bites back: a hatchling raptor next to a 12 kg Repenomamus for 5 s: %d bites, %.1f%% of its health; killed in %d bites\n", bites, f * 100, kill);
    CHECK(bites >= 2, "the large mammal bit a hatchling only %d times in 5 s", bites);
    CHECK(f > 0.05f * bites && f < 0.25f * bites, "%d bites took %.1f%% of a hatchling's health (wants 5-25%% a bite, with bleeding)", bites, f * 100);
    CHECK(kill == 3, "a hatchling raptor took %d bites to kill it (wants 3)", kill);
    badger_vs(CRET_SP_RAPTOR, 1.0f, &f, &bites, &kill);
    fprintf(stderr, "  an adult raptor: %d bites, %.2f%% of its health; killed in %d bites\n", bites, f * 100, kill);
    CHECK(kill == 2, "an adult raptor took %d bites (wants 2)", kill);
    badger_vs(CRET_SP_TREX, 1.0f, &f, &bites, &kill);
    fprintf(stderr, "  an adult tyrannosaur: %d bites, %.3f%% of its health; killed in %d bites\n", bites, f * 100, kill);
    CHECK(f < 0.005f, "an adult tyrannosaur lost %.2f%% to it", f * 100);
    CHECK(kill == 1, "an adult tyrannosaur took %d bites (wants 1)", kill);
}

// ------------------------------------------------------------------ determinism
// Two sims with a hatchling raptor person (a bot drives it, and hunger is
// short so it hunts critters) and 23 bots, stepped in lockstep with critters
// on: the state hash, which covers every active critter, matches every tick.
static void test_determinism(void) {
    enum { TICKS = 4000 };
    CretSim* A = crit_sim(24, CRET_SP_RAPTOR, 0.35f, 41); CretSim* B = crit_sim(24, CRET_SP_RAPTOR, 0.35f, 41);
    A->cfg.hunger_seconds = B->cfg.hunger_seconds = 240; A->agents[0].food = B->agents[0].food = 0.4f;   // hungry: it hunts critters at once
    int bad = -1, bad_trail = -1, peak = 0; CretInput ia[CRET_MAX_AGENTS], ib[CRET_MAX_AGENTS];
    for (int t = 0; t < TICKS; t++) {
        for (int i = 0; i < 24; i++) { ia[i] = cret_bot_input(A, i); ib[i] = cret_bot_input(B, i); }
        cret_sim_step(A, ia); cret_sim_step(B, ib);
        if (bad < 0 && t_sim_hash(A) != t_sim_hash(B)) bad = t;
        if (bad_trail < 0 && (memcmp(A->crit_trail, B->crit_trail, sizeof A->crit_trail) || memcmp(A->agent_trail, B->agent_trail, sizeof A->agent_trail))) bad_trail = t;
        if (A->ncrit > peak) peak = A->ncrit;
        int n = 0; for (int k = 0; k < CRET_MAX_CRITTERS; k++) n += A->critters[k].active;
        CHECK_CAPPED(n == A->ncrit, 3, "tick %d: %d active critters, ncrit %d", t, n, A->ncrit);
        for (int k = 0; k < CRET_MAX_CRITTERS; k++) {
            const CretCritter* c = &A->critters[k]; if (!c->active) continue;
            CHECK_CAPPED(isfinite(c->x) && isfinite(c->y) && isfinite(c->z) && c->kind < CRET_CR_KINDS && c->state < CRET_CRS_COUNT, 3, "tick %d critter %d invalid", t, k);
        }
    }
    const CretCritterStats* st = &A->stats.critters;
    fprintf(stderr, "determinism: 2 sims with critters, %d ticks: %s; peak %d critters, %d eaten, %d bites taken, %d birds flushed\n", TICKS,
            bad < 0 ? "identical" : "DIVERGED", peak, st->eaten[0] + st->eaten[1] + st->eaten[2] + st->eaten[3] + st->eaten[4], st->bites_taken, st->flushed);
    CHECK(bad < 0, "lockstep sims with critters diverged at tick %d", bad);
    int trails = 0; for (int k = 0; k < CRET_MAX_CRITTERS; k++) trails += A->crit_trail[k].n > 1;
    fprintf(stderr, "  scent trails: %s; %d critters with a trail at the end, the person's has %d points\n", bad_trail < 0 ? "identical" : "DIVERGED", trails, A->agent_trail[0].n);
    CHECK(bad_trail < 0, "lockstep scent trails diverged at tick %d", bad_trail);
    CHECK(trails > 10 && A->agent_trail[0].n >= 2, "only %d critter trails recorded (person: %d points)", trails, A->agent_trail[0].n);
    CHECK(peak > 40, "only %d critters around the person", peak);
    CHECK(st->eaten[0] + st->eaten[1] + st->eaten[2] + st->eaten[3] + st->eaten[4] > 0, "the hatchling bot caught nothing in %d ticks", TICKS);
    crit_free(A); crit_free(B);
    // No people (the trainer): no critters, and the rng is untouched by them.
    CretConfig c = t_config(16); CretSim* T = calloc(1, sizeof *T); cret_sim_init(T, c);
    CretInput in[CRET_MAX_AGENTS];
    for (int t = 0; t < 300; t++) { for (int i = 0; i < 16; i++) in[i] = cret_bot_input(T, i); cret_sim_step(T, in); }
    CHECK(T->ncrit == 0, "%d critters in a sim without people", T->ncrit);
    int tn = 0; for (int i = 0; i < 16; i++) tn += T->agent_trail[i].n;
    CHECK(tn == 0, "a sim without people recorded %d trail points", tn);
    crit_free(T);
}

// ------------------------------------------------------------------ scent
// The smell of a source carries downwind (scent.h): a hatchling raptor
// smells a critter 8 m straight downwind of it while sniffing, less across
// the wind, nothing upwind, and nothing at 8 m when not sniffing (0.35 of the
// range). A trail point fades with age and is gone after 45 s; rain shortens
// both. Herbivores have no nose for it. Another animal is smelled only by
// its trail, at least 3 s behind it.
static void test_scent(void) {
    CretSim* s = crit_sim(2, CRET_SP_RAPTOR, 0.35f, 5); no_spawns(s);
    s->cfg.cloud_cover = s->cloud_cover = 0.3f;
    CretAgent* a = &s->agents[0]; a->food = a->water = 1;
    float wx, wz, w; cret_wind(s, &wx, &wz, &w);
    float x0 = a->x, z0 = a->z, R[3]; cret_scent_ranges(s, 0, true, R);
    // Sources 8 m from the nose: downwind of it the nose sits downwind of the source.
    float down = cret_scent_strength(s, 0, true, CRET_SCENT_CRITTER, x0 - wx * 8, z0 - wz * 8, 0, 1);
    float across = cret_scent_strength(s, 0, true, CRET_SCENT_CRITTER, x0 - wz * 8, z0 + wx * 8, 0, 1);
    float up = cret_scent_strength(s, 0, true, CRET_SCENT_CRITTER, x0 + wx * 8, z0 + wz * 8, 0, 1);
    float passive = cret_scent_strength(s, 0, false, CRET_SCENT_CRITTER, x0 - wx * 8, z0 - wz * 8, 0, 1);
    fprintf(stderr, "scent: hatchling raptor ranges %.1f / %.0f / %.0f m; a critter 8 m off: downwind %.2f, across %.2f, upwind %.2f, not sniffing %.2f\n",
            R[0], R[1], R[2], down, across, up, passive);
    CHECK(R[0] > 15 && R[0] < 20, "a hatchling raptor smells critters at %.1f m (wants about 18)", R[0]);
    CHECK(down > across && across > 0 && up == 0, "downwind %.2f, across %.2f, upwind %.2f (wants down > across > 0, upwind 0)", down, across, up);
    CHECK(passive == 0, "smelled a critter 8 m off without sniffing (%.2f)", passive);
    float fresh = cret_scent_strength(s, 0, true, CRET_SCENT_ANIMAL, x0 - wx * 10, z0 - wz * 10, 2, 1);
    float old = cret_scent_strength(s, 0, true, CRET_SCENT_ANIMAL, x0 - wx * 10, z0 - wz * 10, 30, 1);
    float gone = cret_scent_strength(s, 0, true, CRET_SCENT_ANIMAL, x0 - wx * 10, z0 - wz * 10, 46, 1);
    CHECK(fresh > old && old > 0 && gone == 0, "trail point 2 s old %.2f, 30 s %.2f, 46 s %.2f (wants fading, gone after 45 s)", fresh, old, gone);
    s->cloud_cover = 0.97f;
    float rain_old = cret_scent_strength(s, 0, true, CRET_SCENT_ANIMAL, x0 - wx * 10, z0 - wz * 10, 30, 1), Rr[3]; cret_scent_ranges(s, 0, true, Rr);
    CHECK(rain_old == 0 && Rr[0] < 0.6f * R[0], "in rain a 30 s trail still smells %.2f, range %.1f m (dry %.1f)", rain_old, Rr[0], R[0]);
    s->cloud_cover = 0.3f;
    // Another animal walks past downwind of the nose: smelled by its trail, never where it stands.
    CretAgent* b = &s->agents[1]; b->species = CRET_SP_PACHY; b->growth = 1; cret_apply_growth(b); b->alive = true;
    place(s, 1, x0 - wx * 20 - wz * 8, z0 - wz * 20 + wx * 8, atan2f(wx, -wz));
    CretInput in[2] = {{0}, {.forward = 0.5f}};
    CretScent sc; int hits = 0; float dmin = 1e9f;
    for (int t = 0; t < 12 * CRET_TICK_HZ; t++) {
        a->x = x0; a->z = z0; cret_sim_step(s, in);
        if (t % 15) continue;
        cret_scent_query(s, 0, true, &sc);
        for (int q = 0; q < sc.n; q++) if (sc.hit[q].what == CRET_SCENT_ANIMAL) {
            hits++; CHECK_CAPPED(sc.hit[q].age >= CRET_SCENT_MIN_AGE, 3, "a trail point %.1f s old was smelled", sc.hit[q].age);
            dmin = fminf(dmin, hypotf(sc.hit[q].x - b->x, sc.hit[q].z - b->z));
        }
    }
    fprintf(stderr, "  a pachycephalosaur walking past downwind: smelled %d times, its freshest smelled point never nearer it than %.1f m\n", hits, dmin);
    CHECK(hits > 0, "a pachycephalosaur's trail 8-20 m downwind was never smelled");
    CHECK(dmin > 2.0f, "the trail led to %.1f m from the animal", dmin);
    float Rh[3]; s->agents[1].species = CRET_SP_STEGO; cret_scent_ranges(s, 1, true, Rh);
    CHECK(Rh[0] == 0 && Rh[1] == 0 && Rh[2] == 0, "a stegosaur smells prey (%.0f %.0f %.0f m)", Rh[0], Rh[1], Rh[2]);
    crit_free(s);
}

// ------------------------------------------------------------------ stalking
// A critter's watch alternates head up and nose down; a hatchling raptor
// standing still 9 m upwind of a mouse (its smell blowing onto it) is noticed,
// one 9 m downwind is not; a bite catches a mouse at the snap
// (CRET_SNAP_TICKS), long before the full bite would land.
static void test_stalk(void) {
    CretSim* s = crit_sim(1, CRET_SP_RAPTOR, 0.35f, 9); no_spawns(s);
    s->cfg.cloud_cover = s->cloud_cover = 0.3f;
    CretAgent* a = &s->agents[0]; a->food = a->water = 1;
    float wx, wz, w; cret_wind(s, &wx, &wz, &w);
    int k = put_critter(s, CRET_CR_MOUSE, 1.0f, a->x + 3, a->z), up = 0, down = 0;
    for (int t = 0; t < 10 * CRET_TICK_HZ; t++) { float v = cret_critter_watch(s, k); up += v > 0; down += v == 0; s->tick++; }
    CHECK(up > 0 && down > up, "a feeding mouse watched %d ticks and fed %d of 300 (wants both, feeding longer)", up, down);
    // Smell: still raptor 9 m upwind of the mouse, then 9 m downwind.
    int fled[2] = {0, 0};
    for (int side = 0; side < 2; side++) {
        no_spawns(s); float sg = side ? -1.0f : 1.0f;
        float mx = a->x + sg * wx * 9, mz = a->z + sg * wz * 9;   // side 0: the mouse downwind of the raptor
        k = put_critter(s, CRET_CR_MOUSE, 1.0f, mx, mz); s->critters[k].y = cret_height(s->world, mx, mz);
        for (int t = 0; t < 5 * CRET_TICK_HZ; t++) { step_n(s, 1, NULL); fled[side] |= s->critters[k].state == CRET_CRS_FLEE; }
    }
    fprintf(stderr, "stalk: a still hatchling 9 m upwind of a mouse %s it, 9 m downwind %s\n", fled[0] ? "scared" : "did not scare", fled[1] ? "scared" : "did not scare");
    CHECK(fled[0], "a mouse 9 m downwind of a hatchling raptor did not smell it");
    CHECK(!fled[1], "a mouse 9 m upwind of a still hatchling raptor noticed it");
    // The snap: a mouse at the jaws is caught CRET_SNAP_TICKS into the bite.
    no_spawns(s); float px, pz; cret_strike_point(a, &px, &pz);
    k = put_critter(s, CRET_CR_MOUSE, 1.0f, px, pz); s->critters[k].state = CRET_CRS_HIDE;
    int eaten0 = s->stats.critters.eaten[CRET_CR_MOUSE], at = -1;
    CretInput bite = {.action = CRET_ACT_ATTACK};
    for (int t = 0; t < 20 && at < 0; t++) { step_n(s, 1, t ? NULL : &bite); if (s->stats.critters.eaten[CRET_CR_MOUSE] > eaten0) at = t; }
    int hit = (int)(CRET_SPECIES_TABLE[CRET_SP_RAPTOR].hit_time * CRET_TICK_HZ);
    fprintf(stderr, "  a mouse at the jaws caught %d ticks into the bite (the full bite lands at %d)\n", at, hit);
    CHECK(at >= 0 && at <= CRET_SNAP_TICKS && at < hit, "a mouse at the jaws caught at tick %d (wants by %d)", at, CRET_SNAP_TICKS);
    crit_free(s);
}

// ------------------------------------------------------------------ first quest
// A person spawned as a hatchling gets a quest within 150 m (its first step:
// catch small prey for predators, graze for herbivores); an adult past its
// first steps gets the adult band.
static void test_first_quest(void) {
    int species[] = {CRET_SP_RAPTOR, CRET_SP_TREX, CRET_SP_STEGO, CRET_SP_GALLI}; int ok = 0, n = 0;
    for (int q = 0; q < 4; q++)
        for (uint32_t seed = 1; seed <= 4; seed++) {
            CretSim* s = crit_sim(12, species[q], 0.35f, seed);
            CretInput in[CRET_MAX_AGENTS];
            for (int t = 0; t < 10 * CRET_TICK_HZ && !cret_quest_of(s, 0, 0); t++) { for (int i = 0; i < 12; i++) in[i] = i ? cret_bot_input(s, i) : (CretInput){0}; cret_sim_step(s, in); }
            const CretQuest* qs = cret_quest_of(s, 0, 0); float x, z, d = -1; n++;
            if (qs && cret_quest_target(s, qs, &x, &z, NULL)) d = hypotf(x - s->agents[0].x, z - s->agents[0].z);
            bool carn = CRET_SPECIES_TABLE[species[q]].diet == CRET_DIET_CARNIVORE;
            bool kind_ok = qs && qs->spot && (carn ? qs->kind == CRET_Q_CATCH || qs->kind == CRET_Q_DRINK : qs->kind == CRET_Q_GATHER || qs->kind == CRET_Q_DRINK);
            CHECK(qs && kind_ok && d >= 0 && d <= 150, "%s hatchling (seed %u): quest %d, %.0f m away", CRET_SPECIES_TABLE[species[q]].name, seed, qs ? qs->kind : -1, d);
            ok += qs && kind_ok && d >= 0 && d <= 150;
            if (seed == 1) { char title[64], prog[32]; if (qs) cret_quest_text(s, qs, title, sizeof title, prog, sizeof prog); else title[0] = 0;
                             fprintf(stderr, "first quest: %-14s \"%s\" %.0f m away\n", CRET_SPECIES_TABLE[species[q]].name, title, d); }
            crit_free(s);
        }
    fprintf(stderr, "  %d of %d hatchlings got a quest within 150 m\n", ok, n);
    // An adult person past its first steps gets an ordinary quest, 250-500 m off when it leads somewhere.
    CretSim* s = crit_sim(12, CRET_SP_RAPTOR, 1.0f, 2); CretInput in[CRET_MAX_AGENTS] = {{0}};
    s->prog[0].chain = CRET_CHAIN_STEPS;
    for (int t = 0; t < 90 * CRET_TICK_HZ && !cret_quest_of(s, 0, 0); t++) cret_sim_step(s, in);
    const CretQuest* qs = cret_quest_of(s, 0, 0); float x, z, d = -1;
    if (qs && cret_quest_target(s, qs, &x, &z, NULL)) d = hypotf(x - s->agents[0].x, z - s->agents[0].z);
    CHECK(qs && !qs->chain && (d < 0 || (d >= 250 && d <= 500)), "an adult got quest %d (chain %d) %.0f m away", qs ? qs->kind : -1, qs ? qs->chain : -1, d);
    crit_free(s);
}

// ------------------------------------------------------------------ cost
static void test_cost(void) {
    CretSim* s = crit_sim(48, CRET_SP_RAPTOR, 0.8f, 51); s->crit_cfg.per_person = 128;
    CretInput in[CRET_MAX_AGENTS];
    for (int t = 0; t < 300; t++) { for (int i = 0; i < 48; i++) in[i] = cret_bot_input(s, i); cret_sim_step(s, in); }
    double best = 1e9;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = t_now();
        for (int t = 0; t < 200; t++) cret_critters_step(s);
        best = fmin(best, (t_now() - t0) / 200 * 1000);
    }
    fprintf(stderr, "cost: cret_critters_step with %d critters and 48 animals: %.4f ms a tick\n", s->ncrit, best);
    CHECK(s->ncrit >= 100, "only %d critters for the cost run", s->ncrit);
    // Target 0.1 ms; loose for loaded boxes. Under ASan + UBSan the same code runs several times slower.
#if defined(__SANITIZE_ADDRESS__) || (defined(__has_feature) && __has_feature(address_sanitizer))
    double limit = 2.5;
#else
    double limit = 0.25;
#endif
    CHECK(best < limit, "critters cost %.3f ms a tick (limit %.2f)", best, limit);
    crit_free(s);
}

int main(int argc, char** argv) {
    double t0 = t_now(); t_hold_world();
#define RUN_TEST(f) do { double _t = t_now(); f; fprintf(stderr, "  [%s: %.1f s]\n", #f, t_now() - _t); } while (0)
    RUN_TEST(test_density());
    RUN_TEST(test_food_scaling());
    RUN_TEST(test_flee());
    RUN_TEST(test_bites_back());
    RUN_TEST(test_determinism());
    RUN_TEST(test_scent());
    RUN_TEST(test_stalk());
    RUN_TEST(test_first_quest());
    RUN_TEST(test_cost());
    return t_finish("test_critters", t0);
}
