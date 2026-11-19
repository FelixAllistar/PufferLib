// terra: physically motivated terrain generation. Pure C11, no dependencies.
//
// Pipeline (all baked offline, deterministic per seed):
//   1. Tectonics: warped Voronoi plates with motion vectors. Convergent
//      boundaries uplift, divergent ones subside, hotspots build volcanoes.
//   2. Landscape evolution, coarse to fine (multigrid): uplift vs implicit
//      stream-power fluvial incision (Braun & Willett 2013) with orographic
//      rainfall, stratified rock hardness, hillslope diffusion and lakes.
//   3. Fine detail: relief-scaled noise, particle hydraulic erosion,
//      thermal (talus) relaxation.
//   4. Hydrology: priority-flood lakes at spill level, rainfall-weighted flow
//      accumulation, carved river channels and a water-surface field.
//   3b. Bedrock: more uplift under the high ground (peaks), hard beds
//      weathered into benches and cliff bands, and a few bedrock shelves
//      (scoops worn into mountain sides with a bedrock face) per range.
//   4b. Regolith and wind: loose ground thins on steep, convex, high and
//      windward ground; the wind strips exposed ground and drops it in the lee.
//   5. Climate and surface: temperature, moisture, soil, material splat
//      weights and vegetation density layers, a tree line, bare rock where
//      the loose ground is gone.
//
// Conventions: a map is n*n samples, row-major [z*n + x], spacing `cell`
// metres; heights in metres with sea level at 0.
#ifndef TERRA_H
#define TERRA_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define TERRA_VERSION 26u   // 26: tree islands above the tree line (alpine hill-climb); 25: grass line, scree fans, gullies, near-desert, mixed-stand density (mountains hill-climb)
#define TERRA_DRY (-1.0e4f)   // water surface value for cells with no water

enum { TERRA_WATER_NONE, TERRA_WATER_SEA, TERRA_WATER_LAKE, TERRA_WATER_RIVER };

// Surface materials, blended by splat weights.
enum {
    TERRA_MAT_ROCK,      // bare cliff rock
    TERRA_MAT_SCREE,     // talus, gravel
    TERRA_MAT_DIRT,      // dry soil
    TERRA_MAT_FOREST,    // needle/leaf litter under canopy
    TERRA_MAT_FERN,      // fern prairie ground (no grass in the Cretaceous)
    TERRA_MAT_MUD,       // floodplains, swamps, river banks
    TERRA_MAT_SAND,      // beaches and bars
    TERRA_MAT_MOSS,      // wet shaded rock and ground
    TERRA_MAT_COUNT
};

// Vegetation layers (density 0..255), roughly mid/late Cretaceous flora.
enum {
    TERRA_VEG_CONIFER,    // araucarians, podocarps, redwood-like
    TERRA_VEG_BROADLEAF,  // early angiosperms (magnolia/laurel-like), ginkgo
    TERRA_VEG_CYCAD,      // cycads and bennettitaleans, dry open ground
    TERRA_VEG_FERN,       // ground ferns
    TERRA_VEG_TREEFERN,   // tree ferns, wet valleys
    TERRA_VEG_HORSETAIL,  // equisetum stands at water margins
    TERRA_VEG_PALM,       // sabal-like palms, warm coasts
    TERRA_VEG_SHRUB,      // low scrub
    TERRA_VEG_COUNT
};

// Habitat channels (TerraMap.habitat), derived at load from fields every
// build ships (terra_habitat), so native and web place the same plants.
enum {
    TERRA_HAB_MOIST,   // moisture index 0..1: water nearby, drainage, hollows, wet or dry ground
    TERRA_HAB_WET,     // nearness to river or lake water 0..1 (1 within about 25 m)
    TERRA_HAB_COAST,   // nearness to the sea 0..1 (1 within about 60 m)
    TERRA_HAB_CURV,    // landform: 128 + 31 * (hollow depth in m, -4..4); above 128 hollows, below ridges
    TERRA_HAB_COUNT
};

