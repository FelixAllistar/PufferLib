#ifndef SWAT_WEAPONS_H
#define SWAT_WEAPONS_H
#include "controller.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SwatFireMode { SWAT_SAFE, SWAT_SEMI, SWAT_AUTO } SwatFireMode;
typedef enum SwatReloadStage { SWAT_RELOAD_IDLE,SWAT_RELOAD_REMOVE,SWAT_RELOAD_INSERT,SWAT_RELOAD_CHAMBER,SWAT_RELOAD_STAGES } SwatReloadStage;
typedef enum SwatSightProfile { SWAT_IRONS,SWAT_RED_DOT,SWAT_OPTIC,SWAT_SIGHTS } SwatSightProfile;
#define SWAT_WEAPON_PROFILES 12
#define SWAT_MAGAZINES 4
// Events from one swat_weapons_step call, kept on each weapon until its next
// step. They report the staged reload and never drive it. They stay on the
// authority: snapshots, replays and digests do not encode them.
typedef enum SwatWeaponEvent {
    SWAT_WEAPON_EVENT_RELOAD_START=1u<<0,     // timer started at REMOVE, or at INSERT with no seated magazine
    SWAT_WEAPON_EVENT_MAGAZINE_OUT=1u<<1,     // REMOVE -> INSERT: the seated magazine was stowed or pooled
    SWAT_WEAPON_EVENT_MAGAZINE_IN=1u<<2,      // INSERT -> CHAMBER: the next magazine was seated
    SWAT_WEAPON_EVENT_RELOAD_DONE=1u<<3,      // timer finished; the weapon is idle again
    SWAT_WEAPON_EVENT_ROUND_CHAMBERED=1u<<4,  // completion loaded an empty chamber
    SWAT_WEAPON_EVENT_RELOAD_CANCELLED=1u<<5, // cancel_reload interrupted an active reload
    SWAT_WEAPON_EVENT_RELOAD_SWITCHED=1u<<6,  // a weapon switch interrupted an active reload
} SwatWeaponEvent;
#define SWAT_WEAPON_EVENT_INTERRUPTED (SWAT_WEAPON_EVENT_RELOAD_CANCELLED|SWAT_WEAPON_EVENT_RELOAD_SWITCHED)
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
    unsigned int events; // SwatWeaponEvent bits from the latest swat_weapons_step
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

// Optional profile data. The compiled table is the default, and it stays the
// authority unless a file is loaded explicitly at startup, before any
// simulation exists. A file sets numbers on the existing twelve profiles. It
// cannot add profiles or change index-keyed behavior (launcher payloads,
// breach rounds, probes). Format, ranges and examples: EQUIPMENT.md.
#define SWAT_WEAPON_DATA_VERSION 1
#define SWAT_WEAPON_NAME_SIZE 32
// All or nothing: a bad file leaves the active table as it was, returns false,
// and writes "path:line: reason" to error.
bool swat_weapons_load(const char* path,char* error,size_t capacity);
// Writes every field of the active table in the format the loader reads.
bool swat_weapons_save(const char* path);
void swat_weapons_reset(void);
// 0 while the active table equals the compiled one, file or not. Otherwise a
// digest of every field with the top bit set. Replays and the connect
// handshake check this value.
uint32_t swat_weapons_table_id(void);
const SwatWeaponDef* swat_weapon_builtin_def(int profile);
const SwatWeaponDef* swat_weapon_def(int slot);
const SwatWeaponDef* swat_arsenal_def(const SwatArsenal* arsenal,int slot);
void swat_weapons_primary(SwatArsenal* arsenal,int definition);
void swat_weapons_init(SwatArsenal* arsenal, uint32_t seed);
bool swat_weapons_busy(const SwatArsenal* arsenal);
SwatShot swat_weapons_step(SwatArsenal* arsenal, const SwatInput* input,
                           float ads, float speed, bool grounded, bool blocked);
int swat_weapon_rounds(const SwatWeapon* weapon);
// Elapsed reload ticks at which a stage completes: REMOVE at duration/4,
// INSERT at duration*2/3, CHAMBER at duration. IDLE returns 0.
int swat_reload_stage_end(int duration,SwatReloadStage stage);
// Clears the reload timer. Emits no event; swat_weapons_step reports the
// interrupts it causes itself.
void swat_weapons_cancel_reload(SwatWeapon* weapon);
void swat_weapons_magazines(SwatWeapon* weapon,bool enabled,int capacity);

#ifdef __cplusplus
}
#endif
#endif
