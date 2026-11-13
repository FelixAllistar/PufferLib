// Ecosystem arithmetic (eco.h, docs/ecosystem.md), included by sim.c: energy
// and fat. Pure functions of the state they are given; nothing here draws a
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
