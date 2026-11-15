#include "progress.h"
#include "eco.h"
#include "abilities.h"
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
// Quests are picked so the animal that gets one can do it (docs/progression.md):
//   - distances follow the growth stage: hatchlings 70-150 m, juveniles
//     150-300 m, adults 250-500 m, and never further than half of what the
//     animal's stomach and thirst last at a trot (a grazer eats on the way);
//   - hunts name prey of which a living animal weighs at most PREY_RATIO times
//     the hunter (or the group's combined mass); raptors leave grown
//     sauropeltas alone;
//   - only herbivores that can wound a predator get "drive off predators";
//   - a kind with nothing feasible is struck off and another kind is rolled;
//   - the kind just done is rolled a seventh as often, so it rarely repeats;
//   - a quest expires after walking there one and a half times, doing it and
//     two minutes more (at least four minutes).
// A new character first goes through its first steps (chain_quest), one short
// quest at a time and each a little further away; bots skip them.
#define QUEST_SECONDS (15 * 60)        // quests with no place (hunt, defend) and group quests
#define PREY_RATIO 3.0f                // hunt: prey up to this times the hunters' mass
#define COMPANION_R 25.0f              // companion: this close to count

static const float BAND[3][2] = {{70, 150}, {150, 300}, {250, 500}};
int cret_quest_stage(float growth) { return growth < 0.5f ? 0 : growth < 0.9f ? 1 : 2; }
void cret_quest_band(float growth, float* lo, float* hi) { int st = cret_quest_stage(growth); *lo = BAND[st][0]; *hi = BAND[st][1]; }

static bool kind_in(int kind, uint32_t mask) { return (mask >> kind) & 1; }
static float dist_to(const CretAgent* a, float x, float z) { return sqrtf((x - a->x) * (x - a->x) + (z - a->z) * (z - a->z)); }
static bool herb_of(const CretAgent* a) { return CRET_SPECIES_TABLE[a->species].diet == CRET_DIET_HERBIVORE; }

#define M_WATER ((1u << CRET_POI_LAKE) | (1u << CRET_POI_RIVER))
#define M_FORAGE ((1u << CRET_POI_MEADOW) | (1u << CRET_POI_GROVE) | (1u << CRET_POI_MARSH))
#define M_ANY 0xffu

// Who a quest is being made for: one animal, or a group (the leader's
// position; the youngest member's band, the slowest walk, the shortest reach
// and the members' combined mass).
typedef struct {
    int i; const CretAgent* a; bool herb, group, person;
    float lo, hi;          // the band of distances (hi already cut to the reach)
    float band_hi;         // the band's far end, uncut
    float reach;           // how far the reserves carry it (m)
    float walk;            // walking speed (expiry)
    float mass; int members;
} QCtx;

// How far its reserves carry an animal: half of what its stomach and thirst
// last at a trot. A grazer eats on the way, so its food does not count; a
// drinking quest's own water does not either.
static float reach_of(const CretSim* s, const CretAgent* a, bool drink) {
    float food_s = herb_of(a) ? 1e9f : a->food * s->cfg.hunger_seconds;
    float water_s = drink ? 1e9f : a->water * s->cfg.thirst_seconds;
    return 0.5f * fminf(food_s, water_s) * fmaxf(a->trot_speed, 0.5f);
}
static void ctx_make(const CretSim* s, int i, bool group, QCtx* c) {
    const CretAgent* a = &s->agents[i];
    memset(c, 0, sizeof *c); c->i = i; c->a = a; c->herb = herb_of(a); c->group = group; c->person = cret_is_person(s, i);
    float growth = a->growth; c->reach = reach_of(s, a, false); c->walk = a->walk_speed; c->mass = cret_mass(a); c->members = 1;
    if (group) {
        c->mass = 0; c->members = 0;
        for (int j = 0; j < s->cfg.num_agents; j++) {
            const CretAgent* b = &s->agents[j]; if (!b->alive || b->group != a->group) continue;
            growth = fminf(growth, b->growth); c->reach = fminf(c->reach, reach_of(s, b, false)); c->walk = fminf(c->walk, b->walk_speed);
            c->mass += cret_mass(b); c->members++;
        }
    }
    cret_quest_band(growth, &c->lo, &c->band_hi);
    c->hi = fminf(c->band_hi, c->reach);
}
// Walk there one and a half times, do it and two minutes more; at least four minutes.
static int expiry(const CretSim* s, const QCtx* c, float d, float doing_s) {
    float t = fmaxf(1.5f * d / fmaxf(c->walk, 0.3f) + doing_s + 120, 240);
    return s->tick + (int)(t * CRET_TICK_HZ);
}

