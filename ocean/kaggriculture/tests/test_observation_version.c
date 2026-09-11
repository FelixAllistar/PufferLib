#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef KAG_AUDIT_HEADER
#define KAG_AUDIT_HEADER "../kaggriculture.h"
#endif
#include KAG_AUDIT_HEADER
#include "kag_observation_contract.h"

enum { FARM_OFFSET = 31, FARM_BYTES = 132 };

static Env* fixture(obs_t observations[2][OBS_SIZE], int macro_mode) {
    Env* env = calloc(1, sizeof(*env));
    assert(env);
    KGConfig config;
    kg_config_default(&config);
    config.weed_spawn_chance = 0;
    kg_init(&env->game_storage, &config);
    env->game_storage.players[1] = env->game_storage.players[0];
    kg_new_plant(&env->game_storage.players[0], 0, KG_WHEAT, 0, 24);
    kg_new_plant(&env->game_storage.players[1], 0, KG_STRAWBERRY, 0, 24);
    env->macro_mode = macro_mode;
#ifdef KAG_MACRO_MODE_TASKS
    env->frozen_macro_mode = -1;
#endif
    env->frozen_observation_version = -1;
    env->macro_decision_interval = 1;
    env->macro_score_scale = 10000;
    for (int player = 0; player < 2; player++)
        env->agents[player].observations = observations[player];
    return env;
}

static void write_pair(Env* env) {
    /* Exercise the shared-summary path used by both CPU and CUDA rollouts. */
    KagFarmSummary summaries[2];
    for (int p = 0; p < 2; p++)
        kag_collect_farm_summary(&env->game_storage,
            &env->game_storage.players[p], &summaries[p]);
    for (int p = 0; p < 2; p++)
        kag_write_observation_with_summaries(env, p, summaries);
}

static void assert_layout(int macro_mode) {
    obs_t out[2][OBS_SIZE], legacy[2][OBS_SIZE], fixed[2][OBS_SIZE];
    Env* env = fixture(out, macro_mode);
    write_pair(env);
    memcpy(legacy, out, sizeof(out));
    /* Preserve the historical contract explicitly, including the old collision. */
    assert(memcmp(legacy[0], legacy[1], OBS_SIZE) == 0);
    env->observation_version = KAG_OBSERVATION_EGOCENTRIC;
    write_pair(env);
    memcpy(fixed, out, sizeof(out));
    assert(memcmp(fixed[0], fixed[1], OBS_SIZE) != 0);
    assert(memcmp(fixed[0], legacy[0], OBS_SIZE) == 0);
    assert(memcmp(fixed[1], legacy[1], FARM_OFFSET) == 0);
    assert(memcmp(fixed[1] + FARM_OFFSET,
        legacy[1] + FARM_OFFSET + FARM_BYTES, FARM_BYTES) == 0);
    assert(memcmp(fixed[1] + FARM_OFFSET + FARM_BYTES,
        legacy[1] + FARM_OFFSET, FARM_BYTES) == 0);
    assert(memcmp(fixed[1] + FARM_OFFSET + 2 * FARM_BYTES,
        legacy[1] + FARM_OFFSET + 2 * FARM_BYTES,
        OBS_SIZE - FARM_OFFSET - 2 * FARM_BYTES) == 0);

    /* Swapping physical seats cannot change a version-1 actor's observation. */
    KGPlayer swap = env->game_storage.players[0];
    env->game_storage.players[0] = env->game_storage.players[1];
    env->game_storage.players[1] = swap;
    write_pair(env);
    assert(memcmp(out[0], fixed[1], OBS_SIZE) == 0);
    assert(memcmp(out[1], fixed[0], OBS_SIZE) == 0);
    swap = env->game_storage.players[0];
    env->game_storage.players[0] = env->game_storage.players[1];
    env->game_storage.players[1] = swap;

    /* Version belongs to the policy, never to its physical seat. */
    env->frozen_observation_version = KAG_OBSERVATION_LEGACY;
    for (int learner_seat = 0; learner_seat < 2; learner_seat++) {
        env->agents[learner_seat].policy = 0;
        env->agents[1 - learner_seat].policy = 3;
        write_pair(env);
        assert(memcmp(out[learner_seat], fixed[learner_seat], OBS_SIZE) == 0);
        assert(memcmp(out[1 - learner_seat], legacy[1 - learner_seat], OBS_SIZE) == 0);
    }
    env->frozen_observation_version = -1;
    write_pair(env);
    assert(memcmp(out, fixed, sizeof(out)) == 0);
    env->agents[0].policy = env->agents[1].policy = 0;
    env->frozen_observation_version = 0;
    write_pair(env);
    assert(memcmp(out, fixed, sizeof(out)) == 0); /* live mirror self-play */

    /* The ownership repair does not expose opponent private inventory. */
    env->game_storage.players[1].shed[KG_ITEM_MILK] = 70;
    env->game_storage.players[1].seeds[KG_MELON] = 25;
    kag_write_observation(env, 0);
    assert(memcmp(out[0], fixed[0], OBS_SIZE) == 0);
    printf("PASS mode=%d: ownership, seat swap, legacy bytes, mixed banks, mirror, privacy\n", macro_mode);
    free(env);
}

