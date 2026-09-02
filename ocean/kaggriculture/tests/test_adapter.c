#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../kaggriculture.h"

_Static_assert(NUM_ATNS == 47, "Kaggriculture action head count changed");
_Static_assert(KG_POLICY_MARKET_SLOTS == 10,
    "Kaggriculture market head count changed");
_Static_assert(KG_POLICY_ACTION_MASK_SIZE == 1058,
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
    KGPlayer* player = &env.game_storage.players[0];

    /* Buying a seed and buying land exchange cash for equal-cost capital. */
    player->money = 2990;
    player->seeds[KG_WHEAT] = 1;
    assert(fabsf(kag_player_potential(&env, 0) - 3000.0f) < 1e-5f);

    player->money = 2000;
    player->seeds[KG_WHEAT] = 0;
    player->unlocked_mask = 3;
    assert(fabsf(kag_player_potential(&env, 0) - 3000.0f) < 1e-5f);
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

    /* A one-unit liquid inventory remains exactly executable. */
    for (int product = 0; product < KG_NUM_PRODUCTS; product++) {
        game->market.inventory[product] = KG_MARKET_DEFS[product].i0;
        kg_refresh_prices(game);
        assert(fabsf(kag_product_mark(&env, product, 1.0f)
            - (float)game->market.prices[product]) < 1e-5f);
    }
    game->market.inventory[item] = 10000;
    kg_refresh_prices(game);
    float impact_mark = kag_product_mark(&env, item, 50.0f);
    assert(impact_mark < 50.0f * game->market.prices[item]);

    const int offsets[] = {-200, -10, 0, 200};
    const int piles[] = {2, 17, 128, 2048};
    for (int product = 0; product < KG_NUM_PRODUCTS; product++) {
        for (int oi = 0; oi < 4; oi++) {
            for (int pi = 0; pi < 4; pi++) {
                int inventory = KG_MARKET_DEFS[product].i0 + offsets[oi];
                int units = piles[pi];
                game->market.inventory[product] = inventory;
                kg_refresh_prices(game);
                float mark = kag_product_mark(&env, product, (float)units);
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
                    (float)(units - 1));
                if (quote + remainder + 1.0f < mark) {
                    fprintf(stderr, "mark decomposition product=%d inventory=%d "
                        "units=%d quote=%g remainder=%g mark=%g\n",
                        product, inventory, units, quote, remainder, mark);
                    assert(quote + remainder + 1.0f >= mark);
                }
            }
        }

    }

    /* The steep strawberry curve made the old terminal-marginal mark almost
     * worthless. Cumulative revenue must retain the valuable early units. */
    game->market.inventory[KG_ITEM_STRAWBERRY] =
        KG_MARKET_DEFS[KG_ITEM_STRAWBERRY].i0;
    kg_refresh_prices(game);
    float cumulative = kag_product_mark(&env, KG_ITEM_STRAWBERRY, 100.0f);
    float terminal_marginal = 100.0f * kg_market_price(KG_ITEM_STRAWBERRY,
        KG_MARKET_DEFS[KG_ITEM_STRAWBERRY].i0 + 100);
    assert(cumulative > 10.0f * terminal_marginal);
}

static void assert_reward_asset_semantics(void) {
    Env env = {0};
    KGConfig config;
    kg_config_default(&config);
    kg_init(&env.game_storage, &config);
    KGPlayer* player = &env.game_storage.players[0];

    /* Planting preserves seed cost and harvesting preserves product value. */
    player->money = 3000;
    kg_new_plant(player, 0, KG_WHEAT, 0, config.turns_per_day);
    player->tiles[0].yield_units = 3;
    float field_value = kag_player_potential(&env, 0);
    player->tiles[0].yield_units = 0;
    player->shed[KG_ITEM_WHEAT] = 3;
    float held_value = kag_player_potential(&env, 0);
    assert(fabsf(field_value - held_value) < 1e-3f);

    player->shed[KG_ITEM_WHEAT] = 0;
    kg_set_player_tile(player, 0, KG_TILE_EMPTY);
    player->money = 2000;
    player->unlocked_mask = 3;
    assert(fabsf(kag_player_potential(&env, 0) - 3000.0f) < 1e-3f);

    /* Placed and unplaced animals retain purchase cost; feeding/care state is
     * intentionally not a hand-authored value multiplier. */
    memset(player->shed, 0, sizeof(player->shed));
    player->money = 2500;
    player->unlocked_mask = 1;
    player->shed[KG_ITEM_COW] = 1;
    float unplaced = kag_player_potential(&env, 0);
    player->shed[KG_ITEM_COW] = 0;
    kg_new_animal(player, 1, KG_COW, 0);
    float placed = kag_player_potential(&env, 0);
    assert(fabsf(unplaced - placed) < 1e-3f);
    player->tiles[1].fed_today = 0;
    player->tiles[1].cared_today = 0;
    assert(fabsf(kag_player_potential(&env, 0) - placed) < 1e-3f);
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
    env.reward_cash_scale = 0.3f;
    env.reward_money_scale = 0.1f;

    float growth = kag_potential_shaping_reward(
        &env, 3000.0f, 6000.0f);
    assert(fabsf(growth - 0.2f * 0.9997f) < 1e-6f);
    float terminal = kag_potential_shaping_reward(
        &env, 6000.0f, 9000.0f);
    assert(fabsf(terminal - 0.2f * (2.0f * 0.9997f - 1.0f)) < 1e-6f);
    /* Discounted shaping telescopes to the retained terminal net worth. */
    float expected = 0.2f * 2.0f * 0.9997f * 0.9997f;
    assert(fabsf(growth + env.reward_potential_gamma * terminal - expected)
        < 1e-6f);
    assert(fabsf(kag_terminal_money_reward(&env, 6000) - 0.1f) < 1e-6f);
    assert(fabsf(kag_terminal_money_reward(&env, 60000) - 1.9f) < 1e-6f);

    float cash_growth = kag_cash_shaping_reward(&env, 3000, 6000);
    float cash_terminal = kag_cash_shaping_reward(&env, 6000, 9000);
    assert(fabsf(cash_growth - 0.3f * 0.9997f) < 1e-6f);
    float expected_cash = 0.3f * 2.0f * 0.9997f * 0.9997f;
    assert(fabsf(cash_growth
            + env.reward_potential_gamma * cash_terminal - expected_cash)
        < 1e-6f);

    env.reward_potential_gamma = 0.0f;
    env.reward_potential_scale = 0.0001f;
    float legacy = kag_potential_shaping_reward(
        &env, 3000.0f, 6000.0f);
    assert(fabsf(legacy - 0.3f) < 1e-6f);
    assert(fabsf(kag_cash_shaping_reward(&env, 3000, 6000) - 0.3f)
        < 1e-6f);
}

