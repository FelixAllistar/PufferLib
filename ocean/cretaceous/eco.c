// Ecosystem arithmetic (eco.h, docs/ecosystem.md), included by sim.c: energy
// and fat, plants. Pure functions of the state they are given; nothing here draws a
// random number.

// ---------------------------------------------------------------- 1. energy
float cret_eco_gut_kg(const CretAgent* a) {
    return (CRET_SPECIES_TABLE[a->species].diet == CRET_DIET_CARNIVORE ? ECO_GUT_CARN : ECO_GUT_HERB) * cret_mass(a);
}
// k_b = 0.25 x 80 / (hunger_seconds x 80^0.75): a resting adult raptor burns
// its full gut (a quarter of its 80 kg) in hunger_seconds.
float cret_eco_basal(float hunger_seconds, float mass) {
    float kb = ECO_GUT_CARN * ECO_REF_MASS / (fmaxf(hunger_seconds, 1.0f) * powf(ECO_REF_MASS, ECO_BASAL_EXP));
    return kb * powf(fmaxf(mass, 0.01f), ECO_BASAL_EXP);
}
// Movement: k_m M^0.7 v, with k_m = k_b 80^0.75 (TROT_BURN - 1) / (80^0.7 x
// 4.6 m/s), so a raptor at its trot burns TROT_BURN x basal. A sprint counts
// its speed ECO_SPRINT_MULT times; a fight or a swim counts as a sprint.
float cret_eco_burn(const CretConfig* c, const CretAgent* a) {
    float M = fmaxf(cret_mass(a), 0.01f), basal = cret_eco_basal(c->hunger_seconds, M);
    float kb = basal / powf(M, ECO_BASAL_EXP);
    float km = kb * powf(ECO_REF_MASS, ECO_BASAL_EXP - ECO_MOVE_EXP) * (ECO_TROT_BURN - 1) / ECO_REF_TROT;
    float v = a->speed;
    if (a->sprinting) v *= ECO_SPRINT_MULT;
    if (a->swimming || cret_act_is_attack(a->act)) v = fmaxf(v, a->sprint_speed * ECO_SPRINT_MULT);
    return basal + km * powf(M, ECO_MOVE_EXP) * v;
}
float cret_eco_spend(CretAgent* a, float kg) {
    float G = fmaxf(cret_eco_gut_kg(a), 1e-4f), F = fmaxf(ECO_FAT_FRAC * cret_mass(a), 1e-4f);
    float gut = a->food * G, fat = a->fat * F;
    if (a->food > ECO_FAT_ABOVE) {   // well fed: the gut drains faster and the surplus is laid down as fat
        float store = fat < F ? fminf(kg * ECO_FAT_RATE, F - fat) : 0;
        a->food = fmaxf(gut - kg - store, 0) / G; a->fat = (fat + store) / F;
        return kg;
    }
    float g = fminf(gut, kg), f = fminf(fat, kg - g);   // the gut first, then the reserve
    a->food = (gut - g) / G; a->fat = (fat - f) / F;
    if (a->food < 1e-6f) a->food = 0;
    if (a->fat < 1e-6f) a->fat = 0;
    return g + f;
}
float cret_eco_gut_seconds(const CretConfig* c, const CretAgent* a) { return cret_eco_gut_kg(a) / cret_eco_basal(c->hunger_seconds, cret_mass(a)); }
float cret_eco_canopy(const CretWorld* w, float x, float z) {
    const TerraMap* m = &w->map;
    int gx = (int)(x / m->cell + 0.5f), gz = (int)(z / m->cell + 0.5f);
    if (gx < 0 || gz < 0 || gx >= m->n || gz >= m->n) return 0;
    const uint8_t* v = m->veg + ((size_t)gz * m->n + gx) * TERRA_VEG_COUNT;
    return clampf((v[TERRA_VEG_CONIFER] + v[TERRA_VEG_BROADLEAF] + 0.6f * v[TERRA_VEG_TREEFERN] + 0.4f * v[TERRA_VEG_PALM]) / 255.0f, 0, 1);
}
float cret_eco_thirst(const CretSim* s, const CretAgent* a) {
    float clock = s->cfg.thirst_seconds * powf(fmaxf(cret_mass(a), 0.01f) / ECO_REF_MASS, ECO_THIRST_EXP);
    float rate = (a->sprinting ? 2.0f : a->act == CRET_ACT_REST ? 0.6f : 1.0f) / fmaxf(clock, 1.0f);
    if (fabsf(s->time_of_day - 0.5f) * 24.0f < ECO_HEAT_HOURS * 0.5f && !a->swimming && cret_eco_canopy(s->world, a->x, a->z) < ECO_SHADE) rate *= 1 + ECO_HEAT;
    return rate;
}

