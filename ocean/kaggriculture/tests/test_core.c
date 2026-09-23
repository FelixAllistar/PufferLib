#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#ifndef CORE_HEADER
#define CORE_HEADER "../core.h"
#endif
#include CORE_HEADER

#ifdef __CUDACC__
__global__ void core_compile(KGState* state, KGAction* actions, KGConfig* config) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    kg_init(state + i, config + i);
    kg_step(state + i, actions + 2 * i);
}
#endif

uint32_t draw(uint32_t* rng) {
    uint32_t value = *rng;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *rng = value;
    return value;
}

uint64_t hash_bytes(const void* data, size_t size) {
    const unsigned char* bytes = (const unsigned char*)data;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < size; i++) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return hash;
}

void trace(int rules) {
    KGState game, restored;
    unsigned char snapshot[sizeof(KGState)];
    for (int run = 0; run < 24; run++) {
        KGConfig cfg;
        kg_config_default(&cfg);
        cfg.seed = UINT64_C(0x1234567800000000) + 707 * run;
        cfg.board_size = 4 + run % 7;
        cfg.starting_money = run % 2 ? 3000 : 50000;
        cfg.weed_spawn_chance = run % 3 ? 0.005 : 0;
        cfg.shed_capacity = run % 2 ? 100 : 7;
        cfg.farm_hand_cost_mult = run % 4 ? 1 : 0;
        cfg.max_market_orders_per_turn = run % 3 ? 10 : KG_MAX_MARKET_ORDERS;
        kg_init(&game, &cfg);
        uint32_t rng = 991 + run;
        for (int episode = 0; episode < 2; episode++) {
            assert(game.step == 0 && game.done == 0);
            while (!game.done) {
                KGAction actions[2] = {0};
                for (int player = 0; player < 2; player++) {
                    KGAction* action = actions + player;
                    action->farmer = (KGUnitAction){
                        (int)(draw(&rng) % 22) - 2,
                        (int)(draw(&rng) % 16) - 2,
                        (int)(draw(&rng) % 12) - 1};
                    action->hand_count = (int)(draw(&rng) % (KG_MAX_HANDS + 8)) - 3;
                    for (int hand = 0; hand < KG_MAX_HANDS; hand++) {
                        action->hands[hand] = (KGUnitAction){
                            (int)(draw(&rng) % 22) - 2,
                            (int)(draw(&rng) % 16) - 2,
                            (int)(draw(&rng) % 12) - 1};
                    }
                    action->market_count = (int)(draw(&rng) % 37) - 2;
                    for (int order = 0; order < KG_MAX_MARKET_ORDERS; order++) {
                        action->market[order] = (KGMarketOrder){
                            (int)(draw(&rng) % 9) - 1,
                            (int)(draw(&rng) % 16) - 2,
                            (int)(draw(&rng) % 24) - 1};
                    }
                    if (game.step % 24 == 0) {
                        action->market[0] = (KGMarketOrder){KG_MARKET_HIRE, 0, 1};
                        action->market_count = 1;
                    }
                }
                if (rules) {
                    kg_rule_action_ex(&game, 0, run % 3 ? 48 : 96, actions);
                    kg_rule_action(&game, 1, actions + 1);
                }
                kg_step(&game, actions);
                if (game.step % 37 == 0) {
                    assert(kg_state_serialize(&game, snapshot, sizeof(snapshot)));
                    assert(kg_state_deserialize(&restored, snapshot, sizeof(snapshot)));
                    assert(memcmp(&game, &restored, sizeof(game)) == 0);
                    kg_step(&restored, actions);
                    KGState continued = game;
                    kg_step(&continued, actions);
                    assert(memcmp(&continued, &restored, sizeof(game)) == 0);
                }
                printf("%d %d %d %016" PRIx64 "\n", run, episode, game.step,
                    hash_bytes(&game, sizeof(game)));
                if (game.step % 24 == 0) {
                    const char* json = kg_snapshot_json(&game);
                    assert(json);
                    printf("json %016" PRIx64 "\n", hash_bytes(json, strlen(json)));
                    kg_free_string(json);
                }
            }
            assert(game.step == cfg.episode_steps - 1);
            kg_reset(&game);
        }
    }
}

