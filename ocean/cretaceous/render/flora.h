// Flora: the species table, what each tolerates and offers to eat, and how a
// plant of a kind picks its species where it stands. Header-only and GL-free
// (included by veg_place.h), so the renderer, the trunk colliders and the
// food query (world.c, cret_food_kinds_at) all agree on every plant.
//
// The world bake writes eight vegetation layers (TERRA_VEG_*). Each layer is
// a kind here (VEG_CONIFER ...); a kind's eight variant slots belong to its
// species (FLORA_SLOT below), one or two slots each (two seeds of the same
// species). Where a plant of a kind is scattered, its species is chosen by
// habitat (the site's elevation, slope, moisture, nearness to water and
// coast, landform, canopy shade) times a stand noise per species, so species
// grow in patches of 50-200 m, then a slot by the plant's own dice.
// docs/flora.md lists the table with the evidence for each species.
#ifndef CRET_FLORA_H
#define CRET_FLORA_H
#include <math.h>
#include <stdint.h>
#include "../terra/terra.h"

// Food a plant offers (amounts per plant at scale 1, in kg of edible matter;
// cret_food_kinds_at turns them into relative abundance per place).
enum { FOOD_LOW,     // low browse: fronds, leaves and shoots under 1.5 m
       FOOD_HIGH,    // high browse: foliage 1.5-6 m up
       FOOD_FRUIT,   // fruit, seeds and cones, on the plant or fallen
       FOOD_WATER,   // water plants
       FOOD_KINDS };

// What falls under a fruiting plant: one VEG_DROP variant each (render/veg.c
// draws them near the camera).
enum { DROP_NONE = -1, DROP_ARAU_CONE, DROP_PODO_SEED, DROP_MAGNOLIA, DROP_LAUREL, DROP_GINKGO, DROP_CYCAD_SEED, DROP_PALM_FRUIT, DROP_BERRY, DROP_KINDS };

enum {
    // conifers
    FL_ARAUCARIA, FL_PODOCARP, FL_UMBRELLA, FL_REDWOOD, FL_CHEIROLEP,
    // broadleaves
    FL_MAGNOLIA, FL_LAUREL, FL_KATSURA, FL_PLANE, FL_GINKGO, FL_UNDERSTOREY,
    // cycads and bennettitaleans
    FL_CYCAS, FL_ZAMIA, FL_WILLIAMSONIA,
    // ground ferns
    FL_BRACKEN, FL_SHIELD, FL_GLEICHENIA, FL_ROYAL,
    // tree ferns, horsetails, palms
    FL_CYATHEA, FL_DICKSONIA, FL_HORSETAIL_GIANT, FL_HORSETAIL_FIELD, FL_SABAL, FL_FEATHER_PALM,
    // shrubs and herbs
    FL_BERRY_SHRUB, FL_HERB, FL_GUNNERA, FL_SCRUB,
    // not on a bake layer: water lilies on still water, clubmoss and moss in
    // the sward, fungi on dead wood
    FL_LILY, FL_CLUBMOSS, FL_FUNGI,
    FL_COUNT
};

// A tolerance: full inside [lo, hi], falling to nothing over `soft` beyond.
typedef struct { float lo, hi, soft; } FloraRange;
#define FL_ANY {-1e9f, 1e9f, 1.0f}

typedef struct FloraSpecies {
    const char* name;        // what the game and the docs call it
    const char* model;       // the fossil or living plant it is modelled on
    int kind;                // VEG_* kind (bake layer) it grows from; -1 none
    float weight;            // how common it is where its habitat suits
    float stand;             // 0 mixes evenly with its kind .. 1 grows in pure stands with gaps
    // Habitat: elevation (m), slope (rise over run), moisture (0..1),
    // nearness to river water (0..1), to the coast (0..1), landform (-1
    // ridge .. 1 hollow) and canopy shade over it (0 open .. 1 closed).
    FloraRange elev, slope, moist, wet, coast, form, shade;
    float food[FOOD_KINDS];  // kg per plant at scale 1
    int drop; float drop_n;  // what falls under it and how many lie there per plant
} FloraSpecies;

