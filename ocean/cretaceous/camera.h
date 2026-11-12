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
// tyrannosaur fill the frame alike. Zoom 0.6x to 2.5x; on flat ground the
// look pitch stays between 0.55 rad down and 0.45 up (never top-down);
// looking down or up a hill widens that (cam_pitch_range).
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
    float floor;                       // the lowest the lens may sit, sinking back slowly (hysteresis over bumpy ground)
    float pitch_lo, pitch_hi;          // this frame's pitch limits
    bool frame_limit;                  // pitch_hi holds the pitch where the lens sits on the ground behind and the pivot is CAM_FOCUS_EDGE below the centre
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

// Boom collision. A sphere swept out from the pivot stops at rocks, logs and
// cliff props (Box3D prop colliders) and at tree trunks. Terrain and water
// do not stop it: the lens rides up over them, lifted to stay clear, and the
// boom shortens only where the lifted lens would lose the animal (a ridge
// between them, the animal dropping out of the picture, or the lens inside
// the body). Leaves and other animals never stop it, and nothing turns
// see-through.
#define CAM_FOCUS_DROP 0.40f   // rad: how far below the centre of the view a lifted lens may put the pivot (the view is 0.52 to the edge)
#define CAM_FOCUS_EDGE 0.47f   // rad: past this the view stops pitching up, so the animal stays in the picture
#define CAM_BODY_GAP 0.30f     // m: the lens keeps this far out of the animal's body
#define CAM_BOOM_MIN 0.5f      // m: the shortest boom; closer than that the lens goes over the back
#define CAM_PITCH_FLOOR -1.25f // the steepest the view looks down (down a hill)
#define CAM_PITCH_CEIL 1.0f    // and up (up a hill)
#define CAM_STEPS 24           // boom lengths tried, longest first, before refining

typedef struct { float best; } CamCast;
static float cam_cast_fn(b3ShapeId shape, b3Pos point, b3Vec3 normal, float fraction, uint64_t mat, int tri, int child, void* ctx) {
    (void)shape; (void)point; (void)normal; (void)mat; (void)tri; (void)child;
    if (fraction <= 1e-4f) return -1;   // the pivot itself is inside this one: ignore it
    CamCast* c = ctx; if (fraction < c->best) c->best = fraction;
    return fraction;
}