typedef struct TerraParams {
    uint32_t seed;
    int n;               // final resolution (power of two: 512..4096)
    float cell;          // metres per sample at final resolution
    int plates;          // tectonic plate count
    float land_fraction; // target share of continental crust (0..1)
    float uplift;        // peak boundary uplift, m/yr
    float erodibility;   // stream-power K, 1/yr (m=0.5, n=1)
    float diffusion;     // hillslope diffusion, m^2/yr
    float evolve_years;  // geological time simulated at the coarsest level
    int droplets_per_cell_x100; // hydraulic erosion droplets per sample * 100
    float wind_angle;    // prevailing wind direction (radians)
    float rain;          // mean rainfall scale (m/yr at sea level)
    float temperature;   // sea-level mean temperature, deg C
    float shoulders;     // rock shoulder sites per suitable hillside (0: none, the default; bedrock shelves replace them)
    float peaks;         // extra uplift of the high ground: ground above 220 m is raised by this times its height over it (0: none)
    float bedrock;       // bedrock shelves and crags (terra.c bedrock): 1 the default, more or fewer shelves, 0 none
    // Ravines, cut when a world loads (world.c gorges_build, not baked, so not in the bake's hash):
    // TERRA_RAVINE_NONE, TERRA_RAVINE_STEPPED (the narrow three-ledge gorges) or TERRA_RAVINE_WIDE.
    int ravine;
    int threads;         // worker threads (0 = auto)
    int verbose;
} TerraParams;

enum { TERRA_RAVINE_NONE, TERRA_RAVINE_STEPPED, TERRA_RAVINE_WIDE };

// A rock shoulder: a short curved notch bitten into a hillside, its back wall a near-vertical
// scarp a few metres to about 15 m high that the props fill with rock faces (render/props_place.c),
// a talus apron at its foot and the slope above rolling over its top. The bake cuts it into the
// heightfield before hydrology and the surface (terra.c shoulders_cut), so water, splat, plants and
// the sim's collider all see it. In the notch's frame a point lies `a` m along the contour (positive
// to the right looking uphill) and `b` m uphill of (x, z); the wall foot runs along b = terra_shoulder_wall(t)
// for t = a / half.
typedef struct TerraShoulder {
    float x, z;        // the middle of the notch's mouth on the hillside (m)
    float ux, uz;      // unit direction uphill, into the hill (the rock faces look the other way)
    float half;        // half the mouth's width along the contour (m)
    float depth;       // how far the back wall's foot lies uphill of (x, z) in the middle (m)
    float floor;       // floor height at (x, z) (m); it rises 5% toward the wall so it drains out
    float wall;        // planned wall height at the back (m)
    float lean;        // -1..1: which way the curve is skewed (the bites are not symmetric)
    uint32_t seed;     // per-site stream: the wall's wobble, the rocks
} TerraShoulder;
#define TERRA_SHOULDER_RISE 0.05f

typedef struct TerraMap {
    int n;
    float cell;
    uint32_t seed;
    float *height;      // terrain surface (m)
    float *water;       // water surface height or TERRA_DRY
    float *flow;        // rainfall-weighted drainage (m^3/yr)
    float *rain;        // precipitation (m/yr)
    float *soil;        // loose ground over the bedrock (m): regolith and sediment, after the wind (terra.c regolith)
    float *temp;        // mean temperature (C)
    uint8_t *water_kind;// TERRA_WATER_*
    int8_t *flow_dir;   // 2 per sample: downstream unit vector * 127
    uint8_t *splat;     // TERRA_MAT_COUNT per sample, sums to 255
    uint8_t *veg;       // TERRA_VEG_COUNT per sample
    uint8_t *hardness;  // rock hardness 0..255 (for shading strata)
    // Derived at load and after a bake, never saved (terra_habitat):
    // TERRA_HAB_COUNT bytes per sample of an (n/2) x (n/2) grid, 2 cells apart.
    uint8_t *habitat;
    // Set by the owner of the map, never saved or freed here: the B-spline
    // coefficients of the drawn surface (world.c), or NULL. Prop seating
    // (render/props_place.c) puts rims under it.
    const float *surface;
    // Set by the owner of the map, never saved: the ravine style (TERRA_RAVINE_*) world.c cut at load.
    int ravine;
    // Rock shoulders the bake cut (saved with the map); see TerraShoulder.
    TerraShoulder *shoulder;
    int nshoulder;
    // Set by the owner of the map, never saved or freed here: every rock shoulder's rocks laid out once
    // (render/props_place.c prop_shoulders_prepare), or NULL (the prop scatter then lays them out itself).
    const struct PropShoulders *shoulder_props;
} TerraMap;

