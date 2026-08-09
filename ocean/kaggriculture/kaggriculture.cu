// GPU-resident Kaggriculture environment. The policy encoder lives in
// kaggriculture_encoder.cu and is included later, after the generic policy
// types exist. Keeping the two translation fragments separate avoids relying
// on include order and lets this file expose only the environment hooks.
#ifndef PUFFER_KAGGRICULTURE_GPU_ENV_CU
#define PUFFER_KAGGRICULTURE_GPU_ENV_CU

#ifndef PUFFER_GPU_ENV
#error "kaggriculture.cu requires -DPUFFER_GPU_ENV (build with --gpu)"
#endif

#define PUF_GPU_ENV_BANK_LAYOUT 1
#define PUF_GPU_SELFPLAY 1

#include <cuda_runtime.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef KAG_CUDA_BLOCK
#define KAG_CUDA_BLOCK 128
#endif

typedef struct {
    KGConfig game;
    uint64_t base_seed;
    int policy_market_slots;
    int policy_max_hands;
    int opening_turns;
    int reset_opening_turns;
    float reward_potential_scale;
    float reward_win;
    float reward_seed_value;
    float reward_product_value;
    float reward_crop_value;
    float reward_animal_value;
    float reward_land_value;
    float reward_neglect_discount;
    float reward_liquidation_days;
    float reward_productive_action;
    float reward_inactivity;
    float reward_neglect_death;
    float bot_opponent_fraction;
    float bot_pass_fraction;
    float bot_rules_fraction;
    float bot_top_fraction;
    float bot_script_fraction;
    float bot_adaptive_fraction;
    int bot_first;
} KagCudaConfig;

static KagCudaConfig h_kag_cuda_config;
static __constant__ KagCudaConfig d_kag_cuda_config;
static Env* d_kag_matches = nullptr;
static int* d_kag_rows = nullptr;
static KGScriptTape* d_kag_tapes = nullptr;
static KGAction* d_kag_decoded_actions = nullptr;
static int g_kag_total_agents = 0;
static int g_kag_num_matches = 0;
static int g_kag_bound = 0;
static int g_kag_bank_count = 0;
static uint32_t* d_kag_opening_rng = nullptr;
static int* d_kag_bank_completed = nullptr;
static obs_t* g_kag_observations = nullptr;
static float* g_kag_actions = nullptr;
static float* g_kag_rewards = nullptr;
static float* g_kag_terminals = nullptr;
static unsigned char* g_kag_masks = nullptr;

static void kag_cuda_check(const char* what) {
    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        std::fprintf(stderr, "Kaggriculture CUDA %s failed: %s\n",
            what, cudaGetErrorString(err));
        std::exit(1);
    }
}

static inline int kag_cuda_grid(int n) {
    return (n + KAG_CUDA_BLOCK - 1) / KAG_CUDA_BLOCK;
}