static void assert_progress_potential_reward(void) {
    Env env = {0};
    kg_config_default(&env.game_storage.config);
    kg_init(&env.game_storage, &env.game_storage.config);
    env.reward_progress_scale = 1.0f;
    env.reward_potential_gamma = 0.9997f;
    env.reward_progress_terminal_money_scale = 0.0f;
    env.reward_progress_win_scale = 1.0f;
    env.reward_progress_liquidation_days = 6.0f;
    env.reward_progress_seed_scale = 1.0f;
    env.reward_progress_crop_scale = 1.0f;
    env.reward_progress_animal_scale = 1.0f;
    env.reward_progress_product_scale = 1.0f;
    env.reward_progress_maintenance_scale = 0.0f;
    env.reward_progress_land_scale = 1.0f;
    env.reward_progress_health_ratio = 0.4f;
    for (int crop = 0; crop < KG_NUM_CROPS; crop++) {
        env.reward_progress_crop_units[crop] = 4.0f;
        env.reward_progress_seed_realization[crop] = 1.0f;
    }
    for (int animal = 0; animal < KG_NUM_ANIMALS; animal++) {
        env.reward_progress_animal_units_per_event[animal] = 0.8f;
        env.reward_progress_animal_realization[animal] = 1.0f;
    }
    for (int item = 0; item < KG_NUM_PRODUCTS; item++) {
        env.reward_progress_product_realization[item] = 0.8f;
    }

    KGPlayer* player = &env.game_storage.players[0];
    float initial = kag_player_progress_value(&env, 0);
    assert(fabsf(initial - 3000.0f) < 1e-5f);

    /* Buying uncommitted capital is a neutral cash-for-cost-basis exchange. */
    player->money -= KG_CROP_DEFS[KG_TOMATO].seed_cost;
    player->seeds[KG_TOMATO] = 1;
    float held_seed = kag_player_progress_value(&env, 0);
    assert(fabsf(held_seed - initial) < 1e-5f);
    assert(fabsf(kag_progress_potential_reward(&env, initial, held_seed, 0))
        < 1e-5f);

    /* The animal exploration multiplier belongs only to placed future
     * production. Buying livestock that is still in the shed must remain a
     * cash-for-cost-basis exchange even at an intentionally exaggerated
     * scale, otherwise placement can become an artificial value writeoff. */
    player->seeds[KG_TOMATO] = 0;
    player->money = 3000 - KG_ANIMAL_DEFS[KG_COW].cost;
    player->shed[KG_ITEM_COW] = 1;
    env.reward_progress_animal_scale = 40.0f;
    float held_animal_high_scale = kag_player_progress_value(&env, 0);
    env.reward_progress_animal_scale = 1.0f;
    float held_animal_unit_scale = kag_player_progress_value(&env, 0);
    assert(fabsf(held_animal_high_scale - held_animal_unit_scale) < 1e-5f);
    assert(fabsf(held_animal_high_scale - initial) < 1e-5f);
    player->shed[KG_ITEM_COW] = 0;

    /* Daily labor is another neutral cash-for-capital exchange. */
    player->money = 2999;
    player->hires_today = 1;
    float hired = kag_player_progress_value(&env, 0);
    assert(fabsf(hired - initial) < 1e-5f);
    player->money = 3000 - KG_CROP_DEFS[KG_TOMATO].seed_cost;
    player->hires_today = 0;
    player->seeds[KG_TOMATO] = 1;

    /* Planting unlocks the empirically expected live-price future output. */
    player->seeds[KG_TOMATO] = 0;
    kg_new_plant(player, 0, KG_TOMATO, env.game_storage.day,
        env.game_storage.config.turns_per_day);
    float invested = kag_player_progress_value(&env, 0);
    float first = kag_progress_potential_reward(
        &env, held_seed, invested, 0);
    assert(first > 0.0f);

    /* Unlike a high-water objective, losing realizable value is visible. */
    assert(kag_progress_potential_reward(
        &env, invested, initial, 0) < 0.0f);

    /* Zero-terminal potential shaping cancels exactly under the matching
     * discount: phi 0 -> 2 -> terminal 0 contributes no discounted return. */
    float shaping_up = kag_progress_potential_reward(
        &env, 3000.0f, 9000.0f, 0);
    float shaping_writeoff = kag_progress_potential_reward(
        &env, 9000.0f, 12345.0f, 1);
    assert(fabsf(shaping_up
            + env.reward_potential_gamma * shaping_writeoff) < 1e-6f);

    env.reward_progress_terminal_money_scale = 0.25f;
    assert(fabsf(kag_progress_terminal_money_reward(&env, 2000)
            - (-0.25f / 3.0f)) < 1e-6f);
    assert(kag_progress_terminal_money_reward(&env, 3000) == 0.0f);
    assert(fabsf(kag_progress_terminal_money_reward(&env, 6000) - 0.25f)
        < 1e-6f);
    assert(kag_positive_terminal_win_reward(&env, 6000, 5000) == 1.0f);
    assert(kag_positive_terminal_win_reward(&env, 5000, 6000) == 0.0f);
    assert(kag_positive_terminal_win_reward(&env, 5000, 5000) == 0.5f);

    /* The same tomato asset is worth more in a live high-price opportunity;
     * no crop-specific action bonus is needed. */
    env.game_storage.market.inventory[KG_ITEM_TOMATO] = 10000;
    kg_refresh_prices(&env.game_storage);
    float ordinary = kag_player_progress_value(&env, 0);
    env.game_storage.market.inventory[KG_ITEM_TOMATO] = 100;
    kg_refresh_prices(&env.game_storage);
    float opportunity = kag_player_progress_value(&env, 0);
    assert(opportunity > ordinary);

    /* At done, every noncash asset is worth exactly zero. */
    env.game_storage.step = env.game_storage.config.episode_steps - 1;
    env.game_storage.done = 1;
    assert(kag_progress_asset_scale(&env) == 0.0f);
    assert(fabsf(kag_player_progress_value(&env, 0) - player->money) < 1e-5f);
}

static void assert_expansion_curriculum_reward(void) {
    Env env = {0};
    env.reward_expansion_scale = 10.0f;
    env.reward_expansion_land_target = 3;
    env.reward_expansion_plant_target = 50;
    env.reward_expansion_animal_target = 12;

    assert(kag_expansion_progress(&env, 1, 0, 0) == 0.0f);
    assert(fabsf(kag_expansion_progress(&env, 3, 50, 12) - 5.0f)
        < 1e-6f);
    /* A crop-only farm earns its crop branch but none of the two-point
     * balanced completion bonus. */
    assert(fabsf(kag_expansion_progress(&env, 1, 50, 0) - 1.0f)
        < 1e-6f);
    /* Three half-complete branches earn 1.5 branch points plus one balanced
     * point; advancing every branch is deliberately better than specializing. */
    assert(fabsf(kag_expansion_progress(&env, 2, 25, 6) - 2.5f)
        < 1e-6f);
}

static void assert_tagged_curriculum_states(void) {
    Env env = {0};
    KGConfig config;
    kg_config_default(&config);
    kg_init(&env.game_storage, &config);
    env.agents[0].policy = 0;
    env.agents[1].policy = 1;
    env.reset_opening_rng = 707;
    env.curriculum_reward = 1.0f;

    kag_curriculum_prepare(&env, KAG_CURRICULUM_SELL);
    int player = kag_curriculum_player(&env);
    int item = env.curriculum_target_item;
    assert(env.game_storage.players[player].shed[item] > 0);
    assert(env.curriculum_deadline_step > env.game_storage.step);
    assert(!kag_curriculum_success(&env));
    env.game_storage.sold_product_units[player][item] = 1;
    assert(kag_curriculum_success(&env));
    int success = 0;
    assert(kag_curriculum_after_step(&env, &success) == 1.0f);
    assert(success && env.game_storage.done);

    kg_reset(&env.game_storage);
    kag_curriculum_prepare(&env, KAG_CURRICULUM_HARVEST);
    KGPlayer* farm = &env.game_storage.players[player];
    int tile = kg_tile_index(farm->units[0].x, farm->units[0].y);
    assert(farm->tiles[tile].yield_units > 0);

    kg_reset(&env.game_storage);
    kag_curriculum_prepare(&env, KAG_CURRICULUM_PRODUCE);
    if (env.curriculum_branch) {
        assert(farm->units[0].inventory[KG_ITEM_GOOSE
            + env.curriculum_target_item - KG_ITEM_EGG] == 1);
    } else {
        assert(farm->seeds[env.curriculum_target_item] == 1);
    }

    env.curriculum_level = KAG_CURRICULUM_SELL;
    env.curriculum_stage = KAG_CURRICULUM_SELL;
    env.curriculum_window = 2;
    env.curriculum_success_rate = 0.5f;
    kag_curriculum_record(&env, 1);
    kag_curriculum_record(&env, 0);
    assert(env.curriculum_level == KAG_CURRICULUM_HARVEST);
}

static void curriculum_step(Env* env, KGAction actions[KG_NUM_PLAYERS]) {
    kag_curriculum_note_actions(env, actions);
    kg_step(&env->game_storage, actions);
    int success = 0;
    (void)kag_curriculum_after_step(env, &success);
}

