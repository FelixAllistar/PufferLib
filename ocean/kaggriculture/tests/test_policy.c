#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef KAG_REFERENCE
#include KAG_REFERENCE
typedef Env TestEnv;
#else
#include "../policy.h"
#define NUM_ATNS KAG_ACTION_HEADS
#define OBS_SIZE KAG_ENTITY_OBS_SIZE
static const int KG_ACTION_SIZES[NUM_ATNS] = KAG_ACTION_SIZES;
typedef struct {
    KGState game_storage;
    KagPolicy policy;
    unsigned int rng;
} TestEnv;
#endif

static float actions[2][NUM_ATNS], observations[2][OBS_SIZE];
static unsigned char masks[2][KG_POLICY_ACTION_MASK_SIZE];

static void configure(TestEnv* e, int slots, int hands, int delay) {
#ifdef KAG_REFERENCE
    e->macro_mode = 2;
    e->macro_executor_version = 2;
    e->macro_decision_interval = 1;
    e->frozen_macro_mode = e->frozen_macro_executor_version = -1;
    e->frozen_macro_decision_interval = e->frozen_macro_score_features = -1;
    e->observation_version = 3;
    e->frozen_observation_version = -1;
    e->policy_market_slots = slots;
    e->policy_max_hands = hands;
    e->land_buy_min_days = delay;
    e->reward.target_plots = 4;
    e->reward.target_crops = -1;
    e->reward.target_animals = 8;
    for (int p = 0; p < 2; p++) {
        e->agents[p].actions = actions[p];
        e->agents[p].action_mask = masks[p];
        e->agents[p].observations = observations[p];
    }
#else
    e->policy.market_slots = slots;
    e->policy.max_hands = hands;
    e->policy.land_buy_min_days = delay;
#endif
}

static void reset_policy(TestEnv* e, int source) {
    memset(actions, 0, sizeof(actions));
#ifdef KAG_REFERENCE
    e->reset_source = source;
    for (int p = 0; p < 2; p++) {
        e->macro_intent[p] = e->macro_ticks[p] = e->macro_quantity[p] = e->macro_target[p] = 0;
        kag_reward_reset(e, p);
        kag_reset_land_buy_delay(e, p);
    }
#else
    kag_policy_reset(&e->policy, &e->game_storage, source);
#endif
}

static TestEnv* fixture(void) {
    TestEnv* e = (TestEnv*)calloc(1, sizeof(*e));
    assert(e);
    KGConfig c;
    kg_config_default(&c);
    c.weed_spawn_chance = 0;
    kg_init(&e->game_storage, &c);
    configure(e, 10, 16, 0);
    reset_policy(e, 0);
    return e;
}

static void write_mask(TestEnv* e, int p) {
#ifdef KAG_REFERENCE
    kag_write_mask(e, p);
#else
    kag_write_mask(&e->policy, &e->game_storage, p, masks[p]);
#endif
}

static void observe(TestEnv* e, int p) {
#ifdef KAG_REFERENCE
    kag_write_observation(e, p);
#else
    kag_write_observation(&e->policy, &e->game_storage, p, observations[p]);
#endif
}

static void begin_prefix(KagActionMaskState* s, TestEnv* e, int p) {
#ifdef KAG_REFERENCE
    kag_action_mask_begin(s, e, p);
#else
    kag_action_mask_begin(s, &e->game_storage, &e->policy, p);
#endif
}

static KGAction decode_player(TestEnv* e, int p) {
    KGAction a;
#ifdef KAG_REFERENCE
    kag_decode_policy_action(&a, &e->agents[p], &e->game_storage, p, e);
#else
    kag_decode_multi_action(&a, actions[p], &e->game_storage, p, &e->policy);
#endif
    return a;
}

static KGAction decode(TestEnv* e) {
    return decode_player(e, 0);
}

static void sample(TestEnv* e, int p, const float* logits, int deterministic) {
#ifdef KAG_REFERENCE
    kag_sample_cpu_logits(e, p, logits, deterministic);
#else
    kag_sample_cpu_logits(
        &e->policy, &e->game_storage, p, logits, deterministic, &e->rng, actions[p], masks[p]);
#endif
}

