#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../abyss.h"

static float rep_reserve_fraction = 0.30f;
static float weather_penalty_override = -1.0f;

static int first_alive_hostile(Env* env) {
    int best = -1;
    float best_dps = -1.0f;
    for (int i = 0; i < env->entity_count; i++) {
        AbyssEntity* entity = &env->entities[i];
        if (!entity->alive || !entity->gate_required) continue;
        float dps = entity->turret_dps + entity->missile_dps;
        if (dps > best_dps) {
            best = i;
            best_dps = dps;
        }
    }
    return best;
}

static int find_cache(Env* env) {
    for (int i = 0; i < env->entity_count; i++)
        if (env->entities[i].kind == ENTITY_CACHE) return i;
    return -1;
}

static int first_unlocked_damageable(Env* env) {
    int hostile = first_alive_hostile(env);
    int cache = find_cache(env);
    int candidates[2] = {hostile, cache};
    for (int c = 0; c < 2; c++) {
        int index = candidates[c];
        if (index < 0) continue;
        AbyssEntity* entity = &env->entities[index];
        if (!ab_damageable(entity) || entity->locked || entity->locking) continue;
        float distance = ab_len(ab_sub(entity->pos, env->ship_pos));
        if (distance <= env->lock_range) return index;
    }
    return -1;
}

static void rules_action(Env* env, int strategy) {
    float* action = env->agents[0].actions;
    memset(action, 0, NUM_ATNS * sizeof(float));

    int cache = find_cache(env);
    int gate = ab_find_kind(env, ENTITY_CONDUIT);
    int hostile = first_alive_hostile(env);
    int cache_alive = cache >= 0 && env->entities[cache].alive;

    int lock = first_unlocked_damageable(env);
    if (lock >= 0)
        action[1] = TARGET_LOCK_BASE + ab_slot_for_entity(env, lock);

    int weapon = -1;
    if (strategy > 0 && hostile >= 0 && env->entities[hostile].locked) {
        float distance = ab_len(ab_sub(
            env->entities[hostile].pos, env->ship_pos));
        float useful_range = env->weapon_optimal *
            env->weather_range_multiplier + 2.0f * env->weapon_falloff *
            env->weather_range_multiplier;
        if ((strategy != 3 && strategy != 4) || distance <= useful_range)
            weapon = hostile;
    }
    else if (cache_alive && env->entities[cache].locked)
        weapon = cache;
    else if (hostile >= 0 && env->entities[hostile].locked)
        weapon = hostile;
    if (weapon >= 0 && env->weapon_desired_target_index != weapon)
        action[2] = WEAPON_FIRE_BASE + ab_slot_for_entity(env, weapon);

    float armor_fraction = env->armor /
        fmaxf(1.0f, env->ship_armor_hp *
            (env->weather_type == WEATHER_FIRESTORM ? 1.5f : 1.0f));
    float rep_on_fraction = strategy >= 2 ? 0.99f : 0.90f;
    float rep_reserve = strategy == 4 ?
        rep_reserve_fraction * env->cap_capacity :
        env->rep_cap_cost;
    if (!env->rep_on && armor_fraction < rep_on_fraction &&
        env->capacitor >= rep_reserve)
        action[4] = DESIRED_ON;
    else if (env->rep_on && (armor_fraction > 0.97f ||
        env->capacitor < rep_reserve))
        action[4] = DESIRED_OFF;

    int nav = strategy == 2 && hostile >= 0 ? hostile :
        ((strategy == 3 || strategy == 4) && hostile >= 0 ? -1 :
            (!env->cache_looted ? cache : gate));
    if ((strategy == 3 || strategy == 4) && hostile >= 0 &&
        env->navigation_target_index >= 0)
        action[0] = NAV_STOP;
    if (nav >= 0 && nav != env->navigation_target_index)
        action[0] = NAV_APPROACH_BASE + ab_slot_for_entity(env, nav);
    float nav_distance = nav >= 0 ? ab_surface_distance(env, &env->entities[nav]) : 0;
    int want_prop = nav_distance > 9000.0f && env->capacitor >= env->prop_cap_cost;
    if (want_prop != env->prop_desired_on)
        action[3] = want_prop ? DESIRED_ON : DESIRED_OFF;

    if (cache >= 0 && !env->entities[cache].alive && !env->cache_looted) {
        if (env->cargo_open)
            action[5] = INTERACT_LOOT;
        else
            action[5] = INTERACT_OPEN_BASE + ab_slot_for_entity(env, cache);
    } else if (gate >= 0 && hostile < 0 && env->cache_looted) {
        action[5] = INTERACT_ACTIVATE_BASE + ab_slot_for_entity(env, gate);
    }
}