// A random place of the given kinds lo..hi m from (x, z), not in `avoid` and
// not in the `skip` bits (places already visited, for exploring). -1 if none.
static int pick_poi(CretSim* s, float x, float z, uint32_t mask, float lo, float hi, const uint8_t* avoid, int navoid, uint32_t skip) {
    const CretWorld* w = s->world; int cand[CRET_MAX_POIS], n = 0;
    for (int k = 0; k < w->npois; k++) {
        if (!kind_in(w->pois[k].kind, mask) || (skip >> k & 1)) continue;
        bool dup = false; for (int q = 0; q < navoid; q++) if (avoid[q] == k) dup = true;
        float dx = w->pois[k].x - x, dz = w->pois[k].z - z, d = sqrtf(dx * dx + dz * dz);
        if (!dup && d >= lo && d <= hi) cand[n++] = k;
    }
    return n ? cand[(int)(frand(&s->rng) * n) % n] : -1;
}
// The nearest drinking spot lo..hi m away: a dry, walkable point a few metres
// short of water (rings outward, so the first found is the nearest).
static bool water_spot(CretSim* s, const CretAgent* a, float lo, float hi, float* x, float* z) {
    const CretWorld* w = s->world;
    // Rays at most about 20 m apart on every ring (24 on the near ones), so a
    // pond far off is not missed between two rays.
    for (float r = fmaxf(lo, 15) + 5; r <= hi + 5; r += 10) {
        float off = frand(&s->rng) * 6.2831853f; int nq = (int)fmaxf(24, ceilf(6.2831853f * r / 20.0f));
        for (int q = 0; q < nq; q++) {
            float ang = off + q * 6.2831853f / nq, px = a->x + cosf(ang) * r, pz = a->z + sinf(ang) * r;
            if (cret_water(w, px, pz) < cret_height(w, px, pz) + 0.3f) continue;
            float bx = a->x + cosf(ang) * (r - 5), bz = a->z + sinf(ang) * (r - 5);
            if (cret_walkable(w, bx, bz)) { *x = bx; *z = bz; return true; }
        }
    }
    return false;
}
// The best of 24 walkable points lo..hi m away by small prey (a predator) or
// plants (a grazer), with some noise so quests vary.
static bool food_spot(CretSim* s, const CretAgent* a, bool herb, float lo, float hi, float* x, float* z) {
    float bs = 0; bool found = false;
    if (hi < lo) return false;   // its reserves do not carry it that far
    for (int t = 0; t < 24; t++) {
        float ang = frand(&s->rng) * 6.2831853f, d = lo + (hi - lo) * frand(&s->rng);
        float px = a->x + cosf(ang) * d, pz = a->z + sinf(ang) * d;
        if (!cret_walkable(s->world, px, pz)) continue;
        float sc = herb ? cret_forage(s->world, px, pz)
                        : cret_critter_habitat(s, CRET_CR_MOUSE, px, pz) + cret_critter_habitat(s, CRET_CR_LIZARD, px, pz);
        sc *= 0.8f + 0.4f * frand(&s->rng);
        if (sc > bs) { bs = sc; *x = px; *z = pz; found = true; }
    }
    return found && (!herb || bs > 0.15f);
}

bool cret_quest_prey_ok(const CretSim* s, const CretAgent* hunter, float hunters_mass, const CretAgent* b) {
    if (!b->alive || !herb_of(b) || b->species == hunter->species) return false;
    if (hunter->species == CRET_SP_RAPTOR && b->species == CRET_SP_SAUROPELTA && b->growth > 0.6f) return false;   // armour
    return cret_mass(b) <= PREY_RATIO * hunters_mass;
}
// A prey species the hunters can plausibly kill, weighted by how many such
// animals live; -1 if none.
static int pick_prey(CretSim* s, const QCtx* c) {
    int n = 0, count[CRET_SPECIES] = {0};
    for (int j = 0; j < s->cfg.num_agents; j++)
        if (j != c->i && cret_quest_prey_ok(s, c->a, c->mass, &s->agents[j])) { count[s->agents[j].species]++; n++; }
    if (!n) return -1;
    int r = (int)(frand(&s->rng) * n) % n;
    for (int sp = 0; sp < CRET_SPECIES; sp++) { if (r < count[sp]) return sp; r -= count[sp]; }
    return -1;
}
bool cret_quest_can_defend(const CretAgent* a) { return herb_of(a) && a->species != CRET_SP_GALLI && a->growth >= 0.6f; }

// The animal to stay near for a companion quest: the nearest member of its
// group, else the nearest of its species. -1 if none lives.
static int companion_of(const CretSim* s, int i) {
    const CretAgent* a = &s->agents[i]; int best = -1, bestg = -1; float bd = 1e30f, bdg = 1e30f;
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* b = &s->agents[j]; if (j == i || !b->alive || b->species != a->species) continue;
        float d = (b->x - a->x) * (b->x - a->x) + (b->z - a->z) * (b->z - a->z);
        if (d < bd) { bd = d; best = j; }
        if (a->group && b->group == a->group && d < bdg) { bdg = d; bestg = j; }
    }
    return bestg >= 0 ? bestg : best;
}
static int carcass_for(const CretSim* s, const QCtx* c, float kg) {
    int best = -1; float bd = c->hi;
    for (int k = 0; k < CRET_MAX_CARCASSES; k++) {
        const CretCarcass* k_ = &s->carcasses[k]; if (!k_->active || k_->meat < 2 * kg) continue;
        float d = dist_to(c->a, k_->x, k_->z); if (d <= bd) { bd = d; best = k; }
    }
    return best;
}
static float scavenge_kg(const CretAgent* a) { return fminf(fmaxf(0.25f * cret_mass(a) * 0.4f, 0.5f), 40.0f); }   // 40% of a full stomach

