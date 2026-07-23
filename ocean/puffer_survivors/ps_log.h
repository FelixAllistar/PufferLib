#pragma once

#include "ps_constants.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Log {
    float perf;
    float score;
    float episode_return;
    float reward_survival;
    float reward_damage;
    float reward_kill;
    float reward_hurt;
    float reward_pickup;
    float reward_xp;
    float reward_levelup;
    float reward_obstacle;
    float reward_terminal;
    float episode_length;
    float kills;
    float level;
    float xp;
    float damage_dealt;
    float damage_taken;
    float pickups;
    float levelups;
    float obstacle_hits;
    float enemies_alive;
    float projectiles_alive;
    float drops_alive;
    float areas_alive;
    float weapon_levels;
    float wave;
    float hp;
    float survived;
    float n;
    float death_0_25;
    float death_25_50;
    float death_50_75;
    float death_75_100;
    float success;
    float peak_enemies;
    float peak_projectiles;
    float min_hp;
    float upgrade_counts[PS_UPGRADE_COUNT];
    float win_upgrade_counts[PS_UPGRADE_COUNT];
    float move_counts[9];
    float win_move_counts[9];
    float env_id;
};

#ifdef __cplusplus
}
#endif