__device__ static void kag_cuda_choose_bot(Env* env, int match_id) {
    uint32_t opponent_hash = 0x9e3779b9u * ((uint32_t)match_id + 1u);
    env->bot_opponent = KAG_BOT_NONE;
    if ((opponent_hash >> 8)
            >= (uint32_t)(d_kag_cuda_config.bot_opponent_fraction
                * 16777216.0f)) {
        return;
    }
    if (d_kag_cuda_config.bot_pass_fraction >= 1.0f) {
        env->bot_opponent = KAG_BOT_PASS;
        return;
    }
    uint32_t type_hash = opponent_hash * 0x85ebca6bu + 0xc2b2ae35u;
    uint32_t type_value = type_hash >> 8;
    float remainder = 1.0f - d_kag_cuda_config.bot_top_fraction;
    uint32_t top_cut = (uint32_t)(d_kag_cuda_config.bot_top_fraction
        * 16777216.0f);
    uint32_t script_cut = top_cut
        + (uint32_t)(remainder * d_kag_cuda_config.bot_script_fraction
            * 16777216.0f);
    uint32_t rules_cut = script_cut
        + (uint32_t)(remainder
            * (1.0f - d_kag_cuda_config.bot_script_fraction)
            * d_kag_cuda_config.bot_adaptive_fraction * 16777216.0f);
    uint32_t specialist_cut = rules_cut
        + (uint32_t)(remainder
            * (1.0f - d_kag_cuda_config.bot_script_fraction)
            * (1.0f - d_kag_cuda_config.bot_adaptive_fraction)
            * d_kag_cuda_config.bot_rules_fraction * 16777216.0f);
    if (type_value < top_cut) {
        env->bot_opponent = KAG_BOT_SCRIPT_BASE + KG_SCRIPT_TOP;
    } else if (type_value < script_cut) {
        env->bot_opponent = KAG_BOT_SCRIPT_BASE
            + (int)(type_hash % KG_SCRIPT_TOP);
    } else if (type_value < rules_cut) {
        env->bot_opponent = KAG_BOT_ADAPTIVE_BASE
            + KAG_ADAPTIVE_HARVEST_PULSE + (int)(type_hash % 3u);
    } else if (type_value < specialist_cut) {
        env->bot_opponent = KAG_BOT_MIXED;
    } else {
        int specialist = (int)(type_hash % (KG_NUM_CROPS + 1));
        env->bot_opponent = specialist == 0 ? KAG_BOT_STARTER
            : KAG_BOT_CROP_BASE + specialist - 1;
    }
}

__device__ static void kag_cuda_bot_overrides(Env* env,
        KGAction actions[KG_NUM_PLAYERS], const KGScriptTape* tapes) {
    KGState* game = &env->game_storage;
    for (int player = 0; player < KG_NUM_PLAYERS; player++) {
        int bot = env->demo_bots[player];
        if (bot == KAG_BOT_NONE && player == (env->bot_first ? 0 : 1)
                && (env->tag > 0 || env->bot_opponent_fraction >= 1.0f)) {
            bot = env->bot_opponent;
        }
        if (bot == KAG_BOT_PASS) {
            actions[player].hand_count = 0;
            actions[player].market_count = 0;
            actions[player].farmer = (KGUnitAction){KG_OP_PASS, -1, 1};
            actions[player].hand_count = game->players[player].hand_count;
            for (int hand = 0; hand < actions[player].hand_count; hand++) {
                actions[player].hands[hand] =
                    (KGUnitAction){KG_OP_PASS, -1, 1};
            }
        } else if (bot == KAG_BOT_STARTER) {
            kag_starter_action(game, player, &actions[player]);
        } else if (bot >= KAG_BOT_SCRIPT_BASE
                && bot < KAG_BOT_SCRIPT_BASE + KG_SCRIPT_COUNT) {
            int profile = bot - KAG_BOT_SCRIPT_BASE;
            kag_script_action_from_tapes(game, player, profile,
                &actions[player], tapes);
            kag_script_repair(game, player, profile, &actions[player]);
            if (profile == KG_SCRIPT_TOP && game->step >= 26) {
                kag_bot_action(game, player, -1, &actions[player]);
            }
        } else if (bot >= KAG_BOT_ADAPTIVE_BASE
                && bot < KAG_BOT_ADAPTIVE_BASE + KAG_ADAPTIVE_COUNT) {
            kag_adaptive_action(game, player,
                bot - KAG_BOT_ADAPTIVE_BASE, &actions[player]);
        } else if (bot >= KAG_BOT_MIXED) {
            int crop = bot == KAG_BOT_MIXED ? -1 : bot - KAG_BOT_CROP_BASE;
            kag_bot_action(game, player, crop, &actions[player]);
        }
        if (!(bot >= KAG_BOT_SCRIPT_BASE
                && bot < KAG_BOT_SCRIPT_BASE + KG_SCRIPT_COUNT)
                && !(bot >= KAG_BOT_ADAPTIVE_BASE
                    && bot < KAG_BOT_ADAPTIVE_BASE + KAG_ADAPTIVE_COUNT)) {
            kag_apply_policy_limits(env, &game->players[player],
                &actions[player]);
        }
    }
}

