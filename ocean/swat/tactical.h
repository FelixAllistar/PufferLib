#ifndef SWAT_TACTICAL_H
#define SWAT_TACTICAL_H
#include "world.h"
#define SWAT_MAX_PROJECTILES 16
typedef enum SwatProjectileKind { SWAT_FLASHBANG, SWAT_CS_GAS, SWAT_PROJECTILE_KINDS } SwatProjectileKind;
typedef struct SwatProjectile {
    SwatTag tag;
    b3BodyId body;
    b3ShapeId shape;
    b3Pos position;
    b3Vec3 velocity;
    SwatProjectileKind kind;
    int owner,age,remaining_ticks,last_impact_tick;
    bool active,detonated;
} SwatProjectile;
struct SwatSim;
typedef enum SwatContextAction { SWAT_CONTEXT_NONE,SWAT_CONTEXT_OPEN,SWAT_CONTEXT_CLOSE,
    SWAT_CONTEXT_LOCKED,SWAT_CONTEXT_CUFF,SWAT_CONTEXT_COMPLY,SWAT_CONTEXT_SECURED,
    SWAT_CONTEXT_CHARGE } SwatContextAction;
typedef struct SwatContext {
    SwatHit hit;
    SwatContextAction action;
    bool ready;
} SwatContext;
// A narrow ray fan tolerates small reticle errors. Every sample checks real
// cover; physical interactions still use their original reach limits.
SwatHit swat_context_hit(const struct SwatSim* sim,int actor,float range);
SwatContext swat_context(const struct SwatSim* sim,int actor);
// Throws use a sphere sweep for hand clearance, then the authoritative Box3D
// world integrates flight, bounce, rolling and contact impulses.
bool swat_throw(struct SwatSim* sim,int actor,SwatProjectileKind kind);
bool swat_taser(struct SwatSim* sim,int actor);
struct SwatInput;
void swat_door_tools(struct SwatSim* sim,int actor,struct SwatInput* input);
void swat_projectiles_step(struct SwatSim* sim);
float swat_gas_at(const struct SwatSim* sim,b3Pos position);
#endif