const char* cret_quest_kind_name(int kind) {
    static const char* N[CRET_Q_COUNT] = {"-", "drink", "graze", "hunt", "patrol", "defend", "catch", "companion", "ability", "explore",
                                          "nest", "rest", "scavenge", "raid", "guard"};
    return kind > 0 && kind < CRET_Q_COUNT ? N[kind] : "-";
}

static void set_place(CretQuest* q, int p) { q->poi[0] = (uint8_t)p; q->steps = 1; }
static void set_spot(CretQuest* q, float x, float z, float r) { q->spot = 1; q->spot_x = x; q->spot_z = z; q->spot_r = r; }
static void pay(CretQuest* q, float xp, int marks) { q->xp = xp; q->marks = marks; }

// Fills q with a quest of `kind` for c, if one is feasible now.
static bool try_kind(CretSim* s, const QCtx* c, int kind, CretQuest* q) {
    const CretAgent* a = c->a; const CretWorld* w = s->world; float x, z, d = 0;
    memset(q, 0, sizeof *q); q->steps = 1; q->group = c->group ? a->group : 0;
    switch (kind) {
    case CRET_Q_DRINK: {   // a lake or ford in the band, else the nearest water in it; a thirsty animal goes to the nearest water
        float lo = a->water < 0.45f && !c->group ? 0 : c->lo, hi = c->group ? c->hi : fminf(c->band_hi, reach_of(s, a, true));
        int p = pick_poi(s, a->x, a->z, M_WATER, lo, hi, NULL, 0, 0);
        if (p >= 0) { set_place(q, p); d = dist_to(a, w->pois[p].x, w->pois[p].z); }
        else if (water_spot(s, a, lo, hi, &x, &z)) { set_spot(q, x, z, 25); d = dist_to(a, x, z); }
        else return false;
        q->goal = 8; pay(q, 0.05f, 20); q->expires = expiry(s, c, d, 30); break; }
    case CRET_Q_GATHER: {
        if (!c->herb) return false;
        float lo = a->food < 0.4f && !c->group ? 0 : c->lo;
        int p = pick_poi(s, a->x, a->z, M_FORAGE, lo, c->hi, NULL, 0, 0);
        if (p >= 0) { set_place(q, p); d = dist_to(a, w->pois[p].x, w->pois[p].z); }
        else if (food_spot(s, a, true, fmaxf(lo, 30), c->hi, &x, &z)) { set_spot(q, x, z, 40); d = dist_to(a, x, z); }
        else return false;
        q->goal = c->group ? 40 : 20; pay(q, 0.06f, 25); q->expires = expiry(s, c, d, q->goal * 2); break; }
    case CRET_Q_PATROL: {   // three places, each leg within the band
        float px = a->x, pz = a->z;
        for (int k = 0; k < 3; k++) {
            int p = pick_poi(s, px, pz, M_ANY, k ? c->lo * 0.7f : c->lo, k ? c->band_hi : c->hi, q->poi, k, 0);
            if (p < 0) return false;
            q->poi[k] = (uint8_t)p; d += sqrtf((w->pois[p].x - px) * (w->pois[p].x - px) + (w->pois[p].z - pz) * (w->pois[p].z - pz));
            px = w->pois[p].x; pz = w->pois[p].z;
        }
        q->steps = 3; q->goal = 3; pay(q, 0.08f, 40); q->expires = expiry(s, c, d, 0); break; }
    case CRET_Q_HUNT: {
        if (c->herb || (a->species == CRET_SP_RAPTOR && a->growth < 0.5f)) return false;   // a hatchling raptor catches small prey, it does not hunt
        int sp = pick_prey(s, c); if (sp < 0) return false;
        q->species = (uint8_t)sp; q->goal = c->group ? 2 : 1; pay(q, 0.10f, 50); q->expires = s->tick + QUEST_SECONDS * CRET_TICK_HZ; break; }
    case CRET_Q_DEFEND: {
        if (!cret_quest_can_defend(a) || c->group) return false;
        bool predator = false;
        for (int j = 0; j < s->cfg.num_agents; j++) predator |= s->agents[j].alive && !herb_of(&s->agents[j]);
        if (!predator) return false;
        q->goal = 0.25f; pay(q, 0.10f, 50); q->expires = s->tick + QUEST_SECONDS * CRET_TICK_HZ; break; }
    case CRET_Q_CATCH: {   // small prey live only around people
        if (c->herb || c->group || !c->person || !s->crit_cfg.enabled || !food_spot(s, a, false, c->lo, c->hi, &x, &z)) return false;
        set_spot(q, x, z, 40); q->goal = 3; pay(q, 0.05f, 20); q->expires = expiry(s, c, dist_to(a, x, z), 180); break; }
    case CRET_Q_REST: {
        int p = pick_poi(s, a->x, a->z, M_ANY, c->lo, c->hi, NULL, 0, 0); if (p < 0) return false;
        set_place(q, p); q->goal = c->group ? 40 : 20; pay(q, 0.04f, 20); q->expires = expiry(s, c, dist_to(a, w->pois[p].x, w->pois[p].z), q->goal * 2); break; }
    case CRET_Q_EXPLORE: {
        if (c->group) return false;
        int p = pick_poi(s, a->x, a->z, M_ANY, c->lo, c->hi, NULL, 0, s->prog[c->i].visited); if (p < 0) return false;
        set_place(q, p); q->goal = 1; pay(q, 0.06f, 30); q->expires = expiry(s, c, dist_to(a, w->pois[p].x, w->pois[p].z), 0); break; }
    case CRET_Q_SCAVENGE: {   // only with room in the stomach for what it asks
        if (c->herb || c->group || a->food > 0.6f) return false;
        float kg = scavenge_kg(a); int k = carcass_for(s, c, kg); if (k < 0) return false;
        set_spot(q, s->carcasses[k].x, s->carcasses[k].z, 8); q->goal = kg; pay(q, 0.05f, 25);
        q->expires = expiry(s, c, dist_to(a, q->spot_x, q->spot_z), 60); break; }
    case CRET_Q_RAID: {
        if (c->herb || c->group || a->growth < 0.5f) return false;
        int k = cret_nest_near(s, a->x, a->z, c->hi, a->group); if (k < 0 || s->nests[k].owner == c->i + 1) return false;
        set_spot(q, s->nests[k].x, s->nests[k].z, 10); q->species = s->nests[k].species; q->goal = 1; pay(q, 0.08f, 40);
        q->expires = expiry(s, c, dist_to(a, q->spot_x, q->spot_z), 60); break; }
    case CRET_Q_GUARD: {
        int k = cret_nest_of(s, c->i); if (c->group || k < 0 || s->nests[k].eggs <= 0 || dist_to(a, s->nests[k].x, s->nests[k].z) > c->reach) return false;
        set_spot(q, s->nests[k].x, s->nests[k].z, 40); q->goal = 60; pay(q, 0.06f, 30);
        q->expires = expiry(s, c, dist_to(a, q->spot_x, q->spot_z), 120); break; }
    case CRET_Q_COMPANION: {
        int j = c->group ? -1 : companion_of(s, c->i); if (j < 0 || dist_to(a, s->agents[j].x, s->agents[j].z) > c->hi) return false;
        q->species = a->species; q->goal = 30; pay(q, 0.04f, 20); q->expires = expiry(s, c, dist_to(a, s->agents[j].x, s->agents[j].z), 60); break; }
    case CRET_Q_NEST: {
        if (c->group || a->growth < 0.9f || cret_nest_of(s, c->i) >= 0) return false;
        q->goal = 1; pay(q, 0.10f, 60); q->expires = s->tick + QUEST_SECONDS * CRET_TICK_HZ; break; }
    default: return false;
    }
    q->kind = (uint8_t)kind;
    if (c->group) { q->xp *= 1.5f; q->marks = q->marks * 3 / 2; }
    return true;
}

