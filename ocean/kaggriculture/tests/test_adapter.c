#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../kaggriculture.h"

_Static_assert(NUM_ATNS == 42, "Kaggriculture action head count changed");
_Static_assert(KG_POLICY_MARKET_SLOTS == 10,
    "Kaggriculture market head count changed");
_Static_assert(KG_POLICY_ACTION_MASK_SIZE == 838,
    "Kaggriculture action mask changed");

static void assert_rule_regressions(void) {
    KGConfig config;
    KGState game;
    kg_config_default(&config);
    kg_init(&game, &config);
    KGPlayer* farm = &game.players[0];

    kg_new_plant(farm, 0, KG_WHEAT, 0, config.turns_per_day);
    assert(farm->tiles[0].consecutive_unwatered == 1);
    kg_daily_refresh_plants(&game, farm, 0);
    assert(farm->tiles[0].kind == KG_TILE_WEED);

    kg_new_animal(farm, 0, KG_COW, 0);
    farm->tiles[0].pending_care_bonus = 4;
    kg_daily_refresh_animals(&game, farm, 7);
    assert(farm->tiles[0].yield_units == 1);
    assert(farm->tiles[0].pending_care_bonus == 0);

    kg_new_animal(farm, 0, KG_COW, 0);
    farm->tiles[0].fed_today = 1;
    farm->tiles[0].cared_today = 1;
    kg_daily_refresh_animals(&game, farm, 0);
    assert(farm->tiles[0].pending_care_bonus == 1);

    farm->units[0].x = 0;
    farm->units[0].y = 0;
    KGUnitAction dig_animal = {KG_OP_DIG, -1, 1};
    kg_apply_unit_action(&game, farm, 0, &dig_animal);
    assert(kg_is_animal_tile(&farm->tiles[0]));

    kg_set_player_tile(farm, 0, KG_TILE_WEED);
    farm->units[0].x = 0;
    farm->units[0].y = 0;
    KGUnitAction dig = {KG_OP_DIG, -1, 1};
    kg_apply_unit_action(&game, farm, 0, &dig);
    assert(farm->tiles[0].kind == KG_TILE_EMPTY);

    kg_new_plant(farm, 0, KG_TOMATO, 0, config.turns_per_day);
    farm->tiles[0].max_lifespan_step = 0;
    farm->tiles[0].yield_units = 0;
    kg_decay_plants(&game, farm, 0);
    assert(farm->tiles[0].kind == KG_TILE_WEED);
}

static void assert_potential_schedule(void) {
    Env env = {0};
    KGConfig config;
    kg_config_default(&config);
    kg_init(&env.game_storage, &config);
    env.reward_seed_value = 1.0f;
    env.reward_product_value = 1.0f;
    env.reward_crop_value = 1.0f;
    env.reward_animal_value = 1.0f;
    env.reward_land_value = 1.0f;
    env.reward_neglect_discount = 0.5f;
    env.reward_liquidation_days = 6.0f;
    KGPlayer* player = &env.game_storage.players[0];

    player->money = 2990;
    player->seeds[KG_WHEAT] = 1;
    assert(fabsf(kag_player_potential(&env, 0) - 3000.0f) < 1e-5f);

    player->money = 2000;
    player->seeds[KG_WHEAT] = 0;
    player->unlocked_mask = 3;
    assert(fabsf(kag_player_potential(&env, 0) - 3000.0f) < 1e-5f);

    player->money = 2900;
    player->unlocked_mask = 1;
    player->shed[KG_ITEM_FERTILIZER] = 1;
    assert(fabsf(kag_player_potential(&env, 0) - 3000.0f) < 1e-5f);
    env.game_storage.step = config.episode_steps - 3 * config.turns_per_day;
    assert(fabsf(kag_player_potential(&env, 0) - 2950.0f) < 1e-5f);
    env.game_storage.step = config.episode_steps - 1;
    float expected = 2900.0f + 100.0f / (6.0f * config.turns_per_day);
    assert(fabsf(kag_player_potential(&env, 0) - expected) < 1e-4f);
}