// Water as drawn: the level blended between cell centres (cret_water is the
// nearest cell's, which steps along steep creeks), a dry cell taking its
// wettest neighbour's level as the renderer extends the sheet under banks.
static float cam_water(const CretWorld* w, float x, float z) {
    float c = w->map.cell, gx = x / c, gz = z / c; int ix = (int)floorf(gx), iz = (int)floorf(gz);
    float fx = gx - ix, fz = gz - iz, v[4], top = TERRA_DRY;
    for (int k = 0; k < 4; k++) { v[k] = cret_water(w, (ix + (k & 1)) * c, (iz + (k >> 1)) * c); top = fmaxf(top, v[k]); }
    if (top <= TERRA_DRY) return TERRA_DRY;
    for (int k = 0; k < 4; k++) if (v[k] <= TERRA_DRY) v[k] = top;
    return (v[0] * (1 - fx) + v[1] * fx) * (1 - fz) + (v[2] * (1 - fx) + v[3] * fx) * fz;
}
// The lowest the lens may sit at (x, z): CAM_CLEAR over the highest ground
// within the near plane's reach (on a slope that keeps it clear square to
// the ground too) and over the water.
static float cam_floor(const CretWorld* w, float x, float z) {
    const float r = 0.4f;
    float g = fmaxf(cret_height(w, x, z), fmaxf(fmaxf(cret_height(w, x + r, z), cret_height(w, x - r, z)), fmaxf(cret_height(w, x, z + r), cret_height(w, x, z - r))));
    return fmaxf(g, cam_water(w, x, z)) + CAM_CLEAR;
}
// True if the terrain leaves a line of sight between a and b with `margin`
// metres to spare. Water does not block it (the pivot of a swimming animal
// is above the surface, and the lens never goes under).
static bool cam_sees(const CretWorld* w, const float a[3], const float b[3], float margin) {
    for (int i = 1; i < 16; i++) {
        float t = i / 16.0f, x = a[0] + (b[0] - a[0]) * t, y = a[1] + (b[1] - a[1]) * t, z = a[2] + (b[2] - a[2]) * t;
        if (y < cret_height(w, x, z) - 0.05f + margin) return false;
    }
    return true;
}
// The animal's body as a capsule along its heading (centre line at 0.6 of
// its height); returns the distance from p to its surface and, in up, how
// far p must rise to be CAM_BODY_GAP out of it (0 if it already is).
static float cam_body(const CretAgent* a, const float p[3], float* up) {
    float L = CRET_SPECIES_TABLE[a->species].length * cret_growth_scale(a->growth), R = fmaxf(a->radius, 0.4f * a->height);
    float hl = fmaxf(0.5f * L - R, 0), cx = cosf(a->yaw), cz = sinf(a->yaw), cy = a->y + 0.6f * a->height;
    float u = cam_clampf((p[0] - a->x) * cx + (p[2] - a->z) * cz, -hl, hl);
    float dx = p[0] - (a->x + cx * u), dy = p[1] - cy, dz = p[2] - (a->z + cz * u), need = R + CAM_BODY_GAP;
    float h2 = dx * dx + dz * dz;
    if (up) *up = h2 < need * need ? fmaxf(cy + sqrtf(need * need - h2) - p[1], 0) : 0;
    return sqrtf(h2 + dy * dy) - R;
}
// Pitch limits for the ground ahead along the view: where it falls away the
// view may look further down (down a hill), where it rises further up, by
// 0.9 of the slope beyond the first 2.3 degrees, so flat ground keeps the
// flat limits.
static void cam_pitch_range(const CretWorld* w, const CretAgent* a, float yaw, float L, float* lo, float* hi) {
    float h0 = cret_height(w, a->x, a->z), fall = 0, rise = 0;
    for (int k = 0; k < 3; k++) {
        float d = L * (0.5f + 0.75f * k), dh = cret_height(w, a->x + cosf(yaw) * d, a->z + sinf(yaw) * d) - h0;
        fall = fmaxf(fall, atan2f(-dh, d)); rise = fmaxf(rise, atan2f(dh, d));
    }
    *lo = fmaxf(CAM_PITCH_MIN - 0.9f * fmaxf(fall - 0.04f, 0), CAM_PITCH_FLOOR);
    *hi = fminf(CAM_PITCH_MAX + 0.9f * fmaxf(rise - 0.04f, 0), CAM_PITCH_CEIL);
}
// How far a sphere of radius r reaches from p along d (unit) before a prop
// or trunk, up to L.
static float cam_props(const CretSim* s, const CretCamInput* in, const float p[3], const float d[3], float L, float r) {
    float t = L;
    if (b3World_IsValid(s->phys)) {
        b3Vec3 pt = {0, 0, 0}; b3ShapeProxy proxy = {&pt, 1, r};
        b3QueryFilter f = {.categoryBits = CRET_CAT_AGENT, .maskBits = CRET_CAT_PROP, .id = 3, .name = "cret_camera"};
        CamCast cc = {1.0f};
        b3World_CastShape(s->phys, (b3Pos){p[0], p[1], p[2]}, &proxy, (b3Vec3){d[0] * L, d[1] * L, d[2] * L}, f, cam_cast_fn, &cc);
        t = fminf(t, cc.best * L);
    }
    if (in->trunks) t = fminf(t, in->trunks(in->trunks_ctx, p, d, t, r));
    return t;
}
// Whether a boom of length t from pivot p along d keeps the animal once its
// lens is lifted clear of the ground: out of the body, a line of sight to
// the pivot past the terrain and (once lifted off the boom's swept line)
// past props, and (if frame) the pivot no more than CAM_FOCUS_DROP below
// the centre of the view; drop (optional) gets how far below it is. A
// length the boom would grow to must pass with margins (0.25 m of sight,
// 0.05 rad of view), so it does not flutter at the edge over bumpy ground.
static bool cam_keeps(const CretSim* s, const CretCamInput* in, const CretAgent* a, const float p[3], const float d[3], float pitch, float t, bool grow, bool frame, float* drop) {
    const CretWorld* w = s->world;
    float e[3] = {p[0] + d[0] * t, p[1] + d[1] * t, p[2] + d[2] * t};
    float lift = fmaxf(cam_floor(w, e[0], e[2]) - e[1], 0); e[1] += lift;
    float dr = pitch - atan2f(t * sinf(pitch) - lift, t * cosf(pitch));   // the lift tips the pivot down the view
    if (drop) *drop = dr;
    if (cam_body(a, e, NULL) < CAM_BODY_GAP) return false;
    if (!cam_sees(w, e, p, grow ? 0.25f : 0)) return false;
    if (frame && dr >= CAM_FOCUS_DROP - (grow ? 0.05f : 0)) return false;
    if (lift > 0.01f) {
        float v[3] = {e[0] - p[0], e[1] - p[1], e[2] - p[2]}, n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (n > 1e-3f) { v[0] /= n; v[1] /= n; v[2] /= n; if (cam_props(s, in, p, v, n, 0.1f) < n - 0.05f) return false; }
    }
    return true;
}
// The longest boom up to tmax that keeps the animal (cam_keeps): returns
// the longest out of the body and in sight (the shortest boom if none is)
// and puts in *framed the longest that also keeps the pivot in the picture
// (-1 if none does: steep ground right behind the lens).
static float cam_reach(const CretCamRig* r, const CretSim* s, const CretCamInput* in, const CretAgent* a, const float p[3], const float d[3], float pitch, float tmax, float* framed) {
    float len[2] = {-1, -1};
    for (int frame = 0; frame < 2; frame++) {
        float ok = -1, bad = tmax, top = frame ? len[0] : tmax;
        for (int i = 0; i <= CAM_STEPS && top > 0; i++) {
            float t = top * (1 - i / (float)CAM_STEPS);
            if (t < CAM_BOOM_MIN) break;
            if (cam_keeps(s, in, a, p, d, pitch, t, t > r->boom + 0.01f, frame, NULL)) { ok = t; break; }
            bad = t;
        }
        for (int k = 0; k < 5 && ok > 0 && ok < top; k++) {   // refine between the last length that failed and the first that kept it
            float m = 0.5f * (ok + bad);
            if (cam_keeps(s, in, a, p, d, pitch, m, m > r->boom + 0.01f, frame, NULL)) ok = m; else bad = m;
        }
        len[frame] = ok;
    }
    *framed = len[1];
    return len[0] > 0 ? len[0] : fminf(CAM_BOOM_MIN, tmax);
}