static void assert_eval_contract(void) {
    Ini ini = {0};
    puf_ini_load_file(&ini, "config/default.ini");
    puf_ini_put(&ini, "base.env_name", "kaggriculture");
    puf_ini_put(&ini, "env.observation_version", "1");
    puf_ini_put(&ini, "env.frozen_observation_version", "0");
    KagObservationContract contract = kag_observation_contract(&ini);
    assert(kag_observation_pool_compatible(contract, 1.0f, 8));
    assert(!kag_observation_pool_compatible(contract, 0.8f, 8));
    assert(!kag_observation_pool_compatible(contract, 1.0f, 0));
    for (int external = 0; external < 2; external++) {
        int opponent = external ? 0 : 1;
        for (int reverse = 0; reverse < 2; reverse++) {
            kag_observation_pair(&ini, contract, external, reverse);
            assert(puf_ini_get(&ini, "env", "observation_version") == (reverse ? opponent : 1));
            assert(puf_ini_get(&ini, "env", "frozen_observation_version") == (reverse ? 1 : opponent));
        }
        kag_observation_restore(&ini, contract);
        assert(puf_ini_get(&ini, "env", "observation_version") == 1);
        assert(puf_ini_get(&ini, "env", "frozen_observation_version") == 0);
    }
    puf_ini_put(&ini, "env.frozen_observation_version", "-1");
    contract = kag_observation_contract(&ini);
    assert(kag_observation_pool_compatible(contract, 0.5f, 0));
    kag_observation_pair(&ini, contract, 1, 1);
    assert(puf_ini_get(&ini, "env", "observation_version") == 1);
    assert(puf_ini_get(&ini, "env", "frozen_observation_version") == 1);
    kag_observation_restore(&ini, contract);
    assert(puf_ini_get(&ini, "env", "frozen_observation_version") == -1);
    puf_ini_put(&ini, "base.env_name", "robocode");
    /* An executor-only difference requires exactly the same two-seat rules. */
    puf_ini_put(&ini, "base.env_name", "kaggriculture");
    puf_ini_put(&ini, "env.macro_executor_version", "1");
    puf_ini_put(&ini, "env.frozen_macro_executor_version", "0");
    contract = kag_observation_contract(&ini);
    assert(!kag_observation_pool_compatible(contract, 0.8f, 8));
    assert(kag_observation_pool_compatible(contract, 1.0f, 8));
    for (int external = 0; external < 2; external++) {
        for (int reverse = 0; reverse < 2; reverse++) {
            kag_observation_pair(&ini, contract, external, reverse);
            int opponent = external ? 0 : 1;
            assert(puf_ini_get(&ini,"env","macro_executor_version") == (reverse ? opponent : 1));
            assert(puf_ini_get(&ini,"env","frozen_macro_executor_version") == (reverse ? 1 : opponent));
        }
    }
    kag_observation_restore(&ini, contract);
    assert(puf_ini_get(&ini,"env","macro_executor_version") == 1);
    assert(puf_ini_get(&ini,"env","frozen_macro_executor_version") == 0);
    puf_ini_put(&ini, "base.env_name", "robocode");
    contract = kag_observation_contract(&ini);
    assert(!contract.enabled);
    kag_observation_pair(&ini, contract, 1, 1);
    assert(puf_ini_get(&ini, "env", "frozen_observation_version") == -1);
    puf_ini_free(&ini);
    puts("PASS eval: external/rolling opponent versions, both directions, restore, unsafe-pool guard");
}

int main(void) {
    assert_layout(0);
    assert_layout(1);
    assert_layout(2);
    assert_eval_contract();
    return 0;
}
