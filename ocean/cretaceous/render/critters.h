// Small animals (critters.h in the sim) as procedural, animated models:
// a mouse-like multituberculate, Repenomamus, a lizard, a small bird and a
// fish, each modelled in code (render/critter_mesh.h, no downloaded models)
// with three LODs, and posed in the vertex shader (shaders/critter.vs) from a
// gait phase and a few pose numbers, so hundreds of them cost a handful of
// instanced draws. Presentation only: it reads the sim's critter pool and
// never changes it.
//
// Per pool slot the renderer keeps a smoothed position (a client gets
// critters at 15 Hz), a gait phase advanced by the speed, and eased pose
// numbers: crouched while hiding, a flinch when it bolts, a bird's wings
// opening as it takes off, a Repenomamus squaring up when it fights.
//
// Compiled by including render/critters.c from view.c (build.sh lists the
// player sources itself). Frame order: cret_critters_update after the
// camera is set, cret_critters_draw_shadow per cascade, cret_critters_draw in
// the opaque pass.
#ifndef CRET_RENDER_CRITTERS_H
#define CRET_RENDER_CRITTERS_H
#include "gfx.h"
#include "csm.h"
#include "veg.h"
#include "../sim.h"

#define CRET_CRITTER_DRAW_RANGE 70.0f     // m from the camera (the hatchling spawn ring's outer edge)
#define CRET_CRITTER_SHADOW_RANGE 14.0f   // m: nearer critters also cast sun shadows (nearest cascade only)

// A pose, for critters drawn outside the sim's pool (a catch carried in the
// jaws, the gallery). All fields 0 is a critter standing still.
typedef struct CretCritterPose {
    float phase;    // gait cycle (radians); cret_critter_pose_advance moves it with the speed
    float gait;     // 0 still .. 1 its top speed (sets the stride, a mouse's bound)
    float flinch;   // 0..1: the start-up jolt when it bolts
    float crouch;   // 0..1: pressed flat, hiding
    float wings;    // bird: 0 folded .. 1 open and beating; Repenomamus: 0..1 threat (head up, lunging)
    float death;    // 0 alive .. 1 limp: legs drawn in, head and tail hanging, body curled
    float seed;     // 0..1: individual colour and timing
} CretCritterPose;

// Queues one critter of `kind` (CRET_CR_*) and mass `kg` for the next
// rendered frame (shadows included), with its feet (a fish: its centre) at
// pos and turned by the unit quaternion quat (x, y, z, w; identity faces +x,
// +y up). Call it every frame before cret_view_render, e.g. for a catch in a
// jaw: put the grip point (cret_critter_grip) at the jaw. Up to 256 a frame.
void cret_critter_draw_one(int kind, float kg, const float pos[3], const float quat[4], const CretCritterPose* pose);
// The point to hold a critter by, in its own frame (metres from pos above,
// before quat): the middle of the body, at body-centre height.
void cret_critter_grip(int kind, float kg, float out[3]);
// Advances a pose's gait phase for moving at `speed` m/s for dt s, and sets
// its gait from the speed.
void cret_critter_pose_advance(CretCritterPose* p, int kind, float kg, float speed, float dt);
// Rotation about +y by yaw (heading: 0 faces +x, as CretCritter.yaw), as a quaternion.
void cret_critter_yaw_quat(float yaw, float out[4]);

// Forces every critter to one LOD (0 full .. 2 lowest; -1 by distance). The gallery's L key.
void cret_critters_force_lod(int lod);
// Triangle count of a kind's mesh at a LOD (0 before the first frame).
int cret_critters_tris(int kind, int lod);
// Review captures (huntcap.c): while on, critters write alpha 0.5 into the
// HDR target, so their visible pixels can be read back exactly.
void cret_critters_mark(bool on);
// What the last frame drew: critters per kind and LOD, triangles, shadow casters.
typedef struct CretCritterDrawStats { int n[CRET_CR_KINDS][3], tris, casters, draws; } CretCritterDrawStats;
CretCritterDrawStats cret_critters_stats(void);

// Renderer hooks (view.c).
void cret_critters_update(const CretSim* sim, const float cam[3], const float viewproj[16], float time);
int cret_critters_draw(const VegFrameDesc* f);
int cret_critters_draw_shadow(const CretCsm* c, int k);
void cret_critters_reload_shaders(void);
void cret_critters_close(void);
#endif