__device__ static void kag_cuda_fold_log(Env* shell, Env* match) {
    float* dst = (float*)&shell->log;
    float* src = (float*)&match->log;
    constexpr int fields = (int)(sizeof(Log) / sizeof(float));
    for (int field = 0; field < fields; field++) dst[field] += src[field];
    memset(&match->log, 0, sizeof(match->log));
}

__device__ static void kag_cuda_transition(Env* env, Env* shells,
        const int rows[KG_NUM_PLAYERS], const KGScriptTape* tapes,
        KGAction* actions, int* bank_completed) {
    KGState* game = &env->game_storage;
    float before_potential[KG_NUM_PLAYERS] = {
        env->potential[0], env->potential[1]};
    float productive_credit[KG_NUM_PLAYERS] = {0.0f, 0.0f};
    uint32_t before_neglect[KG_NUM_PLAYERS] = {
        game->neglect_deaths[0], game->neglect_deaths[1]};

    for (int player = 0; player < KG_NUM_PLAYERS; player++) {
        kag_decode_action(&actions[player], &env->agents[player], game, player);
        env->agents[player].terminals[0] = 0.0f;
    }
    kag_cuda_bot_overrides(env, actions, tapes);
    kag_log_actions(env, game, actions);

    if (env->reward_productive_action > 0.0f) {
        for (int player = 0; player < KG_NUM_PLAYERS; player++) {
            productive_credit[player] = kag_productive_action_credit(
                game, &game->players[player], &actions[player]);
        }
    }

    kg_step(game, actions);
    int money[2] = {game->players[0].money, game->players[1].money};
    int model_player = env->bot_opponent == KAG_BOT_NONE && env->tag > 0
        ? (env->agents[0].policy == 0 ? 0 : 1)
        : (env->bot_first ? 1 : 0);
    int done = kg_done(game);
    env->potential[0] = done ? (float)money[0] : kag_player_potential(env, 0);
    env->potential[1] = done ? (float)money[1] : kag_player_potential(env, 1);
    for (int player = 0; player < KG_NUM_PLAYERS; player++) {
        float reward = (env->potential[player] - before_potential[player])
            * env->reward_potential_scale
            + productive_credit[player] * env->reward_productive_action;
        reward -= (game->neglect_deaths[player] - before_neglect[player])
            * env->reward_neglect_death;
        env->agents[player].rewards[0] = reward;
        env->episode_returns[player] += reward;
    }

    if (!done) {
        kag_write_all_observations_from_tapes(env, tapes);
        return;
    }

    float win0 = money[0] > money[1] ? 1.0f
        : money[0] == money[1] ? 0.5f : 0.0f;
    float model_win = model_player == 0 ? win0 : 1.0f - win0;
    float outcome = (2.0f * win0 - 1.0f) * env->reward_win;
    env->agents[0].rewards[0] += outcome;
    env->agents[1].rewards[0] -= outcome;
    env->episode_returns[0] += outcome;
    env->episode_returns[1] -= outcome;
    for (int player = 0; player < KG_NUM_PLAYERS; player++) {
        if (kag_abs(money[player] - game->config.starting_money) <= 2) {
            env->agents[player].rewards[0] -= env->reward_inactivity;
            env->episode_returns[player] -= env->reward_inactivity;
        }
        env->agents[player].terminals[0] = 1.0f;
    }

    int model_money = money[model_player];
    int opponent_money = money[1 - model_player];
    env->log.perf += model_win;
    env->log.score += (float)model_money;
    env->log.sweep_score += kag_abs(model_money - game->config.starting_money) <= 2
        ? -3000.0f : (float)model_money;
    env->log.opponent_score += (float)opponent_money;
    env->log.episode_return += env->episode_returns[model_player];
    env->log.episode_length += (float)game->config.episode_steps;
    env->log.land_purchases += (float)(kag_popcount(
        (unsigned)game->players[model_player].unlocked_mask) - 1);
    env->log.water_coverage += game->plant_days[model_player] > 0
        ? (float)game->watered_plant_days[model_player]
            / game->plant_days[model_player] : 1.0f;
    env->log.neglect_deaths += (float)game->neglect_deaths[model_player];
    env->log.planting_day_deaths +=
        (float)game->planting_day_deaths[model_player];
    for (int crop = 0; crop < KG_NUM_CROPS; crop++) {
        env->log.unused_seed_value += game->players[model_player].seeds[crop]
            * KG_CROP_DEFS[crop].seed_cost;
    }
    for (int tile = 0; tile < KG_MAX_TILES; tile++) {
        int x = tile % KG_MAX_BOARD_SIZE;
        int y = tile / KG_MAX_BOARD_SIZE;
        const KGTile* value = &game->players[model_player].tiles[tile];
        if (value->kind == KG_TILE_PLANT) env->log.plants_alive += 1.0f;
        else if (kg_is_animal_tile(value)) env->log.animals_alive += 1.0f;
        else if (value->kind == KG_TILE_WEED) env->log.weeds += 1.0f;
        if ((x >= 5 || y >= 5) && (value->kind == KG_TILE_PLANT
                || kg_is_animal_tile(value))) {
            env->log.productive_extra_tiles += 1.0f;
        }
    }
    env->log.win_rate += model_win;
    env->log.draw_rate += money[0] == money[1] ? 1.0f : 0.0f;
    if (env->bot_opponent == KAG_BOT_NONE && env->tag == 0) {
        env->log.mirror_games += 1.0f;
    } else if (env->bot_opponent == KAG_BOT_NONE) {
        env->log.checkpoint_wins += model_win;
        env->log.checkpoint_draws += money[0] == money[1] ? 1.0f : 0.0f;
        env->log.checkpoint_games += 1.0f;
    } else if (env->bot_opponent == KAG_BOT_STARTER) {
        env->log.starter_games += 1.0f;
    } else if (env->bot_opponent == KAG_BOT_MIXED) {
        env->log.rules_games += 1.0f;
    } else if (env->bot_opponent == KAG_BOT_PASS) {
        env->log.pass_games += 1.0f;
    } else if (env->bot_opponent >= KAG_BOT_SCRIPT_BASE
            && env->bot_opponent < KAG_BOT_SCRIPT_BASE + KG_SCRIPT_COUNT) {
        int profile = env->bot_opponent - KAG_BOT_SCRIPT_BASE;
        env->log.script_games += 1.0f;
        if (profile == KG_SCRIPT_FRONTIER) env->log.script_frontier_games += 1.0f;
        else if (profile == KG_SCRIPT_V20) env->log.script_v20_games += 1.0f;
        else if (profile == KG_SCRIPT_MOON) env->log.script_moon_games += 1.0f;
        else if (profile == KG_SCRIPT_HAMBURGER) env->log.script_hamburger_games += 1.0f;
        else if (profile == KG_SCRIPT_TOP) env->log.script_top_games += 1.0f;
    } else if (env->bot_opponent >= KAG_BOT_ADAPTIVE_BASE
            && env->bot_opponent < KAG_BOT_ADAPTIVE_BASE + KAG_ADAPTIVE_COUNT) {
        int profile = env->bot_opponent - KAG_BOT_ADAPTIVE_BASE;
        env->log.adaptive_games += 1.0f;
        if (profile == KAG_ADAPTIVE_HARVEST_PULSE) {
            env->log.adaptive_pulse_games += 1.0f;
        } else if (profile == KAG_ADAPTIVE_STRUCTURED) {
            env->log.adaptive_structured_games += 1.0f;
        } else if (profile == KAG_ADAPTIVE_TRIAD) {
            env->log.adaptive_triad_games += 1.0f;
        }
    } else {
        env->log.specialist_games += 1.0f;
    }
    env->log.n += 1.0f;
    if (env->tag > 0) {
        env->boundary_reached = 1;
        atomicAdd(&bank_completed[env->tag], 1);
    }
    shells[rows[model_player]].tag = env->tag;
    kag_cuda_fold_log(&shells[rows[model_player]], env);

    kag_reset_with_opening(env, tapes);
    env->episode_returns[0] = 0.0f;
    env->episode_returns[1] = 0.0f;
    env->potential[0] = kag_player_potential(env, 0);
    env->potential[1] = kag_player_potential(env, 1);
    kag_write_all_observations_from_tapes(env, tapes);
}

