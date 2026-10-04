#include "controller.h"
#include <string.h>

SwatInput swat_neutral_input(void) {
    SwatInput input = {0};
    input.gait = SWAT_WALK;
    return input;
}

void swat_controller_init(SwatController* c, b3WorldId world, b3Pos feet, float yaw) {
    memset(c, 0, sizeof(*c));
    swat_body_init(&c->body, world, b3OffsetPos(feet, swat_v(0,0.9144f+0.02f,0)));
    c->body.walkSpeed = 2.8f;
    c->body.runSpeed = 4.6f;
    c->body.crouchSpeed = 1.25f;
    c->body.jumpSpeed = 4.2f;
    c->body.stepUpHeight = 0.30f;
    c->body.stepDownHeight = 0.30f;
    c->yaw = yaw;
    c->stamina = 1.0f;
    c->mobility = 1.0f;
    c->eye_height = c->body.standHeight - 0.2032f;
}

b3Vec3 swat_controller_right(const SwatController* c) {
    return swat_v(-sinf(c->yaw),0,cosf(c->yaw));
}

void swat_controller_pre_step(SwatController* c, const SwatInput* in, bool weapon_busy) {
    c->yaw = swat_angle(c->yaw + swat_clamp(in->yaw_delta, -0.4f, 0.4f));
    c->pitch = swat_clamp(c->pitch + swat_clamp(in->pitch_delta,-0.3f,0.3f),
                         -85.0f*SWAT_RAD,85.0f*SWAT_RAD);
    c->recoil_pitch *= expf(-12.0f*SWAT_DT);
    c->recoil_yaw *= expf(-12.0f*SWAT_DT);

    // A stand request checks the feet hull and the actual leaned capsule.
    // The actual upper capsule is then swept to its desired lean after physics.
    swat_body_set_crouch(&c->body, in->crouch);
    c->sprinting = in->gait == SWAT_SPRINT && in->forward > 0.1f &&
        !c->body.crouched && !in->aim && !in->fire && !in->reload &&
        !weapon_busy && fabsf(in->lean) < 0.01f && c->stamina > 0.05f;
    c->stamina = swat_clamp(c->stamina + (c->sprinting ? -0.16f : 0.10f)*SWAT_DT,0,1);
    c->ready=(SwatReady)(in->ready>=0 && in->ready<SWAT_READY_STATES ? in->ready : SWAT_READY);
    if(in->aim || in->fire) c->ready=SWAT_READY;
    float ready_target=c->sprinting ? -1 : (c->ready==SWAT_HIGH_READY ? 1 : (c->ready==SWAT_LOW_READY ? -1 : 0));
    c->ready_blend+=(ready_target-c->ready_blend)*(1-expf(-12*SWAT_DT));
    float target_ads = in->aim && c->ready==SWAT_READY && !weapon_busy && !in->reload ? 1.0f : 0.0f;
    c->ads += (target_ads - c->ads) * (1.0f-expf(-18.0f*SWAT_DT));
    c->body.sprint = c->sprinting;
    c->body.walkSpeed = in->gait == SWAT_SLOW ? 1.2f : (in->aim ? 1.7f : 2.8f);
    c->body.walkSpeed *= c->mobility;
    c->body.runSpeed = 4.6f*c->mobility;
    c->body.crouchSpeed = 1.25f*c->mobility;
    c->jumped = in->jump && !c->last_jump && c->body.onGround &&
        c->body.jumpCooldown <= 0.0f && !c->body.crouched && c->stamina >= 0.1f;
    if (c->jumped) {
        swat_body_jump(&c->body);
        c->stamina = fmaxf(0.0f,c->stamina-0.1f);
    }
    c->last_jump = in->jump;
    b3Vec3 forward = swat_direction(c->yaw,0);
    b3Vec2 throttle = {swat_clamp(in->forward,-1,1),swat_clamp(in->strafe,-1,1)};
    swat_body_pre_step(&c->body,SWAT_DT,forward,swat_controller_right(c),throttle);
}

void swat_controller_post_step(SwatController* c, const SwatInput* in) {
    swat_body_post_step(&c->body,SWAT_DT);
    b3Vec3 right = swat_controller_right(c);
    b3Vec3 goal = swat_mul(right, c->sprinting ? 0.0f : swat_clamp(in->lean,-1,1)*SWAT_MAX_LEAN);
    b3Vec3 difference = swat_add(goal,swat_mul(c->body.upperOffset,-1));
    float length = b3Length(difference);
    float max_step = 2.5f * SWAT_DT;
    if (length > max_step) difference = swat_mul(difference,max_step/length);
    b3Vec3 desired = swat_add(c->body.upperOffset,difference);
    b3Vec3 achieved = swat_body_lean(&c->body,desired);
    c->lean = swat_clamp(b3Dot(achieved,right)/SWAT_MAX_LEAN,-1,1);
    float eye_height = c->body.totalHeight - 0.2032f;
    // Lower immediately for clearance; ease upwards inside the standing hull.
    c->eye_height = fminf(eye_height,c->eye_height + 3.0f*SWAT_DT);
}

b3Pos swat_controller_eye(const SwatController* c) {
    b3Vec3 offset = c->body.upperOffset;
    offset.y = c->eye_height;
    return b3OffsetPos(swat_body_feet_position(&c->body),offset);
}

b3Vec3 swat_controller_aim(const SwatController* c) {
    return swat_direction(c->yaw+c->recoil_yaw,
        swat_clamp(c->pitch+c->recoil_pitch,-89.0f*SWAT_RAD,89.0f*SWAT_RAD));
}

void swat_controller_recoil(SwatController* c, float vertical, float horizontal) {
    c->recoil_pitch = swat_clamp(c->recoil_pitch+vertical,0,12.0f*SWAT_RAD);
    c->recoil_yaw = swat_clamp(c->recoil_yaw+horizontal,-6.0f*SWAT_RAD,6.0f*SWAT_RAD);
}

void swat_controller_view(const SwatController* c, b3Vec3* forward, b3Vec3* right, b3Vec3* up) {
    *forward = swat_controller_aim(c);
    b3Vec3 r = swat_v(-sinf(c->yaw+c->recoil_yaw),0,cosf(c->yaw+c->recoil_yaw));
    b3Vec3 u = b3Cross(r,*forward);
    float angle = -c->lean*8.0f*SWAT_RAD;
    *right = swat_add(swat_mul(r,cosf(angle)),swat_mul(u,-sinf(angle)));
    *up = swat_add(swat_mul(u,cosf(angle)),swat_mul(r,sinf(angle)));
}
