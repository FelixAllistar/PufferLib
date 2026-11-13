#ifndef SWAT_ENCOUNTER_H
#define SWAT_ENCOUNTER_H
#include "controller.h"
#define SWAT_NAV_SIDE 108
#define SWAT_NAV_CELLS (SWAT_NAV_SIDE*SWAT_NAV_SIDE)
#define SWAT_NAV_LAYERS 4
#define SWAT_NAV_NODES (SWAT_NAV_CELLS*SWAT_NAV_LAYERS)
typedef enum SwatBehavior { SWAT_IDLE,SWAT_INVESTIGATE,SWAT_ENGAGE,SWAT_FLEE,SWAT_SURRENDER,SWAT_ESCORT,SWAT_DETAINED } SwatBehavior;
typedef enum SwatSquadOrder { SWAT_ORDER_NONE,SWAT_ORDER_FALL_IN,SWAT_ORDER_HOLD,SWAT_ORDER_MOVE,SWAT_ORDER_STACK,SWAT_ORDER_CLEAR,SWAT_ORDER_PICK,SWAT_ORDER_WEDGE,SWAT_ORDER_CUFF,SWAT_ORDER_SEARCH,SWAT_ORDER_COVER,SWAT_ORDER_ROOM_SEARCH,SWAT_SQUAD_ORDERS } SwatSquadOrder;
#define SWAT_ROOM_SEARCH_POINTS 9
// Private civilian fear reaction. The public behaviour stays SWAT_FLEE, so the
// wire and its v6 range check are unchanged; stance and motion show the rest.
typedef enum SwatPanic { SWAT_PANIC_NONE,SWAT_PANIC_FREEZE,SWAT_PANIC_COWER,SWAT_PANIC_FLEE } SwatPanic;
#define SWAT_PANIC_TICKS 300
// A suspect's answer to pressure (surrender_impl.h). COMPLY kneels and drops
// the weapon; DROP throws the weapon down and runs; FEINT kneels with the
// weapon hidden and draws when unguarded; FLEE runs armed; FIGHT carries on.
// Commands older than this (10 s) no longer add pressure.
#define SWAT_YIELD_COMMAND_WINDOW 600
// A feigned surrender holds at least 2 s, then waits for an unguarded moment.
#define SWAT_FEINT_MIN_TICKS 120
#define SWAT_FEINT_SPREAD 180
// A flight lasts up to 4 s before the suspect re-evaluates from cover.
#define SWAT_FLEE_TICKS 240
// After a flight the suspect spends up to 1 s turning back toward the officer.
#define SWAT_LOOK_BACK_TICKS 60
// A civilian obeying an order they only heard walks up to 4 m to a wall or
// corner, and gets down where they are after 4 s at most.
#define SWAT_COMPLY_RADIUS 4.0f
#define SWAT_COMPLY_TICKS 240

