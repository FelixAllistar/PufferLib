// Ecosystem arithmetic (eco.h, docs/ecosystem.md), each rule in isolation:
// energy and fat, plants, perception and fear, the value of a hunt, and the
// scripted animals' choice of what to do in hand-built situations.
#include "test_util.h"
#include "../eco.h"

static CretAgent adult(int species, float growth) {
    CretAgent a; memset(&a, 0, sizeof a);
    a.species = (uint8_t)species; a.growth = growth; a.alive = true; a.def_speed = 1; a.stamina = 1;
    cret_apply_growth(&a); a.health = a.max_health; a.food = 1; a.water = 1; a.fat = 0;
    return a;
}
static bool near(float a, float b, float tol) { return fabsf(a - b) <= tol * fmaxf(fabsf(b), 1e-6f); }

// ---------------------------------------------------------------- 1. energy
static void test_energy(void) {
    CretConfig c = cret_default_config();
    // Basal burn: a resting adult raptor burns its full gut (20 kg) in hunger_seconds.
    CretAgent r = adult(CRET_SP_RAPTOR, 1);
    CHECK(near(cret_eco_gut_kg(&r), 20.0f, 1e-4f), "raptor gut %.2f kg, want 20", cret_eco_gut_kg(&r));
    CHECK(near(cret_eco_gut_seconds(&c, &r), c.hunger_seconds, 1e-3f), "raptor gut lasts %.0f s, want %.0f", cret_eco_gut_seconds(&c, &r), c.hunger_seconds);
    // Kleiber: a full gut lasts M^0.25 longer for a carnivore (tyrannosaur 3.16x); a herbivore's smaller gut less.
    CretAgent t = adult(CRET_SP_TREX, 1), p = adult(CRET_SP_PACHY, 1), st = adult(CRET_SP_STEGO, 1);
    float tr = cret_eco_gut_seconds(&c, &t) / c.hunger_seconds;
    CHECK(near(tr, powf(100.0f, 0.25f), 1e-3f), "tyrannosaur gut lasts %.2fx a raptor's, want 3.16", tr);
    float pr = cret_eco_gut_seconds(&c, &p);
    CHECK(near(pr, c.hunger_seconds * (ECO_GUT_HERB / ECO_GUT_CARN) * powf(450.0f / 80.0f, 0.25f), 1e-3f), "pachycephalosaur gut lasts %.0f s", pr);
    CHECK(near(cret_eco_gut_seconds(&c, &st) / c.hunger_seconds, 0.48f * powf(62.5f, 0.25f), 1e-3f), "stegosaur gut");
    // Movement: a trotting raptor burns 2x basal, a sprinting one about 4x.
    float b0 = cret_eco_basal(c.hunger_seconds, 80);
    r.speed = r.trot_speed; float trot = cret_eco_burn(&c, &r) / b0;
    r.speed = r.sprint_speed; r.sprinting = true; float sprint = cret_eco_burn(&c, &r) / b0;
    r.speed = 0; r.sprinting = false; r.act = CRET_ACT_ATTACK; float fight = cret_eco_burn(&c, &r) / b0; r.act = 0;
    CHECK(near(trot, 2.0f, 1e-3f), "raptor trot burns %.2fx basal, want 2", trot);
    CHECK(sprint > 3.7f && sprint < 4.3f, "raptor sprint burns %.2fx basal, want about 4", sprint);
    CHECK(near(fight, sprint, 0.01f), "a fight burns like a sprint (%.2f vs %.2f)", fight, sprint);
    fprintf(stderr, "energy: raptor trot %.2fx, sprint %.2fx basal; full gut lasts raptor %.0f s, tyrannosaur %.0f s, pachycephalosaur %.0f s\n",
            trot, sprint, cret_eco_gut_seconds(&c, &r), cret_eco_gut_seconds(&c, &t), pr);
    // Fat: a full raptor at rest lays down fat while its gut is above 0.6, empties
    // the gut in (0.4 / 1.3 + 0.6) of the clock, then lives on fat.
    r.food = 1; r.fat = 0; float dt = 1.0f / 30, tm = 0, fat_at_06 = -1; int ticks = 0;
    while (r.food > 0 && ticks < 900 * 30 * 2) {
        cret_eco_spend(&r, b0 * dt); tm += dt; ticks++;
        if (fat_at_06 < 0 && r.food <= ECO_FAT_ABOVE) fat_at_06 = r.fat;
    }
    float want = c.hunger_seconds * ((1 - ECO_FAT_ABOVE) / (1 + ECO_FAT_RATE) + ECO_FAT_ABOVE);
    CHECK(near(tm, want, 0.01f), "full gut emptied in %.0f s, want %.0f", tm, want);
    float fat_kg = fat_at_06 * ECO_FAT_FRAC * 80, want_kg = ECO_FAT_RATE * (1 - ECO_FAT_ABOVE) * 20 / (1 + ECO_FAT_RATE);
    CHECK(near(fat_kg, want_kg, 0.01f), "fat laid down %.2f kg, want %.2f", fat_kg, want_kg);
    float fat0 = r.fat; cret_eco_spend(&r, b0 * 10);
    CHECK(r.food == 0 && near(fat0 - r.fat, b0 * 10 / (ECO_FAT_FRAC * 80), 1e-3f), "an empty gut burns fat (%.4f -> %.4f)", fat0, r.fat);
    r.fat = 0.001f; float got = cret_eco_spend(&r, 5.0f);
    CHECK(r.fat == 0 && got < 5.0f, "burning more than is left takes what there is (%.4f kg)", got);
    // Fat stops at full; no store above a full reserve.
    r.food = 1; r.fat = 1; cret_eco_spend(&r, 1.0f); CHECK(r.fat == 1 && near(r.food, 1 - 1.0f / 20, 1e-4f), "full fat: the gut pays the burn alone (food %.4f)", r.food);
}

