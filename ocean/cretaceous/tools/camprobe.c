// Camera probe: steps the player's third-person camera (camera.h) headless
// on hard ground and counts broken invariants. Each species at growth 0.35
// (the youngest the sim allows) and 1.0 stands on sampled spots: flat
// ground, 15, 30 and 45 degree slopes facing up, down and across, ridge
// tops, gully bottoms, cliff feet and tops, shorelines and shallow water,
// under trees, next to boulders and logs and among dense props. A scripted
// player sweeps the camera's yaw through 360 degrees and its pitch through
// its whole range, flicks it, and walks and runs while turning; the sim runs
// at its fixed 30 Hz and the camera at 15, 30 and 60 frames per second on
// the interpolated animal, as in the player's loop.
//
// Invariants, checked every frame (after the first 0.3 s of a run):
//   ground  the lens is at least 0.25 m from the terrain, measured square to
//           the slope (the near plane is 0.3 m away);
//   water   the lens is at least 0.25 m above the water surface as drawn;
//   los     the terrain and props leave a line of sight from the lens to the
//           camera's focus point (the pivot over the shoulders, smoothed as
//           the camera follows it), unless the boom is at its minimum (0.75 m, or the lens within 0.5 m of the
//           body: over its back or shoulder);
//   pop     the lens moved no faster than the animal and the look input
//           explain, plus 4 m/s and 2 boom lengths a second of collision give
//           and take;
//   cut     (counted, not a failure) a jump of the boom in past a prop or
//           trunk that came closer, or because the terrain (within 5 cm) or
//           a prop hid the pivot from where the lens was, or from where the
//           old boom would put it after the turn of the view: it cannot pass
//           through either;
//   pitch   the view pitch is the pitch the player asked for (held for 0.5 s),
//           and a limit the player pushes against is no tighter than a range
//           that widens with the ground ahead (downhill: the flat limit plus
//           0.8 x the fall beyond 3 degrees, to at most 1.2 rad down; uphill:
//           the rise, to 0.95 up). The one known reason for a tighter upper
//           limit: the lens sits on the ground or water behind (within 0.8 m) and the
//           pivot is already 0.38 rad below the centre of the view;
//   stuck   pitch input held one way for 0.25 s moved neither the view nor
//           the lens while that range allowed further motion (and the known
//           reason above did not hold);
//   short   the boom is under 0.9 of its length although nothing (props,
//           trunks, ground with 0.3 m to spare) has been in its way for 1 s;
//   osc     the lens bobbed while the look input was still: its height over
//           the pivot or its distance from it turned back 5 times within 1 s,
//           each swing at least 5 cm (not counting 0.5 s after a cut);
//   frame   the focus point is off screen (16:9, 60 degree vertical view),
//           unless the boom is at its minimum as above;
//   inside  the lens is inside the animal's body (a capsule along it);
//   nan     anything not finite.
// Counts are frames per invariant and per terrain kind.
//
//   make -C ocean/cretaceous camprobe && ./build/cretaceous/camprobe [options]
//   --quick          a subset: the test world, 9 spots, raptor and tyrannosaur, 15 and 60 fps
//   --check          exit 1 if any invariant broke (make test runs --quick --check)
//   --spots N        spots per kind and facing (default 2)
//   --trace          the owner's case: an adult raptor on a 30 degree slope, the camera
//                    uphill or downhill, pitched up and down; prints what the camera does
//   --paths-out F    write the flat-ground lens paths; --paths-ref F compares against them
//   --verbose        one line per run with any violation
// Run from the repository root (the world cache and the props are relative to it).
#include "../camera.h"
#include "../render/veg_place.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------- terrain kinds
enum { K_FLAT, K_SLOPE15, K_SLOPE30, K_SLOPE45, K_RIDGE, K_GULLY, K_CLIFF_FOOT, K_CLIFF_TOP, K_SHORE, K_SHALLOW, K_TREES, K_BOULDER, K_LOG, K_DENSE, K_KINDS };
static const char* KIND_NAME[K_KINDS] = {"flat", "slope15", "slope30", "slope45", "ridge", "gully", "cliff_foot", "cliff_top", "shore", "shallow", "trees", "boulder", "log", "dense"};
enum { V_GROUND, V_WATER, V_LOS, V_POP, V_CUT, V_PITCH, V_STUCK, V_SHORT, V_OSC, V_FRAME, V_INSIDE, V_NAN, V_KINDS };
static const char* VIOL_NAME[V_KINDS] = {"ground", "water", "los", "pop", "cut", "pitch", "stuck", "short", "osc", "frame", "inside", "nan"};

typedef struct { int kind; float x, z, yaw; char facing[12]; } Spot;

static uint32_t rnd(uint32_t* s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x ? x : 0x9e3779b9u; }
static float frnd(uint32_t* s) { return (rnd(s) >> 8) * (1.0f / 16777216.0f); }

static float H(const CretWorld* w, float x, float z) { return cret_height(w, x, z); }
static bool wet(const CretWorld* w, float x, float z) { return cret_water(w, x, z) > H(w, x, z); }
// Slope angle (radians) and the downhill heading at a point.
static float slope_at(const CretWorld* w, float x, float z, float* down) {
    float gx = (H(w, x + 1, z) - H(w, x - 1, z)) * 0.5f, gz = (H(w, x, z + 1) - H(w, x, z - 1)) * 0.5f;
    if (down) *down = atan2f(-gz, -gx);
    return atanf(hypotf(gx, gz));
}
static bool dry_around(const CretWorld* w, float x, float z, float r) {
    if (wet(w, x, z)) return false;
    for (int k = 0; k < 8; k++) { float a = k * 0.785398f; if (wet(w, x + cosf(a) * r, z + sinf(a) * r)) return false; }
    return true;
}
// A plane over a few metres: the slope and its heading barely change.
static bool even_slope(const CretWorld* w, float x, float z, float ang, float down) {
    for (int k = 0; k < 8; k++) {
        float a = k * 0.785398f, d2; float s = slope_at(w, x + cosf(a) * 4, z + sinf(a) * 4, &d2);
        if (fabsf(s - ang) > 0.12f || fabsf(cam_wrap_pi(d2 - down)) > 0.45f) return false;
    }
    return true;
}

