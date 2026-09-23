#pragma once

#define PUF_BACKEND PUF_GPU
#include <cuda_runtime.h>
#include "policy.h"
typedef float obs_t;
#include "pufferenv.h"

#define OBS_SIZE KAG_ENTITY_OBS_SIZE
#define NUM_ATNS KAG_ACTION_HEADS
#define ACT_SIZES KAG_ACTION_SIZES
#define KAG_GPU_THREADS 128

struct Log {
    float perf, score, opponent_score, cash_gain;
    float episode_return, episode_length, draw_rate;
    float production_units, crop_units, animal_units;
    float plants, animal_places, land_purchases, neglect_deaths;
    float start_money, start_plots, ending_plots;
    float root_games, reset_games, root_money, reset_money;
    float root_cash_gain, reset_cash_gain, root_steps, reset_steps;
    float terminal_cash_reward, growth_land_reward, growth_crop_reward, growth_animal_reward;
    float alive_reward, dense_quality_reward;
    float policy_0_score, policy_1_score, checkpoint_fraction;
    float n;
};

struct KagStartMetrics {
    uint32_t production, crop, animal, plants, animal_places, deaths;
    int plots;
};

struct KagRewards {
    int peaks[3];
    float growth[3], alive, quality, total;
};

struct Env {
    Log log;
    Agent agents[KG_NUM_PLAYERS];
    int num_agents, tag, boundary_reached;
    int rows[KG_NUM_PLAYERS];
    int* sampling_rows;
    unsigned int rng;
    KGState game;
    KagPolicy policy;
    int bot_policy, learner_seat;
    float reward_money;
    KGState* reset_states;
    int reset_count;
    float reset_probability;
    KagStartMetrics start[KG_NUM_PLAYERS];
    float growth[3], alive_daily, quality_scale, quality_idle_cost;
    int targets[3];
    KagRewards reward[KG_NUM_PLAYERS];
};

struct {
    int num_games;
    obs_t* observations;
    float* actions;
    float* rewards;
    float* terminals;
    cudaStream_t stream;
    KGState* reset_states;
} kag_gpu;

KG_HD void kag_reset_episode(Env* env) {
    env->rng = 1664525u * env->rng + 1013904223u;
    int source =
        env->reset_count > 0 && (env->rng >> 8) < (uint32_t)(env->reset_probability * 16777216.0f);
    if (source) {
        env->rng = 1664525u * env->rng + 1013904223u;
        env->game = env->reset_states[env->rng % env->reset_count];
    } else {
        env->game.config.seed = env->rng;
        kg_reset(&env->game);
    }
    kag_policy_reset(&env->policy, &env->game, source);
    for (int p = 0; p < KG_NUM_PLAYERS; p++) {
        KGState* g = &env->game;
        KagObservationState* s = &env->policy.history[p];
        env->reward[p] = {.peaks = {s->peak_plots, s->peak_crops, s->peak_animals}};
        env->start[p] = {
            .production = g->production_units[p],
            .plants = g->planted_crops[p],
            .animal_places = g->placed_animals[p],
            .deaths = g->neglect_deaths[p],
            .plots = kag_popcount(g->players[p].unlocked_mask),
        };
        for (int item = 0; item < KG_NUM_PRODUCTS; item++) {
            if (item < KG_NUM_CROPS) {
                env->start[p].crop += g->production_product_units[p][item];
            } else {
                env->start[p].animal += g->production_product_units[p][item];
            }
        }
    }
}

