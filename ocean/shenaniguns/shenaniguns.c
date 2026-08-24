#include "shenaniguns.h"
#include <time.h>

static void bind_agents(Shenanigans* env, obs_t* observations,
        float* actions, float* rewards, float* terminals) {
    for (int i = 0; i < env->num_agents; i++) {
        env->agents[i].observations = observations + i * OBS_SIZE;
        env->agents[i].actions = actions + i * NUM_ATNS;
        env->agents[i].rewards = rewards + i;
        env->agents[i].terminals = terminals + i;
        env->agents[i].action_mask = NULL;
        env->agents[i].policy = 0;
    }
}

void performance_test() {
    long test_time = 10;
    Shenanigans env = {
        .num_agents = 2,
        .num_bots = 4,
        .width = 32.0f,
        .height = 32.0f,
        .max_ticks = 1200,
        .rng = 42,
    };
    allocate_env(&env);
    obs_t observations[2 * OBS_SIZE] = {0};
    float actions[2 * NUM_ATNS] = {0};
    float rewards[2] = {0};
    float terminals[2] = {0};
    bind_agents(&env, observations, actions, rewards, terminals);
    puf_reset(&env);

    long start = time(NULL);
    long i = 0;
    while (time(NULL) - start < test_time) {
        for (int a = 0; a < env.num_agents; a++) {
            float* atn = env.agents[a].actions;
            atn[0] = rand_r(&env.rng) % 5;
            atn[1] = rand_r(&env.rng) % 3;
            atn[2] = rand_r(&env.rng) % 3;
            atn[3] = (rand_r(&env.rng) % 8) == 0 ? 1.0f : 0.0f;
        }
        puf_step(&env);
        i++;
    }
    long end = time(NULL);
    printf("SPS: %ld (steps x %d agents)\n", i * env.num_agents / (end - start),
           env.num_agents);
    printf("slot_0_score=%.3f kills=%.0f deaths=%.0f n=%.0f\n",
           env.log.slot_0_score, env.log.kills, env.log.deaths, env.log.n);
    puf_close(&env);
}

void demo() {
    Shenanigans env = {
        .num_agents = 2,
        .num_bots = 0,
        .width = 32.0f,
        .height = 32.0f,
        .max_ticks = 3600,
    };
    allocate_env(&env);
    obs_t observations[2 * OBS_SIZE] = {0};
    float actions[2 * NUM_ATNS] = {0};
    float rewards[2] = {0};
    float terminals[2] = {0};
    bind_agents(&env, observations, actions, rewards, terminals);
    puf_reset(&env);

    // slot 1 is a stationary target dummy so the human can practice
    env.agents[1].actions[0] = 2.0f;

    env.client = make_client(&env);
    puf_render(&env);

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_ESCAPE)) break;
        if (IsKeyPressed(KEY_R)) puf_reset(&env);

        float* atn = env.agents[0].actions;
        atn[0] = 2.0f; // no turn
        atn[1] = 1.0f; // no forward
        atn[2] = 1.0f; // no strafe
        atn[3] = 0.0f;

        if (IsKeyDown(KEY_LEFT)) atn[0] = 0.0f;
        if (IsKeyDown(KEY_RIGHT)) atn[0] = 4.0f;
        if (IsKeyDown(KEY_W)) atn[1] = 2.0f;
        if (IsKeyDown(KEY_S)) atn[1] = 0.0f;
        if (IsKeyDown(KEY_D)) atn[2] = 2.0f;
        if (IsKeyDown(KEY_A)) atn[2] = 0.0f;
        if (IsKeyDown(KEY_SPACE)) atn[3] = 1.0f;

        puf_step(&env);
        puf_render(&env);
    }
    puf_close(&env);
    CloseWindow();
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--bench") == 0) {
        performance_test();
    } else {
        demo();
    }
    return 0;
}