// Classifies a point as one kind (or -1); yaw0 is the reference heading the facings turn from.
static int classify(const CretWorld* w, float x, float z, float* yaw0) {
    float S = w->size; if (x < 60 || z < 60 || x > S - 60 || z > S - 60) return -1;
    float h0 = H(w, x, z), down; float s = slope_at(w, x, z, &down);
    float wl = cret_water(w, x, z);
    if (wl > h0 + 0.3f && wl < h0 + 1.2f) { *yaw0 = down; return K_SHALLOW; }
    if (!wet(w, x, z) && wl < h0 + 0.05f) {   // dry: a shore if deep water is close
        for (int k = 0; k < 8; k++) {
            float a = k * 0.785398f, px = x + cosf(a) * 6, pz = z + sinf(a) * 6;
            if (cret_water(w, px, pz) > H(w, px, pz) + 0.8f && s < 0.35f) { *yaw0 = a; return K_SHORE; }
        }
    }
    if (!dry_around(w, x, z, 8)) return -1;
    const float deg = 0.0174533f;
    if (s < 3 * deg) {
        bool flat = true;
        for (int k = 0; k < 16; k++) { float a = k * 0.392699f, r = k & 1 ? 20.0f : 8.0f; if (fabsf(H(w, x + cosf(a) * r, z + sinf(a) * r) - h0) > (k & 1 ? 1.0f : 0.4f)) flat = false; }
        if (flat) { *yaw0 = down; return K_FLAT; }
    }
    static const float SL[3] = {15, 30, 45};
    for (int k = 0; k < 3; k++) if (fabsf(s - SL[k] * deg) < 3 * deg && even_slope(w, x, z, s, down)) { *yaw0 = down; return K_SLOPE15 + k; }
    if (s < 25 * deg) {
        float drop = 0, rise = 0, ad = 0, ar = 0;
        for (int k = 0; k < 16; k++) {
            float a = k * 0.392699f, dh = H(w, x + cosf(a) * 6, z + sinf(a) * 6) - h0;
            if (-dh > drop) { drop = -dh; ad = a; }
            if (dh > rise) { rise = dh; ar = a; }
        }
        if (drop > 5 && rise < 1) { *yaw0 = ad; return K_CLIFF_TOP; }
        if (rise > 5 && drop < 1) { *yaw0 = ar; return K_CLIFF_FOOT; }
    }
    if (s < 15 * deg) {
        for (int k = 0; k < 8; k++) {
            float a = k * 0.392699f, c = cosf(a) * 8, e = sinf(a) * 8;
            float h1 = H(w, x + c, z + e) - h0, h2 = H(w, x - c, z - e) - h0;
            if (h1 < -2 && h2 < -2) { *yaw0 = a; return K_RIDGE; }   // falls away both ways across the crest
            if (h1 > 2 && h2 > 2) { *yaw0 = a + 1.5708f; return K_GULLY; }   // facing along the gully
        }
    }
    return -1;
}

// Facings per kind: the heading of the animal relative to the kind's reference.
typedef struct { const char* name; float off; } Facing;
static int facings(int kind, Facing* f) {
    switch (kind) {
    case K_SLOPE15: case K_SLOPE30: case K_SLOPE45:   // reference: downhill
        f[0] = (Facing){"down", 0}; f[1] = (Facing){"up", 3.14159f}; f[2] = (Facing){"across", 1.5708f}; return 3;
    case K_CLIFF_TOP:    // reference: toward the drop
        f[0] = (Facing){"edge", 0}; f[1] = (Facing){"from_edge", 3.14159f}; return 2;
    case K_CLIFF_FOOT:   // reference: toward the wall
        f[0] = (Facing){"wall", 0}; f[1] = (Facing){"from_wall", 3.14159f}; return 2;
    case K_SHORE:        // reference: toward the water
        f[0] = (Facing){"water", 0}; f[1] = (Facing){"land", 3.14159f}; return 2;
    case K_TREES: case K_BOULDER: case K_LOG:   // reference: toward the obstacle; facing away puts the camera at it
        f[0] = (Facing){"away", 3.14159f}; return 1;
    default: f[0] = (Facing){"-", 0}; return 1;
    }
}

static float trunk_near(const CretWorld* w, float x, float z, int* within10, float* ang) {
    float best = 1e9f; *within10 = 0;
    int tx = (int)floorf(x / VEG_TILE), tz = (int)floorf(z / VEG_TILE);
    float tr[5 * 256];
    for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
        int n = cret_tree_trunks(w, tx + dx, tz + dz, tr, 256);
        for (int k = 0; k < n; k++) {
            float d = hypotf(tr[5 * k] - x, tr[5 * k + 2] - z) - tr[5 * k + 3];
            if (d < 10) (*within10)++;
            if (d < best) { best = d; *ang = atan2f(tr[5 * k + 2] - z, tr[5 * k] - x); }
        }
    }
    return best;
}

