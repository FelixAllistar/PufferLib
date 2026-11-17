// Ragdoll description built from a .dino file (tools/dino_file.h) and a bone
// group table: which rig bones merge into which rigid body, each body's
// collision shape and mass from the skinned mesh, the joint frames and limits
// from the range the clips use, and the reference motion (a clip sampled as
// body poses and velocities). Pure math, no Box3D world: loco_sim.h builds
// the physics from this, loco_env.h the RL environment.
//
// Body frames. Every body's frame is the model frame at the bind pose, placed
// at the bind position of the body's first joint. A body's pose in any frame
// of a clip is then exactly that joint's skinning matrix (global pose times
// inverse bind), so the reference poses come straight from the rig and the
// collision shapes are the mesh vertices in bind space. The model faces +X,
// up is +Y (tools/dino_convert.py).
//
// The tables match bone names case-insensitively, so one table serves a rig
// whatever its capitalisation. A bone not named in the table belongs to the
// body of its nearest named ancestor (fingers, toes, jaw, tongue and eyes
// merge into their parents). A body whose bones carry fewer than
// LOCO_MIN_VERTS vertices merges into its parent body and passes on its
// end-effector and foot flags (the test rigs' head and toe joints are tips
// with no mesh of their own). A body whose joint the clips never move (under
// LOCO_MIN_RANGE) is welded to its parent the same way. Vertices skinned to
// joints above the pelvis (a root bone on the ground) belong to no body.
#ifndef CRET_LOCO_RIG_H
#define CRET_LOCO_RIG_H
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "box3d/math_functions.h"
#include "tools/dino_file.h"

#define LOCO_MAX_BODIES 24
#define LOCO_MAX_DOF (3 * (LOCO_MAX_BODIES - 1))
#define LOCO_MAX_EE 8
#define LOCO_MIN_VERTS 8
#define LOCO_DENSITY 1000.0f      // kg/m^3, about water: animals float near neutral
#define LOCO_LIMIT_MARGIN 0.25f   // rad added on each side of the range the clips use
#define LOCO_HINGE_FIT 0.85f      // a hinge body becomes a revolute joint when this share of its rotation is about one axis
#define LOCO_MIN_RANGE 0.035f     // rad (2 degrees): by default a joint the clips move less than this is welded

// Body flags in a table: allowed to touch the ground (feet; anything else
// touching it ends the episode), part of the tail, a knee or ankle that may
// become a hinge, the foot (a box, flat on the ground), and an end effector
// tracked by the reward (its far tip).
enum { LB_CONTACT = 1, LB_TAIL = 2, LB_HINGE = 4, LB_FOOT = 8, LB_EE = 16 };
typedef struct LocoBodyDef { const char* name; const char* bones; int flags; } LocoBodyDef;
typedef struct LocoTable { const char* name; const char* probe; LocoBodyDef body[LOCO_MAX_BODIES]; } LocoTable;

// The licensed velociraptor (tools/fetch_dinos.sh; bone names as in
// tools/dino_retarget.py's RAPTOR): Spine_1 is the hips, ten tail bones merge
// pairwise into five segments, three neck bones into two, and the hand into
// the forearm. 23 bodies.
static const LocoTable LOCO_TABLE_RAPTOR = {"raptor", "spine_1", {
    {"pelvis", "Spine_1", 0}, {"spine", "Spine_2", 0}, {"chest", "Spine_3", 0},
    {"neck_a", "Neck_1,Neck_2", 0}, {"neck_b", "Neck_3", 0}, {"head", "Head", LB_EE},
    {"tail_1", "Tail_1,Tail_2", LB_TAIL}, {"tail_2", "Tail_3,Tail_4", LB_TAIL}, {"tail_3", "Tail_5,Tail_6", LB_TAIL},
    {"tail_4", "Tail_7,Tail_8", LB_TAIL}, {"tail_5", "Tail_9,Tail_10", LB_TAIL | LB_EE},
    {"thigh_l", "UpperLeg.L", 0}, {"shin_l", "LowerLeg.L", LB_HINGE}, {"meta_l", "Ankle.L", LB_HINGE | LB_CONTACT},
    {"foot_l", "Foot.L", LB_HINGE | LB_CONTACT | LB_FOOT | LB_EE},
    {"thigh_r", "UpperLeg.R", 0}, {"shin_r", "LowerLeg.R", LB_HINGE}, {"meta_r", "Ankle.R", LB_HINGE | LB_CONTACT},
    {"foot_r", "Foot.R", LB_HINGE | LB_CONTACT | LB_FOOT | LB_EE},
    {"arm_l", "UpperArm.L", 0}, {"forearm_l", "LowerArm.L", LB_HINGE}, {"arm_r", "UpperArm.R", 0}, {"forearm_r", "LowerArm.R", LB_HINGE}}};

// The synthetic test rigs (tools/dino_testrig.py raptor, tests/fixtures/fixture_biped.dino):
// lower-case names, tails of three to five bones, no arms.
static const LocoTable LOCO_TABLE_TESTRIG = {"testrig", "pelvis", {
    {"pelvis", "pelvis", 0}, {"spine", "spine1,spine", 0}, {"chest", "spine2", 0},
    {"neck_a", "neck", 0}, {"neck_b", "neck1", 0}, {"head", "head", LB_EE},
    {"tail_1", "tail,tail.001", LB_TAIL}, {"tail_2", "tail1,tail.002", LB_TAIL}, {"tail_3", "tail2,tail.003", LB_TAIL},
    {"tail_4", "tail3", LB_TAIL}, {"tail_5", "tail4", LB_TAIL | LB_EE},
    {"thigh_l", "upperleg.l", 0}, {"shin_l", "lowerleg.l", LB_HINGE}, {"meta_l", "ankle.l", LB_HINGE | LB_CONTACT},
    {"foot_l", "toe.l", LB_HINGE | LB_CONTACT | LB_FOOT | LB_EE},
    {"thigh_r", "upperleg.r", 0}, {"shin_r", "lowerleg.r", LB_HINGE}, {"meta_r", "ankle.r", LB_HINGE | LB_CONTACT},
    {"foot_r", "toe.r", LB_HINGE | LB_CONTACT | LB_FOOT | LB_EE}}};

