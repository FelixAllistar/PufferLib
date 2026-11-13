// Dinosaurs: skinned, animated models (tools/dino_convert.py -> assets/dinos/*.dino)
// drawn for every agent whose species has a model. Animation is presentation
// only: it reads the sim (speed, ground and water state, actions) and never
// changes it.
//   - Locomotion blends idle, walk, trot and run by ground speed, with the
//     clips phase-synced and played at the rate that matches their stride.
//   - Jumps, swimming and actions (eat, drink, call, attack) crossfade in;
//     attack clips play at the rate that lands the blow on the sim's hit
//     time, and over the neck and head only while the legs keep running.
//   - The head turns toward the aim; tail swipes and the stegosaur's aimed
//     tail are procedural yaws of the tail chain (with a whip lag and the body
//     twisting against them).
//   - The body pitches and rolls with the ground under the feet (within
//     species limits) and leans into turns; near the camera, two-bone foot
//     IK puts each foot on the terrain or a rock and drops the pelvis so
//     the lowest foot reaches.
//   - GPU skinning: joint matrices for all agents live in one float texture
//     (3 texels per joint, one row per agent), so each LOD and material is one
//     instanced draw, in the view and in each shadow cascade.
#ifndef CRET_DINO_H
#define CRET_DINO_H
#include "gfx.h"
#include "veg.h"            // VegFrameDesc: shared per-frame lighting inputs
#include "csm.h"
#include "../sim.h"

#define DINO_LODS 3
#define DINO_MAX_JOINTS 256
#define DINO_MAX_CLIPS 48
#define DINO_MAX_MATS 8
#define DINO_MAX_SUBS 8

enum { DCLIP_IDLE, DCLIP_IDLE2, DCLIP_WALK, DCLIP_TROT, DCLIP_RUN, DCLIP_JUMP, DCLIP_EAT, DCLIP_DRINK, DCLIP_CALL, DCLIP_ATTACK, DCLIP_REST,
       DCLIP_HURT, DCLIP_DEATH, DCLIP_COUNT };
// Draw slots: one per agent, then one per carcass (the death pose and its decay).
#define DINO_SLOTS (CRET_MAX_AGENTS + CRET_MAX_CARCASSES)

typedef struct DinoClip { char name[32]; float duration, speed; int loop, nframes; float* frames; } DinoClip;   // frames[f][joint][8]

typedef struct DinoLod {
    GfxMesh mesh; int nsub, tris;
    int sub_mat[DINO_MAX_SUBS], sub_first[DINO_MAX_SUBS], sub_count[DINO_MAX_SUBS];
} DinoLod;

typedef struct DinoModel {
    bool ok; char name[32];
    float length, height, bmin[3], bmax[3];
    bool quadruped;                         // four feet: the body follows the slope more
    int njoint; int parent[DINO_MAX_JOINTS];
    float inv_bind[DINO_MAX_JOINTS][12];    // 3x4 row-major
    float rest[DINO_MAX_JOINTS][8];
    int nmat; GLuint t_albedo[DINO_MAX_MATS], t_detail[DINO_MAX_MATS]; int mat_flags[DINO_MAX_MATS]; char mat[DINO_MAX_MATS][32];
    DinoLod lod[DINO_LODS];
    int nclip; DinoClip clip[DINO_MAX_CLIPS];
    int slot[DCLIP_COUNT];                  // clip index per role, -1 if missing
    int loco[3], nloco;                     // walk/trot/run clips sorted by speed
    bool idle_loco;                         // no idle clip: idle is a frozen gait frame
    float idle_frame;                       // seconds into the gait clip used as the idle pose
    // Procedural layers (animate): each joint's share of a yaw about the neck
    // base (head aim, the attack's upper-body mask) and about the tail base
    // (tail swipes, the stegosaur's aimed tail), from names or rest positions.
    float neck_w[DINO_MAX_JOINTS], tail_w[DINO_MAX_JOINTS], neck_pivot[3], tail_pivot[3];
    bool has_neck, has_tail;
    int head_j, tail_mid_j, tail_tip_j;     // joints whose world positions effects use (-1: none)
    // Stand-ins for clips a model lacks (animate): the lower jaw and what hangs from it, and the
    // neck pitch (radians, negative) that brings the head down to the ground from the rest pose.
    int jaw_j; uint8_t jaw_sub[DINO_MAX_JOINTS]; float eat_pitch;
    // The neck and the tail bend joint by joint (chain_bend): up to DINO_CHAIN pivots along each,
    // the first at the base (joint -1: neck_pivot / tail_pivot), with the weight each sits at.
#define DINO_CHAIN 6
    int neck_pj[DINO_CHAIN], tail_pj[DINO_CHAIN], neck_nk, tail_nk; float neck_pw[DINO_CHAIN], tail_pw[DINO_CHAIN];
    // Legs for foot IK (animate): hip, knee and ankle joints from the rigs'
    // names; the foot (the ankle's subtree, plus toes rigged apart) moves
    // rigidly with the ankle.
    int nleg; struct { int hip, knee, ankle; bool front; } leg[4];
    uint16_t leg_bits[DINO_MAX_JOINTS];     // bit l: under leg l's hip, 4+l: under its knee, 8+l: its foot
    float hip_y, front_x, hind_x, hip_z;    // rest: mean hip height, front and hind hip x, half the hips' spread
} DinoModel;