// The lens for a view along yaw and pitch: the boom as long as props,
// trunks and the ground allow (cam_reach), then lifted clear of the ground
// and out of the body. A boom that would pass into a prop, behind a ridge or
// into the body is pulled in at once; one that only shortens to keep the
// pivot in the picture eases in, no faster than the turn of the view (turn,
// radians this frame) sweeps the lens plus 3 m/s more than the animal runs;
// both ease out, at most 1.5 full booms a second. Returns how far below the
// centre of the view the animal's pivot (focus, unsmoothed) lies if the
// ground lifted the lens (otherwise 0); the new boom and floor go to boom
// and floor.
static float cam_place(const CretCamRig* r, const CretSim* s, const CretAgent* a, const CretCamInput* in, float yaw, float pitch, float turn, float L, float dt, bool cut,
                       const float focus[3], float e[3], float* boom, float* floor) {
    const CretWorld* w = s->world; const float* p = r->target;
    float d[3] = {-cosf(pitch) * cosf(yaw), -sinf(pitch), -cosf(pitch) * sinf(yaw)}, framed;
    float hard = cam_reach(r, s, in, a, p, d, pitch, cam_props(s, in, p, d, L, CAM_RADIUS), &framed);
    float want = framed > 0 ? framed : hard, b = fminf(r->boom, hard);
    if (want < b) b = fmaxf(b + (want - b) * (1 - expf(-dt * 8.0f)), b - (3.0f + a->speed) * dt - turn * b);
    else b += fminf((want - b) * (1 - expf(-dt * 2.5f)), 1.5f * L * dt);
    *boom = cut ? want : b;
    for (int k = 0; k < 3; k++) e[k] = p[k] + d[k] * *boom;
    float y0 = e[1];
    // The lens rides over the ground: it rises at once where the ground
    // does and sinks back over a sixth of a second, so it does not dip into
    // every hollow of bumpy ground; the slow sinking never holds it so high
    // that the pivot drops more than CAM_FOCUS_DROP below the centre of the view.
    float f = cam_floor(w, e[0], e[2]), hd = hypotf(e[0] - p[0], e[2] - p[2]);
    *floor = cut || f > r->floor ? f : r->floor + (f - r->floor) * (1 - expf(-dt * 6.0f));
    float cap = fmaxf(f, p[1] - hd * tanf(pitch - CAM_FOCUS_DROP));
    e[1] = fmaxf(e[1], fminf(*floor, cap));
    // Never inside the animal: a boom too short for the body goes over its back.
    float up; cam_body(a, e, &up); e[1] += up;
    if (e[1] <= y0 + 0.01f || up > 0) return 0;
    return pitch - atan2f(focus[1] - e[1], fmaxf(hypotf(e[0] - focus[0], e[2] - focus[2]), 1e-3f));
}