static const LocoTable* const LOCO_TABLES[] = {&LOCO_TABLE_RAPTOR, &LOCO_TABLE_TESTRIG};

enum { LJ_NONE, LJ_BALL, LJ_HINGE };
enum { LS_CAPSULE, LS_BOX };
#define LOCO_MAX_SHAPES 6
// One collision shape of a body, fitted to one table group's vertices (a
// welded body keeps one shape per group it absorbed), in the body frame.
typedef struct LocoShape {
    int type;                    // LS_CAPSULE or LS_BOX
    b3Vec3 c1, c2; float radius; // capsule
    b3Vec3 box_c, box_h; b3Quat box_q;   // box: centre, half extents, orientation
    b3Vec3 com, tip;             // centre and the end farther from the body origin
    float volume, mass;          // m^3; kg at LOCO_DENSITY
} LocoShape;
typedef struct LocoBody {
    char name[24]; int flags, parent, joint, nvert;   // joint: the .dino joint whose skinning matrix is this body's pose
    b3Vec3 bind;                 // that joint's bind position (model space): the body origin at the bind pose
    int nshape; LocoShape shape[LOCO_MAX_SHAPES];
    b3Vec3 com, ee;              // centre of mass and end-effector tip (body frame)
    float volume, mass;          // m^3; kg at LOCO_DENSITY
    // Joint to the parent body. Its anchor is this body's origin; frame B is
    // qf (z along the bone, or along the hinge axis), frame A is q_mean * qf,
    // so the joint reads zero at the mean pose of the clips and its limits
    // (cone and twist, or the hinge range) are centred there.
    int jtype, dof, act0;        // LJ_*, action dimensions (3 ball, 1 hinge), first action index
    b3Vec3 anchor_a;             // the anchor in the parent body's frame
    b3Quat qf_a, qf_b, q_mean;
    float cone, twist_lo, twist_hi, lo, hi;
    float hinge_fit;             // share of the joint's rotation about its best single axis
} LocoBody;

typedef struct LocoRig {
    char table[16]; int nbody, ndof, nee;
    LocoBody body[LOCO_MAX_BODIES];
    int ee_body[LOCO_MAX_EE];
    float total_mass, length, height;
    int joint_body[DF_MAX_JOINTS];   // .dino joint -> body (-1: above the pelvis)
} LocoRig;

// One clip as body poses: period frames (the clip's last frame repeats its
// first), positions and rotations in model space (in place), velocities with
// the clip's forward speed added (the game's clips have the root motion
// removed; the reference walks forward at DinoClip.speed along +X).
typedef struct LocoRef {
    char clip[32]; int period, nbody; float dt, duration, speed;
    b3Vec3* pos; b3Quat* rot; b3Vec3* vel; b3Vec3* angvel;   // [frame * nbody + body]
} LocoRef;

