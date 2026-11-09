// Props: scanned CC0 rocks, boulders, logs, stumps, roots and driftwood
// (Poly Haven, converted by tools/props_convert.py), scattered from the baked
// terra layers and drawn instanced with three distance LODs and dithered
// crossfades. Large props are scattered once per tile; small stones and debris
// are regenerated around the camera like ground cover. Large props also get
// Box3D colliders (world.c) from the same placement (props_place.c).
#ifndef CRET_PROPS_H
#define CRET_PROPS_H
#include "gfx.h"
#include "veg.h"            // VegFrameDesc: shared per-frame lighting inputs
#include "../terra/terra.h"

#include "props_place.h"   // placement classes and scatter, shared with the physics colliders

#define PROP_LODS 3

typedef struct PropVariant {
    GfxMesh lod[PROP_LODS]; int tris[PROP_LODS];
    GfxMesh cap;                  // a disc over the open bottom (its rim, PropCatalog.base), drawn under moved props
    float radius, height; int layer; int kind; char name[48];
} PropVariant;

typedef struct PropTile { PropInstance* items; int n; float ymin, ymax; } PropTile;

typedef struct CretProps {
    const TerraMap* map; float world_size;
    GfxProgram prog, shadow_prog;
    PropVariant var[PROP_MAX_VARIANTS]; int nvar;
    PropCatalog cat;              // names, sizes and classes of the variants (props_place.c)
    GLuint t_albedo, t_detail;    // arrays per material: albedo+ao, normal.xy+roughness
    int nmat; GLuint inst_vbo, shadow_vbo[CSM_MAX];
    int tiles_n; PropTile* tiles;
    PropInstance* near; int nnear, near_cap; int near_cx, near_cz; bool near_valid;
    float near_range;
    float lod_scale;   // LOD and cull distance scale (view preset; env CRET_PROP_LOD overrides)
    void* lists[1 + 4];   // per-frame draw lists (view, then shadow cascades): kept, so culling does not reallocate
    int drawn[PROP_LODS], drawn_tris;
    // Movable props the sim moved (cret_props_set_moved): drawn at their
    // pose instead of the scatter's. mv_in: the scattered instances moved
    // there; res: placements looked up so far (key -> instance).
    PropInstance* mv_in; int nmv, mv_cap; uint32_t* mv_set; int mv_set_n;
    PropInstance* res; int nres, res_cap;
} CretProps;

bool cret_props_init(CretProps* p, const TerraMap* map, float world_size);
void cret_props_draw(CretProps* p, const VegFrameDesc* f);
// Renders prop casters into shadow cascade k: all props in the near cascades,
// only large ones (boulders, outcrops, cliffs, logs) further out.
int cret_props_draw_shadow(CretProps* p, const CretCsm* c, int k, const VegFrameDesc* f);
void cret_props_reload_shaders(CretProps* p);
// The movable props away from their scattered pose this frame (render/view.c,
// from CretSim.dyn): each is drawn at its pose, with its bottom capped, and
// the scattered copy is skipped.
void cret_props_set_moved(CretProps* p, const PropOverride* o, int n);
void cret_props_close(CretProps* p);
// Nearest prop of a class to (x,z) within the static scatter; false if none.
bool cret_props_find(const CretProps* p, int kind, float x, float z, float out[4]);
#endif
