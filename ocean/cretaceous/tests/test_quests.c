// Quest tests (progress.c, character.c): every quest handed out is feasible
// for the animal that gets it, distances follow the growth stage, the same
// kind rarely comes twice in a row, a new character's first steps come in
// order and carry over death and a save and load, bots skip them, and the
// quest state stays deterministic.
//   test_quests            the suite (make test, make asan)
#include "test_util.h"
#include "../progress.h"
#include "../character.h"
#include <unistd.h>

static CretSim* q_sim(int agents, int species, float growth, uint32_t seed) {
    CretConfig c = t_config(agents);
    c.player_growth = growth; c.player_species = species; c.seed = seed;
    c.hunger_seconds = c.thirst_seconds = 1e6f;   // reserves never cut a quest short here
    CretSim* s = calloc(1, sizeof *s);
    if (cret_sim_init(s, c) != 0) { fprintf(stderr, "sim init failed\n"); exit(2); }
    return s;
}
static void q_free(CretSim* s) { cret_sim_close(s); free(s); }
static void bots_step(CretSim* s, const CretInput* in0) {
    CretInput in[CRET_MAX_AGENTS];
    for (int i = 0; i < s->cfg.num_agents; i++) in[i] = i == 0 && in0 ? *in0 : cret_bot_input(s, i);
    cret_sim_step(s, in);
}
static float mass_of(const CretAgent* a) { float g = a->growth; return CRET_SPECIES_TABLE[a->species].mass * g * g * g; }
static float dist(const CretAgent* a, float x, float z) { return hypotf(x - a->x, z - a->z); }