// First steps: a new character's first quests teach the game in order, each
// a little further away. 0 eat (graze, or catch small prey: else scavenge or
// hunt), 1 drink, 2 stay near your group or your own kind, 3 use ability 1
// near another animal or small prey, 4 find a place not visited yet, 5 build
// a nest (adults; younger animals do ordinary quests until they are grown).
// A step that cannot be offered now waits for the next offer.
static bool chain_quest(CretSim* s, const QCtx* c, int step, CretQuest* q) {
    const CretAgent* a = c->a; const CretWorld* w = s->world; float x, z;
    memset(q, 0, sizeof *q); q->steps = 1;
    switch (step) {
    case 0:
        if (c->herb) { if (!food_spot(s, a, true, 40, 100, &x, &z)) return false; q->kind = CRET_Q_GATHER; set_spot(q, x, z, 40); q->goal = 12; }
        else if (s->crit_cfg.enabled && food_spot(s, a, false, 40, 100, &x, &z)) { q->kind = CRET_Q_CATCH; set_spot(q, x, z, 40); q->goal = 2; }
        else if (!try_kind(s, c, CRET_Q_SCAVENGE, q) && !try_kind(s, c, CRET_Q_HUNT, q)) return false;
        pay(q, 0.04f, 15); break;
    case 1: {   // the nearest water beyond 50 m, as far as 200 m (or 400 m when there is none nearer)
        float hi = fminf(200, reach_of(s, a, true));
        int p = pick_poi(s, a->x, a->z, M_WATER, 50, hi, NULL, 0, 0);
        if (p >= 0) set_place(q, p);
        else if (water_spot(s, a, 50, hi, &x, &z) || water_spot(s, a, 0, fminf(400, reach_of(s, a, true)), &x, &z)) set_spot(q, x, z, 25);
        else return false;
        q->kind = CRET_Q_DRINK; q->goal = 6; pay(q, 0.04f, 15); break; }
    case 2: {   // one of its kind within reach (at most 600 m)
        int j = companion_of(s, c->i); if (j < 0 || dist_to(a, s->agents[j].x, s->agents[j].z) > fminf(600, c->reach)) return false; }
        q->kind = CRET_Q_COMPANION; q->species = a->species; q->goal = 20; pay(q, 0.05f, 20); break;
    case 3:
        if (!s->prog[c->i].loadout[0]) return false;
        q->kind = CRET_Q_ABILITY; q->species = a->species; q->goal = 1; pay(q, 0.05f, 20); break;
    case 4: {   // a place not visited yet, 100-400 m off (else up to 700 m once past hatchling; any place when all are known)
        float hi = fminf(400, c->reach), far = fminf(a->growth < 0.5f ? 400 : 700, c->reach);
        int p = pick_poi(s, a->x, a->z, M_ANY, 100, hi, NULL, 0, s->prog[c->i].visited);
        if (p < 0) p = pick_poi(s, a->x, a->z, M_ANY, 100, far, NULL, 0, s->prog[c->i].visited);
        if (p < 0) p = pick_poi(s, a->x, a->z, M_ANY, 100, far, NULL, 0, 0);
        if (p < 0) return false;
        set_place(q, p); q->kind = CRET_Q_EXPLORE; q->goal = 1; pay(q, 0.06f, 30); break; }
    case 5:
        if (!try_kind(s, c, CRET_Q_NEST, q)) return false;
        pay(q, 0.10f, 60); break;
    default: return false;
    }
    float tx = a->x, tz = a->z;
    if (q->spot) { tx = q->spot_x; tz = q->spot_z; } else if (q->kind == CRET_Q_DRINK || q->kind == CRET_Q_EXPLORE) { tx = w->pois[q->poi[0]].x; tz = w->pois[q->poi[0]].z; }
    q->expires = q->kind == CRET_Q_NEST ? s->tick + QUEST_SECONDS * CRET_TICK_HZ : expiry(s, c, dist_to(a, tx, tz), 180);
    q->chain = (uint8_t)(step + 1);
    return true;
}

