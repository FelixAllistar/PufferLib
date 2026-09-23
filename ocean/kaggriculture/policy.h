#pragma once

/* Policy ABI 5, observation v3, controller 2/2. This header owns the
 * environment-specific contract, not a PPO implementation. The caller owns
 * the game, observation/action buffers, and sampling RNG. */
#include "core.h"
#include <limits.h>

#define KAG_POLICY_VERSION 5
#define KAG_EXACT_MARKET_QUANTITIES 100
#define KAG_OBSERVATION_ENTITIES 3
#define KAG_GLOBAL_OFFSET 0
#define KAG_GLOBAL_FEATURES 128
#define KAG_PRODUCT_OFFSET 128
#define KAG_PRODUCT_COUNT 9
#define KAG_PRODUCT_FEATURES 56
#define KAG_PLOT_OFFSET 632
#define KAG_PLOT_COUNT 8
#define KAG_PLOT_FEATURES 24
#define KAG_WORKER_OFFSET 824
#define KAG_WORKER_COUNT 17
#define KAG_WORKER_FEATURES 32
#define KAG_TASK_OFFSET 1368
#define KAG_TASK_FEATURES 56
#define KAG_ENTITY_OBS_SIZE 1424
#define KAG_FUSION_WIDTH (64 + 9 * 32 + 8 * 32 + 17 * 16)
#define KAG_TASK_LOGITS (17 * 44)
#define KAG_MARKET_LOGITS (10 * (2 + 21 + KAG_EXACT_MARKET_QUANTITIES))
#define KAG_ALL_LOGITS (KAG_TASK_LOGITS + KAG_MARKET_LOGITS)
#define KAG_OBS_RESET_SOURCE_INDEX 31

#define KG_OBS_BOARD 10
#define KG_POLICY_DIRECT_HANDS 16
#define KG_POLICY_UNIT_HEADS 17
#define KG_POLICY_UNITS KG_POLICY_UNIT_HEADS
#define KG_POLICY_UNIT_COMMANDS 44
#define KG_POLICY_MARKET_SLOTS 10
#define KG_POLICY_MARKET_CONTINUE_ACTIONS 2
#define KG_POLICY_MARKET_COMMANDS 21
#define KG_POLICY_MARKET_QUANTITIES KAG_EXACT_MARKET_QUANTITIES
#define KG_POLICY_MARKET_QUANTITY_COMMANDS 19
#define KG_POLICY_MARKET_HEAD_OFFSET KG_POLICY_UNIT_HEADS
#define KG_POLICY_MARKET_MASK_OFFSET (KG_POLICY_UNIT_HEADS * KG_POLICY_UNIT_COMMANDS)
#define KG_POLICY_MARKET_SLOT_MASK_SIZE (2 + 21 + KG_POLICY_MARKET_QUANTITIES)
#define KG_POLICY_ACTION_MASK_SIZE KAG_ALL_LOGITS
#define KAG_ACTION_HEADS 47
#define KAG_ACTION_SIZES                                                                           \
    {44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 2, 21, 100, 2, 21, 100,   \
        2, 21, 100, 2, 21, 100, 2, 21, 100, 2, 21, 100, 2, 21, 100, 2, 21, 100, 2, 21, 100, 2, 21, \
        100}
#define KAG_MULTI_SLOTS 5
#define KAG_MULTI_FEED_HEAD 15
#define KAG_MULTI_HARVEST_HEAD 16
#define KAG_MULTI_INTENTS 29
#define KAG_MULTI_MAX_JOBS (320 + KAG_MULTI_SLOTS * KG_MAX_TILES)
#define KAG_MACRO_TARGET_BINS 5

enum {
    KAG_MULTI_STOP = 0,
    KAG_MULTI_PLANT = 1,
    KAG_MULTI_COOP = 6,
    KAG_MULTI_PASTURE = 7,
    KAG_MULTI_CLEAR = 8,
    KAG_MULTI_FERTILIZE = 9,
    KAG_MULTI_HARVEST = 10,
    KAG_MULTI_HARVEST_CROP = 11,
    KAG_MULTI_DELIVER_ITEM = 16,
    KAG_MULTI_DELIVER_ALL = 28,
};
enum {
    KG_M_SEED = 0,
    KG_M_PRODUCT = 5,
    KG_M_ANIMAL = 7,
    KG_M_SELL = 10,
    KG_M_HIRE = 19,
    KG_M_LAND = 20
};

typedef struct {
    int op, item, n;
} KGPolicyMarketSpec;
typedef struct {
    int16_t x, y, priority, op, arg, request;
} KagMultiJob;

/* These features existed independently of the reward weights. Preserve them
 * without importing the retired reward configuration or shaping machinery. */
typedef struct {
    int start_cash, start_step;
    float coverage_sum, idle_sum;
    int peak_plots, peak_crops, peak_animals;
} KagObservationState;

typedef struct {
    int valid, inventory;
    float quotes[32];
} KagQuoteCache;

typedef struct {
    int market_slots, max_hands, land_buy_min_days;
    int reset_source;
    int land_fill_step[KG_NUM_PLAYERS], land_fill_mask[KG_NUM_PLAYERS];
    int macro_intent[KG_NUM_PLAYERS], macro_quantity[KG_NUM_PLAYERS];
    int macro_target[KG_NUM_PLAYERS];
    KagObservationState history[KG_NUM_PLAYERS];
    KagQuoteCache quote_cache[KG_NUM_PRODUCTS];
} KagPolicy;

KG_HD int kag_popcount(unsigned value) {
#ifdef __CUDA_ARCH__
    return __popc(value);
#else
    return __builtin_popcount(value);
#endif
}

KG_HD int kag_discrete_index(float value, int size) {
    int index = (int)value;
    if (index < 0) {
        index = 0;
    }
    if (index >= size) {
        index = size - 1;
    }
    return index;
}

KG_HD int kag_abs(int value) {
    return value < 0 ? -value : value;
}

KG_HD int kag_macro_target_from_bin(int bin) {
    if (bin <= 0) {
        return 0;
    }
    if (bin >= KAG_MACRO_TARGET_BINS) {
        bin = KAG_MACRO_TARGET_BINS - 1;
    }
    return 1 << (bin - 1);
}

KG_HD KGPolicyMarketSpec kag_market_spec(int id) {
    if ((unsigned)id >= KG_POLICY_MARKET_COMMANDS) {
        id = 0;
    }
    if (id < KG_M_PRODUCT) {
        return (KGPolicyMarketSpec){KG_MARKET_BUY_SEED, id - KG_M_SEED, 1};
    }
    if (id < KG_M_ANIMAL) {
        return (KGPolicyMarketSpec){
            KG_MARKET_BUY_PRODUCT, id == KG_M_PRODUCT ? KG_ITEM_WHEAT : KG_ITEM_FERTILIZER, 1};
    }
    if (id < KG_M_SELL) {
        return (KGPolicyMarketSpec){KG_MARKET_BUY_ANIMAL, KG_ITEM_GOOSE + id - KG_M_ANIMAL, 1};
    }
    if (id < KG_M_HIRE) {
        return (KGPolicyMarketSpec){KG_MARKET_SELL, id - KG_M_SELL, 1};
    }
    if (id == KG_M_HIRE) {
        return (KGPolicyMarketSpec){KG_MARKET_HIRE, KG_ITEM_INVALID, 1};
    }
    return (KGPolicyMarketSpec){KG_MARKET_BUY_LAND, KG_ITEM_INVALID, 1};
}

KG_HD int kag_market_action_legal(
    const KGState* game, const KGPlayer* player, KGPolicyMarketSpec spec) {
    int price = 0;
    if (spec.op == KG_MARKET_BUY_SEED) {
        price = KG_CROP_DEFS[spec.item].seed_cost;
        return price > 0 && player->money >= price;
    }
    if (spec.op == KG_MARKET_BUY_PRODUCT) {
        int inventory = game->market.inventory[spec.item];
        price = kg_market_price(spec.item, inventory - 1);
        return price > 0 && player->money >= price
            && kg_shed_total(player) < game->config.shed_capacity;
    }
    if (spec.op == KG_MARKET_BUY_ANIMAL) {
        int animal = spec.item - KG_ITEM_GOOSE;
        if ((unsigned)animal >= KG_NUM_ANIMALS) {
            return 0;
        }
        price = KG_ANIMAL_DEFS[spec.item - KG_ITEM_GOOSE].cost;
        return price > 0 && player->money >= price
            && kg_shed_total(player) < game->config.shed_capacity;
    }
    if (spec.op == KG_MARKET_SELL) {
        if (spec.item < 0 || spec.item >= KG_NUM_PRODUCTS) {
            return 0;
        }
        if (player->shed[spec.item] > 0) {
            return 1;
        }
        /* Conservative pre-sampling mask: preserve potential same-turn
         * deposits. The prefix-aware sampler checks the actual chosen jobs. */
        for (int u = 0; u < player->unit_count; u++) {
            KGPosition pos = {player->units[u].x, player->units[u].y};
            if (player->units[u].inventory[spec.item] > 0
                && kg_is_shed_adjacent(&pos, game->config.board_size)) {
                return 1;
            }
        }
        return 0;
    }
    if (spec.op == KG_MARKET_HIRE) {
        return player->unit_count < KG_MAX_UNITS && game->hour != game->config.turns_per_day - 1
            && game->step < game->config.episode_steps - 2
            && player->money >= kg_hire_cost(player->hires_today, game->config.farm_hand_cost_mult);
    }
    if (spec.op == KG_MARKET_BUY_LAND) {
        int extra = kag_popcount((unsigned int)player->unlocked_mask) - 1;
        int price = extra == 0 ? 1000 : extra == 1 ? 2000 : 4000;
        return extra >= 0 && extra < 3 && player->money >= price;
    }
    return 0;
}

