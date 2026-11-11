#include "progress.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline uint32_t xs32(uint32_t* s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x ? x : 0x9e3779b9u; }
static inline float frand(uint32_t* s) { return (xs32(s) >> 8) * (1.0f / 16777216.0f); }

// ---------------------------------------------------------------- places
static const char* KIND[CRET_POI_KINDS] = {"lake", "river", "meadow", "grove", "marsh", "peak", "beach"};
const char* cret_poi_kind_name(int kind) { return kind >= 0 && kind < CRET_POI_KINDS ? KIND[kind] : "place"; }

static const char* ADJ[] = {"Misty", "Amber", "Silent", "Broken", "Old", "Red", "Ash", "Thunder", "Bone", "Whisper", "Sun", "Moon",
                            "Fern", "Long", "Deep", "High", "Hollow", "Raptor", "Tyrant", "Horn", "Crested", "Copper", "Mossy", "Shale",
                            "Ember", "Grey", "Jade", "Cinder", "Feather", "Talon", "Crooked", "Still"};
static const char* NOUN[CRET_POI_KINDS][4] = {
    {"Lake", "Mere", "Pool", "Tarn"}, {"Ford", "Crossing", "Shallows", "Rapids"}, {"Meadow", "Fernfield", "Glade", "Flats"},
    {"Grove", "Thicket", "Cycad Wood", "Stand"}, {"Marsh", "Fen", "Reedbed", "Mire"}, {"Peak", "Crag", "Tor", "Summit"},
    {"Beach", "Strand", "Shore", "Sands"}};

static bool far_from(const CretWorld* w, float x, float z, float d) {
    for (int k = 0; k < w->npois; k++) { float dx = w->pois[k].x - x, dz = w->pois[k].z - z; if (dx * dx + dz * dz < d * d) return false; }
    return true;
}
// Nearest walkable point to (x, z) within `reach` m (rings of samples).
static bool anchor(const CretWorld* w, float x, float z, float reach, float* ax, float* az) {
    if (cret_walkable(w, x, z)) { *ax = x; *az = z; return true; }
    for (float r = 8; r <= reach; r += 8)
        for (int q = 0; q < 16; q++) {
            float a = q * 0.3927f, px = x + cosf(a) * r, pz = z + sinf(a) * r;
            if (cret_walkable(w, px, pz)) { *ax = px; *az = pz; return true; }
        }
    return false;
}
static void add_poi(CretWorld* w, int kind, float x, float z, float r) {
    if (w->npois >= CRET_MAX_POIS) return;
    CretPoi* p = &w->pois[w->npois]; p->kind = (uint8_t)kind; p->x = x; p->z = z; p->r = r;
    uint32_t h = (uint32_t)(x * 7.31f) * 2654435761u ^ (uint32_t)(z * 3.17f) * 40503u ^ (uint32_t)kind * 97u ^ w->params.seed * 0x9e3779b9u;
    for (int t = 0; t < 32; t++) {   // a name no other place has
        xs32(&h); const char* adj = ADJ[h % (sizeof ADJ / sizeof ADJ[0])]; xs32(&h); const char* noun = NOUN[kind][h % 4];
        if (kind == CRET_POI_LAKE && (h >> 8) % 2) snprintf(p->name, sizeof p->name, "Lake %s", adj);   // "Lake Amber"
        else snprintf(p->name, sizeof p->name, "%s %s", adj, noun);                                     // "Amber Mere"
        bool dup = false; for (int k = 0; k < w->npois; k++) if (!strcmp(w->pois[k].name, p->name)) dup = true;
        if (!dup) break;
    }
    w->npois++;
}