// ------------------------------------------------------------------ math
static inline b3Quat lq_mul(b3Quat a, b3Quat b) { return b3MulQuat(a, b); }
static inline b3Quat lq_inv_mul(b3Quat a, b3Quat b) { return b3InvMulQuat(a, b); }
static inline b3Quat lq_conj(b3Quat q) { return b3Conjugate(q); }
// Box3D's own rotate, lerp and axis-angle helpers assert on their inputs, and the
// release library has no assert handler for a build without NDEBUG, so these are local.
static inline b3Vec3 lv(float x, float y, float z) { b3Vec3 v = {x, y, z}; return v; }
static inline b3Quat lq_make(float x, float y, float z, float w) { b3Quat q = {{x, y, z}, w}; return q; }
static inline b3Vec3 lq_rot(b3Quat q, b3Vec3 v) {
    b3Vec3 t = b3MulSV(2.0f, b3Cross(q.v, v));
    return b3Add(b3Add(v, b3MulSV(q.s, t)), b3Cross(q.v, t));
}
static inline b3Vec3 lq_inv_rot(b3Quat q, b3Vec3 v) { return lq_rot(lq_make(-q.v.x, -q.v.y, -q.v.z, q.s), v); }
static inline b3Quat lq_axis_angle(b3Vec3 axis, float ang) { float s = sinf(0.5f * ang); return lq_make(axis.x * s, axis.y * s, axis.z * s, cosf(0.5f * ang)); }
static inline b3Vec3 lv_lerp(b3Vec3 a, b3Vec3 b, float t) { return lv(a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.z + t * (b.z - a.z)); }
static inline b3Quat lq_nlerp(b3Quat a, b3Quat b, float t) { b3Vec3 v = lv_lerp(a.v, b.v, t); return b3NormalizeQuat(lq_make(v.x, v.y, v.z, a.s + t * (b.s - a.s))); }
static inline float lv_dot(b3Vec3 a, b3Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float lv_len2(b3Vec3 a) { return lv_dot(a, a); }
// Rotation vector (axis * angle, angle in [0, pi]) of q, and its inverse.
static inline b3Vec3 lq_log(b3Quat q) {
    if (q.s < 0) { q.v = b3Neg(q.v); q.s = -q.s; }
    float sn = sqrtf(lv_len2(q.v));
    if (sn < 1e-7f) return b3MulSV(2.0f, q.v);
    float ang = 2.0f * atan2f(sn, q.s);
    return b3MulSV(ang / sn, q.v);
}
static inline b3Quat lq_exp(b3Vec3 r) {
    float ang = sqrtf(lv_len2(r));
    if (ang < 1e-7f) return b3NormalizeQuat(lq_make(0.5f * r.x, 0.5f * r.y, 0.5f * r.z, 1.0f));
    float s = sinf(0.5f * ang) / ang;
    return lq_make(r.x * s, r.y * s, r.z * s, cosf(0.5f * ang));
}
// Angle of the rotation between a and b (radians, 0..pi), from the relative
// quaternion with atan2 (acos of the dot product loses small angles to rounding).
static inline float lq_angle(b3Quat a, b3Quat b) {
    b3Quat r = b3InvMulQuat(a, b);
    return 2.0f * atan2f(sqrtf(r.v.x * r.v.x + r.v.y * r.v.y + r.v.z * r.v.z), fabsf(r.s));
}
// Shortest rotation taking unit vector a to unit vector b.
static inline b3Quat lq_between(b3Vec3 a, b3Vec3 b) {
    float d = lv_dot(a, b);
    if (d < -0.9999f) { b3Vec3 p = fabsf(a.x) < 0.9f ? b3Cross(a, lv(1, 0, 0)) : b3Cross(a, lv(0, 1, 0)); return lq_axis_angle(b3Normalize(p), (float)M_PI); }
    b3Vec3 c = b3Cross(a, b);
    return b3NormalizeQuat(lq_make(c.x, c.y, c.z, 1.0f + d));
}
// Twist angle of q about z (swing-twist decomposition), in (-pi, pi].
static inline float lq_twist_z(b3Quat q) { float t = 2.0f * atan2f(q.v.z, q.s); return t > (float)M_PI ? t - 2.0f * (float)M_PI : t < -(float)M_PI ? t + 2.0f * (float)M_PI : t; }
// Rotation of a 3x3 matrix whose columns may carry a uniform scale (rows of m are the 3x4 rows).
static inline b3Quat lq_from_m34(const float* m) {
    b3Vec3 c0 = b3Normalize(lv(m[0], m[4], m[8])), c1 = lv(m[1], m[5], m[9]);
    c1 = b3Normalize(b3Sub(c1, b3MulSV(lv_dot(c0, c1), c0)));
    b3Vec3 c2 = b3Cross(c0, c1);
    float r00 = c0.x, r11 = c1.y, r22 = c2.z, tr = r00 + r11 + r22; b3Quat q;
    if (tr > 0) { float s = sqrtf(tr + 1.0f) * 2; q = lq_make((c1.z - c2.y) / s, (c2.x - c0.z) / s, (c0.y - c1.x) / s, 0.25f * s); }
    else if (r00 > r11 && r00 > r22) { float s = sqrtf(1.0f + r00 - r11 - r22) * 2; q = lq_make(0.25f * s, (c1.x + c0.y) / s, (c2.x + c0.z) / s, (c1.z - c2.y) / s); }
    else if (r11 > r22) { float s = sqrtf(1.0f + r11 - r00 - r22) * 2; q = lq_make((c1.x + c0.y) / s, 0.25f * s, (c2.y + c1.z) / s, (c2.x - c0.z) / s); }
    else { float s = sqrtf(1.0f + r22 - r00 - r11) * 2; q = lq_make((c2.x + c0.z) / s, (c2.y + c1.z) / s, 0.25f * s, (c0.y - c1.x) / s); }
    return b3NormalizeQuat(q);
}
static inline b3Vec3 lm34_point(const float* m, b3Vec3 p) {
    return lv(m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7], m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]);
}
// Inverse of an affine 3x4 (general 3x3 part).
static inline void lm34_inverse(const float* m, float* o) {
    float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g, det = a * A + b * B + c * C, k = 1.0f / det;
    o[0] = A * k; o[1] = -(b * i - c * h) * k; o[2] = (b * f - c * e) * k;
    o[4] = B * k; o[5] = (a * i - c * g) * k; o[6] = -(a * f - c * d) * k;
    o[8] = C * k; o[9] = -(a * h - b * g) * k; o[10] = (a * e - b * d) * k;
    for (int r = 0; r < 3; r++) o[r * 4 + 3] = -(o[r * 4] * m[3] + o[r * 4 + 1] * m[7] + o[r * 4 + 2] * m[11]);
}
// Eigenvectors of a symmetric 3x3 (Jacobi), sorted by decreasing eigenvalue: rows of v.
static void lm3_eigen(const double a_in[3][3], double val[3], double v[3][3]) {
    double a[3][3]; memcpy(a, a_in, sizeof a);
    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) v[r][c] = r == c;
    for (int sweep = 0; sweep < 50; sweep++) {
        double off = fabs(a[0][1]) + fabs(a[0][2]) + fabs(a[1][2]);
        if (off < 1e-14) break;
        for (int p = 0; p < 2; p++) for (int q = p + 1; q < 3; q++) {
            if (fabs(a[p][q]) < 1e-18) continue;
            double th = 0.5 * atan2(2 * a[p][q], a[q][q] - a[p][p]), c = cos(th), s = sin(th);
            for (int k = 0; k < 3; k++) { double akp = a[k][p], akq = a[k][q]; a[k][p] = c * akp - s * akq; a[k][q] = s * akp + c * akq; }
            for (int k = 0; k < 3; k++) { double apk = a[p][k], aqk = a[q][k]; a[p][k] = c * apk - s * aqk; a[q][k] = s * apk + c * aqk; }
            for (int k = 0; k < 3; k++) { double vpk = v[p][k], vqk = v[q][k]; v[p][k] = c * vpk - s * vqk; v[q][k] = s * vpk + c * vqk; }
        }
    }
    for (int k = 0; k < 3; k++) val[k] = a[k][k];
    for (int i = 0; i < 2; i++) for (int j = i + 1; j < 3; j++) if (val[j] > val[i]) {
        double t = val[i]; val[i] = val[j]; val[j] = t;
        for (int k = 0; k < 3; k++) { t = v[i][k]; v[i][k] = v[j][k]; v[j][k] = t; }
    }
}
static int lf_cmp(const void* a, const void* b) { float x = *(const float*)a, y = *(const float*)b; return x < y ? -1 : x > y; }