static void assert_tagged_curriculum_solutions(void) {
    Env env = {0};
    KGConfig config;
    KGAction actions[KG_NUM_PLAYERS];
    kg_config_default(&config);
    kg_init(&env.game_storage, &config);
    env.agents[0].policy = 0;
    env.agents[1].policy = 1;
    env.reset_opening_rng = 1707;

    kag_curriculum_prepare(&env, KAG_CURRICULUM_SELL);
    int player = kag_curriculum_player(&env);
    int target = env.curriculum_target_item;
    memset(actions, 0, sizeof(actions));
    actions[player].market[0] = (KGMarketOrder){KG_MARKET_SELL, target, 1};
    actions[player].market_count = 1;
    curriculum_step(&env, actions);
    assert(kag_curriculum_success(&env));

    kg_reset(&env.game_storage);
    kag_curriculum_prepare(&env, KAG_CURRICULUM_HARVEST);
    target = env.curriculum_target_item;
    memset(actions, 0, sizeof(actions));
    actions[player].farmer = (KGUnitAction){KG_OP_HARVEST, -1, 1};
    curriculum_step(&env, actions);
    memset(actions, 0, sizeof(actions));
    actions[player].farmer = (KGUnitAction){KG_OP_DROP, -1, 1};
    curriculum_step(&env, actions);
    memset(actions, 0, sizeof(actions));
    actions[player].market[0] = (KGMarketOrder){KG_MARKET_SELL, target, 1};
    actions[player].market_count = 1;
    curriculum_step(&env, actions);
    assert(kag_curriculum_success(&env));

    /* Exercise both causal maintenance branches. Their yield begins at zero
     * and appears only after the correct end-of-day action. */
    for (int want_animal = 0; want_animal <= 1; want_animal++) {
        do {
            kg_reset(&env.game_storage);
            kag_curriculum_prepare(&env, KAG_CURRICULUM_MAINTAIN);
        } while (env.curriculum_branch != want_animal);
        target = env.curriculum_target_item;
        memset(actions, 0, sizeof(actions));
        actions[player].farmer = (KGUnitAction){
            want_animal ? KG_OP_FEED : KG_OP_WATER, -1, 1};
        curriculum_step(&env, actions);
        memset(actions, 0, sizeof(actions));
        actions[player].farmer = (KGUnitAction){KG_OP_HARVEST, -1, 1};
        curriculum_step(&env, actions);
        memset(actions, 0, sizeof(actions));
        actions[player].farmer = (KGUnitAction){KG_OP_DROP, -1, 1};
        curriculum_step(&env, actions);
        memset(actions, 0, sizeof(actions));
        actions[player].market[0] = (KGMarketOrder){KG_MARKET_SELL, target, 1};
        actions[player].market_count = 1;
        curriculum_step(&env, actions);
        assert(kag_curriculum_success(&env));
    }

    for (int want_animal = 0; want_animal <= 1; want_animal++) {
        do {
            kg_reset(&env.game_storage);
            kag_curriculum_prepare(&env, KAG_CURRICULUM_PRODUCE);
        } while (env.curriculum_branch != want_animal);
        target = env.curriculum_target_item;
        memset(actions, 0, sizeof(actions));
        actions[player].farmer = want_animal
            ? (KGUnitAction){KG_OP_PLACE,
                KG_ITEM_GOOSE + target - KG_ITEM_EGG, 1}
            : (KGUnitAction){KG_OP_PLANT, target, 1};
        curriculum_step(&env, actions);
        memset(actions, 0, sizeof(actions));
        actions[player].farmer = (KGUnitAction){
            want_animal ? KG_OP_FEED : KG_OP_WATER, -1, 1};
        curriculum_step(&env, actions);
        assert(kag_curriculum_success(&env));
    }

    /* Acquisition removes the free seed/animal. Both branches must buy the
     * correct input, put it into production, and maintain it. CASH_LOOP then
     * continues through harvest, shed drop, and sale. */
    for (int stage = KAG_CURRICULUM_ACQUIRE;
            stage <= KAG_CURRICULUM_CASH_LOOP; stage++) {
        for (int want_animal = 0; want_animal <= 1; want_animal++) {
            do {
                kg_reset(&env.game_storage);
                kag_curriculum_prepare(&env, stage);
            } while (env.curriculum_branch != want_animal);
            target = env.curriculum_target_item;
            int input = want_animal
                ? KG_ITEM_GOOSE + target - KG_ITEM_EGG : target;
            memset(actions, 0, sizeof(actions));
            actions[player].market[0] = (KGMarketOrder){
                want_animal ? KG_MARKET_BUY_ANIMAL : KG_MARKET_BUY_SEED,
                input, 1};
            actions[player].market_count = 1;
            curriculum_step(&env, actions);
            if (want_animal) {
                memset(actions, 0, sizeof(actions));
                actions[player].farmer = (KGUnitAction){KG_OP_PICKUP, input, 1};
                curriculum_step(&env, actions);
            }
            memset(actions, 0, sizeof(actions));
            actions[player].farmer = want_animal
                ? (KGUnitAction){KG_OP_PLACE, input, 1}
                : (KGUnitAction){KG_OP_PLANT, target, 1};
            curriculum_step(&env, actions);
            memset(actions, 0, sizeof(actions));
            actions[player].farmer = (KGUnitAction){
                want_animal ? KG_OP_FEED : KG_OP_WATER, -1, 1};
            curriculum_step(&env, actions);
            if (stage == KAG_CURRICULUM_ACQUIRE) {
                assert(kag_curriculum_success(&env));
                continue;
            }
            memset(actions, 0, sizeof(actions));
            actions[player].farmer = (KGUnitAction){KG_OP_HARVEST, -1, 1};
            curriculum_step(&env, actions);
            memset(actions, 0, sizeof(actions));
            actions[player].farmer = (KGUnitAction){KG_OP_DROP, -1, 1};
            curriculum_step(&env, actions);
            memset(actions, 0, sizeof(actions));
            actions[player].market[0] = (KGMarketOrder){
                KG_MARKET_SELL, target, 1};
            actions[player].market_count = 1;
            curriculum_step(&env, actions);
            assert(kag_curriculum_success(&env));
        }
    }

    /* Expansion begins on genuinely locked land. Buy land and a worker, then
     * start and maintain the branch on the newly unlocked tile. */
    for (int want_animal = 0; want_animal <= 1; want_animal++) {
        do {
            kg_reset(&env.game_storage);
            kag_curriculum_prepare(&env, KAG_CURRICULUM_EXPAND);
        } while (env.curriculum_branch != want_animal);
        target = env.curriculum_target_item;
        int input = want_animal
            ? KG_ITEM_GOOSE + target - KG_ITEM_EGG : target;
        memset(actions, 0, sizeof(actions));
        actions[player].market[0] = (KGMarketOrder){KG_MARKET_HIRE, -1, 1};
        actions[player].market[1] = (KGMarketOrder){KG_MARKET_BUY_LAND, -1, 1};
        actions[player].market_count = 2;
        curriculum_step(&env, actions);
        if (want_animal) {
            memset(actions, 0, sizeof(actions));
            actions[player].farmer = (KGUnitAction){KG_OP_BUILD_COOP, -1, 1};
            curriculum_step(&env, actions);
        }
        memset(actions, 0, sizeof(actions));
        actions[player].farmer = want_animal
            ? (KGUnitAction){KG_OP_PLACE, input, 1}
            : (KGUnitAction){KG_OP_PLANT, target, 1};
        curriculum_step(&env, actions);
        memset(actions, 0, sizeof(actions));
        actions[player].farmer = (KGUnitAction){
            want_animal ? KG_OP_FEED : KG_OP_WATER, -1, 1};
        curriculum_step(&env, actions);
        assert(kag_curriculum_success(&env));
    }
}

static int macro_action_has_unit_op(const KGAction* action, int op) {
    for (int unit = 0; unit <= action->hand_count; unit++) {
        const KGUnitAction* command = unit == 0
            ? &action->farmer : &action->hands[unit - 1];
        if (command->op == op) return 1;
    }
    return 0;
}

static int macro_action_unit_op_count(const KGAction* action, int op) {
    int count = 0;
    for (int unit = 0; unit <= action->hand_count; unit++) {
        const KGUnitAction* command = unit == 0
            ? &action->farmer : &action->hands[unit - 1];
        count += command->op == op;
    }
    return count;
}

static int macro_action_market_units(const KGAction* action, int op,
        int item) {
    int units = 0;
    for (int order = 0; order < action->market_count; order++) {
        const KGMarketOrder* candidate = &action->market[order];
        if (candidate->op == op && (item < 0 || candidate->item == item)) {
            units += candidate->n;
        }
    }
    return units;
}

static void assert_macro_targeted_plant_progress(void) {
    static const int quadrants[4] = {1, 2, 4, 8};
    static const int targets[4][2] = {
        {2, 2}, {7, 2}, {2, 7}, {7, 7},
    };
    for (int target = 0; target < 4; target++) {
        Env env = {0};
        KGConfig config;
        kg_config_default(&config);
        config.episode_steps = 720;
        kg_init(&env.game_storage, &config);
        KGPlayer* farm = &env.game_storage.players[0];
        farm->unlocked_mask = 15;

        /* Leave exactly one usable square in the selected quadrant. Filling
         * the rest with inert structures catches planners that apply their
         * specialist slot cap before filtering to the requested quadrant. */
        for (int tile = 0; tile < KG_MAX_TILES; tile++) {
            kg_set_player_tile(farm, tile, KG_TILE_COOP);
        }
        int tile = kg_tile_index(targets[target][0], targets[target][1]);
        kg_set_player_tile(farm, tile, KG_TILE_EMPTY);
        memset(farm->seeds, 0, sizeof(farm->seeds));
        farm->seeds[KG_WHEAT] = 1;
        farm->money = 0;
        farm->units[0].x = (uint8_t)targets[target][0];
        farm->units[0].y = (uint8_t)targets[target][1];

        int macro = KAG_MACRO_PLANT_BASE + KG_WHEAT;
        assert(kag_macro_candidate_legal(&env, 0, macro));
        KGAction generated = {0};
        kag_macro_action_ex(&env.game_storage, 0, macro, 1,
            quadrants[target], 1, &generated);
        assert(generated.farmer.op == KG_OP_PLANT);
        assert(generated.farmer.arg == KG_WHEAT);
        kg_apply_unit_action(&env.game_storage, farm, 0, &generated.farmer);
        assert(farm->tiles[tile].kind == KG_TILE_PLANT);
        assert(farm->tiles[tile].crop == KG_WHEAT);
    }
}