// Places from the water, vegetation and relief, on 32 m blocks: the largest
// lakes (anchor on the shore), the biggest rivers (a ford), the thickest
// fern, cycad and horsetail stands, the highest walkable ground and sandy
// beaches. Spaced so quests spread over the island.
void cret_pois_build(CretWorld* w) {
    if (w->pois_ok) return;
    w->pois_ok = true; w->npois = 0;
    const TerraMap* m = &w->map; int n = m->n, B = 8, C = n / B; float bs = m->cell * B;
    if (C < 8) return;
    size_t NC = (size_t)C * C;
    float* lake = calloc(NC, sizeof(float)); float* river = calloc(NC, sizeof(float)); float* sea = calloc(NC, sizeof(float));
    float* hgt = calloc(NC, sizeof(float)); float* flow = calloc(NC, sizeof(float)); float* sand = calloc(NC, sizeof(float));
    float* veg = calloc(NC * 4, sizeof(float));   // fern, cycad, horsetail, trees
    for (int z = 0; z < C * B; z++) for (int x = 0; x < C * B; x++) {
        size_t i = (size_t)z * n + x, c = (size_t)(z / B) * C + x / B; int wk = m->water_kind[i];
        lake[c] += wk == TERRA_WATER_LAKE; river[c] += wk == TERRA_WATER_RIVER; sea[c] += wk == TERRA_WATER_SEA;
        hgt[c] += m->height[i]; if (wk == TERRA_WATER_RIVER && m->flow[i] > flow[c]) flow[c] = m->flow[i];
        sand[c] += m->splat[i * TERRA_MAT_COUNT + TERRA_MAT_SAND];
        const uint8_t* v = m->veg + i * TERRA_VEG_COUNT;
        veg[c * 4] += v[TERRA_VEG_FERN]; veg[c * 4 + 1] += v[TERRA_VEG_CYCAD]; veg[c * 4 + 2] += v[TERRA_VEG_HORSETAIL];
        veg[c * 4 + 3] += v[TERRA_VEG_CONIFER] + v[TERRA_VEG_BROADLEAF] + v[TERRA_VEG_TREEFERN];
    }
    float inv = 1.0f / (B * B);
    for (size_t c = 0; c < NC; c++) { lake[c] *= inv; river[c] *= inv; sea[c] *= inv; hgt[c] *= inv; sand[c] *= inv; for (int k = 0; k < 4; k++) veg[c * 4 + k] *= inv; }
    #define CX(c) (((c) % C + 0.5f) * bs)
    #define CZ(c) (((c) / C + 0.5f) * bs)
    // Lakes: connected blocks, largest first; the anchor is the dry shore block nearest the centre.
    int* comp = malloc(sizeof(int) * NC); int* stack = malloc(sizeof(int) * NC);
    for (size_t c = 0; c < NC; c++) comp[c] = -1;
    typedef struct { int size; float cx, cz; int seed; } Lake;
    Lake lakes[64]; int nl = 0, ncomp = 0;
    for (size_t c0 = 0; c0 < NC; c0++) {
        if (lake[c0] < 0.25f || comp[c0] >= 0) continue;
        int sp = 0, size = 0; float sx = 0, sz = 0; stack[sp++] = (int)c0; comp[c0] = ncomp;
        while (sp) {
            int c = stack[--sp]; size++; sx += CX(c); sz += CZ(c);
            int cx = c % C, cz = c / C;
            for (int d = 0; d < 4; d++) {
                int nx = cx + (d == 0) - (d == 1), nz = cz + (d == 2) - (d == 3);
                if (nx < 0 || nz < 0 || nx >= C || nz >= C) continue;
                int q = nz * C + nx; if (lake[q] >= 0.25f && comp[q] < 0) { comp[q] = ncomp; stack[sp++] = q; }
            }
        }
        if (size >= 3) {
            Lake L = {size, sx / size, sz / size, (int)c0};
            if (nl < 64) lakes[nl++] = L;
            else for (int k = 0; k < 64; k++) if (lakes[k].size < size) { lakes[k] = L; break; }
        }
        ncomp++;
    }
    for (int a = 0; a < nl; a++) for (int b = a + 1; b < nl; b++) if (lakes[b].size > lakes[a].size) { Lake t = lakes[a]; lakes[a] = lakes[b]; lakes[b] = t; }
    int nlakes = 0;
    for (int k = 0; k < nl && nlakes < 8; k++) {
        int id = comp[lakes[k].seed], best = -1; float bd = 1e30f;
        for (size_t c = 0; c < NC; c++) {
            if (comp[c] >= 0 || lake[c] > 0 || sea[c] > 0 || river[c] > 0.3f) continue;
            int cx = (int)(c % C), cz = (int)(c / C); bool shore = false;
            for (int d = 0; d < 4 && !shore; d++) {
                int nx = cx + (d == 0) - (d == 1), nz = cz + (d == 2) - (d == 3);
                if (nx >= 0 && nz >= 0 && nx < C && nz < C && comp[nz * C + nx] == id) shore = true;
            }
            if (!shore || !cret_walkable(w, CX(c), CZ(c))) continue;
            float dx = CX(c) - lakes[k].cx, dz = CZ(c) - lakes[k].cz, d = dx * dx + dz * dz; if (d < bd) { bd = d; best = (int)c; }
        }
        if (best < 0 || !far_from(w, CX(best), CZ(best), 300)) continue;
        float r = sqrtf(lakes[k].size * bs * bs / 3.14159f) + 40;
        add_poi(w, CRET_POI_LAKE, CX(best), CZ(best), fminf(r, 400)); nlakes++;
    }
    // Rivers: the biggest flow away from the coast and the lakes.
    for (int k = 0; k < 6; k++) {
        int best = -1; float bf = 0;
        for (size_t c = 0; c < NC; c++) if (river[c] > 0 && hgt[c] > 3 && hgt[c] < 200 && flow[c] > bf && far_from(w, CX(c), CZ(c), 450)) { bf = flow[c]; best = (int)c; }
        float ax, az; if (best < 0) break;
        if (anchor(w, CX(best), CZ(best), 80, &ax, &az)) add_poi(w, CRET_POI_RIVER, ax, az, 60);
        else flow[best] = 0;
    }
    // Forage stands: fern meadows (open), cycad groves, horsetail marshes.
    for (int kind = CRET_POI_MEADOW; kind <= CRET_POI_MARSH; kind++)
        for (int k = 0; k < 4; k++) {
            int best = -1; float bsc = 20;
            for (size_t c = 0; c < NC; c++) {
                if (lake[c] + river[c] + sea[c] > 0) continue;
                float f = veg[c * 4 + (kind - CRET_POI_MEADOW)], trees = veg[c * 4 + 3];
                float sc = kind == CRET_POI_MEADOW ? f - 0.6f * trees : kind == CRET_POI_GROVE ? f - 0.3f * trees : f;
                if (sc > bsc && far_from(w, CX(c), CZ(c), 450) && cret_walkable(w, CX(c), CZ(c)) && cret_forage(w, CX(c), CZ(c)) > 0.25f) { bsc = sc; best = (int)c; }
            }
            if (best >= 0) add_poi(w, kind, CX(best), CZ(best), 70);
        }
    // Peaks: the highest ground with a walkable spot within 60 m (summits themselves are often cliffs).
    for (int k = 0; k < 2; k++) {
        float ax = 0, az = 0, bh = 20; bool found = false;
        for (int tries = 0; tries < 40; tries++) {
            int best = -1; float hb = bh;
            for (size_t c = 0; c < NC; c++) if (hgt[c] > hb && far_from(w, CX(c), CZ(c), 1800)) { hb = hgt[c]; best = (int)c; }
            if (best < 0) break;
            if (anchor(w, CX(best), CZ(best), 60, &ax, &az)) { found = true; break; }
            hgt[best] = 0;   // unreachable: try the next highest
        }
        if (found) add_poi(w, CRET_POI_PEAK, ax, az, 45);
    }
    // Beaches: sandy, low blocks with the sea close by.
    for (int k = 0; k < 2; k++) {
        int best = -1; float bsd = 90;
        for (size_t c = 0; c < NC; c++) {
            if (sand[c] < bsd || hgt[c] < 0.5f || hgt[c] > 8 || sea[c] > 0.2f) continue;
            int cx = (int)(c % C), cz = (int)(c / C); bool coast = false;
            for (int dz = -3; dz <= 3 && !coast; dz++) for (int dx = -3; dx <= 3; dx++) {
                int nx = cx + dx, nz = cz + dz; if (nx >= 0 && nz >= 0 && nx < C && nz < C && sea[nz * C + nx] > 0.5f) { coast = true; break; }
            }
            if (coast && far_from(w, CX(c), CZ(c), 1500) && cret_walkable(w, CX(c), CZ(c))) { bsd = sand[c]; best = (int)c; }
        }
        if (best >= 0) add_poi(w, CRET_POI_BEACH, CX(best), CZ(best), 60);
    }
    #undef CX
    #undef CZ
    free(lake); free(river); free(sea); free(hgt); free(flow); free(sand); free(veg); free(comp); free(stack);
}

