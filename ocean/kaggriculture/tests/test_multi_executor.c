#include <assert.h>
#include <stdio.h>
#include "../kaggriculture.h"
#include "ini.h"
#include "../../../src/kag_observation_contract.h"

static float actions[2][NUM_ATNS], observations[2][OBS_SIZE];
static unsigned char masks[2][KG_POLICY_ACTION_MASK_SIZE];
static Env* fixture(void) {
    Env* e = calloc(1,sizeof(*e));
    KGConfig c; kg_config_default(&c); c.weed_spawn_chance = 0;
    kg_init(&e->game_storage,&c);
    e->macro_mode = 2; e->macro_executor_version = 2; e->macro_decision_interval = 1;
    e->frozen_macro_mode = e->frozen_macro_executor_version = -1;
    e->frozen_macro_decision_interval = e->frozen_macro_score_features = -1;
    e->observation_version = 3; e->frozen_observation_version = -1;
    e->policy_market_slots = 10; e->policy_max_hands = 16;
    memset(actions,0,sizeof(actions));
    for (int p = 0; p < 2; p++) {
        e->agents[p].actions = actions[p]; e->agents[p].action_mask = masks[p];
        e->agents[p].observations = observations[p];
    }
    return e;
}
static KGAction decode(Env* e) {
    KGAction a;
    kag_decode_policy_action(&a,&e->agents[0],&e->game_storage,0,e);
    return a;
}
static void request(int slot, int intent, int n, int region) {
    actions[0][3*slot] = intent; actions[0][3*slot+1] = n-1; actions[0][3*slot+2] = region;
}
static void workers(KGPlayer* p, int n) {
    p->unit_count = n; p->hand_count = n-1;
    for (int u = 0; u < n; u++) {
        memset(&p->units[u],0,sizeof(p->units[u]));
        p->units[u].x = u; p->units[u].y = 0;
    }
}
static void concurrent_work(void) {
    Env* e = fixture(); KGPlayer* p = &e->game_storage.players[0];
    workers(p,4); p->seeds[0] = p->seeds[1] = 2;
    kg_new_plant(p,kg_tile_index(3,0),KG_WHEAT,0,24);
    p->tiles[3].consecutive_unwatered = 1;
    request(0,1,1,1); request(1,2,1,1); request(2,KAG_MULTI_PASTURE,1,1);
    KGAction a = decode(e);
    assert(a.farmer.op == KG_OP_PLANT && a.farmer.arg == 0);
    assert(a.hands[0].op == KG_OP_PLANT && a.hands[0].arg == 1);
    assert(a.hands[1].op == KG_OP_BUILD_PASTURE);
    assert(a.hands[2].op == KG_OP_WATER);
    assert(a.market_count == 0); /* no invented investment */
    p->seeds[0] = 1; request(0,1,3,1); request(1,1,3,1); request(2,0,1,0);
    a = decode(e);
    int plants = a.farmer.op == KG_OP_PLANT;
    for (int u = 0; u < a.hand_count; u++) plants += a.hands[u].op == KG_OP_PLANT;
    assert(plants == 1); /* both requests share the seed budget */
    kag_write_mask(e,0); KagActionMaskState prefix; kag_action_mask_begin(&prefix,e,0);
    kag_action_mask_commit(&prefix,0,1); kag_action_mask_before(&prefix,1,masks[0]);
    assert(masks[0][44] && !masks[0][45]);
    kag_action_mask_commit(&prefix,1,0); kag_action_mask_commit(&prefix,2,1);
    kag_action_mask_before(&prefix,3,masks[0]); assert(!masks[0][3*44+1]);
    free(e);
}
static void markets_and_auto_feed(void) {
    Env* e = fixture(); KGPlayer* p = &e->game_storage.players[0];
    p->money = 10000; p->shed[KG_ITEM_WHEAT] = 14;
    kag_set_policy_market(&e->agents[0],0,KG_MARKET_SELL,KG_ITEM_WHEAT,14);
    kag_set_policy_market(&e->agents[0],1,KG_MARKET_HIRE,-1,1);
    kag_set_policy_market(&e->agents[0],2,KG_MARKET_HIRE,-1,1);
    kag_set_policy_market(&e->agents[0],3,KG_MARKET_BUY_ANIMAL,KG_ITEM_COW,3);
    KGAction a = decode(e);
    assert(a.market_count == 4 && a.market[0].n == 14 && a.market[3].n == 3);
    kag_write_mask(e,0); KagActionMaskState prefix; kag_action_mask_begin(&prefix,e,0);
    for (int h = 0; h < NUM_ATNS; h++) {
        kag_action_mask_before(&prefix,h,masks[0]);
        kag_action_mask_commit(&prefix,h,(int)actions[0][h]);
    }
    assert(prefix.hands == 2 && prefix.hires == 2); /* not double charged */
    KGState expected = e->game_storage;
    KGAction pair[2] = {a,{0}};
    kg_process_market(&expected,pair);
    assert(prefix.cash == expected.players[0].money);
    assert(prefix.shed[KG_ITEM_COW] == expected.players[0].shed[KG_ITEM_COW]);
    memset(actions,0,sizeof(actions));
    KGTile* t = &p->tiles[0]; t->kind = KG_TILE_PASTURE; t->animal = KG_COW; t->fed_today = 0;
    p->shed[KG_ITEM_WHEAT] = 0;
    a = decode(e); assert(a.market_count == 1 && a.market[0].item == KG_ITEM_WHEAT && a.market[0].n == 1);
    kag_set_policy_market(&e->agents[0],0,KG_MARKET_BUY_PRODUCT,KG_ITEM_WHEAT,1);
    a = decode(e); assert(a.market_count == 1); /* explicit feed not duplicated */
    memset(actions,0,sizeof(actions)); actions[0][15] = 1;
    a = decode(e); assert(a.market_count == 0); /* strategic opt-out */
    KGPosition access[4]; kg_shed_access_count(10,access);
    p->units[0].x = access[0].x; p->units[0].y = access[0].y;
    p->shed[KG_ITEM_WHEAT] = 7;
    a = decode(e); assert(a.farmer.op == KG_OP_PICKUP);
    actions[0][15] = 2;
    kag_set_policy_market(&e->agents[0],0,KG_MARKET_SELL,KG_ITEM_WHEAT,7);
    a = decode(e); assert(a.farmer.op != KG_OP_PICKUP && a.market_count == 1);
    kag_write_mask(e,0); kag_action_mask_begin(&prefix,e,0);
    for (int h = 0; h <= 19; h++) {
        kag_action_mask_before(&prefix,h,masks[0]);
        if (h == 19) assert(masks[0][KG_POLICY_UNIT_HEADS*44+2+21+6]);
        kag_action_mask_commit(&prefix,h,(int)actions[0][h]);
    }
    free(e);
}
static void shared_housing(void) {
    Env* e = fixture(); KGPlayer* p = &e->game_storage.players[0];
    workers(p,2);
    p->tiles[0].kind = KG_TILE_PASTURE; p->tiles[0].animal = KG_ANIMAL_INVALID;
    p->shed[KG_ITEM_COW] = p->shed[KG_ITEM_SHEEP] = 1;
    KGPosition access[4]; kg_shed_access_count(10,access);
    for (int u = 0; u < 2; u++) { p->units[u].x = access[0].x; p->units[u].y = access[0].y; }
    KGAction a = decode(e);
    assert((a.farmer.op == KG_OP_PICKUP) + (a.hands[0].op == KG_OP_PICKUP) == 1);
    free(e);
}
static void strategic_fertilizer_precedes_water(void) {
    Env* e = fixture(); KGPlayer* p = &e->game_storage.players[0];
    workers(p,2);
    e->game_storage.day = (KG_CROP_DEFS[KG_WHEAT].max_yield_day+1)/2;
    kg_new_plant(p,0,KG_WHEAT,0,24); kg_new_plant(p,1,KG_WHEAT,0,24);
    p->tiles[0].watered_today = p->tiles[1].watered_today = 0;
    p->tiles[0].fertilized_until_day = e->game_storage.day+1;
    kg_inventory_add(&p->units[0],KG_ITEM_FERTILIZER,1);
    request(0,KAG_MULTI_FERTILIZE,1,1);
    assert(kag_multi_capacity(&e->game_storage,0,KAG_MULTI_FERTILIZE,1) > 0);
    KGAction a = decode(e);
    assert(a.farmer.op == KG_OP_FERTILIZE && a.hands[0].op == KG_OP_WATER);
    kg_apply_unit_action(&e->game_storage,p,0,&a.farmer);
    assert(p->tiles[0].fertilized_until_day == e->game_storage.day+2);
    memset(actions,0,sizeof(actions));
    a = decode(e); assert(a.farmer.op == KG_OP_WATER);
    int before = p->tiles[0].yield_units;
    kg_apply_unit_action(&e->game_storage,p,0,&a.farmer);
    int expected = before+2;
    if (expected > KG_CROP_DEFS[KG_WHEAT].max_yield) expected = KG_CROP_DEFS[KG_WHEAT].max_yield;
    assert(p->tiles[0].yield_units == expected);
    free(e);
}
static void strategic_harvest_and_wait(void) {
    Env* e = fixture(); KGPlayer* p = &e->game_storage.players[0];
    workers(p,2); e->game_storage.day = KG_CROP_DEFS[KG_WHEAT].first_yield_day;
    kg_new_plant(p,0,KG_WHEAT,0,24); kg_new_plant(p,1,KG_MELON,0,24);
    p->tiles[0].watered_today = p->tiles[1].watered_today = 1;
    p->tiles[0].yield_units = p->tiles[1].yield_units = 2;
    request(0,KAG_MULTI_HARVEST_CROP+KG_WHEAT,1,1);
    KGAction a = decode(e);
    assert(a.farmer.op == KG_OP_HARVEST && a.hands[0].op != KG_OP_HARVEST);
    memset(actions,0,sizeof(actions));
    e->game_storage.day = KG_CROP_DEFS[KG_WHEAT].max_yield_day;
    a = decode(e); assert(a.farmer.op == KG_OP_HARVEST);
    actions[0][KAG_MULTI_HARVEST_HEAD] = 1;
    a = decode(e); assert(a.farmer.op != KG_OP_HARVEST);
    request(0,KAG_MULTI_HARVEST_CROP+KG_WHEAT,1,1);
    a = decode(e); assert(a.farmer.op == KG_OP_HARVEST); /* explicit still works */
    free(e);
}
static void delivery_and_safe_overflow(void) {
    Env* e = fixture(); KGPlayer* p = &e->game_storage.players[0];
    workers(p,2); KGPosition access[4]; kg_shed_access_count(10,access);
    for (int u = 0; u < 2; u++) {
        p->units[u].x = access[0].x; p->units[u].y = access[0].y;
        kg_inventory_add(&p->units[u],KG_ITEM_WHEAT,7);
    }
    memset(p->shed,0,sizeof(p->shed));
    request(0,KAG_MULTI_DELIVER_ALL,2,1);
    kag_set_policy_market(&e->agents[0],0,KG_MARKET_SELL,KG_ITEM_WHEAT,14);
    KGAction a = decode(e);
    assert(a.farmer.op == KG_OP_DROP && a.hands[0].op == KG_OP_DROP);
    kag_write_mask(e,0); KagActionMaskState prefix; kag_action_mask_begin(&prefix,e,0);
    for (int h = 0; h < NUM_ATNS; h++) {
        kag_action_mask_before(&prefix,h,masks[0]);
        if (h == 19) assert(masks[0][KG_POLICY_UNIT_HEADS*44+2+21+13]);
        kag_action_mask_commit(&prefix,h,(int)actions[0][h]);
    }
    KGState copy = e->game_storage; KGPlayer* fp = &copy.players[0];
    kg_apply_unit_action(&copy,fp,0,&a.farmer); kg_apply_unit_action(&copy,fp,1,&a.hands[0]);
    KGAction pair[2] = {a,{0}}; kg_process_market(&copy,pair);
    assert(prefix.cash == fp->money && prefix.shed[KG_ITEM_WHEAT] == fp->shed[KG_ITEM_WHEAT]);
    memset(actions,0,sizeof(actions)); request(0,KAG_MULTI_DELIVER_ALL,2,1);
    p->shed[KG_ITEM_WHEAT] = e->game_storage.config.shed_capacity-3;
    a = decode(e);
    assert(a.farmer.op == KG_OP_PLACE && a.farmer.arg == KG_ITEM_WHEAT && a.farmer.n == 3);
    assert(a.hands[0].op != KG_OP_DROP && a.hands[0].op != KG_OP_PLACE);
    kg_apply_unit_action(&e->game_storage,p,0,&a.farmer);
    assert(p->units[0].inventory[KG_ITEM_WHEAT] == 4); /* never discard excess cargo */
    free(e);
}
static void scarce_fertilizer_and_unfed_chores(void) {
    Env* e = fixture(); KGPlayer* p = &e->game_storage.players[0];
    workers(p,2); e->game_storage.day = 3;
    kg_new_plant(p,0,KG_TOMATO,0,24); kg_new_plant(p,1,KG_STRAWBERRY,0,24);
    kg_inventory_add(&p->units[0],KG_ITEM_FERTILIZER,1);
    request(0,KAG_MULTI_CLEAR,1,1); request(1,KAG_MULTI_FERTILIZE,1,1);
    KGAction a = decode(e);
    assert(a.farmer.op == KG_OP_FERTILIZE && a.hands[0].op == KG_OP_DIG);
    memset(actions,0,sizeof(actions));
    p->tiles[0].kind = KG_TILE_PASTURE; p->tiles[0].animal = KG_COW;
    p->tiles[0].fed_today = 0; p->tiles[0].yield_units = 0;
    p->tiles[0].cared_today = 0; p->tiles[0].fertilizer_available = 1;
    a = decode(e); assert(a.farmer.op == KG_OP_CARE); /* no feed in inventory is not a deadlock */
    p->tiles[0].cared_today = 1;
    a = decode(e); assert(a.farmer.op == KG_OP_COLLECT_FERTILIZER);
    free(e);
}
static void rollout(void) {
    Env* e = fixture(); float logits[KG_POLICY_ACTION_MASK_SIZE] = {0};
    for (int step = 0; step < 719; step++) {
        KGAction pair[2];
        for (int p = 0; p < 2; p++) {
            kag_sample_cpu_logits(e,p,logits,0);
            int offset = 0;
            for (int h = 0; h < NUM_ATNS; h++) {
                int active = 1;
                if (h >= 17) {
                    int slot = (h-17)/3, node = (h-17)%3;
                    for (int prev = 0; prev < slot; prev++) active &= actions[p][17+3*prev] == 1;
                    if (node) active &= actions[p][17+3*slot] == 1;
                    if (node == 2) active &= actions[p][h-1] < 19;
                }
                assert(!active || masks[p][offset+(int)actions[p][h]]);
                offset += KG_ACTION_SIZES[h];
            }
            kag_decode_policy_action(&pair[p],&e->agents[p],&e->game_storage,p,e);
            assert(pair[p].market_count <= 10);
        }
        kg_step(&e->game_storage,pair);
    }
    assert(kg_done(&e->game_storage)); free(e);
}
int main(void) {
    assert(KAG_POLICY_VERSION == 5 && KAG_ALL_LOGITS == 1978);
    assert(kag_controller_valid(2,2) && !kag_controller_valid(3,2) && !kag_controller_valid(1,2));
    for (int n = 1; n <= 100; n++) assert(kag_market_quantity_spec(kag_market_quantity_id(n)) == n);
    concurrent_work(); markets_and_auto_feed(); shared_housing();
    strategic_fertilizer_precedes_water(); strategic_harvest_and_wait();
    delivery_and_safe_overflow(); scarce_fertilizer_and_unfed_chores(); rollout();
    puts("multi executor: concurrent strategy/chores, shared seeds, exact ordered market, feed controls, full rollout PASS");
}