__global__ static void kag_cuda_reset_kernel(Env* shells, Env* matches,
        const int* rows, obs_t* observations, float* actions,
        float* rewards, float* terminals, unsigned char* masks,
        const KGScriptTape* tapes, int num_matches, uint32_t* opening_rng) {
    int match_id = blockIdx.x * blockDim.x + threadIdx.x;
    if (match_id >= num_matches) return;
    Env* env = &matches[match_id];
    memset(env, 0, sizeof(*env));
    env->num_agents = KG_NUM_PLAYERS;
    env->rng = (unsigned)match_id;
    env->policy_market_slots = d_kag_cuda_config.policy_market_slots;
    env->policy_max_hands = d_kag_cuda_config.policy_max_hands;
    env->opening_turns = d_kag_cuda_config.opening_turns;
    env->reset_opening_turns = d_kag_cuda_config.reset_opening_turns;
    env->reward_potential_scale = d_kag_cuda_config.reward_potential_scale;
    env->reward_win = d_kag_cuda_config.reward_win;
    env->reward_seed_value = d_kag_cuda_config.reward_seed_value;
    env->reward_product_value = d_kag_cuda_config.reward_product_value;
    env->reward_crop_value = d_kag_cuda_config.reward_crop_value;
    env->reward_animal_value = d_kag_cuda_config.reward_animal_value;
    env->reward_land_value = d_kag_cuda_config.reward_land_value;
    env->reward_neglect_discount = d_kag_cuda_config.reward_neglect_discount;
    env->reward_liquidation_days = d_kag_cuda_config.reward_liquidation_days;
    env->reward_productive_action = d_kag_cuda_config.reward_productive_action;
    env->reward_inactivity = d_kag_cuda_config.reward_inactivity;
    env->reward_neglect_death = d_kag_cuda_config.reward_neglect_death;
    env->bot_opponent_fraction = d_kag_cuda_config.bot_opponent_fraction;
    env->bot_top_fraction = d_kag_cuda_config.bot_top_fraction;
    env->bot_script_fraction = d_kag_cuda_config.bot_script_fraction;
    env->bot_adaptive_fraction = d_kag_cuda_config.bot_adaptive_fraction;
    env->bot_first = d_kag_cuda_config.bot_first;

    int match_rows[2] = {rows[2 * match_id], rows[2 * match_id + 1]};
    for (int player = 0; player < KG_NUM_PLAYERS; player++) {
        int row = match_rows[player];
        env->agents[player].observations = observations + (size_t)row * OBS_SIZE;
        env->agents[player].actions = actions + (size_t)row * NUM_ATNS;
        env->agents[player].rewards = rewards + row;
        env->agents[player].terminals = terminals + row;
        env->agents[player].action_mask = masks
            + (size_t)row * KG_POLICY_ACTION_MASK_SIZE;
    }
    /* Policy IDs and tag are packed after the row map: the host initializes
     * them in shell.num_agents/policy so reset remains one compact kernel. */
    env->agents[0].policy = shells[match_rows[0]].agents[0].policy;
    env->agents[1].policy = shells[match_rows[1]].agents[0].policy;
    env->tag = env->agents[0].policy > env->agents[1].policy
        ? env->agents[0].policy : env->agents[1].policy;
    kag_cuda_choose_bot(env, match_id);

    KGConfig config = d_kag_cuda_config.game;
    config.seed = d_kag_cuda_config.base_seed
        ^ 0x9e3779b97f4a7c15ULL * ((uint64_t)match_id + 1ULL);
    kg_init(&env->game_storage, &config);
    env->reset_opening_rng = opening_rng[match_id];
    kag_reset_with_opening(env, tapes);
    opening_rng[match_id] = env->reset_opening_rng;
    for (int player = 0; player < KG_NUM_PLAYERS; player++) {
        env->agents[player].rewards[0] = 0.0f;
        env->agents[player].terminals[0] = 0.0f;
        env->potential[player] = kag_player_potential(env, player);
    }
    kag_write_all_observations_from_tapes(env, tapes);
}