static const FloraSpecies FLORA[FL_COUNT] = {
    // name, model, kind, weight, stand, elev, slope, moist, wet, coast, form, shade, food {low, high, fruit, water}, drop, n
    [FL_ARAUCARIA] = {"araucaria", "Araucaria sect. Eutacta (Norfolk-like whorled tiers, big seed cones)", 0, 1.0f, 0.6f,
                      {60, 520, 60}, {0.0f, 0.9f, 0.2f}, {0.25f, 0.75f, 0.15f}, {0, 0.5f, 0.3f}, {0, 0.5f, 0.3f}, FL_ANY, FL_ANY, {0, 2, 3, 0}, DROP_ARAU_CONE, 3},
    [FL_PODOCARP]  = {"podocarp", "Podocarpus / Prumnopitys (fleshy seed cups)", 0, 1.0f, 0.5f,
                      {-20, 300, 80}, FL_ANY, {0.4f, 1.0f, 0.15f}, FL_ANY, {0, 0.6f, 0.3f}, FL_ANY, FL_ANY, {0.5f, 4, 2, 0}, DROP_PODO_SEED, 8},
    [FL_UMBRELLA]  = {"umbrella araucaria", "Araucaria sect. Araucaria (monkey-puzzle-like dome)", 0, 0.7f, 0.7f,
                      {220, 1e9f, 80}, {0.05f, 1.0f, 0.1f}, {0.0f, 0.55f, 0.15f}, {0, 0.3f, 0.2f}, {0, 0.3f, 0.2f}, {-1, 0.2f, 0.4f}, FL_ANY, {0, 1, 3, 0}, DROP_ARAU_CONE, 4},
    [FL_REDWOOD]   = {"dawn redwood", "Metasequoia / Sequoia-like redwoods of floodplains", 0, 1.0f, 0.8f,
                      {-20, 220, 60}, {0.0f, 0.25f, 0.15f}, {0.5f, 1.0f, 0.15f}, {0.25f, 1.0f, 0.25f}, {0, 0.4f, 0.3f}, {-0.2f, 1, 0.4f}, FL_ANY, {0, 2, 0.5f, 0}, DROP_NONE, 0},
    [FL_CHEIROLEP] = {"cheirolep cypress", "Frenelopsis / Pseudofrenelopsis (Cheirolepidiaceae), small cypress-like trees of dry and salty ground", 0, 0.8f, 0.6f,
                      FL_ANY, FL_ANY, {0.0f, 0.42f, 0.15f}, {0, 0.2f, 0.2f}, FL_ANY, {-1, 0.3f, 0.4f}, FL_ANY, {1, 2, 0.5f, 0}, DROP_NONE, 0},
    [FL_MAGNOLIA]  = {"magnolia", "Magnoliaceae / Magnoliidae (Archaeanthus: large flowers, cone-like fruit)", 1, 1.0f, 0.5f,
                      {-20, 250, 80}, FL_ANY, {0.45f, 1.0f, 0.15f}, FL_ANY, {0, 0.5f, 0.3f}, FL_ANY, FL_ANY, {0.5f, 6, 2, 0}, DROP_MAGNOLIA, 5},
    [FL_LAUREL]    = {"laurel", "Lauraceae (Mauldinia, small dark drupes), evergreen", 1, 1.2f, 0.4f,
                      {-20, 400, 80}, FL_ANY, {0.3f, 1.0f, 0.15f}, FL_ANY, {0, 0.7f, 0.3f}, FL_ANY, FL_ANY, {0.5f, 5, 2, 0}, DROP_LAUREL, 10},
    [FL_KATSURA]   = {"katsura", "Trochodendroides / Nyssidium (Cercidiphyllum-like), many-stemmed, of wet valley floors", 1, 0.8f, 0.6f,
                      {-20, 300, 60}, {0, 0.3f, 0.15f}, {0.55f, 1.0f, 0.1f}, {0.1f, 1.0f, 0.2f}, {0, 0.3f, 0.2f}, {-0.2f, 1, 0.4f}, FL_ANY, {1, 6, 0.3f, 0}, DROP_NONE, 0},
    [FL_PLANE]     = {"river plane", "Platanites / Credneria (Platanaceae), river banks", 1, 1.0f, 0.4f,
                      {-20, 260, 60}, {0, 0.3f, 0.15f}, {0.5f, 1.0f, 0.1f}, {0.45f, 1.0f, 0.2f}, {0, 0.4f, 0.3f}, FL_ANY, FL_ANY, {0.5f, 6, 0.5f, 0}, DROP_NONE, 0},
    [FL_GINKGO]    = {"ginkgo", "Ginkgo (fan leaves, fleshy yellow seeds), disturbed stream sides", 1, 0.35f, 0.7f,
                      {20, 450, 80}, FL_ANY, {0.35f, 0.9f, 0.15f}, {0.1f, 0.9f, 0.3f}, {0, 0.2f, 0.2f}, FL_ANY, FL_ANY, {0.3f, 5, 3, 0}, DROP_GINKGO, 12},
    [FL_UNDERSTOREY] = {"chloranth tree", "Chloranthaceae (Hedyosmum-like small understorey tree, white berries)", 1, 0.9f, 0.3f,
                      {-20, 350, 80}, FL_ANY, {0.45f, 1.0f, 0.15f}, FL_ANY, {0, 0.5f, 0.3f}, FL_ANY, {0.45f, 1.0f, 0.25f}, {2, 4, 1, 0}, DROP_NONE, 0},
    [FL_CYCAS]     = {"cycad", "Cycas-like trunked cycad (orange seeds on the crown)", 2, 1.0f, 0.4f,
                      FL_ANY, FL_ANY, {0.0f, 0.6f, 0.15f}, {0, 0.4f, 0.2f}, FL_ANY, FL_ANY, {0, 0.7f, 0.3f}, {3, 1, 2, 0}, DROP_CYCAD_SEED, 4},
    [FL_ZAMIA]     = {"squat cycad", "Zamia / Dioon-like squat cycad with a central cone", 2, 0.8f, 0.5f,
                      {80, 1e9f, 80}, FL_ANY, {0.0f, 0.5f, 0.15f}, {0, 0.3f, 0.2f}, FL_ANY, {-1, 0.2f, 0.4f}, {0, 0.6f, 0.3f}, {3, 0, 1, 0}, DROP_NONE, 0},
    [FL_WILLIAMSONIA] = {"williamsonia", "Williamsonia / Williamsoniella (Bennettitales, flower-like cones)", 2, 0.45f, 0.5f,
                      {-20, 600, 100}, FL_ANY, {0.2f, 0.75f, 0.15f}, FL_ANY, FL_ANY, FL_ANY, {0, 0.7f, 0.3f}, {2, 1, 1, 0}, DROP_NONE, 0},
    [FL_BRACKEN]   = {"bracken", "Dennstaedtiaceae (Pteridium-like), tall fern of open ground", 3, 1.2f, 0.7f,
                      FL_ANY, FL_ANY, {0.15f, 0.8f, 0.15f}, {0, 0.5f, 0.2f}, FL_ANY, FL_ANY, {0, 0.35f, 0.25f}, {1, 0, 0, 0}, DROP_NONE, 0},
    [FL_SHIELD]    = {"shield fern", "Dryopteridaceae (Dryopteris-like), low fern of shade", 3, 1.2f, 0.3f,
                      FL_ANY, FL_ANY, {0.35f, 1.0f, 0.15f}, FL_ANY, FL_ANY, FL_ANY, {0.4f, 1.0f, 0.25f}, {1, 0, 0, 0}, DROP_NONE, 0},
    [FL_GLEICHENIA] = {"forking fern", "Gleicheniaceae (Gleichenites), sprawling forked thickets on open slopes", 3, 0.9f, 0.7f,
                      {30, 1e9f, 60}, {0.08f, 1.2f, 0.06f}, {0.1f, 0.7f, 0.15f}, {0, 0.3f, 0.2f}, FL_ANY, FL_ANY, {0, 0.45f, 0.25f}, {0.8f, 0, 0, 0}, DROP_NONE, 0},
    [FL_ROYAL]     = {"royal fern", "Osmundaceae (Osmunda / Osmundastrum, brown fertile fronds), wet ground", 3, 1.0f, 0.5f,
                      FL_ANY, {0, 0.25f, 0.1f}, {0.6f, 1.0f, 0.1f}, {0.3f, 1.0f, 0.2f}, {0, 0.4f, 0.2f}, FL_ANY, FL_ANY, {1.5f, 0, 0, 0.5f}, DROP_NONE, 0},
    [FL_CYATHEA]   = {"tree fern", "Cyatheaceae (Cyathea-like), tall, of wet shaded valleys", 4, 1.0f, 0.4f,
                      {-20, 400, 80}, FL_ANY, {0.6f, 1.0f, 0.1f}, FL_ANY, FL_ANY, {-0.3f, 1, 0.4f}, {0.3f, 1.0f, 0.3f}, {1, 3, 0, 0}, DROP_NONE, 0},
    [FL_DICKSONIA] = {"stout tree fern", "Dicksoniaceae (Dicksonia-like), short and stout, more open ground", 4, 0.8f, 0.4f,
                      FL_ANY, FL_ANY, {0.45f, 1.0f, 0.1f}, FL_ANY, FL_ANY, FL_ANY, {0, 0.6f, 0.3f}, {2, 2, 0, 0}, DROP_NONE, 0},
    [FL_HORSETAIL_GIANT] = {"giant horsetail", "Equisetites, stands several metres tall at the water's edge", 5, 1.0f, 0.3f,
                      FL_ANY, FL_ANY, FL_ANY, {0.65f, 1.0f, 0.15f}, FL_ANY, FL_ANY, FL_ANY, {2, 0, 0, 2}, DROP_NONE, 0},
    [FL_HORSETAIL_FIELD] = {"field horsetail", "Equisetum arvense-like low branched form of damp ground", 5, 1.0f, 0.4f,
                      FL_ANY, FL_ANY, {0.4f, 1.0f, 0.1f}, {0.0f, 0.75f, 0.15f}, FL_ANY, FL_ANY, FL_ANY, {1, 0, 0, 0.5f}, DROP_NONE, 0},
    [FL_SABAL]     = {"fan palm", "Sabalites (costapalmate fan palm), warm lowlands and coasts", 6, 1.0f, 0.4f,
                      {-20, 40, 20}, FL_ANY, FL_ANY, FL_ANY, FL_ANY, FL_ANY, FL_ANY, {0, 2, 3, 0}, DROP_PALM_FRUIT, 10},
    [FL_FEATHER_PALM] = {"feather palm", "a pinnate-leaved palm of the strand (palm fossils: Phoenicites-like leaves)", 6, 0.6f, 0.5f,
                      {-20, 12, 8}, FL_ANY, FL_ANY, FL_ANY, {0.5f, 1.0f, 0.2f}, FL_ANY, FL_ANY, {1, 2, 3, 0}, DROP_PALM_FRUIT, 8},
    [FL_BERRY_SHRUB] = {"berry shrub", "a fleshy-fruited early angiosperm shrub (cf. Sapindopsis, Chloranthaceae shrubs)", 7, 1.0f, 0.4f,
                      {-20, 500, 80}, FL_ANY, {0.3f, 0.9f, 0.15f}, FL_ANY, {0, 0.5f, 0.2f}, FL_ANY, {0.15f, 0.75f, 0.2f}, {3, 0, 2, 0}, DROP_BERRY, 8},
    [FL_HERB]      = {"flowering herb", "an early eudicot herb (Leefructus, Ranunculales-like) of clearings and banks", 7, 1.1f, 0.6f,
                      FL_ANY, FL_ANY, {0.25f, 1.0f, 0.15f}, FL_ANY, {0, 0.6f, 0.2f}, FL_ANY, {0, 0.3f, 0.2f}, {1.5f, 0, 0.3f, 0}, DROP_NONE, 0},
    [FL_GUNNERA]   = {"gunnera", "Gunnera (Gunneraceae: pollen from the Late Cretaceous), huge leaves at streamsides", 7, 1.0f, 0.4f,
                      {-20, 400, 80}, {0, 0.4f, 0.15f}, {0.55f, 1.0f, 0.1f}, {0.5f, 1.0f, 0.15f}, {0, 0.3f, 0.2f}, FL_ANY, FL_ANY, {4, 0, 0, 1}, DROP_NONE, 0},
    [FL_SCRUB]     = {"evergreen scrub", "small-leaved evergreen scrub of dry ridges and the strand", 7, 0.9f, 0.4f,
                      FL_ANY, FL_ANY, {0.0f, 0.55f, 0.15f}, {0, 0.3f, 0.2f}, FL_ANY, FL_ANY, {0, 0.6f, 0.3f}, {2, 0, 0.3f, 0}, DROP_NONE, 0},
    [FL_LILY]      = {"water lily", "Nymphaeales (Microvictoria, Turonian; Nymphaeaceae pads and flowers)", -1, 1.0f, 0.6f,
                      FL_ANY, FL_ANY, FL_ANY, FL_ANY, FL_ANY, FL_ANY, FL_ANY, {0, 0, 0.2f, 3}, DROP_NONE, 0},
    [FL_CLUBMOSS]  = {"clubmoss and moss", "Selaginella / Lycopodiales mats and moss cushions of damp shade", -1, 1.0f, 0.5f,
                      FL_ANY, FL_ANY, {0.45f, 1.0f, 0.15f}, FL_ANY, FL_ANY, FL_ANY, {0.4f, 1.0f, 0.2f}, {0.2f, 0, 0, 0}, DROP_NONE, 0},
    [FL_FUNGI]     = {"bracket fungi and mushrooms", "polypores and gilled mushrooms on dead wood (Cretaceous mushrooms in amber: Palaeoagaracites)", -1, 1.0f, 0.0f,
                      FL_ANY, FL_ANY, FL_ANY, FL_ANY, FL_ANY, FL_ANY, FL_ANY, {0, 0, 0, 0}, DROP_NONE, 0},
};

