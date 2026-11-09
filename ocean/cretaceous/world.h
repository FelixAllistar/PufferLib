// World: the baked terrain shared by every sim, view and server in a process.
#ifndef CRET_WORLD_H
#define CRET_WORLD_H
#include "terra/terra.h"
#include "box3d/box3d.h"
#include "render/props_place.h"   // shared prop placement (no GL)
#ifdef __cplusplus
extern "C" {
#endif

// Collision categories shared by the sim and the prop colliders.
#define CRET_CAT_AGENT 0x2ull
#define CRET_CAT_PROP  0x4ull

// A named place for quests (progress.c builds them once per world from the
// water, vegetation and relief; every sim and client derives the same list).
enum { CRET_POI_LAKE, CRET_POI_RIVER, CRET_POI_MEADOW, CRET_POI_GROVE, CRET_POI_MARSH, CRET_POI_PEAK, CRET_POI_BEACH, CRET_POI_KINDS };
#define CRET_MAX_POIS 32
typedef struct CretPoi { char name[32]; uint8_t kind; float x, z, r; } CretPoi;   // r: extent (m) around the walkable anchor x, z

// A large prop that blocks movement: same placement as the drawn mesh.
typedef struct CretPropCollider { float x, y, z, scale, q[4]; uint16_t variant, kind; } CretPropCollider;

typedef struct CretWorld {
    TerraParams params;
    TerraMap map;
    float* hcoef;               // B-spline coefficients of map.height: the drawn surface (cret_height)
    float size;                 // metres per side
    b3HeightFieldData* field;   // collision, shared by all physics worlds
    // Prop colliders (assets/props; none if the scans are missing). Instances
    // are bucketed per PROP_TILE tile; shapes are shared by every physics world.
    PropCatalog prop_cat;
    int prop_tiles;                       // tiles per side
    int* prop_start;                      // prop_tiles^2 + 1 offsets into props
    CretPropCollider* props; int nprops;
    b3HullData* prop_hull[PROP_MAX_VARIANTS];   // convex classes (boulders, logs, stumps, roots)
    b3MeshData* prop_mesh[PROP_MAX_VARIANTS];   // coast rocks and cliff faces (concave)
    CretPoi pois[CRET_MAX_POIS]; int npois; bool pois_ok;   // quest places (progress.c, on first use)
    int refs;
} CretWorld;

// Per physics world: static prop bodies are created for tiles near agents and
// destroyed once no agent has been near for a while, so thousands of sims can
// share one island without each holding every collider.
typedef struct CretPropSlot { int tile, last, n; b3BodyId* bodies; } CretPropSlot;
typedef struct CretPropStream {
    int tiles;
    int32_t* slot;                        // tile -> slot index + 1 (0: not loaded)
    CretPropSlot* s; int ns, cap;
} CretPropStream;
void cret_prop_stream_init(CretPropStream* ps, const CretWorld* w);
// Ensures colliders exist within `reach` metres of (x, z); `tick` ages unused tiles.
void cret_prop_stream_touch(CretPropStream* ps, const CretWorld* w, b3WorldId phys, float x, float z, float reach, int tick);
// Unloads tiles not touched for `max_age` ticks.
void cret_prop_stream_trim(CretPropStream* ps, int tick, int max_age);
void cret_prop_stream_free(CretPropStream* ps);   // bodies die with the physics world

// Loads build/cretaceous/worlds/<seed>_<hash>.terra or bakes and caches it.
// Returned worlds are shared per (seed, n, cell); release with cret_world_release.
CretWorld* cret_world_acquire(uint32_t seed, int n, float cell, int verbose);
void cret_world_release(CretWorld* w);
// Physics body holding the shared heightfield.
b3BodyId cret_world_add_ground(const CretWorld* w, b3WorldId world);

// Ground height: the smooth cubic surface the terrain is drawn with (not
// the bilinear one between samples, which sat up to ~1.5 m off it on crests
// and in gullies, so feet sank or floated).
static inline float cret_height(const CretWorld* w, float x, float z) { return terra_bspline_height(w->hcoef, w->map.n, w->map.cell, x, z); }
static inline float cret_water(const CretWorld* w, float x, float z) { return terra_water(&w->map, x, z); }
#ifdef __cplusplus
}
#endif
#endif