__global__ static void kag_cuda_step_kernel(Env* shells, Env* matches,
        const int* rows, const KGScriptTape* tapes, KGAction* decoded_actions,
        int num_matches, uint32_t* opening_rng, int* bank_completed) {
    int match_id = blockIdx.x * blockDim.x + threadIdx.x;
    if (match_id >= num_matches) return;
    int match_rows[2] = {rows[2 * match_id], rows[2 * match_id + 1]};
    kag_cuda_transition(&matches[match_id], shells, match_rows, tapes,
        decoded_actions + 2 * match_id, bank_completed);
    opening_rng[match_id] = matches[match_id].reset_opening_rng;
}

__global__ static void kag_cuda_clear_shells_kernel(Env* shells,
        int total_agents) {
    int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= total_agents) return;
    memset(&shells[row].log, 0, sizeof(shells[row].log));
    shells[row].boundary_reached = 0;
    shells[row].tag = 0;
}

static void kag_cuda_load_config(Dict* kwargs) {
    Env template_env = {};
    template_env.rng = 0;
    puf_init(&template_env, kwargs);
    const KGConfig* game = &template_env.game_storage.config;
    h_kag_cuda_config = {};
    h_kag_cuda_config.game = *game;
    h_kag_cuda_config.base_seed = (uint64_t)dict_get(kwargs, "seed");
    h_kag_cuda_config.policy_market_slots = template_env.policy_market_slots;
    h_kag_cuda_config.policy_max_hands = template_env.policy_max_hands;
    h_kag_cuda_config.opening_turns = template_env.opening_turns;
    h_kag_cuda_config.reset_opening_turns = template_env.reset_opening_turns;
    h_kag_cuda_config.reward_potential_scale = template_env.reward_potential_scale;
    h_kag_cuda_config.reward_win = template_env.reward_win;
    h_kag_cuda_config.reward_seed_value = template_env.reward_seed_value;
    h_kag_cuda_config.reward_product_value = template_env.reward_product_value;
    h_kag_cuda_config.reward_crop_value = template_env.reward_crop_value;
    h_kag_cuda_config.reward_animal_value = template_env.reward_animal_value;
    h_kag_cuda_config.reward_land_value = template_env.reward_land_value;
    h_kag_cuda_config.reward_neglect_discount = template_env.reward_neglect_discount;
    h_kag_cuda_config.reward_liquidation_days = template_env.reward_liquidation_days;
    h_kag_cuda_config.reward_productive_action = template_env.reward_productive_action;
    h_kag_cuda_config.reward_inactivity = template_env.reward_inactivity;
    h_kag_cuda_config.reward_neglect_death = template_env.reward_neglect_death;
    h_kag_cuda_config.bot_opponent_fraction = template_env.bot_opponent_fraction;
    h_kag_cuda_config.bot_pass_fraction = (float)dict_get(kwargs, "bot_pass_fraction");
    h_kag_cuda_config.bot_rules_fraction = (float)dict_get(kwargs, "bot_rules_fraction");
    h_kag_cuda_config.bot_top_fraction = template_env.bot_top_fraction;
    h_kag_cuda_config.bot_script_fraction = template_env.bot_script_fraction;
    h_kag_cuda_config.bot_adaptive_fraction = template_env.bot_adaptive_fraction;
    h_kag_cuda_config.bot_first = template_env.bot_first;
    cudaMemcpyToSymbol(d_kag_cuda_config, &h_kag_cuda_config,
        sizeof(h_kag_cuda_config));
    kag_cuda_check("config upload");
}