// ---------------------------------------------------------------- quests
#define QUEST_SECONDS (20 * 60)
#define NEAR_QUEST_SECONDS (8 * 60)   // a hatchling's errand close by

// How far quests send an animal: hatchlings about 150 m, adults (growth 0.8
// and up) the 300-400 m of the named places. A hatchling walks at about
// 1.7 m/s and starves in 15 minutes, so a 400 m errand used most of a meal.
static float quest_range(const CretAgent* a) { return 150.0f + 250.0f * fminf(fmaxf((a->growth - 0.35f) / 0.45f, 0), 1); }
// Places are picked nearest to the range, however far that is; the young
// (below growth 0.8) turn down one more than three times their range off
// (a hatchling was sent 2.6 km to drink) and are offered another later.
static bool too_far(const CretSim* s, const CretAgent* a, int p) {
    if (p < 0) return true;
    float dx = s->world->pois[p].x - a->x, dz = s->world->pois[p].z - a->z;
    return a->growth < 0.8f && dx * dx + dz * dz > 9 * quest_range(a) * quest_range(a);
}

static bool kind_in(int kind, uint32_t mask) { return (mask >> kind) & 1; }
// A place of the given kinds about `want` m away (with some noise), not in `avoid`.
static int pick_poi(CretSim* s, float x, float z, uint32_t mask, float want, const uint8_t* avoid, int navoid) {
    const CretWorld* w = s->world; int best = -1; float bs = 1e30f;
    for (int k = 0; k < w->npois; k++) {   // every place, scored by the distance error with some noise
        if (!kind_in(w->pois[k].kind, mask)) continue;
        bool skip = false; for (int q = 0; q < navoid; q++) if (avoid[q] == k) skip = true;
        if (skip) continue;
        float dx = w->pois[k].x - x, dz = w->pois[k].z - z, sc = fabsf(sqrtf(dx * dx + dz * dz) - want) * (0.5f + frand(&s->rng));
        if (sc < bs) { bs = sc; best = k; }
    }
    return best;
}

