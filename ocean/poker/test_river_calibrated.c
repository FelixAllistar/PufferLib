#include <math.h>
#include <stdio.h>
#ifndef POKER_PLAYERS
#define POKER_PLAYERS 2
#endif
#if POKER_PLAYERS != 2
#error "Calibrated river fixtures require POKER_PLAYERS=2"
#endif
#ifndef POKER_RIVER_EXACT
#define POKER_RIVER_EXACT 1
#endif
#ifndef POKER_RIVER_CALIBRATED
#define POKER_RIVER_CALIBRATED 1
#endif
#include "native.c"

// Small test ABI for independent Python/model-artifact equivalence checks.
void poker_calibrated_probabilities(double q, uint32_t call, uint32_t eligible,
    uint32_t pot, uint32_t actor_stack, uint32_t other_stack, int can_raise, double* out) {
    PokerRiverResponseContext c=poker_river_response_context(call,eligible,pot,actor_stack,other_stack,can_raise);
    poker_river_response_predict(&c,q,out);
}

void poker_calibrated_fixture(Poker* g, uint64_t seed, uint32_t s0, uint32_t s1, int facing) {
    poker_seed(g,seed);uint32_t stacks[2]={s0,s1};poker_deal(g,0,seed&1,stacks);
    while(g->street[0]<3)poker_step(g,0,0);
    g->line[0][0]=(seed&3)|(((seed/4)&3)<<2)|(((seed/16)&3)<<4);
    g->line[1][0]=((seed/2)&3)|(((seed/8)&3)<<2)|(((seed/32)&3)<<4);
    if(facing)poker_step(g,0,100+seed%1000);
}

void poker_calibrated_hide_other(Poker* g) {
    int other=1-g->actor[0];g->hole[other][0][0]=255;g->hole[other][1][0]=254;
    g->rng[0]^=UINT64_C(0x1234583981);
}

int poker_calibrated_probe(Poker* g, uint64_t seed, const PokerProfile* profiles, double* out) {
    uint64_t simulations=(uint64_t)poker_random(&seed)<<32;simulations|=poker_random(&seed);
    if(!simulations)simulations=1;
    PokerRiverModel ctx;int32_t action=0;
    int ok=poker_river_action_value(g,0,&simulations,128,profiles,&action,&ctx);
    out[0]=ok;out[1]=ok?ctx.calibrated:0;
    if(ok){out[2]=ctx.root_passive_value;out[3]=ctx.root_raise_value;out[4]=ctx.root_value;}
    return action;
}

static void probability_cases(void) {
    assert(poker_river_response_logistic(-1e6)==1e-6);
    assert(poker_river_response_logistic(1e6)==1-1e-6);
    for(int i=0;i<=100;i++)for(int facing=0;facing<2;facing++)for(int legal=0;legal<2;legal++) {
        double p[3];poker_calibrated_probabilities(i/100.,facing?500:0,2000,1500,18500,10000,legal,p);
        assert(p[0]>=0&&p[1]>=0&&p[2]>=0&&fabs(p[0]+p[1]+p[2]-1)<1e-12);
        if(!facing)assert(p[0]==0);
        if(!legal)assert(p[2]==0);
        if(facing)assert(p[0]>=1e-6&&p[0]<=1-1e-6);
    }
    PokerRiverResponseContext c=poker_river_response_context(500,2000,1500,10000,0,0);
    assert(c.stack_ratio==0&&c.can_raise==0);
}

