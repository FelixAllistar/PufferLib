// Ecosystem arithmetic (eco.h, docs/ecosystem.md), each rule in isolation:
// energy and fat, plants, perception and fear, the value of a hunt, and the
// scripted animals' choice of what to do in hand-built situations.
#include "test_util.h"
#include "../eco.h"
#include "../population.h"

static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }

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

// ---------------------------------------------------------------- 3, 4. perception and fear
static void test_fear(void) {
    // D: nothing from a predator under 0.15 of its mass; 1.5 at equal mass; at most 3.
    CHECK(cret_eco_threat_d(80, 5000) == 0 && near(cret_eco_threat_d(400, 400), 1.5f, 1e-5f) && cret_eco_threat_d(8000, 80) == ECO_THREAT_MAX, "D endpoints");
    CHECK(near(cret_eco_threat_d(80, 400), ECO_THREAT_FLOOR, 1e-5f), "a raptor frightens a gallimimus a little (D %.2f)", cret_eco_threat_d(80, 400));
    CHECK(cret_eco_threat_d(320, 450) > cret_eco_threat_d(80, 450), "a pack frightens more than one");
    // FID: (20 + 5 L) D / (1 + 0.5 cover) (1.3 - 0.6 food): a hungry animal lets danger closer, cover too.
    float f0 = cret_eco_fid(3, 1, 0, 0.5f);
    CHECK(near(f0, 35.0f, 1e-4f), "FID of a raptor at D 1 for a half-fed animal in the open %.1f, want 35", f0);
    CHECK(cret_eco_fid(3, 1, 0, 0.1f) < cret_eco_fid(3, 1, 0, 0.9f), "a hungry animal lets danger closer");
    CHECK(cret_eco_fid(3, 1, 1, 0.5f) < f0 && cret_eco_fid(12, 3, 0, 1) == ECO_FID_MAX, "FID in cover, cap");
    // Vigilance: 6 s alone, 6 sqrt(n) in a group, at most 20.
    CHECK(near(cret_eco_vigil_interval(1), 6, 1e-5f) && near(cret_eco_vigil_interval(4), 12, 1e-5f) && cret_eco_vigil_interval(64) == ECO_VIG_MAX, "vigilance interval");
    // Hearing: 8 m at 1 m/s for 80 kg, (v)^2, louder when heavier, at most 150 m, half in rain.
    CretSim* s = calloc(1, sizeof *s); CretConfig c = t_config(3);
    if (cret_sim_init(s, c) != 0) exit(2);
    s->cloud_cover = 0.3f;
    CHECK(near(cret_eco_hear_m(s, 1, 80), 8, 1e-5f) && near(cret_eco_hear_m(s, 2, 80), 32, 1e-5f) && cret_eco_hear_m(s, 9, 8000) == ECO_HEAR_MAX, "hearing");
    s->cloud_cover = 0.95f; CHECK(near(cret_eco_hear_m(s, 1, 80), 4, 1e-5f), "rain halves hearing"); s->cloud_cover = 0.3f;
    // Risk and flight from a hand-built memory: a pachycephalosaur with a raptor known 30 m off.
    CretAgent* p = &s->agents[0]; CretAgent* r = &s->agents[1];
    cret_sim_set_species(s, 0, CRET_SP_PACHY); cret_sim_set_species(s, 1, CRET_SP_RAPTOR);
    *p = adult(CRET_SP_PACHY, 0.6f); *r = adult(CRET_SP_RAPTOR, 1); p->group = r->group = 0;
    float px = s->world->size * 0.5f, pz = px; p->x = px; p->z = pz; r->x = px + 30; r->z = pz; p->food = 0.5f;
    memset(p->known, 0, sizeof p->known);
    p->known[0] = (CretKnown){.who = 2, .x = r->x, .z = r->z, .vx = 0, .vz = 0, .tick = s->tick, .since = s->tick};
    float D = cret_eco_threat_of(s, p, r), R = ECO_RISK_R0 + ECO_RISK_RV * r->sprint_speed, want = D * expf(-30 / R) * ECO_A_IDLE;
    float risk = cret_eco_risk(s, 0, px, pz), ch = cret_eco_cover_h(s->world, px, pz);
    if (ch <= p->height) CHECK(near(risk, want + cret_eco_fear_at(s, px, pz), 1e-3f), "risk of a raptor standing 30 m off %.3f, want %.3f", risk, want);
    CHECK(cret_eco_flight(s, 0, NULL) < 0, "ran from a raptor standing still");
    p->known[0].vx = -3;   // it comes
    float fd; int fl = cret_eco_flight(s, 0, &fd);
    float fid = cret_eco_fid(CRET_SPECIES_TABLE[CRET_SP_RAPTOR].length, D, clampf(ch / p->height, 0, 1), p->food);
    CHECK((fl == 1) == (30 < fid), "flight from a raptor coming at 30 m with FID %.1f: %d", fid, fl);
    CHECK(cret_eco_risk(s, 0, px, pz) > risk, "a raptor coming is riskier than one standing");
    // The landscape of fear: a mark fades over a day and reaches 64 m.
    s->fear[0] = (CretFearMark){.x = px, .z = pz, .w = ECO_FEAR_MARK, .tick = s->tick};
    CHECK(near(cret_eco_fear_at(s, px, pz), ECO_FEAR_MARK, 1e-4f) && cret_eco_fear_at(s, px + 70, pz) == 0, "a fresh mark");
    s->tick += (int)(c.day_seconds * CRET_TICK_HZ * 0.5f);
    CHECK(near(cret_eco_fear_at(s, px, pz), ECO_FEAR_MARK * 0.5f, 1e-3f), "half a day on, half the fear (%.3f)", cret_eco_fear_at(s, px, pz));
    cret_sim_close(s); free(s);
}

