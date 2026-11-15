// Procedural animation layers over the dinosaurs' keyframed clips
// (render/dino.c calls these from animate). Pure math: no GL, no raylib, no
// sim writes, so tests/test_dino_proc.c checks it headless. Presentation
// only: everything here reads the (interpolated) sim and the clip pose and
// shapes the drawn skeleton; the sim, physics, hitboxes and the RL
// observations never see it.
//
// Layers (each with a toggle bit in cret_dino_proc_flags; console
// "anim <layer> [on|off]", keypad 1..8, keypad 0 all; env CRET_ANIM="-tail,-look"):
//   lean    the body rolls into turns by lean_k x atan(v x yaw rate / g) (the
//           bank a runner needs), within lean_max. Off: the older fixed lean.
//   slope   the chest pitches toward the slope under the two ground probes
//           (hips and chest) beyond what the whole body takes (BODY_TILT).
//   spine   the spine bends into the turn: a yaw spread over the joints from
//           the hips to the neck base (front share) and, opposite, from the
//           hips to the tail base (rear share): a C-shape around the turn.
//   tail    the tail is a chain of critically damped springs, one per bend
//           pivot, each following the one before it: it trails the body's
//           yaw rate (swings out of turns), droops with vertical and fore-aft
//           acceleration, lifts a little at a run, sways slowly at rest.
//   head    head stabilisation: the head's world pitch and yaw (from the
//           clip, the body pitch and the procedural body yaws) are low-passed
//           and the neck takes back a share of the fast part, so the head
//           rides steadier than the body while moving.
//   look    look-at: the head turns (yaw and pitch, within limits) toward a
//           target the view picks: a threat, prey or a herd mate nearby.
//   breath  breathing: the chest joint swells and lifts on a cycle whose rate
//           and depth rise as stamina is spent and while sprinting.
//   feet    foot IK (two-bone, every leg chain the rig has, bipeds and
//           quadrupeds) with hip height adjustment and foot locking: a
//           planted foot keeps its world spot until it lifts or strays too far.
// All springs are integrated in closed form (exact for a constant target),
// so they are stable and framerate-independent for any dt.
#ifndef CRET_DINO_PROC_H
#define CRET_DINO_PROC_H
#include <stdbool.h>
#include <stdint.h>

#define DPROC_CHAIN 6          // = DINO_CHAIN (render/dino.h): tail pivots
#define DPROC_SPINE 8          // spine joints per side
#define DPROC_LEGS 4

enum {
    DPROC_LEAN = 1 << 0, DPROC_SLOPE = 1 << 1, DPROC_SPINE_BEND = 1 << 2, DPROC_TAIL = 1 << 3,
    DPROC_HEAD = 1 << 4, DPROC_LOOK = 1 << 5, DPROC_BREATH = 1 << 6, DPROC_FEET = 1 << 7,
    DPROC_LAYERS = 8, DPROC_ALL = (1 << 8) - 1,
};
extern const char* const DPROC_NAMES[DPROC_LAYERS];   // "lean", "slope", ... (bit order)
extern unsigned cret_dino_proc_flags;                  // DPROC_* bits on (default DPROC_ALL)
// Applies a CRET_ANIM-style list ("-tail,-look", "+feet", "none", "all") to flags; returns the result.
unsigned dproc_parse_flags(const char* spec, unsigned flags);
// Layer index by name (-1: unknown; "all" -> DPROC_LAYERS).
int dproc_layer(const char* name);

// ---------------------------------------------------------------- springs
// Critically damped spring: x follows target with angular frequency omega
// (rad/s; settles in about 4.7 / omega s, never overshoots). Exact update.
typedef struct { float x, v; } DpSpring;
void dp_spring(DpSpring* s, float target, float omega, float dt);

// ---------------------------------------------------------------- per-species data
// Adult values (radians, seconds, rad/s, Hz). dproc_params scales them to a
// juvenile: spring rates by 1/sqrt(size) (a shorter pendulum swings faster),
// breathing by mass^-1/4 (mass ~ size^3), lean and limits unchanged.
typedef struct DinoProcParams {
    float lean_k, lean_max, lean_omega;            // share of the bank angle atan(v w / g), limit, spring rate
    float chest_k, chest_max, chest_omega;         // chest pitch: share of the slope left over by the body pitch
    float bend_k, bend_max, bend_omega, bend_front;// spine yaw per rad/s of yaw rate, limit, rate; share at the chest (rest at the hips)
    float tail_lag, tail_acc, tail_vacc, tail_lift;// tail yaw per rad/s of yaw rate; pitch per m/s^2 fore-aft, per m/s^2 vertical; lift at a run
    float tail_omega, tail_tip, tail_yaw_max, tail_pitch_max;   // base pivot rate, tip rate as a share of it, limits
    float sway_amp, sway_hz;                       // idle tail sway
    float stab_k, stab_omega, stab_max;            // head stabilisation: share taken back, low-pass rate, limit
    float look_yaw_max, look_pitch_max, look_omega, look_range;  // look-at limits, rate, range (body lengths)
    float breath_hz, breath_amp;                   // resting breaths per second, chest swell (share of size)
} DinoProcParams;
extern const DinoProcParams DPROC_SPECIES[];        // CRET_SPECIES entries (render/dino_proc.c)
void dproc_params(int species, float size_scale, DinoProcParams* out);