static void step_policy(TestEnv* e) {
#ifdef KAG_REFERENCE
    for (int p = 0; p < 2; p++) {
        kag_reward_step(e, p, kg_done(&e->game_storage));
    }
#else
    kag_policy_step(&e->policy, &e->game_storage);
#endif
}

static void market(int slot, int op, int item, int n) {
    int h = KG_POLICY_MARKET_HEAD_OFFSET + 3 * slot;
    int id = -1;
    for (int i = 0; i < KG_POLICY_MARKET_COMMANDS; i++) {
        KGPolicyMarketSpec spec = kag_market_spec(i);
        if (spec.op == op && spec.item == item) {
            id = i;
        }
    }
    assert(id >= 0);
    actions[0][h] = 1;
    actions[0][h + 1] = id;
    actions[0][h + 2] = kag_market_quantity_id(n);
}

static void request(int slot, int intent, int n, int region) {
    actions[0][3 * slot] = intent;
    actions[0][3 * slot + 1] = n - 1;
    actions[0][3 * slot + 2] = region;
}
static void workers(KGPlayer* p, int n) {
    p->unit_count = n;
    p->hand_count = n - 1;
    for (int u = 0; u < n; u++) {
        memset(&p->units[u], 0, sizeof(p->units[u]));
        p->units[u].x = u;
        p->units[u].y = 0;
    }
}
static void concurrent_work(void) {
    TestEnv* e = fixture();
    KGPlayer* p = &e->game_storage.players[0];
    workers(p, 4);
    p->seeds[0] = p->seeds[1] = 2;
    kg_new_plant(p, kg_tile_index(3, 0), KG_WHEAT, 0, 24);
    p->tiles[3].consecutive_unwatered = 1;
    request(0, 1, 1, 1);
    request(1, 2, 1, 1);
    request(2, KAG_MULTI_PASTURE, 1, 1);
    KGAction a = decode(e);
    assert(a.farmer.op == KG_OP_PLANT && a.farmer.arg == 0);
    assert(a.hands[0].op == KG_OP_PLANT && a.hands[0].arg == 1);
    assert(a.hands[1].op == KG_OP_BUILD_PASTURE);
    assert(a.hands[2].op == KG_OP_WATER);
    assert(a.market_count == 0); /* no invented investment */
    p->seeds[0] = 1;
    request(0, 1, 3, 1);
    request(1, 1, 3, 1);
    request(2, 0, 1, 0);
    a = decode(e);
    int plants = a.farmer.op == KG_OP_PLANT;
    for (int u = 0; u < a.hand_count; u++) {
        plants += a.hands[u].op == KG_OP_PLANT;
    }
    assert(plants == 1); /* both requests share the seed budget */
    write_mask(e, 0);
    KagActionMaskState prefix;
    begin_prefix(&prefix, e, 0);
    kag_action_mask_commit(&prefix, 0, 1);
    kag_action_mask_before(&prefix, 1, masks[0]);
    assert(masks[0][44] && !masks[0][45]);
    kag_action_mask_commit(&prefix, 1, 0);
    kag_action_mask_commit(&prefix, 2, 1);
    kag_action_mask_before(&prefix, 3, masks[0]);
    assert(!masks[0][3 * 44 + 1]);
    free(e);
}
static void markets_and_auto_feed(void) {
    TestEnv* e = fixture();
    KGPlayer* p = &e->game_storage.players[0];
    p->money = 10000;
    p->shed[KG_ITEM_WHEAT] = 14;
    market(0, KG_MARKET_SELL, KG_ITEM_WHEAT, 14);
    market(1, KG_MARKET_HIRE, -1, 1);
    market(2, KG_MARKET_HIRE, -1, 1);
    market(3, KG_MARKET_BUY_ANIMAL, KG_ITEM_COW, 3);
    KGAction a = decode(e);
    assert(a.market_count == 4 && a.market[0].n == 14 && a.market[3].n == 3);
    write_mask(e, 0);
    KagActionMaskState prefix;
    begin_prefix(&prefix, e, 0);
    for (int h = 0; h < NUM_ATNS; h++) {
        kag_action_mask_before(&prefix, h, masks[0]);
        kag_action_mask_commit(&prefix, h, (int)actions[0][h]);
    }
    assert(prefix.hands == 2 && prefix.hires == 2); /* not double charged */
    KGState expected = e->game_storage;
    KGAction pair[2] = {0};
    pair[0] = a;
    kg_process_market(&expected, pair);
    assert(prefix.cash == expected.players[0].money);
    assert(prefix.shed[KG_ITEM_COW] == expected.players[0].shed[KG_ITEM_COW]);
    memset(actions, 0, sizeof(actions));
    KGTile* t = &p->tiles[0];
    t->kind = KG_TILE_PASTURE;
    t->animal = KG_COW;
    t->fed_today = 0;
    p->shed[KG_ITEM_WHEAT] = 0;
    a = decode(e);
    assert(a.market_count == 1 && a.market[0].item == KG_ITEM_WHEAT && a.market[0].n == 1);
    market(0, KG_MARKET_BUY_PRODUCT, KG_ITEM_WHEAT, 1);
    a = decode(e);
    assert(a.market_count == 1); /* explicit feed not duplicated */
    memset(actions, 0, sizeof(actions));
    actions[0][15] = 1;
    a = decode(e);
    assert(a.market_count == 0); /* strategic opt-out */
    KGPosition access[4];
    kg_shed_access_count(10, access);
    p->units[0].x = access[0].x;
    p->units[0].y = access[0].y;
    p->shed[KG_ITEM_WHEAT] = 7;
    a = decode(e);
    assert(a.farmer.op == KG_OP_PICKUP);
    actions[0][15] = 2;
    market(0, KG_MARKET_SELL, KG_ITEM_WHEAT, 7);
    a = decode(e);
    assert(a.farmer.op != KG_OP_PICKUP && a.market_count == 1);
    write_mask(e, 0);
    begin_prefix(&prefix, e, 0);
    for (int h = 0; h <= 19; h++) {
        kag_action_mask_before(&prefix, h, masks[0]);
        if (h == 19) {
            assert(masks[0][KG_POLICY_UNIT_HEADS * 44 + 2 + 21 + 6]);
        }
        kag_action_mask_commit(&prefix, h, (int)actions[0][h]);
    }
    free(e);
}
static void shared_housing(void) {
    TestEnv* e = fixture();
    KGPlayer* p = &e->game_storage.players[0];
    workers(p, 2);
    p->tiles[0].kind = KG_TILE_PASTURE;
    p->tiles[0].animal = KG_ANIMAL_INVALID;
    p->shed[KG_ITEM_COW] = p->shed[KG_ITEM_SHEEP] = 1;
    KGPosition access[4];
    kg_shed_access_count(10, access);
    for (int u = 0; u < 2; u++) {
        p->units[u].x = access[0].x;
        p->units[u].y = access[0].y;
    }
    KGAction a = decode(e);
    assert((a.farmer.op == KG_OP_PICKUP) + (a.hands[0].op == KG_OP_PICKUP) == 1);
    free(e);
}
static void strategic_fertilizer_precedes_water(void) {
    TestEnv* e = fixture();
    KGPlayer* p = &e->game_storage.players[0];
    workers(p, 2);
    e->game_storage.day = (KG_CROP_DEFS[KG_WHEAT].max_yield_day + 1) / 2;
    kg_new_plant(p, 0, KG_WHEAT, 0, 24);
    kg_new_plant(p, 1, KG_WHEAT, 0, 24);
    p->tiles[0].watered_today = p->tiles[1].watered_today = 0;
    p->tiles[0].fertilized_until_day = e->game_storage.day + 1;
    kg_inventory_add(&p->units[0], KG_ITEM_FERTILIZER, 1);
    request(0, KAG_MULTI_FERTILIZE, 1, 1);
    assert(kag_multi_capacity(&e->game_storage, 0, KAG_MULTI_FERTILIZE, 1) > 0);
    KGAction a = decode(e);
    assert(a.farmer.op == KG_OP_FERTILIZE && a.hands[0].op == KG_OP_WATER);
    kg_apply_unit_action(&e->game_storage, p, 0, &a.farmer);
    assert(p->tiles[0].fertilized_until_day == e->game_storage.day + 2);
    memset(actions, 0, sizeof(actions));
    a = decode(e);
    assert(a.farmer.op == KG_OP_WATER);
    int before = p->tiles[0].yield_units;
    kg_apply_unit_action(&e->game_storage, p, 0, &a.farmer);
    int expected = before + 2;
    if (expected > KG_CROP_DEFS[KG_WHEAT].max_yield) {
        expected = KG_CROP_DEFS[KG_WHEAT].max_yield;
    }
    assert(p->tiles[0].yield_units == expected);
    free(e);
}
static void strategic_harvest_and_wait(void) {
    TestEnv* e = fixture();
    KGPlayer* p = &e->game_storage.players[0];
    workers(p, 2);
    e->game_storage.day = KG_CROP_DEFS[KG_WHEAT].first_yield_day;
    kg_new_plant(p, 0, KG_WHEAT, 0, 24);
    kg_new_plant(p, 1, KG_MELON, 0, 24);
    p->tiles[0].watered_today = p->tiles[1].watered_today = 1;
    p->tiles[0].yield_units = p->tiles[1].yield_units = 2;
    request(0, KAG_MULTI_HARVEST_CROP + KG_WHEAT, 1, 1);
    KGAction a = decode(e);
    assert(a.farmer.op == KG_OP_HARVEST && a.hands[0].op != KG_OP_HARVEST);
    memset(actions, 0, sizeof(actions));
    e->game_storage.day = KG_CROP_DEFS[KG_WHEAT].max_yield_day;
    a = decode(e);
    assert(a.farmer.op == KG_OP_HARVEST);
    actions[0][KAG_MULTI_HARVEST_HEAD] = 1;
    a = decode(e);
    assert(a.farmer.op != KG_OP_HARVEST);
    request(0, KAG_MULTI_HARVEST_CROP + KG_WHEAT, 1, 1);
    a = decode(e);
    assert(a.farmer.op == KG_OP_HARVEST); /* explicit still works */
    free(e);
}
static void delivery_and_safe_overflow(void) {
    TestEnv* e = fixture();
    KGPlayer* p = &e->game_storage.players[0];
    workers(p, 2);
    KGPosition access[4];
    kg_shed_access_count(10, access);
    for (int u = 0; u < 2; u++) {
        p->units[u].x = access[0].x;
        p->units[u].y = access[0].y;
        kg_inventory_add(&p->units[u], KG_ITEM_WHEAT, 7);
    }
    memset(p->shed, 0, sizeof(p->shed));
    request(0, KAG_MULTI_DELIVER_ALL, 2, 1);
    market(0, KG_MARKET_SELL, KG_ITEM_WHEAT, 14);
    KGAction a = decode(e);
    assert(a.farmer.op == KG_OP_DROP && a.hands[0].op == KG_OP_DROP);
    write_mask(e, 0);
    KagActionMaskState prefix;
    begin_prefix(&prefix, e, 0);
    for (int h = 0; h < NUM_ATNS; h++) {
        kag_action_mask_before(&prefix, h, masks[0]);
        if (h == 19) {
            assert(masks[0][KG_POLICY_UNIT_HEADS * 44 + 2 + 21 + 13]);
        }
        kag_action_mask_commit(&prefix, h, (int)actions[0][h]);
    }
    KGState copy = e->game_storage;
    KGPlayer* fp = &copy.players[0];
    kg_apply_unit_action(&copy, fp, 0, &a.farmer);
    kg_apply_unit_action(&copy, fp, 1, &a.hands[0]);
    KGAction pair[2] = {0};
    pair[0] = a;
    kg_process_market(&copy, pair);
    assert(prefix.cash == fp->money && prefix.shed[KG_ITEM_WHEAT] == fp->shed[KG_ITEM_WHEAT]);
    memset(actions, 0, sizeof(actions));
    request(0, KAG_MULTI_DELIVER_ALL, 2, 1);
    p->shed[KG_ITEM_WHEAT] = e->game_storage.config.shed_capacity - 3;
    a = decode(e);
    assert(a.farmer.op == KG_OP_PLACE && a.farmer.arg == KG_ITEM_WHEAT && a.farmer.n == 3);
    assert(a.hands[0].op != KG_OP_DROP && a.hands[0].op != KG_OP_PLACE);
    kg_apply_unit_action(&e->game_storage, p, 0, &a.farmer);
    assert(p->units[0].inventory[KG_ITEM_WHEAT] == 4); /* never discard excess cargo */
    free(e);
}
static void scarce_fertilizer_and_unfed_chores(void) {
    TestEnv* e = fixture();
    KGPlayer* p = &e->game_storage.players[0];
    workers(p, 2);
    e->game_storage.day = 3;
    kg_new_plant(p, 0, KG_TOMATO, 0, 24);
    kg_new_plant(p, 1, KG_STRAWBERRY, 0, 24);
    kg_inventory_add(&p->units[0], KG_ITEM_FERTILIZER, 1);
    request(0, KAG_MULTI_CLEAR, 1, 1);
    request(1, KAG_MULTI_FERTILIZE, 1, 1);
    KGAction a = decode(e);
    assert(a.farmer.op == KG_OP_FERTILIZE && a.hands[0].op == KG_OP_DIG);
    memset(actions, 0, sizeof(actions));
    p->tiles[0].kind = KG_TILE_PASTURE;
    p->tiles[0].animal = KG_COW;
    p->tiles[0].fed_today = 0;
    p->tiles[0].yield_units = 0;
    p->tiles[0].cared_today = 0;
    p->tiles[0].fertilizer_available = 1;
    a = decode(e);
    assert(a.farmer.op == KG_OP_CARE); /* no feed in inventory is not a deadlock */
    p->tiles[0].cared_today = 1;
    a = decode(e);
    assert(a.farmer.op == KG_OP_COLLECT_FERTILIZER);
    free(e);
}
static void rollout(void) {
    TestEnv* e = fixture();
    float logits[KG_POLICY_ACTION_MASK_SIZE] = {0};
    for (int step = 0; step < 719; step++) {
        KGAction pair[2];
        for (int p = 0; p < 2; p++) {
            sample(e, p, logits, 0);
            int offset = 0;
            for (int h = 0; h < NUM_ATNS; h++) {
                int active = 1;
                if (h >= 17) {
                    int slot = (h - 17) / 3, node = (h - 17) % 3;
                    for (int prev = 0; prev < slot; prev++) {
                        active &= actions[p][17 + 3 * prev] == 1;
                    }
                    if (node) {
                        active &= actions[p][17 + 3 * slot] == 1;
                    }
                    if (node == 2) {
                        active &= actions[p][h - 1] < 19;
                    }
                }
                assert(!active || masks[p][offset + (int)actions[p][h]]);
                offset += KG_ACTION_SIZES[h];
            }
            pair[p] = decode_player(e, p);
            assert(pair[p].market_count <= 10);
        }
        kg_step(&e->game_storage, pair);
    }
    assert(kg_done(&e->game_storage));
    free(e);
}