#define M_WATER ((1u << CRET_POI_LAKE) | (1u << CRET_POI_RIVER))
#define M_FORAGE ((1u << CRET_POI_MEADOW) | (1u << CRET_POI_GROVE) | (1u << CRET_POI_MARSH))
#define M_ANY 0xffu

// Prey a predator's quest asks for: herbivores in this world it can take
// (a raptor not an adult sauropelta), weighted toward the common ones.
static int pick_prey(CretSim* s, int i) {
    const CretAgent* a = &s->agents[i]; int cand[CRET_MAX_AGENTS], n = 0;
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* b = &s->agents[j];
        if (j == i || CRET_SPECIES_TABLE[b->species].diet != CRET_DIET_HERBIVORE || b->species == a->species) continue;
        if (a->species == CRET_SP_RAPTOR && b->species == CRET_SP_SAUROPELTA) continue;
        cand[n++] = b->species;
    }
    return n ? cand[(int)(frand(&s->rng) * n) % n] : -1;
}

// The nearest drinking spot within 150 m: a dry, walkable point a few
// metres short of water (rings outward, so the first found is the nearest).
static bool water_spot(CretSim* s, const CretAgent* a, float* x, float* z) {
    const CretWorld* w = s->world;
    for (float r = 15; r <= 150; r += 10) {
        float off = frand(&s->rng) * 6.2831853f;
        for (int q = 0; q < 24; q++) {
            float ang = off + q * 0.2618f, px = a->x + cosf(ang) * r, pz = a->z + sinf(ang) * r;
            if (cret_water(w, px, pz) < cret_height(w, px, pz) + 0.3f) continue;
            float bx = a->x + cosf(ang) * (r - 5), bz = a->z + sinf(ang) * (r - 5);
            if (cret_walkable(w, bx, bz)) { *x = bx; *z = bz; return true; }
        }
    }
    return false;
}
// The best of 24 walkable points 60-140 m away by `score` (small prey for a
// predator, plants for a grazer), with some noise so quests vary.
static bool near_spot(CretSim* s, const CretAgent* a, bool herb, float* x, float* z) {
    float bs = 0; bool found = false;
    for (int t = 0; t < 24; t++) {
        float ang = frand(&s->rng) * 6.2831853f, d = 60 + 80 * frand(&s->rng);
        float px = a->x + cosf(ang) * d, pz = a->z + sinf(ang) * d;
        if (!cret_walkable(s->world, px, pz)) continue;
        float sc = herb ? cret_forage(s->world, px, pz)
                        : cret_critter_habitat(s, CRET_CR_MOUSE, px, pz) + cret_critter_habitat(s, CRET_CR_LIZARD, px, pz);
        sc *= 0.8f + 0.4f * frand(&s->rng);
        if (sc > bs) { bs = sc; *x = px; *z = pz; found = true; }
    }
    return found;
}
// Hatchlings (people below growth 0.5) get short errands close by: drink at
// the nearest water when thirsty, else catch three small prey (predators)
// or graze (herbivores) at a good spot 60-140 m away. Small pay: they are
// mostly there to lead a new player to food.
static bool near_quest(CretSim* s, int i, CretQuest* q) {
    const CretAgent* a = &s->agents[i]; bool herb = CRET_SPECIES_TABLE[a->species].diet == CRET_DIET_HERBIVORE;
    float x, z; int kind;
    if (a->water < 0.6f && water_spot(s, a, &x, &z)) kind = CRET_Q_DRINK;
    else if ((herb || s->crit_cfg.enabled) && near_spot(s, a, herb, &x, &z)) kind = herb ? CRET_Q_GATHER : CRET_Q_CATCH;   // no catching with critters off
    else return false;
    memset(q, 0, sizeof *q);
    q->kind = (uint8_t)kind; q->steps = 1; q->expires = s->tick + NEAR_QUEST_SECONDS * CRET_TICK_HZ;
    q->spot = 1; q->spot_x = x; q->spot_z = z; q->spot_r = kind == CRET_Q_DRINK ? 25 : 40;
    q->goal = kind == CRET_Q_DRINK ? 6 : kind == CRET_Q_CATCH ? 3 : 12; q->xp = 0.04f; q->marks = 15;
    return true;
}