static Env* puf_envs_create(int total_agents, Dict* env_kwargs,
        Dict* vec_kwargs, int* bank_layout) {
    if (total_agents < 2 || total_agents % 2) {
        std::fprintf(stderr,
            "Kaggriculture GPU requires an even vec.total_agents >= 2\n");
        std::exit(1);
    }
    g_kag_total_agents = total_agents;
    g_kag_num_matches = total_agents / 2;
    kag_cuda_load_config(env_kwargs);
    kag_script_init();

    int frozen_banks = (int)dict_get(vec_kwargs, "num_frozen_banks");
    g_kag_bank_count = frozen_banks;
    float frozen_pct = (float)dict_get(vec_kwargs, "frozen_bank_pct");
    int frozen_matches = frozen_banks > 0
        ? (int)(frozen_pct * g_kag_num_matches) : 0;
    int frozen_start = g_kag_num_matches - frozen_matches;
    int seat_balance = (int)dict_get(vec_kwargs, "seat_balance");
    int* cursors = (int*)std::calloc((size_t)frozen_banks + 1, sizeof(int));
    int* host_rows = (int*)std::calloc((size_t)total_agents, sizeof(int));
    Env* host_shells = (Env*)std::calloc((size_t)total_agents, sizeof(Env));
    if (!cursors || !host_rows || !host_shells) std::abort();
    for (int bank = 0; bank <= frozen_banks; bank++) {
        cursors[bank] = bank_layout[bank];
    }
    for (int match = 0; match < g_kag_num_matches; match++) {
        int policy[2] = {0, 0};
        if (match >= frozen_start) {
            policy[1] = 1 + (match - frozen_start) % frozen_banks;
        }
        if (seat_balance && frozen_banks > 0
                && ((match / frozen_banks) & 1)) {
            int swap = policy[0];
            policy[0] = policy[1];
            policy[1] = swap;
        }
        for (int player = 0; player < 2; player++) {
            int row = cursors[policy[player]]++;
            host_rows[2 * match + player] = row;
            host_shells[row].agents[0].policy = policy[player];
        }
    }
    for (int bank = 0; bank <= frozen_banks; bank++) {
        if (cursors[bank] != bank_layout[bank + 1]) {
            std::fprintf(stderr, "Kaggriculture GPU bank mapping mismatch at %d\n", bank);
            std::exit(1);
        }
    }

    Env* shells = nullptr;
    cudaMalloc((void**)&shells, (size_t)total_agents * sizeof(Env));
    cudaMemcpy(shells, host_shells, (size_t)total_agents * sizeof(Env),
        cudaMemcpyHostToDevice);
    cudaMalloc((void**)&d_kag_matches,
        (size_t)g_kag_num_matches * sizeof(Env));
    cudaMemset(d_kag_matches, 0,
        (size_t)g_kag_num_matches * sizeof(Env));
    cudaMalloc((void**)&d_kag_rows, (size_t)total_agents * sizeof(int));
    cudaMemcpy(d_kag_rows, host_rows, (size_t)total_agents * sizeof(int),
        cudaMemcpyHostToDevice);
    cudaMalloc((void**)&d_kag_tapes, KG_SCRIPT_COUNT * sizeof(KGScriptTape));
    cudaMemcpy(d_kag_tapes, kag_script_tapes,
        KG_SCRIPT_COUNT * sizeof(KGScriptTape), cudaMemcpyHostToDevice);
    cudaMalloc((void**)&d_kag_decoded_actions,
        (size_t)2 * g_kag_num_matches * sizeof(KGAction));
    cudaMalloc((void**)&d_kag_opening_rng,
        (size_t)g_kag_num_matches * sizeof(uint32_t));
    cudaMalloc((void**)&d_kag_bank_completed,
        (size_t)(g_kag_bank_count + 1) * sizeof(int));
    cudaMemset(d_kag_bank_completed, 0,
        (size_t)(g_kag_bank_count + 1) * sizeof(int));
    uint32_t* host_rng = (uint32_t*)std::calloc(
        (size_t)g_kag_num_matches, sizeof(uint32_t));
    if (!host_rng) std::abort();
    for (int match = 0; match < g_kag_num_matches; match++) {
        uint64_t seed = h_kag_cuda_config.base_seed
            ^ 0x9e3779b97f4a7c15ULL * ((uint64_t)match + 1ULL);
        host_rng[match] = (uint32_t)seed ^ 0xa511e9b3u;
    }
    cudaMemcpy(d_kag_opening_rng, host_rng,
        (size_t)g_kag_num_matches * sizeof(uint32_t),
        cudaMemcpyHostToDevice);
    std::free(host_rng);
    kag_cuda_check("device environment allocation");
    std::free(host_shells);
    std::free(host_rows);
    std::free(cursors);
    return shells;
}