// ------------------------------------------------------------------ feasibility, distances, repeats
// Animals of every species at every stage (slot 0 a person past its first
// steps, the rest bots of the default mix at the same growth) get quests over
// and over (each is withdrawn 8 s after it is offered). Every new personal
// quest is checked when it appears:
//   - hunt: a living animal of the prey species weighs at most 3 times the
//     hunter (times its group's mass for a group quest), never a grown
//     sauropelta for a raptor; a hatchling raptor never hunts;
//   - defend: only herbivores of 0.6 growth and up, never a gallimimus;
//   - places and spots (drink, graze, rest, explore, catch, patrol's first
//     leg): within the stage's band (70-150, 150-300, 250-500 m);
//     carcasses, nests and companions within its far end;
//   - nest: grown animals without a nest; guard: an own nest with eggs;
//   - no first steps for bots or for a character past them.
// Immediate repeats of a kind are counted for slot 0 and the bots that choose
// their own errands, but not tyrannosaurs: on the small test island (few
// places, no lakes, no hunger) a lone tyrannosaur often has nothing feasible
// but a hunt, so its repeats are forced. tools/questprobe.c measures repeats
// on the full island.
static void test_feasible(void) {
    static const float G[3] = {0.35f, 0.65f, 1.0f};
    int offered = 0, bad = 0, repeats = 0, pairs = 0, kinds[CRET_Q_COUNT] = {0}, hatch_hunt = 0, ghunts = 0;
    for (int st = 0; st < 3; st++)
        for (int sp = 0; sp < CRET_SPECIES; sp++) {
            CretSim* s = q_sim(24, sp, G[st], 100 + st * 10 + sp);
            s->cfg.spawn_growth = G[st];
            for (int i = 0; i < s->cfg.num_agents; i++) cret_sim_respawn(s, i);
            s->prog[0].chain = CRET_CHAIN_STEPS;
            CretQuest last[CRET_MAX_AGENTS]; memset(last, 0, sizeof last); int prev_kind[CRET_MAX_AGENTS] = {0}, gexp[CRET_MAX_AGENTS + 1] = {0};
            for (int t = 0; t < 150 * CRET_TICK_HZ; t++) {
                bots_step(s, NULL);
                for (int i = 0; i < s->cfg.num_agents; i++) {
                    const CretAgent* a = &s->agents[i]; const CretQuest* q = &s->prog[i].quest;
                    if (!a->alive || !q->kind || (q->kind == last[i].kind && q->expires == last[i].expires)) continue;
                    last[i] = *q; offered++; kinds[q->kind]++;
                    float lo, hi, x, z; cret_quest_band(a->growth, &lo, &hi);
                    const char* why = NULL;
                    if (q->chain) why = "first steps for a bot or a character past them";
                    else if (q->kind == CRET_Q_HUNT) {
                        bool ok = false;
                        for (int j = 0; j < s->cfg.num_agents; j++) {
                            const CretAgent* b = &s->agents[j];
                            if (!b->alive || b->species != q->species || CRET_SPECIES_TABLE[b->species].diet != CRET_DIET_HERBIVORE) continue;
                            if (a->species == CRET_SP_RAPTOR && b->species == CRET_SP_SAUROPELTA && b->growth > 0.6f) continue;
                            ok |= mass_of(b) <= 3.0f * mass_of(a) + 1e-3f;
                        }
                        if (!ok) why = "hunt: no prey within 3x the hunter's mass";
                        if (a->species == CRET_SP_RAPTOR && a->growth < 0.5f) { why = "a hatchling raptor hunts"; hatch_hunt++; }
                    } else if (q->kind == CRET_Q_DEFEND) {
                        if (a->growth < 0.6f || a->species == CRET_SP_GALLI || CRET_SPECIES_TABLE[a->species].diet != CRET_DIET_HERBIVORE) why = "defend: too young or cannot fight";
                    } else if (q->kind == CRET_Q_NEST) {
                        if (a->growth < 0.9f || cret_nest_of(s, i) >= 0) why = "nest: not grown, or has one";
                    } else if (q->kind == CRET_Q_GUARD) {
                        int k = cret_nest_of(s, i); if (k < 0 || s->nests[k].eggs <= 0) why = "guard: no nest with eggs";
                    } else if ((i == 0 || !a->group) && cret_quest_target(s, q, &x, &z, NULL)) {   // a pack or herd bot may take on its leader's errand, measured from the leader
                        float d = dist(a, x, z);
                        bool band = q->kind == CRET_Q_DRINK || q->kind == CRET_Q_GATHER || q->kind == CRET_Q_REST || q->kind == CRET_Q_EXPLORE || q->kind == CRET_Q_CATCH || q->kind == CRET_Q_PATROL;
                        if (band && (d < lo - 0.5f || d > hi + 0.5f)) why = "place outside the stage's band";
                        if (!band && d > hi + 0.5f) why = "target beyond the stage's far end";
                    }
                    CHECK_CAPPED(!why, 10, "%s %.2f (slot %d): %s: %s", CRET_SPECIES_TABLE[a->species].name, a->growth, i, cret_quest_kind_name(q->kind), why);
                    bad += why != NULL;
                    if ((i == 0 || !a->group) && a->species != CRET_SP_TREX) {   // pack and herd bots follow their leader's errand
                        if (prev_kind[i]) { pairs++; repeats += prev_kind[i] == q->kind; }
                        prev_kind[i] = q->kind;
                    }
                }
                // Turn quests over quickly: each slot's quest is withdrawn every 8 s (staggered by slot).
                for (int i = 0; i < s->cfg.num_agents; i++)
                    if (s->prog[i].quest.kind && (s->tick + i * 7) % (8 * CRET_TICK_HZ) == 0) cret_quest_reset(s, i);
                // A new group hunt: prey within the group's combined mass ratio.
                for (int g = 1; g <= s->cfg.num_agents; g++) {
                    const CretQuest* q = &s->gquest[g];
                    if (q->kind != CRET_Q_HUNT || q->expires == gexp[g]) continue;
                    gexp[g] = q->expires; ghunts++;
                    float m = 0; const CretAgent* h = NULL;
                    for (int j = 0; j < s->cfg.num_agents; j++) if (s->agents[j].alive && s->agents[j].group == g) { m += mass_of(&s->agents[j]); h = &s->agents[j]; }
                    bool ok = false;
                    for (int j = 0; h && j < s->cfg.num_agents; j++) ok |= s->agents[j].species == q->species && cret_quest_prey_ok(s, h, m, &s->agents[j]);
                    CHECK_CAPPED(ok, 5, "group %d hunts %s with no prey within its mass ratio", g, CRET_SPECIES_TABLE[q->species].name);
                }
            }
            q_free(s);
        }
    fprintf(stderr, "feasibility: %d quests offered (and %d group hunts), %d infeasible; immediate repeats %d of %d (%.0f%%); kinds:", offered, ghunts, bad, repeats, pairs, 100.0 * repeats / (pairs ? pairs : 1));
    for (int k = 1; k < CRET_Q_COUNT; k++) if (kinds[k]) fprintf(stderr, " %s %d", cret_quest_kind_name(k), kinds[k]);
    fprintf(stderr, "\n");
    CHECK(offered > 1000, "only %d quests offered", offered);
    CHECK(hatch_hunt == 0, "%d hunts for hatchling raptors", hatch_hunt);
    CHECK(pairs > 200 && repeats < 0.15f * pairs, "%d immediate repeats of %d", repeats, pairs);
}