KG_HD int kag_bot_route(const KGPlayer* farm, const KGUnitState* unit, int tx, int ty) {
    int quadrant = kg_quadrant(unit->x, unit->y, KG_OBS_BOARD);
    if (!(farm->unlocked_mask & quadrant)) {
        if (unit->x >= 5 && unit->y >= 5) {
            if (farm->unlocked_mask & 2) {
                return KG_OP_NORTH;
            }
            if (farm->unlocked_mask & 4) {
                return KG_OP_WEST;
            }
            /* Hands can spawn at the shed's locked SE access cell while only
             * NW is owned. Core movement is bounds-only, so take a two-step
             * escape through locked NE and then west into the owned farm. */
            return KG_OP_NORTH;
        }
        if (unit->x >= 5) {
            return KG_OP_WEST;
        }
        if (unit->y >= 5) {
            return KG_OP_NORTH;
        }
    }
    if (unit->x != tx) {
        return unit->x < tx ? KG_OP_EAST : KG_OP_WEST;
    }
    if (unit->y != ty) {
        return unit->y < ty ? KG_OP_SOUTH : KG_OP_NORTH;
    }
    return KG_OP_PASS;
}

KG_HD int kag_market_quantity_spec(int id) {
    if ((unsigned)id >= KG_POLICY_MARKET_QUANTITIES) {
        id = 0;
    }
    return id + 1;
}

KG_HD int kag_market_quantity_id(int n) {
    if (n < 0) {
        return KG_POLICY_MARKET_QUANTITIES - 1;
    }
    int best = 0;
    for (int id = 1; id < KG_POLICY_MARKET_QUANTITIES; id++) {
        if (kag_market_quantity_spec(id) > n) {
            break;
        }
        best = id;
    }
    return best;
}

KG_HD int kag_ready_units(const KGState* game, const KGTile* t) {
    if (t->kind == KG_TILE_PLANT && (unsigned)t->crop < KG_NUM_CROPS) {
        return game->day - t->planted_day >= KG_CROP_DEFS[t->crop].first_yield_day ? t->yield_units
                                                                                   : 0;
    }
    return kg_is_animal_tile(t) ? t->yield_units : 0;
}

KG_HD int kag_quality_tile(const KGState* game, const KGTile* t) {
    int healthy = t->kind == KG_TILE_PLANT
        ? t->watered_today || t->consecutive_unwatered == 0
        : kg_is_animal_tile(t) && (t->fed_today || t->consecutive_unfed == 0);
    if (!healthy) {
        return 0;
    }
    /* kg_step's final action is episode_steps-2; the resulting state is not
     * another opportunity to harvest newly matured produce. */
    int last_day = (game->config.episode_steps - 2) / game->config.turns_per_day;
    if (t->kind == KG_TILE_PLANT && (unsigned)t->crop < KG_NUM_CROPS) {
        const KGCropDef* d = &KG_CROP_DEFS[t->crop];
        int first = t->planted_day + d->first_yield_day;
        if (game->day >= first && t->yield_units > 0) {
            return 1;
        }
        if (first > last_day) {
            return 0;
        }
        if (!d->ongoing) {
            return game->day <= t->planted_day + d->max_yield_day
                && (t->yield_units > 0 || game->day <= last_day);
        }
        int next = first;
        if (next <= game->day) {
            next += ((game->day - first) / d->interval + 1) * d->interval;
        }
        return next <= last_day && (next - first) / d->interval < d->max_yield;
    }
    if (kg_is_animal_tile(t)) {
        if (t->yield_units > 0) {
            return 1;
        }
        const KGAnimalDef* d = &KG_ANIMAL_DEFS[t->animal];
        int next = t->placed_day + d->first_yield_day;
        if (next <= game->day) {
            next += ((game->day - next) / d->interval + 1) * d->interval;
        }
        return next <= last_day;
    }
    return 0;
}

KG_HD void kag_quality_components(
    const KGState* game, int pid, float* coverage, float* idle, int* crops, int* animals) {
    const KGPlayer* p = &game->players[pid];
    int capacity[4] = {0}, used[4] = {0};
    *crops = *animals = 0;
    for (int y = 0; y < game->config.board_size; y++) {
        for (int x = 0; x < game->config.board_size; x++) {
            int bit = kg_quadrant(x, y, game->config.board_size);
            int q = bit == 1 ? 0 : bit == 2 ? 1 : bit == 4 ? 2 : 3;
            capacity[q]++;
            const KGTile* t = &p->tiles[kg_tile_index(x, y)];
            if (!(p->unlocked_mask & bit) || !kag_quality_tile(game, t)) {
                continue;
            }
            used[q]++;
            *crops += t->kind == KG_TILE_PLANT;
            *animals += kg_is_animal_tile(t);
        }
    }
    float prefix = 1.0f;
    *coverage = *idle = 0.0f;
    for (int q = 0; q < 4; q++) {
        float u = capacity[q] ? (float)used[q] / capacity[q] : 0.0f;
        if (u < prefix) {
            prefix = u;
        }
        *coverage += prefix / 4.0f;
        if (q && (p->unlocked_mask & (1 << q))) {
            *idle += (1.0f - u) / 3.0f;
        }
    }
}

KG_HD void kag_update_land_buy_delay(KagPolicy* policy, const KGState* game, int player_id) {
    if (policy->land_buy_min_days == 0) {
        return;
    }
    const KGPlayer* player = &game->players[player_id];
    int owned = player->unlocked_mask;
    if (policy->land_fill_mask[player_id] != owned) {
        policy->land_fill_mask[player_id] = owned;
        policy->land_fill_step[player_id] = -1;
    }
    if (owned == 15 || policy->land_fill_step[player_id] >= 0) {
        return;
    }
    int quadrant = 1;
    for (int bit = 2; bit <= 8; bit <<= 1) {
        if (owned & bit) {
            quadrant = bit;
        }
    }
    int count = 0;
    for (int y = 0; y < game->config.board_size; y++) {
        for (int x = 0; x < game->config.board_size; x++) {
            if (kg_quadrant(x, y, game->config.board_size) != quadrant) {
                continue;
            }
            const KGTile* tile = &player->tiles[kg_tile_index(x, y)];
            if (!((tile->kind == KG_TILE_PLANT && (unsigned)tile->crop < KG_NUM_CROPS)
                    || kg_is_animal_tile(tile))) {
                return;
            }
            count++;
        }
    }
    if (count > 0) {
        policy->land_fill_step[player_id] = game->step;
    }
}

KG_HD int kag_land_buy_delay_ready(const KagPolicy* policy, const KGState* game, int player_id) {
    if (policy->land_buy_min_days == 0) {
        return 1;
    }
    if (policy->land_fill_mask[player_id] != game->players[player_id].unlocked_mask
        || policy->land_fill_step[player_id] < 0) {
        return 0;
    }
    return (int64_t)game->step - policy->land_fill_step[player_id]
        >= (int64_t)policy->land_buy_min_days * game->config.turns_per_day;
}

KG_HD void kag_policy_reset(KagPolicy* policy, const KGState* game, int reset_source) {
    assert(policy->market_slots > 0 && policy->market_slots <= KG_POLICY_MARKET_SLOTS);
    assert(policy->max_hands > 0 && policy->max_hands <= KG_POLICY_DIRECT_HANDS);
    assert(policy->land_buy_min_days >= 0);
    assert(game->config.board_size == KG_OBS_BOARD);
    policy->reset_source = reset_source;
    for (int p = 0; p < KG_NUM_PLAYERS; p++) {
        KagObservationState* s = &policy->history[p];
        *s = (KagObservationState){0};
        s->start_cash = game->players[p].money;
        s->start_step = game->step;
        float coverage, idle;
        kag_quality_components(game, p, &coverage, &idle, &s->peak_crops, &s->peak_animals);
        s->peak_plots = kag_popcount(game->players[p].unlocked_mask);
        policy->land_fill_mask[p] = 0;
        policy->land_fill_step[p] = -1;
        policy->macro_intent[p] = policy->macro_quantity[p] = policy->macro_target[p] = 0;
    }
}

/* Call once after each game step, including the terminal transition. */
KG_HD void kag_policy_step(KagPolicy* policy, const KGState* game) {
    for (int p = 0; p < KG_NUM_PLAYERS; p++) {
        KagObservationState* s = &policy->history[p];
        float coverage, idle;
        int crops, animals;
        kag_quality_components(game, p, &coverage, &idle, &crops, &animals);
        s->coverage_sum += coverage;
        s->idle_sum += idle;
        int plots = kag_popcount(game->players[p].unlocked_mask);
        if (plots > s->peak_plots) {
            s->peak_plots = plots;
        }
        if (crops > s->peak_crops) {
            s->peak_crops = crops;
        }
        if (animals > s->peak_animals) {
            s->peak_animals = animals;
        }
    }
}

KG_HD int kag_multi_cargo(const KGUnitState* u, int intent) {
    if (intent >= KAG_MULTI_DELIVER_ITEM && intent < KAG_MULTI_DELIVER_ALL) {
        return u->inventory[intent - KAG_MULTI_DELIVER_ITEM];
    }
    int total = 0;
    for (int i = 0; i < KG_NUM_ITEMS; i++) {
        total += u->inventory[i];
    }
    return total;
}

