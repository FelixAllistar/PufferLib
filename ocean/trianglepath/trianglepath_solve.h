#pragma once

#include "trianglepath.h"

/* Exact backward-DP optimal value given a raw cell array. Works on host and
 * device, so the GPU env can log optimal/regret without copying a state. */
TP_HD static inline int tp_solve_value_cells(const uint8_t* cells,
        int height) {
    int dp[TP_MAX_CELLS];
    for (int col = 0; col < height; col++) {
        dp[tp_cell_index(height - 1, col)] =
            cells[tp_cell_index(height - 1, col)];
    }
    for (int row = height - 2; row >= 0; row--) {
        for (int col = 0; col <= row; col++) {
            int left = dp[tp_cell_index(row + 1, col)];
            int right = dp[tp_cell_index(row + 1, col + 1)];
            dp[tp_cell_index(row, col)] =
                cells[tp_cell_index(row, col)]
                + (left > right ? left : right);
        }
    }
    return dp[0];
}

/* Exact backward-DP optimal value and path for a triangle instance.
 * O(H^2) cells, so the oracle stays trivial even at H=64 (2^63 paths). */
TP_HD static inline int tp_solve_value(const TPState* state,
        const TPConfig* cfg) {
    return tp_solve_value_cells(state->cells, cfg->height);
}

/* Optimal total INCLUDING the starting cell (agent collects every cell it
 * visits, including the apex). The DP above includes the apex already. */
TP_HD static inline int tp_optimal_total(const TPState* state,
        const TPConfig* cfg) {
    return tp_solve_value(state, cfg);
}

/* Recover the optimal action at (row, col): 0=left, 1=right. */
TP_HD static inline int tp_optimal_action(const TPState* state,
        const TPConfig* cfg, int row, int col) {
    int dp[TP_MAX_CELLS];
    for (int c = 0; c < cfg->height; c++) {
        dp[tp_cell_index(cfg->height - 1, c)] =
            state->cells[tp_cell_index(cfg->height - 1, c)];
    }
    for (int r = cfg->height - 2; r >= 0; r--) {
        for (int c = 0; c <= r; c++) {
            int left = dp[tp_cell_index(r + 1, c)];
            int right = dp[tp_cell_index(r + 1, c + 1)];
            dp[tp_cell_index(r, c)] =
                state->cells[tp_cell_index(r, c)]
                + (left > right ? left : right);
        }
    }
    if (row >= cfg->height - 1) return TP_LEFT;
    int left = dp[tp_cell_index(row + 1, col)];
    int right = dp[tp_cell_index(row + 1, col + 1)];
    return right > left ? TP_RIGHT : TP_LEFT;
}
