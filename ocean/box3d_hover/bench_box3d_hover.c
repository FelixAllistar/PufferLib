#define B3H_PROFILE 1
#include "box3d_hover.h"

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

static uint64_t fnv1a_bytes(const void* data, size_t size) {
    const unsigned char* p = (const unsigned char*)data;
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < size; i++) {
        h ^= (uint64_t)p[i];
        h *= 1099511628211ull;
    }
    return h;
}

static void bench_actions(float* actions, int agents, int t) {
    static const int dirs[] = {1, 5, 3, 7, 2, 8, 4, 6, 0};
    for (int i = 0; i < agents; i++) {
        actions[i * B3H_NUM_ATNS] = (float)dirs[((t / 17) + i * 3) % 9];
        actions[i * B3H_NUM_ATNS + 1] = (float)(((t / 97) + i) % 3);
    }
}

int main(int argc, char** argv) {
    int steps = argc > 1 ? atoi(argv[1]) : 100000;
    int agents = argc > 2 ? atoi(argv[2]) : B3H_MAX_AGENTS;
    int substeps = argc > 3 ? atoi(argv[3]) : 2;
    if (steps <= 0) steps = 100000;
    if (agents <= 0) agents = B3H_MAX_AGENTS;
    if (agents > B3H_MAX_AGENTS) agents = B3H_MAX_AGENTS;
    if (substeps < 1) substeps = 1;

    float observations[B3H_MAX_AGENTS * B3H_OBS_SIZE] = {0};
    float actions[B3H_MAX_AGENTS * B3H_NUM_ATNS] = {0};
    float rewards[B3H_MAX_AGENTS] = {0};
    float terminals[B3H_MAX_AGENTS] = {0};

    Box3DHover env = {
        .observations = observations,
        .actions = actions,
        .rewards = rewards,
        .terminals = terminals,
        .rng = 1,
        .arena_half = 8.0f,
        .arena_stride = 22.0f,
        .max_steps = 360,
        .substeps = substeps,
    };
    box3d_hover_init(&env, agents);
    c_reset(&env);

    for (int t = 0; t < 1000; t++) {
        bench_actions(actions, agents, t);
        c_step(&env);
    }

    env.prof_apply_ns = 0;
    env.prof_step_ns = 0;
    env.prof_post_ns = 0;
    uint64_t start = ns_now();
    for (int t = 0; t < steps; t++) {
        bench_actions(actions, agents, t);
        c_step(&env);
    }
    uint64_t end = ns_now();

    double seconds = (double)(end - start) / 1000000000.0;
    double world_sps = (double)steps / seconds;
    double agent_sps = (double)steps * (double)agents / seconds;
    b3Counters counters = b3World_GetCounters(env.world);
    b3Profile profile = b3World_GetProfile(env.world);

    printf("steps %d\n", steps);
    printf("agents %d\n", agents);
    printf("substeps %d\n", substeps);
    printf("seconds %.6f\n", seconds);
    printf("world_steps_per_sec %.2f\n", world_sps);
    printf("agent_steps_per_sec %.2f\n", agent_sps);
    printf("apply_ns_per_world_step %.2f\n", (double)env.prof_apply_ns / (double)steps);
    printf("box3d_ns_per_world_step %.2f\n", (double)env.prof_step_ns / (double)steps);
    printf("post_obs_ns_per_world_step %.2f\n", (double)env.prof_post_ns / (double)steps);
    printf("box3d_profile_step_ms %.6f\n", profile.step);
    printf("body_count %d\n", counters.bodyCount);
    printf("shape_count %d\n", counters.shapeCount);
    printf("contact_count %d\n", counters.contactCount);
    printf("captures %.0f\n", env.log.captures);
    printf("timeouts %.0f\n", env.log.timeouts);
    printf("obs_hash %llu\n", (unsigned long long)fnv1a_bytes(observations, sizeof(float) * (size_t)agents * B3H_OBS_SIZE));

    c_close(&env);
    return 0;
}
