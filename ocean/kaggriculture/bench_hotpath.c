/* Microbench: env hot-path components for Kaggriculture.
 *
 *   make -C ocean/kaggriculture hotpath
 *
 * Wall-clock via clock_gettime. No training-loop noise.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "kaggriculture.h"

enum { BENCH_ITERS = 20000, BENCH_WARMUP = 200 };

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

static void densify(Env* env) {
    KGPlayer* farm = &env->game_storage.players[0];
    farm->money = 20000;
    farm->seeds[KG_WHEAT] = 50;
    farm->seeds[KG_CARROT] = 20;
    for (int tile = 0; tile < KG_MAX_TILES; tile++) {
        if (farm->tiles[tile].kind == KG_TILE_EMPTY && (tile % 3) == 0) {
            kg_new_plant(farm, tile, KG_WHEAT, 0,
                env->game_storage.config.turns_per_day);
        }
    }
    while (farm->unit_count < 9 && farm->unit_count < KG_MAX_HANDS + 1) {
        int u = farm->unit_count++;
        farm->hand_count = farm->unit_count - 1;
        farm->units[u].x = (uint8_t)(u % 5);
        farm->units[u].y = (uint8_t)(u % 5);
    }
}

static void print_bench(const char* name, double secs, long iters) {
    double us = 1e6 * secs / (double)iters;
    double rate = (double)iters / secs;
    printf("%-24s  %8.2f us/call   %10.0f calls/s\n", name, us, rate);
}

int main(void) {
    float observations[KG_NUM_PLAYERS * OBS_SIZE];
    float actions[KG_NUM_PLAYERS * NUM_ATNS];
    float rewards[KG_NUM_PLAYERS];
    float terminals[KG_NUM_PLAYERS];
    unsigned char masks[KG_NUM_PLAYERS * KG_POLICY_ACTION_MASK_SIZE];
    Env env = {0};
    KGConfig config;
    kg_config_default(&config);
    env.num_agents = KG_NUM_PLAYERS;
    env.rng = 1;
    env.reward_potential_scale = 0.001f;
    env.reward_win = 0.5f;
    env.reward_seed_value = 1.0f;
    env.reward_product_value = 1.0f;
    env.reward_crop_value = 1.0f;
    env.reward_animal_value = 1.0f;
    env.reward_land_value = 1.0f;
    env.reward_neglect_discount = 0.5f;
    env.reward_liquidation_days = 6.0f;
    env.bot_opponent = KAG_BOT_NONE;
    kg_init(&env.game_storage, &config);
    for (int p = 0; p < KG_NUM_PLAYERS; p++) {
        env.agents[p].observations = observations + p * OBS_SIZE;
        env.agents[p].actions = actions + p * NUM_ATNS;
        env.agents[p].rewards = rewards + p;
        env.agents[p].terminals = terminals + p;
        env.agents[p].action_mask = masks + p * KG_POLICY_ACTION_MASK_SIZE;
        env.agents[p].policy = p == 0 ? 0 : 1;
    }
    densify(&env);
    kag_write_all_observations(&env);

    printf("Kaggriculture hot-path bench  (iters=%d, mask=%d, heads=%d)\n",
        BENCH_ITERS, KG_POLICY_ACTION_MASK_SIZE, NUM_ATNS);
    printf("busy farm: units=%d money=%d\n\n",
        env.game_storage.players[0].unit_count,
        env.game_storage.players[0].money);

    double t0, t1;
    long i;

    for (i = 0; i < BENCH_WARMUP; i++) kag_write_mask(&env, 0);
    t0 = now_sec();
    for (i = 0; i < BENCH_ITERS; i++) kag_write_mask(&env, 0);
    t1 = now_sec();
    print_bench("kag_write_mask", t1 - t0, BENCH_ITERS);

    for (i = 0; i < BENCH_WARMUP; i++) kag_write_observation(&env, 0);
    t0 = now_sec();
    for (i = 0; i < BENCH_ITERS; i++) kag_write_observation(&env, 0);
    t1 = now_sec();
    print_bench("kag_write_observation", t1 - t0, BENCH_ITERS);

    for (i = 0; i < BENCH_WARMUP; i++) kag_write_all_observations(&env);
    t0 = now_sec();
    for (i = 0; i < BENCH_ITERS; i++) kag_write_all_observations(&env);
    t1 = now_sec();
    print_bench("write_all_obs+masks", t1 - t0, BENCH_ITERS);

    KGAction pair[2];
    memset(pair, 0, sizeof(pair));
    pair[0].farmer.op = KG_OP_PASS;
    pair[1].farmer.op = KG_OP_PASS;
    pair[0].hand_count = env.game_storage.players[0].hand_count;
    pair[1].hand_count = env.game_storage.players[1].hand_count;
    densify(&env);
    for (i = 0; i < BENCH_WARMUP; i++) kg_step(&env.game_storage, pair);
    densify(&env);
    t0 = now_sec();
    for (i = 0; i < BENCH_ITERS; i++) {
        if ((i & 63) == 0) densify(&env);
        pair[0].hand_count = env.game_storage.players[0].hand_count;
        pair[1].hand_count = env.game_storage.players[1].hand_count;
        kg_step(&env.game_storage, pair);
    }
    t1 = now_sec();
    print_bench("kg_step(PASS)", t1 - t0, BENCH_ITERS);

    densify(&env);
    for (i = 0; i < BENCH_WARMUP; i++) {
        kag_bot_action(&env.game_storage, 1, -1, &pair[1]);
    }
    t0 = now_sec();
    for (i = 0; i < BENCH_ITERS; i++) {
        kag_bot_action(&env.game_storage, 1, -1, &pair[1]);
    }
    t1 = now_sec();
    print_bench("kag_bot_action", t1 - t0, BENCH_ITERS);

    densify(&env);
    env.bot_opponent = KAG_BOT_NONE;
    env.tag = 0;
    env.potential[0] = kag_player_potential(&env, 0);
    env.potential[1] = kag_player_potential(&env, 1);
    for (i = 0; i < BENCH_WARMUP; i++) {
        kag_clear_policy_actions(&env.agents[0]);
        kag_clear_policy_actions(&env.agents[1]);
        puf_step(&env);
    }
    densify(&env);
    env.potential[0] = kag_player_potential(&env, 0);
    env.potential[1] = kag_player_potential(&env, 1);
    t0 = now_sec();
    for (i = 0; i < BENCH_ITERS; i++) {
        if ((i & 63) == 0) {
            densify(&env);
            env.potential[0] = kag_player_potential(&env, 0);
            env.potential[1] = kag_player_potential(&env, 1);
        }
        kag_clear_policy_actions(&env.agents[0]);
        kag_clear_policy_actions(&env.agents[1]);
        puf_step(&env);
    }
    t1 = now_sec();
    print_bench("puf_step(no bot)", t1 - t0, BENCH_ITERS);

    densify(&env);
    env.bot_opponent = KAG_BOT_MIXED;
    env.tag = 1;
    env.potential[0] = kag_player_potential(&env, 0);
    env.potential[1] = kag_player_potential(&env, 1);
    t0 = now_sec();
    for (i = 0; i < BENCH_ITERS; i++) {
        if ((i & 63) == 0) {
            densify(&env);
            env.potential[0] = kag_player_potential(&env, 0);
            env.potential[1] = kag_player_potential(&env, 1);
        }
        kag_clear_policy_actions(&env.agents[0]);
        kag_clear_policy_actions(&env.agents[1]);
        puf_step(&env);
    }
    t1 = now_sec();
    print_bench("puf_step(+rules bot)", t1 - t0, BENCH_ITERS);

    densify(&env);
    unsigned char a[KG_POLICY_MARKET_COMMANDS];
    unsigned char b[KG_POLICY_MARKET_COMMANDS];
    kag_write_market_mask(&env.game_storage, &env.game_storage.players[0], a);
    for (int id = 0; id < KG_POLICY_MARKET_COMMANDS; id++) {
        b[id] = (unsigned char)kag_market_action_legal(
            &env.game_storage, &env.game_storage.players[0],
            kag_market_spec(id));
    }
    if (memcmp(a, b, KG_POLICY_MARKET_COMMANDS) != 0) {
        fprintf(stderr, "FAIL: optimized market mask != legal()\n");
        for (int id = 0; id < KG_POLICY_MARKET_COMMANDS; id++) {
            if (a[id] != b[id]) {
                fprintf(stderr, "  id=%d opt=%d legal=%d\n", id, a[id], b[id]);
            }
        }
        return 1;
    }
    printf("\nmarket mask == kag_market_action_legal: OK\n");
    printf("ABI: NUM_ATNS=%d MASK=%d OBS=%d\n",
        NUM_ATNS, KG_POLICY_ACTION_MASK_SIZE, OBS_SIZE);

    /* Rough train SPS estimate: 2048 agents, 4 threads, puf_step both players. */
    double us = 1e6 * (t1 - t0) / (double)BENCH_ITERS;
    double env_sps = 2048.0 / (us * 1e-6) * (4.0 / 4.0);
    printf("\nRough SPS ceiling if only puf_step(+bot) on 2048 agents / 4 cores:\n");
    printf("  ~%.0f agent-steps/s  (ignores GPU; order-of-magnitude only)\n",
        env_sps / 2.0);
    return 0;
}
