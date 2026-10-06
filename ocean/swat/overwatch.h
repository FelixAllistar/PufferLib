#ifndef SWAT_OVERWATCH_H
#define SWAT_OVERWATCH_H
#include "controller.h"
#define SWAT_SNIPERS 2
typedef enum SwatSniperOrder { SWAT_SNIPER_NONE, SWAT_SNIPER_ASSIGN,
    SWAT_SNIPER_DESIGNATE, SWAT_SNIPER_EXECUTE, SWAT_SNIPER_HOLD, SWAT_SNIPER_ORDERS } SwatSniperOrder;
typedef enum SwatSniperStatus { SWAT_SNIPER_UNASSIGNED, SWAT_SNIPER_MOVING, SWAT_SNIPER_WATCHING,
    SWAT_SNIPER_READY, SWAT_SNIPER_BLOCKED, SWAT_SNIPER_CROSS_FIRE, SWAT_SNIPER_NO_TARGET,
    SWAT_SNIPER_RECOVERING, SWAT_SNIPER_DOWN, SWAT_SNIPER_STEADYING, SWAT_SNIPER_STATUSES } SwatSniperStatus;
typedef struct SwatSniper {
    bool deployed;
    int post,rifle,target,travel_ticks;
    float target_height;
    SwatSniperStatus status;
} SwatSniper;
struct SwatSim;
int swat_sniper_actor(int unit);
const char* swat_sniper_status(SwatSniperStatus status);
void swat_overwatch_inputs(struct SwatSim* sim,SwatInput* inputs);
bool swat_sniper_safe(const struct SwatSim* sim,int unit,b3Vec3 direction,float distance);
#endif
