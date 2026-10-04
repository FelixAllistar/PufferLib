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
    bool gas_mask;
    int flashbangs, gas_grenades, taser_charges;
    int breaching_charges;
} SwatKitDef;
#define SWAT_KIT_COUNT 3
typedef enum SwatWandMode { SWAT_WAND_FORWARD, SWAT_WAND_UNDER,
    SWAT_WAND_LEFT, SWAT_WAND_RIGHT, SWAT_WAND_OVER, SWAT_WAND_MODES } SwatWandMode;
typedef struct SwatEquipment {
    int kit, stunned_ticks, melee_cooldown, cuff_ticks, cuff_target;
    float wounds[SWAT_HIT_REGIONS];
    bool surrendered, restrained, inspecting, last_command, last_melee;
    int flash_ticks,gas_ticks,taser_cooldown,throw_cooldown;
    int flashbangs,gas_grenades,taser_charges;
    bool last_throw,last_taser,used_tools;
    SwatWandMode wand_mode;
    float wand_yaw,wand_pitch;
    int breaching_charges,door_ticks,door_target,door_mode,last_door_tool;
} SwatEquipment;
const SwatKitDef* swat_kit(int kit);
void swat_equipment_init(SwatEquipment* gear);
void swat_equipment_kit(SwatEquipment* gear,int kit);
float swat_equipment_mobility(const SwatEquipment* gear);
#endif