KG_HD int kag_multi_tile_matches(const KGState* g, int p, int intent, int tile, int quadrant) {
    const KGPlayer* f = &g->players[p];
    int x = tile % KG_MAX_BOARD_SIZE, y = tile / KG_MAX_BOARD_SIZE;
    if (x >= g->config.board_size || y >= g->config.board_size) {
        return 0;
    }
    int bit = kg_quadrant(x, y, g->config.board_size);
    if (!(f->unlocked_mask & bit) || (quadrant && bit != quadrant)) {
        return 0;
    }
    const KGTile* t = &f->tiles[tile];
    if ((intent >= KAG_MULTI_PLANT && intent < KAG_MULTI_COOP) || intent == KAG_MULTI_COOP
        || intent == KAG_MULTI_PASTURE) {
        return t->kind == KG_TILE_EMPTY;
    }
    if (intent == KAG_MULTI_CLEAR) {
        return t->kind == KG_TILE_PLANT
            || (t->kind == KG_TILE_WEED
                || ((t->kind == KG_TILE_COOP || t->kind == KG_TILE_PASTURE)
                    && !kg_is_animal_tile(t)));
    }
    if (intent == KAG_MULTI_FERTILIZE) {
        return t->kind == KG_TILE_PLANT && t->fertilized_until_day < g->day + 2;
    }
    if (intent == KAG_MULTI_HARVEST) {
        return t->kind == KG_TILE_PLANT && kag_ready_units(g, t) > 0;
    }
    if (intent >= KAG_MULTI_HARVEST_CROP && intent < KAG_MULTI_DELIVER_ITEM) {
        return t->kind == KG_TILE_PLANT && t->crop == intent - KAG_MULTI_HARVEST_CROP
            && kag_ready_units(g, t) > 0;
    }
    return 0;
}

KG_HD int kag_multi_capacity(const KGState* g, int p, int intent, int quadrant) {
    if (intent <= 0 || intent >= KAG_MULTI_INTENTS) {
        return 0;
    }
    int count = 0;
    if (intent >= KAG_MULTI_DELIVER_ITEM) {
        const KGPlayer* f = &g->players[p];
        if (kg_shed_total(f) >= g->config.shed_capacity) {
            return 0;
        }
        for (int u = 0; u < f->unit_count; u++) {
            count += kag_multi_cargo(&f->units[u], intent) > 0
                && (!quadrant
                    || kg_quadrant(f->units[u].x, f->units[u].y, g->config.board_size) == quadrant);
        }
        return count;
    }
    for (int t = 0; t < KG_MAX_TILES; t++) {
        count += kag_multi_tile_matches(g, p, intent, t, quadrant);
    }
    if (intent >= KAG_MULTI_PLANT && intent < KAG_MULTI_COOP) {
        int seeds = g->players[p].seeds[intent - KAG_MULTI_PLANT];
        if (count > seeds) {
            count = seeds;
        }
    }
    if (intent == KAG_MULTI_FERTILIZE) {
        const KGPlayer* player = &g->players[p];
        int stock = player->shed[KG_ITEM_FERTILIZER];
        for (int u = 0; u < player->unit_count; u++) {
            stock += player->units[u].inventory[KG_ITEM_FERTILIZER];
        }
        if (count > stock) {
            count = stock;
        }
    }
    return count;
}

KG_HD void kag_multi_add(
    KagMultiJob* jobs, int* count, int x, int y, int priority, int op, int arg, int request) {
    assert(*count < KAG_MULTI_MAX_JOBS);
    jobs[(*count)++] = (KagMultiJob){
        (int16_t)x, (int16_t)y, (int16_t)priority, (int16_t)op, (int16_t)arg, (int16_t)request};
}

