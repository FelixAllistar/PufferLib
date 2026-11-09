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

// Plant kinds, matching TERRA_VEG_* layers. VEG_CARPET (low sward tufts)
// follows the splat, VEG_GRASS (tall grass, sedge and reeds) the splat,
// canopy, moisture and a clump noise; neither is a veg layer.
enum {
    VEG_CONIFER, VEG_BROADLEAF, VEG_CYCAD, VEG_FERN, VEG_TREEFERN, VEG_HORSETAIL, VEG_PALM, VEG_SHRUB, VEG_CARPET, VEG_GRASS, VEG_KINDS
};
// Variants per kind. For the tall kinds, variant & 3 is the species (e.g.
// conifer 0 Norfolk-like araucaria, 1 podocarp, 2 umbrella araucaria, 3
// metasequoia) and variant >> 2 a second seed of it. Tall grass and the sward
// use variants 0-3 only.
#define VEG_VARIANTS 8
#define VEG_IMPOSTOR_VIEWS 8
#define VEG_TILE 64.0f

typedef struct VegInstance { float x, y, z, scale, yaw, hue, phase; uint16_t kind, variant; float cos_yaw, sin_yaw; } VegInstance;
typedef struct VegTile { VegInstance* trees; int ntrees; float ymin, ymax; } VegTile;

// shadow_count: the leading indices that cast sun shadows (twigs follow, view only).
typedef struct VegMesh { GfxMesh m; float radius, height; int tris, shadow_count; } VegMesh;

typedef struct CretVeg {
    const TerraMap* map; float world_size;
    GfxProgram mesh_prog, imp_prog, shadow_prog, imp_shadow_prog;   // *_shadow: sun shadow-map casters
    GfxProgram depth_prog, imp_depth_prog;                           // view depth prepass
    VegMesh mesh[VEG_KINDS][VEG_VARIANTS];
    VegMesh mesh_lo[VEG_KINDS][VEG_VARIANTS];   // reduced mesh: ground cover past lod_dist, trees past tree_lod_dist
    float lod_dist, tree_lod_dist;              // (the smaller tall kinds switch earlier: veg.c TREE_LOD)
    GLuint t_atlas;            // foliage/bark texture array (sRGB, alpha)
    GLuint t_imp_albedo, t_imp_normal;  // impostor arrays: (kind,variant,view) layers
    int imp_layer[VEG_KINDS][VEG_VARIANTS];   // first layer, -1 if no impostor
    GLuint inst_vbo, quad_vao, quad_vbo, shadow_vbo[CSM_MAX];
    int tiles_n; VegTile* tiles;   // static scatter for tall plants
    // Ground cover regenerated around the camera.
    VegInstance* ground; int nground, ground_cap; int ground_cx, ground_cz; bool ground_valid;
    VegInstance* scratch; int scratch_cap;
    // Dense low sward close to the camera (0.5 m lattice, density from the fern/moss splat).
    VegInstance* carpet; int ncarpet, carpet_cap, carpet_cx, carpet_cz; bool carpet_valid; float carpet_range;
    // Tall grass, sedge and reeds (variants 0 meadow, 1 tall, 2 sedge, 3 reeds),
    // regenerated around the camera; drawn out to grass_frac x ground_range.
    VegInstance* grass; int ngrass, grass_cap, grass_cx, grass_cz; bool grass_valid, grass_on; float grass_frac, grass_scatter_r;
    // Trample map (render/trample.h): animals bend the ground cover, which springs back.
    struct CretTrample* trample;
    float near_range, far_range, ground_range;
    int drawn_meshes, drawn_impostors, drawn_ground;
    // Per-frame draw lists (cret_veg_prepare): kept between frames, so the
    // cull does not reallocate. Bucket q >= VEG_VARIANTS is the LOD mesh.
    struct VegList { void* a; int n, cap; } lists[VEG_KINDS][2 * VEG_VARIANTS], far_list;
    size_t list_off[VEG_KINDS][2 * VEG_VARIANTS], far_off; int prepared_frame; bool prepared;
    struct VegList shadow_lists[CSM_MAX][VEG_KINDS][2 * VEG_VARIANTS], shadow_cards[CSM_MAX];   // per cascade, kept
    // Review lineup (cret_veg_lineup): ground cover planted in rows; scattered
    // cover and tall grass are cleared within lineup_r of lineup_c.
    VegInstance* lineup; int nlineup; float lineup_c[2], lineup_r;
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
// Tall-grass density (0..~1) at a point and its wetness (0 dry .. 1 bank),
// before the per-clump dice; capture paths use it to find meadows and banks.
float cret_veg_grass_density(const CretVeg* v, float x, float z, float* wet);
// How far a sphere of radius r swept from o along the unit direction d gets
// before touching a tree trunk (len if none within len). The third-person
// camera boom stops there (cretaceous.c); leaves do not stop it.
float cret_veg_cast_trunks(const CretVeg* v, const float o[3], const float d[3], float len, float r);
// Trunk radius at the base (m) of a plant of this kind, variant and instance
// scale: 0 for kinds without a trunk. Pure CPU (no GL, no CretVeg): the
// generator derives it from the same seeds as the mesh.
float cret_veg_trunk_radius(int kind, int variant, float scale);
// Review lineup (capture): clears every plant within clear_r of (cx, cz) and
// plants each kind x variant of the tall kinds, ferns, horsetails and shrubs
// in one line from (x0, z0) along +x, kind after kind (so each row has a clear
// view from -z).
// out[] receives the placed plants (at most cap); returns how many.
int cret_veg_lineup(CretVeg* v, float x0, float z0, float cx, float cz, float clear_r, VegInstance* out, int cap);
void cret_veg_reload_shaders(CretVeg* v);
void cret_veg_close(CretVeg* v);
#endif