// ------------------------------------------------------------------ rig
// A rig's bone name without the exporter's node number: glTF exports such as
// the licensed raptor's name bones "Spine_1_03" or "UpperLeg.R_060", and the
// tables name them "Spine_1" and "UpperLeg.R". A trailing "_" and digits is
// dropped, unless nothing would be left.
static void loco_bone_base(const char* low, char* out, size_t n) {
    snprintf(out, n, "%s", low);
    size_t len = strlen(out), k = len;
    while (k > 0 && out[k - 1] >= '0' && out[k - 1] <= '9') k--;
    if (k < len && k > 1 && out[k - 1] == '_') out[k - 1] = 0;
}
static bool loco_name_in(const char* name, const char* list) {
    char low[32]; loco_bone_base(name, low, sizeof low);
    char buf[256]; snprintf(buf, sizeof buf, "%s", list);
    for (char* p = buf; *p; p++) if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + 32);
    for (char* tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) if (!strcmp(tok, low)) return true;
    return false;
}
static int loco_find_joint(const DinoFile* d, const char* name) {
    char low[32]; snprintf(low, sizeof low, "%s", name);
    for (char* p = low; *p; p++) if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + 32);
    for (int j = 0; j < d->njoint; j++) { char base[32]; loco_bone_base(d->low[j], base, sizeof base); if (!strcmp(base, low)) return j; }
    return -1;
}
// The table whose probe bone the rig has, or NULL.
static const LocoTable* loco_pick_table(const DinoFile* d) {
    for (size_t k = 0; k < sizeof LOCO_TABLES / sizeof LOCO_TABLES[0]; k++) if (loco_find_joint(d, LOCO_TABLES[k]->probe) >= 0) return LOCO_TABLES[k];
    return NULL;
}
// Body pose (rotation, origin) of body b in a model-space pose G (df_globals).
static void loco_body_pose(const DinoFile* d, const LocoBody* b, const float (*G)[12], b3Quat* q, b3Vec3* p) {
    float m[12]; df_mul(G[b->joint], d->inv_bind[b->joint], m);
    *q = lq_from_m34(m); *p = lm34_point(m, b->bind);
}

// Collision shape, volume and tip fitted to vertices (body frame); foot: a box.
static void loco_fit_shape(LocoShape* b, bool foot, const b3Vec3* pts, int n) {
    double mean[3] = {0, 0, 0}, cov[3][3] = {{0}}, val[3], ax[3][3];
    for (int i = 0; i < n; i++) { mean[0] += pts[i].x; mean[1] += pts[i].y; mean[2] += pts[i].z; }
    for (int k = 0; k < 3; k++) mean[k] /= n;
    for (int i = 0; i < n; i++) {
        double d[3] = {pts[i].x - mean[0], pts[i].y - mean[1], pts[i].z - mean[2]};
        for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) cov[r][c] += d[r] * d[c] / n;
    }
    lm3_eigen(cov, val, ax);
    // Extents along each axis from the 3rd to the 97th percentile, so a claw or a spike does not inflate the body.
    float* proj = (float*)malloc(sizeof(float) * n); float lo[3], hi[3];
    for (int k = 0; k < 3; k++) {
        for (int i = 0; i < n; i++) proj[i] = (float)((pts[i].x - mean[0]) * ax[k][0] + (pts[i].y - mean[1]) * ax[k][1] + (pts[i].z - mean[2]) * ax[k][2]);
        qsort(proj, n, sizeof(float), lf_cmp);
        lo[k] = proj[(int)(0.03f * (n - 1))]; hi[k] = proj[(int)(0.97f * (n - 1))];
    }
    free(proj);
    b3Vec3 e[3], c = lv((float)mean[0], (float)mean[1], (float)mean[2]); float h[3];
    for (int k = 0; k < 3; k++) {
        e[k] = lv((float)ax[k][0], (float)ax[k][1], (float)ax[k][2]);
        h[k] = fmaxf(0.5f * (hi[k] - lo[k]), 0.01f);
        c = b3Add(c, b3MulSV(0.5f * (hi[k] + lo[k]), e[k]));
    }
    if (lv_dot(b3Cross(e[0], e[1]), e[2]) < 0) e[2] = b3Neg(e[2]);   // right-handed
    b->com = c;
    if (foot) {
        // Feet are boxes so they stand flat. The box keeps the mesh's long axis but its up axis is model
        // up, so the sole is level at the bind pose.
        b3Vec3 fwd = b3Normalize(lv(e[0].x, 0, e[0].z)), up = lv(0, 1, 0), side = b3Cross(fwd, up);
        float hx = 0, hy = 0, hz = 0;
        for (int i = 0; i < n; i++) {
            b3Vec3 d = b3Sub(pts[i], c);
            hx = fmaxf(hx, fabsf(lv_dot(d, fwd))); hy = fmaxf(hy, fabsf(lv_dot(d, up))); hz = fmaxf(hz, fabsf(lv_dot(d, side)));
        }
        // Percentile-free extents here: a foot is small and its toe tips are its contact points.
        b->type = LS_BOX; b->box_c = c; b->box_h = lv(fmaxf(hx, 0.02f), fmaxf(hy, 0.01f), fmaxf(hz, 0.02f));
        float m[12] = {fwd.x, up.x, side.x, 0, fwd.y, up.y, side.y, 0, fwd.z, up.z, side.z, 0};
        b->box_q = lq_from_m34(m);
        b->volume = 8.0f * b->box_h.x * b->box_h.y * b->box_h.z * 0.6f;   // a foot fills about 60% of its box
        b->tip = c;
    } else {
        // A capsule along the long axis whose cross-section has the area of the mesh's (an ellipse of the two
        // short half extents), so its volume is close to the limb's.
        float r = sqrtf(h[1] * h[2]), s = fmaxf(h[0] - r, 0.0f);
        b->type = LS_CAPSULE; b->radius = r;
        b->c1 = b3MulSub(c, s, e[0]); b->c2 = b3MulAdd(c, s, e[0]);
        b->volume = (float)M_PI * r * r * 2.0f * s + 4.0f / 3.0f * (float)M_PI * r * r * r;
        // The tip is the capsule end farther from the joint.
        b3Vec3 t1 = b3MulSub(c, s + r, e[0]), t2 = b3MulAdd(c, s + r, e[0]);
        b->tip = lv_len2(t1) > lv_len2(t2) ? t1 : t2;
    }
    b->mass = b->volume * LOCO_DENSITY;
}

