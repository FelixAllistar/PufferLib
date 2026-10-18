#include <math.h>
#include <stdio.h>
#ifndef POKER_PLAYERS
#define POKER_PLAYERS 2
#endif
#if POKER_PLAYERS != 2
#error "test_river_range_calibrated requires POKER_PLAYERS=2"
#endif
#ifndef POKER_RIVER_EXACT
#define POKER_RIVER_EXACT 1
#endif
#ifndef POKER_RIVER_CALIBRATED
#define POKER_RIVER_CALIBRATED 1
#endif
#ifndef POKER_RIVER_RANGE_CALIBRATED
#define POKER_RIVER_RANGE_CALIBRATED 1
#endif
#include "native.c"

// Synthetic-only test ABI. No archived hands or target holdings are loaded.
size_t poker_range_profile_bytes(void) { return 2*sizeof(PokerProfile); }

double poker_range_test_alpha(void) {
#if POKER_RIVER_RANGE_CALIBRATED
    return poker_river_range_alpha;
#else
    return 1;
#endif
}

double poker_range_test_power(double weight) {
#if POKER_RIVER_RANGE_CALIBRATED
    return poker_river_range_weight(weight);
#else
    return weight;
#endif
}

void poker_range_test_probabilities(double q,uint32_t call,uint32_t eligible,
    uint32_t pot,uint32_t actor_stack,uint32_t other_stack,int legal,double* out) {
    PokerRiverResponseContext c=poker_river_response_context(call,eligible,pot,actor_stack,other_stack,legal);
    poker_river_response_predict(&c,q,out);
}

void poker_range_fixture(Poker* g,uint64_t seed,uint32_t s0,uint32_t s1,int kind) {
    poker_seed(g,seed);uint32_t stacks[2]={s0,s1};poker_deal(g,0,seed&1,stacks);
    int street=kind==3?0:kind==4?1:kind==5?2:3;
    while(g->street[0]<street)poker_step(g,0,0);
    g->line[0][0]=(seed*71)&255;g->line[1][0]=(seed*37+17)&255;
    if(kind==1)poker_step(g,0,100+seed%1000);
    if(kind==2) {
        int h=g->actor[0];poker_step(g,0,g->bet[h][0]+g->stack[h][0]);
    }
}

void poker_range_hide_other(Poker* g) {
    int o=1-g->actor[0];g->hole[o][0][0]=255;g->hole[o][1][0]=254;
    g->rng[0]^=UINT64_C(0x87af320148fe);
}

void poker_range_make_unequal_prior(Poker* g) { g->total[1-g->actor[0]][0]++; }

// out=[solver_supported,response_scope,passive_value,raise_value,best_value,samples].
// q holds both990-element opponent reference-equity vectors, row-major.
int poker_range_probe(Poker* g,const PokerProfile* profiles,double* out,double* q) {
    uint64_t rng=5190321;PokerRiverModel ctx;int32_t action=0;
    int ok=poker_river_action_value(g,0,&rng,128,profiles,&action,&ctx);
    memset(out,0,6*sizeof(*out));memset(q,0,1980*sizeof(*q));out[0]=ok;
    if(ok) {
        out[1]=ctx.calibrated;out[2]=ctx.root_passive_value;
        out[3]=ctx.root_raise_value;out[4]=ctx.root_value;out[5]=ctx.samples;
        memcpy(q,ctx.opponent_equity,1980*sizeof(*q));
    }
    assert(rng==5190321);
    return action;
}

static void independent_root(Poker* g,const PokerProfile* profiles,
    PokerRiverModel* ctx,double* weights) {
    const int h=g->actor[0],o=1-h;
    uint64_t known=(UINT64_C(1)<<g->hole[h][0][0])|(UINT64_C(1)<<g->hole[h][1][0]);
    Poker direct={0};direct.street[0]=3;direct.button[0]=g->button[0];direct.line[o][0]=g->line[o][0];
    for(int b=0;b<5;b++) {direct.board[b][0]=g->board[b][0];known|=UINT64_C(1)<<g->board[b][0];}
    int i=0;
    for(int a=0;a<52;a++)if(!(known&(UINT64_C(1)<<a)))
        for(int b=a+1;b<52;b++)if(!(known&(UINT64_C(1)<<b))) {
            // Full street3 weight API is independent of the production cached
            // turn-prior/rank factorization. Apply the coefficient once here.
            double w=poker_range_weight(&direct,0,o,a,b,profiles);
            assert(w>0 && isfinite(w));
#if POKER_RIVER_RANGE_CALIBRATED
            if(ctx->calibrated)w=pow(w,poker_river_range_alpha);
#endif
            weights[i++]=w;
        }
    assert(i==990);
    PokerRiverMoney m={.pot=g->pot[0],.high=g->high[0],.increment=g->increment[0],
        .bet={g->bet[h][0],g->bet[o][0]},.stack={g->stack[h][0],g->stack[o][0]},
        .reopen={g->reopen[h][0],g->reopen[o][0]}};
    poker_river_hero_value(ctx,&m,weights,0,!(g->pending[0]&~(1u<<h)));
}

