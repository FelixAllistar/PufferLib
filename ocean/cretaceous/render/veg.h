// Vegetation: procedural Cretaceous plants, scattered from the baked terra
// vegetation layers, drawn as instanced meshes near the camera and as
// in-engine-baked impostors far away. Meshes are generated from seeds; leaf,
// frond and bark textures are composited at startup from CC0 photo sprites in
// assets/foliage (tools/fetch_foliage.sh), with painted fallbacks.
#ifndef CRET_VEG_H
#define CRET_VEG_H
#include "gfx.h"
#include "csm.h"
#include "../terra/terra.h"

enum {   // plant kinds, matching TERRA_VEG_* layers
    VEG_CONIFER, VEG_BROADLEAF, VEG_CYCAD, VEG_FERN, VEG_TREEFERN, VEG_HORSETAIL, VEG_PALM, VEG_SHRUB, VEG_KINDS
};
#define VEG_VARIANTS 4
#define VEG_IMPOSTOR_VIEWS 8
#define VEG_TILE 64.0f

typedef struct VegInstance { float x, y, z, scale, yaw, hue, phase; uint16_t kind, variant; float cos_yaw, sin_yaw; } VegInstance;
typedef struct VegTile { VegInstance* trees; int ntrees; float ymin, ymax; } VegTile;

typedef struct VegMesh { GfxMesh m; float radius, height; int tris; } VegMesh;

typedef struct CretVeg {
    const TerraMap* map; float world_size;
    GfxProgram mesh_prog, imp_prog, shadow_prog, imp_shadow_prog;   // *_shadow: sun shadow-map casters
    GfxProgram depth_prog, imp_depth_prog;                           // view depth prepass
    VegMesh mesh[VEG_KINDS][VEG_VARIANTS];
    VegMesh mesh_lo[VEG_KINDS][VEG_VARIANTS];   // reduced mesh: ground cover past lod_dist, trees past tree_lod_dist
    float lod_dist, tree_lod_dist;
    GLuint t_atlas;            // foliage/bark texture array (sRGB, alpha)
    GLuint t_imp_albedo, t_imp_normal;  // impostor arrays: (kind,variant,view) layers
    int imp_layer[VEG_KINDS][VEG_VARIANTS];   // first layer, -1 if no impostor
    GLuint inst_vbo, quad_vao, quad_vbo, shadow_vbo[CSM_MAX];
    int tiles_n; VegTile* tiles;   // static scatter for tall plants
    // Ground cover regenerated around the camera.
    VegInstance* ground; int nground, ground_cap; int ground_cx, ground_cz; bool ground_valid;
    VegInstance* scratch; int scratch_cap;
    float near_range, far_range, ground_range;
    int drawn_meshes, drawn_impostors, drawn_ground;
    // Per-frame draw lists (cret_veg_prepare): kept between frames, so the
    // cull does not reallocate. Bucket q >= VEG_VARIANTS is the LOD mesh.
    struct VegList { void* a; int n, cap; } lists[VEG_KINDS][2 * VEG_VARIANTS], far_list;
    size_t list_off[VEG_KINDS][2 * VEG_VARIANTS], far_off; int prepared_frame; bool prepared;
    struct VegList shadow_lists[CSM_MAX][VEG_KINDS][2 * VEG_VARIANTS], shadow_cards[CSM_MAX];   // per cascade, kept
} CretVeg;

bool cret_veg_init(CretVeg* v, const TerraMap* map, float world_size);
// Draws into the bound HDR target with depth test on. Uniform setup for the
// atmosphere and terrain shadow textures is shared with the terrain pass.
typedef struct VegFrameDesc {
    const float* viewproj; const float* cam; const float* sun; float far_plane, time; int frame;
    GLuint t_trans, t_skyview, t_horizon0, t_horizon1, t_weather; float cloud_cover;
    GLuint t_misc;        // terra misc map (alpha: canopy density)
    int map_n; float map_cell;
    const CretCsm* csm;   // sun shadow maps (may be NULL or inactive)
    float depth_ab[2];    // scene depth mapping (u_depth in shaders/depth.glsl)
    float occ[4];         // third-person focus xyz and cone radius: plants in front of it dither out (radius 0: off)
    float overcast, wet;  // weather (view.c): grey sky light and haze; wet, darker, glossier surfaces
} VegFrameDesc;
// Frame order: cret_veg_prepare (cull and upload), optionally
// cret_veg_draw_depth (depth prepass; then draw with GEQUAL and depth writes
// off), then cret_veg_draw. cret_veg_draw prepares by itself if needed.
void cret_veg_prepare(CretVeg* v, const VegFrameDesc* f);
void cret_veg_draw_depth(CretVeg* v, const VegFrameDesc* f);
void cret_veg_draw(CretVeg* v, const VegFrameDesc* f);
// Renders plant casters into shadow cascade k (bound by cret_csm_begin_cascade).
// Near trees use the same mesh LOD as the view; the far cascades use one
// impostor card per tree; ground cover casts only into cascade 0.
int cret_veg_draw_shadow(CretVeg* v, const CretCsm* c, int k, const VegFrameDesc* f);
// Nearest scattered instance of `kind` (any kind, including ground cover) within radius.
bool cret_veg_find(const CretVeg* v, int kind, float x, float z, float radius, float out[3]);
void cret_veg_reload_shaders(CretVeg* v);
void cret_veg_close(CretVeg* v);
#endif