// Which species each variant slot of a bake-layer kind holds (8 slots per
// kind). A species in two slots has two seeds of the same plant; kinds keep
// their slot count, draws and impostor layers as before.
static const int8_t FLORA_SLOT[TERRA_VEG_COUNT][8] = {
    /* conifer   */ {FL_ARAUCARIA, FL_PODOCARP, FL_UMBRELLA, FL_REDWOOD, FL_ARAUCARIA, FL_PODOCARP, FL_CHEIROLEP, FL_REDWOOD},
    /* broadleaf */ {FL_MAGNOLIA, FL_LAUREL, FL_KATSURA, FL_GINKGO, FL_MAGNOLIA, FL_LAUREL, FL_PLANE, FL_UNDERSTOREY},
    /* cycad     */ {FL_CYCAS, FL_ZAMIA, FL_CYCAS, FL_WILLIAMSONIA, FL_CYCAS, FL_ZAMIA, FL_CYCAS, FL_WILLIAMSONIA},
    /* fern      */ {FL_BRACKEN, FL_SHIELD, FL_GLEICHENIA, FL_ROYAL, FL_BRACKEN, FL_SHIELD, FL_GLEICHENIA, FL_ROYAL},
    /* treefern  */ {FL_CYATHEA, FL_CYATHEA, FL_DICKSONIA, FL_DICKSONIA, FL_CYATHEA, FL_CYATHEA, FL_DICKSONIA, FL_DICKSONIA},
    /* horsetail */ {FL_HORSETAIL_GIANT, FL_HORSETAIL_GIANT, FL_HORSETAIL_FIELD, FL_HORSETAIL_FIELD, FL_HORSETAIL_GIANT, FL_HORSETAIL_GIANT, FL_HORSETAIL_FIELD, FL_HORSETAIL_FIELD},
    /* palm      */ {FL_SABAL, FL_SABAL, FL_SABAL, FL_FEATHER_PALM, FL_SABAL, FL_SABAL, FL_SABAL, FL_FEATHER_PALM},
    /* shrub     */ {FL_BERRY_SHRUB, FL_HERB, FL_GUNNERA, FL_SCRUB, FL_BERRY_SHRUB, FL_HERB, FL_GUNNERA, FL_SCRUB},
};
// The species in variant slot v of bake-layer kind k (-1 for other kinds).
static inline int flora_species(int kind, int variant) { return kind >= 0 && kind < TERRA_VEG_COUNT && variant >= 0 && variant < 8 ? FLORA_SLOT[kind][variant] : -1; }