// One frame of the follow camera for animal a (the drawn, interpolated one)
// in sim s: applies the look input and the swing to the player's yaw and
// pitch, keeps the pitch within the limits for the ground ahead and puts the
// lens at pos. The view looks along yaw and pitch.
static void cret_cam_follow(CretCamRig* r, const CretSim* s, const CretAgent* a, const CretCamInput* in, float* yaw, float* pitch, float dt, float pos[3]) {
    const CretWorld* w = s->world;
    float p0 = *pitch, y0 = *yaw;
    if (in->look) { *yaw = cam_wrap_pi(*yaw + in->look_yaw); *pitch += in->look_pitch; }
    // Drag scheme and touch: while the animal runs roughly away from the
    // camera and nothing is turning it, the camera swings in behind it
    // (strafing sideways leaves it alone, so A or D alone doesn't spin you in circles).
    if (in->look && in->swing) {
        float diff = cam_wrap_pi(a->yaw - *yaw), go = cam_clampf(a->speed / 3.0f, 0, 1);
        if (a->alive && a->speed > 0.5f && fabsf(diff) < 1.2f) *yaw = cam_wrap_pi(*yaw + diff * (1 - expf(-dt * 1.8f * go)));
    }
    // The pivot follows the animal with a little lag (more vertically, to
    // smooth the gait), but never so far behind (a tenth of the boom, at
    // most half the animal's height) that a fall or a run down a steep
    // slope takes the animal out of the picture.
    float want[3]; cam_pivot(a, want);
    if (r->zoom_s <= 0) r->zoom_s = in->zoom;
    r->zoom_s += (in->zoom - r->zoom_s) * (1 - expf(-dt * 10.0f));
    float L = cam_boom(a, r->zoom_s);
    bool cut = !r->target_ok;
    if (cut) { memcpy(r->target, want, sizeof want); r->target_ok = true; r->boom = L; }
    float k = 1 - expf(-dt * 12.0f), ky = 1 - expf(-dt * 6.0f);
    r->target[0] += (want[0] - r->target[0]) * k; r->target[2] += (want[2] - r->target[2]) * k; r->target[1] += (want[1] - r->target[1]) * ky;
    float lag = fminf(0.1f * L, 0.5f * a->height + 0.2f);
    r->target[1] = cam_clampf(r->target[1], want[1] - lag, want[1] + lag);
    // Pitch limits for the ground ahead. Input never pushes past a limit; a
    // limit the pitch is already past (the view turned away from the hill)
    // eases it back.
    float lo, hi; cam_pitch_range(w, a, *yaw, L, &lo, &hi);
    float ease = 1 - expf(-dt * 6.0f);
    if (*pitch < lo) { float q = fmaxf(*pitch, p0); *pitch = p0 < lo ? q + (lo - q) * ease : lo; }
    if (*pitch > hi) { float q = fminf(*pitch, p0); *pitch = p0 > hi ? q + (hi - q) * ease : hi; }
    float e[3], boom, floor, turn = fabsf(cam_wrap_pi(*yaw - y0)) + fabsf(*pitch - p0);
    float drop = cam_place(r, s, a, in, *yaw, *pitch, turn, L, dt, cut, want, e, &boom, &floor);
    // A lens sitting on the ground behind cannot drop any further, so past
    // CAM_FOCUS_EDGE looking up would only lose the animal off the bottom of
    // the picture: the pitch stops there (and comes down to it if the ground
    // lifts the lens further).
    r->frame_limit = drop > CAM_FOCUS_EDGE;
    if (r->frame_limit) {
        *pitch = fmaxf(*pitch - (drop - CAM_FOCUS_EDGE), lo); hi = *pitch;
        cam_place(r, s, a, in, *yaw, *pitch, turn, L, dt, cut, want, e, &boom, &floor);
    }
    r->pitch_lo = lo; r->pitch_hi = hi; r->boom = boom; r->floor = floor;
    if (!isfinite(e[0]) || !isfinite(e[1]) || !isfinite(e[2])) { r->target_ok = false; memcpy(e, want, sizeof e); }
    memcpy(pos, e, sizeof e);
}
#endif
