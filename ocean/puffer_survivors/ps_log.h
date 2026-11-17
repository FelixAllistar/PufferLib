#pragma once

#include "ps_constants.h"

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
    float min_hp;
    float dashes;
    // Fun scorecard: the product of the nine band scores, then each score.
    float fun;
    float fun_tension;
    float fun_damage;
    float fun_survival;
    float fun_coverage;
    float fun_combo;
    float fun_burst;
    float fun_growth;
    float fun_straight;
    float fun_crowd;
#ifndef PUFFER_GPU_ENV
    // The raw value behind each score. CPU builds only: see the size limit below.
    float raw_tension;
    float raw_damage;
    float raw_survival;
    float raw_coverage;
    float raw_combo;
    float raw_burst;
    float raw_growth;
    float raw_straight;
    float raw_crowd;
#endif
};

#ifdef PUFFER_GPU_ENV
// The trainer sums GPU logs in one block of 256 threads with sizeof(Log) * 256 bytes of
// dynamic shared memory. Past 48 floats that is over the 48 KB launch limit, the launch
// fails and env/* stats go missing without an error.
PS_STATIC_ASSERT(sizeof(struct Log) <= 48 * sizeof(float), "GPU Log must stay within 48 floats");
#endif