static void assert_market_impact_mark(void) {
    Env env = {0};
    KGConfig config;
    kg_config_default(&config);
    kg_init(&env.game_storage, &config);
    KGState* game = &env.game_storage;
    int item = KG_ITEM_WHEAT;
    game->market.inventory[item] = 10000;
    kg_refresh_prices(game);

    env.reward_market_impact = 0.0f;
    float spot_mark = kag_product_mark(&env, item, 50.0f, 0.0f);
    assert(fabsf(spot_mark - 50.0f * game->market.prices[item]) < 1e-5f);

    env.reward_market_impact = 1.0f;
    /* A one-unit liquid inventory remains exactly executable. */
    for (int product = 0; product < KG_NUM_PRODUCTS; product++) {
        game->market.inventory[product] = KG_MARKET_DEFS[product].i0;
        kg_refresh_prices(game);
        assert(fabsf(kag_product_mark(&env, product, 1.0f, 0.0f)
            - (float)game->market.prices[product]) < 1e-5f);
    }
    game->market.inventory[item] = 10000;
    kg_refresh_prices(game);
    float impact_mark = kag_product_mark(&env, item, 50.0f, 0.0f);
    assert(impact_mark < spot_mark);

    const int offsets[] = {-200, -10, 0, 200};
    const int piles[] = {2, 17, 128, 2048};
    for (int product = 0; product < KG_NUM_PRODUCTS; product++) {
        for (int oi = 0; oi < 4; oi++) {
            for (int pi = 0; pi < 4; pi++) {
                int inventory = KG_MARKET_DEFS[product].i0 + offsets[oi];
                int units = piles[pi];
                game->market.inventory[product] = inventory;
                kg_refresh_prices(game);
                float mark = kag_product_mark(&env, product,
                    (float)units, 0.0f);
                double brute = 0.0;
                for (int unit = 0; unit < units; unit++) {
                    brute += kg_market_price(product, inventory + unit);
                }
                /* The analytic curve is a conservative approximation to the
                 * exact discrete sequence, never the old final quote times N. */
                assert(mark >= (float)units - 1e-3f);
                assert(mark <= (float)brute + 1e-2f);

                float quote = (float)game->market.prices[product];
                game->market.inventory[product]++;
                kg_refresh_prices(game);
                float remainder = kag_product_mark(&env, product,
                    (float)(units - 1), 0.0f);
                if (quote + remainder + 1.0f < mark) {
                    fprintf(stderr, "mark decomposition product=%d inventory=%d "
                        "units=%d quote=%g remainder=%g mark=%g\n",
                        product, inventory, units, quote, remainder, mark);
                    assert(quote + remainder + 1.0f >= mark);
                }
            }
        }

        /* Selling one held unit advances inventory and removes one prior unit,
         * leaving the mark of later projected production unchanged. */
        game->market.inventory[product] = KG_MARKET_DEFS[product].i0;
        kg_refresh_prices(game);
        float future_before = kag_product_mark(&env, product, 20.0f, 10.0f);
        game->market.inventory[product]++;
        kg_refresh_prices(game);
        float future_after = kag_product_mark(&env, product, 20.0f, 9.0f);
        assert(fabsf(future_before - future_after) < 1e-4f);
    }

    /* The steep strawberry curve made the old terminal-marginal mark almost
     * worthless. Cumulative revenue must retain the valuable early units. */
    game->market.inventory[KG_ITEM_STRAWBERRY] =
        KG_MARKET_DEFS[KG_ITEM_STRAWBERRY].i0;
    kg_refresh_prices(game);
    float cumulative = kag_product_mark(&env, KG_ITEM_STRAWBERRY,
        100.0f, 0.0f);
    float terminal_marginal = 100.0f * kg_market_price(KG_ITEM_STRAWBERRY,
        KG_MARKET_DEFS[KG_ITEM_STRAWBERRY].i0 + 100);
    assert(cumulative > 10.0f * terminal_marginal);
}