static bool make_quest(CretSim* s, int i, CretQuest* q, bool group) {
    const CretAgent* a = &s->agents[i]; bool herb = CRET_SPECIES_TABLE[a->species].diet == CRET_DIET_HERBIVORE;
    if (!group && a->growth < 0.5f && cret_is_person(s, i) && near_quest(s, i, q)) return true;
    if (!group && a->group && !cret_is_person(s, i))   // a herd or pack bot takes on its leader's errand, so the group travels as one
        for (int j = 0; j < s->cfg.num_agents; j++) {
            if (!s->agents[j].alive || s->agents[j].group != a->group) continue;
            const CretQuest* lq = &s->prog[j].quest;
            if (j != i && (lq->kind == CRET_Q_DRINK || lq->kind == CRET_Q_GATHER || lq->kind == CRET_Q_PATROL)) {
                *q = *lq; q->step = 0; q->progress = 0; q->expires = s->tick + QUEST_SECONDS * CRET_TICK_HZ; return true;
            }
            break;
        }
    float r = frand(&s->rng); int kind;
    if (group) kind = herb ? (r < 0.5f ? CRET_Q_GATHER : CRET_Q_PATROL) : (r < 0.6f ? CRET_Q_HUNT : CRET_Q_PATROL);
    else if (herb) kind = r < 0.3f ? CRET_Q_DRINK : r < 0.65f ? CRET_Q_GATHER : r < 0.9f ? CRET_Q_PATROL : CRET_Q_DEFEND;
    else kind = r < 0.3f ? CRET_Q_DRINK : r < 0.75f ? CRET_Q_HUNT : CRET_Q_PATROL;
    memset(q, 0, sizeof *q);
    q->group = group ? a->group : 0; q->expires = s->tick + QUEST_SECONDS * CRET_TICK_HZ; q->steps = 1;
    switch (kind) {
    case CRET_Q_DRINK: {
        int p = pick_poi(s, a->x, a->z, M_WATER, quest_range(a), NULL, 0); if (too_far(s, a, p)) return false;
        q->poi[0] = (uint8_t)p; q->goal = 8; q->xp = 0.05f; q->marks = 20; break; }
    case CRET_Q_GATHER: {
        int p = pick_poi(s, a->x, a->z, M_FORAGE, quest_range(a), NULL, 0); if (too_far(s, a, p)) return false;
        q->poi[0] = (uint8_t)p; q->goal = group ? 40 : 20; q->xp = 0.06f; q->marks = 25; break; }
    case CRET_Q_PATROL: {
        float x = a->x, z = a->z;
        for (int k = 0; k < 3; k++) {
            int p = pick_poi(s, x, z, M_ANY, quest_range(a) * (k ? 0.8f : 0.95f), q->poi, k); if (p < 0 || (k == 0 && too_far(s, a, p))) return false;
            q->poi[k] = (uint8_t)p; x = s->world->pois[p].x; z = s->world->pois[p].z;
        }
        q->steps = 3; q->goal = 3; q->xp = 0.08f; q->marks = 40; break; }
    case CRET_Q_HUNT: {
        int sp = pick_prey(s, i); if (sp < 0) return false;
        q->species = (uint8_t)sp; q->goal = group ? 2 : 1; q->xp = 0.10f; q->marks = 50; break; }
    default:   // defend: wound predators by a quarter of their health in all
        q->goal = 0.25f; q->xp = 0.10f; q->marks = 50; break;
    }
    q->kind = (uint8_t)kind;
    if (group) { q->xp *= 1.5f; q->marks = q->marks * 3 / 2; }
    return true;
}

void cret_quest_reset(CretSim* s, int i) {
    memset(&s->prog[i].quest, 0, sizeof s->prog[i].quest); s->prog[i].quest.wait = 5 * CRET_TICK_HZ;
}

const CretQuest* cret_quest_of(const CretSim* s, int i, int which) {
    if (which == 0) return s->prog[i].quest.kind ? &s->prog[i].quest : NULL;
    int g = s->agents[i].group; return g && s->gquest[g].kind ? &s->gquest[g] : NULL;
}

bool cret_quest_target(const CretSim* s, const CretQuest* q, float* x, float* z, float* r) {
    if (q && q->kind && q->spot) { *x = q->spot_x; *z = q->spot_z; if (r) *r = q->spot_r; return true; }
    if (!q || !q->kind || q->kind == CRET_Q_HUNT || q->kind == CRET_Q_DEFEND || q->kind == CRET_Q_CATCH || !s->world->npois) return false;
    int p = q->poi[q->step < q->steps ? q->step : 0]; if (p >= s->world->npois) return false;
    const CretPoi* P = &s->world->pois[p];
    *x = P->x; *z = P->z; if (r) *r = q->kind == CRET_Q_PATROL ? fmaxf(30, P->r * 0.4f) : P->r;
    return true;
}

