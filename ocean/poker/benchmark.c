#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <time.h>
#include <math.h>
#include "native.c"

// One duplicate block uses the same deal, rotating hero through every seat.
// Confidence intervals use independent block means, not correlated hands.
int main(int argc, char** argv) {
    if (argc != 8) {
        fprintf(stderr, "usage: %s hero_style opponents_csv hands seed stack output_blocks.csv label\n", argv[0]);
        return 2;
    }
    int hero_style = atoi(argv[1]), styles[POKER_PLAYERS], ns = 0;
    char* item = strtok(argv[2], ",");
    while (item && ns < POKER_PLAYERS) { styles[ns++] = atoi(item); item = strtok(NULL, ","); }
    assert(ns && !item && (poker_bot_styles() & (1u << hero_style)));
    for (int i = 0; i < ns; i++) assert(poker_bot_styles() & (1u << styles[i]));
    int blocks = (atoi(argv[3]) + POKER_PLAYERS - 1) / POKER_PLAYERS;
    uint64_t seed = strtoull(argv[4], NULL, 10), steps = 0;
    int stack = atoi(argv[5]);
    assert(blocks > 1 && stack >= 100 && stack <= 65535);
    FILE* file = fopen(argv[6], "wx");
    if (!file) { perror(argv[6]); return 2; }
    fprintf(file, "block,mean_bb\n");
    Poker* g = calloc(1, sizeof(Poker));
    poker_seed(g, seed);
    uint32_t stacks[POKER_PLAYERS];
    for (int p = 0; p < POKER_PLAYERS; p++) stacks[p] = stack;
    double sum = 0, sumsq = 0;
    uint64_t faced[4] = {0}, folds[4] = {0}, counts[6] = {0};
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int b = 0; b < blocks; b++) {
        poker_deal(g, 0, 0, stacks);
        uint8_t deck[2 * POKER_PLAYERS + 5];
        for (int p = 0; p < POKER_PLAYERS; p++) for (int h = 0; h < 2; h++) deck[2*p+h] = g->hole[p][h][0];
        for (int i = 0; i < 5; i++) deck[2 * POKER_PLAYERS + i] = g->board[i][0];
        double block = 0;
        for (int hero = 0; hero < POKER_PLAYERS; hero++) {
            poker_reset(g, 0, 0, stacks, deck);
            uint64_t rng[POKER_PLAYERS];
            for (int p = 0; p < POKER_PLAYERS; p++)
                rng[p] = (seed ^ UINT64_C(0xd1b54a32d192ed03)) + (uint64_t)b * 1000003 + hero * 101 + p + 1;
            while (g->actor[0] != POKER_DONE) {
                int p = g->actor[0], offset = (p + POKER_PLAYERS - hero) % POKER_PLAYERS;
                int style = offset ? styles[(offset - 1) % ns] : hero_style;
                int32_t a = poker_bot(g, 0, rng + p, style);
                if (p == hero && g->high[0] > g->bet[p][0]) {
                    faced[g->street[0]]++; folds[g->street[0]] += a < 0;
                }
                counts[style]++;
                poker_step(g, 0, a);
                assert(g->decisions[0] < 4096);
                steps++;
            }
            uint32_t chips = 0;
            for (int p = 0; p < POKER_PLAYERS; p++) chips += g->stack[p][0];
            assert(chips == (uint32_t)stack * POKER_PLAYERS);
            block += ((int)g->stack[hero][0] - stack) / (100.0 * POKER_PLAYERS);
        }
        sum += block; sumsq += block * block;
        fprintf(file, "%d,%.9f\n", b, block);
    }
    fclose(file);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = end.tv_sec - start.tv_sec + (end.tv_nsec - start.tv_nsec) * 1e-9;
    double mean = sum / blocks, variance = (sumsq - sum * mean) / (blocks - 1);
    printf("{\"label\":\"%s\",\"players\":%d,\"hero_style\":%d,\"seed\":%llu,"
        "\"stack_bb\":%.2f,\"hands\":%d,\"duplicate_blocks\":%d,\"bb100\":%.6f,"
        "\"ci95_half_width_bb100\":%.6f,\"seconds\":%.6f,\"decisions\":%llu,"
        "\"decisions_per_second\":%.1f,\"hands_per_second\":%.1f,\"faced\":[",
        argv[7], POKER_PLAYERS, hero_style, (unsigned long long)seed, stack / 100.0,
        blocks * POKER_PLAYERS, blocks, 100 * mean, 196 * sqrt(variance / blocks),
        elapsed, (unsigned long long)steps, steps / elapsed, blocks * POKER_PLAYERS / elapsed);
    for (int i = 0; i < 4; i++) printf("%s%llu", i ? "," : "", (unsigned long long)faced[i]);
    printf("],\"folds\":[");
    for (int i = 0; i < 4; i++) printf("%s%llu", i ? "," : "", (unsigned long long)folds[i]);
    printf("],\"policy_decisions\":[");
    for (int i = 0; i < 6; i++) printf("%s%llu", i ? "," : "", (unsigned long long)counts[i]);
    printf("]}\n");
    free(g);
}