static void assert_reward_asset_semantics(void) {
    Env env = {0};
    KGConfig config;
    kg_config_default(&config);
    kg_init(&env.game_storage, &config);
    env.reward_market_impact = 1.0f;
    env.reward_product_value = 1.0f;
    env.reward_crop_value = 0.0f;
    env.reward_animal_value = 1.0f;
    env.reward_land_value = 1.0f;
    env.reward_neglect_discount = 1.0f;
    env.reward_liquidation_days = 6.0f;
    KGPlayer* player = &env.game_storage.players[0];

    /* Liquid yield has the same mark before and after HARVEST, while land is
     * no longer erased by the generic inventory liquidation schedule. */
    env.game_storage.step = config.episode_steps - config.turns_per_day;
    env.game_storage.day = 29;
    kg_new_plant(player, 0, KG_WHEAT, 0, config.turns_per_day);
    player->tiles[0].watered_today = 1;
    player->tiles[0].yield_units = 3;
    float field_value = kag_player_potential(&env, 0);
    kg_set_player_tile(player, 0, KG_TILE_EMPTY);
    player->shed[KG_ITEM_WHEAT] = 3;
    float held_value = kag_player_potential(&env, 0);
    assert(fabsf(field_value - held_value) < 1e-3f);

    player->shed[KG_ITEM_WHEAT] = 0;
    player->money = 2000;
    player->unlocked_mask = 3;
    assert(fabsf(kag_player_potential(&env, 0) - 3000.0f) < 1e-3f);

    env.reward_production_scale = 0.02f;
    assert(fabsf(kag_realized_production_reward(&env, 100.0f, 400.0f)
        - 0.002f) < 1e-7f);
    assert(kag_realized_production_reward(&env, 400.0f, 400.0f) == 0.0f);
}

static void assert_masks_match(const Env* env, int player_id) {
    const KGState* game = &env->game_storage;
    const KGPlayer* player = &game->players[player_id];
    const unsigned char* mask = env->agents[player_id].action_mask;
    assert(mask != NULL);
    for (int slot = 0; slot < KG_POLICY_UNIT_HEADS; slot++) {
        int base = slot * KG_POLICY_UNIT_COMMANDS;
        for (int id = 0; id < KG_POLICY_UNIT_COMMANDS; id++) {
            int expected = 0;
            if (slot <= KG_POLICY_DIRECT_HANDS) {
                expected = kag_unit_action_legal(game, player, slot,
                    kag_unit_spec(id));
            } else {
                expected = id == KG_U_PASS;
                int cohort = slot - 1 - KG_POLICY_DIRECT_HANDS;
                for (int unit = 1 + KG_POLICY_DIRECT_HANDS + cohort;
                        unit < player->unit_count;
                        unit += KG_POLICY_OVERFLOW_COHORTS) {
                    expected |= kag_unit_action_legal(game, player, unit,
                        kag_unit_spec(id));
                }
            }
            assert(mask[base + id] == (unsigned char)expected);
        }
    }
    for (int slot = 0; slot < KG_POLICY_MARKET_SLOTS; slot++) {
        int base = KG_POLICY_MARKET_MASK_OFFSET
            + slot * KG_POLICY_MARKET_SLOT_MASK_SIZE;
        assert(mask[base] == 1);
        int any = 0;
        for (int id = 0; id < KG_POLICY_MARKET_COMMANDS; id++) {
            int expected = kag_market_action_legal(game, player,
                kag_market_spec(id));
            any |= expected;
            int at = base + KG_POLICY_MARKET_CONTINUE_ACTIONS + id;
            if (mask[at] != (unsigned char)expected) {
                fprintf(stderr, "market mask mismatch slot=%d id=%d got=%d expected=%d\n",
                    slot, id, mask[at], expected);
                abort();
            }
        }
        assert(mask[base + 1] == (unsigned char)any);
        int quantity_base = base + KG_POLICY_MARKET_CONTINUE_ACTIONS
            + KG_POLICY_MARKET_COMMANDS;
        for (int id = 0; id < KG_POLICY_MARKET_QUANTITIES; id++) {
            assert(mask[quantity_base + id] == 1);
        }
    }
}

