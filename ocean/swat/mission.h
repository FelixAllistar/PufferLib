#ifndef SWAT_MISSION_H
#define SWAT_MISSION_H
#include "world.h"
typedef enum SwatMission { SWAT_ANNEX, SWAT_HOUSE, SWAT_GENERATED, SWAT_RANGE, SWAT_MOTEL, SWAT_STOREFRONT, SWAT_BUILDING, SWAT_BREACH_LAB, SWAT_WEAPON_RANGE, SWAT_HOLLOW, SWAT_MISSION_COUNT } SwatMission;
typedef enum SwatMotelOccupancy { SWAT_MOTEL_STANDARD,SWAT_MOTEL_DISPERSED,SWAT_MOTEL_MIXED,SWAT_MOTEL_BUSY,SWAT_MOTEL_OCCUPANCIES } SwatMotelOccupancy;
const char* swat_motel_occupancy_name(int profile);
int swat_motel_occupancy_civilians(int profile);
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
void swat_mission_build_breach_lab(SwatWorld* world);
// Weapon test range: flat floor, three target lanes (open, tall cover, waist
// cover), a backstop and a hinged door behind the officer's spawn. The fixed
// positions below are shared by the simulation spawns and the range tests.
#define SWAT_WEAPON_RANGE_TARGETS 3
extern const b3Pos swat_weapon_range_targets[SWAT_WEAPON_RANGE_TARGETS];
#define SWAT_WEAPON_RANGE_TALL_COVER_X 4.0f   // centre of the tall slab in lane B
#define SWAT_WEAPON_RANGE_COVER_HALF .15f     // its half thickness along x
#define SWAT_WEAPON_RANGE_DOOR_X -2.0f        // door leaf plane behind the spawn
void swat_mission_build_weapon_range(SwatWorld* world);
// The Hollow: rural property raid (HOLLOW.md). Fixed occupant positions are
// shared by the simulation spawns and the Hollow tests.
#define HOLLOW_FLOOR .30f        // ground floor on piers
#define HOLLOW_UPPER 2.90f       // upper floor
#define HOLLOW_UPPER_TOP 5.20f   // upper plate and attic floor
#define HOLLOW_RIDGE 7.0f
#define HOLLOW_KITCHEN_H 2.45f
#define HOLLOW_RISE ((HOLLOW_UPPER-HOLLOW_FLOOR)/15)
#define HOLLOW_TREAD .22f
#define HOLLOW_STAIR_X 18.45f    // foot of the house stair (first riser)
#define HOLLOW_LOFT 2.6f
#define HOLLOW_CELLAR -1.4f
#define SWAT_HOLLOW_OWNER ((b3Pos){20.6f,2.9f,-3.2f})     // upstairs, the flyer room
#define SWAT_HOLLOW_ACCOMPLICE ((b3Pos){37.5f,0,-5.5f})  // barn, by the workbench
#define SWAT_HOLLOW_HOSTAGE ((b3Pos){18.3f,-1.4f,16.2f}) // root cellar
void swat_mission_build_hollow(SwatWorld* world);
// A framed wall runs along its local Z axis; opening coordinates are local Z.
void swat_build_framed_wall(SwatWorld* w,b3Pos origin,float yaw,float length,
                            float height,float opening_center,float opening_width,
                            float sill,float opening_height,bool exterior);
int swat_framed_wall_pieces(float length,float height,float opening_center,float opening_width,
                            float sill,float opening_height,bool exterior);
#endif