static void assert_macro_weed_quadrant_progress(void) {
    Env env = {0};
    KGConfig config;
    kg_config_default(&config);
    config.episode_steps = 720;
    kg_init(&env.game_storage, &config);
    KGPlayer* farm = &env.game_storage.players[0];
    farm->unlocked_mask = 15;
    for (int tile = 0; tile < KG_MAX_TILES; tile++) {
        kg_set_player_tile(farm, tile, KG_TILE_COOP);
    }
    int weed = kg_tile_index(2, 2);
    int outside = kg_tile_index(7, 2);
    kg_set_player_tile(farm, weed, KG_TILE_WEED);
    kg_set_player_tile(farm, outside, KG_TILE_EMPTY);
    farm->seeds[KG_WHEAT] = 1;
    farm->money = 0;
    farm->units[0].x = 2;
    farm->units[0].y = 2;
    int macro = KAG_MACRO_PLANT_BASE + KG_WHEAT;

    /* The selected quadrant has no empty tile yet, but its weed is
     * reclaimable. A legal targeted PLANT must not silently choose HOLD. */
    assert(kag_macro_candidate_legal(&env, 0, macro));
    KGAction generated = {0};
    kag_macro_action_ex(&env.game_storage, 0, macro, 1, 1, 1, &generated);
    assert(generated.farmer.op == KG_OP_DIG);
    kg_apply_unit_action(&env.game_storage, farm, 0, &generated.farmer);
    assert(farm->tiles[weed].kind == KG_TILE_EMPTY);

    memset(&generated, 0, sizeof(generated));
    kag_macro_action_ex(&env.game_storage, 0, macro, 1, 1, 1, &generated);
    assert(generated.farmer.op == KG_OP_PLANT);
    assert(generated.farmer.arg == KG_WHEAT);
    kg_apply_unit_action(&env.game_storage, farm, 0, &generated.farmer);
    assert(farm->tiles[weed].kind == KG_TILE_PLANT);
    assert(farm->tiles[weed].crop == KG_WHEAT);
}

static void assert_macro_low_price_future_plant(void) {
    Env env = {0};
    KGConfig config;
    kg_config_default(&config);
    /* Leave one tomato yield event after the current quote has collapsed. */
    config.episode_steps = 9 * config.turns_per_day;
    kg_init(&env.game_storage, &config);
    KGPlayer* farm = &env.game_storage.players[0];
    for (int tile = 0; tile < KG_MAX_TILES; tile++) {
        kg_set_player_tile(farm, tile, KG_TILE_COOP);
    }
    int tile = kg_tile_index(2, 2);
    kg_set_player_tile(farm, tile, KG_TILE_EMPTY);
    farm->seeds[KG_TOMATO] = 1;
    farm->money = 0;
    farm->units[0].x = 2;
    farm->units[0].y = 2;
    farm->unlocked_mask = 15;
    env.game_storage.market.inventory[KG_ITEM_TOMATO] = 100000;
    kg_refresh_prices(&env.game_storage);

    assert(env.game_storage.market.prices[KG_ITEM_TOMATO]
        < KG_CROP_DEFS[KG_TOMATO].seed_cost);
    assert(kag_macro_crop_events(&env.game_storage, KG_TOMATO) > 0);
    assert(kag_macro_candidate_score(&env, 0,
        KAG_MACRO_PLANT_BASE + KG_TOMATO) < 0.0f);
    assert(kag_macro_candidate_legal(&env, 0,
        KAG_MACRO_PLANT_BASE + KG_TOMATO));

    KGAction generated = {0};
    kag_macro_action_ex(&env.game_storage, 0,
        KAG_MACRO_PLANT_BASE + KG_TOMATO, 1, 1, 1, &generated);
    assert(generated.farmer.op == KG_OP_PLANT);
    assert(generated.farmer.arg == KG_TOMATO);
    kg_apply_unit_action(&env.game_storage, farm, 0, &generated.farmer);
    assert(farm->tiles[tile].kind == KG_TILE_PLANT);
}

static void assert_macro_animal_root_build_cap(void) {
    Env env = {0};
    KGConfig config;
    kg_config_default(&config);
    config.episode_steps = 720;
    kg_init(&env.game_storage, &config);
    KGPlayer* farm = &env.game_storage.players[0];
    farm->money = 10000;
    farm->hand_count = 3;
    farm->unit_count = 4;
    /* Put four workers on the structured profile's first four root slots;
     * its opening cow slots all require pasture. */
    static const int positions[4][2] = {
        {3, 4}, {4, 3}, {3, 3}, {2, 4},
    };
    for (int unit = 0; unit < farm->unit_count; unit++) {
        farm->units[unit].x = (uint8_t)positions[unit][0];
        farm->units[unit].y = (uint8_t)positions[unit][1];
    }

    int macro = KAG_MACRO_ANIMAL_BASE + KG_COW;
    assert(kag_macro_candidate_legal(&env, 0, macro));
    KGAction generated = {0};
    kag_macro_action_ex(&env.game_storage, 0, macro,
        2, 0, 1, &generated);
    assert(macro_action_unit_op_count(&generated, KG_OP_BUILD_PASTURE) == 2);
    assert(!macro_action_has_unit_op(&generated, KG_OP_BUILD_COOP));
    for (int unit = 0; unit <= generated.hand_count; unit++) {
        const KGUnitAction* command = unit == 0
            ? &generated.farmer : &generated.hands[unit - 1];
        assert(command->op != KG_OP_BUILD_COOP);
    }

    /* Once compatible capacity exists, the same intent owns exactly the
     * requested cow purchase: it cannot substitute another species or add
     * unrelated structures. */
    kg_init(&env.game_storage, &config);
    farm = &env.game_storage.players[0];
    farm->money = 10000;
    int first_pasture = kg_tile_index(3, 4);
    int second_pasture = kg_tile_index(4, 3);
    kg_set_player_tile(farm, first_pasture, KG_TILE_PASTURE);
    kg_set_player_tile(farm, second_pasture, KG_TILE_PASTURE);
    farm->units[0].x = 3;
    farm->units[0].y = 4;
    memset(&generated, 0, sizeof(generated));
    assert(kag_macro_candidate_legal(&env, 0, macro));
    kag_macro_action_ex(&env.game_storage, 0, macro,
        2, 0, 1, &generated);
    assert(macro_action_market_units(&generated,
        KG_MARKET_BUY_ANIMAL, KG_ITEM_COW) == 2);
    assert(macro_action_market_units(&generated,
        KG_MARKET_BUY_ANIMAL, -1) == 2);
    assert(!macro_action_has_unit_op(&generated, KG_OP_BUILD_COOP));
    assert(!macro_action_has_unit_op(&generated, KG_OP_BUILD_PASTURE));
    for (int order = 0; order < generated.market_count; order++) {
        if (generated.market[order].op == KG_MARKET_BUY_ANIMAL) {
            assert(generated.market[order].item == KG_ITEM_COW);
        }
    }
}