// Joint frames and limits of body bi against its parent from every frame of the given clips.
static void loco_fit_joint(const DinoFile* d, LocoRig* rig, int bi, const int* clips, int nclips) {
    LocoBody* b = &rig->body[bi]; const LocoBody* pa = &rig->body[b->parent];
    int total = 0;
    for (int k = 0; k < nclips; k++) total += d->clip[clips[k]].nframes;
    b3Quat* rel = (b3Quat*)malloc(sizeof(b3Quat) * total); int n = 0;
    float (*G)[12] = (float (*)[12])malloc(sizeof(float) * 12 * d->njoint);
    for (int k = 0; k < nclips; k++) {
        const DfClip* C = &d->clip[clips[k]];
        for (int f = 0; f < C->nframes; f++) {
            df_globals(d, C->frames + (size_t)f * d->njoint * 8, G);
            b3Quat qa, qb; b3Vec3 pa_, pb_;
            loco_body_pose(d, pa, (const float (*)[12])G, &qa, &pa_); loco_body_pose(d, b, (const float (*)[12])G, &qb, &pb_);
            b3Quat r = lq_inv_mul(qa, qb);
            if (n && r.v.x * rel[0].v.x + r.v.y * rel[0].v.y + r.v.z * rel[0].v.z + r.s * rel[0].s < 0) r = b3NegateQuat(r);
            rel[n++] = r;
        }
    }
    free(G);
    b3Quat mean = lq_make(0, 0, 0, 0);
    for (int i = 0; i < n; i++) { mean.v = b3Add(mean.v, rel[i].v); mean.s += rel[i].s; }
    b->q_mean = n ? b3NormalizeQuat(mean) : b3Quat_identity;
    // Rotation vectors of the deviation from the mean; their principal axis is the hinge axis candidate.
    double cov[3][3] = {{0}}, val[3], ax[3][3];
    for (int i = 0; i < n; i++) {
        b3Vec3 w = lq_log(lq_inv_mul(b->q_mean, rel[i])); double x[3] = {w.x, w.y, w.z};
        for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) cov[r][c] += x[r] * x[c];
    }
    lm3_eigen(cov, val, ax);
    double tot = val[0] + val[1] + val[2];
    b->hinge_fit = tot > 1e-12 ? (float)(val[0] / tot) : 1.0f;
    b3Vec3 bone = lv_len2(b->com) > 1e-8f ? b3Normalize(b->com) : lv(1, 0, 0);
    if ((b->flags & LB_HINGE) && b->hinge_fit >= LOCO_HINGE_FIT) {
        b->jtype = LJ_HINGE; b->dof = 1;
        // Frame A's rotation is mean * qf, so the deviation's axis (in the mean-rotated parent frame) maps to z.
        // A knee or toe joint the clips never move hinges about the model's lateral axis.
        b3Vec3 axis = tot > 1e-8 ? lv((float)ax[0][0], (float)ax[0][1], (float)ax[0][2]) : lq_rot(lq_conj(b->q_mean), lv(0, 0, 1));
        if (lv_dot(axis, lv(0, 0, 1)) < 0) axis = b3Neg(axis);   // a consistent sign: knees bend the same way on both sides
        b->qf_b = lq_between(lv(0, 0, 1), axis);
    } else {
        b->jtype = LJ_BALL; b->dof = 3;
        b->qf_b = lq_between(lv(0, 0, 1), bone);
    }
    b->qf_a = lq_mul(b->q_mean, b->qf_b);
    // Limits: the range of the deviation in the joint frame, plus the margin.
    float cone = 0, tlo = 0, thi = 0, alo = 0, ahi = 0;
    for (int i = 0; i < n; i++) {
        b3Quat dj = lq_mul(lq_conj(b->qf_b), lq_mul(lq_inv_mul(b->q_mean, rel[i]), b->qf_b));
        float tw = lq_twist_z(dj);
        b3Vec3 z = lq_rot(dj, lv(0, 0, 1));
        float sw = acosf(fminf(fmaxf(z.z, -1.0f), 1.0f));
        cone = fmaxf(cone, sw); tlo = fminf(tlo, tw); thi = fmaxf(thi, tw);
        alo = fminf(alo, tw); ahi = fmaxf(ahi, tw);
    }
    free(rel);
    const float pi99 = 0.99f * (float)M_PI;
    b->cone = fminf(cone + LOCO_LIMIT_MARGIN, (float)M_PI * 0.95f);
    b->twist_lo = fmaxf(tlo - LOCO_LIMIT_MARGIN, -pi99); b->twist_hi = fminf(thi + LOCO_LIMIT_MARGIN, pi99);
    b->lo = fmaxf(alo - LOCO_LIMIT_MARGIN, -pi99); b->hi = fminf(ahi + LOCO_LIMIT_MARGIN, pi99);
    b->anchor_a = b3Sub(b->bind, pa->bind);
}

