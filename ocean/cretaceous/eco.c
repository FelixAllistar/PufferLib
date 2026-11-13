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

// ---------------------------------------------------------------- 3. what each animal knows
float cret_eco_cover_h(const CretWorld* w, float x, float z) {
    const TerraMap* m = &w->map;
    int gx = (int)(x / m->cell + 0.5f), gz = (int)(z / m->cell + 0.5f);
    if (gx < 0 || gz < 0 || gx >= m->n || gz >= m->n) return 0;
    const uint8_t* v = m->veg + ((size_t)gz * m->n + gx) * TERRA_VEG_COUNT;
    return fminf(1.2f * (v[TERRA_VEG_FERN] + v[TERRA_VEG_CYCAD] + v[TERRA_VEG_HORSETAIL]) / 255.0f + 2.0f * v[TERRA_VEG_SHRUB] / 255.0f +
                 3.0f * v[TERRA_VEG_TREEFERN] / 255.0f, 4.0f);
}
// Raptors are crepuscular and tyrannosaurs hunt into the night; the herbivores are diurnal.
static bool eco_diurnal(int species) { return species != CRET_SP_RAPTOR && species != CRET_SP_TREX; }
static bool eco_night(const CretSim* s) { return s->time_of_day < 0.21f || s->time_of_day > 0.79f; }
static bool eco_head_down(const CretAgent* a) { return a->act == CRET_ACT_EAT || a->act == CRET_ACT_DRINK; }
static float eco_length(const CretAgent* a) { return CRET_SPECIES_TABLE[a->species].length * cret_growth_scale(a->growth); }
float cret_eco_sight_m(const CretSim* s, const CretAgent* a) {
    float r = ECO_SIGHT_M + ECO_SIGHT_PER_L * eco_length(a);
    if (eco_diurnal(a->species) && eco_night(s)) r *= ECO_NIGHT_SIGHT;
    if (a->act == CRET_ACT_REST) r *= ECO_DOZE_SIGHT;
    return r;
}
float cret_eco_hear_m(const CretSim* s, float speed, float mass) {
    float r = fminf(ECO_HEAR_M * speed * speed * powf(fmaxf(mass, 0.01f) / ECO_REF_MASS, ECO_HEAR_MASS_EXP), ECO_HEAR_MAX);
    return s->cloud_cover > 0.9f ? r * 0.5f : r;   // rain drowns it out
}
// b is hidden from a: slower than a walk, and the cover on the line between
// them (sampled at a sixth, a half and five sixths of the way) stands taller
// than b by 1 / ECO_HIDE_COVER somewhere: a third of the line.
static bool eco_hidden(const CretSim* s, const CretAgent* a, const CretAgent* b) {
    if (b->speed >= b->walk_speed) return false;
    for (int k = 0; k < 3; k++) {
        float f = (1 + 2 * k) / 6.0f;
        if (b->height < ECO_HIDE_COVER * cret_eco_cover_h(s->world, a->x + (b->x - a->x) * f, a->z + (b->z - a->z) * f)) return true;
    }
    return false;
}
bool cret_eco_sees(const CretSim* s, const CretAgent* a, const CretAgent* b, float d) {
    if (d > (eco_head_down(a) ? ECO_HEAD_DOWN_M : cret_eco_sight_m(s, a))) return false;
    float ang = wrapa(atan2f(b->z - a->z, b->x - a->x) - a->yaw);
    if (fabsf(ang) > ECO_CONE && d > 2 + eco_length(a)) return false;   // behind it, unless brushing past
    return !eco_hidden(s, a, b);
}
// Reaction delay in ticks: it grows with the log of the mass, about 0.2 s for
// a raptor, 0.4 s for a pachycephalosaur or gallimimus, 0.55 s for a
// sauropelta, 0.7 s for a stegosaur and 0.75 s for a tyrannosaur, give or
// take 15% each time from the sim RNG.
static int eco_react_ticks(CretSim* s, const CretAgent* a) {
    float k = clampf(logf(fmaxf(cret_mass(a), 1.0f) / 80.0f) / logf(100.0f), 0, 1);
    float sec = (0.2f + 0.55f * k) * (0.85f + 0.3f * frand(&s->rng));
    int t = (int)(sec * CRET_TICK_HZ + 0.5f); return t > 1 ? t : 1;
}
static bool eco_carn(const CretAgent* a) { return CRET_SPECIES_TABLE[a->species].diet == CRET_DIET_CARNIVORE; }
// What agent a keeps in mind: predators for a herbivore; prey and the
// carnivores that could take it for a carnivore. Its own group never.
static bool eco_relevant(const CretSim* s, const CretAgent* a, const CretAgent* b) {
    if (a->group && b->group == a->group) return false;
    if (!eco_carn(a)) return eco_carn(b);
    return !eco_carn(b) || (b->species != a->species && cret_eco_threat_of(s, a, b) > 0);
}
static CretKnown* eco_slot_for(CretAgent* a, int j) {
    CretKnown* best = NULL;
    for (int k = 0; k < CRET_KNOWN; k++) {
        CretKnown* m = &a->known[k];
        if (m->who == j + 1) return m;
        if (!best || (best->who && (!m->who || m->tick < best->tick))) best = m;   // an empty slot, else the stalest
    }
    memset(best, 0, sizeof *best);
    return best;
}
static void eco_note(CretSim* s, CretAgent* a, int j, bool looking, bool alarm, int delay) {
    const CretAgent* b = &s->agents[j]; CretKnown* m = eco_slot_for(a, j);
    if (m->who != j + 1) { m->who = (uint8_t)(j + 1); m->since = s->tick + delay; }
    else if (alarm && m->since > s->tick + delay) m->since = s->tick + delay;
    m->x = b->x; m->z = b->z; m->vx = b->vx; m->vz = b->vz; m->tick = s->tick;
    m->looking = looking; m->alarm |= alarm;
}
void cret_eco_alarm(CretSim* s, int i, int j, int delay_ticks) {
    CretAgent* a = &s->agents[i]; if (!a->alive || j < 0 || j >= s->cfg.num_agents || !s->agents[j].alive) return;
    eco_note(s, a, j, false, true, delay_ticks);
    int at = s->tick + delay_ticks; if (!a->bot_next || a->bot_next > at) a->bot_next = at;
}
void cret_eco_perceive(CretSim* s, int i) {
    CretAgent* a = &s->agents[i]; int n = s->cfg.num_agents;
    for (int k = 0; k < CRET_KNOWN; k++) {   // forgetting
        CretKnown* m = &a->known[k]; if (!m->who) continue;
        int j = m->who - 1; float age = (s->tick - m->tick) * CRET_DT;
        bool threat = j < n && cret_eco_threat_of(s, a, &s->agents[j]) > 0;
        if (j >= n || !s->agents[j].alive || age > (threat ? ECO_PRED_MEMORY_S : ECO_MEMORY_S)) memset(m, 0, sizeof *m);
    }
    float smell = 0;
    if (eco_carn(a)) { float r[3]; cret_scent_ranges(s, i, false, r); smell = r[CRET_SCENT_ANIMAL]; }
    for (int j = 0; j < n; j++) {
        const CretAgent* b = &s->agents[j];
        if (j == i || !b->alive || !eco_relevant(s, a, b)) continue;
        float dx = b->x - a->x, dz = b->z - a->z, d = sqrtf(dx * dx + dz * dz);
        bool sensed = cret_eco_sees(s, a, b, d) || d < cret_eco_hear_m(s, b->speed, cret_mass(b)) || (b->act == CRET_ACT_CALL && d < ECO_CALL_M) ||
                      (smell > 0 && d < smell * cret_scent_lobe(s, -dx, -dz));
        if (!sensed) continue;
        const CretKnown* old = cret_eco_known(s, i, j);
        eco_note(s, a, j, cret_eco_sees(s, b, a, d), false, old || a->hurt_ticks > 0 ? 0 : eco_react_ticks(s, a));
    }
}
bool cret_eco_aware(const CretSim* s, int i, int j) {
    const CretKnown* k = cret_eco_known(s, i, j);
    return k && (s->tick - k->tick) * CRET_DT <= ECO_AWARE_S;
}
const CretKnown* cret_eco_known(const CretSim* s, int i, int j) {
    const CretAgent* a = &s->agents[i];
    for (int k = 0; k < CRET_KNOWN; k++) if (a->known[k].who == j + 1) return a->known[k].since <= s->tick ? &a->known[k] : NULL;
    return NULL;
}

