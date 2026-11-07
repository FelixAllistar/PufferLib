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
#define PROP_DIR "ocean/cretaceous/assets/props/"

enum {   // placement classes
    PROP_BOULDER, PROP_OUTCROP, PROP_STONE, PROP_LOG, PROP_STUMP, PROP_ROOTS, PROP_DEBRIS, PROP_DRIFTWOOD, PROP_COAST, PROP_CLIFF, PROP_KINDS
};

// Size is chosen as a target footprint radius (m), so scans of any scale fit.
typedef struct { float spacing, r_lo, r_hi, sink, align, tilt; bool near; float lod_mul; } PropClass;
extern const PropClass PROP_CLASS[PROP_KINDS];

typedef struct PropInstance { float x, y, z, scale; float q[4]; float moss, tint; uint16_t variant, kind; } PropInstance;

// Variants in sorted file-name order (the scatter depends on this order).
typedef struct PropCatalog {
    int nvar;
    char name[PROP_MAX_VARIANTS][48];
    float radius[PROP_MAX_VARIANTS], height[PROP_MAX_VARIANTS];
    int kind[PROP_MAX_VARIANTS];                       // first class, -1 if none
    int kind_count[PROP_KINDS], kind_list[PROP_KINDS][PROP_MAX_VARIANTS];
} PropCatalog;

// Placement classes an asset name may be scattered as (returns the count).
int prop_classes_for(const char* name, int out[3]);
// Adds a variant (name without .prop) to the catalog; returns its index or -1.
int prop_catalog_add(PropCatalog* c, const char* name, float radius, float height);
// Reads every .prop header in dir (sorted). False if none.
bool prop_catalog_load(PropCatalog* c, const char* dir);
// Deterministic scatter of one class in tile (tx, tz); returns the count written.
int prop_scatter(const PropCatalog* c, const TerraMap* m, int kind, int tx, int tz, PropInstance* out, int cap);
// Canopy density (0..1) at a map cell for (x, z); used by props for shot picking.
float prop_canopy_at(const TerraMap* m, float x, float z);
#ifdef __cplusplus
}
#endif
#endif