KG_HD void kag_multi_work(KGAction* a, const float* actions, const KGState* g, int p) {
    const KGPlayer* f = &g->players[p];
    memset(a, 0, sizeof(*a));
    a->farmer = (KGUnitAction){KG_OP_PASS, -1, 1};
    a->hand_count = f->hand_count;
    for (int u = 0; u < a->hand_count; u++) {
        a->hands[u] = a->farmer;
    }
    int intent[KAG_MULTI_SLOTS] = {0}, left[KAG_MULTI_SLOTS] = {0};
    int region[KAG_MULTI_SLOTS] = {0}, fertilizer = 0;
    for (int s = 0; s < KAG_MULTI_SLOTS; s++) {
        intent[s] = kag_discrete_index(actions[3 * s], KAG_MULTI_INTENTS);
        if (!intent[s]) {
            break;
        }
        left[s] = 1 + kag_discrete_index(actions[3 * s + 1], 44);
        region[s] = kag_macro_target_from_bin(kag_discrete_index(actions[3 * s + 2], 5));
        if (intent[s] == KAG_MULTI_FERTILIZE) {
            fertilizer += left[s];
        }
    }
    int seeds[KG_NUM_CROPS];
    memcpy(seeds, f->seeds, sizeof(seeds));
    int pickup[KG_NUM_ITEMS] = {0}, carried[KG_NUM_ITEMS] = {0};
    int housing[KG_NUM_ANIMALS] = {0};
    for (int u = 0; u < f->unit_count; u++) {
        for (int i = 0; i < KG_NUM_ITEMS; i++) {
            carried[i] += f->units[u].inventory[i];
        }
    }
    int unfed = 0;
    for (int t = 0; t < KG_MAX_TILES; t++) {
        unfed += kg_is_animal_tile(&f->tiles[t]) && !f->tiles[t].fed_today;
    }
    pickup[KG_ITEM_WHEAT] = unfed - carried[KG_ITEM_WHEAT];
    if (actions[KAG_MULTI_FEED_HEAD] == 2) {
        pickup[KG_ITEM_WHEAT] = 0;
    }
    pickup[KG_ITEM_FERTILIZER] = fertilizer - carried[KG_ITEM_FERTILIZER];
    for (int s = 0; s < KG_NUM_ANIMALS; s++) {
        int room = 0;
        for (int t = 0; t < KG_MAX_TILES; t++) {
            room += f->tiles[t].kind == KG_ANIMAL_DEFS[s].structure
                && f->tiles[t].animal == KG_ANIMAL_INVALID;
        }
        for (int other = 0; other < KG_NUM_ANIMALS; other++) {
            if (KG_ANIMAL_DEFS[other].structure == KG_ANIMAL_DEFS[s].structure) {
                room -= carried[KG_ITEM_GOOSE + other];
            }
        }
        pickup[KG_ITEM_GOOSE + s] = room;
        housing[s] = room;
    }
    for (int i = 0; i < KG_NUM_ITEMS; i++) {
        if (pickup[i] < 0) {
            pickup[i] = 0;
        }
        if (pickup[i] > f->shed[i]) {
            pickup[i] = f->shed[i];
        }
    }
    KagMultiJob jobs[KAG_MULTI_MAX_JOBS];
    int count = 0;
    for (int t = 0; t < KG_MAX_TILES; t++) {
        const KGTile* tile = &f->tiles[t];
        int x = t % KG_MAX_BOARD_SIZE, y = t / KG_MAX_BOARD_SIZE;
        if (kg_is_animal_tile(tile)) {
            if (!tile->fed_today) {
                kag_multi_add(jobs, &count, x, y, 0, KG_OP_FEED, -1, -1);
            }
            if (tile->yield_units > 0) {
                kag_multi_add(jobs, &count, x, y, 2, KG_OP_HARVEST, -1, -1);
            }
            if (!tile->cared_today) {
                kag_multi_add(jobs, &count, x, y, 3, KG_OP_CARE, -1, -1);
            }
            if (tile->fertilizer_available) {
                kag_multi_add(jobs, &count, x, y, 4, KG_OP_COLLECT_FERTILIZER, -1, -1);
            }
        } else if (tile->kind == KG_TILE_PLANT) {
            const KGCropDef* d = &KG_CROP_DEFS[tile->crop];
            int age = g->day - tile->planted_day;
            int bonus_window =
                !d->ongoing && age >= (d->max_yield_day + 1) / 2 && age <= d->max_yield_day;
            if (!tile->watered_today
                && (tile->consecutive_unwatered > 0 || bonus_window || d->ongoing)) {
                kag_multi_add(
                    jobs, &count, x, y, tile->consecutive_unwatered ? 0 : 3, KG_OP_WATER, -1, -1);
            } else if (actions[KAG_MULTI_HARVEST_HEAD] != 1 && tile->yield_units > 0
                && g->day - tile->planted_day >= d->first_yield_day
                && (d->ongoing || g->day - tile->planted_day >= d->max_yield_day
                    || g->config.episode_steps - g->step <= g->config.turns_per_day)) {
                kag_multi_add(jobs, &count, x, y, 2, KG_OP_HARVEST, -1, -1);
            }
        } else {
            for (int s = 0; s < KG_NUM_ANIMALS; s++) {
                if (carried[KG_ITEM_GOOSE + s] > 0 && tile->kind == KG_ANIMAL_DEFS[s].structure
                    && tile->animal == KG_ANIMAL_INVALID) {
                    kag_multi_add(jobs, &count, x, y, 1, KG_OP_PLACE, KG_ITEM_GOOSE + s, -1);
                }
            }
        }
        for (int s = 0; s < KAG_MULTI_SLOTS; s++) {
            if (!intent[s] || !kag_multi_tile_matches(g, p, intent[s], t, region[s])) {
                continue;
            }
            int op = KG_OP_PASS, arg = -1;
            if (intent[s] < KAG_MULTI_COOP) {
                op = KG_OP_PLANT;
                arg = intent[s] - 1;
            } else if (intent[s] == KAG_MULTI_COOP) {
                op = KG_OP_BUILD_COOP;
            } else if (intent[s] == KAG_MULTI_PASTURE) {
                op = KG_OP_BUILD_PASTURE;
            } else if (intent[s] == KAG_MULTI_CLEAR) {
                op = KG_OP_DIG;
            } else if (intent[s] == KAG_MULTI_FERTILIZE) {
                op = KG_OP_FERTILIZE;
                arg = KG_ITEM_FERTILIZER;
            } else if (intent[s] == KAG_MULTI_HARVEST
                || (intent[s] >= KAG_MULTI_HARVEST_CROP && intent[s] < KAG_MULTI_DELIVER_ITEM)) {
                op = KG_OP_HARVEST;
            }
            /* Explicit clear/early-harvest beats upkeep at that target, but
             * consumes its batch budget; other plants keep automatic chores. */
            int priority = op == KG_OP_FERTILIZE           ? -2
                : (op == KG_OP_HARVEST || op == KG_OP_DIG) ? -1
                                                           : 4;
            kag_multi_add(jobs, &count, x, y, priority, op, arg, s);
        }
    }
    KGPosition access[4];
    kg_shed_access_count(g->config.board_size, access);
    for (int s = 0; s < KAG_MULTI_SLOTS; s++) {
        if (intent[s] >= KAG_MULTI_DELIVER_ITEM) {
            for (int k = 0; k < 4; k++) {
                kag_multi_add(jobs, &count, access[k].x, access[k].y, 0, KG_OP_DROP,
                    intent[s] - KAG_MULTI_DELIVER_ITEM, s);
            }
        }
    }
    for (int i = 0; i < KG_NUM_ITEMS; i++) {
        if (pickup[i] > 0) {
            for (int s = 0; s < 4; s++) {
                kag_multi_add(jobs, &count, access[s].x, access[s].y, i == KG_ITEM_WHEAT ? 0 : 1,
                    KG_OP_PICKUP, i, -1);
            }
        }
    }
    unsigned char used[KG_MAX_HANDS + 1] = {0}, claimed[KG_MAX_TILES] = {0};
    int pickup_claimed[KG_NUM_ITEMS] = {0};
    int shed_room = g->config.shed_capacity - kg_shed_total(f);
    for (int assigned = 0; assigned < f->unit_count; assigned++) {
        int best_u = -1, best_j = -1, best_cost = INT_MAX;
        for (int u = 0; u < f->unit_count; u++) {
            if (used[u]) {
                continue;
            }
            const KGUnitState* unit = &f->units[u];
            for (int j = 0; j < count; j++) {
                const KagMultiJob* job = &jobs[j];
                if (job->request >= 0 && left[job->request] <= 0) {
                    continue;
                }
                int delivery = job->op == KG_OP_DROP;
                if (!delivery && job->op != KG_OP_PICKUP
                    && claimed[kg_tile_index(job->x, job->y)]) {
                    continue;
                }
                if (delivery) {
                    if (shed_room <= 0 || !kag_multi_cargo(unit, intent[job->request])) {
                        continue;
                    }
                    if (region[job->request]
                        && kg_quadrant(unit->x, unit->y, g->config.board_size)
                            != region[job->request]) {
                        continue;
                    }
                    /* PLACE livestock on empty compatible housing would not
                     * deliver it. Route to another shed access in that case. */
                    int item = job->arg;
                    if (item >= KG_NUM_ITEMS
                        && kag_multi_cargo(unit, KAG_MULTI_DELIVER_ALL) > shed_room) {
                        item = unit->inventory_order_count ? unit->inventory_order[0] : -1;
                    }
                    if (item >= KG_ITEM_GOOSE && item < KG_NUM_ITEMS) {
                        const KGTile* t = &f->tiles[kg_tile_index(job->x, job->y)];
                        if (t->kind == KG_ANIMAL_DEFS[item - KG_ITEM_GOOSE].structure
                            && t->animal == KG_ANIMAL_INVALID) {
                            continue;
                        }
                    }
                }
                if (job->op == KG_OP_PLANT && seeds[job->arg] <= 0) {
                    continue;
                }
                if (job->op == KG_OP_FEED && !unit->inventory[KG_ITEM_WHEAT]) {
                    continue;
                }
                if ((job->op == KG_OP_FERTILIZE || job->op == KG_OP_PLACE)
                    && !unit->inventory[job->arg]) {
                    continue;
                }
                if (job->op == KG_OP_PICKUP) {
                    if (pickup_claimed[job->arg] || unit->inventory[job->arg]) {
                        continue;
                    }
                    if (job->arg >= KG_ITEM_GOOSE && housing[job->arg - KG_ITEM_GOOSE] <= 0) {
                        continue;
                    }
                    int livestock = 0;
                    for (int s = 0; s < KG_NUM_ANIMALS; s++) {
                        livestock += unit->inventory[KG_ITEM_GOOSE + s];
                    }
                    if (livestock) {
                        continue;
                    }
                    if (job->arg >= KG_ITEM_GOOSE && unfed && unit->inventory[KG_ITEM_WHEAT]) {
                        continue;
                    }
                }
                int dist = kag_abs((int)unit->x - job->x) + kag_abs((int)unit->y - job->y);
                /* A selected strategic batch owns its worker budget. Complete
                 * feasible local requests before routing requests, then use
                 * the remaining workers for automatic chores. In particular,
                 * WATER must not steal a local FERTILIZE worker: fertilizing
                 * before watering can change that day's crop yield. */
                int cost =
                    (job->request >= 0 ? 0 : 4096) + (dist ? 1024 : 0) + job->priority * 32 + dist;
                if (cost < best_cost) {
                    best_cost = cost;
                    best_u = u;
                    best_j = j;
                }
            }
        }
        if (best_u < 0) {
            break;
        }
        const KagMultiJob* job = &jobs[best_j];
        const KGUnitState* unit = &f->units[best_u];
        KGUnitAction* cmd = best_u ? &a->hands[best_u - 1] : &a->farmer;
        int local = unit->x == job->x && unit->y == job->y;
        used[best_u] = 1;
        if (job->op == KG_OP_DROP) {
            int total = kag_multi_cargo(unit, intent[job->request]);
            int item = job->arg, n = total;
            int drop_all = item >= KG_NUM_ITEMS && total <= shed_room;
            if (!drop_all) {
                if (item >= KG_NUM_ITEMS) {
                    item = unit->inventory_order[0];
                }
                n = unit->inventory[item];
                if (n > shed_room) {
                    n = shed_room;
                }
            }
            *cmd = local
                ? (KGUnitAction){drop_all ? KG_OP_DROP : KG_OP_PLACE, drop_all ? -1 : item, n}
                : (KGUnitAction){kag_bot_route(f, unit, job->x, job->y), -1, 1};
            shed_room -= n; /* reserve room even for a routed delivery */
            left[job->request]--;
        } else if (job->op == KG_OP_PICKUP) {
            pickup_claimed[job->arg] = 1;
            if (job->arg >= KG_ITEM_GOOSE) {
                int animal = job->arg - KG_ITEM_GOOSE;
                for (int s = 0; s < KG_NUM_ANIMALS; s++) {
                    if (KG_ANIMAL_DEFS[s].structure == KG_ANIMAL_DEFS[animal].structure) {
                        housing[s]--;
                    }
                }
            }
            int n = job->arg >= KG_ITEM_GOOSE ? 1 : pickup[job->arg];
            *cmd = local ? (KGUnitAction){KG_OP_PICKUP, job->arg, n}
                         : (KGUnitAction){kag_bot_route(f, unit, job->x, job->y), -1, 1};
        } else {
            claimed[kg_tile_index(job->x, job->y)] = 1;
            *cmd = local ? (KGUnitAction){job->op, job->arg, 1}
                         : (KGUnitAction){kag_bot_route(f, unit, job->x, job->y), -1, 1};
            if (job->request >= 0) {
                left[job->request]--;
            }
            if (job->op == KG_OP_PLANT) {
                seeds[job->arg]--;
            }
        }
    }
}

/* A sampled prefix owns reservations. It consults only this player's
 * observed state and chosen actions, never hidden opponent orders. */
typedef struct {
    const KGState* game;
    const KagPolicy* policy;
    int player;
    float choices[KAG_ACTION_HEADS];
    int multi_capacity[KAG_MULTI_INTENTS], multi_capacity_ready;
    int cash, shed[KG_NUM_ITEMS], inventory[KG_NUM_PRODUCTS];
    int hands, hires, plots, land_used, land_ready, prepared, wheat_out_of_shed;
} KagActionMaskState;

KG_HD void kag_action_mask_begin(
    KagActionMaskState* s, const KGState* game, const KagPolicy* policy, int player) {
    memset(s, 0, sizeof(*s));
    s->game = game;
    s->policy = policy;
    s->player = player;
}

KG_HD int kag_mask_shed_total(const KagActionMaskState* s) {
    int n = 0;
    for (int i = 0; i < KG_NUM_ITEMS; i++) {
        n += s->shed[i];
    }
    return n;
}

