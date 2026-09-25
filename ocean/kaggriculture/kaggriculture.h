#pragma once

#define PUF_PACKED_MASK 1
#include "policy.h"
typedef float obs_t;
#include "pufferenv.h"

#define OBS_SIZE KAG_ENTITY_OBS_SIZE
#define NUM_ATNS KAG_ACTION_HEADS
#define ACT_SIZES KAG_ACTION_SIZES

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

typedef struct KagStartMetrics {
    uint32_t production, crop, animal, plants, animal_places, deaths;
    int plots;
} KagStartMetrics;

typedef struct KagRewards {
    int peaks[3];
    float growth[3], alive, quality, total;
} KagRewards;

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
        env->reward[p] = (KagRewards){.peaks = {s->peak_plots, s->peak_crops, s->peak_animals}};
        env->start[p] = (KagStartMetrics){
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

typedef struct KagStateBankHeader {
    char magic[8];
    uint32_t version, state_version, state_size, count;
    uint64_t reserved;
} KagStateBankHeader;
#ifdef __cplusplus
static_assert(sizeof(KagStateBankHeader) == 32, "Reset bank header ABI changed");
#else
_Static_assert(sizeof(KagStateBankHeader) == 32, "Reset bank header ABI changed");
#endif

KGState* kag_load_reset_bank(Env* envs, int games, Dict* kwargs) {
    if (envs[0].reset_probability == 0) {
        return NULL;
    }
    const char* path = dict_get_str(kwargs, "reset_state_bank");
    FILE* file = fopen(path, "rb");
    KagStateBankHeader header;
    assert(file && fread(&header, sizeof(header), 1, file) == 1);
    assert(!memcmp(header.magic, "KGRSTB1\0", 8) && header.version == 1);
    assert(header.state_version == KG_STATE_SERIALIZATION_VERSION &&
        header.state_size == sizeof(KGState) && header.count > 0 && !header.reserved);
    KGState* records = (KGState*)malloc((size_t)header.count * sizeof(KGState));
    assert(records && fread(records, sizeof(KGState), header.count, file) == header.count);
    assert(fgetc(file) == EOF);
    fclose(file);
    for (uint32_t i = 0; i < header.count; i++) {
        assert(kg_state_snapshot_valid(records + i) && !records[i].done);
        assert(!memcmp(&records[i].config, &envs[0].game.config, offsetof(KGConfig, seed)));
    }
    for (int i = 0; i < games; i++) {
        envs[i].reset_states = records;
        envs[i].reset_count = header.count;
    }
    printf("Loaded %u replay reset states from %s (prob=%.3f)\n",
        header.count, path, envs[0].reset_probability);
    return records;
}

// Same game/seat assignment for the CPU and GPU vectorizers.
void kag_policy_assignment(Env* envs, int games, Dict* kwargs) {
    int policies = dict_get(kwargs, "num_policies");
    if (policies <= 1) {
        return;
    }
    float fraction = dict_get(kwargs, "hist_policy_percent");
    int frozen_start = games - (int)(fraction * games);
    assert(envs[0].num_agents == 2 && "checkpoint opponents need env.num_agents=2");
    assert(fraction > 0 && fraction <= 1 && games - frozen_start >= policies - 1);
    for (int i = frozen_start; i < games; i++) {
        int index = i - frozen_start;
        int seat = (index / (policies - 1)) % 2;
        envs[i].tag = 1 + index % (policies - 1);
        envs[i].agents[seat].policy = envs[i].tag;
    }
}

KG_HD void kag_observe(Env* env, bool reset) {
    if (reset) {
        env->log = (Log){0};
        kag_reset_episode(env);
    }
    for (int a = 0; a < env->num_agents; a++) {
        Agent* agent = &env->agents[a];
        int player = env->num_agents == 2 ? a : env->learner_seat;
        if (reset) {
            *agent->rewards = *agent->terminals = 0;
        }
        kag_write_observation(
            &env->policy, &env->game, player, agent->observations);
    }
}

KG_HD int kag_reward_cap(int n, int cap) {
    return n < cap ? n : cap;
}

// Replay builders supply expert primitive commands; PPO supplies decoded macros.
// Keep the terminal state available to the caller before any episode reset.
KG_HD void kag_apply_actions(Env* env, const KGAction* commands) {
    KGState* game = &env->game;
    kg_step(game, commands);
    kag_policy_step(&env->policy, game);
    for (int a = 0; a < env->num_agents; a++) {
        Agent* agent = &env->agents[a];
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
        *agent->rewards = growth + alive;
        *agent->rewards += cash;
        *agent->rewards += quality;
        r->alive += alive;
        r->quality += quality;
        r->total += *agent->rewards;
        *agent->terminals = game->done;
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
}

KG_HD void kag_step(Env* env) {
    KGState* game = &env->game;
    KGAction commands[KG_NUM_PLAYERS] = {0};
    for (int a = 0; a < env->num_agents; a++) {
        int player = env->num_agents == 2 ? a : env->learner_seat;
        kag_decode_multi_action(
            &commands[player], env->agents[a].actions, game, player, &env->policy);
    }
    if (env->num_agents == 1 && env->bot_policy == 1) {
        kg_rule_action(game, 1 - env->learner_seat, &commands[1 - env->learner_seat]);
    }
    kag_apply_actions(env, commands);
    if (game->done) {
        kag_reset_episode(env);
    }
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

Vector2 kag_v2(int x, int y) {
    return (Vector2){(float)x, (float)y};
}

Rectangle kag_rect(int x, int y, int width, int height) {
    return (Rectangle){(float)x, (float)y, (float)width, (float)height};
}

void kag_draw_crop_icon(int crop, int cx, int cy, int size) {
    if (crop == KG_WHEAT) {
        DrawLineEx(kag_v2(cx, cy + size/3), kag_v2(cx, cy - size/3), 3, (Color){111,75,33,255});
        for (int i = -1; i <= 1; i++) {
            DrawCircle(cx + i*5, cy - size/4 + abs(i)*5, 4, (Color){240,195,55,255});
        }
    } else if (crop == KG_CARROT) {
        DrawTriangle(kag_v2(cx-7, cy-5), kag_v2(cx+7, cy-5), kag_v2(cx, cy+15),
            (Color){244,125,32,255});
        DrawLine(cx, cy-5, cx-6, cy-14, (Color){52,133,62,255});
        DrawLine(cx, cy-5, cx+7, cy-14, (Color){52,133,62,255});
    } else if (crop == KG_TOMATO) {
        DrawCircle(cx, cy+2, size*.26f, (Color){216,55,45,255});
        DrawTriangle(kag_v2(cx, cy-10), kag_v2(cx-8, cy-2), kag_v2(cx+8, cy-2),
            (Color){51,125,54,255});
    } else if (crop == KG_STRAWBERRY) {
        DrawTriangle(kag_v2(cx-10, cy-7), kag_v2(cx+10, cy-7), kag_v2(cx, cy+15),
            (Color){225,50,72,255});
        DrawCircle(cx, cy-6, 7, (Color){225,50,72,255});
        DrawLine(cx, cy-8, cx, cy-15, (Color){45,125,55,255});
    } else {
        DrawCircle(cx, cy, size*.29f, (Color){49,137,70,255});
        DrawCircleLines(cx, cy, size*.29f, (Color){24,84,44,255});
        DrawLine(cx-5, cy-size/4, cx-5, cy+size/4, (Color){140,190,70,255});
        DrawLine(cx+5, cy-size/4, cx+5, cy+size/4, (Color){140,190,70,255});
    }
}

void kag_draw_tile(const KGTile* tile, int px, int py, int cell,
        int checker) {
    Color soil = checker ? (Color){193,143,91,255} : (Color){203,153,99,255};
    if (tile->kind == KG_TILE_LOCKED) {
        DrawRectangle(px, py, cell-1, cell-1, (Color){104,105,100,255});
        BeginScissorMode(px, py, cell-1, cell-1);
        for (int d = -cell; d < cell*2; d += 14) {
            DrawLine(px+d, py, px+d-cell, py+cell, (Color){72,74,71,150});
        }
        EndScissorMode();
    } else {
        DrawRectangle(px, py, cell-1, cell-1, soil);
        for (int row = 8; row < cell; row += 10) {
            DrawLine(px+4, py+row, px+cell-5, py+row-2, (Color){163,111,70,100});
        }
    }
    DrawRectangleLines(px, py, cell, cell, (Color){83,112,70,255});
    int cx = px + cell/2;
    int cy = py + cell/2;
    if (tile->kind == KG_TILE_PLANT) {
        kag_draw_crop_icon(tile->crop, cx, cy, cell);
        if (tile->watered_today) DrawCircle(px+cell-8, py+8, 4, (Color){52,151,220,255});
        if (tile->yield_units) {
            DrawText(TextFormat("%d", tile->yield_units), px+3, py+2, 11, (Color){56,38,24,255});
        }
    } else if (tile->kind == KG_TILE_WEED) {
        for (int i = -2; i <= 2; i++) {
            DrawLine(cx, cy+12, cx+i*5, cy-10+abs(i)*3, (Color){35,91,42,255});
        }
    } else if (tile->kind == KG_TILE_COOP || tile->kind == KG_TILE_PASTURE) {
        Color building = tile->kind == KG_TILE_COOP
            ? (Color){205,150,77,255} : (Color){102,157,80,255};
        DrawRectangle(cx-15, cy-12, 30, 25, building);
        DrawTriangle(kag_v2(cx-18, cy-12), kag_v2(cx+18, cy-12), kag_v2(cx, cy-24),
            (Color){130,67,43,255});
        if (tile->animal >= 0) {
            const char* glyph = tile->animal == KG_GOOSE ? "G" : tile->animal == KG_COW ? "C" : "S";
            DrawCircle(cx, cy, 11, RAYWHITE);
            DrawText(glyph, cx-4, cy-7, 14, (Color){44,39,34,255});
            if (tile->fed_today) DrawCircle(px+cell-8, py+8, 4, (Color){72,176,86,255});
        }
    }
}

void kag_draw_worker(int cx, int cy, int id, int selected) {
    int offset_x = ((id % 3) - 1) * 8;
    int offset_y = ((id / 3) % 3 - 1) * 6;
    cx += offset_x;
    cy += offset_y;
    Color shirt = id == 0 ? (Color){210,58,53,255} : (Color){43,119,194,255};
    DrawCircle(cx, cy-6, 5, (Color){245,200,151,255});
    DrawRectangle(cx-5, cy-1, 10, 12, shirt);
    if (selected) DrawCircleLines(cx, cy+1, 13, GOLD);
}

void kag_render_farm(const KGState* game, int player,
        int origin_x, int origin_y, int cell, int selected_unit) {
    const KGPlayer* farm = &game->players[player];
    for (int y = 0; y < game->config.board_size; y++) {
        for (int x = 0; x < game->config.board_size; x++) {
            const KGTile* tile = &farm->tiles[y * KG_MAX_BOARD_SIZE + x];
            kag_draw_tile(tile, origin_x+x*cell, origin_y+y*cell, cell, (x+y)&1);
        }
    }
    int half = game->config.board_size/2;
    DrawRectangleLinesEx(kag_rect(origin_x, origin_y, cell*10, cell*10), 3, (Color){83,61,39,255});
    DrawLineEx(kag_v2(origin_x+half*cell, origin_y),
        kag_v2(origin_x+half*cell, origin_y+10*cell), 3, (Color){91,65,42,255});
    DrawLineEx(kag_v2(origin_x, origin_y+half*cell),
        kag_v2(origin_x+10*cell, origin_y+half*cell), 3, (Color){91,65,42,255});

    int shed_x = origin_x + half*cell - 18;
    int shed_y = origin_y + half*cell - 18;
    DrawRectangle(shed_x, shed_y, 36, 36, (Color){173,67,47,255});
    DrawTriangle(kag_v2(shed_x-5, shed_y), kag_v2(shed_x+41, shed_y),
        kag_v2(shed_x+18, shed_y-17), (Color){108,48,36,255});
    DrawRectangle(shed_x+13, shed_y+17, 10, 19, (Color){91,52,35,255});

    for (int unit = farm->unit_count-1; unit >= 0; unit--) {
        const KGUnitState* worker = &farm->units[unit];
        kag_draw_worker(origin_x+worker->x*cell+cell/2,
            origin_y+worker->y*cell+cell/2, unit, selected_unit == unit);
    }
}

void kag_render_inventory(const KGState* game, int player,
        int x, int y, int width, const char* name) {
    const KGPlayer* farm = &game->players[player];
    DrawRectangleRounded(kag_rect(x, y, width, 218), .04f, 6, (Color){240,214,164,255});
    DrawRectangleRoundedLines(kag_rect(x, y, width, 218), .04f, 6, (Color){114,82,47,255});
    DrawText(TextFormat("P%d  %s", player+1, name ? name : "agent"),
        x+15, y+12, 20, (Color){45,36,27,255});
    DrawText(TextFormat("$%d   hands %d   land %d/4", farm->money,
        farm->hand_count, __builtin_popcount((unsigned)farm->unlocked_mask)),
        x+15, y+38, 18, (Color){71,49,29,255});
    DrawText("SHED", x+15, y+69, 14, (Color){105,73,39,255});
    for (int item = 0; item < KG_NUM_PRODUCTS; item++) {
        int col = item % 3;
        int row = item / 3;
        const char* label = KG_PRODUCT_NAMES[item];
        DrawRectangle(x+15+col*(width-30)/3, y+89+row*33,
            (width-42)/3, 27, (Color){216,181,127,255});
        DrawText(TextFormat("%.4s %d", label, farm->shed[item]),
            x+21+col*(width-30)/3, y+95+row*33, 13, (Color){55,42,29,255});
    }
    DrawText("SEEDS", x+15, y+190, 13, (Color){105,73,39,255});
    DrawText(TextFormat("W%d C%d T%d S%d M%d", farm->seeds[0], farm->seeds[1],
        farm->seeds[2], farm->seeds[3], farm->seeds[4]), x+75, y+190, 13, (Color){55,42,29,255});
}

void kag_render_town(const KGState* game, int x, int y, int width) {
    DrawRectangleRounded(kag_rect(x, y, width, 490), .04f, 6, (Color){219,181,151,255});
    DrawRectangleRoundedLines(kag_rect(x, y, width, 490), .04f, 6, (Color){105,69,50,255});
    DrawText("KAGGRICULTURE", x+56, y+22, 24, (Color){73,43,30,255});
    DrawText(TextFormat("DAY %02d / 30", game->day+1), x+103, y+66, 22, (Color){73,43,30,255});
    DrawText(TextFormat("TURN %02d / 24", game->hour+1), x+96, y+94, 18, (Color){92,57,36,255});
    DrawRectangle(x+20, y+132, width-40, 2, (Color){155,107,78,255});
    DrawText("MARKET", x+20, y+151, 17, (Color){73,43,30,255});
    for (int item = 0; item < KG_NUM_PRODUCTS; item++) {
        int col = item / 5;
        int row = item % 5;
        DrawText(TextFormat("%-5.5s $%d", KG_PRODUCT_NAMES[item], game->market.prices[item]),
            x+20+col*165, y+180+row*25, 14, (Color){58,44,34,255});
    }
    DrawText("TOWN SHOPS", x+20, y+322, 17, (Color){73,43,30,255});
    if (!game->shop_count) DrawText("Town center only", x+20, y+350, 14, (Color){92,68,52,255});
    for (int shop = 0; shop < game->shop_count && shop < 5; shop++) {
        DrawText(KG_SHOP_NAMES[game->unlocked_shops[shop]], x+20,
            y+348+shop*23, 14, (Color){58,44,34,255});
    }
}

void kag_render_game(const KGState* game) {
    if (!IsWindowReady()) {
        InitWindow(1440, 900, "PufferLib Kaggriculture");
        assert(IsWindowReady());
        SetTargetFPS(12);
    }
    BeginDrawing();
    ClearBackground((Color){132,172,103,255});
    DrawRectangle(0, 0, 1440, 60, (Color){24,42,35,255});
    DrawText("PUFFERLIB KAGGRICULTURE", 24, 16, 25, RAYWHITE);
    DrawText(TextFormat("step %d   ESC quit", game->step),
        1100, 20, 16, (Color){210,226,211,255});
    for (int p = 0; p < 2; p++) {
        int x = p ? 920 : 20;
        DrawText(TextFormat("P%d   $%d", p + 1, game->players[p].money),
            x, 72, 20, (Color){39,50,31,255});
        kag_render_farm(game, p, x, 100, 50, -1);
        kag_render_inventory(game, p, x, 625, 500, "policy / opponent");
    }
    kag_render_town(game, 540, 100, 360);
    DrawRectangleRounded((Rectangle){540,625,360,218}, .04f, 6, (Color){237,221,183,255});
    DrawText("WHAT TO WATCH", 560, 645, 18, (Color){70,50,33,255});
    DrawText("Workers, production and deliveries", 560, 678, 15, (Color){70,50,33,255});
    DrawText("Blue dot = watered; green = fed", 560, 715, 14, (Color){70,50,33,255});
    DrawText("Number = available harvest yield", 560, 750, 14, (Color){70,50,33,255});
    DrawText("Display updates once per rollout", 560, 790, 14, (Color){70,50,33,255});
    DrawText("Native simulator state; ESC quits evaluation", 20, 872, 14, (Color){29,55,38,255});
    EndDrawing();
}

void puf_render(Env* env) {
#if PUF_BACKEND == PUF_GPU
    KGState game;
    assert(cudaDeviceSynchronize() == cudaSuccess);
    assert(cudaMemcpy(&game, &env->game, sizeof(game), cudaMemcpyDeviceToHost) == cudaSuccess);
    kag_render_game(&game);
#else
    kag_render_game(&env->game);
#endif
}

#if PUF_BACKEND == PUF_CPU
#define MY_VEC_INIT
#define MY_VEC_CLOSE

struct {
    KGState* reset_states;
#ifdef __CUDACC__
    Env* device_envs;
    int* host_rows;
    int* device_rows;
#endif
} kag_cpu;

void puf_reset(Env* env) {
    kag_observe(env, true);
}

void puf_step(Env* env) {
    kag_step(env);
    kag_observe(env, false);
}

Env* my_vec_init(int* size, int* starts, int* counts, Dict* vk, Dict* ek) {
    int agents = dict_get(ek, "num_agents");
    int total = dict_get(vk, "total_agents");
    int buffers = dict_get(vk, "num_buffers");
    assert((agents == 1 || agents == 2) && total % (buffers * agents) == 0);
    int games = total / agents;
    Env* envs = NULL;
#ifdef __CUDACC__
    assert(cudaHostAlloc((void**)&envs, games * sizeof(Env), cudaHostAllocPortable) == cudaSuccess);
    assert(cudaMalloc((void**)&kag_cpu.device_envs, games * sizeof(Env)) == cudaSuccess);
    assert(cudaMalloc((void**)&kag_cpu.device_rows, total * sizeof(int)) == cudaSuccess);
    assert(cudaHostAlloc((void**)&kag_cpu.host_rows,
        total * sizeof(int), cudaHostAllocPortable) == cudaSuccess);
#else
    envs = (Env*)malloc(games * sizeof(Env));
#endif
    memset(envs, 0, games * sizeof(Env));
    for (int i = 0; i < games; i++) {
        envs[i].rng = i;
        puf_init(envs + i, ek);
#ifdef __CUDACC__
        envs[i].sampling_rows = kag_cpu.device_rows;
#endif
    }
    for (int buf = 0; buf < buffers; buf++) {
        starts[buf] = buf * games / buffers;
        counts[buf] = games / buffers;
        kag_policy_assignment(envs + starts[buf], counts[buf], vk);
    }
    kag_cpu.reset_states = kag_load_reset_bank(envs, games, ek);
#ifdef __CUDACC__
    assert(cudaMemcpy(kag_cpu.device_envs, envs,
        games * sizeof(Env), cudaMemcpyHostToDevice) == cudaSuccess);
#endif
    *size = games;
    return envs;
}

#ifdef __CUDACC__
Env* kag_sampling_envs(Env* envs) {
    return kag_cpu.device_envs;
}

// Keep prefix-dependent sampling on device. Each worker uploads only its own
// games after simulation, on the same stream as inference; no cross-buffer race.
void kag_upload_sampling(Env* envs, obs_t* observations, int start, int rows,
        cudaStream_t stream) {
    int agents = envs[0].num_agents;
    int first = start / agents;
    int games = rows / agents;
    for (int i = first; i < first + games; i++) {
        for (int a = 0; a < agents; a++) {
            int row = (envs[i].agents[a].observations - observations) / OBS_SIZE;
            envs[i].rows[a] = row;
            kag_cpu.host_rows[row] = 2 * i + (agents == 2 ? a : envs[i].learner_seat);
        }
    }
    assert(cudaMemcpyAsync(kag_cpu.device_rows + start, kag_cpu.host_rows + start,
        rows * sizeof(int), cudaMemcpyHostToDevice, stream) == cudaSuccess);
    // Only game/policy are mutable sampler inputs. In particular, never rewrite
    // env 0's shared row-map pointer while another buffer is sampling from it.
    assert(cudaMemcpy2DAsync(&kag_cpu.device_envs[first].game, sizeof(Env),
        &envs[first].game, sizeof(Env),
        offsetof(Env, policy) + sizeof(KagPolicy) - offsetof(Env, game),
        games, cudaMemcpyHostToDevice, stream) == cudaSuccess);
}
#endif

void puf_close(Env* env) {
}

void my_vec_close(Env* envs) {
    if (IsWindowReady()) {
        CloseWindow();
    }
    free(kag_cpu.reset_states);
#ifdef __CUDACC__
    assert(cudaFree(kag_cpu.device_envs) == cudaSuccess);
    assert(cudaFree(kag_cpu.device_rows) == cudaSuccess);
    assert(cudaFreeHost(kag_cpu.host_rows) == cudaSuccess);
    assert(cudaFreeHost(envs) == cudaSuccess);
#else
    free(envs);
#endif
}
#endif