// What a slot shows this frame: a live agent or a carcass (dead_t >= 0).
typedef struct DinoSubject {
    bool on; int species, act;
    float x, y, z, yaw, speed, growth;
    bool on_ground, swimming; int hurt;
    float dead_t, decay;      // seconds since death (-1: alive), 0..1 towards gone
    int seed;                 // per-animal variation
    bool limp;                // broken bone
    float aim;                // head aim -1..1 across the neck arc (the stegosaur: its tail behind)
} DinoSubject;

// Per-slot animation state (view side).
typedef struct DinoAnim {
    bool init; int species;
    float phase, idle_t, idle2_w, act_t, air_t, land_t;
    float w_move, w_air, w_act, speed;
    int act, act_clip, prev_act_clip; float prev_act_t, prev_w;
    float pitch, roll, lean, y, last_yaw;
    float tint, hurt_t; int last_hurt;
    int lod_prev; float lod_t; // LOD crossfade: the previous LOD dithers out over ~0.3 s
    float breath;              // procedural layers for models without the clip (idle breathing)
    float aim_s, tail_aim;     // smoothed head aim; the stegosaur's tail aim while it attacks
    float act_ct, act_rate, prev_rate;   // attack clips: clip time and playback rate (the blow lands on the sim's hit time)
    float fl_t, fl_front, fl_side, fl_amp;   // directional flinch (cret_dino_hit): s since the blow, its push in body axes, strength
    float ik_w, pelvis;        // foot IK: weight (fades out in the air, swimming, dead), smoothed pelvis drop (m)
    // Stand-in poses, eased (animate): neck pitch, head yaw, jaw opening, body pitch (rad), how far the body sinks on folded legs (m).
    float pr_neck, pr_yaw, pr_jaw, pr_pitch, pr_crouch;
} DinoAnim;

typedef struct CretDinos {
    DinoModel model[CRET_SPECIES];
    GfxProgram prog, shadow_prog;
    GLuint pose_tex, inst_vbo;
    float* pose;                            // DINO_SLOTS rows x DINO_MAX_JOINTS*12 floats
    DinoSubject subj[DINO_SLOTS];
    DinoAnim anim[DINO_SLOTS];
    int lod[DINO_SLOTS];                    // -1: not drawn this frame
    int lod_last[DINO_SLOTS];               // LOD drawn last frame (-1: none)
    float model_m[DINO_SLOTS][12];          // body transform (3x4) used for culling
    float jpos[DINO_SLOTS][3][3];           // world positions of the head, tail middle and tail tip this frame
    int drawn, drawn_tris;
} CretDinos;

bool cret_dino_init(CretDinos* d);
// True if a species has a model (agents of other species draw as capsules).
bool cret_dino_has(const CretDinos* d, int species);
// Advances animation for every live agent and carcass and uploads joint matrices.
void cret_dino_update(CretDinos* d, const CretSim* sim, const CretWorld* w, const float cam[3], float dt);
void cret_dino_draw(CretDinos* d, const CretSim* sim, const VegFrameDesc* f);
int cret_dino_draw_shadow(CretDinos* d, const CretSim* sim, const CretCsm* c, int k);
void cret_dino_reload_shaders(CretDinos* d);
// A blow on the animal in `slot`, pushing along (dir_x, dir_z) (world, from
// the attacker): the spine and neck jerk away and the body leans with the
// knockback for ~0.4 s. strength ~0.2 (a scratch) .. 1.3 (a heavy blow).
void cret_dino_hit(CretDinos* d, int slot, float dir_x, float dir_z, float strength);
// World position of the head (which 0), tail middle (1) or tail tip (2) as
// last animated; false if the slot was not animated or has no such joint.
bool cret_dino_joint(const CretDinos* d, int slot, int which, float out[3]);
void cret_dino_close(CretDinos* d);
#endif