static void pass_action(const KGState* game, int player, KGAction* action) {
    memset(action, 0, sizeof(*action));
    action->farmer = (KGUnitAction){KG_OP_PASS, -1, 1};
    action->hand_count = game->players[player].hand_count;
    for (int hand = 0; hand < action->hand_count; hand++) {
        action->hands[hand] = (KGUnitAction){KG_OP_PASS, -1, 1};
    }
}

static void assert_policy_land_path(void) {
    KGConfig config;
    KGState game;
    float policy_actions[NUM_ATNS] = {0};
    Agent agent = {.actions = policy_actions};
    static const uint8_t expected_masks[3] = {3, 7, 15};
    kg_config_default(&config);
    kg_init(&game, &config);
    game.players[0].money = 10000;

    KGPolicyMarketSpec land = kag_market_spec(KG_M_LAND);
    assert(land.op == KG_MARKET_BUY_LAND);
    for (int purchase = 0; purchase < 3; purchase++) {
        KGAction actions[KG_NUM_PLAYERS];
        assert(kag_market_action_legal(&game, &game.players[0], land));
        kag_clear_policy_actions(&agent);
        kag_set_policy_market(&agent, 0, KG_MARKET_BUY_LAND, -1, 1);
        kag_decode_action(&actions[0], &agent, &game, 0);
        assert(actions[0].market_count == 1);
        assert(actions[0].market[0].op == KG_MARKET_BUY_LAND);
        pass_action(&game, 1, &actions[1]);
        kg_step(&game, actions);
        assert(game.players[0].unlocked_mask == expected_masks[purchase]);
    }
    assert(!kag_market_action_legal(&game, &game.players[0], land));
    for (int y = 0; y < config.board_size; y++) {
        for (int x = 0; x < config.board_size; x++) {
            assert(game.players[0].tiles[kg_tile_index(x, y)].kind
                != KG_TILE_LOCKED);
        }
    }
}

static void assert_compact_market_order(void) {
    KGConfig config;
    KGState game;
    float policy_actions[NUM_ATNS] = {0};
    Agent agent = {.actions = policy_actions};
    KGAction decoded;
    kg_config_default(&config);
    assert(config.max_market_orders_per_turn == 10);
    kg_init(&game, &config);
    game.players[0].money = 100000;
    kag_clear_policy_actions(&agent);
    kag_set_policy_market(&agent, 0, KG_MARKET_BUY_SEED, KG_WHEAT, 1);
    kag_decode_action(&decoded, &agent, &game, 0);
    assert(decoded.market_count == 1);
    assert(decoded.market[0].op == KG_MARKET_BUY_SEED);
    assert(decoded.market[0].item == KG_WHEAT);
    assert(decoded.market[0].n == 1);

    kag_set_policy_market(&agent, 1, KG_MARKET_BUY_PRODUCT,
        KG_ITEM_WHEAT, 10);
    kag_set_policy_market(&agent, 2, KG_MARKET_BUY_ANIMAL,
        KG_ITEM_COW, 4);
    kag_decode_action(&decoded, &agent, &game, 0);
    assert(decoded.market_count == 3);
    assert(decoded.market[1].op == KG_MARKET_BUY_PRODUCT);
    assert(decoded.market[1].n == 10);
    assert(decoded.market[2].op == KG_MARKET_BUY_ANIMAL);
    assert(decoded.market[2].n == 4);

    agent.actions[KG_POLICY_MARKET_HEAD_OFFSET + 3] =
        (float)PUFFER_CONDITIONAL_STOP;
    kag_decode_action(&decoded, &agent, &game, 0);
    assert(decoded.market_count == 1);
}

