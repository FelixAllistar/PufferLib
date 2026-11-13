// Scent and the senses that find small prey (scent.h). Included by sim.c
// after critters.c; uses sim.c's inline maths and the public sim helpers.
#include "scent.h"

// Full ranges (m) of an adult's nose, straight downwind, for critters,
// carcasses and other animals' trails. A raptor hunts by smell close in and
// follows trails well; a tyrannosaur (very large olfactory bulbs) smells
// carrion from far off but is no better than a raptor at a mouse in the
// ferns. Herbivores have no hunting sense. Growth scales them from 0.45 of
// this at hatching to 1 at adult, so a hatchling raptor (growth 0.35) smells
// a mouse at about 18 m straight downwind and 10 m across the wind.
static const float SCENT_RANGE[CRET_SPECIES][3] = {
    [CRET_SP_RAPTOR] = {28, 140, 70},
    [CRET_SP_TREX] = {24, 450, 160},
};

void cret_wind(const CretSim* s, float* wx, float* wz, float* strength) {
    float t = s->tick * CRET_DT, a = 0.6435f + 0.3f * sinf(t * 0.009f);
    float c = s->cloud_cover, oc = clampf((c - 0.68f) / (0.92f - 0.68f), 0, 1); oc = oc * oc * (3 - 2 * oc);
    *wx = cosf(a); *wz = sinf(a); *strength = 0.75f + 0.9f * oc;
}
static float scent_rain(const CretSim* s) { float r = clampf((s->cloud_cover - 0.88f) / (0.97f - 0.88f), 0, 1); return r * r * (3 - 2 * r); }

float cret_scent_lobe(const CretSim* s, float dx, float dz) {
    float wx, wz, w; cret_wind(s, &wx, &wz, &w);
    float l = sqrtf(dx * dx + dz * dz); if (l < 1e-4f) return 1.25f;
    return clampf(0.55f + 0.7f * (dx * wx + dz * wz) / l, 0.1f, 1.25f);
}

void cret_scent_ranges(const CretSim* s, int i, bool sniffing, float out[3]) {
    const CretAgent* a = &s->agents[i]; float g = 0.45f + 0.55f * clampf(a->growth, 0, 1);
    float k = g * (sniffing ? 1.0f : CRET_SCENT_PASSIVE) * (1 - 0.45f * scent_rain(s));
    for (int q = 0; q < 3; q++) out[q] = a->species < CRET_SPECIES ? SCENT_RANGE[a->species][q] * k : 0;
}

float cret_scent_strength(const CretSim* s, int i, bool sniffing, int what, float x, float z, float age, float size) {
    const CretAgent* a = &s->agents[i]; float R[3]; cret_scent_ranges(s, i, sniffing, R);
    float life = CRET_SCENT_LIFE * (1 - 0.6f * scent_rain(s));
    if (R[what] <= 0 || age > life || age < 0) return 0;
    float wx, wz, w; cret_wind(s, &wx, &wz, &w);
    float drift = CRET_SCENT_DRIFT * w * age, px = x + wx * drift, pz = z + wz * drift;   // where the smell has got to
    float dx = a->x - px, dz = a->z - pz, d = sqrtf(dx * dx + dz * dz);
    float reach = R[what] * size * cret_scent_lobe(s, dx, dz);
    if (d >= reach) return 0;
    return (1 - d / reach) * (1 - age / life);
}

// ---------------------------------------------------------------- trails
static void trail_push(CretTrail* t, float x, float z, int tick) {
    t->x[t->head] = x; t->z[t->head] = z; t->tick[t->head] = tick;
    t->head = (uint8_t)((t->head + 1) % CRET_TRAIL_POINTS); if (t->n < CRET_TRAIL_POINTS) t->n++;
}
// Point q of a trail, 0 the newest.
static int trail_at(const CretTrail* t, int q) { return (t->head + CRET_TRAIL_POINTS - 1 - q) % CRET_TRAIL_POINTS; }
// A new point when it has moved a step, or moved at all for a while; a jump
// (respawn, a new critter in the slot, a client's teleport) starts afresh.
// True when it laid a point along a path (not the first point, nor after a jump).
static bool trail_update(CretTrail* t, float x, float z, int tick) {
    if (!t->n) { trail_push(t, x, z, tick); return false; }
    int h = trail_at(t, 0); float dx = x - t->x[h], dz = z - t->z[h], d2 = dx * dx + dz * dz;
    if (d2 > 20.0f * 20.0f) { memset(t, 0, sizeof *t); trail_push(t, x, z, tick); return false; }
    if (d2 >= CRET_TRAIL_STEP * CRET_TRAIL_STEP || (d2 > 0.25f * 0.25f && tick - t->tick[h] >= CRET_TRAIL_IDLE * CRET_TICK_HZ)) { trail_push(t, x, z, tick); return true; }
    return false;
}

