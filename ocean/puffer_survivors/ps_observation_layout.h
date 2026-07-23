#pragma once

#include "ps_constants.h"

enum {
    PS_OBS_PLAYER_BASE = 0,
    PS_OBS_BOSS_BASE = PS_OBS_PLAYER_BASE + PS_PLAYER_FEATURES,
    PS_OBS_ENEMY_BASE = PS_OBS_BOSS_BASE + PS_BOSS_FEATURES,
    PS_OBS_DROP_BASE = PS_OBS_ENEMY_BASE + PS_SECTORS * PS_RINGS * PS_ENEMY_CHANNELS,
    PS_OBS_OBSTACLE_BASE = PS_OBS_DROP_BASE + PS_SECTORS * PS_RINGS * PS_DROP_CHANNELS,
    PS_OBS_DANGER_BASE = PS_OBS_OBSTACLE_BASE + PS_SECTORS * PS_RINGS * PS_OBSTACLE_CHANNELS,
    PS_OBS_AREA_BASE = PS_OBS_DANGER_BASE + PS_SECTORS * PS_DANGER_CHANNELS,
    PS_OBS_WEAPON_BASE = PS_OBS_AREA_BASE + PS_SECTORS * PS_AREA_CHANNELS,
    PS_OBS_UPGRADE_BASE = PS_OBS_WEAPON_BASE + PS_WEAPON_FEATURES,
    PS_OBS_EXACT_OBSTACLE_BASE = PS_OBS_UPGRADE_BASE + PS_UPGRADE_SLOTS * PS_UPGRADE_FEATURES,
    PS_OBS_END = PS_OBS_EXACT_OBSTACLE_BASE + PS_EXACT_OBSTACLE_FEATURES,
};

// Runtime-selectable legacy/POI slots. Version 6 contains projectile count and
// Bubble readiness; version 7 replaces those with exact health-drop direction;
// Version 8 was the experimental semantic-replacement layout. Version 9 keeps
// the complete v6 prefix intact and appends exact obstacle dx/dy bins, allowing
// legacy checkpoints to migrate by zero-padding only the new encoder columns.
enum {
    PS_PLAYER_ALT_A = 9,
    PS_PLAYER_ALT_B = 11,
    PS_PLAYER_LETHAL_THREAT = 23,
    PS_PLAYER_ALT_C = 24,
    PS_PLAYER_NEAREST_XP_DX = 25,
    PS_PLAYER_NEAREST_XP_DY = 26,
    PS_PLAYER_ALT_D = 27,
};

enum {
    PS_BOSS_PRESENT = 0,
    PS_BOSS_DX = 1,
    PS_BOSS_DY = 2,
    PS_BOSS_PROXIMITY = 3,
    PS_BOSS_HP_FRACTION = 4,
    PS_BOSS_MAX_HP = 5,
    PS_BOSS_CLOSING_SPEED = 6,
    PS_BOSS_COUNT = 7,
};

#if defined(__cplusplus)
static_assert(PS_OBS_END == PS_OBS_SIZE, "Observation layout does not match PS_OBS_SIZE");
#else
_Static_assert(PS_OBS_END == PS_OBS_SIZE, "Observation layout does not match PS_OBS_SIZE");
#endif