// The stage helpers: bands by growth, and defend and the prey rule at the edges.
static void test_rules(void) {
    float lo, hi;
    cret_quest_band(0.35f, &lo, &hi); CHECK(lo == 70 && hi == 150, "hatchling band %.0f-%.0f", lo, hi);
    cret_quest_band(0.6f, &lo, &hi); CHECK(lo == 150 && hi == 300, "juvenile band %.0f-%.0f", lo, hi);
    cret_quest_band(1.0f, &lo, &hi); CHECK(lo == 250 && hi == 500, "adult band %.0f-%.0f", lo, hi);
    CretAgent a = {.alive = true, .species = CRET_SP_STEGO, .growth = 0.35f};
    CHECK(!cret_quest_can_defend(&a), "a hatchling stegosaur may defend");
    a.growth = 0.8f; CHECK(cret_quest_can_defend(&a), "a grown stegosaur may not defend");
    a.species = CRET_SP_GALLI; a.growth = 1; CHECK(!cret_quest_can_defend(&a), "a gallimimus may defend");
    CretAgent r = {.alive = true, .species = CRET_SP_RAPTOR, .growth = 1}, prey = {.alive = true, .species = CRET_SP_STEGO, .growth = 1};
    CHECK(!cret_quest_prey_ok(NULL, &r, mass_of(&r), &prey), "a raptor may hunt an adult stegosaur");
    prey.species = CRET_SP_GALLI; prey.growth = 0.6f;   // 86 kg against 80
    CHECK(cret_quest_prey_ok(NULL, &r, mass_of(&r), &prey), "a raptor may not hunt a young gallimimus");
    prey.species = CRET_SP_SAUROPELTA; prey.growth = 0.65f;
    CHECK(!cret_quest_prey_ok(NULL, &r, 4 * mass_of(&r) * 10, &prey), "raptors may hunt a grown sauropelta");
}