void regressions(void) {
    KGConfig cfg;
    kg_config_default(&cfg);
    cfg.weed_spawn_chance = 0;
    KGState game;
    KGAction actions[2] = {0};

    // Both players quote before either purchase commits.
    kg_init(&game, &cfg);
    for (int player = 0; player < 2; player++) {
        actions[player].market_count = 1;
        actions[player].market[0] = (KGMarketOrder){
            KG_MARKET_BUY_PRODUCT, KG_ITEM_WHEAT, 1};
    }
    kg_step(&game, actions);
    for (int player = 0; player < 2; player++) {
        assert(game.players[player].money == 2974);
        assert(game.players[player].shed[KG_ITEM_WHEAT] == 1);
        assert(game.bought_units[player] == 1);
        assert(game.purchase_spend[player] == 26);
    }
    assert(game.market.inventory[KG_ITEM_WHEAT] == 9997);
    assert(game.exogenous_demand_units[KG_ITEM_WHEAT] == 1);

    // PLACE-to-shed works at all four access tiles, even locked corners.
    for (int x = 4; x <= 5; x++) {
        for (int y = 4; y <= 5; y++) {
            kg_init(&game, &cfg);
            KGPlayer* player = game.players;
            kg_set_unit_position(player, 0, x, y);
            kg_inventory_add(player->units, KG_ITEM_COW, 2);
            int kind = player->tiles[kg_tile_index(x, y)].kind;
            KGUnitAction place = {KG_OP_PLACE, KG_ITEM_COW, 1};
            kg_apply_unit_action(&game, player, 0, &place);
            assert(player->shed[KG_ITEM_COW] == 1);
            assert(player->units[0].inventory[KG_ITEM_COW] == 1);
            assert(player->tiles[kg_tile_index(x, y)].kind == kind);
        }
    }
    kg_init(&game, &cfg);
    KGPlayer* player = game.players;
    kg_inventory_add(player->units, KG_ITEM_COW, 3);
    player->shed[KG_ITEM_WHEAT] = cfg.shed_capacity - 1;
    KGUnitAction place = {KG_OP_PLACE, KG_ITEM_COW, 3};
    kg_apply_unit_action(&game, player, 0, &place);
    assert(player->shed[KG_ITEM_COW] == 1);
    assert(player->units[0].inventory[KG_ITEM_COW] == 2);

    // Actual animal placement takes priority over the shed fallback.
    kg_init(&game, &cfg);
    player = game.players;
    kg_inventory_add(player->units, KG_ITEM_COW, 1);
    kg_set_player_tile(player, kg_tile_index(4, 4), KG_TILE_PASTURE);
    kg_apply_unit_action(&game, player, 0, &place);
    assert(player->shed[KG_ITEM_COW] == 0);
    assert(player->tiles[kg_tile_index(4, 4)].animal == KG_COW);
    assert(game.placed_animals[0] == 1);

    // Insufficient seed demand rejects every simultaneous plant of that crop.
    kg_init(&game, &cfg);
    player = game.players;
    kg_do_hire(&game, player);
    kg_set_unit_position(player, 0, 0, 0);
    kg_set_unit_position(player, 1, 1, 0);
    player->seeds[KG_WHEAT] = 1;
    memset(actions, 0, sizeof(actions));
    actions[0].farmer = (KGUnitAction){KG_OP_PLANT, KG_WHEAT, 1};
    actions[0].hands[0] = actions[0].farmer;
    actions[0].hand_count = 1;
    kg_step(&game, actions);
    assert(player->seeds[KG_WHEAT] == 1 && game.planted_crops[0] == 0);

    // Planting counts as the first missed watering, including on day zero.
    kg_init(&game, &cfg);
    player = game.players;
    kg_set_unit_position(player, 0, 0, 0);
    player->seeds[KG_WHEAT] = 1;
    KGUnitAction plant = {KG_OP_PLANT, KG_WHEAT, 1};
    kg_apply_unit_action(&game, player, 0, &plant);
    memset(actions, 0, sizeof(actions));
    for (int i = 0; i < 24; i++) {
        kg_step(&game, actions);
    }
    assert(player->tiles[0].kind == KG_TILE_WEED);
    assert(game.planting_day_deaths[0] == 1);

    // Cows mature on day eight. Care bonuses accumulate but milk is capped.
    kg_init(&game, &cfg);
    player = game.players;
    kg_set_player_tile(player, 0, KG_TILE_PASTURE);
    kg_new_animal(player, 0, KG_COW, 0);
    for (int day = 0; day < 8; day++) {
        kg_set_unit_position(player, 0, 0, 0);
        kg_inventory_add(player->units, KG_ITEM_WHEAT, 1);
        KGUnitAction feed = {KG_OP_FEED, 0, 1};
        KGUnitAction care = {KG_OP_CARE, 0, 1};
        kg_apply_unit_action(&game, player, 0, &feed);
        kg_apply_unit_action(&game, player, 0, &care);
        for (int step = 0; step < 24; step++) {
            kg_step(&game, actions);
        }
        assert(player->tiles[0].animal == KG_COW);
        assert(player->tiles[0].yield_units == (day == 7 ? 6 : 0));
    }
    kg_set_unit_position(player, 0, 0, 0);
    KGUnitAction harvest = {KG_OP_HARVEST, 0, 1};
    kg_apply_unit_action(&game, player, 0, &harvest);
    assert(player->units[0].inventory[KG_ITEM_MILK] == 6);
    assert(game.production_product_units[0][KG_ITEM_MILK] == 6);

    // Preserve the reset-bank POD ABI, and reject bad input without mutation.
    unsigned char snapshot[sizeof(KGState)];
    assert(kg_state_serialized_size() == sizeof(KGState));
    assert(kg_state_serialization_version() == 1);
    assert(kg_state_serialize(&game, snapshot, sizeof(snapshot)));
    KGState restored = {0};
    assert(kg_state_deserialize(&restored, snapshot, sizeof(snapshot)));
    assert(memcmp(&game, &restored, sizeof(game)) == 0);
    KGState invalid = game;
    invalid.players[0].unit_count = 0;
    assert(!kg_state_deserialize(&restored, &invalid, sizeof(invalid)));
    assert(!kg_state_deserialize(&restored, snapshot, sizeof(snapshot) - 1));
    assert(memcmp(&game, &restored, sizeof(game)) == 0);

    kg_init(&game, &cfg);
    for (int step = 0; step < 719; step++) {
        assert(!game.done);
        kg_step(&game, actions);
    }
    assert(game.done && game.step == 719);
    restored = game;
    kg_step(&game, actions);
    assert(memcmp(&game, &restored, sizeof(game)) == 0);
    puts("Kaggriculture core regressions passed");
}

int main(int argc, char** argv) {
    if (argc == 2 && strcmp(argv[1], "--trace") == 0) {
        trace(0);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--rule-trace") == 0) {
        trace(1);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--rng") == 0) {
        uint64_t seeds[] = {0, 1, 42, UINT64_C(0x100000011), UINT64_C(0x8000000000000043)};
        for (int i = 0; i < 5; i++) {
            KGPythonRandom rng;
            kg_random_seed(&rng, seeds[i]);
            for (int j = 0; j < 32; j++) {
                printf("%" PRIu64 " %" PRIu32 "\n", seeds[i], kg_random_uint32(&rng));
            }
        }
        return 0;
    }
    regressions();
    return 0;
}
