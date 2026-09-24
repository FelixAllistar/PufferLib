#include "shenaniguns.h"

int main(void) {
    Ini ini = {0};
    puf_ini_load_env(&ini, "shenaniguns", 0, NULL);
    Env env = {0};
    obs_t observations[MAX_AGENTS * OBS_SIZE] = {0};
    float actions[MAX_AGENTS * NUM_ATNS] = {0};
    float rewards[MAX_AGENTS] = {0};
    float terminals[MAX_AGENTS] = {0};
    puf_init(&env, puf_ini_section(&ini, "env", 0));
    assert(env.num_agents == 2);
    for (int a = 0; a < env.num_agents; a++) {
        env.agents[a].observations = observations + a * OBS_SIZE;
        env.agents[a].actions = actions + a * NUM_ATNS;
        env.agents[a].rewards = rewards + a;
        env.agents[a].terminals = terminals + a;
        actions[a * NUM_ATNS] = 2;
        actions[a * NUM_ATNS + 1] = 1;
        actions[a * NUM_ATNS + 2] = 1;
    }
    puf_reset(&env);
    rewards[0] = 0.75f;
    rewards[1] = -0.25f;
    env.tag = 1;
    end_episode(&env, 1);
    assert(rewards[0] == 0.75f && rewards[1] == -0.25f);
    assert(terminals[0] == 1 && terminals[1] == 1);
    assert(env.boundary_reached == 1 && env.tick == 0);
    assert(env.log.slot_0_score == 2 && env.log.slot_1_score == 0);
    puf_step(&env);
    assert(terminals[0] == 0 && terminals[1] == 0);
    // Timeouts must not repeat the previous transition's reward.
    env.tick = env.max_ticks;
    rewards[0] = 123;
    puf_step(&env);
    assert(rewards[0] == 0 && rewards[1] == 0);
    assert(terminals[0] == 1 && terminals[1] == 1);
    for (int i = 0; i < 1000; i++) {
        puf_step(&env);
        for (int j = 0; j < env.num_agents * OBS_SIZE; j++) {
            assert(isfinite(observations[j]));
        }
    }
    puf_close(&env);
    puf_ini_free(&ini);
    puts("Shenaniguns adapter tests passed");
}