int main(void) {
    assert(POKER_RIVER_EXACT && POKER_RIVER_CALIBRATED);
    assert(poker_range_test_alpha()>=0 && poker_range_test_alpha()<=1);
    Poker* g=calloc(1,sizeof(*g));PokerProfile profiles[2]={{0}};
    profiles[0].hands=73;profiles[0].vpip=51;profiles[0].pfr=31;
    profiles[1].post_actions=93;profiles[1].post_raises=41;
    unsigned roots=0,pairs=0,privacy=0,rng_cases=0,scope=0,exclusions=0;
    double max_value_error=0;
    for(int i=0;i<24;i++)for(int variant=0;variant<3;variant++)for(int profiled=0;profiled<2;profiled++) {
        uint32_t s0=variant==1?15000:20000,s1=variant==2?15000:20000;
        poker_range_fixture(g,202610189831+i,s0,s1,i&1);
        const PokerProfile* p=profiled?profiles:NULL;
        uint64_t rng=730014+i;PokerRiverModel got;int32_t action;
        assert(poker_river_action_value(g,0,&rng,128,p,&action,&got));
        assert(rng==730014u+i && got.calibrated==(!profiled && variant==0));scope++;
        PokerRiverModel expected=got;double weights[990];
        independent_root(g,p,&expected,weights);pairs+=990;roots++;
        assert(action==expected.root_action);
        const double values[3]={got.root_passive_value,got.root_raise_value,got.root_value};
        const double references[3]={expected.root_passive_value,expected.root_raise_value,expected.root_value};
        for(int k=0;k<3;k++) {
            double error=fabs(values[k]-references[k]);if(error>max_value_error)max_value_error=error;
            assert(error<=1e-12*fmax(1,fabs(references[k])));
        }
        Poker before=*g;uint64_t policy_rng=213987+i,expected_rng=policy_rng;
        for(int j=0;j<3;j++)poker_random(&expected_rng);
        assert(poker_equity_bot(g,0,&policy_rng,128,p)==action && policy_rng==expected_rng);
        assert(!memcmp(g,&before,sizeof(*g)));rng_cases++;
        poker_range_hide_other(g);Poker hidden_before=*g;PokerRiverModel hidden;int32_t hidden_action;
        assert(poker_river_action_value(g,0,&rng,128,p,&hidden_action,&hidden));
        assert(hidden_action==action && hidden.root_passive_value==got.root_passive_value
            && hidden.root_raise_value==got.root_raise_value && hidden.root_value==got.root_value);
        assert(!memcmp(hidden.opponent_equity,got.opponent_equity,sizeof(got.opponent_equity)));
        assert(!memcmp(g,&hidden_before,sizeof(*g)));*g=before;privacy++;
    }
    for(int i=0;i<8;i++)for(int kind=2;kind<6;kind++) {
        poker_range_fixture(g,202610189931+i,20000,20000,kind);
        uint64_t rng=123;int32_t action;assert(!poker_river_action_value(g,0,&rng,128,NULL,&action,NULL));exclusions++;
    }
    poker_range_fixture(g,202610189999,20000,20000,0);poker_range_make_unequal_prior(g);
    uint64_t rng=123;int32_t action;assert(!poker_river_action_value(g,0,&rng,128,NULL,&action,NULL));exclusions++;
    printf("PASS root range calibrated: independent_root_values=%u direct_formula_pairs=%u max_value_error=%.3g privacy=%u rng=%u scope=%u exclusions=%u alpha=%.17g\n",
        roots,pairs,max_value_error,privacy,rng_cases,scope,exclusions,poker_range_test_alpha());
    free(g);
}