static void assert_macro_cow_stock_and_feed_reservation(void) {
    KGConfig config;
    kg_config_default(&config);
    config.episode_steps = 720;

    /* A COW(1) intent may consume at most one carried cow. A sheep backlog
     * must not cause it to place sheep or build extra pasture. */
    Env env = {0};
    kg_init(&env.game_storage, &config);
    KGPlayer* farm = &env.game_storage.players[0];
    farm->money = 10000;
    farm->hand_count = 3;
    farm->unit_count = 4;
    static const int pasture_positions[4][2] = {
        {3, 4}, {4, 3}, {3, 3}, {2, 4},
    };
    for (int unit = 0; unit < farm->unit_count; unit++) {
        farm->units[unit].x = (uint8_t)pasture_positions[unit][0];
        farm->units[unit].y = (uint8_t)pasture_positions[unit][1];
        farm->units[unit].inventory[KG_ITEM_COW] = 1;
        kg_set_player_tile(farm, kg_tile_index(
            pasture_positions[unit][0], pasture_positions[unit][1]),
            KG_TILE_PASTURE);
    }
    farm->shed[KG_ITEM_SHEEP] = 4;
    int macro = KAG_MACRO_ANIMAL_BASE + KG_COW;
    assert(kag_macro_candidate_legal(&env, 0, macro));
    KGAction generated = {0};
    kag_macro_action_ex(&env.game_storage, 0, macro,
        1, 0, 1, &generated);
    int selected_operations = macro_action_unit_op_count(
        &generated, KG_OP_PLACE)
        + macro_action_unit_op_count(&generated, KG_OP_PICKUP)
        + macro_action_unit_op_count(&generated, KG_OP_BUILD_COOP)
        + macro_action_unit_op_count(&generated, KG_OP_BUILD_PASTURE);
    assert(selected_operations <= 1);
    for (int unit = 0; unit <= generated.hand_count; unit++) {
        const KGUnitAction* command = unit == 0
            ? &generated.farmer : &generated.hands[unit - 1];
        if (command->op == KG_OP_PLACE || command->op == KG_OP_PICKUP) {
            assert(command->arg == KG_ITEM_COW);
        } else if (command->op == KG_OP_BUILD_COOP
                || command->op == KG_OP_BUILD_PASTURE) {
            assert(command->op == KG_OP_BUILD_PASTURE);
        }
    }

    /* Reserve the last shed slot for mandatory feed before accepting a new
     * cow. There is enough cash for wheat plus the cow, but not enough room
     * for both resulting purchases. */
    kg_init(&env.game_storage, &config);
    farm = &env.game_storage.players[0];
    int existing = kg_tile_index(0, 0);
    kg_set_player_tile(farm, existing, KG_TILE_COOP);
    kg_new_animal(farm, existing, KG_GOOSE, 0);
    farm->tiles[existing].fed_today = 0;
    int pasture = kg_tile_index(1, 0);
    kg_set_player_tile(farm, pasture, KG_TILE_PASTURE);
    farm->shed[KG_ITEM_CARROT] = config.shed_capacity - 1;
    farm->money = KG_ANIMAL_DEFS[KG_COW].cost
        + env.game_storage.market.prices[KG_ITEM_WHEAT];
    farm->units[0].x = 4;
    farm->units[0].y = 4;
    assert(kag_macro_feed_shortfall(&env.game_storage, 0) == 1);
    assert(!kag_macro_candidate_legal(&env, 0, macro));
    memset(&generated, 0, sizeof(generated));
    kag_macro_action_ex(&env.game_storage, 0, macro,
        1, 0, 1, &generated);
    assert(macro_action_market_units(&generated,
        KG_MARKET_BUY_PRODUCT, KG_ITEM_WHEAT) == 1);
    assert(macro_action_market_units(&generated,
        KG_MARKET_BUY_ANIMAL, KG_ITEM_COW) == 0);

    /* If a selected cow is picked up first, that committed state frees one
     * slot and makes one additional cow purchase feasible. */
    kg_init(&env.game_storage, &config);
    farm = &env.game_storage.players[0];
    farm->money = KG_ANIMAL_DEFS[KG_COW].cost;
    farm->shed[KG_ITEM_CARROT] = config.shed_capacity - 1;
    farm->shed[KG_ITEM_COW] = 1;
    kg_set_player_tile(farm, kg_tile_index(3, 4), KG_TILE_PASTURE);
    kg_set_player_tile(farm, kg_tile_index(4, 3), KG_TILE_PASTURE);
    farm->units[0].x = 4;
    farm->units[0].y = 4;
    assert(kag_macro_candidate_legal(&env, 0, macro));
    memset(&generated, 0, sizeof(generated));
    kag_macro_action_ex(&env.game_storage, 0, macro,
        2, 0, 1, &generated);
    assert(macro_action_unit_op_count(&generated, KG_OP_PICKUP) == 1);
    assert(macro_action_market_units(&generated,
        KG_MARKET_BUY_ANIMAL, KG_ITEM_COW) == 1);
    assert(macro_action_market_units(&generated,
        KG_MARKET_BUY_ANIMAL, -1) == 1);
    KGAction actions[KG_NUM_PLAYERS] = {0};
    actions[0] = generated;
    pass_action(&env.game_storage, 1, &actions[1]);
    kg_step(&env.game_storage, actions);
    assert(kg_shed_total(farm) == config.shed_capacity);
    assert(farm->units[0].inventory[KG_ITEM_COW] == 1);
}

static void assert_macro_full_shed_purchase_guards(void) {
    Env env = {0};
    KGConfig config;
    kg_config_default(&config);
    config.episode_steps = 720;
    kg_init(&env.game_storage, &config);
    KGPlayer* farm = &env.game_storage.players[0];
    farm->money = 10000;
    farm->shed[KG_ITEM_WHEAT] = config.shed_capacity;

    assert(!kag_macro_candidate_legal(&env, 0, KAG_MACRO_BUY_WHEAT));
    KGAction generated = {0};
    kag_macro_action_ex(&env.game_storage, 0, KAG_MACRO_BUY_WHEAT,
        1, 0, 1, &generated);
    assert(macro_action_market_units(&generated,
        KG_MARKET_BUY_PRODUCT, KG_ITEM_WHEAT) == 0);

    int coop = kg_tile_index(3, 4);
    kg_set_player_tile(farm, coop, KG_TILE_COOP);
    assert(!kag_macro_candidate_legal(&env, 0,
        KAG_MACRO_BUY_ANIMAL_BASE + KG_GOOSE));
    memset(&generated, 0, sizeof(generated));
    kag_macro_action_ex(&env.game_storage, 0,
        KAG_MACRO_BUY_ANIMAL_BASE + KG_GOOSE, 1, 0, 1, &generated);
    assert(macro_action_market_units(&generated,
        KG_MARKET_BUY_ANIMAL, KG_ITEM_GOOSE) == 0);
}

static void assert_macro_structured_interval_no_repeat(void) {
    Env env = {0};
    KGConfig config;
    obs_t observations[KG_NUM_PLAYERS * OBS_SIZE] = {0};
    float actions[KG_NUM_PLAYERS * NUM_ATNS] = {0};
    float rewards[KG_NUM_PLAYERS] = {0};
    float terminals[KG_NUM_PLAYERS] = {0};
    unsigned char masks[KG_NUM_PLAYERS * KG_POLICY_ACTION_MASK_SIZE] = {0};
    kg_config_default(&config);
    config.episode_steps = 720;
    kg_init(&env.game_storage, &config);
    env.num_agents = KG_NUM_PLAYERS;
    env.macro_mode = KAG_MACRO_MODE_STRUCTURED;
    env.macro_decision_interval = 4;
    for (int player = 0; player < KG_NUM_PLAYERS; player++) {
        env.agents[player].observations = observations + player * OBS_SIZE;
        env.agents[player].actions = actions + player * NUM_ATNS;
        env.agents[player].rewards = rewards + player;
        env.agents[player].terminals = terminals + player;
        env.agents[player].action_mask = masks
            + player * KG_POLICY_ACTION_MASK_SIZE;
    }
    puf_reset(&env);
    actions[0] = KAG_MACRO_BUY_WHEAT;
    actions[KAG_MACRO_QUANTITY_HEAD] = 4; /* quantity bin = 12 */
    puf_step(&env);
    assert(env.macro_intent[0] == KAG_MACRO_BUY_WHEAT);
    assert(env.macro_ticks[0] == 0);
    assert(env.game_storage.players[0].shed[KG_ITEM_WHEAT] == 12);

    /* With an interval configured above one, the structured quantity must
     * still be consumed once; the next decision can select a new intent. */
    memset(actions, 0, sizeof(actions));
    puf_step(&env);
    assert(env.game_storage.players[0].shed[KG_ITEM_WHEAT] == 12);
}

