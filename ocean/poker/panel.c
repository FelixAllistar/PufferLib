// Evaluation panel for poker checkpoints (2026-10-25): does a policy play poker, or exploit one opponent?
// gen0 beat Rules and Range by raising every hand; a later league policy called every hand. The panel scores one
// policy seat (button rotating, 200 BB every hand) against five copies of each opponent:
//   bots: rules (1), range (7), maniac (8), station (9), tight (10) (archetypes.h); each punishes a different exploit;
//   policies: any checkpoints given with --vs (historical snapshots, other runs), five copies each;
// plus card sense (first preflop decision by hand class) and a self-play script test (distinct action sequences).
// PANEL line: mean and worst BB/hand over the bots; a real poker policy is positive against all of them.
// Build: clang -O2 -march=native -ffp-contract=off -std=c11 -DNDEBUG ocean/poker/panel.c -lm -o build/poker/panel
// Usage: panel WEIGHTS HIDDEN LAYERS [--hands N] [--seed S] [--vs CHECKPOINT ...]
#include <stdio.h>
#include "policy.h"

#define PANEL_MAX_VS 64

static uint64_t rng;
static float memory[POKER_PLAYERS][4 * POKER_POLICY_MAX_HIDDEN];

static int hand_class(uint8_t a, uint8_t b) {
    int r1 = a >> 2, r2 = b >> 2, hi = r1 > r2 ? r1 : r2, lo = r1 > r2 ? r2 : r1;
    if (r1 == r2) return hi >= 8 ? 0 : 1;
    if (hi == 12 && lo >= 10) return 0;
    if ((a & 3) != (b & 3) || hi > 9 || hi - lo < 3) return 1;
    return 2;
}

static int32_t policy_move(const PokerPolicy* net, Poker* g, int p, int* kind) {
    float obs[POKER_OBS], logits[POKER_ACTIONS + 1];
    int32_t amounts[POKER_ACTIONS];
    unsigned mask = poker_actions(g, 0, amounts);
    poker_observe(g, 0, p, obs);
    poker_policy_forward(net, memory[p], obs, logits);
    int32_t amount = amounts[poker_policy_sample(logits, mask, poker_random(&rng))];
    *kind = amount < 0 ? 0 : amount == 0 ? 1 : 2;  // fold, check/call, bet/raise
    return amount;
}

static float* load(const char* path, int h, int layers) {
    size_t size = poker_policy_size(h, layers);
    float* w = malloc(size * sizeof(float));
    FILE* f = fopen(path, "rb");
    if (!f || fread(w, sizeof(float), size, f) != size || fgetc(f) != EOF) {
        fprintf(stderr, "%s: expected %zu floats (H%d L%d, %d observations)\n", path, size, h, layers, POKER_OBS);
        exit(1);
    }
    fclose(f);
    return w;
}

