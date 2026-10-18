// Independent quadratic blocker-equity check for the exact CDF integrator.
#include <math.h>
#include <stdio.h>
#define POKER_RIVER_EXACT 1
#ifndef POKER_PLAYERS
#define POKER_PLAYERS 2
#endif
#if POKER_PLAYERS != 2
#error "Exact river reference test requires a heads-up build"
#endif
#include "native.c"

int main(void) {
    Poker* g = calloc(1, sizeof(*g)); poker_seed(g, 202610188902);
    uint32_t stacks[2] = {20000, 20000};
    double max_error = 0, max_call_error = 0;
    unsigned equity_checks = 0, rng_checks = 0;
    for (int fixture = 0; fixture < 20; fixture++) {
        poker_deal(g, 0, fixture & 1, stacks);
        while (g->street[0] < 3) poker_step(g, 0, 0);
        // Public line inputs span weak, strong and concentrated hypotheses.
        g->line[0][0] = (fixture & 3) | ((fixture % 3 + 1) << 2) | ((fixture % 2 + 2) << 4);
        g->line[1][0] = ((fixture + 1) & 3) | (((fixture + 1) % 3 + 1) << 2) | (3 << 4);
        if (fixture & 1) poker_step(g, 0, 400 + fixture * 100);
        int hero = g->actor[0], opponent = 1 - hero;
        Poker before = *g, view = {0}; view.street[0] = 3; view.button[0] = g->button[0];
        uint64_t rng = 432781, policy_rng = rng;
        PokerRiverModel ctx; int32_t action;
        assert(poker_river_action_value(g,0,&rng,128,NULL,&action,&ctx));
        assert(ctx.samples == 990 && !memcmp(g,&before,sizeof(*g)));
        uint64_t expected_rng = policy_rng;
        for (int j = 0; j < 3; j++) poker_random(&expected_rng);
        assert(poker_equity_bot(g,0,&policy_rng,128,NULL) == action);
        assert(policy_rng == expected_rng); rng_checks++;
        uint8_t cards[7], pool[52], opool[52]; int available = 0, oavailable = 0;
        uint64_t blocked = 0, own = (UINT64_C(1)<<g->hole[hero][0][0]) | (UINT64_C(1)<<g->hole[hero][1][0]);
        for (int b=0;b<5;b++) {cards[b+2]=view.board[b][0]=g->board[b][0];blocked|=UINT64_C(1)<<cards[b+2];}
        for (int c=0;c<52;c++) if (!(blocked & (UINT64_C(1)<<c))) {
            pool[available++]=c; if (!(own & (UINT64_C(1)<<c))) opool[oavailable++]=c;
        }
        assert(available==47 && oavailable==45);
        unsigned passive = g->high[0] > g->bet[hero][0] ? 2 : 1;
        if (passive < (g->line[hero][0]>>6)) passive=g->line[hero][0]>>6;
        uint64_t masks[1081]; uint32_t ranks[1081]; double weights[2][1081]; int refs=0;
        for (int a=0;a<available;a++) for(int b=a+1;b<available;b++) {
            cards[0]=pool[a];cards[1]=pool[b];
            masks[refs]=(UINT64_C(1)<<cards[0])|(UINT64_C(1)<<cards[1]);
            ranks[refs]=poker_rank(cards,7);
            for(int k=0;k<2;k++) {
                view.line[hero][0]=(g->line[hero][0]&63u)|((k?3u:passive)<<6);
                weights[k][refs]=poker_range_weight(&view,0,hero,cards[0],cards[1],NULL);
            }
            refs++;
        }
        cards[0]=g->hole[hero][0][0];cards[1]=g->hole[hero][1][0];
        uint32_t own_rank=poker_rank(cards,7);
        double call_mass=0,call_value=0;
        PokerRiverMoney money={.pot=g->pot[0],.high=g->high[0],.increment=g->increment[0],
            .bet={g->bet[hero][0],g->bet[opponent][0]},.stack={g->stack[hero][0],g->stack[opponent][0]},
            .reopen={g->reopen[hero][0],g->reopen[opponent][0]}};
        money=poker_river_pay(money,0,0);
        int index=0;
        for(int a=0;a<oavailable;a++) for(int b=a+1;b<oavailable;b++) {
            cards[0]=opool[a];cards[1]=opool[b];uint32_t rank=poker_rank(cards,7);
            uint64_t mask=(UINT64_C(1)<<cards[0])|(UINT64_C(1)<<cards[1]);
            assert(ctx.outcome[index]==(own_rank>rank)-(own_rank<rank));
            for(int k=0;k<2;k++) {
                double mass=0,share=0;
                for(int j=0;j<refs;j++) if(!(mask&masks[j])) {
                    mass+=weights[k][j];
                    share+=weights[k][j]*(rank>ranks[j]?1:rank==ranks[j]?.5:0);
                }
                double error=fabs(share/mass-ctx.opponent_equity[k][index]);
                if(error>max_error)max_error=error;
                assert(error<1e-10);equity_checks++;
            }
            double w=poker_range_weight(g,0,opponent,cards[0],cards[1],NULL);
            call_mass+=w;call_value+=w*poker_river_showdown(&ctx,&money,ctx.outcome[index]);
            index++;
        }
        assert(index==990);
        if(fixture&1) {
            double error=fabs(call_value/call_mass-ctx.root_passive_value);
            if(error>max_call_error)max_call_error=error;
            assert(error<1e-8);
        }
        Poker hidden=*g; hidden.rng[0]^=UINT64_C(0x819237551);
        hidden.hole[opponent][0][0]^=31;hidden.hole[opponent][1][0]^=17;
        int32_t hidden_action;PokerRiverModel other;uint64_t another=987654;
        assert(poker_river_action_value(&hidden,0,&another,32,NULL,&hidden_action,&other));
        assert(action==hidden_action && ctx.root_value==other.root_value);
    }
    printf("PASS exact: fixtures=20 blocker_equities=%u max_error=%.3g max_call_error=%.3g RNG=%u hidden-card/seed invariance=20\n",
           equity_checks,max_error,max_call_error,rng_checks);
    free(g);
}