void cret_scent_clear(CretSim* s) {
    memset(s->crit_trail, 0, sizeof s->crit_trail); memset(s->agent_trail, 0, sizeof s->agent_trail);
    memset(s->blood, 0, sizeof s->blood); memset(s->blood_skip, 0, sizeof s->blood_skip); s->blood_head = 0;
    memset(s->signs, 0, sizeof s->signs); memset(s->sign_step, 0, sizeof s->sign_step); s->sign_head = 0;
}
float cret_sign_fresh(const CretSim* s, int k) {
    const CretSign* g = &s->signs[k]; if (!g->kind) return 0;
    float life = g->kind == CRET_SIGN_DUNG ? CRET_DUNG_LIFE : CRET_PRINT_LIFE * (1 - 0.67f * scent_rain(s)), age = (s->tick - g->tick) * CRET_DT;
    return age < 0 || age >= life ? 0 : 1 - age / life;
}
static void sign_put(CretSim* s, int i, int kind, float x, float z, float yaw) {
    const CretAgent* a = &s->agents[i];
    if (cret_water(s->world, x, z) > cret_height(s->world, x, z) + 0.05f) return;   // nothing shows under water
    s->signs[s->sign_head] = (CretSign){x, z, yaw, s->tick, (uint8_t)kind, a->species, (uint8_t)(clampf(a->growth, 0, 1) * 255), (uint8_t)i};
    s->sign_head = (s->sign_head + 1) % CRET_SIGNS;
}
// A point of agent i's trail was laid: a footprint every CRET_SIGN_STEP m, left and right in turn,
// pointing along the last stretch of the trail.
static void sign_print(CretSim* s, int i) {
    const CretAgent* a = &s->agents[i]; const CretTrail* t = &s->agent_trail[i];
    int every = (int)(CRET_SIGN_STEP / CRET_TRAIL_STEP + 0.5f), n = ++s->sign_step[i];
    if (n % every || t->n < 2 || a->swimming) return;
    int h0 = trail_at(t, 0), h1 = trail_at(t, 1); float dx = t->x[h0] - t->x[h1], dz = t->z[h0] - t->z[h1];
    if (dx * dx + dz * dz < 0.04f) return;
    float yaw = atan2f(dz, dx), side = ((n / every) & 1 ? 1.0f : -1.0f) * a->radius * 0.6f;
    sign_put(s, i, CRET_SIGN_PRINT, t->x[h0] - sinf(yaw) * side, t->z[h0] + cosf(yaw) * side, yaw);
}
float cret_blood_fresh(const CretSim* s, int k) {
    const CretBlood* b = &s->blood[k]; if (!b->used) return 0;
    float life = CRET_BLOOD_LIFE * (1 - 0.67f * scent_rain(s)), age = (s->tick - b->tick) * CRET_DT;
    return age < 0 || age >= life ? 0 : 1 - age / life;
}
// A point of agent i's trail was laid: blood on it if it bleeds (every
// CRET_BLOOD_STEP m) or is badly hurt (a third as often).
static void blood_drip(CretSim* s, int i) {
    const CretAgent* a = &s->agents[i];
    bool bleeding = a->bleed > 0.002f * a->max_health, hurt = a->health < 0.5f * a->max_health;
    if (!bleeding && !hurt) { s->blood_skip[i] = 0; return; }
    int every = (int)(CRET_BLOOD_STEP / CRET_TRAIL_STEP + 0.5f) * (bleeding ? 1 : 3);
    if (++s->blood_skip[i] < every) return;
    s->blood_skip[i] = 0;
    s->blood[s->blood_head] = (CretBlood){a->x, a->z, s->tick, (uint8_t)i, 1};
    s->blood_head = (s->blood_head + 1) % CRET_BLOOD_MARKS;
}