void puf_init(Env* env, Dict* kwargs) {
    env->num_agents = dict_get(kwargs, "num_agents");
    env->bot_policy = dict_get(kwargs, "bot_policy");
    env->learner_seat = dict_get(kwargs, "learner_seat");
    env->reward_money = dict_get(kwargs, "reward_money");
    env->reset_probability = dict_get(kwargs, "reset_state_prob");
    const char* growth[] = {"reward_growth_land", "reward_growth_crop", "reward_growth_animal"};
    const char* targets[] = {"reward_target_plots", "reward_target_crops", "reward_target_animals"};
    for (int i = 0; i < 3; i++) {
        env->growth[i] = dict_get(kwargs, growth[i]);
        env->targets[i] = dict_get(kwargs, targets[i]);
        assert(isfinite(env->growth[i]) && env->growth[i] >= 0);
        assert(env->targets[i] >= 0 && env->targets[i] <= KG_MAX_TILES);
    }
    assert(env->targets[0] >= 1 && env->targets[0] <= 4);
    env->alive_daily = dict_get(kwargs, "reward_alive_daily");
    env->quality_scale = dict_get(kwargs, "reward_quality_scale");
    env->quality_idle_cost = dict_get(kwargs, "reward_quality_idle_cost");
    assert(isfinite(env->alive_daily) && env->alive_daily >= 0);
    assert(isfinite(env->quality_scale) && env->quality_scale >= 0);
    assert(env->quality_idle_cost >= 0 && env->quality_idle_cost <= 1);
    env->policy = (KagPolicy){
        .market_slots = (int)dict_get(kwargs, "market_slots"),
        .max_hands = (int)dict_get(kwargs, "max_hands"),
        .land_buy_min_days = (int)dict_get(kwargs, "land_buy_min_days"),
    };
    assert(env->num_agents == 1 || env->num_agents == 2);
    assert(env->bot_policy == 0 || env->bot_policy == 1);
    assert(env->learner_seat == 0 || env->learner_seat == 1);
    assert(isfinite(env->reward_money) && env->reward_money >= 0);
    assert(isfinite(env->reset_probability) && env->reset_probability >= 0 &&
           env->reset_probability <= 1);
    KGConfig config;
    kg_config_default(&config);
    config.seed = env->rng;
    kg_init(&env->game, &config);
    kag_policy_reset(&env->policy, &env->game, 0);
    env->start[0].plots = env->start[1].plots = 1;
    env->reward[0].peaks[0] = env->reward[1].peaks[0] = 1;
}

struct KagStateBankHeader {
    char magic[8];
    uint32_t version, state_version, state_size, count;
    uint64_t reserved;
};
static_assert(sizeof(KagStateBankHeader) == 32, "Reset bank header ABI changed");

Env* puf_vec_create(int total_agents, Dict* kwargs, obs_t* observations, float* actions,
    float* rewards, float* terminals) {
    int num_agents = dict_get(kwargs, "num_agents");
    assert(num_agents == 1 || num_agents == 2);
    assert(total_agents > 0 && total_agents % num_agents == 0);
    int num_games = total_agents / num_agents;
    Env* host = (Env*)calloc(num_games, sizeof(Env));
    int* rows = (int*)malloc(total_agents * sizeof(int));
    int* sampling_rows = NULL;
    assert(cudaMalloc((void**)&sampling_rows, total_agents * sizeof(int)) == cudaSuccess);
    assert(host);
    for (int i = 0; i < num_games; i++) {
        host[i].rng = i;
        puf_init(&host[i], kwargs);
        host[i].sampling_rows = sampling_rows;
        for (int a = 0; a < num_agents; a++) {
            int row = i * num_agents + a;
            host[i].rows[a] = row;
            rows[row] = 2 * i + (num_agents == 2 ? a : host[i].learner_seat);
        }
    }
    assert(cudaMemcpy(sampling_rows, rows, total_agents * sizeof(int), cudaMemcpyHostToDevice) ==
           cudaSuccess);
    free(rows);
    KGState* bank = NULL;
    if (host[0].reset_probability > 0) {
        const char* path = dict_get_str(kwargs, "reset_state_bank");
        FILE* file = fopen(path, "rb");
        KagStateBankHeader header;
        assert(file && fread(&header, sizeof(header), 1, file) == 1);
        assert(!memcmp(header.magic, "KGRSTB1\0", 8) && header.version == 1);
        assert(header.state_version == KG_STATE_SERIALIZATION_VERSION &&
               header.state_size == sizeof(KGState) && header.count > 0 && !header.reserved);
        size_t bytes = (size_t)header.count * sizeof(KGState);
        KGState* records = (KGState*)malloc(bytes);
        assert(records && fread(records, sizeof(KGState), header.count, file) == header.count);
        assert(fgetc(file) == EOF);
        fclose(file);
        for (uint32_t i = 0; i < header.count; i++) {
            assert(kg_state_snapshot_valid(records + i) && !records[i].done);
            // Config seed is last; every rule before it must match this build.
            assert(!memcmp(&records[i].config, &host[0].game.config, offsetof(KGConfig, seed)));
        }
        assert(cudaMalloc((void**)&bank, bytes) == cudaSuccess);
        assert(cudaMemcpy(bank, records, bytes, cudaMemcpyHostToDevice) == cudaSuccess);
        free(records);
        for (int i = 0; i < num_games; i++) {
            host[i].reset_states = bank;
            host[i].reset_count = header.count;
        }
        printf("Loaded %u replay reset states from %s (prob=%.3f)\n", header.count, path,
            host[0].reset_probability);
    }
    Env* envs = NULL;
    assert(cudaMalloc((void**)&envs, num_games * sizeof(Env)) == cudaSuccess);
    assert(cudaMemcpy(envs, host, num_games * sizeof(Env), cudaMemcpyHostToDevice) == cudaSuccess);
    free(host);
    kag_gpu = {num_games, observations, actions, rewards, terminals, 0, bank};
    return envs;
}