static inline float fl_clamp(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline float fl_sstep(float a, float b, float x) { float t = fl_clamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
static inline float fl_range(FloraRange r, float x) { return (x < r.lo ? fl_sstep(r.lo - r.soft, r.lo, x) : 1.0f) * (x > r.hi ? 1.0f - fl_sstep(r.hi, r.hi + r.soft, x) : 1.0f); }
static inline uint32_t fl_hash(uint32_t h) { h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16; return h; }
// Smooth value noise in 0..1 on a unit lattice, seeded.
static inline float fl_vnoise(uint32_t seed, float x, float z) {
    float fx0 = floorf(x), fz0 = floorf(z); int ix = (int)fx0, iz = (int)fz0; float fx = x - fx0, fz = z - fz0;
    fx = fx * fx * (3 - 2 * fx); fz = fz * fz * (3 - 2 * fz);
    #define FLC(a, b) (fl_hash(seed ^ fl_hash((uint32_t)(ix + (a)) * 0x8da6b343u ^ (uint32_t)(iz + (b)) * 0xd8163841u)) * (1.0f / 4294967296.0f))
    float v = (FLC(0, 0) * (1 - fx) + FLC(1, 0) * fx) * (1 - fz) + (FLC(0, 1) * (1 - fx) + FLC(1, 1) * fx) * fz;
    #undef FLC
    return v;
}
// Two octaves at `scale` metres, 0..1 with its mean near 0.5.
static inline float fl_noise(uint32_t seed, float x, float z, float scale) {
    return 0.68f * fl_vnoise(seed, x / scale, z / scale) + 0.32f * fl_vnoise(seed * 0x9e3779b9u + 7u, x / scale * 2.13f + 19.1f, z / scale * 2.13f + 7.3f);
}

// Patches at the 50-200 m scale, seeded by the world seed:
// gaps: canopy openings and clearings (0 open .. 1 full canopy density);
// old: old stands with standing and fallen dead wood (0 young .. 1 old).
static inline float flora_gap(const TerraMap* m, float x, float z) { return fl_sstep(0.24f, 0.36f, fl_noise(m->seed * 0x2c1b3c6du + 0x61u, x, z, 150.0f)); }
static inline float flora_old(const TerraMap* m, float x, float z) { return fl_sstep(0.5f, 0.72f, fl_noise(m->seed * 0x297a2d39u + 0x0du, x, z, 180.0f)); }
// A species' stand noise: 0..2, mean about 1; pure stands where it is high.
static inline float flora_stand(const TerraMap* m, int sp, float x, float z) {
    float n = fl_noise(m->seed * 0x85ebca6bu + (uint32_t)sp * 0x27d4eb2fu, x, z, 70.0f + 9.0f * (sp % 7));
    return 1.0f + FLORA[sp].stand * (2.0f * fl_sstep(0.3f, 0.7f, n) - 1.0f);
}

// The world as a plant at (x, z) sees it. Layer densities are bilinear
// (0..1) like veg_density; shade is the canopy closure over the spot.
typedef struct FloraSite { float elev, slope, moist, wet, coast, form, shade, gap, old, dry_ridge; } FloraSite;
static inline float flora_layer(const TerraMap* m, int layer, float x, float z) {
    float gx = fl_clamp(x / m->cell, 0, m->n - 1.001f), gz = fl_clamp(z / m->cell, 0, m->n - 1.001f);
    int ix = (int)gx, iz = (int)gz; float fx = gx - ix, fz = gz - iz; size_t i = (size_t)iz * m->n + ix; const uint8_t* v = m->veg;
    float a = v[i * TERRA_VEG_COUNT + layer], b = v[(i + 1) * TERRA_VEG_COUNT + layer];
    float c = v[(i + m->n) * TERRA_VEG_COUNT + layer], d = v[(i + m->n + 1) * TERRA_VEG_COUNT + layer];
    return ((a + (b - a) * fx) * (1 - fz) + (c + (d - c) * fx) * fz) * (1.0f / 255.0f);
}
static inline FloraSite flora_site(const TerraMap* m, float x, float z) {
    FloraSite s; float hab[TERRA_HAB_COUNT], n[3];
    terra_habitat_at(m, x, z, hab); terra_normal(m, x, z, n);
    s.elev = terra_height(m, x, z); s.slope = sqrtf(fmaxf(1 - n[1] * n[1], 0)) / fmaxf(n[1], 0.05f);
    s.moist = hab[TERRA_HAB_MOIST]; s.wet = hab[TERRA_HAB_WET]; s.coast = hab[TERRA_HAB_COAST];
    s.form = fl_clamp((hab[TERRA_HAB_CURV] * 255.0f - 128.0f) / 31.0f / 2.0f, -1, 1);   // a 2 m hollow or ridge is full
    s.gap = flora_gap(m, x, z); s.old = flora_old(m, x, z);
    // Dry ridges: convex, dry and high ground, where the canopy opens into cycad scrub.
    s.dry_ridge = fl_sstep(0.05f, 0.45f, -s.form) * fl_sstep(0.5f, 0.3f, s.moist) * fl_sstep(120.0f, 260.0f, s.elev);
    float canopy = flora_layer(m, TERRA_VEG_CONIFER, x, z) + flora_layer(m, TERRA_VEG_BROADLEAF, x, z);
    s.shade = fl_clamp(canopy * 1.15f * s.gap * (1 - 0.7f * s.dry_ridge), 0, 1);
    return s;
}
static inline float flora_suit(int sp, const FloraSite* s) {
    const FloraSpecies* f = &FLORA[sp];
    return fl_range(f->elev, s->elev) * fl_range(f->slope, s->slope) * fl_range(f->moist, s->moist) * fl_range(f->wet, s->wet)
         * fl_range(f->coast, s->coast) * fl_range(f->form, s->form) * fl_range(f->shade, s->shade);
}
// How dense a bake-layer kind grows here relative to its layer: canopy gaps
// and clearings, open dry ridges, more cycads and scrub on them, horsetails
// and gunnera crowding the banks.
// The bank (0..1): the strip just back from river water, where horsetails,
// gunnera and royal ferns crowd and the tall canopy stands back.
static inline float flora_bank(const FloraSite* s) { return fl_sstep(0.55f, 0.85f, s->wet); }
static inline float flora_density_mul(int kind, const FloraSite* s) {
    float bank = flora_bank(s), strand = fl_sstep(0.5f, 0.85f, s->coast) * (1 - fl_sstep(6.0f, 14.0f, s->elev));
    switch (kind) {
    case TERRA_VEG_CONIFER: case TERRA_VEG_BROADLEAF: return s->gap * (1 - 0.65f * s->dry_ridge) * (1 - 0.55f * fl_sstep(0.85f, 0.97f, s->wet)) * (1 - 0.6f * strand);
    case TERRA_VEG_CYCAD: return 1 + 1.2f * s->dry_ridge + 0.4f * (1 - s->gap);
    case TERRA_VEG_SHRUB: return 1 + 0.6f * s->dry_ridge + 0.5f * (1 - s->gap) + 0.5f * bank + 0.8f * strand;
    case TERRA_VEG_FERN: return 1 + 0.25f * (1 - s->gap);
    case TERRA_VEG_TREEFERN: return 1 + 0.5f * fl_sstep(0.3f, 0.6f, s->wet) * (1 - bank);
    default: return 1;
    }
}
// Each species' share of its kind here (weights, summed to 1 in w[]); returns
// how many species the kind has. w[] has room for 8.
static inline int flora_shares(const TerraMap* m, int kind, float x, float z, const FloraSite* s, int sp_out[8], float w[8]) {
    int n = 0; float tot = 0;
    for (int v = 0; v < 8; v++) {
        int sp = FLORA_SLOT[kind][v], seen = 0;
        for (int k = 0; k < n; k++) seen |= sp_out[k] == sp;
        if (seen) continue;
        sp_out[n] = sp; w[n] = FLORA[sp].weight * flora_suit(sp, s) * flora_stand(m, sp, x, z); tot += w[n]; n++;
    }
    // Nothing suits (a plant the layer put somewhere odd): its kind's species
    // weigh in by commonness alone.
    if (tot <= 1e-6f) { tot = 0; for (int k = 0; k < n; k++) { w[k] = FLORA[sp_out[k]].weight; tot += w[k]; } }
    for (int k = 0; k < n; k++) w[k] /= tot;
    return n;
}
// Picks the variant slot of a plant of bake-layer kind at (x, z) from two
// dice u (species) and u2 (which of its slots).
static inline int flora_pick(const TerraMap* m, int kind, float x, float z, const FloraSite* s, float u, float u2) {
    int sp[8]; float w[8]; int n = flora_shares(m, kind, x, z, s, sp, w), pick = n - 1;
    for (int k = 0; k < n - 1; k++) { if (u < w[k]) { pick = k; break; } u -= w[k]; }
    int slots[8], ns = 0;
    for (int v = 0; v < 8; v++) if (FLORA_SLOT[kind][v] == sp[pick]) slots[ns++] = v;
    return slots[(int)(u2 * ns) % ns];
}
#endif