// Roll weights: what fits the diet and stage, with survival first (a thirsty
// animal is sent to drink, a hungry one to eat) and the last kind cut to a
// seventh so it rarely comes twice in a row.
static void weights(const QCtx* c, int last, float* wt) {
    const CretAgent* a = c->a; int st = cret_quest_stage(a->growth);
    memset(wt, 0, sizeof(float) * CRET_Q_COUNT);
    if (c->group) {
        if (c->herb) { wt[CRET_Q_GATHER] = 1; wt[CRET_Q_PATROL] = 1; } else { wt[CRET_Q_HUNT] = 1.5f; wt[CRET_Q_PATROL] = 1; }
        wt[CRET_Q_REST] = 0.5f;
    } else {
        wt[CRET_Q_DRINK] = a->water < 0.7f ? 2 : 0.8f; wt[CRET_Q_PATROL] = st ? 1.2f : 0; wt[CRET_Q_REST] = 1; wt[CRET_Q_EXPLORE] = 1.5f;
        wt[CRET_Q_GUARD] = 1.5f; wt[CRET_Q_NEST] = 0.5f;
        wt[CRET_Q_COMPANION] = st < 2 && CRET_SPECIES_TABLE[a->species].group_max ? 1 : 0;   // the young of herds and packs keep close to their own
        if (c->herb) { wt[CRET_Q_GATHER] = 2.5f; wt[CRET_Q_DEFEND] = 1; }
        else {
            wt[CRET_Q_HUNT] = 3; wt[CRET_Q_SCAVENGE] = 1.2f; wt[CRET_Q_RAID] = 1;
            wt[CRET_Q_CATCH] = st == 0 ? 3 : cret_mass(a) < 200 ? 1 : 0;
        }
        if (a->water < 0.45f) wt[CRET_Q_DRINK] *= 8;
        if (a->food < 0.4f) { wt[CRET_Q_GATHER] *= 8; wt[CRET_Q_CATCH] *= 6; wt[CRET_Q_SCAVENGE] *= 6; wt[CRET_Q_HUNT] *= 2; }
    }
    if (last > 0 && last < CRET_Q_COUNT) wt[last] /= 7;
}