// ---------------------------------------------------------------- 5. hunting and drives
// A sim with n agents on open dry ground away from water, everyone placed by hand.
static CretSim* hand_sim(int n, float* cx, float* cz) {
    CretSim* s = calloc(1, sizeof *s); CretConfig c = t_config(n);
    if (cret_sim_init(s, c) != 0) exit(2);
    for (int k = 0; k < 40000; k++) {
        float x = 60 + (k % 200) * (s->world->size - 120) / 200, z = 60 + (k / 200) * (s->world->size - 120) / 200; bool ok = cret_forage(s->world, x, z) > 0.3f;
        for (int q = 0; q < 16 && ok; q++) ok = cret_walkable(s->world, x + cosf(q * 0.39f) * 60, z + sinf(q * 0.39f) * 60);
        if (ok) { *cx = x; *cz = z; break; }
    }
    for (int i = 0; i < n; i++) { s->agents[i].group = 0; memset(s->agents[i].known, 0, sizeof s->agents[i].known); }
    return s;
}
static void place(CretSim* s, int i, int species, float growth, float x, float z, float yaw) {
    CretAgent* a = &s->agents[i]; if (a->species != species) cret_sim_set_species(s, i, species);
    a->growth = growth; cret_apply_growth(a); a->health = a->max_health; a->food = 0.9f; a->water = 0.9f; a->fat = 0.5f; a->stamina = 1;
    a->x = x; a->z = z; a->y = cret_height(s->world, x, z); a->yaw = yaw; a->vx = a->vz = 0; a->speed = 0; a->group = 0; a->bot_state = BOT_WANDER; a->bot_alert = 0;
    a->digest = 0; a->meal = 0; a->fatigue = 0; memset(a->known, 0, sizeof a->known); cret_sim_place(s, i);
}
static void know(CretSim* s, int i, int j, float vx, float vz) {
    CretAgent* a = &s->agents[i]; const CretAgent* b = &s->agents[j];
    for (int k = 0; k < CRET_KNOWN; k++) if (!a->known[k].who) { a->known[k] = (CretKnown){.who = (uint8_t)(j + 1), .x = b->x, .z = b->z, .vx = vx, .vz = vz, .tick = s->tick, .since = s->tick}; return; }
}
// Runs agent i's next decision (its slot's 3 Hz tick) and returns the activity it chose.
static int decide(CretSim* s, int i) {
    while ((s->tick + i) % 10) s->tick++;
    cret_bot_input(s, i);
    return s->agents[i].bot_state;
}
static void test_hunting(void) {
    float x, z; CretSim* s = hand_sim(4, &x, &z);
    place(s, 0, CRET_SP_RAPTOR, 1, x, z, 0); s->agents[0].food = 0.3f;
    place(s, 1, CRET_SP_GALLI, 0.6f, x + 50, z, 0);
    place(s, 2, CRET_SP_STEGO, 1, x - 50, z, 0);
    place(s, 3, CRET_SP_PACHY, 1, x, z + 50, 0);
    CretHuntEval young = cret_eco_hunt_eval(s, 0, 1, 0, false), seen = cret_eco_hunt_eval(s, 0, 1, 0, true);
    CretHuntEval stego = cret_eco_hunt_eval(s, 0, 2, 0, false), pachy = cret_eco_hunt_eval(s, 0, 3, 0, false), pack = cret_eco_hunt_eval(s, 0, 3, 3, false);
    fprintf(stderr, "hunting: raptor on a young gallimimus 50 m off: unnoticed catch %.2f profit %.2f, noticed catch %.2f profit %.2f; adult stegosaur profit %.2f; "
            "adult pachycephalosaur kill %.2f alone, %.2f with 3 packmates\n", young.p_catch, young.profit, seen.p_catch, seen.profit, stego.profit, pachy.p_kill, pack.p_kill);
    CHECK(young.p_catch >= ECO_CATCH_SURPRISE - 1e-4f && young.profit > ECO_HUNT_MIN, "an unnoticed young gallimimus is a good hunt (catch %.2f, profit %.2f)", young.p_catch, young.profit);
    CHECK(seen.p_catch < young.p_catch && seen.profit < young.profit, "a noticed one is a worse hunt");
    CHECK(stego.profit < 0, "an adult stegosaur is not worth it for one raptor (profit %.2f)", stego.profit);
    CHECK(pack.p_kill > pachy.p_kill + 0.2f && pack.p_catch > pachy.p_catch, "a pack takes an adult pachycephalosaur more surely");
    CHECK(young.rate > pachy.rate && young.rate > stego.rate, "the young gallimimus ranks first");
    // A full gut: no meal, no profit.
    s->agents[0].food = 1; CHECK(cret_eco_hunt_eval(s, 0, 1, 0, false).profit < 0, "a full raptor sees no profit");
    CHECK(cret_eco_charge_m(&s->agents[0]) >= ECO_CHARGE_MIN && cret_eco_charge_m(&s->agents[0]) <= ECO_CHARGE_MAX, "charge distance in range");
    cret_sim_close(s); free(s);
}
static void test_drives(void) {
    float x, z; CretSim* s = hand_sim(3, &x, &z); int got;
    // A hungry raptor that knows an unnoticed young gallimimus 50 m off hunts it.
    place(s, 0, CRET_SP_RAPTOR, 1, x, z, 0); s->agents[0].food = 0.3f;
    place(s, 1, CRET_SP_GALLI, 0.6f, x + 50, z, 3.14f); s->agents[1].act = CRET_ACT_EAT;
    place(s, 2, CRET_SP_PACHY, 1, x - 300, z, 0);
    know(s, 0, 1, 0, 0);
    got = decide(s, 0); CHECK(got == BOT_HUNT && s->agents[0].bot_target == 1, "a hungry raptor chose %s (target %d), want hunt", CRET_BOT_STATE_NAMES[got], s->agents[0].bot_target);
    // Digesting a big meal it does not hunt; it rests.
    place(s, 0, CRET_SP_RAPTOR, 1, x, z, 0); s->agents[0].food = 0.8f; s->agents[0].digest = 1; know(s, 0, 1, 0, 0);
    got = decide(s, 0); CHECK(got == BOT_REST, "a digesting raptor chose %s, want rest", CRET_BOT_STATE_NAMES[got]);
    // A gallimimus with that raptor coming at it from 10 m (inside its flight distance: 22 m in the open, 14.5 m in thick cover) runs.
    place(s, 0, CRET_SP_RAPTOR, 1, x + 10, z, 3.14f); place(s, 1, CRET_SP_GALLI, 1, x, z, 0); s->agents[0].vx = -4; s->agents[0].speed = 4;
    know(s, 1, 0, -4, 0);
    got = decide(s, 1); CHECK(got == BOT_FLEE && s->agents[1].bot_target == 0, "a gallimimus with a raptor coming chose %s, want flee", CRET_BOT_STATE_NAMES[got]);
    // The same raptor standing still 60 m off: it keeps grazing.
    place(s, 0, CRET_SP_RAPTOR, 1, x + 60, z, 3.14f); place(s, 1, CRET_SP_GALLI, 1, x, z, 0); s->agents[1].food = 0.4f;
    know(s, 1, 0, 0, 0);
    got = decide(s, 1); CHECK(got == BOT_GRAZE, "a hungry gallimimus with a raptor standing 60 m off chose %s, want graze", CRET_BOT_STATE_NAMES[got]);
    // A stegosaur with a tyrannosaur coming at it stands and swings its tail; a lone raptor (under 0.15 of its mass) it ignores.
    place(s, 0, CRET_SP_TREX, 1, x + 14, z, 3.14f); place(s, 1, CRET_SP_STEGO, 1, x, z, 0); s->agents[0].vx = -3; s->agents[0].speed = 3;
    s->agents[0].bot_state = BOT_HUNT; s->agents[0].bot_target = 1; know(s, 1, 0, -3, 0);
    got = decide(s, 1); CHECK(got == BOT_DEFEND, "a stegosaur with a tyrannosaur coming chose %s, want defend", CRET_BOT_STATE_NAMES[got]);
    place(s, 0, CRET_SP_RAPTOR, 1, x + 14, z, 3.14f); place(s, 1, CRET_SP_STEGO, 1, x, z, 0); s->agents[1].food = 0.4f; s->agents[0].vx = -3; s->agents[0].speed = 3;
    know(s, 1, 0, -3, 0);
    got = decide(s, 1); CHECK(got == BOT_GRAZE, "a hungry stegosaur with a lone raptor about chose %s, want graze", CRET_BOT_STATE_NAMES[got]);
    // A young pachycephalosaur 40 m from the grown one of its group follows it.
    place(s, 0, CRET_SP_PACHY, 1, x + 40, z, 0); place(s, 1, CRET_SP_PACHY, 0.4f, x, z, 0); s->agents[0].group = s->agents[1].group = 1;
    place(s, 2, CRET_SP_PACHY, 1, x - 300, z, 0); s->agents[0].group = s->agents[1].group = 1;
    got = decide(s, 1); CHECK(got == BOT_GROUP && s->agents[1].bot_target == 0, "a young pachycephalosaur chose %s (target %d), want to follow its parent", CRET_BOT_STATE_NAMES[got], s->agents[1].bot_target);
    // A stegosaur whose young has a tyrannosaur at its side stands to it.
    place(s, 0, CRET_SP_STEGO, 1, x, z, 0); place(s, 1, CRET_SP_STEGO, 0.4f, x + 6, z, 0); place(s, 2, CRET_SP_TREX, 1, x + 12, z, 3.14f);
    s->agents[0].group = s->agents[1].group = 1; know(s, 0, 2, 0, 0);
    got = decide(s, 0); CHECK(got == BOT_DEFEND && s->agents[0].bot_target == 2, "a stegosaur with a tyrannosaur at its young chose %s, want defend", CRET_BOT_STATE_NAMES[got]);
    s->agents[0].group = s->agents[1].group = 0;
    // Parched, with nothing about: water first.
    place(s, 0, CRET_SP_RAPTOR, 1, x + 400, z, 0); place(s, 1, CRET_SP_PACHY, 1, x, z, 0); s->agents[1].water = 0.1f; s->agents[1].food = 0.5f;
    got = decide(s, 1); CHECK(got == BOT_SEEK_WATER || got == BOT_DRINK || got == BOT_WANDER, "a parched pachycephalosaur chose %s, want water", CRET_BOT_STATE_NAMES[got]);
    cret_sim_close(s); free(s);
}

