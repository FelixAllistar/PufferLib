#ifndef SWAT_WEAPON_ART_H
#define SWAT_WEAPON_ART_H
#include "lighting.h"
#include "pose.h"
#define SWAT_KITBASH_GUNS 5
typedef struct SwatWeaponArt {
    bool initialized;
    Model carbine;
    float normal_scale;
    // Optional licensed suspect rifles (git-ignored assets/weapons/kitbash,
    // rebuilt by tools/kitbash): fitted so the pistol grip sits on the
    // carbine's grip landmark. Render-only; authority keeps the carbine.
    Model kitbash[SWAT_KITBASH_GUNS];
    b3Vec3 kitbash_muzzle[SWAT_KITBASH_GUNS]; // drawn muzzle, rifle frame
    int kitbash_count;
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
