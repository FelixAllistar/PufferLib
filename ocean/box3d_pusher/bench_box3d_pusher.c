#include "box3d_pusher.h"

#include <inttypes.h>
#include <stdlib.h>
#include <time.h>

static inline uint64_t ns_now(void) {
    struct timespec ts;
#ifdef CLOCK_MONOTONIC_RAW
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static inline void bench_actions(float* actions, int agents, int t) {
    static const int pattern[] = {1, 5, 3, 7, 2, 8, 4, 6, 0};
    for (int i = 0; i < agents; i++) {
        actions[i * B3P_NUM_ATNS] = (float)pattern[((t / 31) + i) % 9];
        actions[i * B3P_NUM_ATNS + 1] = (float)(((t / 97) + i) % 3);
    }
}

int main(int argc, char** argv) {
    int agents = argc > 1 ? atoi(argv[1]) : 64;
    int steps = argc > 2 ? atoi(argv[2]) : 100000;
    if (agents < 1) agents = 1;
    if (agents > B3P_MAX_AGENTS) agents = B3P_MAX_AGENTS;
    if (steps < 1) steps = 100000;

    static float observations[B3P_MAX_AGENTS * B3P_OBS_SIZE];
    static float actions[B3P_MAX_AGENTS * B3P_NUM_ATNS];
    static float rewards[B3P_MAX_AGENTS];
    static float terminals[B3P_MAX_AGENTS];

    Box3DPusher env = {
        .observations = observations,
        .actions = actions,
        .rewards = rewards,
        .terminals = terminals,
        .rng = 1,
        .arena_half = 8.0f,
        .arena_stride = 22.0f,
        .max_steps = 720,
        .substeps = 1,
    };
    box3d_pusher_init(&env, agents);
    c_reset(&env);

    for (int t = 0; t < 1000; t++) {
        bench_actions(actions, agents, t);
        c_step(&env);
    }

    uint64_t start = ns_now();
    for (int t = 0; t < steps; t++) {
        bench_actions(actions, agents, t);
        c_step(&env);
    }
    uint64_t end = ns_now();
    double seconds = (double)(end - start) / 1000000000.0;
    double agent_steps = (double)steps * (double)agents;
    printf("agents %d\n", agents);
    printf("steps %d\n", steps);
    printf("seconds %.6f\n", seconds);
    printf("agent_steps_per_sec %.2f\n", agent_steps / seconds);
    printf("episodes %.0f perf %.3f goals %.1f score %.1f\n", env.log.n, env.log.n > 0.0f ? env.log.perf / env.log.n : 0.0f, env.log.goals, env.log.score);
    printf("bodies %.0f contacts %.0f box3d_step_ms %.6f\n", env.log.n > 0.0f ? env.log.body_count / env.log.n : 0.0f, env.log.n > 0.0f ? env.log.contact_count / env.log.n : 0.0f, env.log.n > 0.0f ? env.log.box3d_step_ms / env.log.n : 0.0f);
    c_close(&env);
    return 0;
}
