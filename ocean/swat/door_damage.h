// Shared game geometry for local latch failure. No graphics/physics ownership.
#ifndef SWAT_DOOR_DAMAGE_H
#define SWAT_DOOR_DAMAGE_H
#include "world.h"
#include <string.h>
#define SWAT_DOOR_SECTIONS 7
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