void cret_quest_text(const CretSim* s, const CretQuest* q, char* title, int nt, char* prog, int np) {
    const CretWorld* w = s->world; const char* place = q->poi[q->step < q->steps ? q->step : 0] < w->npois ? w->pois[q->poi[q->step < q->steps ? q->step : 0]].name : "?";
    if (q->spot) place = q->kind == CRET_Q_DRINK ? "the water nearby" : "the plants nearby";
    switch (q->kind) {
    case CRET_Q_DRINK: snprintf(title, nt, "Drink at %s", place); snprintf(prog, np, "%d / %d s", (int)q->progress, (int)q->goal); break;
    case CRET_Q_GATHER: snprintf(title, nt, "Graze at %s", place); snprintf(prog, np, "%d / %d s", (int)q->progress, (int)q->goal); break;
    case CRET_Q_PATROL: snprintf(title, nt, "Patrol: %s", place); snprintf(prog, np, "waypoint %d / %d", q->step + 1, q->steps); break;
    case CRET_Q_HUNT: snprintf(title, nt, "Hunt %s", CRET_SPECIES_TABLE[q->species % CRET_SPECIES].name); snprintf(prog, np, "%d / %d", (int)q->progress, (int)q->goal); break;
    case CRET_Q_CATCH: snprintf(title, nt, "Catch small prey nearby"); snprintf(prog, np, "%d / %d", (int)q->progress, (int)q->goal); break;
    case CRET_Q_DEFEND: snprintf(title, nt, "Drive off predators"); snprintf(prog, np, "%d%%", (int)(100 * q->progress / fmaxf(q->goal, 1e-3f))); break;
    default: snprintf(title, nt, "-"); prog[0] = 0;
    }
}

// Growth for the young (adults get half as many marks again instead).
static void reward(CretSim* s, int i, const CretQuest* q) {
    CretAgent* a = &s->agents[i]; int marks = q->marks;
    if (a->growth < 1) { a->growth = fminf(a->growth + q->xp, 1.0f); cret_apply_growth(a); } else marks = marks * 3 / 2;
    s->prog[i].marks += marks; s->prog[i].quests_done++; a->ev_quest++; s->stats.marks_earned += marks;
}

static void complete(CretSim* s, int i, CretQuest* q) {
    s->stats.quests[q->kind]++;
    if (q->group) {
        s->stats.group_quests++;
        for (int j = 0; j < s->cfg.num_agents; j++) if (s->agents[j].alive && s->agents[j].group == q->group) reward(s, j, q);
        memset(q, 0, sizeof *q); q->wait = 60 * CRET_TICK_HZ;
    } else {
        reward(s, i, q);
        memset(q, 0, sizeof *q); q->wait = 10 * CRET_TICK_HZ;
    }
}

static bool at_poi(const CretSim* s, const CretAgent* a, int p, float r) {
    if (p >= s->world->npois) return false;
    float dx = s->world->pois[p].x - a->x, dz = s->world->pois[p].z - a->z; return dx * dx + dz * dz < r * r;
}

// Time-and-place quests advance while the agent does the thing there.
static void advance(CretSim* s, int i, CretQuest* q) {
    const CretAgent* a = &s->agents[i]; float dt = CRET_DT;
    if (!q->kind) return;
    const CretPoi* P = q->poi[q->step] < s->world->npois ? &s->world->pois[q->poi[q->step]] : NULL;
    // In the place: the named one, or a hatchling's spot.
    bool here = q->spot ? (q->spot_x - a->x) * (q->spot_x - a->x) + (q->spot_z - a->z) * (q->spot_z - a->z) < q->spot_r * q->spot_r
                        : P && at_poi(s, a, q->poi[0], P->r);
    if (q->kind == CRET_Q_DRINK) {
        if (here && a->act == CRET_ACT_DRINK && cret_water_ahead(s, a)) q->progress += dt;
    } else if (q->kind == CRET_Q_GATHER) {
        if (here && a->act == CRET_ACT_EAT && CRET_SPECIES_TABLE[a->species].diet == CRET_DIET_HERBIVORE) {
            float L = CRET_SPECIES_TABLE[a->species].length * cret_growth_scale(a->growth) * 0.45f;
            if (cret_forage(s->world, a->x + cosf(a->yaw) * L, a->z + sinf(a->yaw) * L) > 0.1f) q->progress += dt;
        }
    } else if (q->kind == CRET_Q_PATROL) {
        if (P && at_poi(s, a, q->poi[q->step], fmaxf(30, P->r * 0.4f))) { q->step++; q->progress = q->step; }
    }
    if (q->progress >= q->goal) complete(s, i, q);
}