// ------------------------------------------------------------------ first steps
// A raptor hatchling person goes through its first steps in order. Each step
// is finished as a player would (or its count is filled in where the test
// cannot play it); a death part way keeps the steps done; the steps survive a
// save and a load into another sim; the nest step waits until the animal is
// grown; once done they never come back. A bot never gets them.
static int wait_quest(CretSim* s, int limit_s) {
    for (int t = 0; t < limit_s * CRET_TICK_HZ && !s->prog[0].quest.kind; t++) bots_step(s, NULL);
    return s->prog[0].quest.kind;
}
static void finish_step(CretSim* s) {
    CretQuest* q = &s->prog[0].quest; CretAgent* a = &s->agents[0];
    switch (q->kind) {
    case CRET_Q_CATCH: q->progress = q->goal - 1; cret_quest_on_catch(s, 0, 0); break;
    case CRET_Q_ABILITY: {   // another animal close by, then the ability
        int j = 1; while (j < s->cfg.num_agents - 1 && !s->agents[j].alive) j++;
        s->agents[j].x = a->x + 6; s->agents[j].z = a->z;
        q->progress = q->goal - 1; cret_quest_on_ability(s, 0, 0); break; }
    case CRET_Q_SCAVENGE: q->progress = q->goal - 0.01f; cret_quest_on_scavenge(s, 0, 0.1f); break;
    case CRET_Q_HUNT: q->progress = q->goal - 1; { int j = 1; while (s->agents[j].species != q->species) j++; cret_quest_on_kill(s, 0, j); } break;
    default: q->progress = q->goal; bots_step(s, NULL); break;   // time and place quests finish on the next step
    }
}
// Takes agent 0 to dry walkable ground 80-150 m from water, the nearest such
// to the middle of the island (where the places to explore are), as a player
// would wander within reach of some: the drink step is offered only with
// water within 400 m, and the bot driving the hatchling while the test waits
// stalks small prey slowly and stays near where it was.
static void near_water(CretSim* s) {
    const CretWorld* w = s->world; CretAgent* a = &s->agents[0];
    for (float r = 0; r < w->size; r += 24) for (int q = 0; q < 32; q++) {
        float ang = q * 0.19635f, x = w->size * 0.5f + cosf(ang) * r, z = w->size * 0.5f + sinf(ang) * r;
        if (!cret_walkable(w, x, z) || cret_water(w, x, z) > cret_height(w, x, z)) continue;
        int hits = 0;   // as progress.c water_spot sees it: water, and walkable ground 5 m short of it
        for (int k = 0; k < 32; k++) for (float d = 80; d <= 150; d += 10) {
            float c = cosf(k * 0.19635f), sn = sinf(k * 0.19635f), wx = x + c * d, wz = z + sn * d;
            if (cret_water(w, wx, wz) >= cret_height(w, wx, wz) + 0.3f && cret_walkable(w, x + c * (d - 5), z + sn * (d - 5))) { hits++; break; }
        }
        if (!hits) continue;
        a->x = x; a->z = z; a->y = cret_height(w, x, z); cret_sim_place(s, 0);
        return;
    }
}
static void test_first_steps(void) {
    char dir[] = "/tmp/cret_quests_XXXXXX"; if (!mkdtemp(dir)) { CHECK(0, "no temp dir"); return; }
    setenv("XDG_CONFIG_HOME", dir, 1);
    CretSim* s = q_sim(16, CRET_SP_RAPTOR, 0.35f, 7);
    static const int expect[CRET_CHAIN_STEPS][4] = {{CRET_Q_CATCH, CRET_Q_SCAVENGE, CRET_Q_HUNT, 0}, {CRET_Q_DRINK}, {CRET_Q_COMPANION}, {CRET_Q_ABILITY}, {CRET_Q_EXPLORE}, {CRET_Q_NEST}};
    for (int step = 0; step < CRET_CHAIN_STEPS - 1; step++) {
        if (step == 1) near_water(s);
        int kind = wait_quest(s, 40); const CretQuest* q = &s->prog[0].quest;
        bool ok = false; for (int k = 0; k < 4; k++) ok |= expect[step][k] && kind == expect[step][k];
        CHECK(ok && q->chain == step + 1, "first step %d: got %s (chain %d)", step + 1, cret_quest_kind_name(kind), q->chain);
        float x, z; float d = cret_quest_target(s, q, &x, &z, NULL) ? dist(&s->agents[0], x, z) : -1;
        char title[96], prog[48]; cret_quest_text(s, q, title, sizeof title, prog, sizeof prog);
        if (d >= 0) fprintf(stderr, "  first step %d: \"%s\" (%s), %.0f m away\n", step + 1, title, prog, d);
        else fprintf(stderr, "  first step %d: \"%s\" (%s)\n", step + 1, title, prog);
        finish_step(s);
        CHECK(s->prog[0].chain == step + 1, "after step %d the character has %d steps done", step + 1, s->prog[0].chain);
        if (step == 1) {   // a death part way: the next life goes on from step 3
            cret_kill(s, 0); for (int t = 0; t < 3; t++) bots_step(s, NULL);
            if (!s->agents[0].alive) cret_sim_respawn(s, 0);
            CHECK(s->prog[0].chain == 2, "a death lost the first steps (%d done)", s->prog[0].chain);
        }
    }
    // The nest step waits for growth: a hatchling gets an ordinary quest.
    int kind = wait_quest(s, 40);
    CHECK(kind && !s->prog[0].quest.chain && kind != CRET_Q_NEST, "a hatchling past step 5 got %s (chain %d)", cret_quest_kind_name(kind), s->prog[0].quest.chain);
    // Saved and loaded into another sim: the steps and places seen carry over.
    CretCharacter c; memset(&c, 0, sizeof c); snprintf(c.name, sizeof c.name, "Questy");
    cret_character_capture(s, 0, 0.35f, &c); CHECK(cret_character_save(&c), "save failed");
    uint32_t visited = s->prog[0].visited;
    CretCharacter l; CHECK(cret_character_load("Questy", &l), "load failed");
    CHECK(l.first_steps == 5 && l.visited == visited, "loaded first_steps %d visited %x (saved 5, %x)", l.first_steps, l.visited, visited);
    CretSim* s2 = q_sim(16, CRET_SP_RAPTOR, 0.35f, 8);
    cret_character_apply(s2, 0, &l);
    CHECK(s2->prog[0].chain == 5 && s2->prog[0].visited == visited, "applied: chain %d visited %x", s2->prog[0].chain, s2->prog[0].visited);
    // Grown: the nest step comes, and building a nest finishes it.
    cret_sim_restore(s2, 0, 1.0f, 1, 1, 1, false, 0, 0); cret_quest_reset(s2, 0);
    kind = wait_quest(s2, 40);
    CHECK(kind == CRET_Q_NEST && s2->prog[0].quest.chain == CRET_CHAIN_STEPS, "a grown animal at step 6 got %s (chain %d)", cret_quest_kind_name(kind), s2->prog[0].quest.chain);
    CretInput nest = {.action = CRET_ACT_NEST};
    for (int t = 0; t < 6 * CRET_TICK_HZ && s2->prog[0].chain < CRET_CHAIN_STEPS; t++) bots_step(s2, &nest);
    CHECK(s2->prog[0].chain == CRET_CHAIN_STEPS && cret_nest_of(s2, 0) >= 0, "building a nest left the first steps at %d", s2->prog[0].chain);
    // Done: they never come back, also after a death.
    int chained = 0;
    for (int t = 0; t < 300 * CRET_TICK_HZ; t++) {
        bots_step(s2, NULL); chained += s2->prog[0].quest.chain != 0;
        if (t == 100 * CRET_TICK_HZ) cret_kill(s2, 0);
        if (!s2->agents[0].alive && s2->agents[0].dead_ticks > 30) cret_sim_respawn(s2, 0);
        for (int i = 1; i < s2->cfg.num_agents; i++) CHECK_CAPPED(!s2->prog[i].quest.chain, 3, "bot %d got a first step", i);
    }
    CHECK(chained == 0, "first steps came back for %d ticks after they were done", chained);
    // An old save without first_steps: a character with six quests behind it has done them.
    char path[200]; snprintf(path, sizeof path, "%s/cretaceous/characters/Old.ini", dir);
    FILE* f = fopen(path, "w"); if (f) { fprintf(f, "name = Old\nspecies = stego\nquests_done = 9\n"); fclose(f); }
    CHECK(cret_character_load("Old", &l) && l.first_steps == CRET_CHAIN_STEPS, "an old save starts its first steps again (%d)", l.first_steps);
    q_free(s); q_free(s2);
    unlink(path); snprintf(path, sizeof path, "%s/cretaceous/characters/Questy.ini", dir); unlink(path);
}

