#pragma once

/* Offline v3 data uses the production float encoder and stateful rewards.
 * Version 2 byte datasets and untagged checkpoints must never be reinterpreted.
 * Include after kaggriculture.h and ini.h. All builds here are little-endian. */
#include "../../src/kag_observation_contract.h"

#ifndef KAG_BC_SOURCE_HASH
#define KAG_BC_SOURCE_HASH 0ULL
#endif

typedef struct {
    uint32_t magic, version, count, row_obs, row_expert, row_mask, games, steps;
    uint32_t obs_bytes, observation_version, policy_version, macro_mode;
    uint32_t executor, interval, score_features, validation_games;
    uint64_t source_hash, semantics_hash;
    double gamma;
} KagEntityBCHeader;

static inline uint64_t kag_bc_hash_bytes(uint64_t h, const void* p, size_t n) {
    const unsigned char* b = (const unsigned char*)p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ULL; }
    return h;
}

static inline uint64_t kag_bc_game_hash(const KGConfig* c) {
    uint64_t h = 14695981039346656037ULL;
#define KG_BC_HASH_FIELD(field) h = kag_bc_hash_bytes(h, &c->field, sizeof(c->field))
    KG_BC_HASH_FIELD(episode_steps); KG_BC_HASH_FIELD(board_size);
    KG_BC_HASH_FIELD(starting_money); KG_BC_HASH_FIELD(max_market_orders_per_turn);
    KG_BC_HASH_FIELD(turns_per_day); KG_BC_HASH_FIELD(shed_capacity);
    KG_BC_HASH_FIELD(weed_spawn_chance); KG_BC_HASH_FIELD(town_shop_unlock_interval);
    KG_BC_HASH_FIELD(town_shop_sell_interval); KG_BC_HASH_FIELD(town_center_sell_interval);
    KG_BC_HASH_FIELD(farm_hand_cost_mult);
#undef KG_BC_HASH_FIELD
    return h;
}

/* No reset bank, scripted opening, opponent policy or GPU is loaded. The
 * replay supplies BOTH primitive action streams and its exact game seed. */
static inline uint64_t kag_bc_configure(Env* e, Ini* ini, const KGConfig* replay) {
    memset(e, 0, sizeof(*e));
    KagObservationContract c = kag_observation_contract(ini);
    if (c.mode != 2 || (c.executor != 1 && c.executor != 2) || c.interval != 1) {
        fprintf(stderr, "Entity BC requires macro_mode=2, executor=1 or 2, interval=1\n");
        exit(1);
    }
    Dict* d = puf_ini_section(ini, "env", 0);
    float clip = (float)puf_ini_get(ini, "train", "reward_clip");
    if (clip != 0) {
        fprintf(stderr, "Entity BC return targets currently require train.reward_clip=0\n");
        exit(1);
    }
    kag_reward_bind_train_config(d, (float)puf_ini_get(ini, "train", "gamma"), clip);
    kag_reward_configure(e, d);
    KGConfig cfg = kag_load_config(e, d);
    uint64_t h = kag_bc_game_hash(&cfg);
    if (replay && kag_bc_game_hash(replay) != h) {
        fprintf(stderr, "Replay game rules differ from BC profile\n"); exit(1);
    }
    if (replay) cfg = *replay;
    e->num_agents = 2;
    e->macro_mode = c.mode; e->macro_executor_version = c.executor;
    e->observation_version = c.learner;
    e->macro_decision_interval = c.interval; e->macro_score_features = c.score_features;
    e->frozen_macro_mode = e->frozen_macro_executor_version = -1;
    e->frozen_observation_version = e->frozen_macro_decision_interval = -1;
    e->frozen_macro_score_features = -1;
    e->macro_score_scale = (float)dict_get(d, "macro_score_scale");
    if (e->macro_score_scale <= 0) e->macro_score_scale = 10000;
    e->policy_max_hands = kag_reward_integer(d, "policy_max_hands", 16, 1, KG_MAX_HANDS);
    e->policy_market_slots = kag_reward_integer(d, "policy_market_slots", 10, 1, 10);
    e->land_buy_min_days = kag_reward_integer(d, "land_buy_min_days", 2, 0, INT_MAX);
#define KG_BC_HASH_ENV(field) h = kag_bc_hash_bytes(h, &e->field, sizeof(e->field))
    KG_BC_HASH_ENV(reward); /* initialized zero; only 32-bit scalar fields */
    KG_BC_HASH_ENV(macro_mode); KG_BC_HASH_ENV(macro_executor_version);
    KG_BC_HASH_ENV(observation_version); KG_BC_HASH_ENV(macro_decision_interval);
    KG_BC_HASH_ENV(macro_score_features); KG_BC_HASH_ENV(macro_score_scale);
    KG_BC_HASH_ENV(policy_max_hands); KG_BC_HASH_ENV(policy_market_slots);
    KG_BC_HASH_ENV(land_buy_min_days);
#undef KG_BC_HASH_ENV
    kg_init(&e->game_storage, &cfg);
    for (int p = 0; p < 2; p++) {
        kag_reward_reset(e, p); kag_reset_land_buy_delay(e, p);
    }
    return h;
}
