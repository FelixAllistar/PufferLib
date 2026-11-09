// World: the baked terrain shared by every sim, view and server in a process.
#ifndef CRET_WORLD_H
#define CRET_WORLD_H
#include "terra/terra.h"
#include "box3d/box3d.h"
#include "render/props_place.h"   // shared prop placement (no GL)
#ifdef __cplusplus
extern "C" {
#endif

// Collision categories shared by the sim and the prop colliders. Agents
// are kinematic capsules moved by the Box3D mover over the analytic ground;
// they only take part in queries (mask AGENT), so no contact ever forms
// between them and props. Static props and tree trunks are PROP (mask
// AGENT | DYN). Movable props (boulders, logs) are dynamic bodies in PROP |
// DYN, so movers and the camera see them like any prop, and collide with
// the ground heightfield (GROUND, mask DYN), static props and each other.
#define CRET_CAT_GROUND 0x1ull
#define CRET_CAT_AGENT  0x2ull
#define CRET_CAT_PROP   0x4ull
#define CRET_CAT_DYN    0x8ull

// A named place for quests (progress.c builds them once per world from the
// water, vegetation and relief; every sim and client derives the same list).
enum { CRET_POI_LAKE, CRET_POI_RIVER, CRET_POI_MEADOW, CRET_POI_GROVE, CRET_POI_MARSH, CRET_POI_PEAK, CRET_POI_BEACH, CRET_POI_KINDS };
#define CRET_MAX_POIS 32
typedef struct CretPoi { char name[32]; uint8_t kind; float x, z, r; } CretPoi;   // r: extent (m) around the walkable anchor x, z

// A large prop that blocks movement: same placement as the drawn mesh
// (key: PropInstance.key). Dynamic ones (prop_dynamic: boulders, big
// stones, logs, driftwood) are Box3D dynamic bodies the sim may move;
// their index in CretWorld.props is their id in the sim and on the wire.
typedef struct CretPropCollider { float x, y, z, scale, q[4]; uint16_t variant, kind; uint32_t key; uint8_t dyn; } CretPropCollider;
#define CRET_PROP_PARTS 4          // convex pieces of a log collider (slabs along its length)
#define CRET_ROCK_DENSITY 2400.0f  // kg/m^3 of the boulder and stone colliders
#define CRET_WOOD_DENSITY 420.0f   // dead, dry wood (logs, driftwood)

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
    // Colliders per variant, from the LOD1 mesh (LOD2 drops parts of some
    // scans: the bent trunk's ends). Convex classes get one hull (32
    // vertices); logs and driftwood a compound of slabs along the trunk;
    // roots, coast rocks and cliff faces a triangle mesh (static only).
    b3HullData* prop_hull[PROP_MAX_VARIANTS];
    b3HullData* prop_part[PROP_MAX_VARIANTS][CRET_PROP_PARTS]; int prop_nparts[PROP_MAX_VARIANTS];
    b3MeshData* prop_mesh[PROP_MAX_VARIANTS];
    float* prop_mesh_pos[PROP_MAX_VARIANTS]; int32_t* prop_mesh_idx[PROP_MAX_VARIANTS];   // mesh source (Box3D references it)
    float prop_vol[PROP_MAX_VARIANTS];          // collider volume at scale 1 (m^3; mass = density x vol x scale^3)
    int ndyn;                                   // colliders with dyn set
    bool no_trunks;                             // CRET_NO_PROP_COLLIDERS or CRET_NO_TREE_COLLIDERS: trees are walk-through
    CretPoi pois[CRET_MAX_POIS]; int npois; bool pois_ok;   // quest places (progress.c, on first use)
    int refs;
} CretWorld;

// Per physics world: static prop bodies are created for tiles near agents and
// destroyed once no agent has been near for a while, so thousands of sims can
// share one island without each holding every collider.
// A tile also gets one static body with a capsule per tree trunk (the veg
// scatter, render/veg_place.h). Dynamic props are created asleep, with
// userData = id + 1; once the sim tracks one (owned() true: it moved), the
// sim keeps or destroys its body itself and the tile skips it.
typedef struct CretPropSlot { int tile, last, n; b3BodyId* bodies; int32_t* ids; } CretPropSlot;   // ids: collider id, -1 static
typedef struct CretPropStream {
    int tiles;
    int32_t* slot;                        // tile -> slot index + 1 (0: not loaded)
    CretPropSlot* s; int ns, cap;
    bool (*owned)(void* ctx, int id); void* owned_ctx;   // the sim tracks this dynamic prop (dynprops.c)
} CretPropStream;
void cret_prop_stream_init(CretPropStream* ps, const CretWorld* w);
// Ensures colliders exist within `reach` metres of (x, z); `tick` ages unused tiles.
void cret_prop_stream_touch(CretPropStream* ps, const CretWorld* w, b3WorldId phys, float x, float z, float reach, int tick);
// Unloads tiles not touched for `max_age` ticks.
void cret_prop_stream_trim(CretPropStream* ps, int tick, int max_age);
void cret_prop_stream_free(CretPropStream* ps);   // bodies die with the physics world
// True if tile (of PROP_TILE m) under (x, z) is loaded.
bool cret_prop_stream_loaded(const CretPropStream* ps, float x, float z);
// The body of collider `id` at position p, rotation q (x, y, z, w); dynamic
// colliders as dynamic bodies (asleep unless `awake`), the rest static.
b3BodyId cret_prop_body(const CretWorld* w, b3WorldId phys, int id, const float p[3], const float q[4], bool awake);
// Mass (kg) of dynamic collider id.
float cret_prop_mass(const CretWorld* w, int id);
// Trunk capsules of the trees in tile (tx, tz) (render/veg_place.h): base
// centre xyz, radius and height per trunk in out[k*5..]; returns the count.
int cret_tree_trunks(const CretWorld* w, int tx, int tz, float* out, int cap);

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