void cret_scent_record(CretSim* s, bool always) {
    bool people = always;
    for (int i = 0; i < s->cfg.num_agents && !people; i++) people = s->agents[i].alive && cret_is_person(s, i);
    if (!people) return;
    // Each trail is looked at every third tick (staggered), which is plenty at 1.5 m a point.
    for (int i = 0; i < s->cfg.num_agents; i++) {
        if ((i + s->tick) % 3) continue;
        if (!s->agents[i].alive) { s->agent_trail[i].n = 0; continue; }
        if (trail_update(&s->agent_trail[i], s->agents[i].x, s->agents[i].z, s->tick)) { blood_drip(s, i); sign_print(s, i); }
        // Dung: a herbivore, every CRET_DUNG_S s (each at its own time).
        if (CRET_SPECIES_TABLE[s->agents[i].species].diet == CRET_DIET_HERBIVORE && ((s->tick + i) / 3 + i * 131) % (int)(CRET_DUNG_S * CRET_TICK_HZ / 3) == 0)
            sign_put(s, i, CRET_SIGN_DUNG, s->agents[i].x - cosf(s->agents[i].yaw) * s->agents[i].radius, s->agents[i].z - sinf(s->agents[i].yaw) * s->agents[i].radius, s->agents[i].yaw);
    }
    for (int k = 0; k < CRET_MAX_CRITTERS; k++) {
        if ((k + s->tick) % 3) continue;
        const CretCritter* c = &s->critters[k];
        if (!c->active || c->alt > 0.5f) { s->crit_trail[k].n = 0; continue; }
        trail_update(&s->crit_trail[k], c->x, c->z, s->tick);
    }
}

// ---------------------------------------------------------------- queries
static float carcass_size(const CretCarcass* k) {   // a big body smells further than the rest of a mouse
    return clampf(0.5f + 0.25f * log2f(1 + k->meat / 10), 0.5f, 1.5f);
}
static bool scent_wading(const CretSim* s, const CretAgent* a) { return a->swimming || cret_water(s->world, a->x, a->z) > a->y + 0.05f; }
static bool catchable(const CretSim* s, const CretAgent* a, const CretCritter* c) {
    return c->active && c->alt <= 0.5f && (c->kind != CRET_CR_FISH || scent_wading(s, a));
}
// Keeps the hit list sorted by group (critters, carcasses, animals), then distance.
static void scent_add(CretScent* o, const CretScentHit* h) {
    int at = o->n;
    if (at == CRET_SCENT_HITS) {
        const CretScentHit* l = &o->hit[at - 1];
        if (l->what < h->what || (l->what == h->what && l->dist <= h->dist)) return;
        at--;
    } else o->n++;
    while (at > 0 && (o->hit[at - 1].what > h->what || (o->hit[at - 1].what == h->what && o->hit[at - 1].dist > h->dist))) { o->hit[at] = o->hit[at - 1]; at--; }
    o->hit[at] = *h;
}