static void observations_and_history(void) {
    TestEnv* e = fixture();
    KGState* g = &e->game_storage;
    KGPlayer* p = &g->players[0];
    p->money = 123456789;
    g->step = 240;
    g->day = 10;
    reset_policy(e, 1);
    write_mask(e, 0);
    observe(e, 0);
    assert(observations[0][31] == 1 && observations[0][32] == 0);
    assert(observations[0][33] == p->money / 100000.0f);
    assert(observations[0][57] == 240 / 720.0f);
    for (int i = 0; i < 4; i++) {
        assert(observations[0][120 + i] == ((uint32_t)p->money >> (8 * i) & 255u) / 256.0f);
    }
    kg_new_plant(p, 0, KG_STRAWBERRY, g->day, g->config.turns_per_day);
    p->tiles[0].watered_today = 1;
    g->step++;
    g->hour++;
    step_policy(e);
    observe(e, 0);
    assert(observations[0][32] == 1 / 720.0f);
    assert(observations[0][34] > 0 && observations[0][KAG_TASK_OFFSET + 51] == 1 / 100.0f);
    float cached[OBS_SIZE];
    memcpy(cached, observations[0], sizeof(cached));
    observe(e, 0);
    assert(!memcmp(cached, observations[0], sizeof(cached)));
    for (int i = 0; i < KG_NUM_PRODUCTS; i++) {
#ifdef KAG_REFERENCE
        e->quote_cache[i].valid = 0;
#else
        e->policy.quote_cache[i].valid = 0;
#endif
    }
    observe(e, 0);
    assert(!memcmp(cached, observations[0], sizeof(cached)));
    g->market.inventory[KG_ITEM_WHEAT] -= 30;
    kg_refresh_prices(g);
    observe(e, 0);
    assert(memcmp(cached, observations[0], sizeof(cached)));
    reset_policy(e, 0);
    observe(e, 0);
    assert(observations[0][31] == 0 && observations[0][32] == 0 && observations[0][34] == 0);
    free(e);
}