// ---------------------------------------------------------------- 4. fear
float cret_eco_threat_d(float m_threat, float m_me) {
    float r = m_threat / fmaxf(m_me, 0.01f);
    if (r < ECO_THREAT_MIN) return 0;
    return clampf(log2f(r) + ECO_THREAT_OFS, ECO_THREAT_FLOOR, ECO_THREAT_MAX);
}
float cret_eco_fid(float l_threat, float d, float cover, float food) {
    return fminf((ECO_FID0 + ECO_FID_L * l_threat) * d / (1 + 0.5f * clampf(cover, 0, 1)) * (0.7f + 0.6f * clampf(food, 0, 1)), ECO_FID_MAX);
}
float cret_eco_vigil_interval(int n) { return fminf(ECO_VIG_T * sqrtf((float)(n > 1 ? n : 1)), ECO_VIG_MAX); }
// A pack's members near t count together; *n is how many it is (t included).
static float eco_threat_n(const CretSim* s, const CretAgent* a, const CretAgent* t, int* n) {
    *n = 1;
    if (!eco_carn(t) || t == a || (a->group && t->group == a->group) || (eco_carn(a) && t->species == a->species)) return 0;
    float m = cret_mass(t);
    if (t->group)
        for (int j = 0; j < s->cfg.num_agents; j++) {
            const CretAgent* c = &s->agents[j];
            if (c == t || !c->alive || c->group != t->group) continue;
            float dx = c->x - t->x, dz = c->z - t->z; if (dx * dx + dz * dz < ECO_PACK_M * ECO_PACK_M) { m += cret_mass(c); (*n)++; }
        }
    return cret_eco_threat_d(m, cret_mass(a));
}
float cret_eco_threat_of(const CretSim* s, const CretAgent* a, const CretAgent* t) { int n; return eco_threat_n(s, a, t, &n); }
// A: how much attention a known threat pays it. Coming (closing on it, or
// an alarm said so) 1, eating or resting 0.15, otherwise 0.3. Only what
// the animal could see: the threat's motion and action, not its intent.
static float eco_attention(const CretAgent* a, const CretKnown* k, const CretAgent* t) {
    float dx = a->x - k->x, dz = a->z - k->z, d = fmaxf(sqrtf(dx * dx + dz * dz), 1e-3f);
    float closing = (k->vx * dx + k->vz * dz) / d;
    if (k->alarm || (closing > ECO_CLOSING && closing > ECO_HEADING * hypotf(k->vx, k->vz))) return ECO_A_COMING;
    return t->act == CRET_ACT_EAT || t->act == CRET_ACT_REST ? ECO_A_BUSY : ECO_A_IDLE;
}
static float eco_cover(const CretSim* s, const CretAgent* a) { return clampf(cret_eco_cover_h(s->world, a->x, a->z) / fmaxf(a->height, 0.3f), 0, 1); }
float cret_eco_fear_at(const CretSim* s, float x, float z) {
    float f = 0, day = s->cfg.day_seconds * CRET_TICK_HZ;
    for (int k = 0; k < CRET_FEAR_MARKS; k++) {
        const CretFearMark* m = &s->fear[k]; if (m->w <= 0) continue;
        float age = (s->tick - m->tick) / day; if (age >= 1) continue;
        float dx = m->x - x, dz = m->z - z, d2 = dx * dx + dz * dz; if (d2 >= ECO_FEAR_R * ECO_FEAR_R) continue;
        f += m->w * (1 - age) * (1 - sqrtf(d2) / ECO_FEAR_R);
    }
    return f;
}
float cret_eco_risk(const CretSim* s, int i, float x, float z) {
    const CretAgent* a = &s->agents[i]; float r = 0;
    for (int k = 0; k < CRET_KNOWN; k++) {
        const CretKnown* m = &a->known[k]; if (!m->who || m->since > s->tick) continue;
        // A pack's D is its members' mass together; each member it knows carries its share of it.
        const CretAgent* t = &s->agents[m->who - 1]; int np; float D = eco_threat_n(s, a, t, &np); if (D <= 0) continue;
        float age = (s->tick - m->tick) * CRET_DT, w = (age > ECO_MEMORY_S ? ECO_PRED_FADED : 1) / np;
        float dx = x - m->x, dz = z - m->z, R = ECO_RISK_R0 + ECO_RISK_RV * t->sprint_speed;
        r += w * D * expf(-sqrtf(dx * dx + dz * dz) / R) * eco_attention(a, m, t);
    }
    if (!eco_carn(a)) r += cret_eco_fear_at(s, x, z);
    float ch = cret_eco_cover_h(s->world, x, z);
    if (ch > a->height) r *= ECO_COVER_SMALL;
    else if (a->height > 2.5f && ch > 0.6f * a->height) r *= ECO_COVER_BIG;
    return r;
}
int cret_eco_flight(const CretSim* s, int i, float* dist) {
    const CretAgent* a = &s->agents[i]; int best = -1; float bd = 1e9f, cover = eco_cover(s, a);
    for (int k = 0; k < CRET_KNOWN; k++) {
        const CretKnown* m = &a->known[k]; if (!m->who || m->since > s->tick || s->tick - m->tick > ECO_MEMORY_S * CRET_TICK_HZ) continue;
        const CretAgent* t = &s->agents[m->who - 1]; float D = cret_eco_threat_of(s, a, t);
        if (D <= 0 || eco_attention(a, m, t) < ECO_A_COMING) continue;
        float d = hypotf(m->x - a->x, m->z - a->z);
        if (d < cret_eco_fid(eco_length(t), D, cover, a->food) && d < bd) { bd = d; best = m->who - 1; }
    }
    if (dist) *dist = bd;
    return best;
}
static void eco_mark(CretSim* s, float x, float z, float w) {
    CretFearMark* m = &s->fear[s->fear_head]; s->fear_head = (s->fear_head + 1) % CRET_FEAR_MARKS;
    m->x = x; m->z = z; m->w = w; m->tick = s->tick;
}
void cret_eco_step(CretSim* s) {
    int every = (int)(ECO_FEAR_SCENT_S * CRET_TICK_HZ);
    for (int i = 0; i < s->cfg.num_agents; i++) {
        CretAgent* a = &s->agents[i];
        if (!a->alive) { a->eco_hurt = 0; continue; }
        int by = a->last_attacker;
        if (a->hurt_ticks > a->eco_hurt && !eco_carn(a) && by >= 0 && by < s->cfg.num_agents && eco_carn(&s->agents[by])) eco_mark(s, a->x, a->z, ECO_FEAR_MARK);
        a->eco_hurt = a->hurt_ticks;
        if (eco_carn(a) && (s->tick + i) % every == 0 && a->speed > 0.5f) eco_mark(s, a->x, a->z, ECO_FEAR_SCENT);
    }
}