int cret_scent_query(const CretSim* s, int i, bool sniffing, CretScent* out) {
    memset(out, 0, sizeof *out);
    const CretAgent* a = &s->agents[i];
    cret_scent_ranges(s, i, sniffing, out->range);
    cret_wind(s, &out->wind_x, &out->wind_z, &out->wind); out->rain = scent_rain(s);
    if (!a->alive || out->range[CRET_SCENT_CARCASS] <= 0) return 0;
    // Critters where they are now: the three nearest.
    for (int k = 0; k < CRET_MAX_CRITTERS && s->ncrit; k++) {
        const CretCritter* c = &s->critters[k]; if (!catchable(s, a, c)) continue;
        float st = cret_scent_strength(s, i, sniffing, CRET_SCENT_CRITTER, c->x, c->z, 0, 1); if (st <= 0) continue;
        CretScentHit h = {CRET_SCENT_CRITTER, (int16_t)k, c->x, c->z, c->yaw, 0, hypotf(c->x - a->x, c->z - a->z), st};
        scent_add(out, &h);
    }
    if (out->n > 3) out->n = 3;
    for (int k = 0; k < CRET_MAX_CARCASSES; k++) {
        const CretCarcass* c = &s->carcasses[k]; if (!c->active || c->meat <= 0 || c->carrier) continue;
        float st = cret_scent_strength(s, i, sniffing, CRET_SCENT_CARCASS, c->x, c->z, 0, carcass_size(c)); if (st <= 0) continue;
        CretScentHit h = {CRET_SCENT_CARCASS, (int16_t)k, c->x, c->z, c->yaw, c->age * CRET_DT, hypotf(c->x - a->x, c->z - a->z), st};
        scent_add(out, &h);
    }
    // Other animals: the freshest point of each trail that reaches the nose,
    // never one laid in the last CRET_SCENT_MIN_AGE s (where the animal stands).
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretTrail* t = &s->agent_trail[j]; if (j == i || !s->agents[j].alive || t->n < 2) continue;
        for (int q = 0; q < t->n; q++) {
            int p = trail_at(t, q); float age = (s->tick - t->tick[p]) * CRET_DT;
            if (age < CRET_SCENT_MIN_AGE) continue;
            float st = cret_scent_strength(s, i, sniffing, CRET_SCENT_ANIMAL, t->x[p], t->z[p], age, 1); if (st <= 0) continue;
            int nx = q > 0 ? trail_at(t, q - 1) : -1;   // the next point it went on to
            float dir = nx >= 0 ? atan2f(t->z[nx] - t->z[p], t->x[nx] - t->x[p]) : s->agents[j].yaw;
            CretScentHit h = {CRET_SCENT_ANIMAL, (int16_t)j, t->x[p], t->z[p], dir, age, hypotf(t->x[p] - a->x, t->z[p] - a->z), st};
            scent_add(out, &h);
            break;
        }
    }
    return out->n;
}

// ---------------------------------------------------------------- noticing prey
// Sight: a moving critter in front is seen at 16 m, a still one at 7 m and
// a hiding one at 3 m, less in thick plants and at night. Hearing: one
// running or walking close by, all round. Smell: cret_scent_strength.
int cret_critter_sensed(const CretSim* s, int i, int k, bool sniffing) {
    const CretAgent* a = &s->agents[i]; const CretCritter* c = &s->critters[k];
    if (!a->alive || !c->active) return CRET_SENSE_NONE;
    float dx = c->x - a->x, dz = c->z - a->z, d = sqrtf(dx * dx + dz * dz);
    if (c->speed > 0.2f && d <= 6.0f + 1.5f * c->speed) return CRET_SENSE_HEARD;
    float tod = s->time_of_day, day = clampf((0.5f - fabsf(tod - 0.5f) - 0.22f) / 0.06f, 0, 1);   // 1 in daylight, 0 from dusk to dawn
    float sight = (c->state == CRET_CRS_HIDE ? 3.0f : c->speed > 0.2f ? 16.0f : 7.0f) * (1 - 0.45f * clampf(cret_forage(s->world, c->x, c->z), 0, 1)) * (0.5f + 0.5f * day);
    if (d <= sight && fabsf(wrapa(atan2f(dz, dx) - a->yaw)) < 1.4f) return CRET_SENSE_SEEN;
    if (cret_scent_strength(s, i, sniffing, CRET_SCENT_CRITTER, c->x, c->z, 0, 1) > 0) return CRET_SENSE_SMELLED;
    return CRET_SENSE_NONE;
}

int cret_prey_noticed(const CretSim* s, int i, bool sniffing, float range, float* dist, int* how) {
    if (!s->ncrit) return -1;
    const CretAgent* a = &s->agents[i]; int best = -1, bh = 0; float bd = range * range;
    for (int k = 0; k < CRET_MAX_CRITTERS; k++) {
        const CretCritter* c = &s->critters[k]; if (!catchable(s, a, c)) continue;
        float dx = c->x - a->x, dz = c->z - a->z, d = dx * dx + dz * dz; if (d >= bd) continue;
        int h = cret_critter_sensed(s, i, k, sniffing); if (!h) continue;
        bd = d; best = k; bh = h;
    }
    if (best >= 0) { if (dist) *dist = sqrtf(bd); if (how) *how = bh; }
    return best;
}
