// All-in rate (2026-10-28): a 256x3 policy in seat 0 vs five copies of a bot style (or itself), as real cash sessions (session.h, 2026-11-10: 100 BB buy-ins, stacks,
// memory and seat tenure carry over 1000-hand sessions, busted players rebuy). Prints
// the share of hands where it puts its whole stack in, the share it voluntarily enters (calls or raises preflop beyond
// the blinds), and how often it ends all in when it enters. Gauge for the loss-minimization failure (shove-or-fold).
// gen3 vs rules 34% all in; gen4 control 24% (reports/poker_experiments/20261028_gen4/allin_*.txt).
// Build: clang -O2 -march=native -std=c11 -DNDEBUG -Iocean/poker ocean/poker/allin.c -lm -o build/poker/allin
// Usage: allin WEIGHTS
#include <stdio.h>
#include "session.h"
static PokerPolicy net; static PokerMemory memory[POKER_PLAYERS]; static uint64_t rng = 5;
static int32_t pmove(Poker* g, int p) {
    float obs[POKER_PUB], logits[POKER_ACTIONS + 1]; int32_t amounts[POKER_ACTIONS];
    unsigned mask = poker_actions(g, 0, amounts); poker_observe_public(g, 0, p, obs);
    poker_policy_forward(&net, memory[p], obs, logits);
    return amounts[poker_policy_sample(logits, mask, poker_random(&rng))];
}
int main(int argc, char** argv) {
    float* w = poker_policy_load(argv[1], 256, 3);
    if (!w) return 2;
    net = (PokerPolicy){w, 256, 3};
    int opp[] = {-1, 1, 7, 10, 11}; const char* names[] = {"self", "rules", "range", "tight", "trapper"};
    static Poker g; g.rng[0] = 77;
    for (int o = 0; o < 5; o++) {
        long hands = 10000, allin = 0, vpip = 0, allin_vpip = 0;
        PokerSession sess = poker_session(100, POKER_SESSION_HANDS);
        for (long h = 0; h < hands; h++) {
            poker_session_begin(&sess, &g, memory);
            poker_deal(&g, 0, h % POKER_PLAYERS, sess.stack);
            int a = 0, v = 0;
            while (g.actor[0] != POKER_DONE) {
                int p = g.actor[0]; int32_t m = (p == 0 || opp[o] < 0) ? pmove(&g, p) : poker_bot(&g, 0, &rng, opp[o]);
                if (p == 0 && !g.street[0] && m >= 0 && (m > 0 || g.high[0] > g.bet[0][0])) v = 1;
                poker_step(&g, 0, m);
                if (p == 0 && g.stack[0][0] == 0) a = 1;
            }
            allin += a; vpip += v; allin_vpip += a && v;
            poker_session_end(&sess, &g, memory);
        }
        printf("%-8s all-in %5.1f%% of hands, plays %5.1f%%, all-in when playing %5.1f%%\n", names[o],
            100.0 * allin / hands, 100.0 * vpip / hands, 100.0 * allin_vpip / (vpip ? vpip : 1));
    }
}