// One policy seat (0) against five opponents: a bot id, or a policy net when bot < 0. Returns BB/hand and its
// 95% half-width; counts[class][kind] collects the hero's first preflop decisions.
static double match(const PokerPolicy* hero, int bot, const PokerPolicy* other, int hands, uint64_t seed,
        long counts[3][3], double* half) {
    static Poker g;
    memset(&g, 0, sizeof(g));
    g.rng[0] = seed + 1;
    uint64_t bot_rng[POKER_PLAYERS];
    for (int p = 0; p < POKER_PLAYERS; p++) bot_rng[p] = seed + 0x9e3779b9u * (uint64_t)(p + 1);
    uint32_t stacks[POKER_PLAYERS];
    for (int p = 0; p < POKER_PLAYERS; p++) stacks[p] = 20000;
    double sum = 0, sq = 0;
    for (int h = 0; h < hands; h++) {
        poker_deal(&g, 0, h % POKER_PLAYERS, stacks);
        memset(memory, 0, sizeof(memory));
        int first = 1, cls = hand_class(g.hole[0][0][0], g.hole[0][1][0]);
        while (g.actor[0] != POKER_DONE) {
            int p = g.actor[0], kind;
            int32_t amount = p == 0 ? policy_move(hero, &g, 0, &kind)
                : bot >= 0 ? poker_decide(&g, 0, bot_rng + p, bot, 0, NULL) : policy_move(other, &g, p, &kind);
            if (p == 0 && first) { counts[cls][kind]++; first = 0; }
            poker_step(&g, 0, amount);
        }
        double bb = ((double)g.stack[0][0] - (double)g.initial[0][0]) / POKER_BB;
        sum += bb; sq += bb * bb;
    }
    double mean = sum / hands;
    *half = 1.96 * sqrt(fmax(0, sq / hands - mean * mean) / hands);
    return mean;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s WEIGHTS HIDDEN LAYERS [--hands N] [--seed S] [--vs CHECKPOINT ...]\n", argv[0]);
        return 2;
    }
    PokerPolicy hero = {load(argv[1], atoi(argv[2]), atoi(argv[3])), atoi(argv[2]), atoi(argv[3])};
    int hands = 4000, vs = 0;
    uint64_t seed = 7;
    PokerPolicy others[PANEL_MAX_VS];
    const char* names[PANEL_MAX_VS];
    for (int i = 4; i < argc; i++) {
        if (!strcmp(argv[i], "--hands") && i + 1 < argc) hands = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--vs")) {
            while (i + 1 < argc && strncmp(argv[i + 1], "--", 2) && vs < PANEL_MAX_VS) {
                names[vs] = argv[++i];
                others[vs] = (PokerPolicy){load(names[vs], hero.hidden, hero.layers), hero.hidden, hero.layers};
                vs++;
            }
        } else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    rng = seed * 0x9e3779b97f4a7c15ull + 1;

    // Self-play script test: six copies of the hero.
    {
        static Poker g;
        g.rng[0] = seed + 99;
        uint32_t stacks[POKER_PLAYERS];
        for (int p = 0; p < POKER_PLAYERS; p++) stacks[p] = 20000;
        int n = hands < 2000 ? hands : 2000, distinct = 0, flops = 0, showdowns = 0;
        uint64_t* seen = calloc(n, sizeof(uint64_t));
        for (int h = 0; h < n; h++) {
            poker_deal(&g, 0, h % POKER_PLAYERS, stacks);
            memset(memory, 0, sizeof(memory));
            uint64_t hash = 1469598103934665603ull;
            while (g.actor[0] != POKER_DONE) {
                int kind; int32_t amount = policy_move(&hero, &g, g.actor[0], &kind);
                hash = (hash ^ (uint64_t)(kind * 100000 + amount + 1)) * 1099511628211ull;
                poker_step(&g, 0, amount);
            }
            flops += g.street[0] > 0; showdowns += __builtin_popcount(g.live[0]) > 1;
            int known = 0;
            for (int i = 0; i < distinct && !known; i++) known = seen[i] == hash;
            if (!known) seen[distinct++] = hash;
        }
        printf("self-play   %d hands: %d distinct action sequences, %.0f%% see a flop, %.0f%% showdown\n",
            n, distinct, 100.0 * flops / n, 100.0 * showdowns / n);
        free(seen);
    }

    const int bots[5] = {1, 7, 8, 9, 10};
    const char* bot_names[5] = {"rules", "range", "maniac", "station", "tight"};
    long counts[3][3] = {{0}};
    double total = 0, worst = 1e9;
    for (int b = 0; b < 5; b++) {
        double half, bb = match(&hero, bots[b], NULL, hands, seed + 1000 * b, counts, &half);
        total += bb; worst = bb < worst ? bb : worst;
        printf("vs 5 %-8s %+7.3f BB/hand (95%% +/- %.3f)\n", bot_names[b], bb, half);
    }
    for (int v = 0; v < vs; v++) {
        long unused[3][3] = {{0}};
        double half, bb = match(&hero, -1, &others[v], hands, seed + 7000 + v, unused, &half);
        printf("vs 5 policy %+7.3f BB/hand (95%% +/- %.3f)  %s\n", bb, half, names[v]);
    }
    double rate[3][3];
    for (int c = 0; c < 3; c++) {
        long n = counts[c][0] + counts[c][1] + counts[c][2];
        for (int k = 0; k < 3; k++) rate[c][k] = n ? (double)counts[c][k] / n : 0;
    }
    double sense = (rate[2][0] - rate[0][0]) + (rate[0][2] - rate[2][2]);
    printf("first preflop decision fold/call/raise: premium %.0f/%.0f/%.0f%%  trash %.0f/%.0f/%.0f%%\n",
        100 * rate[0][0], 100 * rate[0][1], 100 * rate[0][2], 100 * rate[2][0], 100 * rate[2][1], 100 * rate[2][2]);
    printf("PANEL mean %+.3f worst %+.3f card_sense %+.2f\n", total / 5, worst, sense);
    return 0;
}
