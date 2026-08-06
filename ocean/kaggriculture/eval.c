#include "kaggriculture_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const int FIRST_YIELD_DAY[KG_NUM_CROPS] = {2, 2, 8, 10, 10};

static int eval_is_animal_tile(const KGTile* tile) {
    return tile->animal >= 0 && tile->animal < KG_NUM_ANIMALS
        && (tile->kind == KG_TILE_COOP || tile->kind == KG_TILE_PASTURE);
}

static int eval_arg(int argc, char** argv, const char* flag, int fallback) {
    for (int i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], flag) == 0) return atoi(argv[i + 1]);
    }
    return fallback;
}

static void eval_reset_action(KGAction* action) {
    memset(action, 0, sizeof(*action));
    action->farmer.op = KG_OP_PASS;
    action->farmer.arg = KG_CROP_INVALID;
    for (int hand = 0; hand < KG_MAX_HANDS; hand++) {
        action->hands[hand].op = KG_OP_PASS;
        action->hands[hand].arg = KG_CROP_INVALID;
    }
}

static void eval_market(KGAction* action, int op, int item, int n) {
    if (action->market_count >= KG_MAX_MARKET_ORDERS) return;
    action->market[action->market_count++] = (KGMarketOrder){op, item, n};
}

static void eval_worker(KGState* state, int player_id, int worker_id,
        KGUnitAction* action) {
    KGPlayer* player = &state->players[player_id];
    KGUnitState* worker = &player->units[worker_id];
    KGTile* tile = &player->tiles[worker->y * KG_MAX_BOARD_SIZE + worker->x];
    action->op = KG_OP_PASS;
    action->arg = KG_CROP_INVALID;
    action->n = 1;

    if (tile->kind == KG_TILE_PLANT && tile->yield_units > 0
            && state->day - tile->planted_day >= FIRST_YIELD_DAY[tile->crop]) {
        action->op = KG_OP_HARVEST;
    } else if (tile->kind == KG_TILE_PLANT && !tile->watered_today) {
        action->op = KG_OP_WATER;
    } else if (eval_is_animal_tile(tile) && !tile->fed_today
            && worker->inventory[KG_ITEM_WHEAT] > 0) {
        action->op = KG_OP_FEED;
    } else if (eval_is_animal_tile(tile) && !tile->cared_today) {
        action->op = KG_OP_CARE;
    } else if (eval_is_animal_tile(tile) && tile->fertilizer_available) {
        action->op = KG_OP_COLLECT_FERTILIZER;
    } else if (tile->kind == KG_TILE_EMPTY && player->seeds[KG_WHEAT] > 0) {
        action->op = KG_OP_PLANT;
        action->arg = KG_WHEAT;
    }
}

static void eval_starter(KGState* state, int player_id, KGAction* action) {
    KGPlayer* player = &state->players[player_id];
    eval_reset_action(action);
    action->hand_count = player->hand_count;
    if (action->hand_count > KG_MAX_HANDS) action->hand_count = KG_MAX_HANDS;
    for (int worker = 0; worker < action->hand_count + 1; worker++) {
        KGUnitAction* unit_action = worker == 0 ? &action->farmer : &action->hands[worker - 1];
        eval_worker(state, player_id, worker, unit_action);
    }

    /* This is intentionally a native baseline, not a Python starter clone.
     * It buys seed in one order and sells stored wheat in a second order so
     * the evaluator exercises ordered market processing too. */
    action->market_count = 0;
    if (player->seeds[KG_WHEAT] == 0 && player->money >= 10) {
        eval_market(action, KG_MARKET_BUY_SEED, KG_WHEAT, 1);
    }
    if (player->shed[KG_ITEM_WHEAT] > 0) {
        eval_market(action, KG_MARKET_SELL, KG_ITEM_WHEAT,
            player->shed[KG_ITEM_WHEAT]);
    }
}

static void eval_pass(KGState* state, int player_id, KGAction* action) {
    (void)state;
    (void)player_id;
    eval_reset_action(action);
}

int main(int argc, char** argv) {
    KGConfig config;
    KGState* state;
    int episodes = eval_arg(argc, argv, "--episodes", 32);
    int wins = 0;
    int draws = 0;
    long long total_a_money = 0;
    long long total_b_money = 0;
    clock_t start;

    kg_config_default(&config);
    config.seed = (uint64_t)eval_arg(argc, argv, "--seed", 7);
    config.episode_steps = eval_arg(argc, argv, "--steps", config.episode_steps);
    state = kg_create(&config);
    if (state == NULL) {
        fprintf(stderr, "failed to allocate native evaluator state\n");
        return 1;
    }

    start = clock();
    for (int episode = 0; episode < episodes; episode++) {
        KGAction actions[KG_NUM_PLAYERS];
        kg_reset(state);
        while (!kg_done(state)) {
            eval_starter(state, 0, &actions[0]);
            eval_pass(state, 1, &actions[1]);
            kg_step(state, actions);
        }
        int a_money = state->players[0].money;
        int b_money = state->players[1].money;
        total_a_money += a_money;
        total_b_money += b_money;
        if (a_money > b_money) wins++;
        else if (a_money == b_money) draws++;
    }
    double elapsed = (double)(clock() - start) / (double)CLOCKS_PER_SEC;
    int losses = episodes - wins - draws;
    printf("native eval: episodes=%d steps=%d A_wins=%d draws=%d B_wins=%d "
        "avg_money=(%lld,%lld) agent-steps/s=%.0f\n", episodes,
        config.episode_steps, wins, draws, losses,
        total_a_money / episodes, total_b_money / episodes,
        (double)(episodes * config.episode_steps * KG_NUM_PLAYERS) / elapsed);
    kg_destroy(state);
    return 0;
}
