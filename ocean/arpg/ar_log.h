#pragma once

#include "ar_constants.h"

struct Log {
    float perf;
    float score;
    float episode_return;
    float episode_length;
    float reward_survival;
    float reward_kill;
    float reward_damage;
    float reward_hurt;
    float reward_summon;
    float reward_terminal;
    float kills;
    float summons;
    float pets_lost;
    float pets_alive;
    float damage_dealt;
    float damage_taken;
    float enemies_alive;
    float wave;
    float hp;
    float success;
    float n;
};
