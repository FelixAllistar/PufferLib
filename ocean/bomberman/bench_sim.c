#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "bm_sim.h"

static double seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

int main(int argc, char** argv) {
    int matches = argc > 1 ? atoi(argv[1]) : 512;
    int iterations = argc > 2 ? atoi(argv[2]) : 2000;
    if (matches < 1 || iterations < 1) return 2;

    BMConfig cfg = bm_default_config();
    cfg.num_agents = 2;
    cfg.max_ticks = 1200;
    cfg.reward_alive = 0.0f;
    BMMatch* states = (BMMatch*)calloc((size_t)matches, sizeof(BMMatch));
    float* observations = (float*)malloc((size_t)matches * 2 * BM_OBS_SIZE * sizeof(float));
    if (!states || !observations) return 3;

    for (int i = 0; i < matches; i++) bm_reset_match(&states[i], &cfg, (uint32_t)i + 1u);
    uint32_t rng = 1234567u;
    volatile double checksum = 0.0;

    double t0 = seconds();
    for (int it = 0; it < iterations; it++) {
        for (int i = 0; i < matches; i++) {
            int actions[BM_MAX_AGENTS] = {
                bm_randi(&rng, BM_NUM_ACTIONS), bm_randi(&rng, BM_NUM_ACTIONS), 0, 0};
            float rewards[BM_MAX_AGENTS], terminals[BM_MAX_AGENTS];
            bm_step_match(&states[i], &cfg, actions, rewards, terminals);
            if (states[i].done) bm_reset_match(&states[i], &cfg, bm_xorshift(&rng));
            for (int a = 0; a < 2; a++) {
                float* out = observations + ((size_t)i * 2 + a) * BM_OBS_SIZE;
                bm_write_obs(&states[i], &cfg, a, out);
                checksum += (double)out[(it + i + a) % BM_OBS_SIZE];
            }
        }
    }
    double dt = seconds() - t0;
    double match_steps = (double)matches * iterations;
    printf("matches=%d iterations=%d obs=%d match_bytes=%zu checksum=%.3f\n",
        matches, iterations, BM_OBS_SIZE, sizeof(BMMatch), checksum);
    printf("%.3f s | %.0f match-steps/s | %.0f agent-steps/s | %.1f MiB/s obs writes\n",
        dt, match_steps / dt, 2.0 * match_steps / dt,
        (2.0 * match_steps * BM_OBS_SIZE * sizeof(float)) / dt / (1024.0 * 1024.0));
    free(observations);
    free(states);
    return 0;
}