KG_HD void kag_mask_prepare_market_from_work(KagActionMaskState* s, const KGAction* work) {
    const KGState* g = s->game;
    const KGPlayer* p = &g->players[s->player];
    s->cash = p->money;
    s->hands = p->hand_count;
    s->hires = p->hires_today;
    s->plots = kag_popcount(p->unlocked_mask);
    s->land_ready = kag_land_buy_delay_ready(s->policy, g, s->player);
    memcpy(s->shed, p->shed, sizeof(s->shed));
    memcpy(s->inventory, g->market.inventory, sizeof(s->inventory));
    int count = work->hand_count + 1;
    if (count > p->unit_count) {
        count = p->unit_count;
    }
    for (int u = 0; u < count; u++) {
        const KGUnitState* unit = &p->units[u];
        const KGUnitAction* a = u ? &work->hands[u - 1] : &work->farmer;
        KGPosition pos = {unit->x, unit->y};
        if (!kg_is_shed_adjacent(&pos, g->config.board_size)) {
            continue;
        }
        int item = a->arg;
        if (a->op == KG_OP_PICKUP && (unsigned)item < KG_NUM_ITEMS) {
            int n = a->n > 0 ? a->n : 1;
            if (n > s->shed[item]) {
                n = s->shed[item];
            }
            s->shed[item] -= n;
            if (item == KG_ITEM_WHEAT) {
                s->wheat_out_of_shed += n;
            }
        } else if (a->op == KG_OP_DROP) {
            for (int i = 0; i < unit->inventory_order_count; i++) {
                item = unit->inventory_order[i];
                int n = unit->inventory[item];
                int room = g->config.shed_capacity - kag_mask_shed_total(s);
                if (n > room) {
                    n = room;
                }
                if (n > 0) {
                    s->shed[item] += n;
                    if (item == KG_ITEM_WHEAT) {
                        s->wheat_out_of_shed -= n;
                    }
                }
            }
        } else if (a->op == KG_OP_PLACE && (unsigned)item < KG_NUM_ITEMS) {
            const KGTile* tile = &p->tiles[kg_tile_index(unit->x, unit->y)];
            if (item >= KG_ITEM_GOOSE
                && tile->kind == KG_ANIMAL_DEFS[item - KG_ITEM_GOOSE].structure
                && tile->animal == KG_ANIMAL_INVALID) {
                continue; /* places livestock */
            }
            int n = a->n > 0 ? a->n : 1;
            int room = g->config.shed_capacity - kag_mask_shed_total(s);
            if (n > room) {
                n = room;
            }
            if (n > unit->inventory[item]) {
                n = unit->inventory[item];
            }
            if (n > 0) {
                s->shed[item] += n;
                if (item == KG_ITEM_WHEAT) {
                    s->wheat_out_of_shed -= n;
                }
            }
        }
    }
    s->prepared = 1;
}

KG_HD int kag_mask_market_capacity(const KagActionMaskState* s, int command) {
    const KGState* g = s->game;
    KGPolicyMarketSpec spec = kag_market_spec(command);
    if (spec.op == KG_MARKET_SELL) {
        return s->shed[spec.item];
    }
    int remaining = g->config.episode_steps - 2 - g->step;
    if (spec.op == KG_MARKET_HIRE) {
        if (remaining < 1 || g->hour == g->config.turns_per_day - 1
            || s->hands >= s->policy->max_hands) {
            return 0;
        }
        return s->cash >= kg_hire_cost(s->hires, g->config.farm_hand_cost_mult);
    }
    if (spec.op == KG_MARKET_BUY_LAND) {
        if (remaining < 1 || s->plots >= 4 || !s->land_ready
            || (s->land_used && s->policy->land_buy_min_days > 0)) {
            return 0;
        }
        return s->cash >= (1000 << (s->plots - 1));
    }
    if (spec.op == KG_MARKET_BUY_SEED) {
        if (remaining < 1) {
            return 0;
        }
        return s->cash / KG_CROP_DEFS[spec.item].seed_cost;
    }
    int room = g->config.shed_capacity - kag_mask_shed_total(s);
    if (room <= 0) {
        return 0;
    }
    if (spec.op == KG_MARKET_BUY_ANIMAL) {
        if (remaining < 1) {
            return 0;
        }
        int n = s->cash / KG_ANIMAL_DEFS[spec.item - KG_ITEM_GOOSE].cost;
        return n < room ? n : room;
    }
    if (spec.op == KG_MARKET_BUY_PRODUCT) {
        int cost = 0, n = 0;
        while (n < room && n < KG_POLICY_MARKET_QUANTITIES) {
            int price = kg_market_price(spec.item, s->inventory[spec.item] - n - 1);
            if (price > s->cash - cost) {
                break;
            }
            cost += price;
            n++;
        }
        return n;
    }
    return 0;
}

KG_HD void kag_mask_market_commit(KagActionMaskState* s, int command, int n) {
    KGPolicyMarketSpec spec = kag_market_spec(command);
    const KGState* g = s->game;
    if (spec.op == KG_MARKET_HIRE) {
        s->cash -= kg_hire_cost(s->hires++, g->config.farm_hand_cost_mult);
        s->hands++;
        return;
    }
    if (spec.op == KG_MARKET_BUY_LAND) {
        s->cash -= 1000 << (s->plots - 1);
        s->plots++;
        s->land_used++;
        return;
    }
    int cap = kag_mask_market_capacity(s, command);
    if (n > cap) {
        n = cap;
    }
    for (int j = 0; j < n; j++) {
        if (spec.op == KG_MARKET_SELL) {
            int price = kg_market_price(spec.item, s->inventory[spec.item]);
            s->shed[spec.item]--;
            s->cash += price;
            if (price > 1) {
                s->inventory[spec.item]++;
            }
        } else if (spec.op == KG_MARKET_BUY_PRODUCT) {
            s->cash -= kg_market_price(spec.item, s->inventory[spec.item] - 1);
            s->inventory[spec.item]--;
            s->shed[spec.item]++;
        } else if (spec.op == KG_MARKET_BUY_SEED) {
            s->cash -= KG_CROP_DEFS[spec.item].seed_cost;
        } else if (spec.op == KG_MARKET_BUY_ANIMAL) {
            s->cash -= KG_ANIMAL_DEFS[spec.item - KG_ITEM_GOOSE].cost;
            s->shed[spec.item]++;
        }
    }
}

KG_HD unsigned char* kag_market_slot_mask(unsigned char* mask, int slot) {
    return mask + KG_POLICY_MARKET_MASK_OFFSET + slot * KG_POLICY_MARKET_SLOT_MASK_SIZE;
}

KG_HD void kag_write_mask(KagPolicy* policy, const KGState* g, int p, unsigned char* mask) {
    kag_update_land_buy_delay(policy, g, p);
    memset(mask, 0, KG_POLICY_ACTION_MASK_SIZE);
    for (int h = 0; h < KG_POLICY_UNIT_HEADS; h++) {
        mask[h * 44] = 1;
    }
    // All slots start from the same state; reservations belong to the later
    // prefix mask, not this shared base-capacity calculation.
    for (int intent = 1; intent < KAG_MULTI_INTENTS; intent++) {
        unsigned char available = kag_multi_capacity(g, p, intent, 0) > 0;
        for (int s = 0; s < KAG_MULTI_SLOTS; s++) {
            mask[3 * s * 44 + intent] = available;
        }
    }
    for (int s = 0; s < KAG_MULTI_SLOTS; s++) {
        unsigned char* out = mask + 3 * s * 44;
        for (int q = 0; q < 44; q++) {
            out[44 + q] = 1;
        }
        for (int q = 1; q < 5; q++) {
            out[88 + q] = (g->players[p].unlocked_mask & kag_macro_target_from_bin(q)) != 0;
        }
    }
    /* 0 automatic procurement; 1 explicit purchases; 2 also reserve shed
     * wheat from automatic pickup (e.g. for a policy-requested sale). */
    mask[KAG_MULTI_FEED_HEAD * 44 + 1] = mask[KAG_MULTI_FEED_HEAD * 44 + 2] = 1;
    mask[KAG_MULTI_HARVEST_HEAD * 44 + 1] =
        1; /* 0 ripe auto-harvest, 1 explicit crop harvest only */
    const KGPlayer* player = &g->players[p];
    unsigned char commands[KG_POLICY_MARKET_COMMANDS];
    int any = 0;
    for (int id = 0; id < KG_POLICY_MARKET_COMMANDS; id++) {
        commands[id] = kag_market_action_legal(g, player, kag_market_spec(id));
        if (id == KG_M_HIRE && player->hand_count >= policy->max_hands) {
            commands[id] = 0;
        }
        if (id == KG_M_LAND && !kag_land_buy_delay_ready(policy, g, p)) {
            commands[id] = 0;
        }
        any |= commands[id];
    }
    for (int slot = 0; slot < KG_POLICY_MARKET_SLOTS; slot++) {
        unsigned char* out = kag_market_slot_mask(mask, slot);
        out[0] = 1;
        if (slot >= policy->market_slots) {
            continue;
        }
        out[1] = any;
        memcpy(out + 2, commands, sizeof(commands));
        memset(out + 23, 1, KG_POLICY_MARKET_QUANTITIES);
    }
}

