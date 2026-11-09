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
//   5. Climate and surface: temperature, moisture, soil, material splat
//      weights and vegetation density layers.
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

#define TERRA_VERSION 20u
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
    int threads;         // worker threads (0 = auto)
    int verbose;
} TerraParams;

typedef struct TerraMap {
    int n;
    float cell;
    uint32_t seed;
    float *height;      // terrain surface (m)
    float *water;       // water surface height or TERRA_DRY
    float *flow;        // rainfall-weighted drainage (m^3/yr)
    float *rain;        // precipitation (m/yr)
    float *soil;        // loose sediment depth (m)
    float *temp;        // mean temperature (C)
    uint8_t *water_kind;// TERRA_WATER_*
    int8_t *flow_dir;   // 2 per sample: downstream unit vector * 127
    uint8_t *splat;     // TERRA_MAT_COUNT per sample, sums to 255
    uint8_t *veg;       // TERRA_VEG_COUNT per sample
    uint8_t *hardness;  // rock hardness 0..255 (for shading strata)
} TerraMap;

typedef void (*TerraProgress)(const char* stage, float fraction, void* user);

TerraParams terra_default_params(uint32_t seed);
// Applies "key=value" pairs separated by spaces or commas, e.g.
// "k=6e-5 uplift=1e-3 diffusion=0.006". Keys: n cell plates land uplift k
// diffusion years drops wind rain temp. Returns pairs applied, -1 on a bad key.
int terra_params_apply(TerraParams* p, const char* spec);
// Returns 0 on success. The map owns its arrays; free with terra_free.
int terra_generate(const TerraParams* p, TerraMap* out, TerraProgress progress, void* user);
void terra_free(TerraMap* m);
int terra_save(const TerraMap* m, const TerraParams* p, const char* path);
// Loads only if the file matches TERRA_VERSION and the params (when given).
int terra_load(TerraMap* m, const TerraParams* p, const char* path);
uint64_t terra_params_hash(const TerraParams* p);

// Queries (bilinear, world metres from the map origin corner).
float terra_height(const TerraMap* m, float x, float z);
// The smooth surface: cubic B-spline coefficients (n*n, caller frees) whose
// spline passes through every height sample, and its value at (x, z). The
// renderer draws this surface (render/view.c, shaders/terrain_height.glsl),
// so whatever stands on it should sample it too (cret_height).
float* terra_bspline_coefficients(const float* height, int n);
float terra_bspline_height(const float* coef, int n, float cell, float x, float z);
float terra_water(const TerraMap* m, float x, float z);
void terra_normal(const TerraMap* m, float x, float z, float out[3]);
static inline float terra_size(const TerraMap* m) { return (m->n - 1) * m->cell; }

#ifdef __cplusplus
}
#endif
#endif