static void assert_macro_held_parameter_masks(void) {
    Env env = {0};
    KGConfig config;
    unsigned char mask[KG_POLICY_ACTION_MASK_SIZE] = {0};
    kg_config_default(&config);
    config.episode_steps = 720;
    kg_init(&env.game_storage, &config);
    env.macro_mode = KAG_MACRO_MODE_STRUCTURED;
    env.macro_intent[0] = KAG_MACRO_BUY_SEED_BASE + KG_WHEAT;
    env.macro_ticks[0] = 1;
    env.macro_quantity[0] = 12;
    env.macro_target[0] = 1;
    env.agents[0].action_mask = mask;
    kag_write_mask(&env, 0);

    /* While a sticky intent is executing, newly sampled parameter values are
     * ignored. Only one canonical action may remain exposed on those heads;
     * otherwise identical transitions acquire 8x5 policy aliases. */
    assert(mask[KAG_MACRO_HOLD] == 1);
    for (int macro = 1; macro < KAG_MACRO_COUNT; macro++) {
        assert(mask[macro] == 0);
    }
    const int heads[2] = {KAG_MACRO_QUANTITY_HEAD, KAG_MACRO_TARGET_HEAD};
    for (int index = 0; index < 2; index++) {
        int base = heads[index] * KG_POLICY_UNIT_COMMANDS;
        assert(mask[base] == 1);
        for (int option = 1; option < KG_POLICY_UNIT_COMMANDS; option++) {
            assert(mask[base + option] == 0);
        }
    }
}

static void assert_macro_harvest_maintain_separation(void) {
    Env env = {0};
    KGConfig config;
    kg_config_default(&config);
    config.episode_steps = 720;
    kg_init(&env.game_storage, &config);
    env.game_storage.step = 2 * config.turns_per_day;
    env.game_storage.day = 2;
    env.game_storage.hour = 0;
    KGPlayer* farm = &env.game_storage.players[0];

    int harvest_tile = kg_tile_index(0, 0);
    kg_new_plant(farm, harvest_tile, KG_WHEAT, 0,
        config.turns_per_day);
    farm->tiles[harvest_tile].watered_today = 1;
    farm->tiles[harvest_tile].yield_units = 1;
    farm->hand_count = 0;
    farm->unit_count = 1;
    farm->units[0].x = 0;
    farm->units[0].y = 0;
    assert(kag_macro_candidate_legal(&env, 0, KAG_MACRO_HARVEST));

    KGAction harvest = {0};
    kag_macro_action_ex(&env.game_storage, 0, KAG_MACRO_HARVEST,
        1, 0, 1, &harvest);

    /* Keep the two fixtures separate so worker routing cannot hide the
     * operation under test behind a movement command. */
    assert(macro_action_has_unit_op(&harvest, KG_OP_HARVEST));
    assert(!macro_action_has_unit_op(&harvest, KG_OP_WATER));
    assert(!macro_action_has_unit_op(&harvest, KG_OP_FEED));
    assert(!macro_action_has_unit_op(&harvest, KG_OP_CARE));
    assert(!macro_action_has_unit_op(&harvest, KG_OP_BUILD_COOP));
    assert(!macro_action_has_unit_op(&harvest, KG_OP_BUILD_PASTURE));
    assert(harvest.market_count == 0);

    kg_init(&env.game_storage, &config);
    farm = &env.game_storage.players[0];
    int maintain_tile = kg_tile_index(0, 0);
    kg_new_plant(farm, maintain_tile, KG_WHEAT, 0,
        config.turns_per_day);
    farm->tiles[maintain_tile].watered_today = 0;
    farm->tiles[maintain_tile].consecutive_unwatered = 1;
    farm->tiles[maintain_tile].yield_units = 0;
    farm->hand_count = 0;
    farm->unit_count = 1;
    farm->units[0].x = 0;
    farm->units[0].y = 0;
    assert(kag_macro_candidate_legal(&env, 0, KAG_MACRO_MAINTAIN));

    KGAction maintain = {0};
    kag_macro_action_ex(&env.game_storage, 0, KAG_MACRO_MAINTAIN,
        1, 0, 1, &maintain);
    assert(macro_action_has_unit_op(&maintain, KG_OP_WATER));
    assert(!macro_action_has_unit_op(&maintain, KG_OP_HARVEST));
    assert(!macro_action_has_unit_op(&maintain, KG_OP_BUILD_COOP));
    assert(!macro_action_has_unit_op(&maintain, KG_OP_BUILD_PASTURE));
    assert(maintain.market_count == 0);
}

static void assert_macro_sell_all_value_priority(void) {
    Env env = {0};
    KGConfig config;
    kg_config_default(&config);
    config.episode_steps = 720;
    kg_init(&env.game_storage, &config);
    KGPlayer* farm = &env.game_storage.players[0];
    farm->shed[KG_ITEM_WHEAT] = 10;
    farm->shed[KG_ITEM_MELON] = 10;
    assert(env.game_storage.market.prices[KG_ITEM_MELON]
        > env.game_storage.market.prices[KG_ITEM_WHEAT]);

    KGAction generated = {0};
    kag_macro_action_ex(&env.game_storage, 0, KAG_MACRO_SELL_ALL,
        2, 0, 1, &generated);
    assert(macro_action_market_units(
        &generated, KG_MARKET_SELL, -1) == 2);
    assert(macro_action_market_units(
        &generated, KG_MARKET_SELL, KG_ITEM_MELON) == 2);
    assert(macro_action_market_units(
        &generated, KG_MARKET_SELL, KG_ITEM_WHEAT) == 0);
    for (int order = 0; order < generated.market_count; order++) {
        assert(generated.market[order].op == KG_MARKET_SELL);
    }
}

static void assert_macro_endgame_animal_feed(void) {
    Env env = {0};
    KGConfig config;
    kg_config_default(&config);
    config.episode_steps = 720;
    kg_init(&env.game_storage, &config);
    env.game_storage.step = config.episode_steps
        - 2 * config.turns_per_day;
    env.game_storage.day = env.game_storage.step / config.turns_per_day;
    env.game_storage.hour = 0;
    KGPlayer* farm = &env.game_storage.players[0];
    int tile = kg_tile_index(0, 0);
    kg_new_animal(farm, tile, KG_COW, env.game_storage.day);
    farm->tiles[tile].fed_today = 0;
    farm->tiles[tile].cared_today = 0;
    farm->shed[KG_ITEM_WHEAT] = 0;
    farm->units[0].inventory[KG_ITEM_WHEAT] = 0;
    farm->units[0].x = 0;
    farm->units[0].y = 0;
    assert(kag_macro_liquidation_window(&env.game_storage));
    assert(kag_macro_candidate_legal(&env, 0, KAG_MACRO_MAINTAIN));

    KGAction generated = {0};
    kag_macro_action_ex(&env.game_storage, 0, KAG_MACRO_MAINTAIN,
        1, 0, 1, &generated);
    assert(macro_action_market_units(&generated,
        KG_MARKET_BUY_PRODUCT, KG_ITEM_WHEAT) == 1);
    for (int order = 0; order < generated.market_count; order++) {
        assert(generated.market[order].op == KG_MARKET_BUY_PRODUCT);
        assert(generated.market[order].item == KG_ITEM_WHEAT);
    }
}

static void assert_macro_locked_shed_escape(void) {
    KGPlayer farm = {0};
    farm.unlocked_mask = 1; /* NW only */
    KGUnitState unit = {0};
    unit.x = 5;
    unit.y = 5;
    assert(kag_bot_route(&farm, &unit, 0, 0) == KG_OP_NORTH);
    unit.y = 4;
    assert(kag_bot_route(&farm, &unit, 0, 0) == KG_OP_WEST);
}

static void assert_macro_maintain_requires_progress(void) {
    Env env = {0};
    KGConfig config;
    kg_config_default(&config);
    config.episode_steps = 720;
    kg_init(&env.game_storage, &config);
    KGPlayer* farm = &env.game_storage.players[0];
    int tile = kg_tile_index(0, 0);
    kg_new_animal(farm, tile, KG_COW, env.game_storage.day);
    farm->tiles[tile].fed_today = 0;
    farm->tiles[tile].cared_today = 1;
    farm->money = 0;
    farm->shed[KG_ITEM_CARROT] = config.shed_capacity;
    assert(!kag_macro_candidate_legal(&env, 0, KAG_MACRO_MAINTAIN));

    farm->shed[KG_ITEM_CARROT]--;
    farm->shed[KG_ITEM_WHEAT] = 1;
    assert(kag_macro_candidate_legal(&env, 0, KAG_MACRO_MAINTAIN));
}

static void assert_macro_hire_respects_policy_cap(void) {
    Env env = {0};
    KGConfig config;
    kg_config_default(&config);
    config.episode_steps = 720;
    kg_init(&env.game_storage, &config);
    KGPlayer* farm = &env.game_storage.players[0];
    farm->money = 100000;
    farm->seeds[KG_WHEAT] = 64; /* enough visible work to desire many hands */
    farm->hand_count = 1;
    farm->unit_count = 2;

    KGAction generated = {0};
    kag_macro_action_ex_limit(&env.game_storage, 0, KAG_MACRO_HIRE,
        64, 0, 1, 2, &generated);
    assert(macro_action_market_units(
        &generated, KG_MARKET_HIRE, KG_ITEM_INVALID) == 1);
}

