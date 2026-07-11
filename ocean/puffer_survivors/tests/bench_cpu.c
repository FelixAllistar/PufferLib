#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../puffer_survivors.h"

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

int main(int argc, char** argv) {
    int num_envs = argc > 1 ? atoi(argv[1]) : 64;
    int steps = argc > 2 ? atoi(argv[2]) : 5000;
    if (num_envs < 1 || steps < 1) {
        fprintf(stderr, "usage: %s [num_envs] [steps]\n", argv[0]);
        return 2;
    }

    PufferSurvivors* envs = calloc((size_t)num_envs, sizeof(*envs));
    float* observations = calloc((size_t)num_envs * PS_OBS_SIZE, sizeof(float));
    float* actions = calloc((size_t)num_envs * 2, sizeof(float));
    float* rewards = calloc((size_t)num_envs, sizeof(float));
    float* terminals = calloc((size_t)num_envs, sizeof(float));
    if (!envs || !observations || !actions || !rewards || !terminals) {
        fprintf(stderr, "allocation failed\n");
        free(envs); free(observations); free(actions); free(rewards); free(terminals);
        return 1;
    }

    PSConfig cfg = ps_default_config();
    cfg.player_health = 1000000.0f;
    cfg.max_steps = 1000000000;
    for (int e = 0; e < num_envs; e++) {
        PufferSurvivors* env = &envs[e];
        env->observations = &observations[(size_t)e * PS_OBS_SIZE];
        env->actions = &actions[(size_t)e * 2];
        env->rewards = &rewards[e];
        env->terminals = &terminals[e];
        env->num_agents = 1;
        env->rng = (uint32_t)(e + 1);
        env->cfg = cfg;
        ps_init(env);
        c_reset(env);
    }

    for (int t = 0; t < 250; t++) {
        for (int e = 0; e < num_envs; e++) {
            actions[(size_t)e * 2] = (float)((t + e) % 9);
            actions[(size_t)e * 2 + 1] = (float)((t / 97 + e) % 3);
            c_step(&envs[e]);
        }
    }

    double start = now_seconds();
    for (int t = 0; t < steps; t++) {
        for (int e = 0; e < num_envs; e++) {
            actions[(size_t)e * 2] = (float)((t + e) % 9);
            actions[(size_t)e * 2 + 1] = (float)((t / 97 + e) % 3);
            c_step(&envs[e]);
        }
    }
    double seconds = now_seconds() - start;

    double checksum = 0.0;
    for (int e = 0; e < num_envs; e++) {
        checksum += envs[e].episode_score + envs[e].hp + observations[(size_t)e * PS_OBS_SIZE];
    }
    double env_steps = (double)num_envs * (double)steps;
    printf("num_envs %d\nsteps %d\nseconds %.6f\nenv_steps_per_sec %.2f\nchecksum %.6f\n",
        num_envs, steps, seconds, env_steps / seconds, checksum);

    free(envs);
    free(observations);
    free(actions);
    free(rewards);
    free(terminals);
    return 0;
}
