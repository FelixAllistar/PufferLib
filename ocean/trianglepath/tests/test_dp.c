#define TP_CPU_ADAPTER
#include "../trianglepath.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* Brute force over all 2^(H-1) paths, independent of the DP. */
static int brute_force(const TPState* state, const TPConfig* cfg) {
    int best = -1;
    int paths = 1 << (cfg->height - 1);
    for (int mask = 0; mask < paths; mask++) {
        int row = 0, col = 0, total = 0;
        for (int step = 0; step < cfg->height; step++) {
            total += state->cells[tp_cell_index(row, col)];
            if (step < cfg->height - 1) {
                if (mask & (1 << step)) col++;
                row++;
            }
        }
        if (total > best) best = total;
    }
    return best;
}

static void test_dp_matches_brute(int height, int seed) {
    TPConfig cfg = {height, 1, 9, (uint32_t)seed};
    TPState state;
    uint32_t rng = (uint32_t)seed * 2654435761u;
    tp_reset_state(&state, &cfg, &rng);
    int dp = tp_optimal_total(&state, &cfg);
    int bf = brute_force(&state, &cfg);
    if (dp != bf) {
        fprintf(stderr, "height=%d seed=%d dp=%d brute=%d MISMATCH\n",
            height, seed, dp, bf);
        exit(1);
    }
    printf("height=%d seed=%d dp=%d brute=%d ok\n", height, seed, dp, bf);
}

static void test_optimal_path(int height, int seed) {
    TPConfig cfg = {height, 1, 9, (uint32_t)seed};
    TPState state;
    uint32_t rng = (uint32_t)seed * 2654435761u;
    tp_reset_state(&state, &cfg, &rng);
    int optimal = tp_optimal_total(&state, &cfg);
    int walked = 0;
    int row = 0, col = 0;
    for (int step = 0; step < height; step++) {
        walked += state.cells[tp_cell_index(row, col)];
        if (step < height - 1) {
            if (tp_optimal_action(&state, &cfg, row, col) == TP_RIGHT) col++;
            row++;
        }
    }
    if (walked != optimal) {
        fprintf(stderr, "optimal path height=%d seed=%d walked=%d want=%d\n",
            height, seed, walked, optimal);
        exit(1);
    }
    printf("height=%d seed=%d optimal path %d ok\n", height, seed, walked);
}

int main(void) {
    for (int h = 2; h <= 10; h++) {
        for (int seed = 1; seed <= 8; seed++) {
            test_dp_matches_brute(h, seed);
            test_optimal_path(h, seed);
        }
    }
    puts("TrianglePath DP tests passed");
    return 0;
}
