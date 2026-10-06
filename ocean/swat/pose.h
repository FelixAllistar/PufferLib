#ifndef SWAT_POSE_H
#define SWAT_POSE_H
#include "weapons.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct SwatPose {
    b3Pos eye,shoulder,left_hand,right_hand,sight,muzzle;
    b3Vec3 forward,right,up;
    b3Vec3 weapon_forward,weapon_up;
    float weapon_pitch,reload_fraction;
} SwatPose;
// One weapon geometry definition is used for drawing, clearance and shooting.
SwatPose swat_pose(const SwatController* controller,const SwatArsenal* arsenal);
b3Pos swat_pose_weapon_point(const SwatPose* pose,b3Vec3 local);
#ifdef __cplusplus
}
#endif
#endif
