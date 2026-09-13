#include <assert.h>
#include "../kaggriculture.h"

static void fill_plot(Env* env, int player_id, int quadrant) {
    KGState* game = &env->game_storage;
    KGPlayer* farm = &game->players[player_id];
    for (int y = 0; y < game->config.board_size; y++) {
        for (int x = 0; x < game->config.board_size; x++) {
            if (kg_quadrant(x, y, game->config.board_size) == quadrant)
                kg_new_plant(farm, kg_tile_index(x, y), KG_WHEAT,
                    game->day, game->config.turns_per_day);
        }
    }
}

static void assert_land_mask(Env* env, int player, int expected) {
    unsigned char mask[KG_POLICY_ACTION_MASK_SIZE];
    env->agents[player].action_mask = mask;
    kag_write_mask(env, player);
    for (int slot = 0; slot < KG_POLICY_MARKET_SLOTS; slot++)
        assert(kag_market_slot_mask(mask, slot)
            [KG_POLICY_MARKET_CONTINUE_ACTIONS + KG_M_LAND] == expected);
    env->agents[player].action_mask = NULL;
}

int main(void) {
    Env* env = calloc(1, sizeof(*env));
    KGConfig config;
    kg_config_default(&config);
    kg_init(&env->game_storage, &config);
    KGState* game = &env->game_storage;
    KGPlayer* farm = &game->players[0];
    env->macro_mode = KAG_MACRO_MODE_TASKS;
    env->frozen_macro_mode = -1;
    env->land_buy_min_days = 2;
    farm->money = 20000;

    /* Neither elapsed game time nor 24/25 occupied tiles starts the clock. */
    game->step = 100;
    assert_land_mask(env, 0, 0);
    assert(env->land_fill_step[0] == -1);
    fill_plot(env, 0, 1);
    kg_set_player_tile(farm, 0, KG_TILE_EMPTY);
    assert_land_mask(env, 0, 0);
    kg_set_player_tile(farm, 0, KG_TILE_COOP);
    assert_land_mask(env, 0, 0);
    kg_set_player_tile(farm, 0, KG_TILE_PASTURE);
    assert_land_mask(env, 0, 0);
    kg_set_player_tile(farm, 0, KG_TILE_WEED);
    assert_land_mask(env, 0, 0);
    assert(env->land_fill_step[0] == -1);

    /* Mixed crops/animals qualify. Use an intra-day start, not midnight. */
    kg_new_animal(farm, 0, KG_GOOSE, game->day);
    game->step = 103;
    assert_land_mask(env, 0, 0);
    assert(env->land_fill_step[0] == 103);
    game->step = 103 + 47;
    assert_land_mask(env, 0, 0);
    game->step++;
    assert_land_mask(env, 0, 1);
    assert_land_mask(env, 1, 0);

    /* No restart after harvesting/death; this is a first-full timestamp. */
    kg_set_player_tile(farm, 0, KG_TILE_EMPTY);
    assert_land_mask(env, 0, 1);
    assert(env->land_fill_step[0] == 103);

    /* Reject direct requests while gated and duplicate queued purchases. */
    KGAction action = {0};
    action.market_count = 3;
    for (int i = 0; i < 3; i++)
        action.market[i] = (KGMarketOrder){KG_MARKET_BUY_LAND, KG_ITEM_INVALID, 1};
    kag_apply_policy_limits(env, farm, &action);
    assert(action.market_count == 1);
    kg_do_buy_land(game, farm);
    assert(farm->unlocked_mask == 3);
    action.market_count = 1;
    kag_apply_policy_limits(env, farm, &action);
    assert(action.market_count == 0);
    assert_land_mask(env, 0, 0);
    assert(env->land_fill_step[0] == -1);

    /* The newest plot gets its own clock, even if earlier crops are gone. */
    fill_plot(env, 0, 2);
    assert_land_mask(env, 0, 0);
    assert(env->land_fill_step[0] == 151);
    game->step += 48;
    assert_land_mask(env, 0, 1);
    farm->money = 0;
    assert_land_mask(env, 0, 0); /* Existing affordability still applies. */
    farm->money = 20000;

    /* Reset/saved-state starts never inherit a previous episode's timestamp.
     * If already full on arrival, start at the observed reset step. */
    kag_reset_land_buy_delay(env, 0);
    assert_land_mask(env, 0, 0);
    assert(env->land_fill_step[0] == game->step);
    game->config.turns_per_day = 10;
    game->step += 19;
    assert_land_mask(env, 0, 0);
    game->step++;
    assert_land_mask(env, 0, 1);

    /* Zero removes both the fill requirement and the wait/queue constraint. */
    kg_do_buy_land(game, farm);
    env->land_buy_min_days = 0;
    assert_land_mask(env, 0, 1);
    action.market_count = 3;
    kag_apply_policy_limits(env, farm, &action);
    assert(action.market_count == 3);
    env->land_buy_min_days = 2;
    env->macro_mode = KAG_MACRO_MODE_STRUCTURED;
    assert(!kag_macro_candidate_legal(env, 0, KAG_MACRO_EXPAND));

    free(env);
    puts("land buy delay: fill detection, 48-turn boundary, reset, seats, queues, zero-disable PASS");
    return 0;
}