static void land_gate_and_queue_limits(void) {
    TestEnv* e = fixture();
    KGState* g = &e->game_storage;
    KGPlayer* p = &g->players[0];
    configure(e, 2, 1, 2);
    p->money = 50000;
    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 5; x++) {
            kg_new_plant(p, kg_tile_index(x, y), KG_STRAWBERRY, 0, 24);
        }
    }
    write_mask(e, 0);
    KagActionMaskState s;
    begin_prefix(&s, e, 0);
    kag_action_mask_before(&s, 17, masks[0]);
    assert(!masks[0][KG_POLICY_MARKET_MASK_OFFSET + 2 + KG_M_LAND]);
    g->step = 48;
    g->day = 2;
    write_mask(e, 0);
    begin_prefix(&s, e, 0);
    kag_action_mask_before(&s, 17, masks[0]);
    assert(masks[0][KG_POLICY_MARKET_MASK_OFFSET + 2 + KG_M_LAND]);
    market(0, KG_MARKET_HIRE, -1, 1);
    market(1, KG_MARKET_HIRE, -1, 1);
    market(2, KG_MARKET_BUY_LAND, -1, 1);
    KGAction a = decode(e);
    assert(a.market_count == 1 && a.market[0].op == KG_MARKET_HIRE);
    free(e);
}