// Thirst: a heavier animal lasts (M / 80)^0.1 longer; midday in the open is 50% faster.
static void test_thirst(void) {
    CretSim* s = calloc(1, sizeof *s); CretConfig c = t_config(2);
    if (cret_sim_init(s, c) != 0) exit(2);
    CretAgent* a = &s->agents[0]; *a = adult(CRET_SP_STEGO, 1); a->x = a->z = s->world->size * 0.5f;
    s->time_of_day = 0.2f;
    float base = cret_eco_thirst(s, a), want = 1.0f / (c.thirst_seconds * powf(5000.0f / 80, 0.1f));
    CHECK(near(base, want, 1e-4f), "stegosaur thirst %.6f/s, want %.6f", base, want);
    // Midday: in the open 1.5x, in shade unchanged.
    s->time_of_day = 0.5f; float open = -1, shade = -1;
    for (int k = 0; k < 4000 && (open < 0 || shade < 0); k++) {
        a->x = 40 + (k % 63) * (s->world->size - 80) / 63; a->z = 40 + (k / 63) * (s->world->size - 80) / 63;
        float cv = cret_eco_canopy(s->world, a->x, a->z), v = cret_eco_thirst(s, a);
        if (cv < 0.05f && open < 0) open = v; else if (cv > 0.6f && shade < 0) shade = v;
    }
    CHECK(open > 0 && near(open, base * (1 + ECO_HEAT), 1e-4f), "midday in the open %.6f, want %.6f", open, base * (1 + ECO_HEAT));
    if (shade > 0) CHECK(near(shade, base, 1e-4f), "midday in shade %.6f, want %.6f", shade, base);
    cret_sim_close(s); free(s);
}

// Growth needs a fed gut and some fat; a big meal makes a carnivore digest.
static void test_growth_digest(void) {
    CretSim* s = calloc(1, sizeof *s); CretConfig c = t_config(2);
    if (cret_sim_init(s, c) != 0) exit(2);
    CretAgent* a = &s->agents[0]; CretInput in[2] = {{0}};
    a->growth = 0.5f; cret_apply_growth(a); a->food = 0.9f; a->water = 1; a->fat = 0.05f;
    float g0 = a->growth; for (int t = 0; t < 30; t++) cret_sim_step(s, in);
    CHECK(a->growth == g0, "grew with fat %.2f (growth %.4f -> %.4f)", a->fat, g0, a->growth);
    a->fat = 0.5f; for (int t = 0; t < 30; t++) cret_sim_step(s, in);
    CHECK(a->growth > g0, "did not grow fed with fat (growth %.4f)", a->growth);
    // Digestion: eating more than half the gut in one meal; over once the gut is under half.
    CretAgent* r = &s->agents[1]; cret_sim_set_species(s, 1, CRET_SP_RAPTOR); r = &s->agents[1];
    r->food = 0.2f; r->meal = 0; r->digest = 0;
    for (int t = 0; t < 20; t++) { r->ev_food = 0.04f; r->food += 0.04f; cret_eco_spend(r, 0); r->meal += r->ev_food; }
    CHECK(r->meal >= ECO_DIGEST_MEAL, "a 0.8 gut meal counts %.2f", r->meal);
    CretInput none[2] = {{0}}; cret_sim_step(s, none);
    CHECK(r->digest == 1, "a big meal: digesting");
    r->food = 0.45f; cret_sim_step(s, none);
    CHECK(r->digest == 0, "digesting with the gut under half");
    cret_sim_close(s); free(s);
}