// ------------------------------------------------------------------ determinism
// Two sims with people of every diet at their first steps, in lockstep for
// 6000 ticks with quests turned over: the quest and progress state match on
// every tick (t_sim_hash covers them).
static void test_determinism(void) {
    CretSim* A = q_sim(24, CRET_SP_TREX, 0.6f, 31); CretSim* B = q_sim(24, CRET_SP_TREX, 0.6f, 31);
    A->human[3] = B->human[3] = 1; cret_sim_set_species(A, 3, CRET_SP_PACHY); cret_sim_set_species(B, 3, CRET_SP_PACHY);
    int diverged = -1, changes = 0; uint8_t k0 = 0;
    for (int t = 0; t < 6000 && diverged < 0; t++) {
        bots_step(A, NULL); bots_step(B, NULL);
        if (t % 400 == 399) for (int i = 0; i < 24; i += 5) { cret_quest_reset(A, i); cret_quest_reset(B, i); }
        if (t_sim_hash(A) != t_sim_hash(B) || memcmp(A->gquest, B->gquest, sizeof A->gquest)) diverged = t;
        changes += A->prog[0].quest.kind != k0; k0 = A->prog[0].quest.kind;
    }
    CHECK(diverged < 0, "quest state diverged at tick %d", diverged);
    CHECK(changes >= 4, "only %d quest changes in the determinism run", changes);
    fprintf(stderr, "determinism: 6000 ticks in lockstep, %d quest changes for slot 0, %d first steps done\n", changes, A->prog[0].chain + A->prog[3].chain);
    q_free(A); q_free(B);
}

int main(int argc, char** argv) {
    double t0 = t_now(); t_hold_world();
#define RUN_TEST(f) do { double _t = t_now(); f; fprintf(stderr, "  [%s: %.1f s]\n", #f, t_now() - _t); } while (0)
    RUN_TEST(test_rules());
    RUN_TEST(test_feasible());
    RUN_TEST(test_first_steps());
    RUN_TEST(test_determinism());
    return t_finish("test_quests", t0);
}