KG_HD void kag_action_mask_before(KagActionMaskState* s, int head, unsigned char* mask) {
    const KGState* g = s->game;
    const KGPlayer* p = &g->players[s->player];
    if (head < KG_POLICY_UNIT_HEADS) {
        unsigned char* out = mask + head * KG_POLICY_UNIT_COMMANDS;
        if (head >= KAG_MULTI_SLOTS * 3) {
            return;
        }
        int slot = head / 3, node = head % 3, stopped = 0, reserved = 0;
        int seeds_reserved[KG_NUM_CROPS] = {0};
        for (int prev = 0; prev < slot; prev++) {
            int intent = (int)s->choices[3 * prev];
            if (!intent) {
                stopped = 1;
                break;
            }
            int n = 1 + (int)s->choices[3 * prev + 1];
            reserved += n;
            if (intent >= KAG_MULTI_PLANT && intent < KAG_MULTI_COOP) {
                seeds_reserved[intent - 1] += n;
            }
        }
        memset(out, 0, KG_POLICY_UNIT_COMMANDS);
        if (stopped || (node && s->choices[3 * slot] == 0)) {
            out[0] = 1;
            return;
        }
        if (!s->multi_capacity_ready) {
            for (int intent = 1; intent < KAG_MULTI_INTENTS; intent++) {
                s->multi_capacity[intent] = kag_multi_capacity(g, s->player, intent, 0);
            }
            s->multi_capacity_ready = 1;
        }
        int workers = p->unit_count - reserved;
        if (node == 0) {
            out[0] = 1;
            for (int intent = 1; intent < KAG_MULTI_INTENTS; intent++) {
                int cap = s->multi_capacity[intent];
                if (intent < KAG_MULTI_COOP && p->seeds[intent - 1] <= seeds_reserved[intent - 1]) {
                    cap = 0;
                }
                out[intent] = workers > 0 && cap > 0;
            }
        } else {
            int intent = (int)s->choices[3 * slot];
            int cap = s->multi_capacity[intent];
            if (intent > 0 && intent < KAG_MULTI_COOP) {
                int available = p->seeds[intent - 1] - seeds_reserved[intent - 1];
                if (cap > available) {
                    cap = available;
                }
            }
            if (cap > workers) {
                cap = workers;
            }
            if (node == 1) {
                for (int q = 0; q < 44; q++) {
                    out[q] = q < cap;
                }
            } else {
                out[0] = 1;
                for (int q = 1; q < 5; q++) {
                    out[q] = kag_multi_capacity(g, s->player, intent, kag_macro_target_from_bin(q))
                        >= 1 + (int)s->choices[head - 1];
                }
            }
            out[0] = 1;
        }
        return;
    }
    if (!s->prepared) {
        KGAction work;
        kag_multi_work(&work, s->choices, g, s->player);
        kag_mask_prepare_market_from_work(s, &work);
    }
    int relative = head - KG_POLICY_UNIT_HEADS;
    int slot = relative / 3, node = relative % 3;
    unsigned char* out = kag_market_slot_mask(mask, slot);
    int stopped = slot >= s->policy->market_slots;
    for (int previous = 0; previous < slot; previous++) {
        stopped |= s->choices[KG_POLICY_UNIT_HEADS + 3 * previous] != 1;
    }
    if (node == 0) {
        int any = 0;
        for (int command = 0; command < KG_POLICY_MARKET_COMMANDS; command++) {
            out[2 + command] = !stopped && kag_mask_market_capacity(s, command) > 0;
            any |= out[2 + command];
        }
        out[0] = 1;
        out[1] = any;
        if (!any) {
            out[2] = 1;
        }
        memset(out + 23, 0, KG_POLICY_MARKET_QUANTITIES);
        out[23] = 1;
    } else if (node == 2) {
        int command = (int)s->choices[head - 1];
        int cap = stopped || s->choices[head - 2] == 0 ? 0 : kag_mask_market_capacity(s, command);
        int any = 0;
        for (int q = 0; q < KG_POLICY_MARKET_QUANTITIES; q++) {
            out[23 + q] = kag_market_quantity_spec(q) <= cap;
            any |= out[23 + q];
        }
        if (!any) {
            out[23] = 1;
        }
    }
}

KG_HD void kag_action_mask_commit(KagActionMaskState* s, int head, int action) {
    s->choices[head] = (float)action;
    if (head < KG_POLICY_UNIT_HEADS) {
        return;
    }
    int node = (head - KG_POLICY_UNIT_HEADS) % 3;
    if (node == 1 && s->choices[head - 1] == 1 && action >= KG_POLICY_MARKET_QUANTITY_COMMANDS) {
        kag_mask_market_commit(s, action, 1);
    }
    if (node == 2 && s->choices[head - 2] == 1
        && s->choices[head - 1] < KG_POLICY_MARKET_QUANTITY_COMMANDS) {
        kag_mask_market_commit(s, (int)s->choices[head - 1], kag_market_quantity_spec(action));
    }
}

KG_HD void kag_decode_multi_action(
    KGAction* action, const float* actions, const KGState* game, int p, KagPolicy* policy) {
    kag_multi_work(action, actions, game, p);
    policy->macro_intent[p] = kag_discrete_index(actions[0], KAG_MULTI_INTENTS);
    policy->macro_quantity[p] =
        policy->macro_intent[p] ? 1 + kag_discrete_index(actions[1], 44) : 0;
    policy->macro_target[p] =
        policy->macro_intent[p] ? kag_macro_target_from_bin(kag_discrete_index(actions[2], 5)) : 0;
    KagActionMaskState prefix;
    kag_action_mask_begin(&prefix, game, policy, p);
    memcpy(prefix.choices, actions, sizeof(prefix.choices));
    kag_mask_prepare_market_from_work(&prefix, action);
    int limit = policy->market_slots;
    if (limit > game->config.max_market_orders_per_turn) {
        limit = game->config.max_market_orders_per_turn;
    }
    for (int slot = 0; slot < limit; slot++) {
        int h = KG_POLICY_MARKET_HEAD_OFFSET + 3 * slot;
        if (kag_discrete_index(actions[h], 2) != 1) {
            break;
        }
        int id = kag_discrete_index(actions[h + 1], KG_POLICY_MARKET_COMMANDS);
        KGPolicyMarketSpec spec = kag_market_spec(id);
        int n = id < KG_POLICY_MARKET_QUANTITY_COMMANDS
            ? kag_market_quantity_spec(
                  kag_discrete_index(actions[h + 2], KG_POLICY_MARKET_QUANTITIES))
            : 1;
        /* Preserve the requested order, including quantity. Market fills may
         * differ under simultaneous opponent trades. Masks use only our own
         * observed prefix; the core remains the authority on actual fills. */
        action->market[action->market_count++] = (KGMarketOrder){spec.op, spec.item, n};
        if (kag_mask_market_capacity(&prefix, id) > 0) {
            kag_mask_market_commit(&prefix, id, n);
        }
    }
    if (kag_discrete_index(actions[KAG_MULTI_FEED_HEAD], 3) == 0 && action->market_count < limit) {
        const KGPlayer* f = &game->players[p];
        int need = 0, available = prefix.shed[KG_ITEM_WHEAT] + prefix.wheat_out_of_shed;
        for (int t = 0; t < KG_MAX_TILES; t++) {
            need += kg_is_animal_tile(&f->tiles[t]) && !f->tiles[t].fed_today;
        }
        for (int u = 0; u < f->unit_count; u++) {
            available += f->units[u].inventory[KG_ITEM_WHEAT];
        }
        /* FEED lowers need and carried stock equally. Stock harvested by this
         * turn's workers is not assumed available for feeding other workers. */
        need -= available;
        int command = KG_M_PRODUCT;
        int cap = kag_mask_market_capacity(&prefix, command);
        if (need > cap) {
            need = cap;
        }
        if (need > 0) {
            action->market[action->market_count++] =
                (KGMarketOrder){KG_MARKET_BUY_PRODUCT, KG_ITEM_WHEAT, need};
        }
    }
    int count = action->market_count, written = 0;
    int hands = game->players[p].hand_count, land_seen = 0;
    for (int i = 0; i < count; i++) {
        KGMarketOrder order = action->market[i];
        if (order.op == KG_MARKET_BUY_LAND && policy->land_buy_min_days > 0) {
            if (land_seen || !kag_land_buy_delay_ready(policy, game, p)) {
                continue;
            }
            land_seen = 1;
        }
        if (order.op == KG_MARKET_HIRE) {
            if (hands >= policy->max_hands) {
                continue;
            }
            hands++;
        }
        action->market[written++] = order;
    }
    action->market_count = written;
}

KG_HD int kag_action_head_active(const float* choices, int head) {
    if (head < KG_POLICY_UNIT_HEADS) {
        return 1;
    }
    int slot = (head - KG_POLICY_UNIT_HEADS) / 3;
    int node = (head - KG_POLICY_UNIT_HEADS) % 3;
    for (int prev = 0; prev < slot; prev++) {
        if (choices[KG_POLICY_UNIT_HEADS + 3 * prev] != 1) {
            return 0;
        }
    }
    if (node && choices[KG_POLICY_UNIT_HEADS + 3 * slot] != 1) {
        return 0;
    }
    return node != 2 || choices[head - 1] < KG_POLICY_MARKET_QUANTITY_COMMANDS;
}