// ---------------------------------------------------------------- 2. plants
// A point of rich ground (cret_forage at least `want`) in the test world.
static bool rich_point(const CretSim* s, float want, float* x, float* z) {
    for (int k = 0; k < 40000; k++) {
        float px = 40 + (k % 200) * (s->world->size - 80) / 200, pz = 40 + (k / 200) * (s->world->size - 80) / 200;
        if (cret_forage(s->world, px, pz) >= want && cret_walkable(s->world, px, pz)) { *x = px; *z = pz; return true; }
    }
    return false;
}
static void test_plants(void) {
    CretConfig c = cret_default_config();
    // Regrowth: eaten to 0.2, back at 0.8 in one game day; logistic, never past 1, never under the seed bank.
    float r = cret_eco_regrow_rate(&c), day = c.day_seconds;
    CHECK(near(cret_eco_regrow(0.2f, r, day), 0.8f, 1e-4f), "0.2 regrows to %.4f in a day, want 0.8", cret_eco_regrow(0.2f, r, day));
    CHECK(near(cret_eco_regrow(0.5f, r, 0), 0.5f, 1e-6f) && cret_eco_regrow(0.99f, r, 1e6f) <= 1.0f, "regrowth endpoints");
    CHECK(near(cret_eco_regrow(0.0f, r, 0), ECO_SEED_BANK, 1e-6f), "the seed bank floor (%.3f)", cret_eco_regrow(0.0f, r, 0));
    float half = cret_eco_regrow(0.2f, r, day * 0.5f);
    CHECK(near(half, 0.5f, 1e-3f), "half a day from 0.2 is the midpoint (%.4f), as the logistic is symmetric", half);
    // Holling type II: half of c_max at B_half, saturating.
    CHECK(near(cret_eco_intake(2.0f, ECO_B_HALF), 1.0f, 1e-5f) && near(cret_eco_intake(2.0f, 1.0f), 2.0f / (1 + ECO_B_HALF), 1e-5f) && cret_eco_intake(2, 0) == 0, "Holling intake");
    CretAgent p = adult(CRET_SP_PACHY, 1);
    float cm = cret_eco_cmax(&c, cret_mass(&p)), bas = cret_eco_basal(c.hunger_seconds, cret_mass(&p));
    CHECK(near(cm / bas, ECO_INTAKE_X, 1e-4f), "c_max is %.2fx basal, want %.1f", cm / bas, ECO_INTAKE_X);
    // A sim: grazing takes plants off the cell; the cell grows back; a herd of 8
    // pachycephalosaurs eating 60% of the time (vigilance) takes a rich cell to 0.3 in about four minutes.
    CretSim* s = calloc(1, sizeof *s); CretConfig tc = t_config(9);
    if (cret_sim_init(s, tc) != 0) exit(2);
    float x, z; CHECK(rich_point(s, 0.9f, &x, &z), "no rich ground in the test world");
    CHECK(cret_eco_biomass(s, x, z) == 1 && near(cret_graze_at(s, x, z), cret_forage(s->world, x, z), 1e-6f), "an ungrazed cell is full");
    for (int i = 0; i < 8; i++) { CretAgent* a = &s->agents[i]; *a = adult(CRET_SP_PACHY, 1); a->food = 0; }
    int ticks = 0; float eaten = 0;
    while (cret_eco_biomass(s, x, z) > 0.3f && ticks < 30 * 900) {
        for (int i = 0; i < 8; i++) if ((ticks + i * 7) % 10 < 6) eaten += cret_eco_graze(s, &s->agents[i], x, z, 1e9f);
        ticks++; s->tick++;
    }
    float mins = ticks / 30.0f / 60.0f;
    fprintf(stderr, "plants: 8 pachycephalosaurs took a cell of richness %.2f to 0.3 in %.1f min, eating %.0f kg; c_max %.3f kg/s (%.1fx basal)\n",
            cret_forage(s->world, x, z), mins, eaten, cm, cm / bas);
    CHECK(mins > 3.0f && mins < 5.5f, "8 pachycephalosaurs exhaust a rich cell in %.1f min, want about 4", mins);
    // It grows back: half a day later it is well on its way, a day and a half later all but full.
    float b0 = cret_eco_biomass(s, x, z); s->tick += (int)(day * 0.5f * 30);
    float b1 = cret_eco_biomass(s, x, z);
    CHECK(near(b1, cret_eco_regrow(b0, r, day * 0.5f), 1e-4f) && b1 > b0, "regrowth in the sim %.3f -> %.3f", b0, b1);
    // The table: many grazed cells fill it; old ones that grew back leave, the rest stay, and reads stay right.
    for (int k = 0; k < 3000; k++) { cret_eco_set_biomass(s, 16 + (k % 60) * 32.0f, 16 + (k / 60) * 32.0f, 0.3f); s->tick += 3; }
    CHECK(s->nplants <= CRET_PLANT_SLOTS * 3 / 4, "%d cells kept, over the table's limit", s->nplants);
    float last = cret_eco_biomass(s, 16 + (2999 % 60) * 32.0f, 16 + (2999 / 60) * 32.0f);
    CHECK(near(last, 0.3f, 0.02f), "the newest grazed cell reads %.3f, want 0.3", last);
    cret_sim_close(s); free(s);
}

int main(void) {
    double t0 = t_now(); t_hold_world();
#define RUN_TEST(f) do { double _t = t_now(); f; fprintf(stderr, "  [%s: %.1f s]\n", #f, t_now() - _t); } while (0)
    RUN_TEST(test_energy());
    RUN_TEST(test_thirst());
    RUN_TEST(test_growth_digest());
    RUN_TEST(test_plants());
    return t_finish("test_eco", t0);
}
