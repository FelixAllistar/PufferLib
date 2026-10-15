// Opt-in counters for the standalone performance tool; normal play keeps them off.
#ifndef SWAT_DAMAGE_PROFILE_H
#define SWAT_DAMAGE_PROFILE_H
#include "box3d/box3d.h"
typedef struct SwatDamageProfile {
    bool enabled;
    uint64_t support_scans,hulls,shape_rebuilds,shapes_created,shapes_destroyed,door_builds,door_vertices;
    double support_ms,hull_ms,collision_ms,door_ms,upload_ms;
} SwatDamageProfile;
extern _Thread_local SwatDamageProfile swat_damage_profile;
static inline uint64_t swat_profile_begin(void){return swat_damage_profile.enabled?b3GetTicks():0;}
static inline void swat_profile_end(double* total,uint64_t start){if(swat_damage_profile.enabled)*total+=b3GetMilliseconds(start);}
#define SWAT_PROFILE_ADD(field,value) do {if(swat_damage_profile.enabled)swat_damage_profile.field+=(value);}while(0)
#endif
