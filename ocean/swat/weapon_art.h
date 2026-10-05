#ifndef SWAT_WEAPON_ART_H
#define SWAT_WEAPON_ART_H
#include "lighting.h"
#include "pose.h"
typedef struct SwatWeaponArt {
    bool initialized;
    Model carbine;
} SwatWeaponArt;
void swat_weapon_art_init(SwatWeaponArt* art);
void swat_weapon_art_close(SwatWeaponArt* art);
Matrix swat_weapon_art_transform(const SwatPose* pose);
// Camera-only framing; authoritative muzzle, clearance and world model stay
// in swat_pose. Move the complete gun/hand presentation as one rigid unit.
SwatPose swat_weapon_view_pose(const SwatPose* achieved,const SwatArsenal* arsenal,
    float ads,float horizontal,float vertical,float eye_relief);
// False requests the procedural fallback. Source scale is never changed.
bool swat_weapon_art_draw(SwatWeaponArt* art,SwatLighting* light,
                           const SwatArsenal* arsenal,const SwatPose* pose);
#endif
