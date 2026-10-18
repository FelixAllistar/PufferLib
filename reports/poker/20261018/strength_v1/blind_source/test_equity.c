#include <math.h>
#include <stdio.h>
#include "native.c"

int main(void) {
    Poker* g = calloc(1, sizeof(Poker));
    poker_seed(g, 91);
#if POKER_PLAYERS == 2
    // Public size is measured before paying the action, relative to the pot
    // after a hypothetical call. Raising again must retain the street ordinal.
    uint32_t initial[2] = {20000, 20000};
    uint8_t deck[9] = {0, 1, 4, 5, 8, 12, 16, 20, 24};
    poker_reset(g, 0, 1, initial, deck);
    poker_step(g, 0, 250);
    assert(g->pressure[1][0][0] == (32 | 6));
    poker_step(g, 0, 750);
    assert(g->pressure[0][0][0] == (64 | 8));
    poker_step(g, 0, 0);
    assert(g->street[0] == 1 && g->pressure[1][0][0] == (32 | 6));
    poker_step(g, 0, 1000);
    assert(g->pressure[0][1][0] == (32 | 5));
    poker_step(g, 0, 4000);
    assert(g->pressure[1][1][0] == (64 | 7));
    poker_step(g, 0, 0);
    assert(g->street[0] == 2 && g->pressure[1][1][0] == (64 | 7));
    poker_reset(g, 0, 1, initial, deck);
    for (int p = 0; p < 2; p++) for (int s = 0; s < 4; s++)
        assert(!g->pressure[p][s][0]);
    // Clear the fixture before the synthetic side-pot state below.
    poker_seed(g, 91);
#endif
    // Everyone plays the royal-flush board. A short hero can win only the
    // 100-chip layer; deeper players' side-pot money must never enter its price.
    g->actor[0] = 0; g->street[0] = 3;
    g->hole[0][0][0] = 0; g->hole[0][1][0] = 4;
    for (int i = 0; i < 5; i++) g->board[i][0] = 51 - 4*i;
    g->live[0] = (1u << POKER_PLAYERS) - 1;
    g->stack[0][0] = 50; g->total[0][0] = g->bet[0][0] = 50;
    g->high[0] = 300;
    for (int p = 1; p < POKER_PLAYERS; p++) g->total[p][0] = g->bet[p][0] = 300;
    g->pot[0] = 50 + 300*(POKER_PLAYERS-1);
    uint64_t rng = 371;
    PokerEquity e;
    poker_equity(g,0,&rng,128,0,NULL,&e);
    assert(fabs(e.share - 1.0/POKER_PLAYERS)<1e-6);
    assert(fabs(e.payout-100)<1e-4 && e.eligible_pot==100*POKER_PLAYERS);
#if POKER_PLAYERS > 2
    // Folded contributions are still in the eligible main pot.
    g->live[0] &= ~(1u << (POKER_PLAYERS-1));
    poker_equity(g,0,&rng,128,1,NULL,&e);
    assert(fabs(e.payout-100.0*POKER_PLAYERS/(POKER_PLAYERS-1))<1e-4);
#endif
    uint32_t stacks[POKER_PLAYERS];
    PokerProfile profiles[POKER_PLAYERS] = {0};
    uint64_t steps = 0;
    for (int h = 0; h < 2000; h++) {
        for (int p = 0; p < POKER_PLAYERS; p++) stacks[p] = 1 + poker_random(&rng)%20000;
        poker_deal(g,0,h%POKER_PLAYERS,stacks); poker_profile_begin(profiles);
        while (g->actor[0]!=POKER_DONE) {
            int style = 6 + (poker_random(&rng)%3);
            int32_t a = poker_bot_with_history(g,0,&rng,style,profiles);
            poker_profile_update(profiles,g,0,a);poker_step(g,0,a); steps++;
            assert(g->decisions[0]<4096);
        }
        uint32_t before=0,after=0;
        for (int p=0;p<POKER_PLAYERS;p++) {
            before+=stacks[p];after+=g->stack[p][0];
            assert(profiles[p].pfr<=profiles[p].vpip && profiles[p].vpip<=profiles[p].hands);
        }
        assert(before==after);
    }
    free(g);
    printf("PASS equity: players=%d, 2000 unequal-stack hands, %llu legal decisions, side pots and public profiles\n",
        POKER_PLAYERS,(unsigned long long)steps);
}
