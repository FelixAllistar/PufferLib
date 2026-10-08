#ifndef SWAT_ENCOUNTER_H
#define SWAT_ENCOUNTER_H
#include "controller.h"
#define SWAT_NAV_SIDE 108
#define SWAT_NAV_CELLS (SWAT_NAV_SIDE*SWAT_NAV_SIDE)
#define SWAT_NAV_LAYERS 4
#define SWAT_NAV_NODES (SWAT_NAV_CELLS*SWAT_NAV_LAYERS)
typedef enum SwatBehavior { SWAT_IDLE,SWAT_INVESTIGATE,SWAT_ENGAGE,SWAT_FLEE,SWAT_SURRENDER,SWAT_ESCORT,SWAT_DETAINED } SwatBehavior;
typedef enum SwatSquadOrder { SWAT_ORDER_NONE,SWAT_ORDER_FALL_IN,SWAT_ORDER_HOLD,SWAT_ORDER_MOVE,SWAT_ORDER_STACK,SWAT_ORDER_CLEAR,SWAT_ORDER_PICK,SWAT_ORDER_WEDGE,SWAT_ORDER_CUFF,SWAT_ORDER_SEARCH,SWAT_ORDER_COVER,SWAT_SQUAD_ORDERS } SwatSquadOrder;
typedef struct SwatMind {
    SwatBehavior state;
    bool bot,queued;
    int team,order,pending_order,target,memory_ticks,replan_tick,command_tick,escort_owner;
    float resolve;
    float order_yaw,pending_yaw; // Authority planning state, reconstructed by input replay.
    int order_door,pending_door; // Zero is no doorway; world object zero is the floor.
    bool entry_settled;
    b3Pos memory,goal,waypoint,pending_goal;
} SwatMind;
typedef struct SwatNavigation {
    // A single lazy heap allocation owns the grid and route scratch. Small
    // training worlds retain their original size; wide motel ground keeps 60cm.
    unsigned char *walkable,(*links)[4],*dirty_cells,*dirty_edges;
    float *height,*x,*z;
    int32_t *parent;
    uint32_t *queue;
    int width,depth,cells,nodes;
    float min_x,min_z;
    unsigned char object_state[SWAT_MAX_OBJECTS];
    float top,bottom;
    int generation,count,built_tick,updated_cells;
    bool built;
} SwatNavigation;
typedef struct SwatEvidence { bool dropped,collected; int actor; b3Pos position; } SwatEvidence;
typedef struct SwatDebrief { int roe_violations,arrests,rescued,evidence; float unlawful_damage; } SwatDebrief;
struct SwatSim;
void swat_encounter_inputs(struct SwatSim* sim,SwatInput inputs[]);
void swat_encounter_orders(struct SwatSim* sim,const SwatInput inputs[]);
void swat_encounter_step(struct SwatSim* sim);
bool swat_navigation_crouch(const struct SwatSim* sim,b3Pos position);
bool swat_navigation_next(struct SwatSim* sim,b3Pos start,b3Pos goal,b3Pos* next);
// Same planner with locally visible people included; does not move the actor.
bool swat_navigation_next_for_actor(struct SwatSim* sim,int actor,b3Pos goal,b3Pos* next);
bool swat_navigation_yield_door(const struct SwatSim* sim,int actor,SwatInput* input);
bool swat_collect_evidence(struct SwatSim* sim,int actor);
const char* swat_squad_order_name(int order);
#endif