void kag_sample_cpu_logits(KagPolicy* policy, const KGState* game, int player, const float* logits,
    int deterministic, unsigned int* rng, float* actions, unsigned char* mask) {
    const int sizes[KAG_ACTION_HEADS] = KAG_ACTION_SIZES;
    kag_write_mask(policy, game, player, mask);
    KagActionMaskState state;
    kag_action_mask_begin(&state, game, policy, player);
    int offset = 0;
    for (int h = 0; h < KAG_ACTION_HEADS; h++) {
        int size = sizes[h];
        kag_action_mask_before(&state, h, mask);
        int selected = 0;
        if (kag_action_head_active(state.choices, h)) {
            float maximum = -INFINITY, sum = 0;
            for (int a = 0; a < size; a++) {
                if (mask[offset + a]) {
                    if (logits[offset + a] > maximum) {
                        maximum = logits[offset + a];
                        selected = a;
                    }
                }
            }
            if (!deterministic) {
                for (int a = 0; a < size; a++) {
                    if (mask[offset + a]) {
                        sum += expf(logits[offset + a] - maximum);
                    }
                }
                *rng = 1664525u * *rng + 1013904223u;
                float target = (*rng >> 8) * (1.0f / 16777216.0f) * sum;
                for (int a = 0; a < size; a++) {
                    if (mask[offset + a]) {
                        selected = a;
                        target -= expf(logits[offset + a] - maximum);
                        if (target < 0) {
                            break;
                        }
                    }
                }
            }
            kag_action_mask_commit(&state, h, selected);
        }
        actions[h] = (float)selected;
        offset += size;
    }
}

enum {
    KAG_ROUTE_MAINTAIN,
    KAG_ROUTE_HARVEST,
    KAG_ROUTE_EMPTY,
    KAG_ROUTE_WEED,
    KAG_ROUTE_SHED,
    KAG_ROUTE_COUNT
};

typedef struct {
    uint8_t dx[KG_MAX_TILES];
    uint8_t dy[KG_MAX_TILES];
    uint8_t dist[KG_MAX_TILES];
    uint8_t source[KG_MAX_TILES];
    uint8_t present;
} KagRouteTable;

KG_HD int kag_route_class(const KGTile* tile, int day) {
    if (tile->kind == KG_TILE_PLANT) {
        if (!tile->watered_today) {
            return 0; /* maintain */
        }
        return tile->yield_units > 0
                && day - tile->planted_day >= KG_CROP_DEFS[tile->crop].first_yield_day
            ? 1
            : -1;
    }
    if (kg_is_animal_tile(tile)) {
        if (!tile->fed_today || !tile->cared_today) {
            return 0;
        }
        return tile->yield_units > 0 ? 1 : -1;
    }
    if (tile->kind == KG_TILE_EMPTY) {
        return 2;
    }
    if (tile->kind == KG_TILE_WEED) {
        return 3;
    }
    return -1;
}

KG_HD void kag_route_relax(KagRouteTable* table, int x, int y, int nx, int ny) {
    if (!table->source[kg_tile_index(x, y)] && (unsigned)nx < KG_MAX_BOARD_SIZE
        && (unsigned)ny < KG_MAX_BOARD_SIZE) {
        int src = kg_tile_index(nx, ny);
        int dst = kg_tile_index(x, y);
        int candidate = table->dist[src] + 1;
        int current = table->dist[dst];
        int src_source = table->dy[src] * KG_MAX_BOARD_SIZE + table->dx[src];
        int dst_source = table->dy[dst] * KG_MAX_BOARD_SIZE + table->dx[dst];
        if (candidate < current || (candidate == current && src_source < dst_source)) {
            table->dx[dst] = table->dx[src];
            table->dy[dst] = table->dy[src];
            table->dist[dst] = (uint8_t)candidate;
        }
    }
}

KG_HD void kag_build_route_table(
    KagRouteTable* table, const KGState* game, const KGPlayer* farm, int route_class) {
    memset(table->dx, 255, sizeof(table->dx));
    memset(table->dy, 255, sizeof(table->dy));
    memset(table->dist, 255, sizeof(table->dist));
    memset(table->source, 0, sizeof(table->source));
    table->present = 0;
    for (int tile = 0; tile < KG_MAX_TILES; tile++) {
        if (kag_route_class(&farm->tiles[tile], game->day) == route_class) {
            table->dx[tile] = (uint8_t)(tile % KG_MAX_BOARD_SIZE);
            table->dy[tile] = (uint8_t)(tile / KG_MAX_BOARD_SIZE);
            table->dist[tile] = 0;
            table->source[tile] = 1;
            table->present |= (uint8_t)(1u << route_class);
        }
    }
    for (int y = 0; y < KG_MAX_BOARD_SIZE; y++) {
        for (int x = 0; x < KG_MAX_BOARD_SIZE; x++) {
            kag_route_relax(table, x, y, x - 1, y);
            kag_route_relax(table, x, y, x, y - 1);
        }
    }
    for (int y = KG_MAX_BOARD_SIZE - 1; y >= 0; y--) {
        for (int x = KG_MAX_BOARD_SIZE - 1; x >= 0; x--) {
            kag_route_relax(table, x, y, x + 1, y);
            kag_route_relax(table, x, y, x, y + 1);
        }
    }
}

KG_HD void kag_update_quote_cache(KagPolicy* policy, const KGState* game, int item) {
    KagQuoteCache* c = &policy->quote_cache[item];
    int inventory = game->market.inventory[item];
    if (c->valid && c->inventory == inventory) {
        return;
    }
    const int quantities[8] = {1, 5, 10, 20, 30, 50, 75, 100};
    float proceeds = 0.0f;
    int order_bin = 0, bulk_bin = 0;
    for (int n = 1; n <= 100; n++) {
        proceeds += kg_market_price(item, inventory + n - 1);
        if (order_bin < 8 && n == (order_bin < 6 ? order_bin + 1 : order_bin == 6 ? 8 : 10)) {
            c->quotes[order_bin] = proceeds / 10000.0f;
            c->quotes[8 + order_bin] = kg_market_price(item, inventory + n) / 1000.0f;
            order_bin++;
        }
        if (bulk_bin < 8 && n == quantities[bulk_bin]) {
            c->quotes[16 + bulk_bin] = proceeds / 10000.0f;
            c->quotes[24 + bulk_bin] = kg_market_price(item, inventory + n) / 1000.0f;
            bulk_bin++;
        }
    }
    c->inventory = inventory;
    c->valid = 1;
}

