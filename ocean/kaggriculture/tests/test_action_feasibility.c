#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../kaggriculture.h"

static obs_t observations[2][OBS_SIZE];
static unsigned char masks[2][KG_POLICY_ACTION_MASK_SIZE];
static float actions[2][NUM_ATNS], rewards[2], terminals[2];

static Env* fixture(int mode) {
    Env* e = calloc(1, sizeof(*e));
    KGConfig c; kg_config_default(&c); c.weed_spawn_chance = 0;
    kg_init(&e->game_storage, &c);
    e->macro_mode = mode; e->macro_decision_interval = 1;
    e->frozen_macro_mode = e->frozen_macro_executor_version = -1;
    e->frozen_macro_decision_interval = e->frozen_macro_score_features = -1;
    e->observation_version = KAG_OBSERVATION_ENTITIES;
    e->frozen_observation_version = -1;
    e->policy_market_slots = 10; e->policy_max_hands = 16;
    memset(actions, 0, sizeof(actions));
    for (int p = 0; p < 2; p++) {
        e->agents[p].observations = observations[p]; e->agents[p].actions = actions[p];
        e->agents[p].action_mask = masks[p]; e->agents[p].rewards = rewards + p;
        e->agents[p].terminals = terminals + p; e->agents[p].policy = p;
    }
    return e;
}
static void begin(Env* e, KagActionMaskState* s) {
    kag_write_mask(e, 0); kag_action_mask_begin(s, e, 0);
}
static void order(KagActionMaskState* s, int slot, int command, int quantity) {
    int h = KG_POLICY_UNIT_HEADS + 3 * slot;
    kag_action_mask_before(s, h, masks[0]);
    unsigned char* m = kag_market_slot_mask(masks[0], slot);
    assert(m[1] && m[2 + command]);
    kag_action_mask_commit(s, h, 1);
    kag_action_mask_before(s, h + 1, masks[0]);
    kag_action_mask_commit(s, h + 1, command);
    kag_action_mask_before(s, h + 2, masks[0]);
    if (command < KG_POLICY_MARKET_QUANTITY_COMMANDS) {
        int q = kag_market_quantity_id(quantity); assert(m[23 + q]);
        kag_action_mask_commit(s, h + 2, q);
    }
}
static void market_prefix(void) {
    Env* e = fixture(0); KagActionMaskState s;
    KGPlayer* p = &e->game_storage.players[0];
    begin(e, &s); kag_action_mask_before(&s, 17, masks[0]);
    for (int i = 0; i < KG_NUM_PRODUCTS; i++) assert(!masks[0][748 + 2 + KG_M_SELL + i]);
    p->shed[KG_ITEM_WHEAT] = 3; p->money = 0;
    begin(e, &s); order(&s, 0, KG_M_SELL + KG_ITEM_WHEAT, 3);
    kag_action_mask_before(&s, 20, masks[0]);
    assert(!masks[0][779 + 2 + KG_M_SELL + KG_ITEM_WHEAT]);
    assert(s.shed[KG_ITEM_WHEAT] == 0 && s.cash > 0 && p->shed[KG_ITEM_WHEAT] == 3);
    assert(masks[0][779 + 2]); /* Selling can fund a subsequent seed purchase. */
    int price = KG_CROP_DEFS[KG_WHEAT].seed_cost;
    p->shed[KG_ITEM_WHEAT] = 0; p->money = 2 * price;
    begin(e, &s); order(&s, 0, 0, 2);
    assert(masks[0][748 + 23] && masks[0][748 + 24] && !masks[0][748 + 25]);
    kag_action_mask_before(&s, 20, masks[0]);
    assert(!masks[0][779 + 1]); /* No money, stock, or feasible subsequent order. */
    p->shed[KG_ITEM_WHEAT] = e->game_storage.config.shed_capacity;
    p->money = 1000;
    begin(e, &s); order(&s, 0, KG_M_SELL + KG_ITEM_WHEAT, 2);
    kag_action_mask_before(&s, 20, masks[0]);
    assert(masks[0][779 + 2 + KG_M_PRODUCT]); /* Freed shed capacity. */
    e->game_storage.hour = e->game_storage.config.turns_per_day - 1;
    begin(e, &s); kag_action_mask_before(&s, 17, masks[0]);
    assert(!masks[0][748 + 2 + KG_M_HIRE]);
    free(e);
}
static void deposits_and_workers(void) {
    Env* e = fixture(0); KagActionMaskState s;
    KGPlayer* p = &e->game_storage.players[0];
    KGPosition access[4]; kg_shed_access_count(10, access);
    p->units[0].x = access[0].x; p->units[0].y = access[0].y;
    kg_inventory_add(&p->units[0], KG_ITEM_MILK, 4);
    begin(e, &s); kag_action_mask_commit(&s, 0, KG_U_PASS);
    kag_action_mask_before(&s, 17, masks[0]);
    assert(!masks[0][748 + 2 + KG_M_SELL + KG_ITEM_MILK]);
    begin(e, &s); kag_action_mask_commit(&s, 0, KG_U_DROP);
    order(&s, 0, KG_M_SELL + KG_ITEM_MILK, 4);
    assert(s.shed[KG_ITEM_MILK] == 0 && p->shed[KG_ITEM_MILK] == 0);
    assert(p->units[0].inventory[KG_ITEM_MILK] == 4); /* Preview is read-only. */
    p->units[0].x = 0; p->units[0].y = 0;
    kg_new_plant(p, 0, KG_WHEAT, 0, 24);
    kg_inventory_add(&p->units[0], KG_ITEM_FERTILIZER, 2);
    p->tiles[0].fertilized_until_day = 2;
    begin(e, &s);
    int fertilize = kag_unit_action_id(KG_OP_FERTILIZE, -1, 1);
    assert(!masks[0][fertilize]);
    free(e);

    e = fixture(3); p = &e->game_storage.players[0];
    p->hand_count = 2; p->unit_count = 3; p->seeds[KG_WHEAT] = 1;
    begin(e, &s); kag_action_mask_commit(&s, 0, KAG_TASK_PLANT_BASE);
    kag_action_mask_before(&s, 1, masks[0]);
    for (int q = 0; q < 4; q++) assert(!masks[0][44 + KAG_TASK_PLANT_BASE + q]);
    free(e);
}
static void all_controllers(void) {
    float logits[KG_POLICY_ACTION_MASK_SIZE] = {0};
    for (int mode = 0; mode < 4; mode++) for (int executor = 0; executor < 2; executor++) {
        if (executor && (mode == 0 || mode == 3)) continue;
        Env* e = fixture(mode); e->macro_executor_version = executor;
        for (int step = 0; step < 32; step++) {
            for (int p = 0; p < 2; p++) kag_sample_cpu_logits(e, p, logits, 0);
            puf_step(e); assert(isfinite(rewards[0]) && isfinite(rewards[1]));
        }
        /* A per-bank controller travels with its seat, including reset. */
        e->controller[1] = (KagController){1, 2, 1, 1, 0};
        puf_reset(e); assert(kag_agent_macro_mode(e, 1) == 2 && kag_agent_executor_version(e, 1) == 1);
        free(e);
    }
    Env* e = fixture(2); KagActionMaskState s;
    begin(e, &s); kag_action_mask_commit(&s, 0, KAG_MACRO_HOLD);
    for (int head = 1; head <= 2; head++) {
        kag_action_mask_before(&s, head, masks[0]);
        for (int a = 1; a < 44; a++) assert(!masks[0][44 * head + a]);
    }
    free(e);
}
int main(void) {
    market_prefix(); deposits_and_workers(); all_controllers();
    puts("prefix masks: empty/depleted sales, command quantities, cash/space reuse, deposits, upkeep aliases, six controllers PASS");
}