// Up to n spots of each kind, at least 80 m apart, from random points; the
// prop and tree kinds come from the colliders and the trunk scatter.
static int find_spots(const CretWorld* w, int n, Spot* out, int cap, uint32_t seed) {
    int cnt[K_KINDS] = {0}, m = 0; uint32_t r = seed;
    float S = w->size;
    for (int it = 0; it < 400000 && m < cap; it++) {
        float x = 60 + frnd(&r) * (S - 120), z = 60 + frnd(&r) * (S - 120), y0;
        int k = classify(w, x, z, &y0);
        if (k < 0 || cnt[k] >= n) continue;
        bool far = true; for (int j = 0; j < m; j++) if (out[j].kind == k && hypotf(out[j].x - x, out[j].z - z) < 80) far = false;
        if (!far) continue;
        out[m++] = (Spot){k, x, z, y0, ""}; cnt[k]++;
    }
    // Under trees: walkable ground 1.5-3 m from a trunk with 4 more within 10 m.
    for (int it = 0; it < 200000 && cnt[K_TREES] < n && m < cap; it++) {
        float x = 60 + frnd(&r) * (S - 120), z = 60 + frnd(&r) * (S - 120), a = 0; int c;
        if (!cret_walkable(w, x, z)) continue;
        float d = trunk_near(w, x, z, &c, &a);
        if (d < 1.5f || d > 3 || c < 5) continue;
        out[m++] = (Spot){K_TREES, x, z, a, ""}; cnt[K_TREES]++;
    }
    // Next to boulders and logs, and among dense props: from the colliders.
    for (int it = 0; it < 20000 && w->nprops && m < cap; it++) {
        const CretPropCollider* p = &w->props[rnd(&r) % (uint32_t)w->nprops];
        int pk = w->prop_cat.kind[p->variant];
        float pr = w->prop_cat.radius[p->variant] * p->scale;
        int k = (pk == PROP_BOULDER || pk == PROP_OUTCROP) && pr > 1.0f ? K_BOULDER : (pk == PROP_LOG || pk == PROP_DRIFTWOOD) && pr > 1.0f ? K_LOG : -1;
        int near = 0; for (int j = 0; j < w->nprops; j++) if (fabsf(w->props[j].x - p->x) < 10 && fabsf(w->props[j].z - p->z) < 10) near++;
        if (near >= 6 && cnt[K_DENSE] < n) k = K_DENSE;
        if (k < 0 || cnt[k] >= n) continue;
        float a = frnd(&r) * 6.2832f, gap = k == K_DENSE ? 0.5f : 1.2f;
        float x = p->x + cosf(a) * (pr + gap), z = p->z + sinf(a) * (pr + gap);
        if (!cret_walkable(w, x, z)) continue;
        bool far = true; for (int j = 0; j < m; j++) if (out[j].kind == k && hypotf(out[j].x - x, out[j].z - z) < 80) far = false;
        if (!far) continue;
        out[m++] = (Spot){k, x, z, a + 3.14159f, ""}; cnt[k]++;   // reference: toward the prop
    }
    return m;
}

// ---------------------------------------------------------------- the scripted player
typedef struct { float dur, yaw_rate, pitch_rate, fwd, turn; bool sprint, swing; } Seg;
static Seg mk(float dur, float yaw_rate, float pitch_rate, float fwd, float turn, bool sprint, bool swing) {
    return (Seg){dur, yaw_rate, pitch_rate, fwd, turn, sprint, swing};
}
static int script(Seg* s) {
    int n = 0;
    s[n++] = mk(0.8f, 0, 0, 0, 0, false, false);
    for (int q = 0; q < 4; q++) {   // pitch sweeps at four headings around the animal
        s[n++] = mk(0.52f, 3.0f, 0, 0, 0, false, false);
        s[n++] = mk(1.6f, 0, -0.8f, 0, 0, false, false); s[n++] = mk(0.4f, 0, 0, 0, 0, false, false);
        s[n++] = mk(3.2f, 0, 0.8f, 0, 0, false, false); s[n++] = mk(0.4f, 0, 0, 0, 0, false, false);
        s[n++] = mk(1.5f, 0, -0.8f, 0, 0, false, false); s[n++] = mk(1.2f, 0, 0, 0, 0, false, false);
    }
    // Full turns with the camera high and low.
    s[n++] = mk(1.6f, 0, 1.0f, 0, 0, false, false); s[n++] = mk(5.3f, 1.2f, 0, 0, 0, false, false); s[n++] = mk(1.0f, 0, 0, 0, 0, false, false);
    s[n++] = mk(3.0f, 0, -1.0f, 0, 0, false, false); s[n++] = mk(5.3f, -1.2f, 0, 0, 0, false, false); s[n++] = mk(1.0f, 0, 0, 0, 0, false, false);
    s[n++] = mk(0.8f, 0, 0.8f, 0, 0, false, false);
    // Flicks.
    s[n++] = mk(0.26f, 12, 0, 0, 0, false, false); s[n++] = mk(0.6f, 0, 0, 0, 0, false, false);
    s[n++] = mk(0.25f, 0, 6, 0, 0, false, false); s[n++] = mk(0.6f, 0, 0, 0, 0, false, false);
    s[n++] = mk(0.35f, 0, -6, 0, 0, false, false); s[n++] = mk(0.6f, 0, 0, 0, 0, false, false);
    s[n++] = mk(0.26f, -12, 0, 0, 0, false, false); s[n++] = mk(0.25f, 0, 3, 0, 0, false, false); s[n++] = mk(1.2f, 0, 0, 0, 0, false, false);
    // Walking and running in circles (so the animal stays on its kind of
    // ground) while turning the camera, then with the look still, then the swing.
    s[n++] = mk(3.0f, 0.6f, 0, 0.5f, 0.5f, false, false);
    s[n++] = mk(2.0f, 0, 0, 0.5f, 0.2f, false, false);
    s[n++] = mk(3.0f, -0.9f, 0, 1, -0.5f, true, false);
    s[n++] = mk(2.0f, 0, 0, 1, -0.5f, true, false);
    s[n++] = mk(2.0f, 0, 0, 1, 0.6f, true, true);
    s[n++] = mk(1.5f, 0, 0, 0, 0, false, false);
    return n;
}