// A raptor tears a piece it can lift (a quarter of its mass) off a body too heavy to carry.
static void test_tear(void) {
    float x, z; CretSim* s = hand_sim(2, &x, &z);
    place(s, 0, CRET_SP_RAPTOR, 1, x, z, 0);
    float mx, my, mz; cret_mouth_point(&s->agents[0], &mx, &my, &mz);
    s->carcasses[0] = (CretCarcass){.active = true, .species = CRET_SP_GALLI, .x = mx + 0.5f, .y = cret_height(s->world, mx, mz), .z = mz, .growth = 1, .meat = 150, .max_meat = 200, .agent = -1};
    int p = cret_carry_tear(s, 0, 0, 100);
    CHECK(p >= 0 && s->carcasses[p].item == CRET_ITEM_MEAT && near(s->carcasses[p].meat, 20, 1e-3f) && near(s->carcasses[0].meat, 130, 1e-3f) && s->agents[0].carry == p + 1,
          "a raptor tore %.1f kg off (piece slot %d, body %.1f kg left)", p >= 0 ? s->carcasses[p].meat : 0, p, s->carcasses[0].meat);
    CHECK(cret_carry_tear(s, 0, 0, 10) < 0, "a full mouth tears nothing");
    cret_carry_drop(s, 0, false); s->agents[0].x += 30;
    CHECK(cret_carry_tear(s, 0, 0, 10) < 0, "nothing is torn out of reach");
    cret_sim_close(s); free(s);
}

