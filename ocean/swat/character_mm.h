#ifndef SWAT_CHARACTER_MM_H
#define SWAT_CHARACTER_MM_H
// Presentation-only motion matching for third-person bodies (anim/mm.h).
// A database named by SWAT_MM_DB (or the motion_matching_db setting) is
// loaded once; without one nothing here is used and bodies keep their clips.
// Every input is replicated actor state: the feet position (velocity is the
// smoothed difference of replicated positions), facing yaw, actual stance,
// role and surrender/wound flags, and the replicated sound log for heard
// commands. Nothing is written back to the simulation.
#include "character_asset.h"
#include "sim.h"
#include "raylib.h"
#include "raymath.h"
#include "mm.h"
#ifdef __cplusplus
extern "C" {
#endif

// Settings entry (motion_matching_db); an SWAT_MM_DB environment variable
// takes precedence. Call before the first swat_mm_db().
void swat_mm_configure(const char* path);
// The database, loaded on the first call; NULL when none is configured or it
// fails to load (logged once). SWAT_MM_DB=0 disables a configured setting.
const mm_db* swat_mm_db(void);
void swat_mm_shutdown(void);

// Joint mapping from the database skeleton onto one body asset, by name:
// exact, then "mixamorig*:" suffix, then the BC_CIV01 civilian rig's names.
// Building it samples the asset's rest pose (overwriting its current pose).
typedef struct SwatMMRig SwatMMRig;
// fingers=false leaves the hands' descendants on the sampled clip (the
// officer's measured carbine grip).
SwatMMRig* swat_mm_rig_build(SwatCharacterAsset* asset,const mm_db* db,bool fingers);
void swat_mm_rig_free(SwatMMRig* rig);
// True when the body is the database skeleton itself (same rest pose and hip
// height): its pose is used as is, including the runtime's foot locking.
bool swat_mm_rig_identity(const SwatMMRig* rig);
bool swat_mm_rig_valid(const SwatMMRig* rig);
float swat_mm_rig_hip_ratio(const SwatMMRig* rig);
// Leg nodes per side (0 left, 1 right): upper leg, leg, foot; -1 if missing.
int swat_mm_rig_leg(const SwatMMRig* rig,int side,int joint);

// Procedural look-at state (critically damped spring, half-life 0.15 s).
typedef struct SwatMMLook {
    float yaw,pitch,yaw_velocity,pitch_velocity; // smoothed target, relative to facing
    float weight,weight_velocity;               // 0 released .. 1 looking
    int target;                                 // officer looked at, -1 none
    int commander,command_tick;                 // last officer heard commanding, -1 none
    int tick,episode;
    bool valid;
} SwatMMLook;
#define SWAT_LOOK_YAW_LIMIT (70*SWAT_RAD)
#define SWAT_LOOK_PITCH_LIMIT (35*SWAT_RAD)
#define SWAT_LOOK_RANGE 15.0f
#define SWAT_LOOK_HALFLIFE .15f

typedef struct SwatMMBody {
    mm_controller controller;
    bool ready,driven;      // controller initialized; drove the last update
    int tick,episode;
    b3Pos feet;
    mm_vec3 velocity;       // smoothed from replicated feet positions
    float speed;
    bool fleeing;
    mm_filter filter;
    SwatMMLook look;
} SwatMMBody;
SwatMMBody* swat_mm_body_new(void);
void swat_mm_body_free(SwatMMBody* body);
// Style by role (officer tactical, suspect suspect, civilian civilian, a
// fleeing or surrendered civilian scared, a wounded body injured when the
// database has it) and the stance tag; a style no clip of that stance has is
// dropped.
mm_filter swat_mm_filter(const mm_db* db,const SwatActor* actor,bool fleeing);
// Advances the controller to the actor's replicated state at sim->tick (a
// repeated tick changes nothing). A teleport, gap or new episode restarts it.
bool swat_mm_update(SwatMMBody* body,const mm_db* db,const SwatSim* sim,int actor);
// Marks the body as not driven this tick, so the next drive restarts cleanly.
void swat_mm_release(SwatMMBody* body);
// Writes the matched pose onto the asset's current (sampled) pose: mapped
// joints take the database's world rotations (corrected for each body's rest
// pose), keep their own bone lengths, and the hips translation is scaled by
// the hip-height ratio. Root is the capsule (feet, facing). foot[2] are
// scene-space ankle targets for bodies that are not the database skeleton.
bool swat_mm_apply(const SwatMMBody* body,SwatMMRig* rig,SwatCharacterAsset* asset,Matrix* root,Matrix foot[2]);
// Mixamo yaw convention: yaw 0 faces +Z; the engine's yaw 0 faces +X.
static inline float swat_mm_yaw(float engine_yaw) { return SWAT_PI*.5f-engine_yaw; }
static inline Matrix swat_mm_root(b3Pos feet,float engine_yaw) {
    return MatrixMultiply(MatrixRotateY(swat_mm_yaw(engine_yaw)),MatrixTranslate((float)feet.x,(float)feet.y,(float)feet.z));
}

// Look-at target for this tick. Non-officers: the nearest officer within
// 15 m whose eye they can see, else the officer whose command they last
// heard (replicated sound log, 10 s). aim, when given, is the direction to
// look along instead (armed bodies follow their aim). dt in seconds.
void swat_mm_look_update(SwatMMLook* look,const SwatSim* sim,int actor,const b3Vec3* aim,float dt);
// Turns neck and head (and with chest, a quarter of the yaw into the upper
// chest) toward the smoothed target, clamped to +/-70 yaw and +/-35 pitch
// from the facing. Rotations only.
bool swat_mm_look_apply(const SwatMMLook* look,const SwatMMRig* rig,SwatCharacterAsset* asset,Matrix root,float facing_yaw,bool chest);
// World yaw (engine convention) and pitch of the head's forward axis.
bool swat_mm_head_direction(const SwatMMRig* rig,const SwatCharacterAsset* asset,const float* matrices,Matrix root,Vector3* position,Vector3* forward);
#ifdef __cplusplus
}
#endif
#endif