typedef enum SwatYield { SWAT_YIELD_NONE,SWAT_YIELD_COMPLY,SWAT_YIELD_DROP,SWAT_YIELD_FEINT,SWAT_YIELD_FLEE,SWAT_YIELD_FIGHT,SWAT_YIELDS } SwatYield;
// What one suspect perceives when weighing surrender. pressure is the weighted
// sum documented in ARCHITECTURE.md; will is the suspect's resolve.
typedef struct SwatPressure { int commands,officers,aimed,allies; float wounds,ammo,pressure,will; } SwatPressure;
typedef enum SwatRoomTask { SWAT_ROOM_TASK_NONE,SWAT_ROOM_TASK_PERSON,SWAT_ROOM_TASK_WEAPON,SWAT_ROOM_TASK_ESCORT,SWAT_ROOM_TASK_REPOSITION,SWAT_ROOM_TASK_CLOSE_DOOR,SWAT_ROOM_TASKS } SwatRoomTask;
typedef struct SwatMind {
    SwatBehavior state;
    bool bot,queued;
    int team,order,pending_order,target,memory_ticks,replan_tick,command_tick,escort_owner;
    float resolve;
    float order_yaw,pending_yaw; // Authority planning state, reconstructed by input replay.
    int order_door,pending_door; // Zero is no doorway; world object zero is the floor.
    int order_actor,pending_actor; // Person/evidence owner + 1; zero is no directed task.
    int order_room,pending_room,room_step,room_task; // Room IDs + 1; completion is a searched route, not an all-clear claim.
    int room_scan; // Authority scan dwell, reconstructed by input replay.
    bool room_goal,room_blocked;
    bool entry_settled;
    bool tool_goal,tool_blocked,tool_crouch;
    int tool_replan_tick,tool_generation,tool_scan; // Bounded private surface search.
    bool restraint_goal;
    int restraint_replan_tick,restraint_actor; // Private approach cache; replay reconstructs it.
    b3Pos memory,goal,waypoint,pending_goal,task_point,room_point; // Authority planning state, reconstructed by input replay.
    int room_tool_ticks;
    int fire_retry_tick; // Private bounded retry after an observed lane blocks routing.
    bool clamber; // Current waypoint crosses a clamber link; replay reconstructs it.
    // Civilian fear (SwatPanic), private authority state that input replay
    // reconstructs. panic_from is a perceived point: a seen gunman's eye or a
    // coarse point along a heard bearing, never a hidden actor position.
    int panic,panic_ticks,panic_count,panic_plan_tick,panic_deadline;
    int police_tick; // Tick + 1 when an officer was last in this civilian's sight.
    bool panic_seen,panic_known,panic_goal_valid;
    bool panic_route; // This tick's movement is a panic route (stricter fire lanes, no door use).
    b3Pos panic_from,panic_goal;
    // Inputs come from an outside policy (role training): surrender is the
    // policy's own choice, and commands use the role contract's fixed rule.
    bool external;
    // Surrender decisions (SwatYield), private authority state that input
    // replay reconstructs. yield_from is a perceived officer eye position.
    int yield,yield_count,yield_until,yield_plan_tick;
    int heard_commands,heard_command_tick; // Commands heard in the current window; tick + 1 of the latest.
    bool yield_dry,yield_goal_valid;
    b3Pos yield_from,yield_goal;
    // A civilian who obeys an order they only heard walks to a safe spot
    // (comply_goal) before getting down; private authority state that input
    // replay reconstructs. comply_until is the tick they get down regardless.
    bool complying;
    int comply_until;
    b3Pos comply_goal;
    // Squad bot challenge and cover record, private authority state that
    // input replay reconstructs. challenge_actor is the person (+ 1) this bot's
    // own command last reached (the clock is the ROE record's challenged_tick);
    // cover_actor and cover_tick (tick + 1) mark when it began covering a
    // person whose hands it cannot see.
    int challenge_actor,cover_actor,cover_tick;
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
    uint32_t object_state[SWAT_MAX_OBJECTS];
    float top,bottom;
    int generation,count,built_tick,updated_cells;
    bool built;
} SwatNavigation;
typedef struct SwatEvidence { bool dropped,collected; int actor; b3Pos position; } SwatEvidence;
// roe_violations counts unlawful-force engagements (SWAT_ROE_ENGAGEMENT_TICKS
// in sim.h), not rounds; unlawful_damage is the total health those hits removed.
// lethal_violations counts engagements of lethal officer/sniper force against
// a person whose perceived ROE state (roe.h) was not a lethal threat; feints
// counts surrenders officers saw end in a drawn weapon or flight.
typedef struct SwatDebrief { int roe_violations,arrests,rescued,evidence; float unlawful_damage; int lethal_violations,feints; } SwatDebrief;
struct SwatSim;
void swat_encounter_inputs(struct SwatSim* sim,SwatInput inputs[]);
void swat_encounter_orders(struct SwatSim* sim,const SwatInput inputs[]);
void swat_encounter_step(struct SwatSim* sim);
bool swat_navigation_crouch(const struct SwatSim* sim,b3Pos position);
// Closest supported sample within 2 m and 65 cm height; -1 outside the grid.
int swat_navigation_nearest(const SwatNavigation* nav,b3Pos position);
bool swat_navigation_next(struct SwatSim* sim,b3Pos start,b3Pos goal,b3Pos* next);
// Same planner with locally visible people included; does not move the actor.
bool swat_navigation_next_for_actor(struct SwatSim* sim,int actor,b3Pos goal,b3Pos* next);
bool swat_navigation_yield_door(const struct SwatSim* sim,int actor,SwatInput* input);
bool swat_collect_evidence(struct SwatSim* sim,int actor);
// This suspect or civilian heard an officer's command (tactical rules).
// heard_only: they could not see who gave it, nor did an officer giving it in
// person see them. arrival is the direction the sound came from at their eye
// (SwatAcousticPath.direction).
void swat_person_commanded(struct SwatSim* sim,int officer,int person,bool heard_only,b3Vec3 arrival);
// The suspect's perceived pressure; commander counts as seen (-1 for none).
SwatPressure swat_suspect_pressure(const struct SwatSim* sim,int suspect,int commander);
// One deterministic decision from that pressure; advances the decision count.
SwatYield swat_suspect_choose(struct SwatSim* sim,int suspect,const SwatPressure* pressure);
const char* swat_squad_order_name(int order);
// Directed tasks acknowledge one recipient; formation orders need the team.
bool swat_squad_order_accepted(const struct SwatSim* sim,int team,int order);
#endif