// Group physical rows by policy, as the CPU env_setup path does. Games keep
// their own seat order; both observation/action IO and prefix sampling use it.
void kag_assign_policies(Env* envs, Dict* kwargs, int* layout) {
    int policies = dict_get(kwargs, "num_policies");
    if (policies <= 1) {
        return;
    }
    int games = kag_gpu.num_games;
    float fraction = dict_get(kwargs, "hist_policy_percent");
    int frozen_start = games - (int)(fraction * games);
    assert(fraction > 0 && fraction <= 1 && games - frozen_start >= policies - 1);
    Env* host = (Env*)malloc(games * sizeof(Env));
    assert(cudaMemcpy(host, envs, games * sizeof(Env), cudaMemcpyDeviceToHost) == cudaSuccess);
    assert(host[0].num_agents == 2 && "checkpoint opponents need env.num_agents=2");
    int* counts = (int*)calloc(policies, sizeof(int));
    int* rows = (int*)malloc(2 * games * sizeof(int));
    for (int i = 0; i < games; i++) {
        if (i >= frozen_start) {
            int index = i - frozen_start;
            int opponent_seat = (index / (policies - 1)) % 2;
            host[i].tag = 1 + index % (policies - 1);
            host[i].agents[opponent_seat].policy = host[i].tag;
        }
        for (int a = 0; a < 2; a++) {
            counts[host[i].agents[a].policy]++;
        }
    }
    layout[0] = 0;
    for (int p = 0; p < policies; p++) {
        layout[p + 1] = layout[p] + counts[p];
        counts[p] = layout[p];
    }
    for (int i = 0; i < games; i++) {
        for (int a = 0; a < 2; a++) {
            int row = counts[host[i].agents[a].policy]++;
            host[i].rows[a] = row;
            rows[row] = 2 * i + a;
        }
    }
    assert(cudaMemcpy(host[0].sampling_rows, rows, 2 * games * sizeof(int),
               cudaMemcpyHostToDevice) == cudaSuccess);
    assert(cudaMemcpy(envs, host, games * sizeof(Env), cudaMemcpyHostToDevice) == cudaSuccess);
    printf("GPU policy rows:");
    for (int p = 0; p < policies; p++) {
        printf(" %d:%d", p, layout[p + 1] - layout[p]);
    }
    printf(" (checkpoint games %.3f, balanced seats)\n", fraction);
    free(rows);
    free(counts);
    free(host);
}

void puf_bind_stream(cudaStream_t stream) {
    kag_gpu.stream = stream;
}

__global__ void kag_observe_kernel(
    Env* envs, obs_t* observations, float* rewards, float* terminals, int num_games, bool reset) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= num_games) {
        return;
    }
    Env* env = envs + i;
    if (reset) {
        env->log = (Log){0};
        kag_reset_episode(env);
    }
    for (int a = 0; a < env->num_agents; a++) {
        int row = env->rows[a];
        int player = env->num_agents == 2 ? a : env->learner_seat;
        if (reset) {
            rewards[row] = terminals[row] = 0;
        }
        kag_write_observation(
            &env->policy, &env->game, player, observations + (long)row * OBS_SIZE);
    }
}

KG_HD int kag_reward_cap(int n, int cap) {
    return n < cap ? n : cap;
}