static void puf_envs_reset(Env* envs, obs_t* observations, float* rewards,
        float* terminals, int total_agents) {
    if (total_agents != g_kag_total_agents) std::abort();
    if (d_kag_bank_completed) {
        cudaMemset(d_kag_bank_completed, 0,
            (size_t)(g_kag_bank_count + 1) * sizeof(int));
    }
    cudaMemset(rewards, 0, (size_t)total_agents * sizeof(float));
    cudaMemset(terminals, 0, (size_t)total_agents * sizeof(float));
    if (!g_kag_actions || !g_kag_masks) {
        std::fprintf(stderr, "Kaggriculture GPU buffers were not bound\n");
        std::exit(1);
    }
    g_kag_observations = observations;
    g_kag_rewards = rewards;
    g_kag_terminals = terminals;
    kag_cuda_clear_shells_kernel<<<kag_cuda_grid(total_agents),
        KAG_CUDA_BLOCK>>>(envs, total_agents);
    kag_cuda_reset_kernel<<<kag_cuda_grid(g_kag_num_matches),
        KAG_CUDA_BLOCK>>>(envs, d_kag_matches, d_kag_rows,
            observations, g_kag_actions, rewards, terminals,
            g_kag_masks, d_kag_tapes, g_kag_num_matches,
            d_kag_opening_rng);
    g_kag_bound = 1;
}