// ---------------------------------------------------------------- checks
// Water as drawn: the level blended between cell centres; a dry cell next to
// water takes its wettest neighbour's level (the renderer continues the sheet
// under the banks), one farther off sits far below ground.
static float drawn_water(const CretWorld* w, float x, float z) {
    float c = w->map.cell, gx = x / c, gz = z / c; int ix = (int)floorf(gx), iz = (int)floorf(gz);
    float fx = gx - ix, fz = gz - iz, v[4];
    for (int k = 0; k < 4; k++) {
        int cx = ix + (k & 1), cz = iz + (k >> 1);
        v[k] = cret_water(w, cx * c, cz * c);
        for (int n = 0; n < 9 && v[k] <= TERRA_DRY; n++) if (n != 4) v[k] = fmaxf(v[k], cret_water(w, (cx + n % 3 - 1) * c, (cz + n / 3 - 1) * c));
        if (v[k] <= TERRA_DRY) v[k] = -1e3f;
    }
    return (v[0] * (1 - fx) + v[1] * fx) * (1 - fz) + (v[2] * (1 - fx) + v[3] * fx) * fz;
}
// Clearance square to the terrain (the plane through the point below).
static float ground_gap(const CretWorld* w, const float p[3]) {
    float h = H(w, p[0], p[2]), gx = (H(w, p[0] + 0.5f, p[2]) - H(w, p[0] - 0.5f, p[2])), gz = (H(w, p[0], p[2] + 0.5f) - H(w, p[0], p[2] - 0.5f));
    return (p[1] - h) / sqrtf(1 + gx * gx + gz * gz);
}
// True if the segment a-b stays above the terrain less margin.
static bool terrain_sight(const CretWorld* w, const float a[3], const float b[3], float margin) {
    for (int i = 1; i < 48; i++) {
        float t = i / 48.0f, x = a[0] + (b[0] - a[0]) * t, y = a[1] + (b[1] - a[1]) * t, z = a[2] + (b[2] - a[2]) * t;
        if (y < H(w, x, z) - margin) return false;
    }
    return true;
}
// Fraction along p -> p + v before a sphere of radius r meets a prop or a trunk (1: none).
static float prop_cast(const CretSim* s, const float p[3], const float v[3], float r) {
    if (!b3World_IsValid(s->phys)) return 1;
    b3Vec3 pt = {0, 0, 0}; b3ShapeProxy proxy = {&pt, 1, r};
    b3QueryFilter f = {.categoryBits = CRET_CAT_AGENT, .maskBits = CRET_CAT_PROP, .id = 4, .name = "camprobe"};
    CamCast cc = {1.0f};
    b3World_CastShape(s->phys, (b3Pos){p[0], p[1], p[2]}, &proxy, (b3Vec3){v[0], v[1], v[2]}, f, cam_cast_fn, &cc);
    return cc.best;
}
// The pitch range the probe expects: wider where the ground ahead falls or rises.
static void pitch_range(const CretWorld* w, const CretAgent* a, float yaw, float L, float* lo, float* hi) {
    float h0 = H(w, a->x, a->z), fall = 0, rise = 0;
    for (int k = 0; k < 3; k++) {
        float d = L * (0.5f + 0.75f * k), dh = H(w, a->x + cosf(yaw) * d, a->z + sinf(yaw) * d) - h0;
        fall = fmaxf(fall, atan2f(-dh, d) - 0.05f); rise = fmaxf(rise, atan2f(dh, d) - 0.05f);
    }
    *lo = fmaxf(CAM_PITCH_MIN - 0.8f * fall, -1.2f); *hi = fminf(CAM_PITCH_MAX + 0.8f * rise, 0.95f);
}
static float seg_dist(const float p[3], const float a[3], const float b[3]) {
    float ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, ap[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
    float t = cam_clampf((ab[0] * ap[0] + ab[1] * ap[1] + ab[2] * ap[2]) / fmaxf(ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2], 1e-6f), 0, 1);
    float d[3] = {ap[0] - ab[0] * t, ap[1] - ab[1] * t, ap[2] - ab[2] * t};
    return sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

typedef struct { long v[K_KINDS][V_KINDS]; long frames[K_KINDS]; int runs[K_KINDS], bad_runs[K_KINDS]; } Tally;
typedef struct { float t, lens[3], yaw, pitch; } PathPt;   // flat-ground paths, relative to the animal

typedef struct {
    int species; float growth; int fps; Spot spot; float yaw;
    bool trace; FILE* tf;
    PathPt* path; int npath, cap;
    long v[V_KINDS]; long frames;
} Run;

static int g_dump;          // --dump N: print the first N violation frames
static int g_only = -1;     // --only KIND: runs on that terrain kind only
#define HIST 64
static void run_one(CretSim* s, Run* R, const Seg* seg, int nseg) {
    CretAgent* a = &s->agents[0];
    cret_sim_set_species(s, 0, R->species);
    cret_sim_restore(s, 0, R->growth, 1, 1, 1, false, 0, 0);
    a->x = R->spot.x; a->z = R->spot.z; a->y = H(s->world, a->x, a->z); a->yaw = R->yaw;
    a->vx = a->vy = a->vz = a->speed = 0; cret_sim_place(s, 0);
    CretInput in = {0};
    for (int t = 0; t < 15; t++) cret_sim_step(s, &in);   // settle
    CretCamRig rig = {0}; float yaw = a->yaw, pitch = -0.15f, asked = pitch, zoom = 1;
    float dt = 1.0f / R->fps, acc = 0, tt = 0;
    CretAgent prev = *a, draw = *a;
    float lens[3], last_lens[3] = {0}, last_P[3] = {0}, last_yaw = yaw, last_pitch = pitch, last_boom = 0, last_rig_boom = 0;
    float hist_pitch[HIST], hist_lens[HIST][3]; int hist_sign[HIST], hn = 0;
    float clear_t = 0, osc_t[2][8]; int osc_n[2] = {0}, osc_sign[2] = {0}; float osc_last[2] = {0};
    float pitch_bad_t = 0, cut_t = -10;
    for (int k = 0; k < 2; k++) for (int j = 0; j < 8; j++) osc_t[k][j] = -10;
    int si = 0; float seg_t = 0;
    while (si < nseg) {
        const Seg* g = &seg[si];
        in = (CretInput){.forward = g->fwd, .turn = g->turn, .sprint = g->sprint};
        acc += dt;
        while (acc >= CRET_DT) { acc -= CRET_DT; prev = *a; cret_sim_step(s, &in); }
        // The drawn animal, between the last two ticks (interpolate_agents).
        float al = acc / CRET_DT; draw = *a;
        if (prev.alive && fabsf(a->x - prev.x) + fabsf(a->z - prev.z) < 20) {
            draw.x = prev.x + (a->x - prev.x) * al; draw.y = prev.y + (a->y - prev.y) * al; draw.z = prev.z + (a->z - prev.z) * al;
            draw.yaw = prev.yaw + cam_wrap_pi(a->yaw - prev.yaw) * al;
        }
        CretCamInput ci = {.look_yaw = g->yaw_rate * dt, .look_pitch = g->pitch_rate * dt, .zoom = zoom, .look = true, .swing = g->swing};
        cret_cam_follow(&rig, s, &draw, &ci, &yaw, &pitch, dt, lens);
        float L = cam_boom(&draw, zoom), P[3]; cam_pivot(&draw, P);
        float lo, hi; pitch_range(s->world, &draw, yaw, L, &lo, &hi);
        // What the player asked for: the input, except where a limit holds or moves the view.
        asked += g->pitch_rate * dt;
        if (pitch <= rig.pitch_lo + 0.01f || pitch >= rig.pitch_hi - 0.01f || asked < rig.pitch_lo || asked > rig.pitch_hi) asked = pitch;
        tt += dt; seg_t += dt;
        if (seg_t >= g->dur) { seg_t = 0; si++; }
        int sign = g->pitch_rate > 0.3f ? 1 : g->pitch_rate < -0.3f ? -1 : 0;
        bool still = g->yaw_rate == 0 && g->pitch_rate == 0;
        int h = hn % HIST; hist_pitch[h] = pitch; memcpy(hist_lens[h], lens, sizeof lens); hist_sign[h] = sign; hn++;
        if (R->path && R->npath < R->cap) { PathPt* q = &R->path[R->npath++]; q->t = tt; q->lens[0] = lens[0] - draw.x; q->lens[1] = lens[1] - draw.y; q->lens[2] = lens[2] - draw.z; q->yaw = yaw; q->pitch = pitch; }
        float fwd[3] = {cosf(pitch) * cosf(yaw), sinf(pitch), cosf(pitch) * sinf(yaw)};
        float toP[3] = {P[0] - lens[0], P[1] - lens[1], P[2] - lens[2]};
        float dist = sqrtf(toP[0] * toP[0] + toP[1] * toP[1] + toP[2] * toP[2]);
        float boom = sqrtf((lens[0] - rig.target[0]) * (lens[0] - rig.target[0]) + (lens[1] - rig.target[1]) * (lens[1] - rig.target[1]) + (lens[2] - rig.target[2]) * (lens[2] - rig.target[2]));
        bool v[V_KINDS] = {0};
        if (tt > 0.3f) {
            v[V_NAN] = !isfinite(lens[0]) || !isfinite(lens[1]) || !isfinite(lens[2]) || !isfinite(yaw) || !isfinite(pitch);
            if (v[V_NAN]) { R->v[V_NAN]++; R->frames++; continue; }
            float gap = ground_gap(s->world, lens);
            v[V_GROUND] = gap < 0.25f;
            float wl = drawn_water(s->world, lens[0], lens[2]);
            v[V_WATER] = wl > H(s->world, lens[0], lens[2]) - 1 && lens[1] - wl < 0.25f;
            // The body as a capsule along the heading at 0.6 of the height; a
            // lens within 0.5 m of it is at the minimum, over the back or shoulder.
            float bl = CRET_SPECIES_TABLE[draw.species].length * cret_growth_scale(draw.growth), br = fmaxf(draw.radius, 0.4f * draw.height);
            float hl = fmaxf(0.5f * bl - br, 0), cy = draw.y + 0.6f * draw.height;
            float b0[3] = {draw.x - cosf(draw.yaw) * hl, cy, draw.z - sinf(draw.yaw) * hl}, b1[3] = {draw.x + cosf(draw.yaw) * hl, cy, draw.z + sinf(draw.yaw) * hl};
            float body = seg_dist(lens, b0, b1) - br;
            bool minimum = boom < 0.75f || body < 0.5f;
            const float* T = rig.target;
            bool sight = terrain_sight(s->world, lens, T, 0.02f);
            float back[3] = {lens[0] - T[0], lens[1] - T[1], lens[2] - T[2]};
            if (sight && boom > 0.1f) sight = prop_cast(s, T, back, 0.05f) > 0.98f;
            v[V_LOS] = !sight && !minimum;
            float dl = sqrtf((lens[0] - last_lens[0]) * (lens[0] - last_lens[0]) + (lens[1] - last_lens[1]) * (lens[1] - last_lens[1]) + (lens[2] - last_lens[2]) * (lens[2] - last_lens[2]));
            float dp = sqrtf((P[0] - last_P[0]) * (P[0] - last_P[0]) + (P[1] - last_P[1]) * (P[1] - last_P[1]) + (P[2] - last_P[2]) * (P[2] - last_P[2]));
            float turn = fabsf(cam_wrap_pi(yaw - last_yaw)) + fabsf(pitch - last_pitch);
            float allowed = 1.5f * dp + 1.3f * turn * fmaxf(boom, dist) + (4.0f + 2.0f * L) * dt + 0.02f;
            v[V_POP] = dl > allowed;
            // A cut, not a pop: the boom jumped in because a prop or trunk came
            // closer than it was, or the terrain hid the animal from where it was.
            if (v[V_POP] && boom < last_boom - 0.3f) {
                float bv[3] = {-fwd[0] * L, -fwd[1] * L, -fwd[2] * L};
                // Where the old boom would put the lens now (the turn of the
                // view moves it as well): hidden there too counts.
                float now[3], lift, d[3] = {-fwd[0], -fwd[1], -fwd[2]}; cam_lens(s->world, T, d, last_rig_boom, now, &lift);
                float was[3] = {last_lens[0] - T[0], last_lens[1] - T[1], last_lens[2] - T[2]}, nw[3] = {now[0] - T[0], now[1] - T[1], now[2] - T[2]};
                bool hidden = !terrain_sight(s->world, last_lens, T, -0.05f) || prop_cast(s, T, was, 0.1f) < 0.98f ||
                              !terrain_sight(s->world, now, T, -0.05f) || prop_cast(s, T, nw, 0.1f) < 0.98f;
                if (prop_cast(s, T, bv, CAM_RADIUS) * L < last_boom - 0.05f || hidden) { v[V_POP] = false; v[V_CUT] = true; }
            }
            // Pitch: the view follows the asked pitch within 0.06 rad (0.5 s of
            // grace while a limit moves), and a limit the player pushes against
            // is no tighter than the ground ahead calls for.
            pitch_bad_t = fabsf(pitch - asked) > 0.06f ? pitch_bad_t + dt : 0;
            // A known reason for a tighter upper limit: the lens sits on the
            // ground behind and the pivot is already near the bottom of the view.
            float elev = atan2f(P[1] - lens[1], hypotf(P[0] - lens[0], P[2] - lens[2]));
            bool framed = rig.frame_limit && pitch - elev > 0.38f && fminf(gap, lens[1] - drawn_water(s->world, lens[0], lens[2])) < 0.8f;
            bool tight = (sign < 0 && pitch < rig.pitch_lo + 0.01f && rig.pitch_lo > lo + 0.05f) || (sign > 0 && pitch > rig.pitch_hi - 0.01f && rig.pitch_hi < hi - 0.05f && !framed);
            v[V_PITCH] = pitch_bad_t > 0.5f || tight;
            // Stuck: 0.25 s of input one way that moved nothing while the range allows more.
            int n = (int)(0.25f * R->fps);
            if (sign && hn > n) {
                bool held = true; for (int j = 1; j <= n; j++) if (hist_sign[(hn - 1 - j) % HIST] != sign) held = false;
                const float* l0 = hist_lens[(hn - 1 - n) % HIST]; float p0 = hist_pitch[(hn - 1 - n) % HIST];
                float mv = fabsf(lens[0] - l0[0]) + fabsf(lens[1] - l0[1]) + fabsf(lens[2] - l0[2]);
                bool room = sign > 0 ? pitch < hi - 0.03f && !framed : pitch > lo + 0.03f;
                v[V_STUCK] = held && room && fabsf(pitch - p0) < 0.005f && mv < 0.02f;
            }
            // Short: nothing in the boom's way for a second, yet the boom is short.
            float d[3] = {-fwd[0], -fwd[1], -fwd[2]}, bv[3] = {d[0] * L, d[1] * L, d[2] * L};
            bool open = prop_cast(s, rig.target, bv, CAM_RADIUS) >= 1.0f;
            for (int j = 1; j <= 24 && open; j++) {
                float u = L * j / 24.0f, x = rig.target[0] + d[0] * u, y = rig.target[1] + d[1] * u, z = rig.target[2] + d[2] * u;
                if (y < fmaxf(H(s->world, x, z), drawn_water(s->world, x, z)) + CAM_CLEAR + 0.3f) open = false;
            }
            clear_t = open ? clear_t + dt : 0;
            v[V_SHORT] = clear_t > 1.0f && boom < 0.9f * L;
            // Oscillation: the lens height over the pivot or its distance turns
            // back 5 times in a second, each swing at least 5 cm (a turn counts
            // once the signal has come 5 cm back from its last extreme), not
            // counting the half second after a cut (the boom recovering).
            if (v[V_CUT]) cut_t = tt;
            float sig[2] = {lens[1] - rig.target[1], boom};
            for (int k = 0; k < 2; k++) {
                if (!still) { osc_sign[k] = 0; osc_last[k] = sig[k]; continue; }
                if (!osc_sign[k]) { osc_sign[k] = 1; osc_last[k] = sig[k]; }
                if ((sig[k] - osc_last[k]) * osc_sign[k] > 0) osc_last[k] = sig[k];   // further the same way: a new extreme
                else if (fabsf(sig[k] - osc_last[k]) > 0.05f) { osc_sign[k] = -osc_sign[k]; osc_last[k] = sig[k]; if (tt - cut_t > 0.5f) osc_t[k][osc_n[k]++ % 8] = tt; }
                int c = 0; for (int j = 0; j < 8; j++) c += tt - osc_t[k][j] < 1.0f;
                if (c >= 5) v[V_OSC] = true;
            }
            // Frame: the focus point projected with the 60 degree view at 16:9.
            float rt[3] = {-sinf(yaw), 0, cosf(yaw)}, up[3] = {rt[1] * fwd[2] - rt[2] * fwd[1], rt[2] * fwd[0] - rt[0] * fwd[2], rt[0] * fwd[1] - rt[1] * fwd[0]};
            float zc = toP[0] * fwd[0] + toP[1] * fwd[1] + toP[2] * fwd[2];
            float xs = (toP[0] * rt[0] + toP[2] * rt[2]) / fmaxf(zc, 1e-3f), ys = (toP[0] * up[0] + toP[1] * up[1] + toP[2] * up[2]) / fmaxf(zc, 1e-3f);
            v[V_FRAME] = !minimum && (zc < 0.05f || fabsf(ys) > 0.577f * 0.92f || fabsf(xs) > 1.026f * 0.95f);
            v[V_INSIDE] = body < 0.15f;
            R->frames++;
            for (int k = 0; k < V_KINDS; k++) R->v[k] += v[k];
            bool any = false; for (int k = 0; k < V_KINDS; k++) any |= v[k];
            #pragma omp critical
            if (any && g_dump > 0) {
                g_dump--;
                printf("  %s %s %s g%.2f %d fps t %.2f seg %d:", KIND_NAME[R->spot.kind], R->spot.facing, CRET_SPECIES_TABLE[R->species].name, R->growth, R->fps, tt, si);
                for (int k = 0; k < V_KINDS; k++) if (v[k]) printf(" %s", VIOL_NAME[k]);
                printf("  moved %.2f of %.2f (boom %+.2f, up %+.2f)", dl, allowed, boom - last_boom, lens[1] - last_lens[1]);
                printf("  yaw %+.2f (body %+.2f) pitch %+.2f asked %+.2f [%+.2f %+.2f] boom %.2f/%.2f gap %.2f lens %.1f %.1f %.1f animal %.1f %.1f %.1f\n",
                       yaw, draw.yaw, pitch, asked, lo, hi, boom, L, gap, lens[0], lens[1], lens[2], draw.x, draw.y, draw.z);
            }
        }
        if (R->trace && R->tf && ((int)roundf(tt * R->fps) % (R->fps / 10 > 0 ? R->fps / 10 : 1)) == 0) {
            float gap = ground_gap(s->world, lens);
            float fall = atan2f(P[1] - lens[1], hypotf(P[0] - lens[0], P[2] - lens[2]));
            fprintf(R->tf, "%5.1f  in %+5.2f  asked %+5.2f  view %+5.2f [%+5.2f %+5.2f]  boom %5.2f/%5.2f  gap %5.2f  focus %+5.1f deg off centre  animal %.1f %.1f %.1f%s%s%s%s\n",
                    tt, g->pitch_rate, asked, pitch, lo, hi, boom, L, gap, (fall - pitch) * 57.3f, draw.x, draw.y, draw.z,
                    v[V_FRAME] ? "  OFF-SCREEN" : "", v[V_STUCK] ? "  STUCK" : "", v[V_LOS] ? "  NO-SIGHT" : "", v[V_PITCH] ? "  PITCH" : "");
        }
        memcpy(last_lens, lens, sizeof lens); memcpy(last_P, P, sizeof P); last_yaw = yaw; last_pitch = pitch; last_boom = boom; last_rig_boom = rig.boom;
    }
}

// ---------------------------------------------------------------- the owner's case
// An adult raptor on an even 30 degree slope, the camera straight behind it on
// the uphill side (the animal faces downhill) or the downhill side, the
// player pitching up (the lens drops toward the ground) to the limit, then
// down to the other limit, then back to the start.
static void trace_case(CretSim* s, const Spot* spots, int nspots) {
    const Spot* sp = NULL; for (int i = 0; i < nspots; i++) if (spots[i].kind == K_SLOPE30) { sp = &spots[i]; break; }
    if (!sp) { printf("trace: no 30 degree slope found\n"); return; }
    Seg seg[8]; int n = 0;
    seg[n++] = mk(0.6f, 0, 0, 0, 0, false, false);
    seg[n++] = mk(2.5f, 0, 0.5f, 0, 0, false, false); seg[n++] = mk(0.5f, 0, 0, 0, 0, false, false);
    seg[n++] = mk(4.0f, 0, -0.5f, 0, 0, false, false); seg[n++] = mk(0.5f, 0, 0, 0, 0, false, false);
    seg[n++] = mk(1.5f, 0, 0.5f, 0, 0, false, false); seg[n++] = mk(1.0f, 0, 0, 0, 0, false, false);
    for (int c = 0; c < 2; c++) {
        Run R = {.species = CRET_SP_RAPTOR, .growth = 1, .fps = 30, .spot = *sp, .yaw = sp->yaw + (c ? 3.14159f : 0), .trace = true, .tf = stdout};
        printf("\n== camera %s: the raptor faces %s at %.0f %.0f (every 0.1 s: pitch input, asked and view pitch [range], boom/full, clearance, focus angle)\n",
               c ? "downhill" : "uphill", c ? "uphill" : "downhill", sp->x, sp->z);
        run_one(s, &R, seg, n);
        printf("frames %ld:", R.frames); for (int k = 0; k < V_KINDS; k++) printf(" %s %ld", VIOL_NAME[k], R.v[k]); printf("\n");
    }
}

int main(int argc, char** argv) {
    bool quick = false, check = false, trace = false, verbose = false; int per = 2; const char *paths_out = NULL, *paths_ref = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--quick")) quick = true;
        else if (!strcmp(argv[i], "--check")) check = true;
        else if (!strcmp(argv[i], "--trace")) trace = true;
        else if (!strcmp(argv[i], "--verbose")) verbose = true;
        else if (!strcmp(argv[i], "--spots") && i + 1 < argc) per = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) g_dump = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--only") && i + 1 < argc) { i++; for (int k = 0; k < K_KINDS; k++) if (!strcmp(argv[i], KIND_NAME[k])) g_only = k; }
        else if (!strcmp(argv[i], "--paths-out") && i + 1 < argc) paths_out = argv[++i];
        else if (!strcmp(argv[i], "--paths-ref") && i + 1 < argc) paths_ref = argv[++i];
        else { fprintf(stderr, "camprobe: unknown option %s\n", argv[i]); return 2; }
    }
    CretConfig cfg = cret_default_config();
    if (quick) { cfg.world_n = 512; cfg.world_cell = 4.0f; per = 1; }
    cfg.num_agents = 1; cfg.weather = 0; cfg.player_growth = 1; cfg.respawn_ticks = 1;
    CretWorld* w = cret_world_acquire(cfg.world_seed, cfg.world_n, cfg.world_cell, 1);
    if (!w) { fprintf(stderr, "camprobe: no world\n"); return 2; }
    Spot spots[256]; int nsp = find_spots(w, per, spots, 256, 12345);
    int have[K_KINDS] = {0}; for (int i = 0; i < nsp; i++) have[spots[i].kind]++;
    printf("camprobe: %s world, %d spots:", quick ? "test" : "game", nsp);
    for (int k = 0; k < K_KINDS; k++) printf(" %s %d", KIND_NAME[k], have[k]);
    printf("%s\n", w->nprops ? "" : "  (no props: assets/props missing)");
    if (trace) {
        CretSim* s = calloc(1, sizeof *s); if (cret_sim_init(s, cfg) != 0) return 2;
        trace_case(s, spots, nsp); cret_sim_close(s); return 0;
    }
    // Runs: spot x facing x species x growth x fps.
    static const int SPECIES_ALL[] = {CRET_SP_RAPTOR, CRET_SP_STEGO, CRET_SP_TREX, CRET_SP_PACHY, CRET_SP_SAUROPELTA, CRET_SP_GALLI};
    static const int SPECIES_QUICK[] = {CRET_SP_RAPTOR, CRET_SP_TREX};
    static const int FPS_ALL[] = {15, 30, 60}, FPS_QUICK[] = {15, 60};
    const int* sp = quick ? SPECIES_QUICK : SPECIES_ALL; int nsp_ = quick ? 2 : 6;
    const int* fps = quick ? FPS_QUICK : FPS_ALL; int nfps = quick ? 2 : 3;
    static const float GROWTH[2] = {0.35f, 1.0f};
    int cap = nsp * 3 * nsp_ * 2 * nfps; Run* runs = calloc((size_t)cap, sizeof *runs); int nr = 0;
    for (int i = 0; i < nsp; i++) {
        Facing f[3]; int nf = facings(spots[i].kind, f);
        if ((quick && i > 0 && spots[i].kind == spots[i - 1].kind) || (g_only >= 0 && spots[i].kind != g_only)) continue;
        for (int j = 0; j < nf; j++) for (int a = 0; a < nsp_; a++) for (int g = 0; g < 2; g++) for (int q = 0; q < nfps; q++) {
            Run* R = &runs[nr++]; R->species = sp[a]; R->growth = GROWTH[g]; R->fps = fps[q];
            R->spot = spots[i]; snprintf(R->spot.facing, sizeof R->spot.facing, "%s", f[j].name); R->yaw = spots[i].yaw + f[j].off;
            if ((paths_out || paths_ref) && spots[i].kind == K_FLAT) { R->cap = 120 * fps[q]; R->path = calloc((size_t)R->cap, sizeof(PathPt)); }
        }
    }
    Seg seg[64]; int nseg = script(seg);
    // Each run gets a fresh sim, so props pushed in one run do not change
    // another and the counts do not depend on the thread schedule. Starting
    // and closing sims shares the world's bookkeeping, so one at a time.
    int failed = 0;
    #pragma omp parallel for schedule(dynamic, 1)
    for (int i = 0; i < nr; i++) {
        CretSim* s = calloc(1, sizeof *s); int ok;
        #pragma omp critical(camprobe_sim)
        ok = cret_sim_init(s, cfg) == 0;
        if (ok) run_one(s, &runs[i], seg, nseg);
        #pragma omp critical(camprobe_sim)
        { if (ok) cret_sim_close(s); else failed++; }
        free(s);
    }
    if (failed) { fprintf(stderr, "camprobe: %d sims failed to start\n", failed); return 2; }
    Tally T = {0}; long tot[V_KINDS] = {0}; long fps_tot[3][V_KINDS] = {{0}}; long all = 0;
    for (int i = 0; i < nr; i++) {
        Run* R = &runs[i]; int k = R->spot.kind; long any = 0;
        T.frames[k] += R->frames; T.runs[k]++;
        for (int v = 0; v < V_KINDS; v++) { T.v[k][v] += R->v[v]; tot[v] += R->v[v]; any += v == V_CUT ? 0 : R->v[v]; for (int q = 0; q < nfps; q++) if (fps[q] == R->fps) fps_tot[q][v] += R->v[v]; }
        T.bad_runs[k] += any > 0; all += any;
        if (verbose && any) {
            printf("  %-10s %-9s %-18s g%.2f %2d fps at %6.0f %6.0f:", KIND_NAME[k], R->spot.facing, CRET_SPECIES_TABLE[R->species].name, R->growth, R->fps, R->spot.x, R->spot.z);
            for (int v = 0; v < V_KINDS; v++) if (R->v[v]) printf(" %s %ld", VIOL_NAME[v], R->v[v]);
            printf("\n");
        }
    }
    printf("%d runs; violation frames per terrain kind:\n%-11s %6s %9s", nr, "kind", "runs", "frames");
    for (int v = 0; v < V_KINDS; v++) printf(" %7s", VIOL_NAME[v]);
    printf("  bad runs\n");
    for (int k = 0; k < K_KINDS; k++) {
        if (!T.runs[k]) continue;
        printf("%-11s %6d %9ld", KIND_NAME[k], T.runs[k], T.frames[k]);
        for (int v = 0; v < V_KINDS; v++) printf(" %7ld", T.v[k][v]);
        printf("  %d\n", T.bad_runs[k]);
    }
    long frames = 0; for (int k = 0; k < K_KINDS; k++) frames += T.frames[k];
    printf("%-11s %6d %9ld", "total", nr, frames); for (int v = 0; v < V_KINDS; v++) printf(" %7ld", tot[v]); printf("\n");
    for (int q = 0; q < nfps; q++) { printf("%-8s%3d %16s", "at fps", fps[q], ""); for (int v = 0; v < V_KINDS; v++) printf(" %7ld", fps_tot[q][v]); printf("\n"); }
    // Flat-ground paths: written for a later comparison, or compared with an earlier run.
    if (paths_out) {
        FILE* f = fopen(paths_out, "wb");
        for (int i = 0; f && i < nr; i++) if (runs[i].path) { fwrite(&runs[i].npath, sizeof(int), 1, f); fwrite(runs[i].path, sizeof(PathPt), (size_t)runs[i].npath, f); }
        if (f) fclose(f);
        printf("flat-ground lens paths written to %s\n", paths_out);
    }
    if (paths_ref) {
        FILE* f = fopen(paths_ref, "rb"); double sum = 0, worst = 0, wp = 0; long n = 0; int runs_cmp = 0;
        for (int i = 0; f && i < nr; i++) {
            if (!runs[i].path) continue;
            int m = 0; if (fread(&m, sizeof m, 1, f) != 1) break;
            PathPt* ref = calloc((size_t)m, sizeof *ref); if (fread(ref, sizeof *ref, (size_t)m, f) != (size_t)m) { free(ref); break; }
            for (int j = 0; j < m && j < runs[i].npath; j++) {
                const PathPt *a = &runs[i].path[j], *b = &ref[j];
                double d = sqrt((a->lens[0] - b->lens[0]) * (a->lens[0] - b->lens[0]) + (a->lens[1] - b->lens[1]) * (a->lens[1] - b->lens[1]) + (a->lens[2] - b->lens[2]) * (a->lens[2] - b->lens[2]));
                sum += d; n++; if (d > worst) worst = d;
                wp = fmax(wp, fabs(a->pitch - b->pitch));
            }
            runs_cmp++; free(ref);
        }
        if (f) fclose(f);
        printf("flat-ground lens paths vs %s: %d runs, %ld frames, mean %.4f m, worst %.3f m apart; view pitch at most %.3f rad apart\n", paths_ref, runs_cmp, n, n ? sum / n : 0, worst, wp);
    }
    if (check && all) { printf("camprobe: FAILED (%ld violation frames)\n", all); return 1; }
    if (check) printf("camprobe: ok\n");
    return 0;
}
