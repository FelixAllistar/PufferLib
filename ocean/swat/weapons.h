#ifndef SWAT_WEAPONS_H
#define SWAT_WEAPONS_H
#include "controller.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SwatFireMode { SWAT_SAFE, SWAT_SEMI, SWAT_AUTO } SwatFireMode;
typedef enum SwatReloadStage { SWAT_RELOAD_IDLE,SWAT_RELOAD_REMOVE,SWAT_RELOAD_INSERT,SWAT_RELOAD_CHAMBER,SWAT_RELOAD_STAGES } SwatReloadStage;
typedef enum SwatSightProfile { SWAT_IRONS,SWAT_RED_DOT,SWAT_OPTIC,SWAT_SIGHTS } SwatSightProfile;
#define SWAT_WEAPON_PROFILES 8
#define SWAT_MAGAZINES 4
typedef struct SwatWeaponDef {
    const char* name;
    int capacity, shot_ticks, reload_ticks, empty_reload_ticks, equip_ticks;
    float damage, range, energy, hip_spread, ads_spread, recoil;
    bool automatic;
    float barrel, sight_height;
} SwatWeaponDef;

typedef struct SwatWeapon {
    int magazine, reserve, cooldown, reload_remaining, reload_duration;
    bool chambered;
    SwatFireMode mode;
    SwatReloadStage reload_stage;
    bool magazine_seated, use_magazines;
    int magazines[SWAT_MAGAZINES], magazine_count;
} SwatWeapon;

typedef struct SwatArsenal {
    SwatWeapon slots[2];
    int active, equip_remaining;
    int primary; // 0 carbine, 2 impact launcher, 3 precision, 4 marksman
    bool last_fire, last_reload, last_selector;
    int shots;
    uint32_t rng;
    int sight,fire_buffer_ticks,reload_buffer_ticks;
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
void swat_weapons_cancel_reload(SwatWeapon* weapon);
void swat_weapons_magazines(SwatWeapon* weapon,bool enabled,int capacity);

#ifdef __cplusplus
}
#endif
#endif
