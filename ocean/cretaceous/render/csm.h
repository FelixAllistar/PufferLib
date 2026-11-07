// Cascaded sun shadow maps. Each cascade is a sphere fitted around a slice of
// the view frustum, so its size never changes as the camera turns; its centre
// is snapped to whole shadow texels in light space, so shadows do not shimmer
// as the camera moves. Casters (plants, props) render into one depth array;
// receivers sample it through shaders/csm.glsl. Terrain self-shadowing stays
// with the baked horizon map, which covers the whole island.
#ifndef CRET_CSM_H
#define CRET_CSM_H
#include "gfx.h"

#define CSM_MAX 4

typedef struct CretCsm {
    int requested, quality;      // asked for / running: 0 off, 1 low, 2 high (env CRET_SHADOWS)
    int count, res;
    float split[CSM_MAX + 1];    // view distances bounding each cascade
    float vp[CSM_MAX][16];       // light view-projection per cascade (column-major)
    float cx[CSM_MAX], cy[CSM_MAX], radius[CSM_MAX], znear[CSM_MAX], zfar[CSM_MAX];   // light-space box
    float texel[CSM_MAX];        // world size of one shadow texel
    float right[3], up[3], fwd[3];   // light basis; fwd points from the sun into the scene
    float view_planes[6][4];         // camera frustum (normalised): casters must shadow something visible
    GLuint tex, fbo[CSM_MAX], dummy;
    bool active;                 // rendered this frame (sun up, quality > 0)
    // CRET_SHADOW_DEBUG: GPU time per cascade and per frame.
    // Elapsed-time queries cannot nest, so even frames time the cascades and
    // odd frames the whole frame (do not combine with CRET_PROFILE/CRET_PROP_DEBUG).
    bool debug, frame_on; double cpu_ms; GLuint q[CSM_MAX + 1]; bool q_used[CSM_MAX]; double ms[CSM_MAX + 1]; int nms[2], casters[CSM_MAX], nframes;
} CretCsm;

bool cret_csm_init(CretCsm* c, int quality);
// Fits cascades for this frame. Returns false (and deactivates) when the sun is down or quality is 0.
bool cret_csm_setup(CretCsm* c, const float cam_pos[3], const float cam_fwd[3], float fov_y_deg, float aspect, const float sun[3], const float viewproj[16]);
// Light-space position of a world point: x right, y up, z depth away from the sun.
void cret_csm_to_light(const CretCsm* c, const float p[3], float out[3]);
// True if a caster (bounding sphere at world p with radius r, standing h above
// the ground) can shadow anything in cascade k.
bool cret_csm_caster_visible(const CretCsm* c, int k, const float p[3], float r, float h);
// True if a sphere lies wholly inside cascade k's box and the view frustum, so
// everything in it casts (tiles skip their per-instance tests).
bool cret_csm_caster_inside(const CretCsm* c, int k, const float p[3], float r);
// World-space bounds of cascade k's light box (for tile iteration).
void cret_csm_bounds(const CretCsm* c, int k, float lo[3], float hi[3]);
// Binds the depth array and cascade uniforms to the current receiver program on `unit`.
void cret_csm_bind(const CretCsm* c, const GfxProgram* p, int unit);
void cret_csm_begin_cascade(CretCsm* c, int k);   // binds layer k, clears depth, sets caster state
void cret_csm_end_cascade(CretCsm* c, int k);
void cret_csm_end(CretCsm* c);                     // restores state; caller rebinds its target/viewport
void cret_csm_frame_timer(CretCsm* c, bool begin); // whole-frame GPU timer (debug only)
void cret_csm_close(CretCsm* c);
#endif
