// The third-person camera rig: the boom from a pivot over the animal's
// shoulders, its collision with props, trunks, terrain and water, the pitch
// limits and the swing in behind a running animal. No raylib and no GL, so
// the player (cretaceous.c) and the headless camera probe (tools/camprobe.c)
// step the same code. Header-only (static functions), because the player's
// source list lives in the repository's build.sh.
#ifndef CRET_CAMERA_H
#define CRET_CAMERA_H
#include "sim.h"
#include <math.h>
#include <string.h>

// The pivot sits just behind the shoulders a little above the back (1.05 x
// height), the boom is 1.5 body lengths at zoom 1, so a raptor and a
// tyrannosaur fill the frame alike. Zoom 0.6x to 2.5x; the look pitch stays
// between 0.55 rad down and 0.45 up (never top-down).
#define CAM_BOOM_LENGTHS 1.5f
#define CAM_PIVOT_UP 1.05f
#define CAM_PIVOT_FWD 0.05f
#define CAM_PITCH_MIN -0.55f
#define CAM_PITCH_MAX 0.45f
#define CAM_ZOOM_MIN 0.6f
#define CAM_ZOOM_MAX 2.5f
#define CAM_RADIUS 0.3f      // the boom's sphere: the lens stays this far from rocks, logs and trunks
#define CAM_CLEAR 0.45f      // and this far above ground and water

// Tree trunks as the renderer places them (cret_veg_cast_trunks): distance
// along d from o before a sphere of radius r touches one (len if none).
typedef float (*CretCamTrunks)(const void* ctx, const float o[3], const float d[3], float len, float r);

// Rig state that persists between frames.
typedef struct CretCamRig {
    float zoom_s;                      // smoothed zoom
    float boom;                        // current boom length after collision (pulled in at once, eased out)
    float target[3]; bool target_ok;   // smoothed pivot; clear target_ok to cut to the animal
    float pitch_lo, pitch_hi;          // this frame's pitch limits
} CretCamRig;

// One frame of input to the rig.
typedef struct CretCamInput {
    float look_yaw, look_pitch;   // radians this frame (pitch up positive)
    float zoom;                   // the player's zoom (CAM_ZOOM_MIN..MAX)
    bool look;                    // play has the mouse or the touch drag
    bool swing;                   // drag scheme or touch while nothing turns the camera: swing in behind a running animal
    CretCamTrunks trunks; const void* trunks_ctx;   // optional
} CretCamInput;

static inline float cam_clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline float cam_wrap_pi(float a) { const float P = 3.14159265f; while (a > P) a -= 2 * P; while (a < -P) a += 2 * P; return a; }

static inline void cam_pivot(const CretAgent* a, float out[3]) {
    float L = CRET_SPECIES_TABLE[a->species].length * cret_growth_scale(a->growth);
    out[0] = a->x + cosf(a->yaw) * CAM_PIVOT_FWD * L; out[1] = a->y + a->height * CAM_PIVOT_UP; out[2] = a->z + sinf(a->yaw) * CAM_PIVOT_FWD * L;
}
static inline float cam_boom(const CretAgent* a, float zoom) {
    return fmaxf(CAM_BOOM_LENGTHS * CRET_SPECIES_TABLE[a->species].length * cret_growth_scale(a->growth) * zoom, 1.5f);
}