// Folds a uniform export scale out of the bind space. A glTF rig exported with
// a scale on its armature (the licensed raptor's skin is in centimetres under
// a 0.01 root) gives skinning matrices G * inv_bind that scale by that factor,
// so shapes fitted to bind-space vertices come out 100 times too big and the
// rotations read from those matrices are not pure rotations. With s the
// skinning matrices' scale at the rest pose, the vertices become s times
// larger and every inverse bind's linear part 1/s times: G * inv_bind then maps
// the new bind space to model space with no scale, and skinning is unchanged.
// Only joints that carry vertices count (the raptor's unskinned root has its
// own scale and shapes nothing). Returns s (1 for a rig already in model
// units); 0 when those joints' scales differ by more than 1%, which the
// builder cannot represent.
static float loco_unscale_bind(DinoFile* d) {
    static float G[DF_MAX_JOINTS][12]; float local[DF_MAX_JOINTS * 8];
    static bool skinned[DF_MAX_JOINTS];
    memset(skinned, 0, sizeof skinned);
    for (int i = 0; i < d->nvert; i++) for (int k = 0; k < 4; k++) if (d->vert[i].w[k]) skinned[d->vert[i].j[k]] = true;
    for (int j = 0; j < d->njoint; j++) memcpy(local + j * 8, d->rest[j], sizeof d->rest[j]);
    df_globals(d, local, G);
    float lo = 1e30f, hi = 0;
    for (int j = 0; j < d->njoint; j++) {
        if (!skinned[j]) continue;
        float m[12]; df_mul(G[j], d->inv_bind[j], m);
        float det = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]);
        float sj = cbrtf(fabsf(det));
        lo = fminf(lo, sj); hi = fmaxf(hi, sj);
    }
    if (hi <= 0 || hi > lo * 1.01f) return 0;
    float sc = 0.5f * (lo + hi);
    if (fabsf(sc - 1.0f) < 1e-3f) return 1.0f;
    for (int i = 0; i < d->nvert; i++) for (int k = 0; k < 3; k++) d->vert[i].p[k] *= sc;
    for (int j = 0; j < d->njoint; j++) for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) d->inv_bind[j][r * 4 + c] /= sc;
    return sc;
}

