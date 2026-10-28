// State set round trip (states.h): capture at random decisions of bot hands, restore onto another table, and check
// that seat 0's observation and legal actions there match the deciding seat's on the original table, and that
// playing the restored hand to the end keeps chips conserved.
// Build: clang -O2 -std=c11 ocean/poker/test_states.c -lm -o build/poker/test_states
#include <stdio.h>
#include "states.h"

int main(void) {
    static Poker g, h;
    uint64_t rng = 7;
    g.rng[0] = 11; h.rng[3] = 13;
    uint8_t styles[POKER_PLAYERS] = {1, 7, 8, 9, 10, 11};
    long checked = 0;
    for (int hand = 0; hand < 20000; hand++) {
        uint32_t stacks[POKER_PLAYERS];
        for (int p = 0; p < POKER_PLAYERS; p++) stacks[p] = (20 + poker_random(&rng) % 400) * POKER_BB;
        poker_deal(&g, 0, hand % POKER_PLAYERS, stacks);
        while (g.actor[0] != POKER_DONE) {
            int q = g.actor[0];
            if (poker_random(&rng) % 4 == 0) {
                PokerState s;
                poker_state_capture(&g, 0, styles, &s);
                poker_state_restore(&h, 3, &s);
                float a[POKER_OBS], b[POKER_OBS];
                int32_t ma[POKER_ACTIONS], mb[POKER_ACTIONS];
                poker_observe(&g, 0, q, a);
                poker_observe(&h, 3, 0, b);
                unsigned ka = poker_actions(&g, 0, ma), kb = poker_actions(&h, 3, mb);
                if (memcmp(a, b, sizeof(a)) || ka != kb || memcmp(ma, mb, sizeof(ma)) || s.creator[0] != styles[q]) {
                    for (int i = 0; i < POKER_OBS; i++) if (a[i] != b[i]) printf("obs %d: %f vs %f\n", i, a[i], b[i]);
                    printf("FAIL hand %d seat %d street %d\n", hand, q, g.street[0]);
                    return 1;
                }
                uint32_t before = h.pot[3];
                for (int p = 0; p < POKER_PLAYERS; p++) before += h.stack[p][3];
                while (h.actor[3] != POKER_DONE) poker_step(&h, 3, poker_decide(&h, 3, &rng, styles[(h.actor[3] + q) % POKER_PLAYERS], 0, NULL));
                uint32_t after = 0;
                for (int p = 0; p < POKER_PLAYERS; p++) after += h.stack[p][3];
                if (after != before) { printf("FAIL chips %u -> %u\n", before, after); return 1; }
                checked++;
            }
            poker_step(&g, 0, poker_decide(&g, 0, &rng, styles[q], 0, NULL));
        }
    }
    printf("OK %ld states round-tripped (record %zu bytes)\n", checked, sizeof(PokerState));
}