static uint64_t hash_bytes(const void* data, size_t size) {
    const unsigned char* bytes = (const unsigned char*)data;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < size; i++) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return hash;
}

static void rich_state(KGState* g, int index) {
    g->step = index % 8 == 7 ? 716 : (index * 37) % 700;
    g->day = g->step / g->config.turns_per_day;
    g->hour = g->step % g->config.turns_per_day;
    for (int pid = 0; pid < 2; pid++) {
        KGPlayer* p = &g->players[pid];
        p->money = 1000000;
        for (int q = 0; q < index % 4; q++) {
            kg_do_buy_land(g, p);
        }
        int n = 1 + (index * 3 + pid) % 17;
        for (int u = 1; u < n; u++) {
            kg_do_hire(g, p);
        }
        p->money = index % 5 == 0 ? 0 : index % 3 == 0 ? 301 : 80000;
        for (int c = 0; c < KG_NUM_CROPS; c++) {
            p->seeds[c] = (index + c) % 7;
        }
        for (int t = 0; t < KG_MAX_TILES; t++) {
            if (p->tiles[t].kind == KG_TILE_LOCKED) {
                continue;
            }
            int kind = (t + index + pid) % 12;
            if (kind < 5) {
                kg_new_plant(p, t, kind, g->day - 8, g->config.turns_per_day);
                p->tiles[t].watered_today = (t + index) % 2;
                p->tiles[t].yield_units = (t + index) % 4;
                p->tiles[t].fertilized_until_day = g->day - 1 + t % 4;
            } else if (kind < 8) {
                kg_new_animal(p, t, kind - 5, g->day - 9);
                p->tiles[t].fed_today = t % 2;
                p->tiles[t].cared_today = (t + 1) % 2;
                p->tiles[t].yield_units = t % 4;
                p->tiles[t].fertilizer_available = 1;
            } else if (kind == 8) {
                kg_set_player_tile(p, t, KG_TILE_WEED);
            } else if (kind == 9) {
                kg_set_player_tile(p, t, KG_TILE_PASTURE);
            }
        }
        KGPosition access[4];
        kg_shed_access_count(10, access);
        for (int u = 0; u < p->unit_count; u++) {
            p->units[u].x = u % 3 ? u % 5 : access[u % 4].x;
            p->units[u].y = u % 3 ? (index + u) % 5 : access[u % 4].y;
            kg_inventory_add(&p->units[u], (u + index) % KG_NUM_ITEMS, 1 + u % 7);
            kg_inventory_add(&p->units[u], KG_ITEM_WHEAT, u % 4);
        }
        for (int i = 0; i < KG_NUM_ITEMS; i++) {
            p->shed[i] = 0;
        }
        p->shed[index % KG_NUM_ITEMS] = index % 2 ? g->config.shed_capacity : 3;
        kg_sync_public_positions(p);
    }
    for (int item = 0; item < KG_NUM_PRODUCTS; item++) {
        g->market.inventory[item] += (index - 32) * 11;
    }
    kg_refresh_prices(g);
}