static void tree_cases(void) {
    Poker g={0};g.actor[0]=0;g.street[0]=3;g.live[0]=g.active[0]=3;
    g.total[0][0]=g.total[1][0]=1000;g.button[0]=1;
    PokerRiverModel ctx={.game=&g,.hero=0,.opponent=1,.samples=8,.calibrated=1,
        .initial_stack={19000,19000},.initial_pot=2000};
    PokerRiverMoney m={.pot=21000,.high=19000,.increment=19000,.bet={19000,0},.stack={0,19000}};
    double weights[POKER_RIVER_OPPONENTS]={0};
    for(int i=0;i<8;i++) {
        weights[i]=1+.1*i;ctx.outcome[i]=(i%3)-1;
        ctx.opponent_equity[0][i]=ctx.opponent_equity[1][i]=.1+.1*i;
    }
    // A root hero shove leaves opponent fold/call, never a raise. Independently
    // integrate the calibrated probabilities with the native terminal utility.
    assert(poker_river_raise(&m,1)==0);
    PokerRiverMoney called=poker_river_pay(m,1,0);
    double expected=0;
    for(int i=0;i<8;i++) {
        double p[3];poker_calibrated_probabilities(ctx.opponent_equity[1][i],19000,40000,21000,19000,0,0,p);
        assert(p[2]==0);
        expected+=weights[i]*(p[0]*2000+p[1]*poker_river_showdown(&ctx,&called,ctx.outcome[i]));
    }
    double got=poker_river_opponent_value(&ctx,&m,weights,0,1);
    assert(fabs(expected-got)<1e-8);
    double deeper=poker_river_opponent_value(&ctx,&m,weights,1,1);
    ctx.calibrated=0;
    double fallback=poker_river_opponent_value(&ctx,&m,weights,1,1);
    assert(deeper==fallback&&got!=fallback);
    assert(fallback==poker_river_opponent_value(&ctx,&m,weights,0,1));

    // With a legal raise, continuation weighting uses the unconditional raise
    // probability once, and the deeper hero decision aggregates that posterior.
    ctx.calibrated=1;m=(PokerRiverMoney){.pot=3000,.high=1000,.increment=1000,
        .bet={1000,0},.stack={18000,19000}};
    called=poker_river_pay(m,1,0);int32_t raise=poker_river_raise(&m,1);assert(raise);
    double posterior[POKER_RIVER_OPPONENTS]={0};expected=0;
    for(int i=0;i<8;i++) {
        double p[3];poker_calibrated_probabilities(ctx.opponent_equity[1][i],1000,4000,3000,19000,18000,1,p);
        expected+=weights[i]*(p[0]*2000+p[1]*poker_river_showdown(&ctx,&called,ctx.outcome[i]));
        posterior[i]=weights[i]*p[2];
    }
    PokerRiverMoney raised=poker_river_pay(m,1,raise);
    expected+=poker_river_hero_value(&ctx,&raised,posterior,1,1);
    assert(fabs(expected-poker_river_opponent_value(&ctx,&m,weights,0,1))<1e-8);
}

int main(void) {
    assert(POKER_PLAYERS==2);probability_cases();tree_cases();
    Poker* g=calloc(1,sizeof(*g));PokerProfile profiles[2]={{0}};
    unsigned scope=0,privacy=0,rng_checks=0;
    for(int i=0;i<48;i++)for(int variant=0;variant<3;variant++) {
        uint32_t s0=variant==1?15000:20000,s1=variant==2?15000:20000;
        poker_calibrated_fixture(g,202610189010+i,s0,s1,i&1);
        double out[5];uint64_t seed=922103+i;
        int32_t action=poker_calibrated_probe(g,seed,NULL,out);
        assert(out[0]&&out[1]==(variant==0));scope++;
        Poker before=*g;uint64_t rng=seed,expected=seed;
        for(int j=0;j<3;j++)poker_random(&expected);
        assert(poker_equity_bot(g,0,&rng,128,NULL)==action&&rng==expected);
        assert(!memcmp(g,&before,sizeof(*g)));rng_checks++;
        double hidden_out[5];poker_calibrated_hide_other(g);
        assert(poker_calibrated_probe(g,seed,NULL,hidden_out)==action);
        assert(!memcmp(out,hidden_out,sizeof(out)));privacy++;*g=before;
        poker_calibrated_probe(g,seed,profiles,out);assert(out[0]&&!out[1]);scope++;
    }
    printf("PASS calibrated river: probability_cases=404 allin_no_raise=1 posterior_weighting=1 deeper_fallback=1 scope=%u privacy=%u rng=%u\n",scope,privacy,rng_checks);
    free(g);
}