// ---------------------------------------------------------------- 8. population
static void test_population(void) {
    CretSim* s = calloc(1, sizeof *s); CretConfig c = t_config(16); c.respawn_ticks = 60;
    if (cret_sim_init(s, c) != 0) exit(2);
    CretPop P; cret_pop_init(&P, s, true);
    float tg[2], al[2]; cret_pop_targets(&P, s, tg, al);
    CHECK(near(tg[CRET_DIET_CARNIVORE], CRET_POP_PRED_SHARE * al[CRET_DIET_HERBIVORE], 1e-4f), "predator target %.0f kg, want %.0f%% of %.0f", tg[1], CRET_POP_PRED_SHARE * 100, al[0]);
    CHECK(tg[CRET_DIET_HERBIVORE] > 0 && P.plants_ha > 0, "a herbivore target from the plants (%.0f ha)", P.plants_ha);
    CHECK(near(cret_pop_species_target(&P, s, CRET_SP_RAPTOR), tg[CRET_DIET_CARNIVORE] / 2, 1e-4f), "each predator species gets half the predator target");
    float herb = 0; for (int sp = 0; sp < CRET_SPECIES; sp++) if (CRET_SPECIES_TABLE[sp].diet == CRET_DIET_HERBIVORE) herb += cret_pop_species_target(&P, s, sp);
    CHECK(near(herb, tg[CRET_DIET_HERBIVORE], 1e-3f), "the herbivore shares add up to the target (%.0f of %.0f kg)", herb, tg[0]);
    // Predators over their target: a dead raptor waits while another lives, the last one does not.
    int r1 = -1, r2 = -1;
    for (int i = 0; i < 16; i++) if (s->agents[i].species == CRET_SP_RAPTOR) { if (r1 < 0) r1 = i; else if (r2 < 0) r2 = i; }
    for (int i = 0; i < 16; i++) if (s->agents[i].species == CRET_SP_RAPTOR && i != r1 && i != r2) cret_kill(s, i), s->agents[i].dead_ticks = -1000000;   // out of the way
    P.ratio[CRET_SP_RAPTOR] = 3;   // well over (as smoothed over a day)
    cret_kill(s, r1); CretInput in[16] = {{0}};
    for (int t = 0; t < 5 * c.respawn_ticks; t++) { cret_sim_step(s, in); P.ratio[CRET_SP_RAPTOR] = 3; cret_pop_step(&P, s); }
    CHECK(!s->agents[r1].alive, "a dead raptor came back with predators over their target");
    cret_kill(s, r2);
    for (int t = 0; t < 3 * c.respawn_ticks; t++) { cret_sim_step(s, in); P.ratio[CRET_SP_RAPTOR] = 3; cret_pop_step(&P, s); }
    CHECK(s->agents[r1].alive || s->agents[r2].alive, "the last raptors stayed dead: the species died out");
    cret_sim_close(s); free(s);
}

int main(void) {
    double t0 = t_now(); t_hold_world();
#define RUN_TEST(f) do { double _t = t_now(); f; fprintf(stderr, "  [%s: %.1f s]\n", #f, t_now() - _t); } while (0)
    RUN_TEST(test_energy());
    RUN_TEST(test_thirst());
    RUN_TEST(test_growth_digest());
    RUN_TEST(test_plants());
    RUN_TEST(test_fear());
    RUN_TEST(test_hunting());
    RUN_TEST(test_drives());
    RUN_TEST(test_tear());
    RUN_TEST(test_population());
    return t_finish("test_eco", t0);
}