static void trace(int rich) {
    TestEnv* e = fixture();
    float logits[KG_POLICY_ACTION_MASK_SIZE];
    int episodes = rich ? 64 : 12;
    for (int run = 0; run < episodes; run++) {
        KGConfig cfg;
        kg_config_default(&cfg);
        cfg.seed = UINT64_C(0x1234567800000000) + run;
        cfg.weed_spawn_chance = run % 2 ? .005 : 0;
        cfg.shed_capacity = run % 3 ? 100 : 7;
        cfg.farm_hand_cost_mult = run % 5 ? 1 : 0;
        cfg.max_market_orders_per_turn = run % 4 ? 10 : 3;
        cfg.turns_per_day = run % 3 == 0 ? 12 : run % 3 == 1 ? 24 : 48;
        kg_init(&e->game_storage, &cfg);
        configure(e,
            run % 3 == 0       ? 1
                : run % 3 == 1 ? 3
                               : 10,
            run % 3 == 0       ? 1
                : run % 3 == 1 ? 8
                               : 16,
            run % 4 ? 0 : 2);
        if (rich) {
            rich_state(&e->game_storage, run);
        }
        reset_policy(e, rich);
        e->rng = 12345 + run;
        for (int turn = 0; turn < (rich ? 4 : 719) && !kg_done(&e->game_storage); turn++) {
            KGAction pair[2];
            printf("%d %d", run, turn);
            for (int p = 0; p < 2; p++) {
                for (int a = 0; a < KG_POLICY_ACTION_MASK_SIZE; a++) {
                    logits[a] = sinf((a * 17 + run * 7 + turn * 13 + p) * .73f);
                }
                observe(e, p);
                write_mask(e, p);
                printf(" %016" PRIx64 " %016" PRIx64, hash_bytes(masks[p], sizeof(masks[p])),
                    hash_bytes(observations[p], sizeof(observations[p])));
                sample(e, p, logits, turn % 2);
                printf(" %016" PRIx64 " %016" PRIx64, hash_bytes(masks[p], sizeof(masks[p])),
                    hash_bytes(actions[p], sizeof(actions[p])));
                pair[p] = decode_player(e, p);
                printf(" %016" PRIx64, hash_bytes(&pair[p], sizeof(pair[p])));
            }
            kg_step(&e->game_storage, pair);
            step_policy(e);
            printf(" %u %016" PRIx64 "\n", e->rng,
                hash_bytes(&e->game_storage, sizeof(e->game_storage)));
        }
    }
    free(e);
}

int main(int argc, char** argv) {
    if (argc == 2) {
        assert(!strcmp(argv[1], "--trace") || !strcmp(argv[1], "--rich-trace"));
        trace(!strcmp(argv[1], "--rich-trace"));
        return 0;
    }
    assert(KAG_POLICY_VERSION == 5 && KAG_ALL_LOGITS == 1978);
    for (int n = 1; n <= 100; n++) {
        assert(kag_market_quantity_spec(kag_market_quantity_id(n)) == n);
    }
    concurrent_work();
    markets_and_auto_feed();
    shared_housing();
    strategic_fertilizer_precedes_water();
    strategic_harvest_and_wait();
    delivery_and_safe_overflow();
    scarce_fertilizer_and_unfed_chores();
    rollout();
    observations_and_history();
    land_gate_and_queue_limits();
    puts(
        "controller 2/2: worker/market reservations, chores, quantities, history and rollout PASS");
    return 0;
}