// ---------------------------------------------------------------- 2. plants
// Biomass per ECO_CELL_M cell, B in ECO_SEED_BANK..1 of its capacity. Only
// grazed cells are stored (CretSim.plants, an open-addressed table keyed by
// cell + 1); every other cell is full. A cell's B is kept with the tick it was
// last written and advanced by the closed form of the logistic regrowth when
// read, so no cell is ever stepped: B(t) = 1 / (1 + (1 / B0 - 1) e^(-r t)).
float cret_eco_regrow_rate(const CretConfig* c) { return logf((ECO_REGROW_TO / ECO_REGROW_FROM) * (1 - ECO_REGROW_FROM) / (1 - ECO_REGROW_TO)) / fmaxf(c->day_seconds, 1.0f); }
// The cell holding (x, z), clamped to the island; -1 without a world (a
// client before its welcome) or for a position that is not finite.
static int eco_cell(const CretSim* s, float x, float z) {
    if (!s->world || !isfinite(x) || !isfinite(z)) return -1;
    int n = (int)ceilf(s->world->size / ECO_CELL_M);
    return (int)clampf(z / ECO_CELL_M, 0, n - 1) * n + (int)clampf(x / ECO_CELL_M, 0, n - 1);
}
static int eco_slot(const CretSim* s, int cell) {
    uint32_t h = (uint32_t)cell * 2654435761u;
    for (int k = 0; k < ECO_PLANT_SLOTS; k++) {
        int i = (int)((h + (uint32_t)k) & (ECO_PLANT_SLOTS - 1));
        if (s->plants[i].cell == cell + 1) return i;
        if (!s->plants[i].cell) return -1;
    }
    return -1;
}
float cret_eco_regrow(float b0, float rate, float seconds) {
    b0 = clampf(b0, ECO_SEED_BANK, 1);
    if (b0 >= 1) return 1;
    return 1.0f / (1.0f + (1.0f / b0 - 1.0f) * expf(-rate * seconds));
}
static float eco_cell_b(const CretSim* s, int cell) {
    int k = cell < 0 ? -1 : eco_slot(s, cell); if (k < 0) return 1;
    return cret_eco_regrow(s->plants[k].b, cret_eco_regrow_rate(&s->cfg), (s->tick - s->plants[k].tick) * CRET_DT);
}
float cret_eco_biomass(const CretSim* s, float x, float z) { return eco_cell_b(s, eco_cell(s, x, z)); }
float cret_graze_at(const CretSim* s, float x, float z) { return s->world ? cret_forage(s->world, x, z) * cret_eco_biomass(s, x, z) : 0; }
float cret_eco_intake(float cmax, float bk) { return bk > 0 ? cmax * bk / (bk + ECO_B_HALF) : 0; }
float cret_eco_cmax(const CretConfig* c, float mass) { return ECO_INTAKE_X * cret_eco_basal(c->hunger_seconds, mass); }
// Writes a cell's biomass now. A full table first drops the cells that have
// grown back (ECO_REGROWN), in slot order, then, if none have, the most grown.
void cret_eco_set_biomass(CretSim* s, float x, float z, float b) {
    int cell = eco_cell(s, x, z); if (cell < 0 || !isfinite(b)) return;
    int k = eco_slot(s, cell);
    b = clampf(b, ECO_SEED_BANK, 1);
    if (k < 0) {
        if (b >= 1) return;   // full: nothing to keep
        if (s->nplants >= ECO_PLANT_SLOTS * 3 / 4) {   // full: rebuild with the cells that have not grown back
            CretPlantCell keep[ECO_PLANT_SLOTS]; int n = 0; float rate = cret_eco_regrow_rate(&s->cfg), cut = ECO_REGROWN;
            for (int i = 0; i < ECO_PLANT_SLOTS; i++) {
                CretPlantCell p = s->plants[i]; if (!p.cell) continue;
                p.b = cret_eco_regrow(p.b, rate, (s->tick - p.tick) * CRET_DT); p.tick = s->tick; keep[n++] = p;
            }
            for (;;) {   // drop the most grown cells until the table is at most half full
                int m = 0; for (int i = 0; i < n; i++) m += keep[i].b < cut;
                if (m <= ECO_PLANT_SLOTS / 2) break;
                cut -= 0.05f;
            }
            memset(s->plants, 0, sizeof s->plants); s->nplants = 0;
            for (int i = 0; i < n; i++) {
                if (keep[i].b >= cut) continue;
                uint32_t h = (uint32_t)(keep[i].cell - 1) * 2654435761u; int j = 0;
                while (s->plants[(h + (uint32_t)j) & (ECO_PLANT_SLOTS - 1)].cell) j++;
                s->plants[(h + (uint32_t)j) & (ECO_PLANT_SLOTS - 1)] = keep[i]; s->nplants++;
            }
        }
        uint32_t h = (uint32_t)cell * 2654435761u; int j = 0;
        while (s->plants[(h + (uint32_t)j) & (ECO_PLANT_SLOTS - 1)].cell) j++;
        k = (int)((h + (uint32_t)j) & (ECO_PLANT_SLOTS - 1)); s->plants[k].cell = cell + 1; s->nplants++;
    }
    s->plants[k].b = b; s->plants[k].tick = s->tick;
}
// One tick of grazing by agent a with its mouth at (x, z): Holling type II
// intake on the plants there, taken off the cell. Returns kg eaten.
float cret_eco_graze(CretSim* s, CretAgent* a, float x, float z, float room_kg) {
    float K = cret_forage(s->world, x, z); if (K <= 0) return 0;
    int cell = eco_cell(s, x, z); float b = eco_cell_b(s, cell);
    float kg = fminf(cret_eco_intake(cret_eco_cmax(&s->cfg, cret_mass(a)), K * b) * CRET_DT, fmaxf(room_kg, 0));
    if (kg <= 0) return 0;
    cret_eco_set_biomass(s, x, z, b - kg / (fmaxf(K, 0.05f) * ECO_CELL_KG));
    return kg;
}