static bool make_quest(CretSim* s, int i, CretQuest* out, bool group) {
    const CretAgent* a = &s->agents[i]; CretProgress* p = &s->prog[i]; CretQuest q; QCtx c;
    ctx_make(s, i, group, &c);
    if (!group && c.person && p->chain < CRET_CHAIN_STEPS && chain_quest(s, &c, p->chain, &q)) { q.last = out->last; *out = q; return true; }
    if (!group && a->group && !c.person)   // a herd or pack bot takes on its leader's errand, so the group travels as one
        for (int j = 0; j < s->cfg.num_agents; j++) {
            if (!s->agents[j].alive || s->agents[j].group != a->group) continue;
            const CretQuest* lq = &s->prog[j].quest;
            if (j != i && !lq->chain && kind_in(lq->kind, (1u << CRET_Q_DRINK) | (1u << CRET_Q_GATHER) | (1u << CRET_Q_PATROL) | (1u << CRET_Q_REST))) {
                q = *lq; q.step = 0; q.progress = 0; q.last = out->last; q.expires = s->tick + QUEST_SECONDS * CRET_TICK_HZ; *out = q; return true;
            }
            break;
        }
    float wt[CRET_Q_COUNT]; weights(&c, out->last, wt);
    for (;;) {   // roll a kind; strike off the ones with nothing feasible and roll again
        float total = 0; for (int k = 1; k < CRET_Q_COUNT; k++) total += wt[k];
        if (total <= 0) return false;
        float r = frand(&s->rng) * total; int kind = CRET_Q_COUNT - 1;
        for (int k = 1; k < CRET_Q_COUNT; k++) { if (r < wt[k]) { kind = k; break; } r -= wt[k]; }
        if (wt[kind] <= 0) for (kind = CRET_Q_COUNT - 1; wt[kind] <= 0; kind--) {}
        if (try_kind(s, &c, kind, &q)) { q.last = out->last; *out = q; return true; }
        wt[kind] = 0;
    }
}

void cret_quest_reset(CretSim* s, int i) {
    CretQuest* q = &s->prog[i].quest; uint8_t last = q->kind ? q->kind : q->last;   // the next quest still avoids the last kind
    memset(q, 0, sizeof *q); q->last = last; q->wait = 5 * CRET_TICK_HZ;
}

const CretQuest* cret_quest_of(const CretSim* s, int i, int which) {
    if (which == 0) return s->prog[i].quest.kind ? &s->prog[i].quest : NULL;
    int g = s->agents[i].group; return g && s->gquest[g].kind ? &s->gquest[g] : NULL;
}

// The quest's own agent: the slot holding the personal quest q (-1 for a group quest).
static int owner_of(const CretSim* s, const CretQuest* q) {
    return q >= &s->prog[0].quest && (const char*)q < (const char*)&s->prog[CRET_MAX_AGENTS] ? (int)(((const char*)q - (const char*)s->prog) / sizeof s->prog[0]) : -1;
}
// Radius of a named place for a quest: drinking anywhere on a lake's shore
// counts, the other kinds want the middle of the place.
static float place_r(const CretQuest* q, const CretPoi* P) { return q->kind == CRET_Q_DRINK || q->kind == CRET_Q_GATHER ? P->r : fmaxf(30, P->r * 0.4f); }

bool cret_quest_target(const CretSim* s, const CretQuest* q, float* x, float* z, float* r) {
    if (!q || !q->kind) return false;
    if (q->spot) { *x = q->spot_x; *z = q->spot_z; if (r) *r = q->spot_r; return true; }
    if (q->kind == CRET_Q_COMPANION) {   // the animal to stay near, wherever it is now
        int i = owner_of(s, q), j = i >= 0 ? companion_of(s, i) : -1; if (j < 0) return false;
        *x = s->agents[j].x; *z = s->agents[j].z; if (r) *r = COMPANION_R; return true;
    }
    if (!kind_in(q->kind, (1u << CRET_Q_DRINK) | (1u << CRET_Q_GATHER) | (1u << CRET_Q_PATROL) | (1u << CRET_Q_REST) | (1u << CRET_Q_EXPLORE)) || !s->world->npois) return false;
    int p = q->poi[q->step < q->steps ? q->step : 0]; if (p >= s->world->npois) return false;
    const CretPoi* P = &s->world->pois[p];
    *x = P->x; *z = P->z; if (r) *r = place_r(q, P);
    return true;
}