static void assert_macro_diversify_preserves_plan(void) {
    Env env = {0};
    KGConfig config;
    kg_config_default(&config);
    config.episode_steps = 720;
    kg_init(&env.game_storage, &config);
    KGAction generated = {0};
    kag_macro_action_ex(&env.game_storage, 0, KAG_MACRO_DIVERSIFY,
        1, 0, 1, &generated);
    int active = generated.market_count > 0
        || generated.farmer.op != KG_OP_PASS;
    for (int hand = 0; hand < generated.hand_count; hand++) {
        active |= generated.hands[hand].op != KG_OP_PASS;
    }
    assert(active);
}

static void assert_macro_marginal_buy_quote(void) {
    Env env = {0};
    KGConfig config;
    kg_config_default(&config);
    config.episode_steps = 720;
    kg_init(&env.game_storage, &config);
    KGPlayer* farm = &env.game_storage.players[0];
    int inventory = env.game_storage.market.inventory[KG_ITEM_WHEAT];
    int displayed = env.game_storage.market.prices[KG_ITEM_WHEAT];
    int marginal = kg_market_price(KG_ITEM_WHEAT, inventory - 1);
    assert(marginal >= displayed);
    farm->money = marginal - 1;
    assert(!kag_macro_candidate_legal(&env, 0, KAG_MACRO_BUY_WHEAT));

    int tile = kg_tile_index(0, 0);
    kg_new_animal(farm, tile, KG_COW, env.game_storage.day);
    farm->tiles[tile].fed_today = 0;
    farm->tiles[tile].cared_today = 1;
    assert(!kag_macro_candidate_legal(&env, 0, KAG_MACRO_MAINTAIN));
    farm->money = marginal;
    assert(kag_macro_candidate_legal(&env, 0, KAG_MACRO_BUY_WHEAT));
    assert(kag_macro_candidate_legal(&env, 0, KAG_MACRO_MAINTAIN));
}

static void assert_macro_diversify_feed_priority_and_legality(void) {
    KGAction action = {0};
    action.market_count = 3;
    action.market[0] = (KGMarketOrder){KG_MARKET_BUY_ANIMAL,
        KG_ITEM_COW, 1};
    action.market[1] = (KGMarketOrder){KG_MARKET_BUY_PRODUCT,
        KG_ITEM_WHEAT, 1};
    action.market[2] = (KGMarketOrder){KG_MARKET_BUY_SEED,
        KG_WHEAT, 1};
    kag_macro_prioritize_feed(&action);
    assert(action.market[0].op == KG_MARKET_BUY_PRODUCT);
    assert(action.market[0].item == KG_ITEM_WHEAT);
    assert(action.market[1].op == KG_MARKET_BUY_ANIMAL);
    assert(action.market[2].op == KG_MARKET_BUY_SEED);

    Env env = {0};
    KGConfig config;
    kg_config_default(&config);
    config.episode_steps = 720;
    kg_init(&env.game_storage, &config);
    KGPlayer* farm = &env.game_storage.players[0];
    farm->money = 0;
    farm->hand_count = 0;
    farm->unit_count = 1;
    memset(farm->shed, 0, sizeof(farm->shed));
    memset(farm->seeds, 0, sizeof(farm->seeds));
    kg_set_player_tile(farm, kg_tile_index(0, 0), KG_TILE_PASTURE);
    assert(!kag_macro_candidate_legal(&env, 0, KAG_MACRO_DIVERSIFY));
    farm->shed[KG_ITEM_COW] = 1;
    assert(!kag_macro_candidate_legal(&env, 0, KAG_MACRO_DIVERSIFY));

    farm->shed[KG_ITEM_COW] = 0;
    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 5; x++) {
            kg_set_player_tile(farm, kg_tile_index(x, y), KG_TILE_PASTURE);
        }
    }
    kg_set_player_tile(farm, kg_tile_index(1, 0), KG_TILE_EMPTY);
    farm->seeds[KG_TOMATO] = 1;
    assert(!kag_macro_candidate_legal(&env, 0, KAG_MACRO_DIVERSIFY));

    farm->seeds[KG_TOMATO] = 0;
    kg_new_plant(farm, kg_tile_index(1, 0), KG_TOMATO,
        env.game_storage.day, config.turns_per_day);
    farm->tiles[kg_tile_index(1, 0)].watered_today = 1;
    assert(!kag_macro_candidate_legal(&env, 0, KAG_MACRO_DIVERSIFY));
}

static void assert_native_macro_mode(void) {
    Env env = {0};
    KGConfig config;
    obs_t observations[KG_NUM_PLAYERS * OBS_SIZE] = {0};
    float actions[KG_NUM_PLAYERS * NUM_ATNS] = {0};
    float rewards[KG_NUM_PLAYERS] = {0};
    float terminals[KG_NUM_PLAYERS] = {0};
    unsigned char masks[KG_NUM_PLAYERS * KG_POLICY_ACTION_MASK_SIZE] = {0};
    kg_config_default(&config);
    /* A wheat planting macro is only legal when the crop can mature before
       terminal.  Keep the fixture at a full season rather than making the
       test accidentally exercise the legality fallback to HOLD. */
    config.episode_steps = 720;
    config.seed = 17;
    kg_init(&env.game_storage, &config);
    env.num_agents = KG_NUM_PLAYERS;
    env.policy_market_slots = KG_POLICY_MARKET_SLOTS;
    env.policy_max_hands = KG_POLICY_DIRECT_HANDS;
    env.macro_mode = 1;
    env.macro_decision_interval = 2;
    env.macro_score_scale = 10000.0f;
    for (int player = 0; player < KG_NUM_PLAYERS; player++) {
        env.agents[player].observations = observations + player * OBS_SIZE;
        env.agents[player].actions = actions + player * NUM_ATNS;
        env.agents[player].rewards = rewards + player;
        env.agents[player].terminals = terminals + player;
        env.agents[player].action_mask = masks
            + player * KG_POLICY_ACTION_MASK_SIZE;
    }
    puf_reset(&env);
    assert(masks[KAG_MACRO_HOLD] == 1);
    assert((observations[KAG_MACRO_OBS_OFFSET] & 128) != 0);
    for (int macro = KAG_MACRO_RESERVED_BASE; macro < KAG_MACRO_COUNT; macro++) {
        assert(masks[macro] == 0);
    }
    /* Select a valid macro at a decision boundary, then ensure the following
     * held turn ignores a changed selector and still has a well-formed mask. */
    actions[0] = (float)(KAG_MACRO_PLANT_BASE + KG_WHEAT);
    puf_step(&env);
    assert(env.macro_intent[0] == KAG_MACRO_PLANT_BASE + KG_WHEAT);
    assert(env.macro_ticks[0] == 1);
    assert(masks[KAG_MACRO_HOLD] == 1);
    actions[0] = (float)KAG_MACRO_SELL_ALL;
    puf_step(&env);
    assert(env.macro_intent[0] == KAG_MACRO_PLANT_BASE + KG_WHEAT);
    assert(env.macro_ticks[0] == 0);
    assert(masks[KAG_MACRO_HOLD] == 1);
}

