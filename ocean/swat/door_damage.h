// Shared game geometry for local latch failure. No graphics/physics ownership.
#ifndef SWAT_DOOR_DAMAGE_H
#define SWAT_DOOR_DAMAGE_H
#include "world.h"
#include <string.h>
#define SWAT_DOOR_SECTIONS 7
#define SWAT_DOOR_HINGES 3
#define SWAT_DOOR_HINGE_MASK ((1u<<SWAT_DOOR_HINGES)-1u)
typedef enum SwatDoorSupport {
    SWAT_DOOR_FIXED,SWAT_DOOR_BOTTOM,SWAT_DOOR_MIDDLE,SWAT_DOOR_TOP,
    SWAT_DOOR_LATCH,SWAT_DOOR_FREE
} SwatDoorSupport;
static inline b3Vec3 swat_door_hinge(const SwatObject* o,int hinge) {
    // Wood-support damage probes at Y=.23/1.10/1.92 m, 18 mm inside the
    // 1.08 x 2.19 m slab. These matched the old decorative barrels. The seated
    // candidate's new metal is on the pivot; retain these probes in solid wood
    // beyond its 7 mm edge relief, rather than sampling half outside the slab.
    const float y[SWAT_DOOR_HINGES]={.23f,1.10f,1.92f};
    return swat_v(0,(y[hinge]/2.19f-.5f)*2*o->half.y,
        -o->half.z+.018f*(o->half.z/.54f));
}
static inline b3Vec3 swat_door_hinge_pivot(const SwatObject* o,int hinge) {
    // Physical attachment and the new seated metal share the ordinary source
    // X=0 opening axis. Wood-support damage probes remain separate above.
    b3Vec3 p=swat_door_hinge(o,hinge);p.z=-o->half.z;return p;
}
static inline b3Vec3 swat_door_latch(const SwatObject* o) {return swat_v(0,.06f,o->half.z-.01f);}
static inline int swat_door_support(unsigned int broken,bool latched) {
    int count=0,last=-1;
    for(int h=0;h<SWAT_DOOR_HINGES;h++)if(!(broken&(1u<<h))){count++;last=h;}
    if(count>=2 || (count==1 && latched))return SWAT_DOOR_FIXED;
    return count?SWAT_DOOR_BOTTOM+last:latched?SWAT_DOOR_LATCH:SWAT_DOOR_FREE;
}
static inline bool swat_door_loose(const SwatObject* o) {
    return o->motion && swat_door_support(o->motion->state.broken_hinges,o->motion->state.latched)==SWAT_DOOR_FREE;
}
static inline bool swat_door_dynamic(const SwatObject* o) {
    return o->motion && swat_door_support(o->motion->state.broken_hinges,o->motion->state.latched)!=SWAT_DOOR_FIXED;
}
static inline bool swat_door_state_valid(const SwatDoorState* s) {
    if(!s->broken_hinges || (s->broken_hinges&~SWAT_DOOR_HINGE_MASK))return false;
    const float v[]={s->rotation.v.x,s->rotation.v.y,s->rotation.v.z,s->rotation.s,
        s->velocity.x,s->velocity.y,s->velocity.z,s->angular_velocity.x,s->angular_velocity.y,s->angular_velocity.z};
    for(int i=0;i<10;i++)if(!isfinite(v[i]) || fabsf(v[i])>(i<4?1.001f:100))return false;
    // Use the same unit-length requirement as b3Body_SetTransform so an
    // accepted packet cannot reach a physics assertion on application.
    return b3IsValidQuat(s->rotation) && isfinite(s->position.x) && isfinite(s->position.y) && isfinite(s->position.z) &&
        fabs(s->position.x)<=1000 && fabs(s->position.y)<=1000 && fabs(s->position.z)<=1000;
}
static inline void swat_door_section(const SwatObject* door,int section,float corners[4][2]) {
    const float heights[]={-.24f,-.16f,-.085f,.02f,.115f,.23f};
    const float depths[]={0,0,.03f,.10f,.14f,.06f,0,0};
    float scale=fminf(1,door->half.y/.6f),width=fminf(1,door->half.z/.45f);
    float low=section ? .06f*scale+heights[section-1]*scale : -door->half.y;
    float high=section==SWAT_DOOR_SECTIONS-1 ? door->half.y : .06f*scale+heights[section]*scale;
    float left=-door->half.z,lo=door->half.z-depths[section]*width,hi=door->half.z-depths[section+1]*width;
    const float v[4][2]={{low,left},{high,left},{high,hi},{low,lo}};
    memcpy(corners,v,sizeof(v));
}
static inline float swat_door_edge(const SwatObject* door,float y) {
    for(int i=0;i<SWAT_DOOR_SECTIONS;i++) {
        float v[4][2];swat_door_section(door,i,v);
        if(y>=v[0][0] && y<=v[1][0])return v[3][1]+(v[2][1]-v[3][1])*(y-v[0][0])/(v[1][0]-v[0][0]);
    }
    return door->half.z;
}
#endif
