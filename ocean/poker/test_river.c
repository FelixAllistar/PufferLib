#include <math.h>
#include <stdio.h>
#ifndef POKER_RIVER_VALUE
#define POKER_RIVER_VALUE 1
#endif
#include "native.c"

static void value_cases(void) {
    Poker g = {0};
    g.actor[0] = 0; g.live[0] = g.active[0] = 3;
    g.street[0] = 3; g.button[0] = 1; g.pot[0] = 150;
    g.total[0][0] = g.total[1][0] = 75;
    PokerRiverModel c = {.game=&g,.hero=0,.opponent=1,.samples=10,
        .initial_stack={1000,1000},.initial_pot=150};
    PokerRiverMoney m = {.pot=150,.increment=100,.stack={1000,1000}};
    double w[128] = {0};
    for (int i = 0; i < 10; i++) {
        w[i] = 1; c.outcome[i] = i < 6 ? 1 : -1;
        c.opponent_equity[0][i] = c.opponent_equity[1][i] = i < 5 ? 0 : .7;
    }
    poker_river_hero_value(&c,&m,w,0,1);
    assert(c.root_action==0 && fabs(c.root_value-90)<1e-9);
    assert(fabs(c.root_raise_value-60)<1e-9);
    for (int i=0;i<10;i++) {
        c.outcome[i]=-1; c.opponent_equity[1][i]=.2;
    }
    poker_river_hero_value(&c,&m,w,0,1);
    assert(c.root_action==100 && fabs(c.root_value-150)<1e-9);
    for (int i=0;i<10;i++) {
        c.outcome[i]=1; c.opponent_equity[1][i]=.9;
    }
    poker_river_hero_value(&c,&m,w,0,1);
    assert(c.root_action==100 && c.root_value>150 && c.root_value<=1150);
}

int main(void) {
    value_cases();
    Poker* g=calloc(1,sizeof(*g)); poker_seed(g,20261018789);
    uint64_t rng=53111; unsigned covered=0,privacy=0,terminals=0;
    uint32_t stacks[POKER_PLAYERS];
    for (int hand=0;hand<6000;hand++) {
        for(int p=0;p<POKER_PLAYERS;p++) stacks[p]=1000+poker_random(&rng)%20000;
        poker_deal(g,0,hand%POKER_PLAYERS,stacks);
        while(g->actor[0]!=POKER_DONE) {
            int h=g->actor[0];
            int32_t a; uint64_t test_rng=13333;
            Poker before=*g; PokerRiverModel diag;
            int modeled=poker_river_action_value(g,0,&test_rng,32,NULL,&a,&diag);
            assert(!memcmp(g,&before,sizeof(before)));
            if(modeled) {
                covered++;
                Poker hidden=*g;
                for(int p=0;p<POKER_PLAYERS;p++) if(p!=h) {
                    hidden.hole[p][0][0]=(hidden.hole[p][0][0]+9)%52;
                    hidden.hole[p][1][0]=(hidden.hole[p][1][0]+13)%52;
                }
                hidden.rng[0]^=UINT64_C(0x91823477);
                int32_t b; uint64_t other_rng=13333;
                PokerRiverModel other;
                assert(poker_river_action_value(&hidden,0,&other_rng,32,NULL,&b,&other));
                assert(a==b && other_rng==test_rng && diag.root_value==other.root_value);
                privacy++;
                // Compare model's terminal chip accounting to the real kernel.
                int o=diag.opponent;
                Poker end=*g;
                PokerRiverMoney money={.pot=g->pot[0],.high=g->high[0],.increment=g->increment[0],
                    .bet={g->bet[h][0],g->bet[o][0]},.stack={g->stack[h][0],g->stack[o][0]},
                    .reopen={g->reopen[h][0],g->reopen[o][0]}};
                int32_t raise=poker_river_raise(&money,0);
                int32_t chosen=(poker_random(&rng)&1)?raise:0;
                poker_step(&end,0,chosen);
                money=poker_river_pay(money,0,chosen);
                if(end.actor[0]!=POKER_DONE) {
                    assert(end.actor[0]==o);
                    poker_step(&end,0,0); money=poker_river_pay(money,1,0);
                }
                assert(end.actor[0]==POKER_DONE);
                uint8_t cards[7];
                for(int j=0;j<5;j++) cards[2+j]=g->board[j][0];
                cards[0]=g->hole[h][0][0];cards[1]=g->hole[h][1][0];
                uint32_t rh=poker_rank(cards,7);
                cards[0]=g->hole[o][0][0];cards[1]=g->hole[o][1][0];
                uint32_t ro=poker_rank(cards,7);
                double expected=poker_river_showdown(&diag,&money,(rh>ro)-(rh<ro));
                assert(expected==(int)end.stack[h][0]-(int)g->stack[h][0]);
                terminals++;
            }
            a=poker_bot(g,0,&rng,hand%3==0?1:7);
            poker_step(g,0,a);
            assert(g->decisions[0]<4096);
        }
        uint32_t before=0,after=0;
        for(int p=0;p<POKER_PLAYERS;p++){before+=stacks[p];after+=g->stack[p][0];}
        assert(before==after);
    }
    assert(covered>10);
    printf("PASS river %d seats: modeled=%u privacy=%u terminal accounting=%u\n",POKER_PLAYERS,covered,privacy,terminals);
    free(g);
}
