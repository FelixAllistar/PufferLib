#ifndef SWAT_CONTROLLER_H
#define SWAT_CONTROLLER_H
#include "body.h"
#include "swat_math.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SWAT_MAX_LEAN 0.42f
typedef enum SwatGait { SWAT_SLOW, SWAT_WALK, SWAT_SPRINT } SwatGait;
typedef struct SwatInput {
    float forward, strafe;          // [-1,1]
    float yaw_delta, pitch_delta;   // radians for this fixed simulation tick
    float lean;                    // [-1,1], negative = left
    SwatGait gait;
    bool crouch, jump, aim, fire, reload, interact, selector;
    int weapon;                    // 0 = keep, 1 = primary, 2 = sidearm
} SwatInput;

typedef struct SwatController {
    SwatBody body;
    float yaw, pitch, ads, stamina, eye_height;
    float recoil_pitch, recoil_yaw;
    float lean;                    // achieved lean relative to current right
    bool sprinting, last_jump, jumped, muzzle_blocked;
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
