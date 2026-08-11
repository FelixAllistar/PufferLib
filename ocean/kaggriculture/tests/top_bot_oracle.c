#include "../kaggriculture.h"

void kg_top_policy_input(const KGState* game, int player,
        uint8_t observation[OBS_SIZE], uint8_t mask[KG_POLICY_ACTION_MASK_SIZE]) {
    Env env = {};
    obs_t local_observation[OBS_SIZE] = {};
    uint8_t local_mask[KG_POLICY_ACTION_MASK_SIZE] = {};
    env.game_storage = *game;
    env.agents[player].observations = local_observation;
    env.agents[player].action_mask = local_mask;
    kag_write_observation(&env, player);
    kag_write_mask(&env, player);
    memcpy(observation, local_observation, sizeof(local_observation));
    memcpy(mask, local_mask, sizeof(local_mask));
}

void kg_top_bot_action(const KGState* game, int player, KGAction* action) {
    if (game->step < 26) {
        kag_script_action(game, player, KG_SCRIPT_TOP, action);
        kag_script_repair(game, player, KG_SCRIPT_TOP, action);
    } else {
        kag_bot_action(game, player, -1, action);
    }
}

void kg_top_projected_action(const KGState* game, int player,
        KGAction* action) {
    KGAction rich;
    kg_top_bot_action(game, player, &rich);
    kag_compact_animal_repair(game, player, &rich);
    float heads[NUM_ATNS] = {0};
    Agent policy = {.actions = heads};
    kag_set_policy_unit(&policy, 0,
        rich.farmer.op, rich.farmer.arg, rich.farmer.n);
    for (int hand = 0; hand < rich.hand_count
            && hand < KG_POLICY_DIRECT_HANDS; hand++) {
        kag_set_policy_unit(&policy, hand + 1, rich.hands[hand].op,
            rich.hands[hand].arg, rich.hands[hand].n);
    }
    for (int order = 0; order < rich.market_count
            && order < KG_POLICY_MARKET_SLOTS; order++) {
        kag_set_policy_market(&policy, order, rich.market[order].op,
            rich.market[order].item, rich.market[order].n);
    }
    kag_decode_action(action, &policy, game, player);
}
