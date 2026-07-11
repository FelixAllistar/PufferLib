#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float perf;
    float score;
    float episode_return;
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
} Log;

#ifdef __cplusplus
}
#endif