static void evaluate(int scenario, int strategy, int episodes) {
    Ini ini = {0};
    puf_ini_load_env(&ini, "abyss", 0, NULL);
    Dict* cfg = puf_ini_section(&ini, "env", 0);
    Env env = {0};
    obs_t observations[OBS_SIZE] = {0};
    float actions[NUM_ATNS] = {0};
    float rewards[1] = {0};
    float terminals[1] = {0};
    unsigned char action_mask[ABYSS_ACTION_MASK_SIZE] = {0};
    env.agents[0].observations = observations;
    env.agents[0].actions = actions;
    env.agents[0].rewards = rewards;
    env.agents[0].terminals = terminals;
    env.rng = 1;
    puf_init(&env, cfg);
    env.agents[0].action_mask = action_mask;
    env.configured_scenario_episode = scenario;

    int completed = 0;
    int survived = 0;
    int rooms = 0;
    int caches = 0;
    double ticks = 0;
    for (int episode = 0; episode < episodes; episode++) {
        terminals[0] = 0;
        puf_reset(&env);
        if (weather_penalty_override >= 0) {
            env.weather_penalty = weather_penalty_override;
            env.weather_range_multiplier = env.weather_type == WEATHER_DARK ?
                1.0f - weather_penalty_override : 1.0f;
        }
        while (!terminals[0]) {
            rules_action(&env, strategy);
            puf_step(&env);
        }
        completed += env.rooms_cleared == 3 && env.caches_looted == 3;
        survived += env.hull > 0;
        rooms += env.rooms_cleared;
        caches += env.caches_looted;
        ticks += env.tick;
    }
    printf("scenario=%02d priority=%s completion=%.6f survival=%.6f "
        "rooms=%.3f caches=%.3f ticks=%.2f",
        scenario, strategy == 4 ? "threat_reserve" :
            (strategy == 3 ? "threat_wait" :
                (strategy == 2 ? "threat_rush" :
                    (strategy == 1 ? "threat" : "cache"))),
        completed / (double)episodes, survived / (double)episodes,
        rooms / (double)episodes, caches / (double)episodes, ticks / episodes);
    if (episodes == 1) {
        int cache = find_cache(&env);
        printf(" room=%d threats=%d cache_alive=%d cache_dist=%.1f "
            "nav=%d weapon=%d desired=%d cargo=%d interaction=%d",
            env.room, ab_gate_targets_alive(&env),
            cache >= 0 ? env.entities[cache].alive : -1,
            cache >= 0 ? ab_surface_distance(&env, &env.entities[cache]) : -1,
            env.navigation_target_index, env.weapon_target_index,
            env.weapon_desired_target_index, env.cargo_open, env.interaction_kind);
    }
    printf("\n");
}

static void trace_first_failure(int scenario, int strategy) {
    Ini ini = {0};
    puf_ini_load_env(&ini, "abyss", 0, NULL);
    Dict* cfg = puf_ini_section(&ini, "env", 0);
    Env env = {0};
    obs_t observations[OBS_SIZE] = {0};
    float actions[NUM_ATNS] = {0};
    float rewards[1] = {0};
    float terminals[1] = {0};
    unsigned char action_mask[ABYSS_ACTION_MASK_SIZE] = {0};
    env.agents[0].observations = observations;
    env.agents[0].actions = actions;
    env.agents[0].rewards = rewards;
    env.agents[0].terminals = terminals;
    env.rng = 1;
    puf_init(&env, cfg);
    env.agents[0].action_mask = action_mask;
    env.configured_scenario_episode = scenario;

    unsigned int failed_seed = 0;
    for (int episode = 0; episode < 100000 && failed_seed == 0; episode++) {
        unsigned int seed = env.rng;
        terminals[0] = 0;
        puf_reset(&env);
        while (!terminals[0]) {
            rules_action(&env, strategy);
            puf_step(&env);
        }
        if (env.hull <= 0) failed_seed = seed;
    }
    if (failed_seed == 0) {
        printf("no failure found\n");
        return;
    }

    env.rng = failed_seed;
    terminals[0] = 0;
    puf_reset(&env);
    printf("trace scenario=%d strategy=%d seed=%u weather_penalty=%.2f\n",
        scenario, strategy, failed_seed, env.weather_penalty);
    while (!terminals[0]) {
        int hostile = first_alive_hostile(&env);
        float distance = hostile >= 0 ?
            ab_len(ab_sub(env.entities[hostile].pos, env.ship_pos)) : -1.0f;
        float hostile_hp = hostile >= 0 ?
            env.entities[hostile].shield + env.entities[hostile].armor +
                env.entities[hostile].hull : 0.0f;
        rules_action(&env, strategy);
        if (env.tick % 5 == 0 || env.hull <= 0 || env.room != 1) {
            printf("t=%d room=%d hp=%.0f/%.0f/%.0f cap=%.0f d=%.0f "
                "enemy_hp=%.0f lock=%d/%d weapon=%d desired=%d "
                "prop=%d/%d rep=%d/%d a=[%.0f,%.0f,%.0f,%.0f,%.0f,%.0f]\n",
                env.tick, env.room, env.shield, env.armor, env.hull,
                env.capacitor, distance, hostile_hp,
                hostile >= 0 ? env.entities[hostile].locking : 0,
                hostile >= 0 ? env.entities[hostile].locked : 0,
                env.weapon_on, env.weapon_desired_target_index,
                env.prop_on, env.prop_desired_on, env.rep_on,
                env.rep_cycle_active, actions[0], actions[1], actions[2],
                actions[3], actions[4], actions[5]);
        }
        puf_step(&env);
    }
    printf("terminal t=%d room=%d hp=%.0f/%.0f/%.0f cap=%.0f rooms=%d caches=%d\n",
        env.tick, env.room, env.shield, env.armor, env.hull, env.capacitor,
        env.rooms_cleared, env.caches_looted);
}

int main(int argc, char** argv) {
    if ((argc == 6 || argc == 7) && strcmp(argv[1], "eval") == 0) {
        rep_reserve_fraction = strtof(argv[5], NULL);
        if (argc == 7) weather_penalty_override = strtof(argv[6], NULL);
        evaluate(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]));
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "trace") == 0) {
        trace_first_failure(atoi(argv[2]), atoi(argv[3]));
        return 0;
    }
    int episodes = argc > 1 ? atoi(argv[1]) : 10000;
    int scenarios[] = {7, 9, 11};
    for (int i = 0; i < 3; i++) {
        evaluate(scenarios[i], 0, episodes);
        evaluate(scenarios[i], 1, episodes);
        evaluate(scenarios[i], 2, episodes);
        evaluate(scenarios[i], 3, episodes);
        evaluate(scenarios[i], 4, episodes);
    }
    return 0;
}
