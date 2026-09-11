#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../kaggriculture.h"

int main(void) {
    Env* e=calloc(1,sizeof(*e)); assert(e);
    KGConfig cfg; kg_config_default(&cfg); kg_init(&e->game_storage,&cfg);
    KGState* g=&e->game_storage; KGPlayer* p=&g->players[0];
    e->reward_expansion_scale=1; e->reward_expansion_deadline=720;
    e->reward_expansion_land_target=3; e->reward_expansion_plant_target=12;
    e->reward_expansion_animal_target=6;
    kag_reset_expansion_peaks(e,0);
    for(int i=0;i<12;i++) kg_set_player_tile(p,(i/5)*10+i%5,KG_TILE_PASTURE);
    assert(kag_live_tiles(p,1)==0);
    assert(kag_expansion_reward(e,0)==0);
    assert(kag_player_potential(e,0)==cfg.starting_money);
    assert(kag_player_progress_value(e,0)==cfg.starting_money);
    kg_new_animal(p,0,KG_COW,0);
    assert(kag_live_tiles(p,1)==1 && kag_expansion_reward(e,0)>0);
    assert(kag_expansion_reward(e,0)==0); /* no repeated achievement reward */
    kg_set_player_tile(p,0,KG_TILE_PASTURE);
    assert(kag_live_tiles(p,1)==0 && kag_expansion_reward(e,0)==0);
    kag_reset_expansion_peaks(e,0); assert(e->expansion_peak_animals[0]==0);

    /* Exact market accounting: purchases exclude hires/land by definition. */
    kg_reset(g); p->money=20000;
    int initial=p->money, hires=0, land=0;
    for(int i=0;i<6;i++) {int before=p->money; kg_do_hire(g,p);hires+=before-p->money;}
    assert(hires==20);
    for(int i=0;i<3;i++) {int before=p->money;kg_do_buy_land(g,p);land+=before-p->money;}
    assert(land==7000);
    assert(kg_commit_unit(g,0,KG_MARKET_BUY_SEED,KG_WHEAT,10));
    assert(kg_commit_unit(g,0,KG_MARKET_BUY_ANIMAL,KG_ITEM_COW,400));
    assert(kg_commit_unit(g,0,KG_MARKET_BUY_PRODUCT,KG_ITEM_WHEAT,200));
    assert(kg_commit_unit(g,0,KG_MARKET_SELL,KG_ITEM_WHEAT,199));
    assert(!kg_commit_unit(g,0,KG_MARKET_SELL,KG_ITEM_WHEAT,199));
    assert(g->purchase_spend[0]==610 && g->sales_revenue[0]==199);
    assert(p->money==initial+199-610-hires-land);
    kg_reset(g);
    KGAction actions[KG_NUM_PLAYERS]={0};
    actions[0].market_count=1;
    actions[0].market[0]=(KGMarketOrder){KG_MARKET_BUY_SEED,KG_WHEAT,1000};
    kg_process_market(g,actions);
    assert(p->money==0 && p->seeds[KG_WHEAT]==300);
    assert(g->bought_units[0]==300 && g->purchase_spend[0]==3000);
    /* Requested but unaffordable units never enter the accounting ledger. */
    kg_process_market(g,actions);
    assert(g->bought_units[0]==300 && g->purchase_spend[0]==3000);

    /* Progress shaping cancels at terminal; cash shaping intentionally does not. */
    e->reward_progress_scale=2; e->reward_potential_gamma=.9997f;
    double gamma=e->reward_potential_gamma, sum=0;
    float v[]={3000,4800,2400,9000};
    for(int t=0;t<3;t++) sum+=pow(gamma,t)*kag_progress_potential_reward(e,v[t],v[t+1],t==2);
    assert(fabs(sum)<1e-6);
    e->reward_money_scale=1; e->reward_progress_terminal_money_scale=4;
    assert(fabs(kag_terminal_money_reward(e,6000)+kag_progress_terminal_money_reward(e,6000)-5)<1e-6);
    e->reward_progress_win_scale=2;
    assert(kag_positive_terminal_win_reward(e,6000,6000)==1);
    assert(kag_positive_terminal_win_reward(e,5000,6000)==0);
    free(e); puts("reward occupancy/accounting tests passed");
}
