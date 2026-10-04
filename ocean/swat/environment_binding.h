#ifndef SWAT_ENVIRONMENT_BINDING_H
#define SWAT_ENVIRONMENT_BINDING_H
#include "world.h"

// Render-only recipe. Never stored in SwatObject, layout tokens, or packets.
// Each invocation belongs to exactly the object passed in this map/epoch.
typedef enum SwatEnvironmentSurface {
    SWAT_ENV_NONE, SWAT_ENV_PLASTER, SWAT_ENV_WOOD, SWAT_ENV_DOOR
} SwatEnvironmentSurface;

static inline SwatEnvironmentSurface swat_environment_surface(const SwatObject* o) {
    if(!o->active) return SWAT_ENV_NONE;
    if(o->material==SWAT_WOOD) return o->door ? SWAT_ENV_DOOR : SWAT_ENV_WOOD;
    if(o->material==SWAT_DRYWALL || o->material==SWAT_PLASTER) return SWAT_ENV_PLASTER;
    return SWAT_ENV_NONE;
}

// A unit-slab point follows the same center, pitch and yaw as its collider.
// The door hinge is unit (0,0,-.5); a skin's geometry is its own whole box.
static inline b3Pos swat_environment_point(const SwatObject* o,b3Vec3 unit) {
    b3Vec3 p=swat_v(2*o->half.x*unit.x,2*o->half.y*unit.y,2*o->half.z*unit.z);
    float cp=cosf(o->pitch),sp=sinf(o->pitch),cy=cosf(o->yaw),sy=sinf(o->yaw);
    float x=cp*p.x-sp*p.y,y=sp*p.x+cp*p.y;
    return b3OffsetPos(o->center,swat_v(cy*x+sy*p.z,y,-sy*x+cy*p.z));
}
#endif