static void puf_envs_step(Env* envs, const float* actions,
        obs_t* observations, float* rewards, float* terminals,
        int start, int count, cudaStream_t stream) {
    if (start != 0 || count != g_kag_total_agents) {
        std::fprintf(stderr, "Kaggriculture GPU requires full-batch stepping\n");
        std::exit(1);
    }
    if (!g_kag_bound || actions != g_kag_actions
            || observations != g_kag_observations
            || rewards != g_kag_rewards || terminals != g_kag_terminals) {
        std::fprintf(stderr, "Kaggriculture GPU buffer binding changed\n");
        std::exit(1);
    }
    kag_cuda_step_kernel<<<kag_cuda_grid(g_kag_num_matches),
        KAG_CUDA_BLOCK, 0, stream>>>(envs, d_kag_matches,
            d_kag_rows, d_kag_tapes, d_kag_decoded_actions,
            g_kag_num_matches, d_kag_opening_rng, d_kag_bank_completed);
}

static void puf_envs_bind_buffers(float* actions, unsigned char* masks) {
    g_kag_actions = actions;
    g_kag_masks = masks;
}

/* Per-bank completed-episode counters feed host-side selfplay rotation.
 * Reads are cheap (a few ints) and only happen on the train-log cadence. */
static void puf_envs_selfplay_counts(int* out, int num_banks) {
    if (num_banks > 0 && d_kag_bank_completed) {
        cudaMemcpy(out, d_kag_bank_completed + 1,
            (size_t)num_banks * sizeof(int), cudaMemcpyDeviceToHost);
    }
}

static void puf_envs_selfplay_clear(int bank) {
    if (bank > 0 && d_kag_bank_completed) {
        cudaMemset(d_kag_bank_completed + bank, 0, sizeof(int));
    }
}

static void puf_envs_close(Env* envs) {
    if (d_kag_decoded_actions) cudaFree(d_kag_decoded_actions);
    if (d_kag_tapes) cudaFree(d_kag_tapes);
    if (d_kag_rows) cudaFree(d_kag_rows);
    if (d_kag_matches) cudaFree(d_kag_matches);
    if (d_kag_opening_rng) cudaFree(d_kag_opening_rng);
    if (d_kag_bank_completed) cudaFree(d_kag_bank_completed);
    cudaFree(envs);
    d_kag_tapes = nullptr;
    d_kag_rows = nullptr;
    d_kag_matches = nullptr;
    d_kag_decoded_actions = nullptr;
    d_kag_opening_rng = nullptr;
    d_kag_bank_completed = nullptr;
    g_kag_bound = 0;
    g_kag_total_agents = 0;
    g_kag_num_matches = 0;
    g_kag_bank_count = 0;
}

#define PUF_GPU_ENV_BIND_BUFFERS 1

#endif
