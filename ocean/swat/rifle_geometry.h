#ifndef SWAT_RIFLE_GEOMETRY_H
#define SWAT_RIFLE_GEOMETRY_H
#include "swat_math.h"
// Measured rigid-rifle interface, in metres: +X forward, +Y up, +Z right.
// Geometry stays private. These points drive authority whether art is present
// or absent; rendering never changes the weapon's reach or firing origin.
#define SWAT_CARBINE_REACH .89999995f
#define SWAT_CARBINE_SIGHT_HEIGHT .12474874f
static const b3Vec3 swat_carbine_muzzle={SWAT_CARBINE_REACH,.04362635f,0};
static const b3Vec3 swat_carbine_rear_sight={.29982166f,SWAT_CARBINE_SIGHT_HEIGHT,0};
static const b3Vec3 swat_carbine_front_sight={.65497363f,.12472590f,0};
static const b3Vec3 swat_carbine_right_hand={.26177320f,-.07099672f,.01466539f};
static const b3Vec3 swat_carbine_left_hand={.55807343f,.01222156f,0};
#endif
