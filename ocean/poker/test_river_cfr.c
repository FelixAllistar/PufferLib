// Standalone, bounded native correctness gate. No match data or fitted choices.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#ifndef POKER_PLAYERS
#define POKER_PLAYERS 2
#endif
#if POKER_PLAYERS != 2
#error "test_river_cfr requires POKER_PLAYERS=2"
#endif
#ifndef POKER_RIVER_CFR
#define POKER_RIVER_CFR 1
#endif
#include "river_cfr_core.h"

// Fault injection affects adapter calls in this test translation unit only.
static int fault, cert_calls, increments[8], iteration_calls;
static RcfrGame* test_create(const RcfrHand* h, int n, const double* p0,
    const double* p1, const RcfrPublic* s, char* error, size_t length) {
    return fault == 1 ? NULL : rcfr_create(h,n,p0,p1,s,error,length);
}
static int test_iterate(RcfrGame* g, uint64_t n) {
    assert(iteration_calls < 8); increments[iteration_calls++] = (int)n;
    return rcfr_iterate(g,n);
}
static int test_certificate(RcfrGame* g, RcfrCertificate* out) {
    cert_calls++;
    if (!rcfr_certificate(g,out)) return 0;
    if (fault == 2) out->value_chips = NAN;
    if (fault == 3 || fault == 4) {
        double gap = fault == 4 && cert_calls >= 3 ? 4 : 6;
        out->lower_chips = out->value_chips-gap/2;
        out->upper_chips = out->value_chips+gap/2;
        out->nashconv_chips=gap;out->gap_over_two_chips=gap/2;
        out->lower_bb=out->lower_chips/100;out->upper_bb=out->upper_chips/100;
        out->nashconv_bb=gap/100;out->gap_over_two_bb=gap/200;
    }
    return 1;
}
#define rcfr_create test_create
#define rcfr_iterate test_iterate
#define rcfr_certificate test_certificate
#include "native.c"
#undef rcfr_create
#undef rcfr_iterate
#undef rcfr_certificate