static void assert_policy_ablation_limits(void) {
    Env env = {0};
    unsigned char mask[KG_POLICY_ACTION_MASK_SIZE] = {0};
    KGConfig config;
    kg_config_default(&config);
    kg_init(&env.game_storage, &config);
    env.agents[0].action_mask = mask;
    env.policy_market_slots = 1;
    env.policy_max_hands = 8;
    kag_write_mask(&env, 0);

    int first = KG_POLICY_MARKET_MASK_OFFSET;
    assert(mask[first] == 1);
    assert(mask[first + 1] == 1);
    for (int slot = 1; slot < KG_POLICY_MARKET_SLOTS; slot++) {
        int base = KG_POLICY_MARKET_MASK_OFFSET
            + slot * KG_POLICY_MARKET_SLOT_MASK_SIZE;
        assert(mask[base] == 1);
        for (int action = 1; action < KG_POLICY_MARKET_SLOT_MASK_SIZE; action++) {
            assert(mask[base + action] == 0);
        }
    }

    KGAction action = {0};
    action.market_count = 3;
    action.market[0] = (KGMarketOrder){KG_MARKET_BUY_SEED, KG_WHEAT, 1};
    action.market[1] = (KGMarketOrder){KG_MARKET_HIRE, -1, 1};
    action.market[2] = (KGMarketOrder){KG_MARKET_BUY_LAND, -1, 1};
    kag_apply_policy_limits(&env, &env.game_storage.players[0], &action);
    assert(action.market_count == 1);
    assert(action.market[0].op == KG_MARKET_BUY_SEED);

    env.policy_market_slots = KG_POLICY_MARKET_SLOTS;
    action.market_count = KG_POLICY_MARKET_SLOTS;
    for (int order = 0; order < action.market_count; order++) {
        action.market[order] = (KGMarketOrder){KG_MARKET_HIRE, -1, 1};
    }
    kag_apply_policy_limits(&env, &env.game_storage.players[0], &action);
    assert(action.market_count == 8);

    env.game_storage.players[0].hand_count = 8;
    memset(mask, 0, sizeof(mask));
    kag_write_mask(&env, 0);
    for (int slot = 0; slot < KG_POLICY_MARKET_SLOTS; slot++) {
        int base = KG_POLICY_MARKET_MASK_OFFSET
            + slot * KG_POLICY_MARKET_SLOT_MASK_SIZE
            + KG_POLICY_MARKET_CONTINUE_ACTIONS;
        assert(mask[base + KG_M_HIRE] == 0);
    }
}

static void assert_move_onto_locked(void) {
    KGConfig config;
    KGState game;
    kg_config_default(&config);
    kg_init(&game, &config);
    KGPlayer* farm = &game.players[0];
    /* SE corner of board is locked at reset. */
    farm->units[0].x = 9;
    farm->units[0].y = 8;
    assert(farm->tiles[kg_tile_index(9, 9)].kind == KG_TILE_LOCKED);
    KGUnitAction south = {KG_OP_SOUTH, -1, 1};
    kg_apply_unit_action(&game, farm, 0, &south);
    assert(farm->units[0].x == 9);
    assert(farm->units[0].y == 9);
    assert(kg_unit_can_move_to(farm, 9, 9, config.board_size));
}

static void assert_rules_bots(void) {
    int scores[KG_NUM_CROPS + 1];
    for (int variant = 0; variant <= KG_NUM_CROPS; variant++) {
        KGConfig config;
        kg_config_default(&config);
        config.seed = (uint64_t)(100 + variant);
        KGState game;
        kg_init(&game, &config);
        while (!kg_done(&game)) {
            KGAction actions[KG_NUM_PLAYERS];
            int fixed_crop = variant == 0 ? -1 : variant - 1;
            kag_bot_action(&game, 0, fixed_crop, &actions[0]);
            pass_action(&game, 1, &actions[1]);
            kg_step(&game, actions);
        }
        scores[variant] = game.players[0].money;
    }
    printf("rules bots vs pass: mixed=%d wheat=%d carrot=%d tomato=%d "
        "strawberry=%d melon=%d\n", scores[0], scores[1], scores[2],
        scores[3], scores[4], scores[5]);
    for (int variant = 0; variant <= KG_NUM_CROPS; variant++) {
        assert(scores[variant] > 3000);
    }
}

