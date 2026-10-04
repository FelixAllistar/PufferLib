#ifndef SWAT_CONTROLLER_H
#define SWAT_CONTROLLER_H
#include "body.h"
#include "swat_math.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SWAT_MAX_LEAN 0.42f
typedef enum SwatGait { SWAT_SLOW, SWAT_WALK, SWAT_SPRINT } SwatGait;
typedef enum SwatReady { SWAT_READY,SWAT_LOW_READY,SWAT_HIGH_READY,SWAT_READY_STATES } SwatReady;
typedef enum SwatDoorTool { SWAT_DOOR_NONE,SWAT_LOCKPICK,SWAT_PLACE_CHARGE,SWAT_DETONATE_CHARGE,SWAT_WEDGE,SWAT_REMOVE_WEDGE,SWAT_DISARM,SWAT_DOOR_TOOLS } SwatDoorTool;
typedef struct SwatInput {
    float forward, strafe;          // [-1,1]
    float yaw_delta, pitch_delta;   // radians for this fixed simulation tick
    float lean;                    // [-1,1], negative = left
    SwatGait gait;
    bool crouch, jump, aim, fire, reload, interact, selector;
    int weapon;                    // 0 = keep, 1 = primary, 2 = sidearm
    bool inspect, command, melee;
    int loadout;                   // 0 = keep, 1..3 = staging-area kit request
    int throwable;                 // 0 = none, 1 = flashbang, 2 = CS gas
    bool taser,pepper_spray,peek;
    int door_tool;                  // held pick/place, pressed remote detonation
    int sniper_order, sniper_unit, sniper_post, sniper_rifle;
    bool sniper_control;
    int ready;                    // 0 normal, 1 low ready, 2 high ready
    bool cancel_reload;
    int primary_profile,sight_profile; // staging requests: zero keeps current
    bool magazine_inventory;
    int squad_order,squad_team;
    bool squad_queue,squad_execute;
} SwatInput;

typedef struct SwatController {
    SwatBody body;
    float yaw, pitch, ads, stamina, eye_height;
    float recoil_pitch, recoil_yaw;
    float lean;                    // achieved lean relative to current right
    float mobility;
    bool sprinting, last_jump, jumped, muzzle_blocked;
    float ready_blend;
    SwatReady ready;
} SwatController;

SwatInput swat_neutral_input(void);
void swat_controller_init(SwatController* c, b3WorldId world, b3Pos feet, float yaw);
void swat_controller_pre_step(SwatController* c, const SwatInput* input, bool weapon_busy);
void swat_controller_post_step(SwatController* c, const SwatInput* input);
b3Pos swat_controller_eye(const SwatController* c);
b3Vec3 swat_controller_aim(const SwatController* c);
b3Vec3 swat_controller_right(const SwatController* c);
void swat_controller_view(const SwatController* c, b3Vec3* forward, b3Vec3* right, b3Vec3* up);
void swat_controller_recoil(SwatController* c, float vertical, float horizontal);

#ifdef __cplusplus
}
#endif
#endif