__global__ void kag_step_kernel(
    Env* envs, const float* actions, float* rewards, float* terminals, int num_games) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= num_games) {
        return;
    }
    Env* env = envs + i;
    KGState* game = &env->game;
    KGAction commands[KG_NUM_PLAYERS] = {0};
    for (int a = 0; a < env->num_agents; a++) {
        int player = env->num_agents == 2 ? a : env->learner_seat;
        kag_decode_multi_action(
            &commands[player], actions + (long)env->rows[a] * NUM_ATNS, game, player, &env->policy);
    }
    if (env->num_agents == 1 && env->bot_policy == 1) {
        kg_rule_action(game, 1 - env->learner_seat, &commands[1 - env->learner_seat]);
    }
    kg_step(game, commands);
    kag_policy_step(&env->policy, game);
    for (int a = 0; a < env->num_agents; a++) {
        int row = env->rows[a];
        int player = env->num_agents == 2 ? a : env->learner_seat;
        KagObservationState* s = &env->policy.history[player];
        KagRewards* r = &env->reward[player];
        int peaks[] = {s->peak_plots, s->peak_crops, s->peak_animals};
        float growth = 0;
        for (int j = 0; j < 3; j++) {
            float bonus = env->growth[j] * (kag_reward_cap(peaks[j], env->targets[j]) -
                                               kag_reward_cap(r->peaks[j], env->targets[j]));
            r->growth[j] += bonus;
            growth += bonus;
            r->peaks[j] = peaks[j];
        }
        int active = (env->targets[1] > 0) + (env->targets[2] > 0);
        float alive = env->targets[1] > 0
                          ? (float)kag_reward_cap(s->crops, env->targets[1]) / env->targets[1]
                          : 0;
        alive += env->targets[2] > 0
                     ? (float)kag_reward_cap(s->animals, env->targets[2]) / env->targets[2]
                     : 0;
        alive = active ? env->alive_daily * alive / (active * game->config.turns_per_day) : 0;
        float quality = env->quality_scale * (s->coverage - env->quality_idle_cost * s->idle) /
                        game->config.episode_steps;
        float cash = game->done
                         ? env->reward_money * (game->players[player].money - s->start_cash) /
                               game->config.starting_money
                         : 0;
        rewards[row] = growth + alive;
        rewards[row] += cash;
        rewards[row] += quality;
        r->alive += alive;
        r->quality += quality;
        r->total += rewards[row];
        terminals[row] = game->done;
        if (!game->done || env->agents[a].policy != 0) {
            continue;
        }
        int money = game->players[player].money;
        int opponent_money = game->players[1 - player].money;
        int gain = money - env->policy.history[player].start_cash;
        int steps = game->step - env->policy.history[player].start_step;
        KagStartMetrics* start = &env->start[player];
        Log* log = &env->log;
        float win = money > opponent_money ? 1 : (money == opponent_money ? 0.5f : 0);
        log->perf += win;
        log->policy_0_score += win;
        log->policy_1_score += 1 - win;
        log->checkpoint_fraction += env->tag > 0;
        log->score += money;
        log->opponent_score += opponent_money;
        log->cash_gain += gain;
        log->episode_return += r->total;
        log->terminal_cash_reward += cash;
        log->growth_land_reward += r->growth[0];
        log->growth_crop_reward += r->growth[1];
        log->growth_animal_reward += r->growth[2];
        log->alive_reward += r->alive;
        log->dense_quality_reward += r->quality;
        log->episode_length += steps;
        log->draw_rate += money == opponent_money;
        log->production_units += game->production_units[player] - start->production;
        for (int product = 0; product < KG_NUM_PRODUCTS; product++) {
            if (product < KG_NUM_CROPS) {
                log->crop_units += game->production_product_units[player][product];
            } else {
                log->animal_units += game->production_product_units[player][product];
            }
        }
        log->crop_units -= start->crop;
        log->animal_units -= start->animal;
        log->plants += game->planted_crops[player] - start->plants;
        log->animal_places += game->placed_animals[player] - start->animal_places;
        int plots = kag_popcount(game->players[player].unlocked_mask);
        log->land_purchases += plots - start->plots;
        log->neglect_deaths += game->neglect_deaths[player] - start->deaths;
        log->start_money += env->policy.history[player].start_cash;
        log->start_plots += start->plots;
        log->ending_plots += plots;
        if (env->policy.reset_source) {
            log->reset_games++;
            log->reset_money += money;
            log->reset_cash_gain += gain;
            log->reset_steps += steps;
        } else {
            log->root_games++;
            log->root_money += money;
            log->root_cash_gain += gain;
            log->root_steps += steps;
        }
        log->n++;
    }
    if (game->done) {
        kag_reset_episode(env);
    }
}