static void assert_native_tapes(void) {
    KGConfig config;
    KGState game;
    kg_config_default(&config);
    kg_init(&game, &config);
    KGPlayer* farm = &game.players[0];
    farm->hand_count = 15;
    farm->unit_count = 16;
    for (int unit = 0; unit < farm->unit_count; unit++) {
        farm->units[unit].x = 0;
        farm->units[unit].y = 0;
    }
    kag_script_init();
    for (int profile = 0; profile < KG_SCRIPT_COUNT; profile++) {
        for (int step = 0; step < KG_SCRIPT_FRAMES; step++) {
            game.step = step;
            KGAction action;
            kag_script_action(&game, 0, profile, &action);
            assert(action.hand_count == farm->hand_count);
            assert(action.market_count >= 0 && action.market_count <= 10);
            for (int unit = 0; unit < farm->unit_count; unit++) {
                const KGUnitAction* command = unit == 0
                    ? &action.farmer : &action.hands[unit - 1];
                assert(command->op >= KG_OP_PASS && command->op <= KG_OP_CARE);
                assert(command->n > 0 && command->n <= 127);
                if (command->op == KG_OP_PLANT) {
                    assert(command->arg >= KG_WHEAT && command->arg <= KG_MELON);
                } else if (command->op == KG_OP_PICKUP
                        || command->op == KG_OP_PLACE) {
                    assert(command->arg >= KG_ITEM_WHEAT
                        && command->arg <= KG_ITEM_SHEEP);
                } else {
                    /* The engine ignores arg for these operations. Some
                     * captured public tapes preserve the source agent's
                     * harmless payload (for example FEED with arg=0), so
                     * validate the packed nibble rather than canonicalizing
                     * it and breaking tape fidelity. */
                    assert(command->arg >= -1 && command->arg <= 14);
                }
            }
            for (int order = 0; order < action.market_count; order++) {
                const KGMarketOrder* market = &action.market[order];
                assert(market->op >= KG_MARKET_BUY_SEED
                    && market->op <= KG_MARKET_BUY_LAND);
                assert(market->item == -1
                    || (market->item >= KG_ITEM_WHEAT
                        && market->item <= KG_ITEM_SHEEP));
                assert(market->n > 0 && market->n <= 127);
            }
            kag_script_repair(&game, 0, profile, &action);
        }
    }
    printf("native tapes: %d profiles, %d frames, max hands=%d, max market=%d\n",
        KG_SCRIPT_COUNT, KG_SCRIPT_FRAMES, 15, 10);
}

static void assert_native_public_profiles(void) {
    KGConfig config;
    kg_config_default(&config);
    config.episode_steps = 720;
    for (int profile = KAG_ADAPTIVE_HARVEST_PULSE;
            profile <= KAG_ADAPTIVE_TRIAD; profile++) {
        KGState game;
        int plants = 0;
        int animal_places = 0;
        kg_init(&game, &config);
        while (!kg_done(&game)) {
            KGAction actions[KG_NUM_PLAYERS];
            kag_public_action(&game, 0, profile, &actions[0]);
            pass_action(&game, 1, &actions[1]);
            assert(actions[0].hand_count == game.players[0].hand_count);
            assert(actions[0].market_count >= 0
                && actions[0].market_count <= KG_MAX_MARKET_ORDERS);
            for (int unit = 0; unit < actions[0].hand_count + 1; unit++) {
                const KGUnitAction* action = unit == 0
                    ? &actions[0].farmer : &actions[0].hands[unit - 1];
                assert(action->op >= KG_OP_PASS && action->op <= KG_OP_CARE);
                plants += action->op == KG_OP_PLANT;
                animal_places += action->op == KG_OP_PLACE
                    && action->arg >= KG_ITEM_GOOSE
                    && action->arg <= KG_ITEM_SHEEP;
            }
            kg_step(&game, actions);
        }
        assert(game.players[0].money > config.starting_money);
        printf("public profile %d: money=%d plants=%d animal_places=%d land=%d\n",
            profile, game.players[0].money, plants, animal_places,
            kag_public_land_count(&game.players[0]));
    }
    printf("native public profiles: pulse, structured, triad PASS\n");
}