static void reset_fault(int value) {
    fault=value;cert_calls=iteration_calls=0;memset(increments,0,sizeof(increments));
}
static void fixture(Poker* g) {
    memset(g,0,sizeof(*g));
    const uint8_t board[5]={50,45,20,11,1}; // Ah Kd 7c 4s 2d
    for (int b=0;b<5;b++)g->board[b][0]=board[b];
    g->hole[0][0][0]=48;g->hole[0][1][0]=49;
    g->hole[1][0][0]=40;g->hole[1][1][0]=41;
    g->actor[0]=0;g->button[0]=1;g->street[0]=3;
    g->pot[0]=2000;g->increment[0]=100;g->pending[0]=3;
    g->live[0]=g->active[0]=3;g->line[0][0]=43;g->line[1][0]=42;
    for (int p=0;p<2;p++) {
        g->initial[p][0]=20000;g->total[p][0]=1000;g->stack[p][0]=19000;
    }
}
static void short_tie(Poker* g) {
    fixture(g);
    const uint8_t board[5]={50,46,42,38,34};
    for (int b=0;b<5;b++)g->board[b][0]=board[b];
    g->pot[0]=38000;g->hole[0][0][0]=4;g->hole[0][1][0]=8;
    for(int p=0;p<2;p++){g->total[p][0]=19000;g->stack[p][0]=1000;}
}
static void kuhn_check(void) {
    const RcfrHand hands[3]={{0,1,0},{2,3,1},{4,5,2}};
    const double prior[3]={1,1,1};
    const RcfrPublic state={.pot=2,.total={1,1},.stack={1,1},
        .increment=1,.pending=3,.actor=0,.chips_per_bb=1};
    RcfrGame* game=rcfr_create(hands,3,prior,prior,&state,NULL,0);
    assert(game && rcfr_iterate(game,10000));
    RcfrCertificate c;assert(rcfr_certificate(game,&c));
    assert(fabs(c.value_chips+1.0/18)<.002 && c.nashconv_chips<.002);
    assert(c.lower_chips<=-1.0/18+1e-10 && c.upper_chips>=-1.0/18-1e-10);
    assert(c.gap_over_two_chips==c.nashconv_chips/2);rcfr_free(game);
}
int main(void) {
    kuhn_check();
    Poker *g=calloc(1,sizeof(*g)),*shadow=calloc(1,sizeof(*shadow));
    PokerCfrInputs *in=calloc(1,sizeof(*in)),*altered=calloc(1,sizeof(*altered));
    PokerCfrResult *out=calloc(1,sizeof(*out)),*second=calloc(1,sizeof(*second));
    assert(g && shadow && in && altered && out && second);
    fixture(g);memcpy(shadow,g,sizeof(*g));reset_fault(0);
    assert(poker_cfr_make_inputs(g,0,NULL,in));
    unsigned factors=0;
    for(int h=0;h<POKER_CFR_HANDS;h++) {
        RcfrHand pair=in->hand[h];assert(pair.c0<pair.c1 && pair.c1<52);
        for(int b=0;b<5;b++)assert(pair.c0!=g->board[b][0] && pair.c1!=g->board[b][0]);
        for(int p=0;p<2;p++) {
            double expected=pow(poker_range_weight(g,0,p,pair.c0,pair.c1,NULL),0.42089278763026228);
            assert(in->prior[p][h]==expected);factors++;
        }
    }
    assert(poker_cfr_solve_public(in,out));
    assert(out->checkpoint<=1000 && out->certificate.nashconv_bb<=.05);
    g->hole[0][0][0]=in->hand[400].c0;g->hole[0][1][0]=in->hand[400].c1;
    g->hole[1][0][0]=255;g->hole[1][1][0]=254;g->rng[0]=UINT64_MAX;
    g->decisions[0]=64000;g->raises[0]=255;g->last_raiser[0]=254;
    for(int i=0;i<POKER_HISTORY;i++)g->history[i][0]=UINT32_MAX-i;
    assert(poker_cfr_make_inputs(g,0,NULL,altered));
    assert(!memcmp(in,altered,sizeof(*in)));reset_fault(0);
    assert(poker_cfr_solve_public(altered,second));assert(!memcmp(out,second,sizeof(*out)));
    unsigned samples=0;
    for(int h=0;h<POKER_CFR_HANDS;h+=180)for(int k=0;k<3;k++) {
        uint64_t rng=91+k,expected_rng=rng;int32_t action=123;
        double draw=poker_random(&expected_rng)/4294967296.0,cumulative=0;
        int32_t expected=0;
        for(int a=0;a<out->action_count;a++) {
            cumulative+=out->average[a*POKER_CFR_HANDS+h];
            if(draw<cumulative || a+1==out->action_count){expected=out->action[a];break;}
        }
        assert(poker_cfr_select_row(in,out,in->hand[h].c0,in->hand[h].c1,&rng,&action));
        assert(action==expected && rng==expected_rng);samples++;
    }
    // Whole policy call leaves the game unchanged and consumes exactly the
    // original external seed+roll draws, independently of solver work.
    memcpy(g,shadow,sizeof(*g));uint64_t rng=711,expected_rng=rng;
    poker_random(&expected_rng);poker_random(&expected_rng);poker_random(&expected_rng);
    reset_fault(0);int32_t chosen=poker_decide(g,0,&rng,7,0,NULL);
    assert(rng==expected_rng && !memcmp(g,shadow,sizeof(*g)));
    // This free root offers check or a legal minimum/capped raise, never fold.
    assert(chosen>=0);
    if(chosen) {
        uint32_t maximum=g->bet[0][0]+g->stack[0][0];
        assert(poker_can_raise(g,0) && (uint32_t)chosen<=maximum);
        assert((uint32_t)chosen>=poker_min_raise(g,0) || (uint32_t)chosen==maximum);
    }
    PokerProfile profiles[2]={{0}};assert(!poker_cfr_make_inputs(g,0,profiles,in));
    for(int street=0;street<3;street++){g->street[0]=street;assert(!poker_cfr_make_inputs(g,0,NULL,in));}
    fixture(g);g->initial[0][0]=19999;assert(!poker_cfr_make_inputs(g,0,NULL,in));
    fixture(g);g->board[1][0]=g->board[0][0];assert(!poker_cfr_make_inputs(g,0,NULL,in));
    short_tie(g);unsigned faults=0;
    for(int mode=1;mode<=3;mode++) {
        reset_fault(mode);rng=881;chosen=991;
        assert(!poker_cfr_action(g,0,&rng,NULL,&chosen));assert(rng==881 && chosen==991);
        if(mode==3){const int expected[5]={100,100,200,400,200};
            assert(iteration_calls==5 && cert_calls==5);
            assert(!memcmp(increments,expected,sizeof(expected)));}
        faults++;
    }
    reset_fault(4);assert(poker_cfr_make_inputs(g,0,NULL,in));assert(poker_cfr_solve_public(in,out));
    assert(out->checkpoint==400 && iteration_calls==3 && cert_calls==3);
    assert(increments[0]==100 && increments[1]==100 && increments[2]==200);
    printf("river CFR: Kuhn analytic gate, %u public factors, full-solve privacy, %u row samples, "
        "external RNG/state, exclusions, %u rejection faults and first-checkpoint schedule passed\n",factors,samples,faults);
    free(g);free(shadow);free(in);free(altered);free(out);free(second);return 0;
}
