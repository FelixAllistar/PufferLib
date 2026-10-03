#ifndef SWAT_WEAPONS_H
#define SWAT_WEAPONS_H
#include "controller.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SwatFireMode { SWAT_SAFE, SWAT_SEMI, SWAT_AUTO } SwatFireMode;
typedef struct SwatWeaponDef {
    const char* name;
    int capacity, shot_ticks, reload_ticks, empty_reload_ticks, equip_ticks;
    float damage, range, energy, hip_spread, ads_spread, recoil;
    bool automatic;
} SwatWeaponDef;

typedef struct SwatWeapon {
    int magazine, reserve, cooldown, reload_remaining, reload_duration;
    bool chambered;
    SwatFireMode mode;
} SwatWeapon;

typedef struct SwatArsenal {
    SwatWeapon slots[2];
    int active, equip_remaining;
    int primary; // definition index: 0 carbine, 2 impact launcher
    bool last_fire, last_reload, last_selector;
    int shots;
    uint32_t rng;
} SwatArsenal;

typedef struct SwatShot {
    bool fired;
    float damage, range, energy, pitch_offset, yaw_offset, recoil_yaw;
} SwatShot;

const SwatWeaponDef* swat_weapon_def(int slot);
const SwatWeaponDef* swat_arsenal_def(const SwatArsenal* arsenal,int slot);
void swat_weapons_primary(SwatArsenal* arsenal,int definition);
void swat_weapons_init(SwatArsenal* arsenal, uint32_t seed);
bool swat_weapons_busy(const SwatArsenal* arsenal);
SwatShot swat_weapons_step(SwatArsenal* arsenal, const SwatInput* input,
                           float ads, float speed, bool grounded, bool blocked);
int swat_weapon_rounds(const SwatWeapon* weapon);

#ifdef __cplusplus
}
#endif
#endif