static void assert_discounted_economic_reward(void) {
    Env env = {0};
    kg_config_default(&env.game_storage.config);
    env.game_storage.config.starting_money = 3000;
    env.reward_potential_scale = 0.2f;
    env.reward_potential_gamma = 0.9997f;
    env.reward_money_scale = 0.1f;

    float growth = kag_potential_shaping_reward(
        &env, 3000.0f, 6000.0f, 0);
    assert(fabsf(growth - 0.2f * 0.9997f) < 1e-6f);
    float terminal = kag_potential_shaping_reward(
        &env, 6000.0f, 6000.0f, 1);
    assert(fabsf(terminal + 0.2f) < 1e-6f);
    /* Discounted shaping telescopes exactly when its gamma matches PPO's:
     * initial phi is zero and terminal phi is forced back to zero. */
    assert(fabsf(growth + env.reward_potential_gamma * terminal) < 1e-6f);
    assert(fabsf(kag_terminal_money_reward(&env, 6000) - 0.1f) < 1e-6f);
    assert(fabsf(kag_terminal_money_reward(&env, 60000) - 1.9f) < 1e-6f);

    env.reward_potential_gamma = 0.0f;
    env.reward_potential_scale = 0.0001f;
    float legacy = kag_potential_shaping_reward(
        &env, 3000.0f, 6000.0f, 0);
    assert(fabsf(legacy - 0.3f) < 1e-6f);
}

