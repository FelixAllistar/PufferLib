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
    int breaching_charges,wall_charges;
    bool lockpicks;
} SwatKitDef;
#define SWAT_KIT_COUNT 3
typedef enum SwatWandMode { SWAT_WAND_FORWARD, SWAT_WAND_UNDER,
    SWAT_WAND_LEFT, SWAT_WAND_RIGHT, SWAT_WAND_OVER, SWAT_WAND_MODES } SwatWandMode;
// A kneeling detainee picked up for escort stands in place for the authored
// RestrainedKneelToStand clip (4.2 s) before following, so the body never slides.
#define SWAT_RISE_TICKS 252
// Authoritative hit reaction for suspects and civilians under tactical rules:
// 0.3 s with fire blocked (buffered shots dropped) after a small aim kick.
// Replicated in gear.flinch_ticks for presentation.
#define SWAT_FLINCH_TICKS 18
#define SWAT_FLINCH_PITCH (3*SWAT_RAD)
#define SWAT_FLINCH_YAW (1.5f*SWAT_RAD)
typedef struct SwatEquipment {
    int kit, stunned_ticks, melee_cooldown, cuff_ticks, cuff_target;
    int rise_ticks; // escorted detainee standing up in place before following
    float wounds[SWAT_HIT_REGIONS];
    bool surrendered, restrained, inspecting, last_command, last_melee;
    int flash_ticks,gas_ticks,taser_cooldown,throw_cooldown;
    int flashbangs,gas_grenades,taser_charges;
    bool last_throw,last_taser,used_tools;
    SwatWandMode wand_mode;
    float wand_yaw,wand_pitch;
    int wedges,spray_ticks,tether_ticks;
    int devices[4];
    bool last_device;
    bool door_completed;
    int breaching_charges,wall_charges,door_ticks,door_target,door_mode,last_door_tool;
    int flinch_ticks; // hit reaction remaining, SWAT_FLINCH_TICKS..0
} SwatEquipment;
const SwatKitDef* swat_kit(int kit);
void swat_equipment_init(SwatEquipment* gear);
void swat_equipment_kit(SwatEquipment* gear,int kit);
float swat_equipment_mobility(const SwatEquipment* gear);
#endif