static void assert_native_structured_macro_mode(void) {
    Env env = {0};
    KGConfig config;
    obs_t observations[KG_NUM_PLAYERS * OBS_SIZE] = {0};
    float actions[KG_NUM_PLAYERS * NUM_ATNS] = {0};
    float rewards[KG_NUM_PLAYERS] = {0};
    float terminals[KG_NUM_PLAYERS] = {0};
    unsigned char masks[KG_NUM_PLAYERS * KG_POLICY_ACTION_MASK_SIZE] = {0};
    kg_config_default(&config);
    config.episode_steps = 720;
    config.seed = 23;
    kg_init(&env.game_storage, &config);
    env.num_agents = KG_NUM_PLAYERS;
    env.policy_market_slots = KG_POLICY_MARKET_SLOTS;
    env.policy_max_hands = KG_POLICY_DIRECT_HANDS;
    env.macro_mode = KAG_MACRO_MODE_STRUCTURED;
    env.macro_decision_interval = 1;
    env.macro_score_scale = 10000.0f;
    for (int player = 0; player < KG_NUM_PLAYERS; player++) {
        env.agents[player].observations = observations + player * OBS_SIZE;
        env.agents[player].actions = actions + player * NUM_ATNS;
        env.agents[player].rewards = rewards + player;
        env.agents[player].terminals = terminals + player;
        env.agents[player].action_mask = masks
            + player * KG_POLICY_ACTION_MASK_SIZE;
    }
    puf_reset(&env);

    unsigned char* quantity_mask = masks
        + KAG_MACRO_QUANTITY_HEAD * KG_POLICY_UNIT_COMMANDS;
    unsigned char* target_mask = masks
        + KAG_MACRO_TARGET_HEAD * KG_POLICY_UNIT_COMMANDS;
    for (int bin = 0; bin < KAG_MACRO_QUANTITY_BINS; bin++) {
        assert(quantity_mask[bin] == 1);
    }
    assert(quantity_mask[KAG_MACRO_QUANTITY_BINS] == 0);
    assert(target_mask[0] == 1); /* AUTO */
    assert(target_mask[1] == 1); /* unlocked NW */
    for (int bin = 2; bin < KAG_MACRO_TARGET_BINS; bin++) {
        assert(target_mask[bin] == 0);
    }
    assert(target_mask[KAG_MACRO_TARGET_BINS] == 0);

    /* Mode 2 decodes the selector, quantity, and target independently. */
    actions[0] = (float)(KAG_MACRO_PLANT_BASE + KG_WHEAT);
    actions[KAG_MACRO_QUANTITY_HEAD] = 6.0f; /* 32 */
    actions[KAG_MACRO_TARGET_HEAD] = 1.0f;   /* NW */
    puf_step(&env);
    assert(env.macro_intent[0] == KAG_MACRO_PLANT_BASE + KG_WHEAT);
    assert(env.macro_quantity[0] == 32);
    assert(env.macro_target[0] == 1);
    assert(observations[KAG_MACRO_OBS_OFFSET + KAG_MACRO_COUNT + 2]
        == kag_u8_scale(32, 64));
    assert(observations[KAG_MACRO_OBS_OFFSET + KAG_MACRO_COUNT + 3]
        == kag_u8_scale(1, 8));

    /* A parameterized purchase emits the requested quantity, rather than the
     * old fixed ten-seed/one-animal constants. */
    puf_reset(&env);
    KGAction generated = {0};
    kag_macro_action_ex(&env.game_storage, 0,
        KAG_MACRO_BUY_SEED_BASE + KG_TOMATO, 12, 4, 1, &generated);
    int found = 0;
    for (int order = 0; order < generated.market_count; order++) {
        if (generated.market[order].op == KG_MARKET_BUY_SEED
                && generated.market[order].item == KG_TOMATO) {
            assert(generated.market[order].n == 12);
            found = 1;
        }
    }
    assert(found);

    /* Structured quantities are clipped to physical farm capacity. This is
     * the regression for the 700-unit purchase / $39k unused-seed failure:
     * existing seed stock leaves room for exactly three more units. */
    puf_reset(&env);
    KGPlayer* farm = &env.game_storage.players[0];
    for (int crop = 0; crop < KG_NUM_CROPS; crop++) farm->seeds[crop] = 0;
    int soil = kag_macro_reclaimable_tiles(farm);
    assert(soil > 3);
    farm->seeds[KG_WHEAT] = soil - 3;
    memset(&generated, 0, sizeof(generated));
    kag_macro_action_ex(&env.game_storage, 0,
        KAG_MACRO_BUY_SEED_BASE + KG_TOMATO, 64, 0, 1, &generated);
    found = 0;
    for (int order = 0; order < generated.market_count; order++) {
        if (generated.market[order].op == KG_MARKET_BUY_SEED
                && generated.market[order].item == KG_TOMATO) {
            assert(generated.market[order].n == 3);
            found = 1;
        }
    }
    assert(found);

    /* A hire is one-day labor. Visible work permits one hand here, and a
     * requested quantity of 64 must not buy the whole Fibonacci ladder. */
    puf_reset(&env);
    farm = &env.game_storage.players[0];
    for (int crop = 0; crop < KG_NUM_CROPS; crop++) farm->seeds[crop] = 0;
    farm->seeds[KG_WHEAT] = 8;
    assert(kag_macro_desired_hands(farm) == 1);
    assert(kag_macro_candidate_score(&env, 0, KAG_MACRO_HIRE) <= 250.0f);
    memset(&generated, 0, sizeof(generated));
    kag_macro_action_ex(&env.game_storage, 0,
        KAG_MACRO_HIRE, 64, 0, 1, &generated);
    int hires = 0;
    for (int order = 0; order < generated.market_count; order++) {
        hires += generated.market[order].op == KG_MARKET_HIRE;
    }
    assert(hires == 1);

    /* Land represents multiple productive slots. It is unattractive on an
     * empty root plot, then becomes valuable once that plot is crowded. */
    puf_reset(&env);
    farm = &env.game_storage.players[0];
    farm->money = 10000;
    float empty_land_score = kag_macro_candidate_score(
        &env, 0, KAG_MACRO_EXPAND);
    for (int tile = 0; tile < KG_MAX_TILES; tile++) {
        if (farm->tiles[tile].kind == KG_TILE_EMPTY
                || farm->tiles[tile].kind == KG_TILE_WEED) {
            farm->tiles[tile].kind = KG_TILE_PLANT;
            farm->tiles[tile].crop = KG_WHEAT;
        }
    }
    float crowded_land_score = kag_macro_candidate_score(
        &env, 0, KAG_MACRO_EXPAND);
    assert(empty_land_score < 0.0f);
    assert(crowded_land_score > empty_land_score);
    assert(crowded_land_score > 0.0f);

    /* The final two days are liquidation-only for new capital. SELL/CASH_OUT
     * stays available, while land, seeds, animals, hires, and diversification
     * cannot consume freshly realized cash again. */
    puf_reset(&env);
    farm = &env.game_storage.players[0];
    env.game_storage.step = env.game_storage.config.episode_steps
        - env.game_storage.config.turns_per_day;
    env.game_storage.day = env.game_storage.step
        / env.game_storage.config.turns_per_day;
    env.game_storage.hour = env.game_storage.step
        % env.game_storage.config.turns_per_day;
    farm->shed[KG_ITEM_WHEAT] = 5;
    kag_write_mask(&env, 0);
    assert(masks[KAG_MACRO_CASH_OUT] == 1);
    assert(masks[KAG_MACRO_EXPAND] == 0);
    assert(masks[KAG_MACRO_BUY_SEED_BASE + KG_WHEAT] == 0);
    assert(masks[KAG_MACRO_BUY_ANIMAL_BASE + KG_COW] == 0);
    assert(masks[KAG_MACRO_HIRE] == 0);
    assert(masks[KAG_MACRO_DIVERSIFY] == 0);
    memset(&generated, 0, sizeof(generated));
    kag_macro_action_ex(&env.game_storage, 0,
        KAG_MACRO_BUY_SEED_BASE + KG_WHEAT, 64, 0, 1, &generated);
    for (int order = 0; order < generated.market_count; order++) {
        assert(generated.market[order].op != KG_MARKET_BUY_SEED);
    }
}

int main(void) {
    assert_progress_potential_reward();
    assert_expansion_curriculum_reward();
    assert_tagged_curriculum_states();
    assert_tagged_curriculum_solutions();
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
    assert_macro_targeted_plant_progress();
    assert_macro_weed_quadrant_progress();
    assert_macro_low_price_future_plant();
    assert_macro_animal_root_build_cap();
    assert_macro_cow_stock_and_feed_reservation();
    assert_macro_full_shed_purchase_guards();
    assert_macro_structured_interval_no_repeat();
    assert_macro_held_parameter_masks();
    assert_macro_harvest_maintain_separation();
    assert_macro_sell_all_value_priority();
    assert_macro_endgame_animal_feed();
    assert_macro_locked_shed_escape();
    assert_macro_maintain_requires_progress();
    assert_macro_hire_respects_policy_cap();
    assert_macro_diversify_preserves_plan();
    assert_macro_marginal_buy_quote();
    assert_macro_diversify_feed_priority_and_legality();
    assert_native_macro_mode();
    assert_native_structured_macro_mode();
    Env env = {0};
    env.reward_potential_scale = 0.0f;
    env.reward_potential_gamma = 0.9997f;
    env.reward_cash_scale = 0.0f;
    env.reward_money_scale = 1.0f;
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
    assert(rewards[0] == 0.0f);
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
    float expected_return = (env.log.money - config.starting_money)
        / config.starting_money;
    assert(fabsf(env.log.episode_return - expected_return) < 1e-5f);
    puf_close(&env);
    printf("Kaggriculture adapter: PASS\n");
    return 0;
}