// Builds the rig from a .dino. clips: the clip indices whose range sets the
// joint limits (the training clip and the other gaits). Returns false with a
// message in err on a rig the tables do not cover.
static bool loco_rig_build(LocoRig* rig, const DinoFile* d, const int* clips, int nclips, float min_range, char* err, size_t errn) {
    memset(rig, 0, sizeof *rig);
    const LocoTable* t = loco_pick_table(d);
    if (!t) { snprintf(err, errn, "no bone table matches this rig (add one to loco/loco_rig.h)"); return false; }
    snprintf(rig->table, sizeof rig->table, "%s", t->name);
    // Joints named in the table, then every other joint to its nearest named ancestor.
    int nt = 0, root_of[LOCO_MAX_BODIES];
    for (int k = 0; k < LOCO_MAX_BODIES && t->body[k].name; k++) { root_of[k] = -1; nt++; }
    int jb[DF_MAX_JOINTS];
    for (int j = 0; j < d->njoint; j++) {
        jb[j] = -1;
        for (int k = 0; k < nt; k++) if (loco_name_in(d->low[j], t->body[k].bones)) { jb[j] = k; if (root_of[k] < 0) root_of[k] = j; break; }
        if (jb[j] < 0 && d->parent[j] >= 0) jb[j] = jb[d->parent[j]];
    }
    if (root_of[0] < 0) { snprintf(err, errn, "rig has no %s bone", t->body[0].bones); return false; }
    // Table parent of each present body: the body of the nearest ancestor joint outside it.
    int par[LOCO_MAX_BODIES], cnt[LOCO_MAX_BODIES] = {0}, flags[LOCO_MAX_BODIES];
    for (int k = 0; k < nt; k++) {
        par[k] = -1; flags[k] = t->body[k].flags;
        if (root_of[k] < 0) continue;
        for (int j = d->parent[root_of[k]]; j >= 0; j = d->parent[j]) if (jb[j] >= 0 && jb[j] != k) { par[k] = jb[j]; break; }
        if (k > 0 && par[k] < 0) { snprintf(err, errn, "body %s has no parent body", t->body[k].name); return false; }
    }
    for (int i = 0; i < d->nvert; i++) {
        const DfVert* v = &d->vert[i]; int best = 0;
        for (int q = 1; q < 4; q++) if (v->w[q] > v->w[best]) best = q;
        int k = jb[v->j[best]]; if (k >= 0) cnt[k]++;
    }
    // Merge bodies without mesh into their parents (children first), passing on the tip flags.
    int map[LOCO_MAX_BODIES];
    for (int k = 0; k < nt; k++) map[k] = root_of[k] < 0 ? -1 : k;
    for (int k = nt - 1; k > 0; k--) if (map[k] >= 0 && cnt[k] < LOCO_MIN_VERTS) {
        int p = par[k]; while (map[p] != p) p = par[p];
        map[k] = p; cnt[p] += cnt[k]; flags[p] |= flags[k] & (LB_EE | LB_FOOT | LB_CONTACT);
    }
    for (int k = 0; k < nt; k++) if (map[k] >= 0) { int m = map[k]; while (map[m] != m) m = map[m]; map[k] = m; }
    int part[LOCO_MAX_BODIES];   // each table group's body before welding: the groups that keep their own shapes
    for (int k = 0; k < nt; k++) part[k] = map[k];
    // Weld joints the clips do not move (under min_range; 0 welds none) into their parents, children first: a joint
    // with no range has nothing to track, and a light body hinged to a heavy one is where Box3D's soft
    // limits give way (the test rig's toes, held at a fixed angle in every clip, flipped up to 90 degrees).
    float range[LOCO_MAX_BODIES] = {0};
    {
        float (*G)[12] = (float (*)[12])malloc(sizeof(float) * 12 * d->njoint);
        b3Quat first[LOCO_MAX_BODIES]; bool have = false;
        for (int c = 0; c < nclips; c++) {
            const DfClip* C = &d->clip[clips[c]];
            for (int f = 0; f < C->nframes; f++) {
                df_globals(d, C->frames + (size_t)f * d->njoint * 8, G);
                b3Quat q[LOCO_MAX_BODIES];
                for (int k = 0; k < nt; k++) if (root_of[k] >= 0) { float m[12]; df_mul(G[root_of[k]], d->inv_bind[root_of[k]], m); q[k] = lq_from_m34(m); }
                for (int k = 1; k < nt; k++) if (root_of[k] >= 0 && par[k] >= 0) {
                    b3Quat rel = lq_inv_mul(q[par[k]], q[k]);
                    if (!have) first[k] = rel; else range[k] = fmaxf(range[k], lq_angle(first[k], rel));
                }
                have = true;
            }
        }
        free(G);
    }
    for (int k = nt - 1; k > 0; k--) if (map[k] == k && range[k] < min_range) {
        int p = map[par[k]];
        for (int j = 0; j < nt; j++) if (map[j] == k) map[j] = p;
        cnt[p] += cnt[k]; flags[p] |= flags[k] & (LB_EE | LB_FOOT | LB_CONTACT);
    }
    int idx[LOCO_MAX_BODIES];
    for (int k = 0; k < nt; k++) idx[k] = -1;
    for (int k = 0; k < nt; k++) if (map[k] == k) {
        LocoBody* b = &rig->body[rig->nbody]; idx[k] = rig->nbody++;
        snprintf(b->name, sizeof b->name, "%s", t->body[k].name);
        b->flags = flags[k]; b->joint = root_of[k]; b->nvert = cnt[k];
        b->parent = k == 0 ? -1 : idx[map[par[k]]];
    }
    for (int j = 0; j < d->njoint; j++) rig->joint_body[j] = jb[j] < 0 ? -1 : idx[map[jb[j]]];
    // Bind positions, then each body's vertices in its own frame.
    for (int b = 0; b < rig->nbody; b++) {
        float inv[12]; lm34_inverse(d->inv_bind[rig->body[b].joint], inv);
        rig->body[b].bind = lv(inv[3], inv[7], inv[11]);
    }
    // Shapes: one per group that survived the merge of empty groups, in its body's frame. A body's mass,
    // centre and end-effector tip (the tip of the part that carries the end-effector flag) follow.
    b3Vec3* pts = (b3Vec3*)malloc(sizeof(b3Vec3) * (d->nvert > 0 ? d->nvert : 1));
    for (int k = 0; k < nt; k++) if (part[k] == k) {
        LocoBody* B = &rig->body[idx[map[k]]]; int n = 0;
        if (B->nshape >= LOCO_MAX_SHAPES) { snprintf(err, errn, "body %s welds more than %d groups", B->name, LOCO_MAX_SHAPES); free(pts); return false; }
        for (int i = 0; i < d->nvert; i++) {
            const DfVert* v = &d->vert[i]; int best = 0;
            for (int q = 1; q < 4; q++) if (v->w[q] > v->w[best]) best = q;
            int g = jb[v->j[best]];
            if (g >= 0 && part[g] == k) pts[n++] = b3Sub(lv(v->p[0], v->p[1], v->p[2]), B->bind);
        }
        if (n < LOCO_MIN_VERTS) continue;
        int pf = 0;
        for (int g = 0; g < nt; g++) if (part[g] == k) pf |= t->body[g].flags;
        LocoShape* S = &B->shape[B->nshape++];
        loco_fit_shape(S, (pf & LB_FOOT) != 0, pts, n);
        B->com = b3MulAdd(B->com, S->mass, S->com); B->mass += S->mass; B->volume += S->volume;
        if (pf & LB_EE) B->ee = S->tip;
    }
    free(pts);
    for (int b = 0; b < rig->nbody; b++) {
        LocoBody* B = &rig->body[b];
        if (B->nshape == 0) { snprintf(err, errn, "body %s has no mesh", B->name); return false; }
        B->com = b3MulSV(1.0f / B->mass, B->com);
        rig->total_mass += B->mass;
        if ((B->flags & LB_EE) && rig->nee < LOCO_MAX_EE) rig->ee_body[rig->nee++] = b;
    }
    for (int b = 1; b < rig->nbody; b++) {
        loco_fit_joint(d, rig, b, clips, nclips);
        rig->body[b].act0 = rig->ndof; rig->ndof += rig->body[b].dof;
    }
    rig->length = d->length; rig->height = d->height;
    return true;
}

