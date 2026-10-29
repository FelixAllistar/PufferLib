// Escalation probe (2026-10-29): what does a policy do once a pot gets contested? The user saw gen4 play hesitantly,
// then jump to all in as soon as someone bets or raises back after it engaged. The policy sits in seat 0 at 100 BB
// against five copies of a bot (or the web's mixed field: Fullhouse, range, Fullhouse, rules and range), button
// rotating, and every one of its decisions is binned by situation and by action.
// Situations: unopened (no bet to call; preflop: only the blinds), facing a bet (it has not bet or raised this hand),
// facing a raise back (someone raised after the policy itself bet or raised this hand).
// Actions: fold, check/call, small raise (to at most half the pot after calling), medium (up to 1.2 pots), big (more,
// not all in), all in.
// Build: clang -O2 -march=native -std=c11 -DNDEBUG -DPOKER_FULLHOUSE_MODEL='"/path/fullhouse_weights.h"' -Iocean/poker
//   ocean/poker/escalation.c -lm -o build/poker/escalation
// Usage: escalation WEIGHTS HIDDEN LAYERS [HANDS]
#include <stdio.h>
#include "policy.h"

static PokerPolicy net;
static float memory[POKER_PLAYERS][4 * POKER_POLICY_MAX_HIDDEN];
static uint64_t rng = 9;
enum { UNOPENED, FACING_BET, FACING_RERAISE, SITUATIONS };
enum { FOLD, CALL, SMALL, MEDIUM, BIG, ALLIN, KINDS };
static const char* SIT[] = {"unopened", "facing a bet", "facing a raise back"};
static const char* KIND[] = {"fold", "check/call", "small", "medium", "big", "all in"};

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: escalation WEIGHTS HIDDEN LAYERS [HANDS]\n"); return 2; }
    int h = atoi(argv[2]), l = atoi(argv[3]); long hands = argc > 4 ? atol(argv[4]) : 20000;
    size_t n = poker_policy_size(h, l); float* w = malloc(n * sizeof(float)); FILE* f = fopen(argv[1], "rb");
    if (!f || fread(w, sizeof(float), n, f) != n) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    net = (PokerPolicy){w, h, l};
    const int mixed[POKER_PLAYERS] = {0, 4, 7, 4, 1, 7};
    const int fields[][POKER_PLAYERS] = {{0, 7, 7, 7, 7, 7}, {0, 1, 1, 1, 1, 1}, {0, 10, 10, 10, 10, 10}, {0, 11, 11, 11, 11, 11}};
    const char* fname[] = {"range", "rules", "tight", "trapper", "mixed web field"};
    static Poker g; g.rng[0] = 4242;
    for (int fi = 0; fi < 5; fi++) {
        const int* field = fi < 4 ? fields[fi] : mixed;
        if (fi == 4 && !poker_bot_supported(4)) continue;
        long count[SITUATIONS][KINDS] = {{0}}, allin_hands = 0, played = 0; double bb = 0;
        for (long hand = 0; hand < hands; hand++) {
            uint32_t st[POKER_PLAYERS]; for (int p = 0; p < POKER_PLAYERS; p++) st[p] = 100 * POKER_BB;
            poker_deal(&g, 0, hand % POKER_PLAYERS, st); memset(memory, 0, sizeof(memory));
            int aggressed = 0, raised_after = 0, entered = 0;
            while (g.actor[0] != POKER_DONE) {
                int p = g.actor[0];
                if (p) {
                    uint32_t before = g.high[0];
                    poker_step(&g, 0, poker_bot(&g, 0, &rng, field[p]));
                    if (aggressed && g.high[0] > before) raised_after = 1;
                    continue;
                }
                float obs[POKER_OBS], logits[POKER_ACTIONS + 1]; int32_t amounts[POKER_ACTIONS];
                unsigned mask = poker_actions(&g, 0, amounts); poker_observe(&g, 0, 0, obs);
                poker_policy_forward(&net, memory[0], obs, logits);
                int32_t a = amounts[poker_policy_sample(logits, mask, poker_random(&rng))];
                uint32_t call = g.high[0] - g.bet[0][0], pot = g.pot[0] + call;
                int facing = g.street[0] ? call > 0 : g.high[0] > POKER_BB;
                int sit = !facing ? UNOPENED : raised_after ? FACING_RERAISE : FACING_BET;
                int kind;
                if (a < 0) kind = FOLD;
                else if (a == 0) kind = CALL;
                else if ((uint32_t)a >= g.bet[0][0] + g.stack[0][0]) kind = ALLIN;
                else { uint32_t add = a - g.high[0]; kind = add * 2 <= pot ? SMALL : add * 5 <= pot * 6 ? MEDIUM : BIG; }
                count[sit][kind]++;
                if (a > 0) { aggressed = 1; raised_after = 0; }
                if (a >= 0 && (a > 0 || call)) entered = 1;
                poker_step(&g, 0, a);
            }
            allin_hands += g.stack[0][0] == 0 || (g.total[0][0] >= 100 * POKER_BB);
            played += entered;
            bb += ((int32_t)g.stack[0][0] - (int32_t)g.initial[0][0]) / (double)POKER_BB;
        }
        printf("== vs %s: %.2f BB/hand, enters %.0f%% of hands, all in %.1f%% of hands\n", fname[fi], bb / hands,
            100.0 * played / hands, 100.0 * allin_hands / hands);
        for (int s = 0; s < SITUATIONS; s++) {
            long total = 0; for (int k = 0; k < KINDS; k++) total += count[s][k];
            printf("  %-20s %7ld decisions:", SIT[s], total);
            for (int k = 0; k < KINDS; k++) printf("  %s %4.1f%%", KIND[k], total ? 100.0 * count[s][k] / total : 0);
            printf("\n");
        }
    }
}
