#ifndef SWAT_MISSION_H
#define SWAT_MISSION_H
#include "world.h"
typedef enum SwatMission { SWAT_ANNEX, SWAT_HOUSE, SWAT_GENERATED, SWAT_RANGE, SWAT_MOTEL, SWAT_STOREFRONT, SWAT_MISSION_COUNT } SwatMission;
typedef struct SwatOverwatch {
    const char* name;
    b3Pos position, target;
} SwatOverwatch;
typedef struct SwatMissionDef {
    const char* name;
    const char* briefing;
    b3Pos staging, extraction;
    int overwatch_count;
    SwatOverwatch overwatch[3];
} SwatMissionDef;
const SwatMissionDef* swat_mission(int mission);
void swat_mission_build_house(SwatWorld* world);
void swat_mission_build_test_range(SwatWorld* world);
// A framed wall runs along its local Z axis; opening coordinates are local Z.
void swat_build_framed_wall(SwatWorld* w,b3Pos origin,float yaw,float length,
                            float height,float opening_center,float opening_width,
                            float sill,float opening_height,bool exterior);
int swat_framed_wall_pieces(float length,float height,float opening_center,float opening_width,
                            float sill,float opening_height,bool exterior);
#endif