void cret_quest_on_kill(CretSim* s, int killer, int victim) {
    if (killer < 0 || killer >= s->cfg.num_agents) return;
    int sp = s->agents[victim].species; CretAgent* a = &s->agents[killer];
    CretQuest* q = &s->prog[killer].quest;
    if (q->kind == CRET_Q_HUNT && q->species == sp && (q->progress += 1) >= q->goal) complete(s, killer, q);
    if (a->group) { q = &s->gquest[a->group]; if (q->kind == CRET_Q_HUNT && q->species == sp && (q->progress += 1) >= q->goal) complete(s, killer, q); }
}

void cret_quest_on_catch(CretSim* s, int i, int kind) {
    CretQuest* q = &s->prog[i].quest; (void)kind;   // any small prey counts, wherever it is caught
    if (q->kind == CRET_Q_CATCH && (q->progress += 1) >= q->goal) complete(s, i, q);
}

void cret_quest_on_damage(CretSim* s, int attacker, int victim, float frac) {
    if (CRET_SPECIES_TABLE[s->agents[attacker].species].diet != CRET_DIET_HERBIVORE || CRET_SPECIES_TABLE[s->agents[victim].species].diet != CRET_DIET_CARNIVORE) return;
    CretQuest* q = &s->prog[attacker].quest;
    if (q->kind == CRET_Q_DEFEND && (q->progress += frac) >= q->goal) complete(s, attacker, q);
}

// ---------------------------------------------------------------- nests
#define NEST_BUILD_TICKS (5 * CRET_TICK_HZ)
#define EGG_SECONDS 90
#define NEST_MAX_EGGS 5

int cret_nest_of(const CretSim* s, int i) {
    const CretAgent* a = &s->agents[i];
    for (int k = 0; k < CRET_MAX_NESTS; k++) {
        const CretNest* n = &s->nests[k];
        if (n->active && n->species == a->species && (n->owner == i + 1 || (a->group && n->group == a->group))) return k;
    }
    return -1;
}

int cret_nest_near(const CretSim* s, float x, float z, float r, int except_group) {
    int best = -1; float bd = r * r;
    for (int k = 0; k < CRET_MAX_NESTS; k++) {
        const CretNest* n = &s->nests[k]; if (!n->active || n->eggs <= 0 || (except_group && n->group == except_group)) continue;
        float dx = n->x - x, dz = n->z - z, d = dx * dx + dz * dz; if (d < bd) { bd = d; best = k; }
    }
    return best;
}

int cret_nest_spawn(CretSim* s, int i, int species, int group) {
    for (int k = 0; k < CRET_MAX_NESTS; k++) {
        CretNest* n = &s->nests[k];
        if (!n->active || n->eggs <= 0 || n->species != species) continue;
        if (group >= 0 ? !(n->owner == i + 1 || (group && n->group == group))   // a person: its own or its group's nest
                       : (n->group && cret_group_size(s, n->group) >= CRET_SPECIES_TABLE[species].group_max)) continue;   // a bot: one with room
        n->eggs--; s->stats.nest_spawns++;
        return k;
    }
    return -1;
}

float cret_nest_radius(int species) {
    float L = CRET_SPECIES_TABLE[species < CRET_SPECIES ? species : 0].length;
    return 0.6f + 0.13f * L;   // 1 m for a raptor, 1.4 m for a gallimimus, 2.2 m for a tyrannosaur
}

int cret_nest_egg_at(const CretSim* s, int i, float hx, float hz) {
    const CretAgent* a = &s->agents[i];
    for (int k = 0; k < CRET_MAX_NESTS; k++) {
        const CretNest* n = &s->nests[k];
        if (!n->active || n->eggs <= 0 || n->owner == i + 1 || (a->group && n->group == a->group)) continue;
        float dx = n->x - hx, dz = n->z - hz; if (dx * dx + dz * dz <= (2.5f + a->radius) * (2.5f + a->radius)) return k;
    }
    return -1;
}

float cret_nest_eat(CretSim* s, int i, float hx, float hz) {
    CretAgent* a = &s->agents[i];
    if (a->act_ticks % 45 != 44) return 0;
    int k = cret_nest_egg_at(s, i, hx, hz); if (k < 0) return 0;
    s->nests[k].eggs--; s->stats.eggs_eaten++;
    float mass = cret_mass(a), d = fminf(CRET_EGG_KG / (0.25f * mass), 1 - a->food);
    return fmaxf(d, 0);
}

