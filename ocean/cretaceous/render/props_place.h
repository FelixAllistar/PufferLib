// Prop placement shared by the renderer (props.c) and the physics colliders
// (world.c): the placement classes, the catalog of converted scans (read
// from .prop headers, no GL), and the deterministic per-tile scatter from the
// baked terra layers. Both sides call the same scatter, so colliders sit
// exactly where the meshes are drawn.
#ifndef CRET_PROPS_PLACE_H
#define CRET_PROPS_PLACE_H
#include <stdbool.h>
#include <stdint.h>
#include "../terra/terra.h"
#ifdef __cplusplus
extern "C" {
#endif

#define PROP_MAX_VARIANTS 64
#define PROP_TILE 64.0f
#define PROP_BASE_PTS 16    // rim points per variant that seating buries (prop_catalog_set_base)
#define PROP_DIR "ocean/cretaceous/assets/props/"

enum {   // placement classes
    PROP_BOULDER, PROP_OUTCROP, PROP_STONE, PROP_LOG, PROP_STUMP, PROP_ROOTS, PROP_DEBRIS, PROP_DRIFTWOOD, PROP_COAST, PROP_CLIFF, PROP_KINDS
};

// Size is chosen as a target footprint radius (m), so scans of any scale fit.
typedef struct { float spacing, r_lo, r_hi, sink, align, tilt; bool near; float lod_mul; } PropClass;
extern const PropClass PROP_CLASS[PROP_KINDS];

// key: unique per placement (tile, class and lattice cell; prop_key), shared
// by the drawn instance and its collider, so a moved dynamic prop is found.
typedef struct PropInstance { float x, y, z, scale; float q[4]; float moss, tint; uint16_t variant, kind; uint32_t key; } PropInstance;
// A dynamic prop drawn away from its scattered pose (the sim moved it).
typedef struct PropOverride { uint32_t key; float p[3], q[4]; } PropOverride;

// Variants in sorted file-name order (the scatter depends on this order).
typedef struct PropCatalog {
    int nvar;
    char name[PROP_MAX_VARIANTS][48];
    float radius[PROP_MAX_VARIANTS], height[PROP_MAX_VARIANTS];
    int kind[PROP_MAX_VARIANTS];                       // first class, -1 if none
    int kind_count[PROP_KINDS], kind_list[PROP_KINDS][PROP_MAX_VARIANTS];
    // Seating: the outermost points of the mesh's bottom band (the open
    // bottom's rim) in 16 sectors, from LOD0 (prop_catalog_set_base).
    float base[PROP_MAX_VARIANTS][PROP_BASE_PTS][3]; int nbase[PROP_MAX_VARIANTS];
} PropCatalog;

// Placement classes an asset name may be scattered as (returns the count).
int prop_classes_for(const char* name, int out[3]);
// Adds a variant (name without .prop) to the catalog; returns its index or -1.
int prop_catalog_add(PropCatalog* c, const char* name, float radius, float height);
// Reads every .prop header in dir (sorted) and each variant's LOD0 rim. False if none.
bool prop_catalog_load(PropCatalog* c, const char* dir);
// Positions (xyz, Y up, base at 0) and triangle indices of LOD `lod` of a
// .prop file; returns the vertex count (0 on failure; the caller frees).
int prop_read_lod(const char* path, int lod, float** pos, int32_t** idx, int* ni);
// Seating rim of variant v from its LOD0 positions (or the file's LOD0).
void prop_catalog_set_base(PropCatalog* c, int v, const float* pos, int nv);
bool prop_catalog_base_file(PropCatalog* c, int v, const char* path);
// Classes whose instances (of footprint radius r, m) are Box3D dynamic
// bodies: boulders, large stones, logs and driftwood (sim.c, dynprops.c).
bool prop_dynamic(int kind, float r);
// 0..1: how much (x, z) lies in a tor, a crowd of big outcrops on easy ground (a landmark).
float prop_tor_at(float x, float z);
// 0..1: how much (x, z) lies on the wall of a gorge (world.c, the same lines gorges_build cuts; a pure
// function of position, before the checks for water and low ground). `in`, if given: the level unit
// direction from the wall toward the gorge's floor (in[0], in[1]) and how far the floor's edge lies that way (in[2], m).
float cret_gorge_wall_at(float x, float z, float in[3]);
#define PROP_STONE_DYN_R 0.28f   // stones this big (footprint radius, m) get a dynamic collider; smaller ones are walked over
// Placement key of lattice cell `cell` of class `kind` in tile (tx, tz).
uint32_t prop_key(const TerraMap* m, int kind, int tx, int tz, int cell);
// Deterministic scatter of one class in tile (tx, tz); returns the count written.
int prop_scatter(const PropCatalog* c, const TerraMap* m, int kind, int tx, int tz, PropInstance* out, int cap);
// Canopy density (0..1) at a map cell for (x, z); used by props for shot picking.
float prop_canopy_at(const TerraMap* m, float x, float z);
#ifdef __cplusplus
}
#endif
#endif
