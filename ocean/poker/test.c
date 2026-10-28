#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <time.h>
#include <math.h>
#include "native.c"

void check(Poker* g, int t) {
    uint32_t chips = g->pot[t], initial = 0, committed = 0;
    uint64_t cards = 0;
    for (int p = 0; p < POKER_PLAYERS; p++) {
        chips += g->stack[p][t];
        initial += g->initial[p][t];
        committed += g->total[p][t];
        assert(g->stack[p][t] <= 65535u * POKER_PLAYERS);
        if (g->actor[t] != POKER_DONE) {
            assert(g->stack[p][t] + g->total[p][t] == g->initial[p][t]);
        }
        for (int h = 0; h < 2; h++) {
            unsigned c = g->hole[p][h][t];
            assert(c < 52 && !(cards & (UINT64_C(1) << c)));
            cards |= UINT64_C(1) << c;
        }
    }
    for (int i = 0; i < 5; i++) {
        unsigned c = g->board[i][t];
        assert(c < 52 && !(cards & (UINT64_C(1) << c)));
        cards |= UINT64_C(1) << c;
    }
    assert(chips == initial);
    if (g->actor[t] != POKER_DONE) {
        assert(committed == g->pot[t]);
        assert(g->pending[t] & (1u << g->actor[t]));
        assert(g->active[t] & (1u << g->actor[t]));
        assert(g->live[t] & (1u << g->actor[t]));
    }
}

int main(int argc, char** argv) {
    int hands = argc > 1 ? atoi(argv[1]) : 20000;
    int bench = argc > 2;
    int mode = !bench ? 0 : !strcmp(argv[2], "random") ? 1
        : !strcmp(argv[2], "minraise") ? 2 : !strcmp(argv[2], "call") ? 3 : 4;
    Poker* g = calloc(1, sizeof(Poker));
    poker_seed(g, 73);
    uint64_t policy_rng[POKER_BATCH];
    uint32_t stacks[POKER_PLAYERS];
    uint64_t steps = 0, showdowns = 0;
    for (int t = 0; t < POKER_BATCH; t++) {
        policy_rng[t] = 100003 + t;
    }
    struct timespec start, stop;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int h = 0; h < hands; h += POKER_BATCH) {
        for (int t = 0; t < POKER_BATCH; t++) {
            for (int p = 0; p < POKER_PLAYERS; p++) {
                stacks[p] = bench ? 20000 : h % 1024 == 0 ? 65535
                    : 1 + poker_random(&policy_rng[t]) % 65535;
            }
            poker_deal(g, t, (h + t) % POKER_PLAYERS, stacks);
        }
        int running = POKER_BATCH;
        while (running) {
            running = 0;
            for (int t = 0; t < POKER_BATCH; t++) {
                if (g->actor[t] == POKER_DONE) {
                    continue;
                }
                running++;
                if (!bench) {
                    check(g, t);
                    assert(g->decisions[t] < 4096);
                    float before[POKER_OBS], after[POKER_OBS];
                    int p = g->actor[t], other = (p + 1) % POKER_PLAYERS;
                    poker_observe(g, t, p, before);
                    uint8_t saved = g->hole[other][0][t];
                    g->hole[other][0][t] ^= 1;
                    poker_observe(g, t, p, after);
                    assert(!memcmp(before, after, sizeof(before)));
                    g->hole[other][0][t] = saved;
                    // Card features: own hole cards one-hot, pair/suited flags, board counts, made-hand class.
                    const float* f = before + POKER_OBS - POKER_CARD_FEATURES;
                    uint8_t a = g->hole[p][0][t], b = g->hole[p][1][t];
                    float ranks = 0, boards = 0, classes = 0;
                    for (int i = 0; i < 13; i++) ranks += f[i] + f[17 + i];
                    for (int i = 0; i < 17; i++) boards += f[36 + i];
                    for (int i = 0; i < 9; i++) classes += f[53 + i];
                    int board_n = g->street[t] ? g->street[t] + 2 : 0;
                    assert(f[a >> 2] == 1 && f[13 + (a & 3)] == 1 && f[17 + (b >> 2)] == 1 && f[30 + (b & 3)] == 1);
                    assert(ranks == 2 && f[34] == ((a >> 2) == (b >> 2)) && f[35] == ((a & 3) == (b & 3)));
                    assert(fabsf(boards - 0.45f * board_n) < 1e-5f && classes == (board_n ? 1 : 0));
                    for (int i = 0; i < POKER_CARD_FEATURES; i++) assert(f[i] >= 0 && f[i] <= 1);
                    int r0 = a >> 2, r1 = b >> 2, hi = r0 > r1 ? r0 : r1, lo = r0 < r1 ? r0 : r1;
                    float strength = (r0 == r1 ? 30 + 3 * hi : 2 * hi + lo + 4 * ((a & 3) == (b & 3)) + 4 * (hi - lo <= 2)) / 66.0f;
                    assert(f[62] == strength && (board_n && board_n < 5 ? 1 : f[66] == 0 && f[67] == 0));
                }
                int32_t action;
                if (mode == 3) {
                    action = 0;
                } else if (mode == 4 || (!bench && (h / POKER_BATCH) % 2)) {
                    action = poker_bot(g, t, policy_rng + t, 1);
                } else {
                    int32_t amounts[8];
                    unsigned mask = poker_actions(g, t, amounts);
                    int pick = mode == 2 ? __builtin_popcount(mask & 3)
                        : poker_random(&policy_rng[t]) % __builtin_popcount(mask);
                    if (mode == 2 && !(mask & ~3u)) {
                        pick = __builtin_popcount(mask & 1);
                    }
                    while (pick--) {
                        mask &= mask - 1;
                    }
                    action = amounts[__builtin_ctz(mask)];
                }
                poker_step(g, t, action);
                steps++;
            }
        }
        for (int t = 0; t < POKER_BATCH; t++) {
            showdowns += __builtin_popcount(g->live[t]) > 1;
            if (!bench) {
                check(g, t);
                uint32_t before[15 + 7 * POKER_PLAYERS], after[15 + 7 * POKER_PLAYERS];
                poker_snapshot(g, t, before);
                poker_step(g, t, 0);
                poker_snapshot(g, t, after);
                assert(!memcmp(before, after, sizeof(before)));
            }
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &stop);
    double elapsed = stop.tv_sec - start.tv_sec + 1e-9 * (stop.tv_nsec - start.tv_nsec);
    int actual_hands = (hands + POKER_BATCH - 1) / POKER_BATCH * POKER_BATCH;
    printf("players=%d hands=%d steps=%llu seconds=%.6f decisions_s=%.0f "
        "hands_s=%.0f bytes_table=%zu showdown_fraction=%.3f mode=%s\n",
        POKER_PLAYERS, actual_hands, (unsigned long long)steps, elapsed, steps / elapsed,
        actual_hands / elapsed, sizeof(Poker) / POKER_BATCH,
        (double)showdowns / actual_hands, bench ? argv[2] : "checked");
    free(g);
}
