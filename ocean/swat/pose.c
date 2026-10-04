#include "pose.h"
#include "rifle_geometry.h"

b3Pos swat_pose_weapon_point(const SwatPose* p,b3Vec3 local) {
    return b3OffsetPos(p->shoulder,swat_add(swat_mul(p->weapon_forward,local.x),
        swat_add(swat_mul(p->weapon_up,local.y),swat_mul(p->right,local.z))));
}

SwatPose swat_pose(const SwatController* c,const SwatArsenal* a) {
    SwatPose p={0};
    p.eye=swat_controller_eye(c);
    swat_controller_view(c,&p.forward,&p.right,&p.up);
    const SwatWeapon* weapon=&a->slots[a->active];
    const SwatWeaponDef* definition=swat_arsenal_def(a,a->active);
    p.reload_fraction=weapon->reload_duration ? 1-(float)weapon->reload_remaining/weapon->reload_duration : 0;
    float busy=swat_weapons_busy(a) ? 1 : 0;
    p.weapon_pitch=c->ready_blend*55*SWAT_RAD-busy*28*SWAT_RAD;
    p.weapon_forward=swat_add(swat_mul(p.forward,cosf(p.weapon_pitch)),swat_mul(p.up,sinf(p.weapon_pitch)));
    p.weapon_up=swat_add(swat_mul(p.up,cosf(p.weapon_pitch)),swat_mul(p.forward,-sinf(p.weapon_pitch)));
    b3Vec3 gun_forward=p.weapon_forward;
    // At ADS the sight lies on the eye's aim ray. The muzzle sits one sight
    // height below it; hip offset smoothly moves the entire assembly.
    b3Vec3 lateral=swat_mul(p.right,.12f*(1-c->ads));
    b3Vec3 vertical=swat_mul(p.up,-definition->sight_height-.10f*(1-c->ads)-.04f*busy);
    p.shoulder=b3OffsetPos(p.eye,swat_add(swat_mul(p.forward,-.08f),swat_add(lateral,vertical)));
    p.muzzle=b3OffsetPos(p.shoulder,swat_mul(gun_forward,definition->barrel));
    p.sight=b3OffsetPos(p.shoulder,swat_add(swat_mul(gun_forward,definition->barrel*.35f),swat_mul(p.up,definition->sight_height)));
    p.right_hand=b3OffsetPos(p.shoulder,swat_add(swat_mul(gun_forward,.12f),swat_mul(p.up,-.035f)));
    p.left_hand=b3OffsetPos(p.shoulder,swat_add(swat_mul(gun_forward,definition->barrel*.65f),swat_mul(p.up,-.02f)));
    if(a->active==0 && a->primary==0) {
        p.muzzle=swat_pose_weapon_point(&p,swat_carbine_muzzle);
        p.sight=swat_pose_weapon_point(&p,swat_carbine_rear_sight);
        p.right_hand=swat_pose_weapon_point(&p,swat_carbine_right_hand);
        p.left_hand=swat_pose_weapon_point(&p,swat_carbine_left_hand);
    }
    if(weapon->reload_remaining) p.left_hand=b3OffsetPos(p.right_hand,swat_mul(p.up,-.08f-.10f*sinf(p.reload_fraction*SWAT_PI)));
    return p;
}