void cret_quest_text(const CretSim* s, const CretQuest* q, char* title, int nt, char* prog, int np) {
    const CretWorld* w = s->world; int pi = q->poi[q->step < q->steps ? q->step : 0];
    const char* place = pi < w->npois ? w->pois[pi].name : "?";
    const char* sp = CRET_SPECIES_TABLE[q->species % CRET_SPECIES].name;
    int g = (int)q->goal, n = (int)q->progress;
    prog[0] = 0;
    switch (q->kind) {
    case CRET_Q_DRINK:
        if (q->spot) snprintf(title, nt, "Drink at the marked water"); else snprintf(title, nt, "Drink at %s", place);
        snprintf(prog, np, "drink %d / %d s", n, g); break;
    case CRET_Q_GATHER:
        if (q->spot) snprintf(title, nt, "Graze at the marked plants"); else snprintf(title, nt, "Graze at %s", place);
        snprintf(prog, np, "graze %d / %d s", n, g); break;
    case CRET_Q_PATROL: snprintf(title, nt, "Patrol to %s", place); snprintf(prog, np, "place %d of %d", q->step + 1, q->steps); break;
    case CRET_Q_HUNT: if (g > 1) snprintf(title, nt, "Hunt %d %s", g, sp); else snprintf(title, nt, "Hunt a %s", sp); snprintf(prog, np, "%d / %d killed", n, g); break;
    case CRET_Q_CATCH: snprintf(title, nt, "Catch %d small animals", g); snprintf(prog, np, "%d / %d caught", n, g); break;
    case CRET_Q_DEFEND: snprintf(title, nt, "Drive off a predator"); snprintf(prog, np, "wound it: %d%%", (int)(100 * q->progress / fmaxf(q->goal, 1e-3f))); break;
    case CRET_Q_COMPANION: snprintf(title, nt, "Stay near another %s", sp); snprintf(prog, np, "together %d / %d s", n, g); break;
    case CRET_Q_ABILITY: {
        int ab = CRET_SPECIES_ABILITIES[q->species % CRET_SPECIES].ab[0];
        snprintf(title, nt, "Use %s (key 1) near an animal", CRET_ABILITIES[ab].name); snprintf(prog, np, "%d / %d", n, g); break; }
    case CRET_Q_EXPLORE: snprintf(title, nt, "Find %s", place); snprintf(prog, np, "a place you have not seen"); break;
    case CRET_Q_NEST: snprintf(title, nt, "Build a nest"); snprintf(prog, np, "hold N for 5 s, away from other nests"); break;
    case CRET_Q_REST: snprintf(title, nt, "Rest at %s", place); snprintf(prog, np, "stand still %d / %d s", n, g); break;
    case CRET_Q_SCAVENGE: snprintf(title, nt, "Eat %.0f kg from a carcass", fmaxf(roundf(q->goal), 1)); snprintf(prog, np, "%.1f / %.0f kg", q->progress, fmaxf(roundf(q->goal), 1)); break;
    case CRET_Q_RAID: snprintf(title, nt, "Raid the %s nest", sp); snprintf(prog, np, "eat an egg"); break;
    case CRET_Q_GUARD: snprintf(title, nt, "Guard your nest"); snprintf(prog, np, "stay close %d / %d s", n, g); break;
    default: snprintf(title, nt, "-");
    }
}

// Growth for the young (adults get half as many marks again instead).
static void reward(CretSim* s, int i, const CretQuest* q) {
    CretAgent* a = &s->agents[i]; int marks = q->marks;
    if (a->growth < 1) { a->growth = fminf(a->growth + q->xp, 1.0f); cret_apply_growth(a); } else marks = marks * 3 / 2;
    s->prog[i].marks += marks; s->prog[i].quests_done++; a->ev_quest++; s->stats.marks_earned += marks;
}

// Ends a quest: the next is offered after `wait` seconds and will be of another kind if it can.
static void finish(CretQuest* q, int wait_s) { uint8_t kind = q->kind; memset(q, 0, sizeof *q); q->last = kind; q->wait = wait_s * CRET_TICK_HZ; }

static void complete(CretSim* s, int i, CretQuest* q) {
    s->stats.quests[q->kind]++;
    if (q->group) {
        s->stats.group_quests++;
        for (int j = 0; j < s->cfg.num_agents; j++) if (s->agents[j].alive && s->agents[j].group == q->group) reward(s, j, q);
        finish(q, 60);
    } else {
        reward(s, i, q);
        if (q->chain) { CretProgress* p = &s->prog[i]; if (q->chain > p->chain) p->chain = q->chain; s->stats.chain_steps++; }
        finish(q, q->chain ? 4 : 10);
    }
}

static bool near(const CretAgent* a, float x, float z, float r) { return (x - a->x) * (x - a->x) + (z - a->z) * (z - a->z) < r * r; }