// ------------------------------------------------------------------ reference motion
static void loco_ref_free(LocoRef* r) {
    free(r->pos); free(r->rot); free(r->vel); free(r->angvel);
    memset(r, 0, sizeof *r);
}
static bool loco_ref_build(LocoRef* r, const DinoFile* d, const LocoRig* rig, int clip) {
    memset(r, 0, sizeof *r);
    const DfClip* C = &d->clip[clip];
    if (C->nframes < 3 || C->duration <= 0) return false;
    snprintf(r->clip, sizeof r->clip, "%s", C->name);
    r->period = C->nframes - 1; r->nbody = rig->nbody; r->dt = C->duration / r->period; r->duration = C->duration; r->speed = C->speed;
    size_t n = (size_t)r->period * rig->nbody;
    r->pos = (b3Vec3*)malloc(sizeof(b3Vec3) * n); r->rot = (b3Quat*)malloc(sizeof(b3Quat) * n);
    r->vel = (b3Vec3*)malloc(sizeof(b3Vec3) * n); r->angvel = (b3Vec3*)malloc(sizeof(b3Vec3) * n);
    float (*G)[12] = (float (*)[12])malloc(sizeof(float) * 12 * d->njoint);
    for (int f = 0; f < r->period; f++) {
        df_globals(d, C->frames + (size_t)f * d->njoint * 8, G);
        for (int b = 0; b < rig->nbody; b++) loco_body_pose(d, &rig->body[b], (const float (*)[12])G, &r->rot[f * rig->nbody + b], &r->pos[f * rig->nbody + b]);
    }
    free(G);
    // Central differences over the cycle; the clip's forward speed is added to every body.
    for (int f = 0; f < r->period; f++) {
        int fp = (f + 1) % r->period, fm = (f + r->period - 1) % r->period;
        for (int b = 0; b < rig->nbody; b++) {
            int i = f * rig->nbody + b;
            b3Vec3 v = b3MulSV(0.5f / r->dt, b3Sub(r->pos[fp * rig->nbody + b], r->pos[fm * rig->nbody + b]));
            v.x += r->speed;
            r->vel[i] = v;
            b3Quat dq = lq_mul(r->rot[fp * rig->nbody + b], lq_conj(r->rot[fm * rig->nbody + b]));
            r->angvel[i] = b3MulSV(0.5f / r->dt, lq_log(dq));
        }
    }
    return true;
}
// Reference body poses and velocities at time t (seconds from the cycle start,
// any t >= 0) in model space, with the forward travel speed * t added.
static void loco_ref_sample(const LocoRef* r, double t, b3Vec3* pos, b3Quat* rot, b3Vec3* vel, b3Vec3* angvel) {
    double s = t / r->dt; long i = (long)floor(s); float a = (float)(s - (double)i);
    int i0 = (int)(i % r->period), i1 = (i0 + 1) % r->period; if (i0 < 0) { i0 += r->period; i1 = (i0 + 1) % r->period; }
    float dx = (float)(r->speed * t);
    for (int b = 0; b < r->nbody; b++) {
        int k0 = i0 * r->nbody + b, k1 = i1 * r->nbody + b;
        if (pos) { pos[b] = lv_lerp(r->pos[k0], r->pos[k1], a); pos[b].x += dx; }
        if (rot) { b3Quat q1 = r->rot[k1]; const b3Quat q0 = r->rot[k0]; if (q0.v.x * q1.v.x + q0.v.y * q1.v.y + q0.v.z * q1.v.z + q0.s * q1.s < 0) q1 = b3NegateQuat(q1); rot[b] = lq_nlerp(q0, q1, a); }
        if (vel) vel[b] = lv_lerp(r->vel[k0], r->vel[k1], a);
        if (angvel) angvel[b] = lv_lerp(r->angvel[k0], r->angvel[k1], a);
    }
}
// Joint deviation of body b (rotation of frame B relative to frame A, as the
// joint measures it) from the two bodies' world rotations.
static inline b3Quat loco_joint_dev(const LocoBody* b, b3Quat qa, b3Quat qb) {
    return lq_mul(lq_conj(b->qf_a), lq_mul(lq_inv_mul(qa, qb), b->qf_b));
}

// Lowest point (y, model space) of the bodies' shapes in a pose.
static float loco_lowest(const LocoRig* rig, const b3Vec3* pos, const b3Quat* rot) {
    float lo = 1e9f;
    for (int b = 0; b < rig->nbody; b++) {
        for (int k = 0; k < rig->body[b].nshape; k++) {
            const LocoShape* S = &rig->body[b].shape[k];
            if (S->type == LS_BOX) {
                for (int c = 0; c < 8; c++) {
                    b3Vec3 v = lv(c & 1 ? S->box_h.x : -S->box_h.x, c & 2 ? S->box_h.y : -S->box_h.y, c & 4 ? S->box_h.z : -S->box_h.z);
                    lo = fminf(lo, b3Add(pos[b], lq_rot(rot[b], b3Add(S->box_c, lq_rot(S->box_q, v)))).y);
                }
            } else {
                lo = fminf(lo, b3Add(pos[b], lq_rot(rot[b], S->c1)).y - S->radius);
                lo = fminf(lo, b3Add(pos[b], lq_rot(rot[b], S->c2)).y - S->radius);
            }
        }
    }
    return lo;
}

// Index of the clip called name, or -1.
static int loco_clip_index(const DinoFile* d, const char* name) {
    for (int c = 0; c < d->nclip; c++) if (!strcmp(d->clip[c].name, name)) return c;
    return -1;
}
// Clips whose range sets the joint limits: the training clip and the gaits and idles.
static int loco_limit_clips(const DinoFile* d, int train, int* out) {
    static const char* const GAITS[] = {"idle", "idle2", "walk", "trot", "run"};
    int n = 0; out[n++] = train;
    for (size_t g = 0; g < sizeof GAITS / sizeof GAITS[0]; g++) { int c = loco_clip_index(d, GAITS[g]); if (c >= 0 && c != train) out[n++] = c; }
    return n;
}
#endif