// ---------------------------------------------------------------- 5. hunting, as arithmetic
// Top speed with injuries: a broken leg limps (0.55, as move_agent), a badly
// hurt animal runs at 0.8 of its sprint.
static float eco_top_speed(const CretAgent* a, float v) {
    if (a->broken > 0) v *= 0.55f;
    if (a->health < 0.6f * a->max_health) v *= 0.8f;
    return v;
}
// Damage per second agent a deals to agent b with its primary (armour off).
static float eco_dps(const CretAgent* a, const CretAgent* b) {
    const CretSpecies* A = &CRET_SPECIES_TABLE[a->species]; const CretSpecies* B = &CRET_SPECIES_TABLE[b->species];
    return A->damage * cret_growth_scale(a->growth) * (1 - B->armor) / fmaxf(A->cooldown, 0.1f);
}
float cret_eco_charge_m(const CretAgent* a) { return clampf(ECO_CHARGE_V * a->sprint_speed, ECO_CHARGE_MIN, ECO_CHARGE_MAX); }
CretHuntEval cret_eco_hunt_eval(const CretSim* s, int i, int j, int pack, bool noticed) {
    const CretAgent* a = &s->agents[i]; const CretAgent* b = &s->agents[j]; CretHuntEval e = {0};
    const CretSpecies* SA = &CRET_SPECIES_TABLE[a->species]; const CretSpecies* SB = &CRET_SPECIES_TABLE[b->species];
    float d = fmaxf(hypotf(b->x - a->x, b->z - a->z) - 0.5f * (eco_length(a) + eco_length(b)), 0), reach = SA->reach * cret_growth_scale(a->growth);
    // P_catch: an unnoticed prey loses its reaction and half its run-up first.
    float vm = eco_top_speed(a, a->sprint_speed), vq = eco_top_speed(b, b->sprint_speed), vt = eco_top_speed(b, b->trot_speed);
    float tm = a->stamina / fmaxf(SA->stamina_drain, 1e-3f), tq = b->stamina / fmaxf(SB->stamina_drain, 1e-3f);
    // The plan: an unaware prey is stalked to the charge distance (at
    // ECO_STALK_V of the walk) and loses its reaction; an aware one lets the
    // stalker come to its flight distance and bolts from there at once.
    float d0 = d, fid = cret_eco_fid(eco_length(a), cret_eco_threat_of(s, b, a), eco_cover(s, b), b->food);
    d = fminf(d, noticed ? fmaxf(fid, cret_eco_charge_m(a)) : cret_eco_charge_m(a));
    float stalk_s = (d0 - d) / fmaxf(ECO_STALK_V * a->walk_speed, 0.3f);
    float lag = noticed ? 0 : 0.4f + 0.5f * vq / fmaxf(SB->accel, 0.5f);
    float gap = fmaxf(d - vm * lag - reach, 0);
    if (gap <= 0) { e.p_catch = noticed ? ECO_CATCH_NOTICED : ECO_CATCH_SURPRISE; e.chase_s = d / fmaxf(vm, 0.5f); }
    else {
        // The gap closes at vm - vq while both sprint, then at vm - vt once the prey tires.
        float t1 = fminf(tm, tq), c1 = vm - vq, t2 = fmaxf(tm - tq, 0), c2 = vm - vt, t;
        if (c1 > 0 && c1 * t1 >= gap) t = gap / c1;
        else if (c2 > 0 && fmaxf(c1, 0) * t1 + c2 * t2 >= gap) t = t1 + (gap - fmaxf(c1, 0) * t1) / c2;
        else t = -1;
        float base = noticed ? ECO_CATCH_NOTICED : ECO_CATCH_SURPRISE;
        if (t >= 0) { e.p_catch = base; e.chase_s = t; }
        else {   // not caught within the predator's stamina: the share of the gap it can close
            float closed = fmaxf(c1, 0) * t1 + fmaxf(c2, 0) * t2;
            e.p_catch = closed > 0 ? fmaxf(base * clampf(closed / gap, 0, 1) * clampf(closed / gap, 0, 1), ECO_CATCH_SLOWER) : ECO_CATCH_SLOWER;
            e.chase_s = tm;
        }
    }
    if (SB->tail || SB->armor > 0.4f) e.p_catch = ECO_CATCH_SURPRISE;   // the armoured stand and fight: the kill decides
    e.p_catch = fminf(e.p_catch + ECO_CATCH_PACK * pack, ECO_CATCH_MAX);
    // P_kill: who would finish the other first, the pack's bites adding up.
    float ttk_me = b->health / fmaxf(eco_dps(a, b) * (1 + pack), 1e-3f), dps_them = eco_dps(b, a);
    float ttk_them = dps_them > 0 ? a->health / dps_them : 1e4f;
    e.p_kill = 1.0f / (1.0f + expf(-(ttk_them - ttk_me) / ECO_KILL_T));
    // What it gains and what it costs, in gut fractions.
    float G = fmaxf(cret_eco_gut_kg(a), 1e-3f), basal = cret_eco_basal(s->cfg.hunger_seconds, cret_mass(a));
    e.meal = fminf(ECO_MEAL_FRAC * cret_mass(b), (1 - a->food) * G) / G;
    e.cost = (e.chase_s * ECO_SPRINT_BURN + stalk_s * ECO_STALK_BURN) * basal / G;
    float lost = clampf(dps_them / (1 + pack) * fminf(ttk_me, ttk_them) / fmaxf(a->health, 1), 0, 1);
    e.danger = ECO_DANGER_W * lost * (1 - ECO_DANGER_FED + ECO_DANGER_FED * a->food);
    e.profit = e.p_catch * e.p_kill * e.meal - e.cost - e.danger;
    float handling = e.meal * G / fmaxf(0.02f * cret_mass(a), 1e-3f) + 10;   // eating at 2% of body mass a second (survive)
    e.rate = e.meal * e.p_catch * e.p_kill / (handling + e.chase_s);
    return e;
}