static void nests_step(CretSim* s) {
    // Building: hold the nest action for 5 s as a fed adult, away from other nests.
    for (int i = 0; i < s->cfg.num_agents; i++) {
        CretAgent* a = &s->agents[i];
        if (!a->alive || a->act != CRET_ACT_NEST) { a->nest_ticks = 0; continue; }
        int slot = -1; bool ok = a->growth >= 0.9f && a->food >= 0.3f && cret_nest_of(s, i) < 0;
        for (int k = 0; k < CRET_MAX_NESTS && ok; k++) {
            const CretNest* n = &s->nests[k];
            if (!n->active) { if (slot < 0) slot = k; continue; }
            float dx = n->x - a->x, dz = n->z - a->z; if (dx * dx + dz * dz < 60.0f * 60.0f) ok = false;
        }
        if (!ok || slot < 0) { a->act = 0; a->nest_ticks = 0; continue; }
        if (++a->nest_ticks < NEST_BUILD_TICKS) continue;
        float L = CRET_SPECIES_TABLE[a->species].length * cret_growth_scale(a->growth) * 0.45f;
        float nx = a->x + cosf(a->yaw) * L, nz = a->z + sinf(a->yaw) * L;
        s->nests[slot] = (CretNest){.active = true, .species = a->species, .group = a->group, .owner = (uint8_t)(i + 1),
                                    .x = nx, .y = cret_height(s->world, nx, nz), .z = nz, .eggs = 2};
        a->food = fmaxf(a->food - 0.2f, 0); a->act = 0; a->nest_ticks = 0;
        s->stats.nests_built++; s->stats.eggs_laid += 2;
    }
    if (s->tick % 30) return;
    // Every second: eggs while an adult of the nest stays near; abandoned nests fall apart.
    for (int k = 0; k < CRET_MAX_NESTS; k++) {
        CretNest* n = &s->nests[k]; if (!n->active) continue;
        int o = n->owner - 1;
        if (o >= 0 && o < s->cfg.num_agents && s->agents[o].alive && s->agents[o].species == n->species) n->group = s->agents[o].group;
        bool tended = false, alive = false;
        for (int j = 0; j < s->cfg.num_agents; j++) {
            const CretAgent* b = &s->agents[j];
            if (!b->alive || b->species != n->species || !(j == o || (n->group && b->group == n->group))) continue;
            alive = true;
            float dx = b->x - n->x, dz = b->z - n->z;
            if (b->growth >= 0.9f && b->food > 0.3f && dx * dx + dz * dz < 40.0f * 40.0f) tended = true;
        }
        if (tended && n->eggs < NEST_MAX_EGGS && (n->egg_ticks += 30) >= EGG_SECONDS * CRET_TICK_HZ) { n->egg_ticks = 0; n->eggs++; s->stats.eggs_laid++; }
        n->idle_ticks = alive && (n->eggs > 0 || tended) ? 0 : n->idle_ticks + 30;
        if (n->idle_ticks > 10 * 60 * CRET_TICK_HZ) n->active = false;
    }
}

// ---------------------------------------------------------------- step
void cret_progress_step(CretSim* s) {
    if (!s->world->pois_ok) cret_pois_build(s->world);
    for (int i = 0; i < s->cfg.num_agents; i++) {
        CretAgent* a = &s->agents[i]; CretQuest* q = &s->prog[i].quest;
        if (!a->alive) continue;
        // A person back as a hatchling drops its errand (far off, or a spot near where it was) for one close by.
        if (a->ticks_alive == 1 && a->growth < 0.5f && q->kind && cret_is_person(s, i)) cret_quest_reset(s, i);
        if (!q->kind) { if (--q->wait <= 0 && !make_quest(s, i, q, false)) q->wait = 30 * CRET_TICK_HZ; }
        else if (s->tick > q->expires) { memset(q, 0, sizeof *q); q->wait = 20 * CRET_TICK_HZ; }
        else advance(s, i, q);
        if (a->group && s->gquest[a->group].kind && s->gquest[a->group].kind != CRET_Q_HUNT) advance(s, i, &s->gquest[a->group]);
    }
    // Group quests: offered to groups of two or more, dropped when the group breaks up.
    if (s->tick % 30 == 0)
        for (int g = 1; g <= s->cfg.num_agents; g++) {
            CretQuest* q = &s->gquest[g]; int first = -1, n = 0;
            for (int j = 0; j < s->cfg.num_agents; j++) if (s->agents[j].alive && s->agents[j].group == g) { if (first < 0) first = j; n++; }
            if (n < 2) { if (q->kind) { memset(q, 0, sizeof *q); q->wait = 60 * CRET_TICK_HZ; } continue; }
            if (q->kind && s->tick > q->expires) { memset(q, 0, sizeof *q); q->wait = 60 * CRET_TICK_HZ; }
            if (!q->kind && (q->wait -= 30) <= 0 && !make_quest(s, first, q, true)) q->wait = 60 * CRET_TICK_HZ;
        }
    nests_step(s);
}