// Boom collision: a sphere swept out from the pivot stops at rocks, logs and
// cliff props (Box3D prop colliders) and at tree trunks; terrain and water
// lift the lens instead, unless a ridge hides the animal, then the boom
// shortens to it. Leaves and other animals never stop it, and nothing turns
// see-through.
typedef struct { float best; } CamCast;
static float cam_cast_fn(b3ShapeId shape, b3Pos point, b3Vec3 normal, float fraction, uint64_t mat, int tri, int child, void* ctx) {
    (void)shape; (void)point; (void)normal; (void)mat; (void)tri; (void)child;
    if (fraction <= 1e-4f) return -1;   // the pivot itself is inside this one: ignore it
    CamCast* c = ctx; if (fraction < c->best) c->best = fraction;
    return fraction;
}
static inline float cam_ground(const CretWorld* w, float x, float z) { return fmaxf(cret_height(w, x, z), cret_water(w, x, z)) + CAM_CLEAR; }
static bool cam_sees(const CretWorld* w, const float a[3], const float b[3]) {   // terrain between two points?
    for (int i = 1; i < 16; i++) {
        float t = i / 16.0f, x = a[0] + (b[0] - a[0]) * t, y = a[1] + (b[1] - a[1]) * t, z = a[2] + (b[2] - a[2]) * t;
        if (y < cam_ground(w, x, z) - CAM_CLEAR - 0.05f) return false;
    }
    return true;
}
static float cam_clear(const CretSim* s, const CretCamInput* in, const float p[3], const float d[3], float L) {
    float t = L;
    if (b3World_IsValid(s->phys)) {
        b3Vec3 pt = {0, 0, 0}; b3ShapeProxy proxy = {&pt, 1, CAM_RADIUS};
        b3QueryFilter f = {.categoryBits = CRET_CAT_AGENT, .maskBits = CRET_CAT_PROP, .id = 3, .name = "cret_camera"};
        CamCast cc = {1.0f};
        b3World_CastShape(s->phys, (b3Pos){p[0], p[1], p[2]}, &proxy, (b3Vec3){d[0] * L, d[1] * L, d[2] * L}, f, cam_cast_fn, &cc);
        t = fminf(t, cc.best * L);
    }
    if (in->trunks) t = fminf(t, in->trunks(in->trunks_ctx, p, d, t, CAM_RADIUS));
    // Terrain: the full boom lifted clear of the ground, if the animal stays in sight from there; else stop at the ridge.
    float e[3] = {p[0] + d[0] * t, p[1] + d[1] * t, p[2] + d[2] * t};
    e[1] = fmaxf(e[1], cam_ground(s->world, e[0], e[2]));
    if (!cam_sees(s->world, e, p)) {
        for (int i = 1; i <= 32; i++) {
            float u = t * i / 32.0f, x = p[0] + d[0] * u, y = p[1] + d[1] * u, z = p[2] + d[2] * u;
            if (y < cam_ground(s->world, x, z) - CAM_CLEAR) { t = fmaxf(u - t / 32.0f, 0.5f); break; }
        }
    }
    return t;
}

// One frame of the follow camera for animal a (the drawn, interpolated one)
// in sim s: applies the look input and the swing to the player's yaw and
// pitch, clamps the pitch and puts the lens at pos. The view looks along
// yaw and pitch.
static void cret_cam_follow(CretCamRig* r, const CretSim* s, const CretAgent* a, const CretCamInput* in, float* yaw, float* pitch, float dt, float pos[3]) {
    if (in->look) { *yaw = cam_wrap_pi(*yaw + in->look_yaw); *pitch += in->look_pitch; }
    // Drag scheme and touch: while the animal runs roughly away from the
    // camera and nothing is turning it, the camera swings in behind it
    // (strafing sideways leaves it alone, so A or D alone doesn't spin you in circles).
    if (in->look && in->swing) {
        float diff = cam_wrap_pi(a->yaw - *yaw), go = cam_clampf(a->speed / 3.0f, 0, 1);
        if (a->alive && a->speed > 0.5f && fabsf(diff) < 1.2f) *yaw = cam_wrap_pi(*yaw + diff * (1 - expf(-dt * 1.8f * go)));
    }
    r->pitch_lo = CAM_PITCH_MIN; r->pitch_hi = CAM_PITCH_MAX;
    *pitch = cam_clampf(*pitch, r->pitch_lo, r->pitch_hi);
    // The pivot follows the animal with a little lag (more vertically, to
    // smooth the gait); the camera looks along the control yaw and pitch.
    float want[3]; cam_pivot(a, want);
    if (r->zoom_s <= 0) r->zoom_s = in->zoom;
    r->zoom_s += (in->zoom - r->zoom_s) * (1 - expf(-dt * 10.0f));
    float L = cam_boom(a, r->zoom_s);
    if (!r->target_ok) { memcpy(r->target, want, sizeof want); r->target_ok = true; r->boom = L; }
    float k = 1 - expf(-dt * 12.0f), ky = 1 - expf(-dt * 6.0f);
    r->target[0] += (want[0] - r->target[0]) * k; r->target[2] += (want[2] - r->target[2]) * k; r->target[1] += (want[1] - r->target[1]) * ky;
    float d[3] = {-cosf(*pitch) * cosf(*yaw), -sinf(*pitch), -cosf(*pitch) * sinf(*yaw)};
    float clear = cam_clear(s, in, r->target, d, L);
    r->boom = clear < r->boom ? clear : r->boom + (clear - r->boom) * (1 - expf(-dt * 2.5f));   // in at once, out slowly
    float cx = r->target[0] + d[0] * r->boom, cy = r->target[1] + d[1] * r->boom, cz = r->target[2] + d[2] * r->boom;
    cy = fmaxf(cy, cam_ground(s->world, cx, cz));
    pos[0] = cx; pos[1] = cy; pos[2] = cz;
}
#endif
