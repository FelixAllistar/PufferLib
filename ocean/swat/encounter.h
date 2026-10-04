#ifndef SWAT_ENCOUNTER_H
#define SWAT_ENCOUNTER_H
#include "controller.h"
#define SWAT_NAV_SIDE 80
#define SWAT_NAV_CELLS (SWAT_NAV_SIDE*SWAT_NAV_SIDE)
typedef enum SwatBehavior { SWAT_IDLE,SWAT_INVESTIGATE,SWAT_ENGAGE,SWAT_FLEE,SWAT_SURRENDER,SWAT_ESCORT,SWAT_DETAINED } SwatBehavior;
typedef enum SwatSquadOrder { SWAT_ORDER_NONE,SWAT_ORDER_FALL_IN,SWAT_ORDER_HOLD,SWAT_ORDER_MOVE,SWAT_ORDER_STACK,SWAT_ORDER_CLEAR,SWAT_ORDER_PICK,SWAT_ORDER_WEDGE,SWAT_ORDER_CUFF,SWAT_ORDER_SEARCH,SWAT_ORDER_COVER,SWAT_SQUAD_ORDERS } SwatSquadOrder;
typedef struct SwatMind {
    SwatBehavior state;
    bool bot,queued;
    int team,order,pending_order,target,memory_ticks,replan_tick,command_tick,escort_owner;
    float resolve;
    b3Pos memory,goal,waypoint,pending_goal;
} SwatMind;
typedef struct SwatNavigation {
    unsigned char walkable[SWAT_NAV_CELLS];
    int generation,count,built_tick;
    bool built;
} SwatNavigation;
typedef struct SwatEvidence { bool dropped,collected; int actor; b3Pos position; } SwatEvidence;
typedef struct SwatDebrief { int roe_violations,arrests,rescued,evidence; float unlawful_damage; } SwatDebrief;
struct SwatSim;
void swat_encounter_inputs(struct SwatSim* sim,SwatInput inputs[]);
void swat_encounter_orders(struct SwatSim* sim,const SwatInput inputs[]);
void swat_encounter_step(struct SwatSim* sim);
bool swat_navigation_next(struct SwatSim* sim,b3Pos start,b3Pos goal,b3Pos* next);
bool swat_collect_evidence(struct SwatSim* sim,int actor);
const char* swat_squad_order_name(int order);
#endif
