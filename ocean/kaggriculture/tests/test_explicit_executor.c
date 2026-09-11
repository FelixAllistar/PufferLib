#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../kaggriculture.h"

static void init(Env* e) {
    memset(e,0,sizeof(*e));KGConfig c;kg_config_default(&c);c.weed_spawn_chance=0;
    kg_init(&e->game_storage,&c);e->macro_mode=2;e->macro_executor_version=1;
    e->frozen_macro_executor_version=0;e->policy_max_hands=240;
}
int main(void) {
    Env* e=calloc(1,sizeof(*e));init(e);KGState* g=&e->game_storage;KGPlayer* p=&g->players[0];
    KGAction a;float req[NUM_ATNS]={0};Agent agent={0};agent.actions=req;
    assert(!kag_macro_candidate_legal(e,0,KAG_MACRO_DIVERSIFY));
    e->agents[1].policy=1;assert(kag_macro_candidate_legal(e,1,KAG_MACRO_DIVERSIFY));
    p->seeds[KG_MELON]=1;req[0]=KAG_MACRO_PLANT_BASE+KG_MELON;req[1]=0;req[2]=1;
    kag_decode_macro_action(&a,&agent,g,0,e);assert(a.farmer.op==KG_OP_PLANT && a.farmer.arg==KG_MELON);
    e->macro_executor_version=0;kag_decode_macro_action(&a,&agent,g,0,e);assert(a.farmer.op==KG_OP_WEST);
    init(e);kg_set_player_tile(p,0,KG_TILE_PASTURE);kg_set_player_tile(p,44,KG_TILE_PASTURE);
    kg_inventory_add(&p->units[0],KG_ITEM_COW,1);req[0]=KAG_MACRO_ANIMAL_BASE+KG_COW;
    kag_decode_macro_action(&a,&agent,g,0,e);assert(a.farmer.op==KG_OP_PLACE && a.farmer.arg==KG_ITEM_COW);
    req[0]=KAG_MACRO_DIVERSIFY;kag_decode_macro_action(&a,&agent,g,0,e);
    assert(a.farmer.op==KG_OP_PLACE); /* masked/malformed old ID falls back to operations, not strategy */
    assert(a.market_count==0);
    init(e);kg_set_player_tile(p,44,KG_TILE_PASTURE);req[0]=KAG_EXPLICIT_RECLAIM;
    assert(kag_macro_candidate_legal(e,0,KAG_EXPLICIT_RECLAIM));
    kag_decode_macro_action(&a,&agent,g,0,e);assert(a.farmer.op==KG_OP_DIG);
    kg_inventory_add(&p->units[0],KG_ITEM_COW,1);
    kag_decode_macro_action(&a,&agent,g,0,e);assert(a.farmer.op!=KG_OP_DIG); /* don't destroy reserved housing */
    init(e);p->money=0;req[0]=KAG_MACRO_ANIMAL_BASE+KG_COW;req[1]=7;
    kag_decode_macro_action(&a,&agent,g,0,e);assert(a.farmer.op!=KG_OP_BUILD_PASTURE && !a.market_count);
    init(e);p->seeds[KG_MELON]=1;KGUnitAction plant={KG_OP_PLANT,KG_MELON,1};
    kg_apply_unit_action(g,p,0,&plant);p->tiles[44].watered_today=1;
    kg_inventory_add(&p->units[0],KG_ITEM_FERTILIZER,1);req[0]=KAG_EXPLICIT_FERTILIZE;
    kag_decode_macro_action(&a,&agent,g,0,e);assert(a.farmer.op==KG_OP_FERTILIZE);
    /* Requests cannot spend strategy money through HOLD/MAINTAIN/HARVEST. */
    for(int m=0;m<3;m++) {
        init(e);g->day=9;g->step=216;p->money=10000;
        req[0]=m==0?KAG_MACRO_HOLD:m==1?KAG_MACRO_MAINTAIN:KAG_MACRO_HARVEST;
        kag_decode_macro_action(&a,&agent,g,0,e);assert(a.market_count==0);
    }
    /* Multiple workers, one seed: no duplicate/over-budget plant commands. */
    init(e);kg_do_hire(g,p);kg_set_unit_position(p,1,3,4);p->seeds[KG_MELON]=1;
    req[0]=KAG_MACRO_PLANT_BASE+KG_MELON;req[1]=7;
    kag_decode_macro_action(&a,&agent,g,0,e);
    int n=a.farmer.op==KG_OP_PLANT;for(int u=0;u<a.hand_count;u++)n+=a.hands[u].op==KG_OP_PLANT;
    assert(n==1);int blocked[KG_NUM_CROPS];kg_validate_plant_atomic(&a,p,blocked);assert(!blocked[KG_MELON]);
    /* Hiring is PPO's choice even without the old workload heuristic. */
    init(e);g->step=719;g->day=29;g->hour=23;p->money=10000;
    assert(kag_macro_candidate_legal(e,0,KAG_MACRO_HIRE));
    req[0]=KAG_MACRO_HIRE;req[1]=0;kag_decode_macro_action(&a,&agent,g,0,e);
    assert(a.market_count==1 && a.market[0].op==KG_MARKET_HIRE);
    assert(kag_macro_candidate_legal(e,0,KAG_MACRO_EXPAND));
    init(e);p->money=100000000;
    for(int u=0;u<16;u++)kg_do_hire(g,p);
    assert(p->hand_count==16 && kag_macro_candidate_legal(e,0,KAG_MACRO_HIRE));
    req[0]=KAG_MACRO_HIRE;req[1]=0;kag_decode_macro_action(&a,&agent,g,0,e);
    assert(a.market_count==1 && a.market[0].op==KG_MARKET_HIRE);
    free(e);puts("explicit executor tests passed");
}