// ---------------------------------------------------------------- rig
// The spine joints the bend and the chest pitch turn, from the skeleton's
// parents and the neck/tail weights render/dino.c finds: front = the joints
// between the hips' common ancestor and the neck base (pelvis excluded),
// rear = those between it and the tail base. chest: the joint that breathes.
typedef struct DinoProcRig {
    int front[DPROC_SPINE], nfront, rear[DPROC_SPINE], nrear, chest, neck_base, tail_base, pelvis;
} DinoProcRig;
void dproc_rig(DinoProcRig* r, const int* parent, int njoint, const float* neck_w, const float* tail_w, int head_j, int hip_j);

// ---------------------------------------------------------------- per-slot state
typedef struct DpFootLock { bool on; float x, z, w; } DpFootLock;
typedef struct DinoProcState {
    bool init;
    float yaw_acc, last_yaw;           // unwrapped heading (re-based near 0)
    DpSpring yaw, vel, vy;             // filtered heading (v: yaw rate), ground speed (v: fore-aft accel), height (v: vertical speed)
    DpSpring vacc;                     // vertical acceleration, filtered
    float last_vy;
    DpSpring lean, bend, chest;
    DpSpring tail_yaw[DPROC_CHAIN], tail_pitch[DPROC_CHAIN];
    float sway_t;
    DpSpring stab_e, stab_a;           // low-passed head world elevation, azimuth
    float last_e, last_a; bool head_init;
    DpSpring look_yaw, look_pitch;
    float breath_ph; DpSpring breath_rate, breath_depth;
    DpFootLock lock[DPROC_LEGS];
} DinoProcState;

typedef struct DinoProcIn {
    float dt;
    float yaw, speed, y;               // heading (rad), ground speed (m/s), height (m): the drawn subject
    float walk, sprint;                // the species' gait speeds at this size (m/s)
    float slope, body_pitch;           // ground pitch between the probes, pitch the body already takes (rad)
    float stamina; bool sprinting;     // 0..1
    float active;                      // 0..1 how much the layers apply (0: dead, knocked down)
    float head_free;                   // 0..1 how free the head is (0 while an action or the aim owns it)
    float move;                        // 0..1 moving (gait weight)
    bool look_on; float look_yaw, look_pitch;   // look target: yaw from the heading, pitch above level (rad)
    int ntail;                         // tail pivots in use
    int seed;
} DinoProcIn;
typedef struct DinoProcOut {
    float lean;                        // roll (rad; same sense as the body roll)
    float chest_pitch;                 // spread over the front spine (+ lifts the front)
    float bend_front, bend_rear;       // yaw spread over the front / rear spine (body yaw sense)
    float tail_yaw[DPROC_CHAIN], tail_pitch[DPROC_CHAIN];   // per pivot, totals (dino.c divides by the pivots)
    float look_yaw, look_pitch;        // neck yaw and pitch toward the target
    float breath;                      // chest swell this frame (share of size, >= 0)
} DinoProcOut;
// Advances the body-level layers one frame.
void dproc_step(DinoProcState* s, const DinoProcParams* p, const DinoProcIn* in, unsigned flags, DinoProcOut* out);
// Head stabilisation, after the clip pose is known: elev / azim are the
// neck-base-to-head angles in model space (from the clip), body_pitch and
// body_yaw the procedural body turns on top of them (not the heading).
// Writes the neck pitch and yaw that take back stab_k of the fast part.
void dproc_head(DinoProcState* s, const DinoProcParams* p, float elev, float azim, float body_pitch, float body_yaw,
                float weight, float dt, unsigned flags, float* neck_pitch, float* neck_yaw);

// ---------------------------------------------------------------- legs
// Two-bone IK: hip H, knee K and ankle P as animated, ankle target W. Writes
// the knee K2 and the reached ankle T (W pulled within reach: |a - b| ..
// 0.999 (a + b) from the hip); the knee stays on the side of the hip-ankle
// line the animation had it (pole; a straight leg bends in the plane of u x +z), and both bone lengths are kept. False if
// a bone or the hip-target distance is degenerate (nothing written).
bool dp_two_bone(const float H[3], const float K[3], const float P[3], const float W[3], float K2[3], float T[3]);
// Foot lock: x, z the foot's world spot this frame as animated; plant 0..1
// (on the ground in the clip). Locks when planted, releases when it lifts or
// strays max_off from the locked spot; writes the world offset to add.
void dp_foot_lock(DpFootLock* L, float x, float z, float plant, float max_off, float dt, float* off_x, float* off_z);
#endif
