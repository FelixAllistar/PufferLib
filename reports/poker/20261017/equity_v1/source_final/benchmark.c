#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <time.h>
#include <math.h>
#include "native.c"

// One duplicate block uses the same deal, rotating hero through every seat.
// Confidence intervals use independent block means, not correlated hands.
int main(int argc, char** argv) {
    if (argc != 8 && argc != 10) {
        fprintf(stderr, "usage: %s hero_style opponents_csv hands seed stack output_blocks.csv label [session_blocks history]\n", argv[0]);
        return 2;
    }
    int hero_style = atoi(argv[1]), styles[POKER_PLAYERS], ns = 0;
    char* item = strtok(argv[2], ",");
    while (item && ns < POKER_PLAYERS) { styles[ns++] = atoi(item); item = strtok(NULL, ","); }
    assert(ns && !item && (poker_bot_styles() & (1u << hero_style)));
    for (int i = 0; i < ns; i++) assert(poker_bot_styles() & (1u << styles[i]));
    int blocks = (atoi(argv[3]) + POKER_PLAYERS - 1) / POKER_PLAYERS;
    int session_blocks = argc == 10 ? atoi(argv[8]) : 1;
    int history = argc == 10 ? atoi(argv[9]) : 0;
    assert(session_blocks > 0 && (history == 0 || history == 1));
    assert(!history || session_blocks > 1);
    // Whole sessions form independent CI clusters when table memory is enabled.
    int clusters = (blocks + session_blocks - 1) / session_blocks;
    assert(clusters > 1);
    blocks = clusters * session_blocks;
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
    uint64_t faced[4] = {0}, folds[4] = {0}, counts[POKER_BOT_COUNT] = {0};
    PokerProfile profiles[POKER_PLAYERS][POKER_PLAYERS] = {0};
    double cluster = 0;
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int b = 0; b < blocks; b++) {
        if (b % session_blocks == 0) memset(profiles, 0, sizeof(profiles));
        int button = session_blocks > 1 ? (b % session_blocks) % POKER_PLAYERS : 0;
        poker_deal(g, 0, button, stacks);
        uint8_t deck[2 * POKER_PLAYERS + 5];
        for (int p = 0; p < POKER_PLAYERS; p++) for (int h = 0; h < 2; h++) deck[2*p+h] = g->hole[p][h][0];
        for (int i = 0; i < 5; i++) deck[2 * POKER_PLAYERS + i] = g->board[i][0];
        double block = 0;
        for (int hero = 0; hero < POKER_PLAYERS; hero++) {
            poker_reset(g, 0, button, stacks, deck);
            PokerProfile* table = history ? profiles[hero] : NULL;
            poker_profile_begin(table);
            uint64_t rng[POKER_PLAYERS];
            for (int p = 0; p < POKER_PLAYERS; p++)
                rng[p] = (seed ^ UINT64_C(0xd1b54a32d192ed03)) + (uint64_t)b * 1000003 + hero * 101 + p + 1;
            while (g->actor[0] != POKER_DONE) {
                int p = g->actor[0], offset = (p + POKER_PLAYERS - hero) % POKER_PLAYERS;
                int style = offset ? styles[(offset - 1) % ns] : hero_style;
                int32_t a = poker_bot_with_history(g, 0, rng + p, style, table);
                if (p == hero && g->high[0] > g->bet[p][0]) {
                    faced[g->street[0]]++; folds[g->street[0]] += a < 0;
                }
                counts[style]++;
                poker_profile_update(table, g, 0, a);
                poker_step(g, 0, a);
                assert(g->decisions[0] < 4096);
                steps++;
            }
            uint32_t chips = 0;
            for (int p = 0; p < POKER_PLAYERS; p++) chips += g->stack[p][0];
            assert(chips == (uint32_t)stack * POKER_PLAYERS);
            block += ((int)g->stack[hero][0] - stack) / (100.0 * POKER_PLAYERS);
        }
        cluster += block / session_blocks;
        if ((b + 1) % session_blocks == 0) {
            sum += cluster; sumsq += cluster * cluster;
            fprintf(file, "%d,%.9f\n", b / session_blocks, cluster);
            cluster = 0;
        }
    }
    fclose(file);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = end.tv_sec - start.tv_sec + (end.tv_nsec - start.tv_nsec) * 1e-9;
    double mean = sum / clusters, variance = (sumsq - sum * mean) / (clusters - 1);
    printf("{\"label\":\"%s\",\"players\":%d,\"hero_style\":%d,\"seed\":%llu,"
        "\"stack_bb\":%.2f,\"hands\":%d,\"duplicate_blocks\":%d,\"bb100\":%.6f,"
        "\"ci95_half_width_bb100\":%.6f,\"seconds\":%.6f,\"decisions\":%llu,"
        "\"independent_clusters\":%d,\"session_blocks\":%d,\"native_history\":%d,"
        "\"decisions_per_second\":%.1f,\"hands_per_second\":%.1f,\"faced\":[",
        argv[7], POKER_PLAYERS, hero_style, (unsigned long long)seed, stack / 100.0,
        blocks * POKER_PLAYERS, blocks, 100 * mean, 196 * sqrt(variance / clusters),
        elapsed, (unsigned long long)steps, clusters, session_blocks, history,
        steps / elapsed, blocks * POKER_PLAYERS / elapsed);
    for (int i = 0; i < 4; i++) printf("%s%llu", i ? "," : "", (unsigned long long)faced[i]);
    printf("],\"folds\":[");
    for (int i = 0; i < 4; i++) printf("%s%llu", i ? "," : "", (unsigned long long)folds[i]);
    printf("],\"policy_decisions\":[");
    for (int i = 0; i < POKER_BOT_COUNT; i++) printf("%s%llu", i ? "," : "", (unsigned long long)counts[i]);
    printf("]}\n");
    free(g);
}
