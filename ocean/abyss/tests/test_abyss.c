#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../abyss.h"

static void assert_finite_observation(const obs_t* observation) {
    for (int i = 0; i < OBS_SIZE; i++) {
        assert(isfinite(observation[i]));
    }
}

int main(void) {
    Ini ini = {0};
    puf_ini_load_env(&ini, "abyss", 0, NULL);
    Dict* cfg = puf_ini_section(&ini, "env", 0);

    Env env = {0};
    obs_t observations[OBS_SIZE] = {0};
    float actions[NUM_ATNS] = {0};
    float rewards[1] = {0};
    float terminals[1] = {0};
    env.agents[0].observations = observations;
    env.agents[0].actions = actions;
    env.agents[0].rewards = rewards;
    env.agents[0].terminals = terminals;
    env.rng = 1;
    puf_init(&env, cfg);
    puf_reset(&env);

    assert(env.num_agents == 1);
    assert(env.room == 1);
    assert(env.entity_count >= 2);
    assert_finite_observation(observations);
    assert(ab_turret_hit_chance(0, 1, 1, 1000, 2000, 1000) == 1.0f);
    assert(ab_lock_time(578.0f, 40.0f) > 0.0f);

    int cache = -1;
    int gate = -1;
    for (int i = 0; i < env.entity_count; i++) {
        if (env.entities[i].kind == ENTITY_CACHE) cache = i;
        if (env.entities[i].kind == ENTITY_CONDUIT) gate = i;
        if (env.entities[i].kind == ENTITY_HOSTILE) env.entities[i].alive = 0;
    }
    assert(cache >= 0 && gate >= 0);

    env.entities[cache].alive = 0;
    env.ship_pos = env.entities[cache].pos;
    actions[5] = INTERACT_LOOT;
    puf_step(&env);
    assert(env.cargo_open == 1);
    puf_step(&env);
    assert(env.cache_looted == 1);
    assert(env.cargo_open == 0);

    env.room = 3;
    env.rooms_cleared = 2;
    env.ship_pos = env.entities[gate].pos;
    memset(actions, 0, sizeof(actions));
    actions[5] = INTERACT_GATE;
    puf_step(&env);
    assert(terminals[0] == 1.0f);
    assert(env.log.n == 1.0f);
    assert(env.log.completion_rate == 1.0f);
    assert(env.log.rooms_cleared == 3.0f);
    assert_finite_observation(observations);

    puf_close(&env);
    puf_ini_free(&ini);
    puts("native Abyss smoke ok");
    return 0;
}
