// Role task contract v1 layout: observation, ray and action-head constants.
// Dependency-free, so the scripted baselines (roles_baselines.h) compile
// without the simulation and can only read what an agent observes.
// See CONTRACT.md "Role task contract v1".
#ifndef SWAT_ROLES_LAYOUT_H
#define SWAT_ROLES_LAYOUT_H

#define SWAT_ROLES_VERSION 1
#define SWAT_ROLES_SELF 45
#define SWAT_GAZE_RAYS 8
// One fixed reticle ray along the achieved view, then the learned gaze rays.
#define SWAT_ROLES_RAYS (1+SWAT_GAZE_RAYS)
#define SWAT_ROLES_RAY_CHANNELS 11
#define SWAT_ROLES_OBS (SWAT_ROLES_SELF+SWAT_ROLES_RAYS*SWAT_ROLES_RAY_CHANNELS)
#define SWAT_ROLES_RANGE 30.0f
#define SWAT_ROLES_MOTOR_HEADS 16
#define SWAT_GAZE_YAW_BINS 9
#define SWAT_GAZE_PITCH_BINS 5
#define SWAT_ROLES_HEADS (SWAT_ROLES_MOTOR_HEADS+2*SWAT_GAZE_RAYS)
#define SWAT_ROLES_ACTION_SIZES {5,5,3,3,3,2,3,2,2,2,3,2,2,2,2,2, \
    9,5,9,5,9,5,9,5,9,5,9,5,9,5,9,5}
#define SWAT_ROLES_LOGITS 155

enum {
    SWAT_ROLE_HEAD_YAW, SWAT_ROLE_HEAD_PITCH, SWAT_ROLE_HEAD_FORWARD, SWAT_ROLE_HEAD_STRAFE,
    SWAT_ROLE_HEAD_LEAN, SWAT_ROLE_HEAD_CROUCH, SWAT_ROLE_HEAD_GAIT, SWAT_ROLE_HEAD_AIM,
    SWAT_ROLE_HEAD_TRIGGER, SWAT_ROLE_HEAD_RELOAD, SWAT_ROLE_HEAD_WEAPON, SWAT_ROLE_HEAD_INTERACT,
    SWAT_ROLE_HEAD_JUMP, SWAT_ROLE_HEAD_SELECTOR, SWAT_ROLE_HEAD_COMMAND, SWAT_ROLE_HEAD_COMPLY,
    SWAT_ROLE_HEAD_GAZE
};
// Ray channels. 0-1 are the ray's own direction offsets, so the recurrent
// policy can relate what it saw to where it chose to look.
enum {
    SWAT_RAY_YAW, SWAT_RAY_PITCH, SWAT_RAY_DEPTH, SWAT_RAY_SOLID, SWAT_RAY_DESTRUCTIBLE,
    SWAT_RAY_DOOR, SWAT_RAY_OFFICER, SWAT_RAY_SUSPECT, SWAT_RAY_CIVILIAN, SWAT_RAY_COMPLIANT,
    SWAT_RAY_INTEGRITY
};
static const float swat_gaze_yaw_degrees[SWAT_GAZE_YAW_BINS]={-80,-50,-25,-10,0,10,25,50,80};
static const float swat_gaze_pitch_degrees[SWAT_GAZE_PITCH_BINS]={-40,-15,0,15,40};
static const int swat_roles_neutral[SWAT_ROLES_MOTOR_HEADS]={2,2,1,1,1,0,1,0,0,0,0,0,0,0,0,0};
#endif
