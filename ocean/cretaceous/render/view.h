// Presentation: terrain (CDLOD), physically based sky, volumetric clouds,
// water, post. Reads the sim; never changes it.
#ifndef CRET_VIEW_H
#define CRET_VIEW_H
#include "raylib.h"
#include "gfx.h"
#include "../sim.h"
#include "veg.h"
#include "props.h"   // scanned rocks, logs, stumps (props.c)
#include "dino.h"    // skinned, animated dinosaurs (dino.c)
#include "nest.h"    // nests, eggs and carried food (nest.c)
#include "water_look.h"   // water look parameters (config/water.ini, F5 reloads)

#define CRET_LOD_LEVELS 9
#define CRET_PATCH_QUADS 32
#define CRET_LEAF_SIZE 32.0f

typedef struct CretCamera {
    float pos[3];
    float yaw, pitch;   // radians; yaw 0 looks along +x
    float fov;          // vertical degrees
} CretCamera;

// Quality presets: one switch for every knob in CretViewSettings below the
// preset field (env CRET_PRESET=low|medium|high|ultra; single knobs such as
// CRET_SHADOWS still override). High is the reference look and the one sized
// for a mid-range GPU (a GTX 1060 at 1080p); ultra goes further for stronger
// ones (longer plant and prop ranges, denser sward and grass, larger shadow
// maps, finer far terrain).
enum { CRET_PRESET_LOW, CRET_PRESET_MEDIUM, CRET_PRESET_HIGH, CRET_PRESET_ULTRA };
typedef struct CretViewSettings {
    bool clouds, water, wireframe, vegetation;
    float cloud_coverage;  // 0..1
    float exposure;        // EV bias
    float lod_scale;       // larger = finer terrain further out
    float cloud_quality;   // 0 = mobile (fewer march/light steps), 1 = desktop; env CRET_CLOUD_QUALITY
    int shadow_quality;    // sun shadow maps: 0 off, 1 low (3 x 1024^2), 2 high (4 x 2048^2), 3 ultra (4 x 3072^2); env CRET_SHADOWS
    int preset;            // CRET_PRESET_*, set with cret_view_set_preset
    float render_scale;    // 3D resolution relative to the window (bilinear upscale in post); env CRET_RENDER_SCALE
    float veg_range;       // scales plant mesh, ground-cover and LOD distances
    float veg_density;     // sward and tall-grass clumps per square metre, relative to high
    float prop_lod;        // scales prop LOD and cull distances
    int terrain_detail;    // 2: parallax relief + up to 4 materials; 1: no parallax; 0: also 2 materials, one triplanar scale
} CretViewSettings;

typedef struct CretNode { float x, z, size, ymin, ymax; int level, quads; } CretNode;

typedef struct CretView {
    const CretWorld* world;
    int width, height;
    CretViewSettings settings;
    int aniso_preset;   // the preset + 1 that texture anisotropy was last set for (view.c apply_settings)
    GfxProgram terrain, water, sky_lut, trans_lut, sky, clouds, cloud_resolve, post, object, bright, blur, lum, adapt, object_shadow, nest;
    GfxTarget lum_rt, adapt_rt[2]; int adapt_i; bool adapt_valid;   // auto-exposure
    GfxMesh patch, patch_half, tri, ocean_ring;
    GLuint t_height, t_normal, t_horizon0, t_horizon1, t_splat0, t_splat1, t_misc, t_water;
    GLuint t_biome;   // biomes at half resolution (build_terrain): R conifer forest, G dry open grassland
    GLuint t_mat_a, t_mat_b, t_noise, t_cloud_shape, t_cloud_detail, t_weather;
    GLuint t_canopy;   // half-res far canopy colour (RGB) and drawn crown cover (A) (view.c build_canopy)
    GLuint t_cover;    // half-res understorey density, its woody share, drainage wetness, curvature: far ground (view.c build_terrain)
    GfxTarget hdr, scene_copy, trans, skyview, cloud_rt, cloud_q, cloud_hist[2], bloom[2], ldr;
    int hist_index; bool hist_valid;   // cloud temporal accumulation
    float* minmax[CRET_LOD_LEVELS];   // per level: node grid of (min, max)
    int minmax_n[CRET_LOD_LEVELS];
    CretNode* nodes; int node_count, node_cap;
    // The selected nodes as instances of the patch meshes (node_instances):
    // full-size patches first, then half-size ones; the open-ocean tiles in a
    // buffer of their own, made once.
    GLuint node_vbo, ocean_vbo; int node_full, node_half, ocean_n;
    float view[16], proj[16], viewproj[16], inv_viewproj[16], prev_viewproj[16];
    float cam_pos[3], prev_cam[3], sun_dir[3], far_plane;
    // View basis (right, up, forward) and tan of the half field of view (x, y),
    // this frame and last: the cloud passes reproject by direction with these
    // (the 60 km inverse view-projection is not exact enough at pixel scale).
    float basis[9], prev_basis[9], tan_fov[2];
    float mist;   // 0..1: forest mist round the camera (conifer forest; post.fs), eased over a few seconds
    bool mist_set;   // the first frame takes the level at once
    float night, true_sun_y;   // 0 day .. 1 night (moon is the key light); the real sun's height
    float overcast, rain, wet, cover;   // weather, derived from cloud cover each frame (view.c)
    float depth_ab[2]; bool clip_control;   // reverse-Z scene depth (shaders/depth.glsl)
    int rw, rh;                             // 3D render size (window size x render_scale)
    float veg_base[4];                      // plant ranges at init: near, ground, ground LOD, tree LOD
    int frame;
    float time;
    WaterLook water_look;   // config/water.ini (CRET_WATER_INI), reread with the shaders
    Mesh capsule;     // raylib meshes drawn with our object shader
    CretVeg veg; bool veg_ok;
    CretProps props; bool props_ok;   // props.c
    CretDinos dinos; bool dinos_ok;   // dino.c: agents with a model draw as dinosaurs
    CretCsm csm;                      // sun shadow cascades (csm.c)
    CretNestDraw nests;               // nest.c: nests, eggs, carried food
    // Effects drawn into the HDR scene after the animals, before water
    // (render/fx.c: blood, decals, trails); depth test on, writes off.
    void (*fx_draw)(void* ctx, const struct CretView* v, const VegFrameDesc* fd); void* fx_ctx;
    bool initialized;
} CretView;

bool cret_view_init(CretView* v, const CretWorld* w, int width, int height);
void cret_view_resize(CretView* v, int width, int height);
// Applies a CRET_PRESET_* to v->settings (shadows, clouds, resolution, ranges, terrain detail).
void cret_view_set_preset(CretView* v, int preset);
// Renders a full frame into the default framebuffer (or the bound raylib target).
void cret_view_render(CretView* v, const CretSim* sim, const CretCamera* cam, float dt);
void cret_view_reload_shaders(CretView* v);
void cret_view_close(CretView* v);
// Sun direction for a time of day (0..1); y up. Exposed for HUD/tools.
void cret_sun_direction(float time_of_day, float out[3]);
#endif