typedef void (*TerraProgress)(const char* stage, float fraction, void* user);

TerraParams terra_default_params(uint32_t seed);
// Applies "key=value" pairs separated by spaces or commas, e.g.
// "k=6e-5 uplift=1e-3 diffusion=0.006". Keys: n cell plates land uplift k
// diffusion years drops wind rain temp shoulders peaks bedrock ravine (0 none, 1 stepped, 2 wide).
// Returns pairs applied, -1 on a bad key.
int terra_params_apply(TerraParams* p, const char* spec);
// Returns 0 on success. The map owns its arrays; free with terra_free.
int terra_generate(const TerraParams* p, TerraMap* out, TerraProgress progress, void* user);
void terra_free(TerraMap* m);
int terra_save(const TerraMap* m, const TerraParams* p, const char* path);
// Loads only if the file matches TERRA_VERSION and the params (when given).
int terra_load(TerraMap* m, const TerraParams* p, const char* path);
uint64_t terra_params_hash(const TerraParams* p);
// Derives m->habitat from the height, water, flow, splat and vegetation
// layers (not rain, soil or temperature, which the web build does not ship).
// terra_generate and terra_load call it; a map built by hand may too.
// Deterministic for any thread count. Returns 0 on success.
int terra_habitat(TerraMap* m);
// Bilinear habitat at a point, each channel 0..1 (TERRA_HAB_CURV: 0.5 flat).
// Without a habitat grid: moist 0.5, wet 0, coast 0, flat.
void terra_habitat_at(const TerraMap* m, float x, float z, float out[TERRA_HAB_COUNT]);

// Queries (bilinear, world metres from the map origin corner).
float terra_height(const TerraMap* m, float x, float z);
// The smooth surface: cubic B-spline coefficients (n*n, caller frees) whose
// spline passes through every height sample, and its value at (x, z). The
// renderer draws this surface (render/view.c, shaders/terrain_height.glsl),
// so whatever stands on it should sample it too (cret_height).
float* terra_bspline_coefficients(const float* height, int n);
float terra_bspline_height(const float* coef, int n, float cell, float x, float z);
// terra_bspline_height at count points (out[k] for x[k], z[k]), bit for bit, several times faster per point.
void terra_bspline_heights(const float* coef, int n, float cell, const float* x, const float* z, int count, float* out);
float terra_water(const TerraMap* m, float x, float z);
// terra_water at count points.
void terra_waters(const TerraMap* m, const float* x, const float* z, int count, float* out);
void terra_normal(const TerraMap* m, float x, float z, float out[3]);
static inline float terra_size(const TerraMap* m) { return (m->n - 1) * m->cell; }

// Rock shoulder geometry, shared by the cut, the surface, the props and the tests.
// How far uphill of the mouth the wall's foot lies at t = a / half (negative past |t| = 1, where the
// curve sweeps back down the slope and the notch's sides die out into the hillside).
float terra_shoulder_wall(const TerraShoulder* s, float t);
// Signed horizontal distance (m, approximate) from the wall's foot: negative inside the notch, positive
// into the hill. Optionally also the notch-frame coordinates a and b.
float terra_shoulder_edge(const TerraShoulder* s, float x, float z, float* a, float* b);
// Radius (m) around (x, z) outside which a shoulder changes nothing.
float terra_shoulder_reach(const TerraShoulder* s);
// The wall's foot at t: position (out[0], out[1]) and the level unit normal out of the hill (out[2], out[3]).
void terra_shoulder_foot(const TerraShoulder* s, float t, float out[4]);
// terra_shoulder_edge of the nearest shoulder whose reach holds (x, z) plus `margin`, or 1e9 if none;
// its index in *which (when given). A linear scan: the island has a few hundred at most.
float terra_shoulder_near(const TerraMap* m, float x, float z, float margin, int* which);

#ifdef __cplusplus
}
#endif
#endif