void puf_reset(Env* envs) {
    int blocks = (kag_gpu.num_games + KAG_GPU_THREADS - 1) / KAG_GPU_THREADS;
    kag_observe_kernel<<<blocks, KAG_GPU_THREADS, 0, kag_gpu.stream>>>(
        envs, kag_gpu.observations, kag_gpu.rewards, kag_gpu.terminals, kag_gpu.num_games, true);
    assert(cudaGetLastError() == cudaSuccess);
}

void puf_step(Env* envs) {
    int blocks = (kag_gpu.num_games + KAG_GPU_THREADS - 1) / KAG_GPU_THREADS;
    kag_step_kernel<<<blocks, KAG_GPU_THREADS, 0, kag_gpu.stream>>>(
        envs, kag_gpu.actions, kag_gpu.rewards, kag_gpu.terminals, kag_gpu.num_games);
    kag_observe_kernel<<<blocks, KAG_GPU_THREADS, 0, kag_gpu.stream>>>(
        envs, kag_gpu.observations, kag_gpu.rewards, kag_gpu.terminals, kag_gpu.num_games, false);
    assert(cudaGetLastError() == cudaSuccess);
}

void puf_log(Log* log, Dict* out) {
    dict_set(out, "perf", log->perf);
    dict_set(out, "policy_0_score", log->policy_0_score);
    dict_set(out, "policy_1_score", log->policy_1_score);
    dict_set(out, "checkpoint_fraction", log->checkpoint_fraction);
    dict_set(out, "score", log->score);
    dict_set(out, "opponent_score", log->opponent_score);
    dict_set(out, "cash_gain", log->cash_gain);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "terminal_cash_reward", log->terminal_cash_reward);
    dict_set(out, "growth_land_reward", log->growth_land_reward);
    dict_set(out, "growth_crop_reward", log->growth_crop_reward);
    dict_set(out, "growth_animal_reward", log->growth_animal_reward);
    dict_set(out, "alive_reward", log->alive_reward);
    dict_set(out, "dense_quality_reward", log->dense_quality_reward);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "draw_rate", log->draw_rate);
    dict_set(out, "production_units", log->production_units);
    dict_set(out, "crop_units", log->crop_units);
    dict_set(out, "animal_units", log->animal_units);
    dict_set(out, "plants", log->plants);
    dict_set(out, "animal_places", log->animal_places);
    dict_set(out, "land_purchases", log->land_purchases);
    dict_set(out, "neglect_deaths", log->neglect_deaths);
    dict_set(out, "start_money", log->start_money);
    dict_set(out, "start_plots", log->start_plots);
    dict_set(out, "ending_plots", log->ending_plots);
    dict_set(out, "reset_fraction", log->reset_games);
    dict_set(out, "root_fraction", log->root_games);
    dict_set(out, "root_money", log->root_games ? log->root_money / log->root_games : 0);
    dict_set(out, "reset_money", log->reset_games ? log->reset_money / log->reset_games : 0);
    dict_set(out, "root_cash_gain", log->root_games ? log->root_cash_gain / log->root_games : 0);
    dict_set(
        out, "reset_cash_gain", log->reset_games ? log->reset_cash_gain / log->reset_games : 0);
    dict_set(out, "root_steps", log->root_games ? log->root_steps / log->root_games : 0);
    dict_set(out, "reset_steps", log->reset_games ? log->reset_steps / log->reset_games : 0);
}

void puf_render(Env* envs) {
    assert(0 && "Kaggriculture renderer is not ported; use headless train/eval");
}

void puf_close(Env* envs) {
    int* sampling_rows;
    assert(cudaMemcpy(&sampling_rows, &envs->sampling_rows, sizeof(sampling_rows),
               cudaMemcpyDeviceToHost) == cudaSuccess);
    assert(cudaFree(sampling_rows) == cudaSuccess);
    assert(cudaFree(kag_gpu.reset_states) == cudaSuccess);
    assert(cudaFree(envs) == cudaSuccess);
    kag_gpu = {};
}
