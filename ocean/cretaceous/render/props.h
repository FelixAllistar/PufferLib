// Props: scanned CC0 rocks, boulders, logs, stumps, roots and driftwood
// (Poly Haven, converted by tools/props_convert.py), scattered from the baked
// terra layers and drawn instanced with three distance LODs and dithered
// crossfades. Large props are scattered once per tile; small stones and debris
// are regenerated around the camera like ground cover. Visual only for now:
// no physics colliders.
#ifndef CRET_PROPS_H
#define CRET_PROPS_H
#include "gfx.h"
#include "veg.h"            // VegFrameDesc: shared per-frame lighting inputs
#include "../terra/terra.h"

#define PROP_LODS 3
#define PROP_MAX_VARIANTS 64
#define PROP_TILE 64.0f

enum {   // placement classes
    PROP_BOULDER, PROP_OUTCROP, PROP_STONE, PROP_LOG, PROP_STUMP, PROP_ROOTS, PROP_DEBRIS, PROP_DRIFTWOOD, PROP_COAST, PROP_CLIFF, PROP_KINDS
};

typedef struct PropVariant {
    GfxMesh lod[PROP_LODS]; int tris[PROP_LODS];
    float radius, height; int layer; int kind; char name[48];
} PropVariant;

typedef struct PropInstance { float x, y, z, scale; float q[4]; float moss, tint; uint16_t variant, kind; } PropInstance;
typedef struct PropTile { PropInstance* items; int n; float ymin, ymax; } PropTile;

typedef struct CretProps {
    const TerraMap* map; float world_size;
    GfxProgram prog, shadow_prog;
    PropVariant var[PROP_MAX_VARIANTS]; int nvar;
    int kind_first[PROP_KINDS], kind_count[PROP_KINDS];   // variants per class (indices into kind_list)
    int kind_list[PROP_KINDS][PROP_MAX_VARIANTS];
    GLuint t_albedo, t_detail;    // arrays per material: albedo+ao, normal.xy+roughness
    int nmat; GLuint inst_vbo, shadow_vbo[CSM_MAX];
    int tiles_n; PropTile* tiles;
    PropInstance* near; int nnear, near_cap; int near_cx, near_cz; bool near_valid;
    float near_range;
    float lod_scale;   // LOD and cull distance scale (view preset; env CRET_PROP_LOD overrides)
    void* lists[1 + 4];   // per-frame draw lists (view, then shadow cascades): kept, so culling does not reallocate
    int drawn[PROP_LODS], drawn_tris;
} CretProps;

bool cret_props_init(CretProps* p, const TerraMap* map, float world_size);
void cret_props_draw(CretProps* p, const VegFrameDesc* f);
// Renders prop casters into shadow cascade k: all props in the near cascades,
// only large ones (boulders, outcrops, cliffs, logs) further out.
int cret_props_draw_shadow(CretProps* p, const CretCsm* c, int k, const VegFrameDesc* f);
void cret_props_reload_shaders(CretProps* p);
void cret_props_close(CretProps* p);
// Nearest prop of a class to (x,z) within the static scatter; false if none.
bool cret_props_find(const CretProps* p, int kind, float x, float z, float out[4]);
#endif