int main(void) {
    assert_discounted_economic_reward();
    assert_rule_regressions();
    assert_potential_schedule();
    assert_market_impact_mark();
    assert_reward_asset_semantics();
    assert_policy_land_path();
    assert_compact_market_order();
    assert_policy_ablation_limits();
    assert_move_onto_locked();
    assert_rules_bots();
    assert_native_tapes();
    assert_native_public_profiles();
    Env env = {0};
    env.reward_potential_scale = 0.0001f;
    env.reward_win = 1.0f;
    env.reward_seed_value = 1.0f;
    env.reward_product_value = 1.0f;
    env.reward_crop_value = 1.0f;
    env.reward_animal_value = 1.0f;
    env.reward_land_value = 1.0f;
    env.reward_neglect_discount = 0.5f;
    env.reward_liquidation_days = 6.0f;
    obs_t observations[KG_NUM_PLAYERS * OBS_SIZE] = {0};
    float actions[KG_NUM_PLAYERS * NUM_ATNS] = {0};
    float rewards[KG_NUM_PLAYERS] = {0};
    float terminals[KG_NUM_PLAYERS] = {0};
    unsigned char masks[KG_NUM_PLAYERS * KG_POLICY_ACTION_MASK_SIZE] = {0};
    int terminal_count = 0;

    env.rng = 0;
    KGConfig config;
    kg_config_default(&config);
    config.episode_steps = 8;
    config.turns_per_day = 24;
    config.seed = 7;
    kg_init(&env.game_storage, &config);
    env.num_agents = KG_NUM_PLAYERS;
    for (int player = 0; player < KG_NUM_PLAYERS; player++) {
        env.agents[player].observations = observations + player * OBS_SIZE;
        env.agents[player].actions = actions + player * NUM_ATNS;
        env.agents[player].rewards = rewards + player;
        env.agents[player].terminals = terminals + player;
        env.agents[player].action_mask = masks
            + player * KG_POLICY_ACTION_MASK_SIZE;
    }
    puf_reset(&env);
    assert(env.game_storage.step == 0);
    assert(observations[0] > 0);
    assert_masks_match(&env, 0);
    assert_masks_match(&env, 1);
    obs_t public_before[OBS_SIZE];
    memcpy(public_before, observations, sizeof(public_before));
    env.game_storage.players[1].shed[KG_ITEM_MELON] = 77;
    env.game_storage.players[1].seeds[KG_MELON] = 33;
    env.game_storage.players[1].units[0].inventory[KG_ITEM_FERTILIZER] = 9;
    kag_write_observation(&env, 0);
    assert(memcmp(public_before, observations, sizeof(public_before)) == 0);
    env.game_storage.players[1].shed[KG_ITEM_MELON] = 0;
    env.game_storage.players[1].seeds[KG_MELON] = 0;
    env.game_storage.players[1].units[0].inventory[KG_ITEM_FERTILIZER] = 0;
    kag_clear_policy_actions(&env.agents[0]);
    kag_clear_policy_actions(&env.agents[1]);
    kag_set_policy_market(&env.agents[0], 0, KG_MARKET_HIRE, -1, 1);
    puf_step(&env);
    assert(rewards[0] < 0.0f);
    assert(rewards[1] == 0.0f);
    assert(env.game_storage.players[0].hand_count == 1);
    assert_masks_match(&env, 0);

    kag_clear_policy_actions(&env.agents[0]);
    kag_set_policy_market(&env.agents[0], 0, KG_MARKET_BUY_SEED, KG_WHEAT, 1);
    puf_step(&env);
    assert(env.game_storage.players[0].seeds[KG_WHEAT] == 1);
    assert_masks_match(&env, 0);

    for (int hire = 0; hire < 2; hire++) {
        kag_clear_policy_actions(&env.agents[0]);
        kag_set_policy_market(&env.agents[0], 0, KG_MARKET_HIRE, -1, 1);
        puf_step(&env);
        assert_masks_match(&env, 0);
    }
    assert(env.game_storage.players[0].hand_count == 3);

    kag_clear_policy_actions(&env.agents[0]);
    kag_set_policy_unit(&env.agents[0], 0, KG_OP_NORTH, -1, 1);
    kag_set_policy_unit(&env.agents[0], 1, KG_OP_WEST, -1, 1);
    kag_set_policy_unit(&env.agents[0], 2, KG_OP_NORTH, -1, 1);
    kag_set_policy_market(&env.agents[0], 0, KG_MARKET_BUY_SEED, KG_CARROT, 2);
    puf_step(&env);
    assert(env.game_storage.players[0].farmer.y == 3);
    assert(env.game_storage.players[0].hands[0].x == 4);
    assert(env.game_storage.players[0].hands[1].y == 4);
    assert(env.game_storage.players[0].seeds[KG_CARROT] == 2);

    kag_clear_policy_actions(&env.agents[0]);
    kag_clear_policy_actions(&env.agents[1]);
    for (int step = 0; step < config.episode_steps; step++) {
        puf_step(&env);
        if (terminals[0] != 0.0f) {
            terminal_count++;
            assert(terminals[1] == 1.0f);
            assert(env.game_storage.step == 0);
        }
    }
    assert(terminal_count == 1);
    assert(env.log.n == 1.0f);
    float terminal_outcome = env.log.money > env.log.opponent_money ? 1.0f
        : env.log.money < env.log.opponent_money ? -1.0f : 0.0f;
    float expected_return = terminal_outcome * env.reward_win
        + (env.log.money - config.starting_money)
            * env.reward_potential_scale;
    assert(fabsf(env.log.episode_return - expected_return) < 1e-5f);
    puf_close(&env);
    printf("Kaggriculture adapter: PASS\n");
    return 0;
}
