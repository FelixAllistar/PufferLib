#define _POSIX_C_SOURCE 200809L
#include "pokemon_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static double seconds(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
int main(int argc, char** argv) {
    int episodes = argc > 1 ? atoi(argv[1]) : 10000;
    int draft = argc > 2 ? atoi(argv[2]) : 0;
    if (episodes <= 0 || (draft != 0 && draft != 1)) return 1;
    PKGame g = {0};
    g.rng = 42; g.draft = draft; g.max_updates = 512;
    uint64_t policy_rng = 123;
    long long decisions = 0, turns = 0, invalid = 0;
    int counts[6] = {0};
    double start = seconds();
    for (int i = 0; i < episodes; i++) {
        pk_game_reset(&g);
        while (!g.result) {
            int a = pk_random_action(g.masks[0], &policy_rng);
            int b = pk_random_action(g.masks[1], &policy_rng);
            pk_game_step(&g, a, b);
            decisions++;
        }
        if (g.result == 4) { fprintf(stderr, "engine error\n"); return 2; }
        counts[g.result]++;
        turns += pk_turn(&g.battle);
        invalid += g.invalid_actions;
    }
    double elapsed = seconds() - start;
    printf("mode=%s episodes=%d seconds=%.6f battles_per_sec=%.0f joint_decisions_per_sec=%.0f agent_steps_per_sec=%.0f\n",
        draft ? "private_draft" : "sampled", episodes, elapsed, episodes / elapsed,
        decisions / elapsed, 2.0 * decisions / elapsed);
    printf("mean_turns=%.2f mean_decisions=%.2f p1_wins=%d p2_wins=%d ties=%d timeouts=%d invalid=%lld\n",
        (double)turns / episodes, (double)decisions / episodes,
        counts[1], counts[2], counts[3], counts[5], invalid);
    puts("Single CPU thread; includes teams, reset, legal masks, both observations and random action selection; excludes policy inference and GPU transfers.");
    return 0;
}
