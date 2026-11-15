// Foot IK for the dinosaurs (render/dino.c), without GL so tests can run it
// on synthetic skeletons and ground (tests/test_dino_ik.c).
//   - dino_find_legs: legs by joint name (hip, knee, ankle, and toes rigged
//     outside the leg), the joints each leg moves, and the rest measures the
//     body tilt and the IK read (mean hip height, front and hind hip x, the
//     hips' half spread).
//   - dino_foot_ik: over animated model-space joint frames and the body
//     transform, keeps each foot at its animated height above the real ground
//     under it (cret_ground_at), drops the pelvis so the lowest foot reaches
//     (no further than the highest foot can fold), bends two-bone legs in
//     their bend plane and tilts planted feet toward the ground normal.
// render/dino.c includes dino_ik.c (the player's build lists dino.c only);
// the test builds dino_ik.c on its own with a stub cret_ground_at.
#ifndef CRET_DINO_IK_H
#define CRET_DINO_IK_H
#include <stdbool.h>
#include <stdint.h>
#include "../sim.h"

#define DINO_MAX_JOINTS 256

// Legs for foot IK: hip, knee and ankle joints from the rigs' names; the foot
// (the ankle's subtree, plus toes rigged apart) moves rigidly with the ankle.
typedef struct DinoLegs {
    int nleg; struct { int hip, knee, ankle; bool front; } leg[4];
    uint16_t leg_bits[DINO_MAX_JOINTS];     // bit l: under leg l's hip, 4+l: under its knee, 8+l: its foot
    float hip_y, front_x, hind_x, hip_z;    // rest: mean hip height, front and hind hip x, half the hips' spread
} DinoLegs;

// Finds the legs of a skeleton: njoint joints with parents (parent[j] < j),
// rest local T, R (xyzw), uniform S, and lower-case names. height (m) is the
// model's, used for hip_y when no leg is found.
void dino_find_legs(DinoLegs* L, int njoint, const int* parent, const float (*rest)[8], float height, char (*nm)[32]);
// Foot IK over the animated model-space frames G (3x4 row-major, njoint of
// them) and the body transform M (3x4, linear part gs times a rotation),
// both changed in place. pelvis is the smoothed pelvis drop (m) kept between
// frames; wt fades the whole thing (air, water, death); dt is the frame time;
// crouch (m): the body sits this much lower than the animation has it, on
// legs that fold to keep the feet where they are.
void dino_foot_ik(const CretSim* sim, const DinoLegs* L, int njoint, float* pelvis, float (*G)[12], float* M, float gs, float wt, float dt, float crouch);
#endif
