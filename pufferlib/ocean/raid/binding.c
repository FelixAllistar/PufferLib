#include "raid.h"

#define Env Raid
#include "../env_binding.h"

static int my_init(Env* env, PyObject* args, PyObject* kwargs) {
    env->arena_width = unpack(kwargs, "arena_width");
    env->arena_height = unpack(kwargs, "arena_height");
    env->num_players = unpack(kwargs, "num_players");
    env->max_episode_ticks = unpack(kwargs, "max_episode_ticks");
    env->player_damage = unpack(kwargs, "player_damage");
    env->player_hit_chance = unpack(kwargs, "player_hit_chance");
    env->olm_base_damage = unpack(kwargs, "olm_base_damage");
    env->prayer_reduction = unpack(kwargs, "prayer_reduction");
    env->reward_claw_imbalance = unpack(kwargs, "reward_claw_imbalance");
    init(env);
    return 0;
}

static int my_log(PyObject* dict, Log* log) {
    assign_to_dict(dict, "perf", log->perf);
    assign_to_dict(dict, "score", log->score);
    assign_to_dict(dict, "episode_return", log->episode_return);
    assign_to_dict(dict, "episode_length", log->episode_length);
    assign_to_dict(dict, "damage_dealt", log->damage_dealt);
    assign_to_dict(dict, "damage_taken", log->damage_taken);
    assign_to_dict(dict, "olm_kills", log->olm_kills);
    assign_to_dict(dict, "player_deaths", log->player_deaths);
    return 0;
}
