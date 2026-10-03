#ifndef SWAT_EQUIPMENT_H
#define SWAT_EQUIPMENT_H
#include "swat_math.h"
#include <stdbool.h>
typedef enum SwatHitRegion { SWAT_HEAD, SWAT_TORSO, SWAT_ARMS, SWAT_LEGS, SWAT_HIT_REGIONS } SwatHitRegion;
typedef struct SwatKitDef {
    const char* name;
    const char* description;
    float mass, mobility, torso_protection;
    bool less_lethal, optiwand, ram;
} SwatKitDef;
#define SWAT_KIT_COUNT 3
typedef struct SwatEquipment {
    int kit, stunned_ticks, melee_cooldown, cuff_ticks, cuff_target;
    float wounds[SWAT_HIT_REGIONS];
    bool surrendered, restrained, inspecting, last_command, last_melee;
} SwatEquipment;
const SwatKitDef* swat_kit(int kit);
void swat_equipment_init(SwatEquipment* gear);
float swat_equipment_mobility(const SwatEquipment* gear);
#endif