// Time-and-place quests advance while the agent does the thing there.
static void advance(CretSim* s, int i, CretQuest* q) {
    const CretAgent* a = &s->agents[i]; float dt = CRET_DT;
    if (!q->kind) return;
    const CretPoi* P = q->poi[q->step] < s->world->npois ? &s->world->pois[q->poi[q->step]] : NULL;
    // In the place: the named one, or the spot.
    bool here = q->spot ? near(a, q->spot_x, q->spot_z, q->spot_r) : P && near(a, P->x, P->z, place_r(q, P));
    switch (q->kind) {
    case CRET_Q_DRINK: if (here && a->act == CRET_ACT_DRINK && cret_water_ahead(s, a)) q->progress += dt; break;
    case CRET_Q_GATHER:
        if (here && a->act == CRET_ACT_EAT && herb_of(a)) {
            float L = CRET_SPECIES_TABLE[a->species].length * cret_growth_scale(a->growth) * 0.45f;
            if (cret_graze_at(s, a->x + cosf(a->yaw) * L, a->z + sinf(a->yaw) * L) > 0.1f) q->progress += dt;
        }
        break;
    case CRET_Q_PATROL: if (here) { q->step++; q->progress = q->step; } break;
    case CRET_Q_EXPLORE: if (here) q->progress = 1; break;
    case CRET_Q_REST: if (here && (a->act == CRET_ACT_REST || (!a->act && a->speed < 0.3f))) q->progress += dt; break;
    case CRET_Q_COMPANION: {
        int j = companion_of(s, i); if (j >= 0 && near(a, s->agents[j].x, s->agents[j].z, COMPANION_R)) q->progress += dt;
        break; }
    case CRET_Q_GUARD: {
        int k = cret_nest_of(s, i);
        if (k < 0 || s->nests[k].eggs <= 0) { finish(q, 10); return; }   // the nest is gone or empty: nothing to guard
        if (near(a, s->nests[k].x, s->nests[k].z, 40)) q->progress += dt;
        break; }
    default: break;
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

void cret_quest_on_ability(CretSim* s, int i, int slot) {
    CretQuest* q = &s->prog[i].quest; const CretAgent* a = &s->agents[i];
    if (q->kind != CRET_Q_ABILITY || slot != 0) return;
    bool something = false;   // another animal or small prey within 25 m
    for (int j = 0; j < s->cfg.num_agents && !something; j++) something = j != i && s->agents[j].alive && near(a, s->agents[j].x, s->agents[j].z, 25);
    for (int k = 0; k < CRET_MAX_CRITTERS && !something; k++) something = s->critters[k].active && near(a, s->critters[k].x, s->critters[k].z, 25);
    if (something && (q->progress += 1) >= q->goal) complete(s, i, q);
}

void cret_quest_on_scavenge(CretSim* s, int i, float kg) {
    CretQuest* q = &s->prog[i].quest;
    if (q->kind == CRET_Q_SCAVENGE && (q->progress += kg) >= q->goal) complete(s, i, q);
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
        if (group < 0 && cret_predator_near(s, n->x, n->z, 90.0f, species)) continue;   // a bot does not hatch under a predator's nose
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

void cret_nest_raided(CretSim* s, int i, int k) {
    s->nests[k].eggs--; s->stats.eggs_eaten++;
    CretQuest* q = &s->prog[i].quest;
    if (q->kind == CRET_Q_RAID && (q->progress += 1) >= q->goal) complete(s, i, q);
}

float cret_nest_eat(CretSim* s, int i, float hx, float hz) {
    CretAgent* a = &s->agents[i];
    if (a->act_ticks % 45 != 44) return 0;
    int k = cret_nest_egg_at(s, i, hx, hz); if (k < 0) return 0;
    cret_nest_raided(s, i, k);
    float d = fminf(CRET_EGG_KG / cret_eco_gut_kg(a), 1 - a->food);
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
        if (s->prog[i].quest.kind == CRET_Q_NEST) complete(s, i, &s->prog[i].quest);
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
        else if (s->tick > q->expires) finish(q, 20);
        else advance(s, i, q);
        // Named places seen: within a place's radius (at most 60 m from its middle), once a second.
        if ((s->tick + i) % 30 == 0)
            for (int k = 0; k < s->world->npois; k++) {
                const CretPoi* P = &s->world->pois[k];
                if (near(a, P->x, P->z, fminf(P->r, 60))) s->prog[i].visited |= 1u << k;
            }
        if (a->group && s->gquest[a->group].kind && s->gquest[a->group].kind != CRET_Q_HUNT) advance(s, i, &s->gquest[a->group]);
    }
    // Group quests: offered to groups of two or more, dropped when the group breaks up.
    if (s->tick % 30 == 0)
        for (int g = 1; g <= s->cfg.num_agents; g++) {
            CretQuest* q = &s->gquest[g]; int first = -1, n = 0;
            for (int j = 0; j < s->cfg.num_agents; j++) if (s->agents[j].alive && s->agents[j].group == g) { if (first < 0) first = j; n++; }
            if (n < 2) { if (q->kind) finish(q, 60); continue; }
            if (q->kind && s->tick > q->expires) finish(q, 60);
            if (!q->kind && (q->wait -= 30) <= 0 && !make_quest(s, first, q, true)) q->wait = 60 * CRET_TICK_HZ;
        }
    nests_step(s);
}