KG_HD void kag_write_observation(KagPolicy* policy, const KGState* game, int pid, float* out) {
    const KGPlayer* me = &game->players[pid];
    const KGPlayer* opponent = &game->players[1 - pid];
    const KagObservationState* rs = &policy->history[pid];
    memset(out, 0, KAG_ENTITY_OBS_SIZE * sizeof(*out));
    float episode = (float)game->config.episode_steps;
    float board = (float)(game->config.board_size - 1);
    if (board < 1.0f) {
        board = 1.0f;
    }
    out[0] = me->money / 100000.0f;
    out[1] = opponent->money / 100000.0f;
    out[2] = ((float)me->money - opponent->money) / 100000.0f;
    out[3] = game->step / episode;
    out[4] = game->day * game->config.turns_per_day / episode;
    out[5] = game->hour / (float)game->config.turns_per_day;
    out[6] = (episode - game->step) / episode;
    for (int q = 0; q < 4; q++) {
        out[7 + q] = (me->unlocked_mask & (1 << q)) != 0;
        out[11 + q] = (opponent->unlocked_mask & (1 << q)) != 0;
    }
    out[15] = me->hires_today / 16.0f;
    out[16] = opponent->hires_today / 16.0f;
    out[17] = me->unit_count / 17.0f;
    out[18] = opponent->unit_count / 17.0f;
    for (int shop = 0; shop < game->shop_count; shop++) {
        out[19 + game->unlocked_shops[shop]] = 1.0f;
    }
    out[27] = (me->money % 1000) / 1000.0f;
    out[28] = (opponent->money % 1000) / 1000.0f;
    out[29] = kg_hire_cost(me->hires_today, game->config.farm_hand_cost_mult) / 10000.0f;
    int lands = kag_popcount(me->unlocked_mask);
    out[30] = lands < 4 ? (1000 << (lands - 1)) / 10000.0f : 0.0f;
    out[KAG_OBS_RESET_SOURCE_INDEX] = policy->reset_source != 0;
    out[32] = (game->step - rs->start_step) / episode;
    out[33] = rs->start_cash / 100000.0f;
    out[34] = rs->coverage_sum / episode;
    out[35] = rs->idle_sum / episode;
    out[36] = kag_land_buy_delay_ready(policy, game, pid);
    out[37] = policy->land_buy_min_days / 30.0f;
    out[38] = policy->land_fill_step[pid] >= 0;
    out[39] = -1.0f;
    if (policy->land_fill_step[pid] >= 0) {
        int left = policy->land_buy_min_days * game->config.turns_per_day
            - (game->step - policy->land_fill_step[pid]);
        out[39] = (left > 0 ? left : 0) / episode;
    }
    for (int c = 0; c < KG_NUM_CROPS; c++) {
        out[40 + c] = me->seeds[c] / 100.0f;
    }
    for (int item = 0; item < KG_NUM_ITEMS; item++) {
        out[45 + item] = me->shed[item] / 100.0f;
    }
    out[57] = rs->start_step / episode;
    out[58] = game->config.starting_money / 100000.0f;
    out[59] = game->config.shed_capacity / 100.0f;
    out[60] = (policy->max_hands > 0 && policy->max_hands < 16 ? policy->max_hands : 16) / 16.0f;
    out[61] = policy->market_slots / 10.0f;
    out[62] = game->config.turns_per_day / 24.0f;
    out[63] = game->config.episode_steps / 720.0f;
    for (int unit = 0; unit < KG_POLICY_UNITS && unit < opponent->unit_count; unit++) {
        out[64 + 3 * unit] = 1.0f;
        out[65 + 3 * unit] = opponent->units[unit].x / board;
        out[66 + 3 * unit] = opponent->units[unit].y / board;
    }
    out[115] = game->config.town_shop_sell_interval / 24.0f;
    out[116] = game->config.town_center_sell_interval / 24.0f;
    out[117] = game->config.town_shop_unlock_interval / 30.0f;
    if (game->config.town_shop_sell_interval > 0) {
        out[118] = (game->step % game->config.town_shop_sell_interval)
            / (float)game->config.town_shop_sell_interval;
    }
    if (game->config.town_center_sell_interval > 0) {
        out[119] = (game->step % game->config.town_center_sell_interval)
            / (float)game->config.town_center_sell_interval;
    }
    /* Each 8-bit limb survives the trainer's bf16 cast exactly. Keep the
     * uncapped magnitude above as well, so scale and small cash changes are
     * both available even late in a high-money game. */
    for (int limb = 0; limb < 4; limb++) {
        out[120 + limb] = (((uint32_t)me->money >> (8 * limb)) & 255u) / 256.0f;
        out[124 + limb] = (((uint32_t)opponent->money >> (8 * limb)) & 255u) / 256.0f;
    }

    int count[2][KG_NUM_PRODUCTS] = {{0}}, healthy[2][KG_NUM_PRODUCTS] = {{0}};
    int ready[2][KG_NUM_PRODUCTS] = {{0}}, ready_tiles[2][KG_NUM_PRODUCTS] = {{0}};
    int ages[KG_NUM_PRODUCTS] = {0}, held[KG_NUM_PRODUCTS] = {0};
    for (int view = 0; view < 2; view++) {
        const KGPlayer* p = &game->players[view == 0 ? pid : 1 - pid];
        int capacity[4] = {0};
        for (int q = 0; q < 4; q++) {
            float* row = out + KAG_PLOT_OFFSET + (view * 4 + q) * KAG_PLOT_FEATURES;
            row[0] = view == 0;
            row[1 + q] = 1.0f;
            row[5] = (p->unlocked_mask & (1 << q)) != 0;
        }
        for (int y = 0; y < game->config.board_size; y++) {
            for (int x = 0; x < game->config.board_size; x++) {
                int bit = kg_quadrant(x, y, game->config.board_size);
                int q = bit == 1 ? 0 : bit == 2 ? 1 : bit == 4 ? 2 : 3;
                capacity[q]++;
                const KGTile* t = &p->tiles[kg_tile_index(x, y)];
                float* row = out + KAG_PLOT_OFFSET + (view * 4 + q) * KAG_PLOT_FEATURES;
                row[6] += t->kind == KG_TILE_EMPTY;
                row[7] += t->kind == KG_TILE_WEED;
                int crop = t->kind == KG_TILE_PLANT && (unsigned)t->crop < KG_NUM_CROPS;
                int animal = kg_is_animal_tile(t);
                if (!crop && !animal) {
                    continue;
                }
                int item = crop ? t->crop : KG_ANIMAL_DEFS[t->animal].product;
                int good = kag_quality_tile(game, t), yield = kag_ready_units(game, t);
                row[8] += crop;
                row[9] += animal;
                row[10] += good;
                row[11] += crop && t->watered_today;
                row[12] += animal && t->fed_today;
                row[13] += animal ? t->cared_today : t->fertilized_until_day >= game->day;
                row[crop ? 14 : 15] += yield / 100.0f;
                row[crop ? 16 + t->crop : 21 + t->animal] += 1.0f;
                count[view][item]++;
                healthy[view][item] += good;
                ready[view][item] += yield;
                ready_tiles[view][item] += yield > 0;
                if (!view) {
                    ages[item] += game->day - (crop ? t->planted_day : t->placed_day);
                }
                if (animal) {
                    count[view][KG_ITEM_FERTILIZER]++;
                    healthy[view][KG_ITEM_FERTILIZER] += good;
                    ready[view][KG_ITEM_FERTILIZER] += t->fertilizer_available;
                    ready_tiles[view][KG_ITEM_FERTILIZER] += t->fertilizer_available != 0;
                }
            }
        }
        for (int q = 0; q < 4; q++) {
            float* row = out + KAG_PLOT_OFFSET + (view * 4 + q) * KAG_PLOT_FEATURES;
            float cap = capacity[q] ? (float)capacity[q] : 1.0f;
            for (int f = 6; f <= 13; f++) {
                row[f] /= cap;
            }
            for (int f = 16; f < 24; f++) {
                row[f] /= cap;
            }
        }
    }
    KagRouteTable routes[KAG_ROUTE_COUNT];
    for (int r = 0; r < KAG_ROUTE_SHED; r++) {
        kag_build_route_table(&routes[r], game, me, r);
    }
    for (int unit = 0; unit < KG_POLICY_UNITS && unit < me->unit_count; unit++) {
        const KGUnitState* u = &me->units[unit];
        float* row = out + KAG_WORKER_OFFSET + unit * KAG_WORKER_FEATURES;
        int tile = kg_tile_index(u->x, u->y);
        const KGTile* t = &me->tiles[tile];
        row[0] = 1.0f;
        row[1] = u->x / board;
        row[2] = u->y / board;
        row[3] = unit == 0;
        for (int item = 0; item < KG_NUM_ITEMS; item++) {
            row[4 + item] = u->inventory[item] / 100.0f;
            row[16] += row[4 + item];
            if (item < KG_NUM_PRODUCTS) {
                held[item] += u->inventory[item];
            }
        }
        int entity = t->kind;
        if (t->kind == KG_TILE_PLANT && (unsigned)t->crop < KG_NUM_CROPS) {
            entity = 5 + t->crop;
        } else if (kg_is_animal_tile(t)) {
            entity = 10 + t->animal;
        }
        row[17] = entity / 12.0f;
        int crop = t->kind == KG_TILE_PLANT, animal = kg_is_animal_tile(t);
        row[18] = crop ? (game->day - t->planted_day) / 30.0f
            : animal   ? (game->day - t->placed_day) / 30.0f
                       : 0.0f;
        row[19] = kag_ready_units(game, t) / 100.0f;
        row[20] = crop ? !t->watered_today : animal ? !t->fed_today : 0.0f;
        for (int r = 0; r < KAG_ROUTE_SHED; r++) {
            row[21 + 2 * r] = routes[r].dist[tile] == 255 ? -1.0f : routes[r].dx[tile] / board;
            row[22 + 2 * r] = routes[r].dist[tile] == 255 ? -1.0f : routes[r].dy[tile] / board;
        }
        KGPosition access[4];
        kg_shed_access_count(game->config.board_size, access);
        int nearest = 0, best = INT_MAX;
        for (int s = 0; s < 4; s++) {
            int distance = kag_abs((int)u->x - access[s].x) + kag_abs((int)u->y - access[s].y);
            if (distance < best) {
                best = distance;
                nearest = s;
            }
        }
        row[29] = access[nearest].x / board;
        row[30] = access[nearest].y / board;
        KGPosition pos = {u->x, u->y};
        row[31] = kg_is_shed_adjacent(&pos, game->config.board_size);
    }
    for (int item = 0; item < KG_NUM_PRODUCTS; item++) {
        float* row = out + KAG_PRODUCT_OFFSET + item * KAG_PRODUCT_FEATURES;
        row[item] = 1.0f;
        row[9] = game->market.prices[item] / 1000.0f;
        row[10] = (game->market.inventory[item] - KG_MARKET_DEFS[item].i0)
            / (float)KG_MARKET_DEFS[item].throughput;
        row[11] = me->shed[item] / 100.0f;
        row[12] = held[item] / 100.0f;
        row[13] = ready[0][item] / 100.0f;
        row[14] = count[0][item] / 25.0f;
        row[15] = healthy[0][item] / 25.0f;
        row[16] = ready_tiles[0][item] / 25.0f;
        if (item < KG_NUM_CROPS) {
            row[17] = me->seeds[item] / 100.0f;
            row[18] = KG_CROP_DEFS[item].seed_cost / 1000.0f;
        } else if (item < KG_ITEM_FERTILIZER) {
            int animal = item - KG_ITEM_EGG;
            row[17] = me->shed[KG_ITEM_GOOSE + animal] / 100.0f;
            row[18] = KG_ANIMAL_DEFS[animal].cost / 1000.0f;
        }
        row[19] = count[0][item] ? ages[item] / (30.0f * count[0][item]) : 0.0f;
        kag_update_quote_cache(policy, game, item);
        for (int f = 0; f < 32; f++) {
            row[20 + f] = policy->quote_cache[item].quotes[f];
        }
        row[52] = count[1][item] / 25.0f;
        row[53] = healthy[1][item] / 25.0f;
        row[54] = ready[1][item] / 100.0f;
        row[55] = KG_MARKET_DEFS[item].throughput / 500.0f;
    }
    float* task = out + KAG_TASK_OFFSET;
    for (int t = 0; t < KG_POLICY_UNIT_COMMANDS; t++) {
        task[t] = t == 0 || kag_multi_capacity(game, pid, t, 0) > 0;
    }
    /* v3 replaces duplicated tail features with controller identity and raw
     * episode peaks. These make sticky actions and milestone history visible. */
    task[44] = 2 / 3.0f;
    task[45] = 2;
    task[46] = policy->macro_intent[pid] / 44.0f;
    task[47] = 0;
    task[48] = policy->macro_quantity[pid] / 100.0f;
    task[49] = policy->macro_target[pid] / 15.0f;
    task[50] = rs->peak_plots / 4.0f;
    task[51] = rs->peak_crops / 100.0f;
    task[52] = rs->peak_animals / 100.0f;
    task[53] = 1.0f; /* state-aware market feasibility contract */
    task[54] = 0;
    task[55] = 1 / episode;
}
