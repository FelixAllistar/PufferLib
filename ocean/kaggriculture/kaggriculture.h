#pragma once

/*
 * PufferLib adapter for Kaggriculture.
 *
 * The rule engine is kept in kaggriculture_core.c so it can be differential
 * tested through its structured API. The practical trainer envelope has one
 * farmer head, eight direct farm-hand heads, three overflow cohorts, and
 * a conditionally visited ten-slot market tree. Each market slot first chooses
 * STOP/CONTINUE and only a continued slot chooses a command. The CUDA sampler
 * and PPO loss ignore every unvisited command/tail head, so fresh policies do
 * not receive entropy pressure to emit ten random transactions.
 * The direct heads cover the labor range used by strong play; overflow hands
 * remain scalable instead of being dropped. The rule core retains capacity
 * for all 240 hires accepted by a default day, independent of the policy
 * architecture.
 *
 * It is a CPU environment in PufferLib's native trainer, which still lets
 * the CUDA policy execute against many parallel environments.  A GPU-resident
 * environment is deliberately not claimed until the same parity tests cover
 * that path too.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pufferenv.h"
#include "kaggriculture_core.h"

#define KG_OBS_BOARD 10
#define KG_POLICY_DIRECT_HANDS 8
#define KG_POLICY_OVERFLOW_COHORTS 3
#define KG_POLICY_UNIT_HEADS \
    (1 + KG_POLICY_DIRECT_HANDS + KG_POLICY_OVERFLOW_COHORTS)
#define KG_POLICY_UNITS KG_POLICY_UNIT_HEADS
#define OBS_SIZE 1024
/* First byte after the stable semantic payload. Existing checkpoints keep the
 * same 1024-byte ABI; warm starts must zero this previously-unused encoder
 * column before reset-source observations are enabled. */
#define KAG_OBS_RESET_SOURCE_INDEX 942
/* The environment stores semantic observations as bytes.  Let the generic
 * byte->precision transfer normalize them once, before rollout storage and
 * every encoder/bank sees the data.  This removes a per-bank scale kernel from
 * the CUDA policy hot path while preserving the public byte ABI. */
#define PUFFERLIB_OBS_U8_NORMALIZED 1
#define PUFFER_EPISODE_PROGRESS_OBS_INDEX 3

/* Complete unit commands retain every operation/item. Market commands combine
 * operation, item, and a practical binary quantity. Repeated ordered slots can
 * express every useful exact quantity through the 100-tile/shed limits. */
#define KG_POLICY_UNIT_COMMANDS 44
#define KG_POLICY_MARKET_SLOTS 10
#define KG_POLICY_MARKET_CONTINUE_ACTIONS 2
#define KG_POLICY_MARKET_COMMANDS 21
#define KG_POLICY_MARKET_QUANTITIES 8
#define KG_POLICY_MARKET_QUANTITY_COMMANDS 19
#define KG_POLICY_MARKET_HEADS (3 * KG_POLICY_MARKET_SLOTS)
#define KG_POLICY_MARKET_HEAD_OFFSET KG_POLICY_UNIT_HEADS
#define NUM_ATNS (KG_POLICY_UNIT_HEADS + KG_POLICY_MARKET_HEADS)
#define KG_POLICY_UNIT_COMMAND_MASK_OFFSET 0
#define KG_POLICY_MARKET_MASK_OFFSET \
    (KG_POLICY_UNIT_HEADS * KG_POLICY_UNIT_COMMANDS)
#define KG_POLICY_MARKET_SLOT_MASK_SIZE \
    (KG_POLICY_MARKET_CONTINUE_ACTIONS + KG_POLICY_MARKET_COMMANDS \
        + KG_POLICY_MARKET_QUANTITIES)
#define KG_POLICY_ACTION_MASK_SIZE \
    (KG_POLICY_MARKET_MASK_OFFSET \
        + KG_POLICY_MARKET_SLOTS * KG_POLICY_MARKET_SLOT_MASK_SIZE)
#define ACT_SIZES { \
    44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44, \
    2, 21, 8, 2, 21, 8, 2, 21, 8, 2, 21, 8, 2, 21, 8, \
    2, 21, 8, 2, 21, 8, 2, 21, 8, 2, 21, 8, 2, 21, 8}
static const int KG_ACTION_SIZES[NUM_ATNS] = ACT_SIZES;
#define PUFFER_CONDITIONAL_MARKET_QUEUE 1
#define PUFFER_CONDITIONAL_PREFIX_HEADS KG_POLICY_UNIT_HEADS
#define PUFFER_CONDITIONAL_SLOTS KG_POLICY_MARKET_SLOTS
#define PUFFER_CONDITIONAL_HEADS_PER_SLOT 3
#define PUFFER_CONDITIONAL_QUANTITY_COMMANDS KG_POLICY_MARKET_QUANTITY_COMMANDS
#define PUFFER_CONDITIONAL_STOP 0
#define PUFFER_CONDITIONAL_CONTINUE 1
#ifdef __cplusplus
static_assert(OBS_SIZE == 1024, "Kaggriculture observation ABI changed");
static_assert(KG_POLICY_MARKET_SLOTS <= KG_MAX_MARKET_ORDERS,
    "market slots exceed core order capacity");
static_assert(NUM_ATNS == 42, "Kaggriculture action head count changed");
static_assert(KG_POLICY_ACTION_MASK_SIZE == 838,
    "Kaggriculture action mask ABI changed");
#else
_Static_assert(OBS_SIZE == 1024, "Kaggriculture observation ABI changed");
_Static_assert(KG_POLICY_MARKET_SLOTS <= KG_MAX_MARKET_ORDERS,
    "market slots exceed core order capacity");
_Static_assert(NUM_ATNS == 42, "Kaggriculture action head count changed");
_Static_assert(KG_POLICY_ACTION_MASK_SIZE == 838,
    "Kaggriculture action mask ABI changed");
#endif

enum {
    KG_POLICY_N_ONE = 1,
    KG_POLICY_N_ALL = -1,
    KG_U_PASS = 0,
    KG_U_MOVE = 1,
    KG_U_PICKUP = 5,
    KG_U_DROP = 17,
    KG_U_PLANT = 18,
    KG_U_SINGLE = 23,
    KG_U_PLACE = 32,
    KG_M_SEED = 0,
    KG_M_PRODUCT = 5,
    KG_M_ANIMAL = 7,
    KG_M_SELL = 10,
    KG_M_HIRE = 19,
    KG_M_LAND = 20,
};
typedef uint8_t obs_t;

struct Log {
    float perf;
    /* score/sweep_score are the learner's configured marked potential
     * (kag_player_potential_full), not cash. Cash and production are logged
     * separately below so a price-depressed but productive farm is visible. */
    float score;
    float sweep_score;
    float opponent_score;
    float money;
    float opponent_money;
    float gdp;
    float opponent_gdp;
    float production_units;
    float opponent_production_units;
    float crop_production_units;
    float animal_production_units;
    float successful_plants;
    float successful_animal_places;
    float sold_units;
    float sales_revenue;
    float bought_units;
    float purchase_spend;
    float crop_sold_units;
    float crop_sales_revenue;
    float animal_product_sold_units;
    float animal_product_sales_revenue;
    float strawberry_sold_units;
    float strawberry_sales_revenue;
    float milk_sold_units;
    float milk_sales_revenue;
    float ending_shed_units;
    float ending_shed_value;
    float carrot_opportunity_fraction;
    float carrot_opportunity_no_production_price;
    float carrot_opportunity_response;
    float carrot_opportunity_production;
    float carrot_nonopportunity_production;
    float carrot_opportunity_sold_units;
    float carrot_opportunity_sales_revenue;
    float carrot_opportunity_sale_price;
    float tomato_opportunity_fraction;
    float tomato_opportunity_no_production_price;
    float tomato_opportunity_response;
    float tomato_opportunity_production;
    float tomato_nonopportunity_production;
    float tomato_opportunity_sold_units;
    float tomato_opportunity_sales_revenue;
    float tomato_opportunity_sale_price;
    float egg_opportunity_fraction;
    float egg_opportunity_no_production_price;
    float egg_opportunity_response;
    float egg_opportunity_production;
    float egg_nonopportunity_production;
    float egg_opportunity_sold_units;
    float egg_opportunity_sales_revenue;
    float egg_opportunity_sale_price;
    float strawberry_units;
    float opponent_strawberry_units;
    float strawberry_value;
    float opponent_strawberry_value;
    float milk_units;
    float opponent_milk_units;
    float milk_value;
    float opponent_milk_value;
    float episode_return;
    float episode_length;
    float land_purchases;
    float water_coverage;
    float neglect_deaths;
    float planting_day_deaths;
    float unused_seed_value;
    float productive_extra_tiles;
    float win_rate;
    float draw_rate;
    float checkpoint_wins;
    float checkpoint_draws;
    float checkpoint_games;
    float mirror_games;
    float starter_games;
    float rules_games;
    float pass_games;
    float specialist_games;
    float script_games;
    float script_top_games;
    float adaptive_games;
    float script_frontier_games;
    float script_v20_games;
    float script_moon_games;
    float script_hamburger_games;
    float script_lugovoy_games;
    float script_thunder_games;
    float adaptive_pulse_games;
    float adaptive_structured_games;
    float adaptive_triad_games;
    float adaptive_thunder_games;
    float reset_games;
    float n;
    /* Episode action/terminal diagnostics. These stay out of the reward and
     * are averaged by vec_log like the existing score fields. */
    float market_orders;
    float buy_orders;
    float seed_buy_orders;
    float product_buy_orders;
    float animal_buy_orders;
    float sell_orders;
    float hire_orders;
    float land_orders;
    float animal_place_actions;
    float animal_feed_actions;
    float animal_care_actions;
    float animal_harvest_actions;
    float fertilizer_collect_actions;
    float plants_alive;
    float animals_alive;
    float weeds;
};

struct Env {
    Log log;
    Agent agents[KG_NUM_PLAYERS];
    int num_agents;
    unsigned int rng;
    int tag;
    int boundary_reached;
    int render_enabled;
    int render_selected_unit;
    int policy_market_slots;
    int policy_max_hands;
    int opening_turns;
    int reset_opening_turns;
    int reset_opening_min;
    int reset_source;
    uint32_t reset_opening_rng;
    const char* render_names[KG_NUM_PLAYERS];
    float episode_returns[KG_NUM_PLAYERS];
    float reward_potential_scale;
    float reward_potential_gamma;
    float reward_money_scale;
    float reward_win;
    float reward_seed_value;
    float reward_product_value;
    float reward_crop_value;
    float reward_animal_value;
    float reward_land_value;
    float reward_margin_scale;
    float reward_differential_scale;
    float reward_inactivity_threshold;
    float reset_opening_prob;
    float reward_neglect_discount;
    float reward_liquidation_days;
    float reward_productive_action;
    float reward_inactivity;
    float reward_neglect_death;
    float potential[KG_NUM_PLAYERS];
    int bot_opponent;
    float bot_opponent_fraction;
    int bot_first;
    float bot_top_fraction;
    float bot_script_fraction;
    float bot_adaptive_fraction;
    int demo_bots[KG_NUM_PLAYERS];

    /* Inline state makes the generic Puffer environment array an arena. */
    KGState game_storage;
};

/* The build scripts compile one translation unit for each environment. */
#include "kaggriculture_core.c"
#include "kaggriculture_bots.h"

KG_HD static inline uint8_t kag_u8(int value) {
    return (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value);
}

KG_HD static inline uint8_t kag_u8_scale(int value, int scale) {
    if (value <= 0) return 0;
    if (value >= scale) return 255;
    return (uint8_t)((value * 255) / scale);
}

KG_HD static inline uint8_t kag_u8_signed(int value, int magnitude) {
    if (value <= -magnitude) return 0;
    if (value >= magnitude) return 255;
    return (uint8_t)(((value + magnitude) * 255) / (2 * magnitude));
}

KG_HD static inline int kag_discrete_index(float value, int size) {
    int index = (int)value;
    if (index < 0) index = 0;
    if (index >= size) index = size - 1;
    return index;
}

KG_HD static inline int kag_popcount(unsigned value) {
#ifdef __CUDA_ARCH__
    return __popc(value);
#else
    return __builtin_popcount(value);
#endif
}

KG_HD static inline int kag_abs(int value) {
    return value < 0 ? -value : value;
}

KG_HD static inline float kag_player_potential_scaled(const Env* env,
        int player_id, int apply_liquidation) {
    const KGState* game = &env->game_storage;
    const KGPlayer* player = &game->players[player_id];
    float value = (float)player->money;
    float assets = 0.0f;
    float asset_scale = 1.0f;
    if (apply_liquidation) {
        int liquidation_steps = (int)(env->reward_liquidation_days
            * game->config.turns_per_day);
        int remaining_steps = game->config.episode_steps - game->step;
        if (liquidation_steps > 0 && remaining_steps < liquidation_steps) {
            asset_scale = remaining_steps > 0
                ? (float)remaining_steps / liquidation_steps : 0.0f;
        }
    }
    for (int crop = 0; crop < KG_NUM_CROPS; crop++) {
        assets += player->seeds[crop] * KG_CROP_DEFS[crop].seed_cost
            * env->reward_seed_value;
    }
    for (int item = 0; item < KG_NUM_PRODUCTS; item++) {
        assets += player->shed[item] * game->market.prices[item]
            * env->reward_product_value;
    }
    for (int animal = 0; animal < KG_NUM_ANIMALS; animal++) {
        assets += player->shed[KG_ITEM_GOOSE + animal]
            * KG_ANIMAL_DEFS[animal].cost * env->reward_animal_value;
    }
    for (int unit = 0; unit < player->unit_count; unit++) {
        const KGUnitState* held = &player->units[unit];
        /* inventory_order is maintained by kg_inventory_add/remove and only
         * contains nonzero entries.  Avoid touching all twelve item slots for
         * every hired hand on every reward-potential update. */
        for (int order = 0; order < held->inventory_order_count; order++) {
            int item = held->inventory_order[order];
            int count = held->inventory[item];
            if (item < KG_NUM_PRODUCTS) {
                assets += count * game->market.prices[item]
                    * env->reward_product_value;
            } else {
                int animal = item - KG_ITEM_GOOSE;
                if ((unsigned)animal < KG_NUM_ANIMALS) {
                    assets += count * KG_ANIMAL_DEFS[animal].cost
                        * env->reward_animal_value;
                }
            }
        }
    }
    for (int word = 0; word < KG_TILE_WORDS; word++) {
        uint64_t plants = player->plant_bits[word];
        while (plants) {
            int tile = word * 64 + kg_ctz64(plants);
            const KGTile* crop_tile = &player->tiles[tile];
            /* Rewarding only a previous neglect miss makes WATER payoff zero
             * for an already-healthy plant, which is why the model waters at
             * ~50% coverage: it has no reason to keep a good plant watered.
             * Discount on a missed tick instead, so watering today always
             * carries the value-protection signal. */
            float health = crop_tile->watered_today
                ? 1.0f : env->reward_neglect_discount;
            /* The planted field is worth its eventual output, not its seed
             * cost. This makes PLANT and WATER move potential immediately
             * instead of hiding the payout behind the harvest. Neglect still
             * devalues the field through the health multiplier. */
            const KGCropDef* plant_def = &KG_CROP_DEFS[crop_tile->crop];
            int expected = plant_def->max_yield
                * game->market.prices[crop_tile->crop];
            assets += (float)expected * env->reward_crop_value * health;
            /* Yield is already real inventory held on the tile. Valuing it
             * gives WATER/CARE their economic credit before HARVEST converts
             * it into carried inventory; the conversion itself stays neutral. */
            assets += crop_tile->yield_units
                * game->market.prices[crop_tile->crop]
                * env->reward_product_value * health;
            plants &= plants - 1;
        }
        uint64_t animals = player->animal_bits[word];
        while (animals) {
            int tile = word * 64 + kg_ctz64(animals);
            const KGTile* animal_tile = &player->tiles[tile];
            int animal = animal_tile->animal;
            if (animal >= 0) {
                /* Mirror the crop logic: feeding keeps the herd's value at
                 * full, skipping a feed devalues it. */
                float health = animal_tile->fed_today
                    ? 1.0f : env->reward_neglect_discount;
                /* The occupied structure is worth its expected production,
                 * not its purchase cost. This makes BUILD/PLACE and FEED
                 * move potential immediately, mirroring the crop change. */
                const KGAnimalDef* anim_def = &KG_ANIMAL_DEFS[animal];
                int expected = anim_def->max_held
                    * game->market.prices[anim_def->product];
                assets += (float)expected * env->reward_animal_value * health;
                assets += animal_tile->yield_units
                    * game->market.prices[anim_def->product]
                    * env->reward_product_value * health;
            }
            animals &= animals - 1;
        }
    }
    int extra_land = kag_popcount((unsigned)player->unlocked_mask) - 1;
    int land_value = extra_land <= 0 ? 0
        : extra_land == 1 ? 1000 : extra_land == 2 ? 3000 : 7000;
    assets += land_value
        * env->reward_land_value;
    return value + asset_scale * assets;
}

/* Reward shaping uses the liquidation-aware potential above.  Terminal
 * reporting deliberately uses the full mark-to-market value: otherwise a
 * nonzero reward_liquidation_days would write every unsold asset down to zero
 * exactly at the episode boundary and hide the player's actual farm. */
KG_HD static inline float kag_player_potential(const Env* env, int player_id) {
    return kag_player_potential_scaled(env, player_id, 1);
}

KG_HD static inline float kag_player_potential_full(const Env* env, int player_id) {
    return kag_player_potential_scaled(env, player_id, 0);
}

/* Legacy shaping used an undiscounted dollar delta.  When a positive shaping
 * gamma is configured, use the discount-consistent potential form on
 * a centered, starting-money-normalized potential instead.  Centering keeps
 * the initial state at zero; forcing terminal phi to zero prevents unsold
 * assets or final cash from becoming a second terminal objective. */
KG_HD static inline float kag_potential_shaping_reward(const Env* env,
        float before, float after, int done) {
    if (env->reward_potential_gamma <= 0.0f) {
        return (after - before) * env->reward_potential_scale;
    }
    float starting_money = (float)env->game_storage.config.starting_money;
    float before_phi = (before - starting_money) / starting_money;
    float after_phi = done ? 0.0f
        : (after - starting_money) / starting_money;
    return (env->reward_potential_gamma * after_phi - before_phi)
        * env->reward_potential_scale;
}

KG_HD static inline float kag_terminal_money_reward(const Env* env,
        int money) {
    float starting_money = (float)env->game_storage.config.starting_money;
    return env->reward_money_scale
        * ((float)money - starting_money) / starting_money;
}

KG_HD static inline uint8_t kag_tile_entity(const KGTile* tile) {
    if (tile->kind == KG_TILE_PLANT
            && tile->crop >= 0 && tile->crop < KG_NUM_CROPS) {
        return (uint8_t)(5 + tile->crop);
    }
    if (kg_is_animal_tile(tile)) {
        return (uint8_t)(10 + tile->animal);
    }
    return tile->kind;
}

KG_HD static inline void kag_encode_tile(const KGTile* tile, int day, obs_t* out) {
    int age = tile->kind == KG_TILE_PLANT ? day - tile->planted_day
        : kg_is_animal_tile(tile) ? day - tile->placed_day : 0;
    out[0] = kag_u8_scale(kag_tile_entity(tile), 12);
    out[1] = kag_u8_scale(age, 30);
    if (tile->kind == KG_TILE_PLANT) {
        int yield = tile->yield_units > 63 ? 63 : tile->yield_units;
        int neglect = tile->consecutive_unwatered > 3
            ? 3 : tile->consecutive_unwatered;
        int fertilized = tile->fertilized_until_day >= day
            ? tile->fertilized_until_day - day + 1 : 0;
        if (fertilized > 7) fertilized = 7;
        int at_risk = neglect > 0 && !tile->watered_today;
        out[2] = (uint8_t)(kag_u8_scale(yield, 63) / 2
            + (tile->watered_today ? 128 : 0));
        out[3] = (uint8_t)((at_risk ? 128 : 0) + fertilized * 16);
    } else if (kg_is_animal_tile(tile)) {
        int yield = tile->yield_units > 63 ? 63 : tile->yield_units;
        int neglect = tile->consecutive_unfed > 3
            ? 3 : tile->consecutive_unfed;
        int pending = tile->pending_care_bonus > 31
            ? 31 : tile->pending_care_bonus;
        int at_risk = neglect > 0 && !tile->fed_today;
        out[2] = (uint8_t)(kag_u8_scale(yield, 63) / 2
            + (tile->fed_today ? 128 : 0));
        out[3] = (uint8_t)((at_risk ? 128 : 0)
            + (tile->cared_today ? 64 : 0)
            + (tile->fertilizer_available ? 32 : 0)
            + (pending > 15 ? 15 : pending) * 2);
    } else {
        out[2] = 0;
        out[3] = 0;
    }
}

/* Nearest-target tables for the route fields.  kag_build_route_tables runs
 * once per farm per observation write; each unit/cohort then does O(1)
 * lookups instead of scanning all 100 tiles.  Table bytes match the previous
 * per-unit scan exactly, including the first-tile-wins tie break. */
enum {KAG_ROUTE_MAINTAIN, KAG_ROUTE_HARVEST, KAG_ROUTE_EMPTY,
    KAG_ROUTE_WEED, KAG_ROUTE_SHED, KAG_ROUTE_COUNT};

typedef struct {
    uint8_t dx[KG_MAX_TILES];
    uint8_t dy[KG_MAX_TILES];
    uint8_t dist[KG_MAX_TILES];
    uint8_t source[KG_MAX_TILES];
    uint8_t present;
} KagRouteTable;

KG_HD static inline void kag_encode_unit_routes(const KGState* game,
        const KGPlayer* farm, const KagRouteTable tables[KAG_ROUTE_COUNT],
        int ux, int uy, obs_t* out) {
    const KGTile* current = &farm->tiles[kg_tile_index(ux, uy)];
    int status = 0;
    if (current->kind == KG_TILE_EMPTY) status |= 128;
    if (current->kind == KG_TILE_WEED) status |= 64;
    if (current->kind == KG_TILE_PLANT) {
        if (!current->watered_today) status |= 128;
        if (current->yield_units > 0
                && game->day - current->planted_day
                    >= KG_CROP_DEFS[current->crop].first_yield_day) status |= 64;
        if (current->consecutive_unwatered > 0
                && !current->watered_today) status |= 32;
    } else if (kg_is_animal_tile(current)) {
        if (!current->fed_today) status |= 128;
        if (current->yield_units > 0) status |= 64;
        if (!current->cared_today) status |= 32;
        if (current->fertilizer_available) status |= 16;
    }
    KGPosition unit_pos = {(uint8_t)ux, (uint8_t)uy};
    if (kg_is_shed_adjacent(&unit_pos, game->config.board_size)) status |= 8;
    out[0] = kag_u8_scale(kag_tile_entity(current), 12);
    out[1] = (uint8_t)status;

    int present = tables[0].present | tables[1].present
        | tables[2].present | tables[3].present;
    int index = kg_tile_index(ux, uy);
    out[2] = (uint8_t)((present | (1 << KAG_ROUTE_SHED)) * 8);
    int k = 3;
    for (int route = 0; route < KAG_ROUTE_COUNT; route++) {
        int dx = 0;
        int dy = 0;
        if (tables[route].dist[index] != 255) {
            dx = tables[route].dx[index] - ux;
            dy = tables[route].dy[index] - uy;
        }
        if (route == KAG_ROUTE_SHED) {
            dx = 4 - ux;
            dy = 4 - uy;
            int best = kag_abs(dx) + kag_abs(dy);
            for (int access = 1; access < 4; access++) {
                int sx = access == 1 || access == 3 ? 5 : 4;
                int sy = access >= 2 ? 5 : 4;
                int distance = kag_abs(sx - ux) + kag_abs(sy - uy);
                if (distance < best) {
                    best = distance;
                    dx = sx - ux;
                    dy = sy - uy;
                }
            }
        }
        out[k++] = kag_u8_signed(dx, 9);
        out[k++] = kag_u8_signed(dy, 9);
    }
}

/* Public farm features are identical in both players' observations.  Build
 * them once per environment step instead of scanning both 100-tile farms a
 * second time while writing the opposite private view. */
typedef struct {
    int entity[4][13];
    int state[4][8];
    int crop_state[KG_NUM_CROPS][6];
    int animal_state[KG_NUM_ANIMALS][6];
} KagFarmSummary;

KG_HD static inline int kag_route_class(const KGTile* tile, int day) {
    if (tile->kind == KG_TILE_PLANT) {
        if (!tile->watered_today) return 0; /* maintain */
        return tile->yield_units > 0
            && day - tile->planted_day
                >= KG_CROP_DEFS[tile->crop].first_yield_day ? 1 : -1;
    }
    if (kg_is_animal_tile(tile)) {
        if (!tile->fed_today || !tile->cared_today) return 0;
        return tile->yield_units > 0 ? 1 : -1;
    }
    if (tile->kind == KG_TILE_EMPTY) return 2;
    if (tile->kind == KG_TILE_WEED) return 3;
    return -1;
}

KG_HD static inline void kag_route_relax(KagRouteTable* table,
        int x, int y, int nx, int ny) {
    if (!table->source[kg_tile_index(x, y)]
            && (unsigned)nx < KG_MAX_BOARD_SIZE
            && (unsigned)ny < KG_MAX_BOARD_SIZE) {
        int src = kg_tile_index(nx, ny);
        int dst = kg_tile_index(x, y);
        int candidate = table->dist[src] + 1;
        int current = table->dist[dst];
        int src_source = table->dy[src] * KG_MAX_BOARD_SIZE + table->dx[src];
        int dst_source = table->dy[dst] * KG_MAX_BOARD_SIZE + table->dx[dst];
        if (candidate < current
                || (candidate == current && src_source < dst_source)) {
            table->dx[dst] = table->dx[src];
            table->dy[dst] = table->dy[src];
            table->dist[dst] = (uint8_t)candidate;
        }
    }
}

KG_HD static inline void kag_build_route_table(KagRouteTable* table,
        const KGState* game, const KGPlayer* farm, int route_class) {
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

KG_HD static inline void kag_collect_farm_summary(const KGState* game,
        const KGPlayer* farm, KagFarmSummary* summary) {
    memset(summary, 0, sizeof(*summary));
    for (int tile = 0; tile < KG_MAX_TILES; tile++) {
        const KGTile* t = &farm->tiles[tile];
        int x = tile % KG_MAX_BOARD_SIZE;
        int y = tile / KG_MAX_BOARD_SIZE;
        int quadrant = (x >= 5) + 2 * (y >= 5);
        int kind = kag_tile_entity(t);
        if ((unsigned)kind < 13) summary->entity[quadrant][kind]++;
        if (t->kind == KG_TILE_PLANT && (unsigned)t->crop < KG_NUM_CROPS) {
            int age = game->day - t->planted_day;
            int needs = !t->watered_today;
            int risk = needs && t->consecutive_unwatered > 0;
            int harvestable = t->yield_units > 0
                && age >= KG_CROP_DEFS[t->crop].first_yield_day;
            int cared = t->fertilized_until_day >= game->day;
            int* state = summary->state[quadrant];
            state[0] += age;
            state[1] += t->yield_units;
            state[2] += needs;
            state[3] += risk;
            state[4] += harvestable;
            state[5] += cared;
            int* crop = summary->crop_state[t->crop];
            crop[0]++;
            crop[1] += needs;
            crop[2] += risk;
            crop[3] += harvestable;
            crop[4] += t->yield_units;
            crop[5] += age;
        } else if (kg_is_animal_tile(t)) {
            int age = game->day - t->placed_day;
            int needs = !t->fed_today;
            int risk = needs && t->consecutive_unfed > 0;
            int* state = summary->state[quadrant];
            state[0] += age;
            state[1] += t->yield_units;
            state[2] += needs;
            state[3] += risk;
            state[4] += t->yield_units > 0;
            state[5] += t->cared_today;
            state[6] += t->fertilizer_available;
            state[7] += t->pending_care_bonus;
            if ((unsigned)t->animal < KG_NUM_ANIMALS) {
                int* animal = summary->animal_state[t->animal];
                animal[0]++;
                animal[1] += needs;
                animal[2] += risk;
                animal[3] += t->yield_units;
                animal[4] += t->cared_today;
                animal[5] += t->fertilizer_available;
            }
        }
    }
}

KG_HD static inline void kag_write_observation_with_summaries(Env* env, int player_id,
        const KagFarmSummary summaries[KG_NUM_PLAYERS]) {
    obs_t* out = (obs_t*)env->agents[player_id].observations;
    KGState* game = &env->game_storage;
    KGPlayer* me = &game->players[player_id];
    KGPlayer* opponent = &game->players[1 - player_id];
    int k = 0;

    out[k++] = kag_u8_scale(me->money, 100000);
    out[k++] = kag_u8_scale(opponent->money, 100000);
    out[k++] = kag_u8_signed(me->money - opponent->money, 100000);
    out[k++] = kag_u8_scale(game->step, game->config.episode_steps);
    int season_days = game->config.episode_steps / game->config.turns_per_day;
    out[k++] = kag_u8_scale(game->day, season_days > 1 ? season_days - 1 : 1);
    out[k++] = kag_u8_scale(game->hour,
        game->config.turns_per_day > 1 ? game->config.turns_per_day - 1 : 1);
    out[k++] = kag_u8_scale(game->config.episode_steps - game->step,
        game->config.episode_steps);
    for (int bit = 0; bit < 4; bit++) {
        out[k++] = (me->unlocked_mask & (1 << bit)) ? 255 : 0;
    }
    for (int bit = 0; bit < 4; bit++) {
        out[k++] = (opponent->unlocked_mask & (1 << bit)) ? 255 : 0;
    }
    out[k++] = kag_u8_scale(me->hires_today, 16);
    out[k++] = kag_u8_scale(opponent->hires_today, 16);
    out[k++] = kag_u8_scale(me->unit_count, 16);
    out[k++] = kag_u8_scale(opponent->unit_count, 16);
    uint8_t shops = 0;
    for (int shop = 0; shop < game->shop_count; shop++) {
        shops |= (uint8_t)(1u << game->unlocked_shops[shop]);
    }
    for (int bit = 0; bit < 8; bit++) out[k++] = (shops & (1 << bit)) ? 255 : 0;
    int season_phase = season_days > 0 ? 4 * game->day / season_days : 0;
    if (season_phase > 3) season_phase = 3;
    for (int phase = 0; phase < 4; phase++) {
        out[k++] = phase == season_phase ? 255 : 0;
    }

    /* Farms interact only through the market. Preserve public production and
     * lifecycle state by product/quadrant without forcing the model to decode
     * 200 ordinal cell IDs through one dense matrix. */
    for (int view_player = 0; view_player < KG_NUM_PLAYERS; view_player++) {
        const KagFarmSummary* summary = &summaries[view_player];
        for (int quadrant = 0; quadrant < 4; quadrant++) {
            for (int kind = 0; kind < 13; kind++) {
                out[k++] = kag_u8_scale(summary->entity[quadrant][kind], 25);
            }
            out[k++] = kag_u8_scale(summary->state[quadrant][0], 25 * 30);
            out[k++] = kag_u8_scale(summary->state[quadrant][1], 25 * 16);
            for (int feature = 2; feature < 7; feature++) {
                out[k++] = kag_u8_scale(summary->state[quadrant][feature], 25);
            }
            out[k++] = kag_u8_scale(summary->state[quadrant][7], 25 * 16);
        }
        for (int crop = 0; crop < KG_NUM_CROPS; crop++) {
            for (int feature = 0; feature < 4; feature++) {
                out[k++] = kag_u8_scale(summary->crop_state[crop][feature], 100);
            }
            out[k++] = kag_u8_scale(summary->crop_state[crop][4], 100 * 16);
            out[k++] = kag_u8_scale(summary->crop_state[crop][5], 100 * 30);
        }
        for (int animal = 0; animal < KG_NUM_ANIMALS; animal++) {
            for (int feature = 0; feature < 6; feature++) {
                int scale = feature == 3 ? 100 * 16 : 100;
                out[k++] = kag_u8_scale(summary->animal_state[animal][feature], scale);
            }
        }
    }

    for (int item = 0; item < KG_NUM_ITEMS; item++) {
        out[k++] = kag_u8_scale(me->shed[item], 100);
    }
    for (int crop = 0; crop < KG_NUM_CROPS; crop++) {
        out[k++] = kag_u8_scale(me->seeds[crop], 100);
    }

    KagRouteTable route_tables[KAG_ROUTE_COUNT];
    for (int route = 0; route < KAG_ROUTE_COUNT; route++) {
        kag_build_route_table(&route_tables[route], game, me, route);
    }
    for (int view = 0; view < KG_POLICY_UNIT_HEADS; view++) {
        int count = 0;
        int sum_x = 0;
        int sum_y = 0;
        int inventory[KG_NUM_ITEMS] = {0};
        int overflow = view > KG_POLICY_DIRECT_HANDS;
        int first = overflow
            ? 1 + KG_POLICY_DIRECT_HANDS
                + view - (1 + KG_POLICY_DIRECT_HANDS)
            : view;
        int stride = overflow ? KG_POLICY_OVERFLOW_COHORTS : KG_MAX_HANDS + 1;
        for (int unit_id = first; unit_id < me->unit_count; unit_id += stride) {
            KGUnitState* unit = &me->units[unit_id];
            count++;
            sum_x += unit->x;
            sum_y += unit->y;
            for (int item = 0; item < KG_NUM_ITEMS; item++) {
                inventory[item] += unit->inventory[item];
            }
        }
        if (count == 0) {
            for (int zero = 0; zero < 48; zero++) out[k++] = 0;
        } else {
            int ux = sum_x / count;
            int uy = sum_y / count;
            uint8_t route_bytes[13];
            kag_encode_unit_routes(game, me, route_tables,
                ux, uy, route_bytes);
            out[k++] = kag_u8_scale(count, 16);
            out[k++] = kag_u8_scale(ux, 9);
            out[k++] = kag_u8_scale(uy, 9);
            int entity = kag_tile_entity(&me->tiles[kg_tile_index(ux, uy)]);
            for (int kind = 0; kind < 13; kind++) {
                out[k++] = kind == entity ? 255 : 0;
            }
            for (int bit = 0; bit < 5; bit++) {
                out[k++] = (route_bytes[1] & (128 >> bit)) ? 255 : 0;
            }
            for (int item = 0; item < KG_NUM_ITEMS; item++) {
                out[k++] = kag_u8_scale(inventory[item], 100);
            }
            int present = route_bytes[2] / 8;
            for (int route = 0; route < 5; route++) {
                out[k++] = (present & (1 << route)) ? 255 : 0;
            }
            for (int route_coord = 0; route_coord < 10; route_coord++) {
                out[k++] = route_bytes[3 + route_coord];
            }
        }
    }
    for (int view = 0; view < KG_POLICY_UNIT_HEADS; view++) {
        int count = 0;
        int sum_x = 0;
        int sum_y = 0;
        int overflow = view > KG_POLICY_DIRECT_HANDS;
        int first = overflow
            ? 1 + KG_POLICY_DIRECT_HANDS
                + view - (1 + KG_POLICY_DIRECT_HANDS)
            : view;
        int stride = overflow ? KG_POLICY_OVERFLOW_COHORTS : KG_MAX_HANDS + 1;
        for (int unit_id = first; unit_id < opponent->unit_count; unit_id += stride) {
            const KGUnitState* unit = &opponent->units[unit_id];
            count++;
            sum_x += unit->x;
            sum_y += unit->y;
        }
        if (count == 0) {
            for (int zero = 0; zero < 3; zero++) out[k++] = 0;
        } else {
            out[k++] = kag_u8_scale(count, 16);
            out[k++] = kag_u8_scale(sum_x / count, 9);
            out[k++] = kag_u8_scale(sum_y / count, 9);
        }
    }
    for (int item = 0; item < KG_NUM_PRODUCTS; item++) {
        out[k++] = kag_u8_signed(game->market.inventory[item]
            - KG_MARKET_DEFS[item].i0, KG_MARKET_DEFS[item].throughput * 4);
    }
    for (int item = 0; item < KG_NUM_PRODUCTS; item++) {
        out[k++] = kag_u8_scale(game->market.prices[item], 1000);
    }
    /* DAGS source bit: root episodes are 0, data/reset starts are 255. */
#ifndef __CUDA_ARCH__
    if (k != KAG_OBS_RESET_SOURCE_INDEX) {
        fprintf(stderr,
            "kaggriculture reset-source observation moved: %d != %d\n",
            k, KAG_OBS_RESET_SOURCE_INDEX);
        abort();
    }
#endif
    out[k++] = env->reset_source ? 255 : 0;
    if (k > OBS_SIZE) {
#ifndef __CUDA_ARCH__
        fprintf(stderr, "kaggriculture observation overflow: %d > %d\n", k, OBS_SIZE);
        abort();
#endif
    }
    if (k < OBS_SIZE) {
        memset(out + k, 0, (size_t)(OBS_SIZE - k) * sizeof(obs_t));
    }
}

KG_HD static inline void kag_write_observation(Env* env, int player_id) {
    KagFarmSummary summaries[KG_NUM_PLAYERS];
    for (int player = 0; player < KG_NUM_PLAYERS; player++) {
        kag_collect_farm_summary(&env->game_storage,
            &env->game_storage.players[player], &summaries[player]);
    }
    kag_write_observation_with_summaries(env, player_id, summaries);
}

typedef struct {
    int op;
    int arg;
    int n;
} KGPolicyUnitSpec;

typedef struct {
    int op;
    int item;
    int n;
} KGPolicyMarketSpec;

KG_HD static inline int kag_single_op(int index) {
    switch (index) {
        case 0: return KG_OP_WATER;
        case 1: return KG_OP_HARVEST;
        case 2: return KG_OP_FERTILIZE;
        case 3: return KG_OP_BUILD_COOP;
        case 4: return KG_OP_BUILD_PASTURE;
        case 5: return KG_OP_DIG;
        case 6: return KG_OP_FEED;
        case 7: return KG_OP_COLLECT_FERTILIZER;
        default: return KG_OP_CARE;
    }
}

KG_HD static inline KGPolicyUnitSpec kag_unit_spec(int id) {
    if ((unsigned)id >= KG_POLICY_UNIT_COMMANDS) id = 0;
    if (id == KG_U_PASS) return (KGPolicyUnitSpec){KG_OP_PASS, -1, 1};
    if (id < KG_U_PICKUP) {
        return (KGPolicyUnitSpec){KG_OP_NORTH + id - KG_U_MOVE, -1, 1};
    }
    if (id < KG_U_DROP) {
        return (KGPolicyUnitSpec){KG_OP_PICKUP, id - KG_U_PICKUP, 1};
    }
    if (id == KG_U_DROP) return (KGPolicyUnitSpec){KG_OP_DROP, -1, KG_POLICY_N_ALL};
    if (id < KG_U_SINGLE) {
        return (KGPolicyUnitSpec){KG_OP_PLANT, id - KG_U_PLANT, 1};
    }
    if (id < KG_U_PLACE) {
        return (KGPolicyUnitSpec){kag_single_op(id - KG_U_SINGLE), -1, 1};
    }
    return (KGPolicyUnitSpec){KG_OP_PLACE, id - KG_U_PLACE, 1};
}

KG_HD static inline KGPolicyMarketSpec kag_market_spec(int id) {
    if ((unsigned)id >= KG_POLICY_MARKET_COMMANDS) id = 0;
    if (id < KG_M_PRODUCT) {
        return (KGPolicyMarketSpec){KG_MARKET_BUY_SEED,
            id - KG_M_SEED, 1};
    }
    if (id < KG_M_ANIMAL) {
        return (KGPolicyMarketSpec){KG_MARKET_BUY_PRODUCT,
            id == KG_M_PRODUCT ? KG_ITEM_WHEAT : KG_ITEM_FERTILIZER, 1};
    }
    if (id < KG_M_SELL) {
        return (KGPolicyMarketSpec){KG_MARKET_BUY_ANIMAL,
            KG_ITEM_GOOSE + id - KG_M_ANIMAL, 1};
    }
    if (id < KG_M_HIRE) {
        return (KGPolicyMarketSpec){KG_MARKET_SELL,
            id - KG_M_SELL, 1};
    }
    if (id == KG_M_HIRE) {
        return (KGPolicyMarketSpec){KG_MARKET_HIRE, KG_ITEM_INVALID, 1};
    }
    return (KGPolicyMarketSpec){KG_MARKET_BUY_LAND, KG_ITEM_INVALID, 1};
}

KG_HD static inline int kag_unit_action_id(int op, int arg, int n) {
    (void)n;
    for (int id = 0; id < KG_POLICY_UNIT_COMMANDS; id++) {
        KGPolicyUnitSpec spec = kag_unit_spec(id);
        if (spec.op == op && spec.arg == arg) return id;
    }
    return 0;
}

KG_HD static inline int kag_market_action_id(int op, int item, int n) {
    (void)n;
    for (int id = 0; id < KG_POLICY_MARKET_COMMANDS; id++) {
        KGPolicyMarketSpec spec = kag_market_spec(id);
        if (spec.op == op && spec.item == item) return id;
    }
    return 0;
}

KG_HD static inline int kag_market_quantity_spec(int id) {
    if ((unsigned)id >= KG_POLICY_MARKET_QUANTITIES) id = 0;
    return id < 6 ? id + 1 : id == 6 ? 8 : 10;
}

KG_HD static inline int kag_market_quantity_id(int n) {
    if (n < 0) return KG_POLICY_MARKET_QUANTITIES - 1;
    int best = 0;
    for (int id = 1; id < KG_POLICY_MARKET_QUANTITIES; id++) {
        if (kag_market_quantity_spec(id) > n) break;
        best = id;
    }
    return best;
}

KG_HD static inline int kag_market_quantity(const KGState* game, const KGPlayer* player,
        KGPolicyMarketSpec spec) {
    (void)game;
    if (spec.n != KG_POLICY_N_ALL) return spec.n;
    if (spec.op == KG_MARKET_SELL) return player->shed[spec.item];
    if (spec.op == KG_MARKET_BUY_SEED) {
        int affordable = player->money / KG_CROP_DEFS[spec.item].seed_cost;
        return affordable < KG_MAX_TILES ? affordable : KG_MAX_TILES;
    }
    if (spec.op == KG_MARKET_BUY_ANIMAL) {
        int animal = spec.item - KG_ITEM_GOOSE;
        int affordable = player->money / KG_ANIMAL_DEFS[animal].cost;
        return affordable < KG_MAX_TILES ? affordable : KG_MAX_TILES;
    }
    if (spec.op == KG_MARKET_BUY_PRODUCT) {
        int price = game->market.prices[spec.item];
        int affordable = price > 0 ? player->money / price : 0;
        return affordable < game->config.shed_capacity
            ? affordable : game->config.shed_capacity;
    }
    return 1;
}

KG_HD static inline int kag_unit_action_legal(const KGState* game, const KGPlayer* player,
        int unit_id, KGPolicyUnitSpec spec) {
    const KGUnitState* unit;
    const KGTile* tile;
    KGPosition pos;
    int x;
    int y;
    if (unit_id < 0 || unit_id >= player->unit_count) return spec.op == KG_OP_PASS;
    unit = &player->units[unit_id];
    x = unit->x;
    y = unit->y;
    pos = (KGPosition){(uint8_t)x, (uint8_t)y};
    tile = &player->tiles[y * KG_MAX_BOARD_SIZE + x];
    if (spec.op == KG_OP_PASS) return 1;
    if (spec.op >= KG_OP_NORTH && spec.op <= KG_OP_WEST) {
        int move = spec.op - KG_OP_NORTH;
        int dx = move == 2 ? 1 : move == 3 ? -1 : 0;
        int dy = move == 0 ? -1 : move == 1 ? 1 : 0;
        return kg_unit_can_move_to(player, x + dx, y + dy,
            game->config.board_size);
    }
    if (tile->kind == KG_TILE_LOCKED) return 0;
    if (spec.op == KG_OP_PICKUP) {
        return kg_is_shed_adjacent(&pos, game->config.board_size)
            && spec.arg >= 0 && spec.arg < KG_NUM_ITEMS
            && player->shed[spec.arg] > 0;
    }
    if (spec.op == KG_OP_DROP) {
        return kg_is_shed_adjacent(&pos, game->config.board_size)
            && unit->inventory_order_count > 0;
    }
    if (spec.op == KG_OP_PLANT) {
        return spec.arg >= 0 && spec.arg < KG_NUM_CROPS
            && tile->kind == KG_TILE_EMPTY && player->seeds[spec.arg] > 0;
    }
    if (spec.op == KG_OP_WATER) {
        return tile->kind == KG_TILE_PLANT && !tile->watered_today;
    }
    if (spec.op == KG_OP_HARVEST) {
        if (tile->yield_units <= 0) return 0;
        if (tile->kind == KG_TILE_PLANT) {
            return tile->crop >= 0 && tile->crop < KG_NUM_CROPS
                && game->day - tile->planted_day
                    >= KG_CROP_DEFS[tile->crop].first_yield_day;
        }
        return kg_is_animal_tile(tile);
    }
    if (spec.op == KG_OP_FERTILIZE) {
        return tile->kind == KG_TILE_PLANT
            && unit->inventory[KG_ITEM_FERTILIZER] > 0;
    }
    if (spec.op == KG_OP_BUILD_COOP || spec.op == KG_OP_BUILD_PASTURE) {
        return tile->kind == KG_TILE_EMPTY;
    }
    if (spec.op == KG_OP_DIG) {
        return tile->kind != KG_TILE_EMPTY && tile->kind != KG_TILE_LOCKED
            && !kg_is_animal_tile(tile);
    }
    if (spec.op == KG_OP_PLACE) {
        if (spec.arg >= KG_ITEM_GOOSE && spec.arg <= KG_ITEM_SHEEP) {
            int animal = spec.arg - KG_ITEM_GOOSE;
            return tile->kind == KG_ANIMAL_DEFS[animal].structure
                && tile->animal == KG_ANIMAL_INVALID
                && unit->inventory[spec.arg] > 0;
        }
        return kg_is_shed_adjacent(&pos, game->config.board_size)
            && unit->inventory[spec.arg] > 0
            && game->config.shed_capacity - kg_shed_total(player) > 0;
    }
    if (spec.op == KG_OP_FEED) {
        return kg_is_animal_tile(tile) && !tile->fed_today
            && unit->inventory[KG_ITEM_WHEAT] > 0;
    }
    if (spec.op == KG_OP_COLLECT_FERTILIZER) {
        return kg_is_animal_tile(tile) && tile->fertilizer_available;
    }
    if (spec.op == KG_OP_CARE) {
        return kg_is_animal_tile(tile) && !tile->cared_today;
    }
    return 0;
}

KG_HD static inline int kag_market_action_legal(const KGState* game, const KGPlayer* player,
        KGPolicyMarketSpec spec) {
    int price = 0;
    if (spec.op == KG_MARKET_BUY_SEED) {
        price = KG_CROP_DEFS[spec.item].seed_cost;
        return price > 0 && player->money >= price;
    }
    if (spec.op == KG_MARKET_BUY_PRODUCT) {
        price = game->market.prices[spec.item];
        return price > 0 && player->money >= price;
    }
    if (spec.op == KG_MARKET_BUY_ANIMAL) {
        int animal = spec.item - KG_ITEM_GOOSE;
        if ((unsigned)animal >= KG_NUM_ANIMALS) return 0;
        price = KG_ANIMAL_DEFS[spec.item - KG_ITEM_GOOSE].cost;
        return price > 0 && player->money >= price;
    }
    if (spec.op == KG_MARKET_SELL) {
        return spec.item >= 0 && spec.item < KG_NUM_PRODUCTS;
    }
    if (spec.op == KG_MARKET_HIRE) {
        return player->unit_count < KG_MAX_UNITS
            && player->money >= kg_hire_cost(player->hires_today,
                game->config.farm_hand_cost_mult);
    }
    if (spec.op == KG_MARKET_BUY_LAND) {
        int extra = kag_popcount((unsigned int)player->unlocked_mask) - 1;
        int price = extra == 0 ? 1000 : extra == 1 ? 2000 : 4000;
        return extra >= 0 && extra < 3 && player->money >= price;
    }
    return 0;
}

KG_HD static inline void kag_write_unit_mask(const KGState* game, const KGPlayer* player,
        int unit_id, unsigned char* mask) {
    const KGUnitState* unit;
    const KGTile* tile;
    KGPosition pos;
    int x;
    int y;
    int adjacent;
    if (unit_id < 0 || unit_id >= player->unit_count) {
        mask[KG_U_PASS] = 1;
        return;
    }
    unit = &player->units[unit_id];
    x = unit->x;
    y = unit->y;
    pos = (KGPosition){(uint8_t)x, (uint8_t)y};
    tile = &player->tiles[y * KG_MAX_BOARD_SIZE + x];
    mask[KG_U_PASS] = 1;
    for (int move = 0; move < 4; move++) {
        int dx = move == 2 ? 1 : move == 3 ? -1 : 0;
        int dy = move == 0 ? -1 : move == 1 ? 1 : 0;
        if (kg_unit_can_move_to(player, x + dx, y + dy,
                game->config.board_size)) {
            mask[KG_U_MOVE + move] = 1;
        }
    }
    adjacent = kg_is_shed_adjacent(&pos, game->config.board_size);
    if (adjacent) {
        for (int item = 0; item < KG_NUM_ITEMS; item++) {
            if (player->shed[item] > 0) {
                mask[KG_U_PICKUP + item] = 1;
            }
        }
        if (unit->inventory_order_count > 0) mask[KG_U_DROP] = 1;
    }
    if (tile->kind == KG_TILE_LOCKED) return;
    if (tile->kind == KG_TILE_EMPTY) {
        for (int crop = 0; crop < KG_NUM_CROPS; crop++) {
            if (player->seeds[crop] > 0) mask[KG_U_PLANT + crop] = 1;
        }
        mask[KG_U_SINGLE + 3] = 1; /* BUILD_COOP */
        mask[KG_U_SINGLE + 4] = 1; /* BUILD_PASTURE */
    }
    if (tile->kind == KG_TILE_PLANT) {
        if (!tile->watered_today) mask[KG_U_SINGLE] = 1; /* WATER */
        if (tile->yield_units > 0 && tile->crop >= 0 && tile->crop < KG_NUM_CROPS
                && game->day - tile->planted_day
                    >= KG_CROP_DEFS[tile->crop].first_yield_day) {
            mask[KG_U_SINGLE + 1] = 1; /* HARVEST */
        }
        if (unit->inventory[KG_ITEM_FERTILIZER] > 0) {
            mask[KG_U_SINGLE + 2] = 1; /* FERTILIZE */
        }
    } else if (kg_is_animal_tile(tile)) {
        if (tile->yield_units > 0) mask[KG_U_SINGLE + 1] = 1;
        if (!tile->fed_today && unit->inventory[KG_ITEM_WHEAT] > 0) {
            mask[KG_U_SINGLE + 6] = 1; /* FEED */
        }
        if (tile->fertilizer_available) mask[KG_U_SINGLE + 7] = 1;
        if (!tile->cared_today) mask[KG_U_SINGLE + 8] = 1;
    }
    if (tile->kind != KG_TILE_EMPTY && tile->kind != KG_TILE_LOCKED
            && !kg_is_animal_tile(tile)) {
        mask[KG_U_SINGLE + 5] = 1; /* DIG */
    }
    int shed_room = adjacent
        ? game->config.shed_capacity - kg_shed_total(player) : 0;
    for (int item = 0; item < KG_NUM_ITEMS; item++) {
        int legal = 0;
        if (item >= KG_ITEM_GOOSE && item <= KG_ITEM_SHEEP) {
            int animal = item - KG_ITEM_GOOSE;
            legal = tile->kind == KG_ANIMAL_DEFS[animal].structure
                && tile->animal == KG_ANIMAL_INVALID
                && unit->inventory[item] > 0;
        } else {
            legal = shed_room > 0 && unit->inventory[item] > 0;
        }
        if (legal) {
            mask[KG_U_PLACE + item] = 1;
        }
    }
}

/* Market masks express simulator legality, never a hand-written strategy.
 * Ordered commands may intentionally prepare later commands in the same
 * queue, so structure/feed/inventory "usefulness" is not a mask concern. */
KG_HD static inline void kag_write_market_mask(const KGState* game, const KGPlayer* player,
        unsigned char* mask) {
    for (int id = 0; id < KG_POLICY_MARKET_COMMANDS; id++) {
        KGPolicyMarketSpec spec = kag_market_spec(id);
        mask[id] = (unsigned char)kag_market_action_legal(game, player, spec);
    }
}

KG_HD static inline unsigned char* kag_market_slot_mask(unsigned char* mask, int slot) {
    return mask + KG_POLICY_MARKET_MASK_OFFSET
        + slot * KG_POLICY_MARKET_SLOT_MASK_SIZE;
}

KG_HD static inline int kag_policy_market_slot_limit(const Env* env) {
    return env->policy_market_slots > 0
        ? env->policy_market_slots : KG_POLICY_MARKET_SLOTS;
}

KG_HD static inline int kag_policy_hand_limit(const Env* env) {
    return env->policy_max_hands > 0
        ? env->policy_max_hands : KG_MAX_HANDS;
}

KG_HD static inline void kag_write_market_slots(const Env* env,
        const KGState* game, const KGPlayer* player, unsigned char* mask) {
    unsigned char commands[KG_POLICY_MARKET_COMMANDS];
    kag_write_market_mask(game, player, commands);
    if (player->hand_count >= kag_policy_hand_limit(env)) {
        for (int id = 0; id < KG_POLICY_MARKET_COMMANDS; id++) {
            if (kag_market_spec(id).op == KG_MARKET_HIRE) commands[id] = 0;
        }
    }
    int has_command = 0;
    for (int id = 0; id < KG_POLICY_MARKET_COMMANDS; id++) {
        has_command |= commands[id] != 0;
    }
    for (int slot = 0; slot < KG_POLICY_MARKET_SLOTS; slot++) {
        unsigned char* slot_mask = kag_market_slot_mask(mask, slot);
        slot_mask[0] = 1; /* STOP */
        if (slot >= kag_policy_market_slot_limit(env)) continue;
        slot_mask[1] = (unsigned char)has_command; /* CONTINUE */
        memcpy(slot_mask + KG_POLICY_MARKET_CONTINUE_ACTIONS, commands,
            KG_POLICY_MARKET_COMMANDS);
        memset(slot_mask + KG_POLICY_MARKET_CONTINUE_ACTIONS
                + KG_POLICY_MARKET_COMMANDS, 1,
            KG_POLICY_MARKET_QUANTITIES);
    }
}

KG_HD static inline void kag_write_mask(Env* env, int player_id) {
    Agent* agent = &env->agents[player_id];
    KGState* game = &env->game_storage;
    KGPlayer* player = &game->players[player_id];
    unsigned char* mask = agent->action_mask;
    if (mask == NULL) return;
    memset(mask, 0, KG_POLICY_ACTION_MASK_SIZE);
    kag_write_unit_mask(game, player, 0, mask);
    for (int unit = 1; unit <= KG_POLICY_DIRECT_HANDS; unit++) {
        unsigned char* unit_mask = mask + unit * KG_POLICY_UNIT_COMMANDS;
        if (unit >= player->unit_count) {
            unit_mask[KG_U_PASS] = 1;
            continue;
        }
        kag_write_unit_mask(game, player, unit, unit_mask);
    }
    for (int cohort = 0; cohort < KG_POLICY_OVERFLOW_COHORTS; cohort++) {
        int slot = 1 + KG_POLICY_DIRECT_HANDS + cohort;
        unsigned char* cohort_mask = mask + slot * KG_POLICY_UNIT_COMMANDS;
        cohort_mask[KG_U_PASS] = 1;
        for (int unit = 1 + KG_POLICY_DIRECT_HANDS + cohort;
                unit < player->unit_count; unit += KG_POLICY_OVERFLOW_COHORTS) {
            kag_write_unit_mask(game, player, unit, cohort_mask);
        }
    }
    kag_write_market_slots(env, game, player, mask);
}

/* Strategy injection is deliberately separate from legality masking. This
 * optional ablation forces the learned policy's heads to reproduce the top
 * replay prefix; it never changes the simulator or another policy's masks. */
KG_HD static inline void kag_write_opening_mask(Env* env, int player_id,
        const KGAction* target) {
    Agent* agent = &env->agents[player_id];
    unsigned char* mask = agent->action_mask;
    if (mask == NULL || agent->policy != 0
            || env->opening_turns <= env->game_storage.step) return;

    for (int slot = 0; slot < KG_POLICY_UNITS; slot++) {
        int unit = slot;
        if (slot > KG_POLICY_DIRECT_HANDS) {
            unit = 1 + KG_POLICY_DIRECT_HANDS
                + slot - (1 + KG_POLICY_DIRECT_HANDS);
        }
        KGUnitAction command = {KG_OP_PASS, -1, 1};
        if (unit == 0) command = target->farmer;
        else if (unit <= target->hand_count) command = target->hands[unit - 1];
        int id = kag_unit_action_id(command.op, command.arg, command.n);
        unsigned char* head = mask + slot * KG_POLICY_UNIT_COMMANDS;
        int legal = head[id] != 0;
        memset(head, 0, KG_POLICY_UNIT_COMMANDS);
        head[legal ? id : KG_U_PASS] = 1;
    }

    int stopped = 0;
    for (int slot = 0; slot < KG_POLICY_MARKET_SLOTS; slot++) {
        unsigned char* slot_mask = kag_market_slot_mask(mask, slot);
        unsigned char* commands = slot_mask + KG_POLICY_MARKET_CONTINUE_ACTIONS;
        unsigned char* quantities = commands + KG_POLICY_MARKET_COMMANDS;
        if (!stopped && slot < target->market_count
                && slot < kag_policy_market_slot_limit(env)) {
            const KGMarketOrder* order = &target->market[slot];
            int id = kag_market_action_id(order->op, order->item, order->n);
            int legal = commands[id] != 0;
            memset(slot_mask, 0, KG_POLICY_MARKET_SLOT_MASK_SIZE);
            if (legal) {
                slot_mask[PUFFER_CONDITIONAL_CONTINUE] = 1;
                commands[id] = 1;
                quantities[kag_market_quantity_id(order->n)] = 1;
            } else {
                slot_mask[PUFFER_CONDITIONAL_STOP] = 1;
                commands[0] = 1;
                quantities[0] = 1;
                stopped = 1;
            }
        } else {
            memset(slot_mask, 0, KG_POLICY_MARKET_SLOT_MASK_SIZE);
            slot_mask[PUFFER_CONDITIONAL_STOP] = 1;
            commands[0] = 1;
            quantities[0] = 1;
            stopped = 1;
        }
    }
}

KG_HD static inline KGUnitAction kag_decode_unit_action(const Agent* agent, int slot) {
    KGPolicyUnitSpec spec = {KG_OP_PASS, -1, KG_POLICY_N_ONE};
    KGUnitAction action = {KG_OP_PASS, -1, 1};
    int id;
    id = kag_discrete_index(agent->actions[slot], KG_POLICY_UNIT_COMMANDS);
    spec = kag_unit_spec(id);
    action.op = spec.op;
    action.arg = spec.arg;
    action.n = 0x7fffffff;
    return action;
}

KG_HD static inline void kag_decode_action(KGAction* action, const Agent* agent,
        const KGState* game, int player_id) {
    const KGPlayer* player = &game->players[player_id];
    int hand_count = player->hand_count;
    action->farmer = kag_decode_unit_action(agent, 0);
    action->hand_count = hand_count;
    for (int hand = 0; hand < hand_count; hand++) {
        int unit = hand + 1;
        int slot = unit <= KG_POLICY_DIRECT_HANDS ? unit
            : 1 + KG_POLICY_DIRECT_HANDS
                + (unit - 1 - KG_POLICY_DIRECT_HANDS)
                    % KG_POLICY_OVERFLOW_COHORTS;
        action->hands[hand] = kag_decode_unit_action(agent, slot);
    }
    action->market_count = 0;
    int order_limit = game->config.max_market_orders_per_turn;
    if (order_limit > KG_MAX_MARKET_ORDERS) order_limit = KG_MAX_MARKET_ORDERS;
    if (order_limit > KG_POLICY_MARKET_SLOTS) order_limit = KG_POLICY_MARKET_SLOTS;
    for (int slot = 0; slot < order_limit; slot++) {
        int continue_head = KG_POLICY_MARKET_HEAD_OFFSET + 3 * slot;
        int command_head = continue_head + 1;
        int keep_going = kag_discrete_index(agent->actions[continue_head],
            KG_POLICY_MARKET_CONTINUE_ACTIONS);
        if (keep_going != PUFFER_CONDITIONAL_CONTINUE) break;
        int id = kag_discrete_index(
            agent->actions[command_head], KG_POLICY_MARKET_COMMANDS);
        KGPolicyMarketSpec spec = kag_market_spec(id);
        if (id < KG_POLICY_MARKET_QUANTITY_COMMANDS) {
            int quantity_id = kag_discrete_index(agent->actions[command_head + 1],
                KG_POLICY_MARKET_QUANTITIES);
            spec.n = kag_market_quantity_spec(quantity_id);
        }
        KGMarketOrder* decoded = &action->market[action->market_count++];
        decoded->op = spec.op;
        decoded->item = spec.item;
        decoded->n = kag_market_quantity(game, player, spec);
    }
}

/* Training ablations constrain both learned and scripted sides without
 * changing the parity-tested game core. Truncate before filtering HIRE so a
 * rejected order cannot expose a later queue slot. */
KG_HD static inline void kag_apply_policy_limits(const Env* env,
        const KGPlayer* player, KGAction* action) {
    int input_count = action->market_count;
    int slot_limit = kag_policy_market_slot_limit(env);
    if (input_count > slot_limit) input_count = slot_limit;
    int hand_count = player->hand_count;
    int hand_limit = kag_policy_hand_limit(env);
    int output_count = 0;
    for (int order = 0; order < input_count; order++) {
        KGMarketOrder candidate = action->market[order];
        if (candidate.op == KG_MARKET_HIRE) {
            if (hand_count >= hand_limit) continue;
            hand_count++;
        }
        action->market[output_count++] = candidate;
    }
    action->market_count = output_count;
}

/* Helpers used by the native headed demo and C-only evaluator. */
static inline void kag_clear_policy_actions(Agent* agent) {
    if (agent->actions == NULL) return;
    for (int head = 0; head < NUM_ATNS; head++) agent->actions[head] = 0.0f;
}

static inline void kag_set_policy_unit(Agent* agent, int unit,
        int op, int arg, int n) {
    if (agent->actions == NULL || unit < 0 || unit >= KG_POLICY_UNITS) return;
    agent->actions[unit] = (float)kag_unit_action_id(op, arg, n);
}

static inline void kag_set_policy_market(Agent* agent, int order,
        int op, int item, int n) {
    if (agent->actions == NULL || order < 0
            || order >= KG_POLICY_MARKET_SLOTS) return;
    int continue_head = KG_POLICY_MARKET_HEAD_OFFSET + 3 * order;
    agent->actions[continue_head] = (float)PUFFER_CONDITIONAL_CONTINUE;
    agent->actions[continue_head + 1] =
        (float)kag_market_action_id(op, item, n);
    agent->actions[continue_head + 2] = (float)kag_market_quantity_id(n);
}

KG_HD static void kag_write_all_observations_from_tapes(Env* env,
        const KGScriptTape* tapes) {
    KagFarmSummary summaries[KG_NUM_PLAYERS];
    for (int player = 0; player < KG_NUM_PLAYERS; player++) {
        kag_collect_farm_summary(&env->game_storage,
            &env->game_storage.players[player], &summaries[player]);
    }
    for (int player = 0; player < KG_NUM_PLAYERS; player++) {
        kag_write_observation_with_summaries(env, player, summaries);
        kag_write_mask(env, player);
        if (env->opening_turns > env->game_storage.step
                && env->agents[player].policy == 0) {
            KGAction target;
            kag_script_action_from_tapes(&env->game_storage, player,
                KG_SCRIPT_TOP, &target, tapes);
            kag_script_repair(&env->game_storage, player, KG_SCRIPT_TOP,
                &target);
            kag_write_opening_mask(env, player, &target);
        }
    }
}

static inline void kag_write_all_observations(Env* env) {
    kag_script_init();
    kag_write_all_observations_from_tapes(env, kag_script_tapes);
}

KG_HD static inline uint32_t kag_reset_opening_random(Env* env) {
    uint32_t x = env->reset_opening_rng;
    if (x == 0) x = 0x6d2b79f5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    env->reset_opening_rng = x;
    return x;
}

/* Reset-state sampling executes genuine simultaneous game turns. The explicit
 * mixture probability keeps root starts independent of the sampled prefix
 * range, matching the DAGS start-distribution construction. */
KG_HD static inline void kag_reset_with_opening(Env* env,
        const KGScriptTape* tapes) {
    KGState* game = &env->game_storage;
    kg_reset(game);
    env->reset_source = 0;
    int limit = env->reset_opening_turns;
    if (limit <= 0 || env->reset_opening_prob <= 0.0f) return;
    if (env->reset_opening_prob < 1.0f) {
        uint32_t draw = kag_reset_opening_random(env) >> 8;
        uint32_t cutoff = (uint32_t)(env->reset_opening_prob * 16777216.0f);
        if (draw >= cutoff) return;
    }
    env->reset_source = 1;
    if (limit >= game->config.episode_steps) {
        limit = game->config.episode_steps - 1;
    }
    if (limit >= KG_SCRIPT_FRAMES) limit = KG_SCRIPT_FRAMES - 1;
    int min_prefix = env->reset_opening_min;
    if (min_prefix < 0) min_prefix = 0;
    if (min_prefix > limit) min_prefix = limit;
    int prefix = min_prefix + (int)(kag_reset_opening_random(env)
        % (uint32_t)(limit - min_prefix + 1));
    for (int step = 0; step < prefix; step++) {
        KGAction actions[KG_NUM_PLAYERS];
        for (int player = 0; player < KG_NUM_PLAYERS; player++) {
            kag_script_action_from_tapes(game, player, KG_SCRIPT_TOP,
                &actions[player], tapes);
            kag_script_repair(game, player, KG_SCRIPT_TOP, &actions[player]);
        }
        kg_step(game, actions);
    }
}

static inline KGConfig kag_load_config(Env* env, Dict* kwargs) {
    KGConfig config;
    kg_config_default(&config);
    config.episode_steps = (int)dict_get(kwargs, "episode_steps");
    config.board_size = (int)dict_get(kwargs, "board_size");
    config.starting_money = (int)dict_get(kwargs, "starting_money");
    config.max_market_orders_per_turn = (int)dict_get(kwargs, "max_market_orders_per_turn");
    config.turns_per_day = (int)dict_get(kwargs, "turns_per_day");
    config.shed_capacity = (int)dict_get(kwargs, "shed_capacity");
    config.weed_spawn_chance = dict_get(kwargs, "weed_spawn_chance");
    config.town_shop_unlock_interval = (int)dict_get(kwargs, "town_shop_unlock_interval");
    config.town_shop_sell_interval = (int)dict_get(kwargs, "town_shop_sell_interval");
    config.town_center_sell_interval = (int)dict_get(kwargs, "town_center_sell_interval");
    config.farm_hand_cost_mult = (int)dict_get(kwargs, "farm_hand_cost_mult");
    config.seed = (uint64_t)dict_get(kwargs, "seed");
    if (config.board_size != KG_OBS_BOARD) {
        fprintf(stderr, "kaggriculture board_size must be %d for this adapter\n",
            KG_OBS_BOARD);
        exit(1);
    }
    /* Give parallel envs independent deterministic daily streams. */
    config.seed ^= 0x9e3779b97f4a7c15ULL * ((uint64_t)env->rng + 1ULL);
    return config;
}

KG_HD static inline void kag_starter_action(const KGState* game, int player_id,
        KGAction* action) {
    const KGPlayer* player = &game->players[player_id];
    action->hand_count = 0;
    action->market_count = 0;
    action->farmer = (KGUnitAction){KG_OP_PASS, KG_CROP_INVALID, 1};
    action->hand_count = player->hand_count;
    for (int hand = 0; hand < action->hand_count; hand++) {
        action->hands[hand] = (KGUnitAction){KG_OP_PASS, KG_CROP_INVALID, 1};
    }
    for (int worker = 0; worker < player->unit_count; worker++) {
        const KGUnitState* unit = &player->units[worker];
        const KGTile* tile = &player->tiles[
            unit->y * KG_MAX_BOARD_SIZE + unit->x];
        KGUnitAction* command = worker == 0
            ? &action->farmer : &action->hands[worker - 1];
        if (tile->kind == KG_TILE_PLANT && tile->yield_units > 0
                && game->day - tile->planted_day
                    >= KG_CROP_DEFS[tile->crop].first_yield_day) {
            command->op = KG_OP_HARVEST;
        } else if (tile->kind == KG_TILE_PLANT && !tile->watered_today) {
            command->op = KG_OP_WATER;
        } else if (tile->kind == KG_TILE_EMPTY
                && player->seeds[KG_WHEAT] > 0) {
            command->op = KG_OP_PLANT;
            command->arg = KG_WHEAT;
        }
    }
    if (player->seeds[KG_WHEAT] == 0 && player->money >= 10) {
        action->market[action->market_count++] =
            (KGMarketOrder){KG_MARKET_BUY_SEED, KG_WHEAT, 1};
    }
    if (player->shed[KG_ITEM_WHEAT] > 0) {
        action->market[action->market_count++] =
            (KGMarketOrder){KG_MARKET_SELL, KG_ITEM_WHEAT,
                player->shed[KG_ITEM_WHEAT]};
    }
}

enum {
    KAG_BOT_NONE = 0,
    KAG_BOT_PASS,
    KAG_BOT_STARTER,
    KAG_BOT_MIXED,
    KAG_BOT_CROP_BASE,
    KAG_BOT_SCRIPT_BASE = KAG_BOT_CROP_BASE + KG_NUM_CROPS,
    KAG_BOT_ADAPTIVE_BASE = KAG_BOT_SCRIPT_BASE + KG_SCRIPT_COUNT,
};

enum {
    KAG_ADAPTIVE_FIELDS = 0,
    KAG_ADAPTIVE_SCENARIO,
    KAG_ADAPTIVE_SOIL,
    KAG_ADAPTIVE_KAITO,
    KAG_ADAPTIVE_SHIELD,
    KAG_ADAPTIVE_FRONTIER12,
    KAG_ADAPTIVE_HARVEST_PULSE,
    KAG_ADAPTIVE_STRUCTURED,
    KAG_ADAPTIVE_TRIAD,
    KAG_ADAPTIVE_THUNDER,
    KAG_ADAPTIVE_COUNT,
};

typedef struct {
    uint8_t x;
    uint8_t y;
    uint8_t priority;
    uint8_t op;
    int8_t arg;
} KagBotJob;

KG_HD static inline void kag_bot_crop_rank(const KGState* game, int player_id,
        int rank[KG_NUM_CROPS]) {
    int own[KG_NUM_CROPS] = {0};
    int opponent[KG_NUM_CROPS] = {0};
    int demand[KG_NUM_CROPS] = {1, 1, 1, 1, 1};
    for (int player = 0; player < KG_NUM_PLAYERS; player++) {
        const KGPlayer* farm = &game->players[player];
        for (int tile = 0; tile < KG_MAX_TILES; tile++) {
            if (farm->tiles[tile].kind != KG_TILE_PLANT) continue;
            int crop = farm->tiles[tile].crop;
            if (player == player_id) own[crop]++;
            else opponent[crop]++;
        }
    }
    for (int active = 0; active < game->shop_count; active++) {
        int shop = game->unlocked_shops[active];
        int products = 0;
        while (products < KG_NUM_PRODUCTS
                && kg_shop_product(shop, products) >= 0) products++;
        int multiplier = products == 1 ? 2 : 1;
        for (int item = 0; item < products; item++) {
            int product = kg_shop_product(shop, item);
            if (product < KG_NUM_CROPS) demand[product] += multiplier;
        }
    }

    float scores[KG_NUM_CROPS];
    int remaining = 30 - game->day;
    for (int crop = 0; crop < KG_NUM_CROPS; crop++) {
        const KGCropDef* def = &KG_CROP_DEFS[crop];
        int events = 0;
        if (remaining > def->first_yield_day) {
            events = def->ongoing
                ? 1 + (remaining - def->first_yield_day - 1) / def->interval
                : def->max_yield;
            if (events > def->max_yield) events = def->max_yield;
        }
        float gross = (float)events * game->market.prices[crop]
            - def->seed_cost;
        if (gross < 1.0f) gross = 1.0f;
        scores[crop] = gross * (1.0f + 0.12f * demand[crop])
            / (1.0f + 0.12f * own[crop] + 0.06f * opponent[crop]);
        rank[crop] = crop;
    }
    for (int i = 1; i < KG_NUM_CROPS; i++) {
        int crop = rank[i];
        int j = i - 1;
        while (j >= 0 && scores[rank[j]] < scores[crop]) {
            rank[j + 1] = rank[j];
            j--;
        }
        rank[j + 1] = crop;
    }
}

KG_HD static inline int kag_bot_crop(int fixed_crop, int slot,
        const int rank[KG_NUM_CROPS]) {
    if (fixed_crop >= 0) return fixed_crop;
    int bucket = slot % 10;
    if (slot % 8 == 0) return KG_WHEAT;
    return bucket < 5 ? rank[0] : bucket < 8 ? rank[1] : rank[2];
}

KG_HD static inline int kag_bot_route(const KGPlayer* farm, const KGUnitState* unit,
        int tx, int ty) {
    int quadrant = kg_quadrant(unit->x, unit->y, KG_OBS_BOARD);
    if (!(farm->unlocked_mask & quadrant)) {
        if (unit->x >= 5 && unit->y >= 5) {
            if (farm->unlocked_mask & 2) return KG_OP_NORTH;
            if (farm->unlocked_mask & 4) return KG_OP_WEST;
            return KG_OP_PASS;
        }
        if (unit->x >= 5) return KG_OP_WEST;
        if (unit->y >= 5) return KG_OP_NORTH;
    }
    if (unit->x != tx) return unit->x < tx ? KG_OP_EAST : KG_OP_WEST;
    if (unit->y != ty) return unit->y < ty ? KG_OP_SOUTH : KG_OP_NORTH;
    return KG_OP_PASS;
}

KG_HD static inline int kag_bot_jobs(const KGState* game, int player_id,
        const KGPlayer* farm, int fixed_crop, KagBotJob jobs[KG_MAX_TILES],
        int seed_need[KG_NUM_CROPS]) {
    int seed_budget[KG_NUM_CROPS];
    memcpy(seed_budget, farm->seeds, sizeof(seed_budget));
    memset(seed_need, 0, KG_NUM_CROPS * sizeof(*seed_need));
    int crop_rank[KG_NUM_CROPS];
    kag_bot_crop_rank(game, player_id, crop_rank);
    int count = 0;
    /* The exported top continuation prioritizes every occupied animal tile
     * before crop work. Keep this as a separate row-major pass so job order,
     * worker tie-breaking, and therefore the entire trajectory match the
     * Python oracle exactly. One animal job replaces that tile's crop job, so
     * the fixed KG_MAX_TILES capacity remains sufficient. */
    for (int tile_id = 0; tile_id < KG_MAX_TILES; tile_id++) {
        const KGTile* tile = &farm->tiles[tile_id];
        if (!kg_is_animal_tile(tile)) continue;
        uint8_t x = (uint8_t)(tile_id % KG_MAX_BOARD_SIZE);
        uint8_t y = (uint8_t)(tile_id / KG_MAX_BOARD_SIZE);
        /* Feeding is the only deadline: two missed day refreshes destroy the
         * animal. Harvest, fertilizer, and care can safely wait behind it. */
        if (!tile->fed_today) {
            jobs[count++] = (KagBotJob){x, y, 0, KG_OP_FEED, -1};
        } else if (tile->yield_units > 0) {
            jobs[count++] = (KagBotJob){x, y, 1, KG_OP_HARVEST, -1};
        } else if (tile->fertilizer_available) {
            jobs[count++] = (KagBotJob){x, y, 1,
                KG_OP_COLLECT_FERTILIZER, -1};
        } else if (!tile->cared_today) {
            jobs[count++] = (KagBotJob){x, y, 2, KG_OP_CARE, -1};
        }
    }
    int slot = 0;
    for (int tile_id = 0; tile_id < KG_MAX_TILES; tile_id++) {
        const KGTile* tile = &farm->tiles[tile_id];
        if (tile->kind == KG_TILE_LOCKED) continue;
        int crop_slot = slot++;
        int specialist_cap = fixed_crop == KG_WHEAT ? 25
            : fixed_crop == KG_CARROT ? 6
            : fixed_crop == KG_MELON ? 10 : 16;
        if (fixed_crop >= 0 && crop_slot >= specialist_cap) {
            continue;
        }
        uint8_t x = (uint8_t)(tile_id % KG_MAX_BOARD_SIZE);
        uint8_t y = (uint8_t)(tile_id / KG_MAX_BOARD_SIZE);
        int crop = kag_bot_crop(fixed_crop, crop_slot, crop_rank);
        if (tile->kind == KG_TILE_PLANT) {
            int age = game->day - tile->planted_day;
            const KGCropDef* def = &KG_CROP_DEFS[tile->crop];
            int bonus_window = !def->ongoing
                && age >= (def->max_yield_day + 1) / 2
                && age <= def->max_yield_day;
            int must_water = tile->consecutive_unwatered > 0
                || bonus_window || def->ongoing;
            if (!tile->watered_today && must_water) {
                jobs[count++] = (KagBotJob){x, y,
                    (uint8_t)(tile->consecutive_unwatered ? 0 : 2),
                    KG_OP_WATER, -1};
            } else if (tile->yield_units > 0 && age >= def->first_yield_day) {
                jobs[count++] = (KagBotJob){x, y, 1, KG_OP_HARVEST, -1};
            }
        } else if (tile->kind == KG_TILE_WEED) {
            jobs[count++] = (KagBotJob){x, y, 3, KG_OP_DIG, -1};
        } else if (tile->kind == KG_TILE_EMPTY) {
            int viable = game->market.prices[crop]
                > KG_CROP_DEFS[crop].seed_cost;
            if (viable) seed_need[crop]++;
            if (viable && seed_budget[crop] > 0
                    && game->day + KG_CROP_DEFS[crop].first_yield_day < 30) {
                jobs[count++] = (KagBotJob){x, y, 4, KG_OP_PLANT,
                    (int8_t)crop};
                seed_budget[crop]--;
            }
        }
    }
    return count;
}

KG_HD static inline void kag_bot_action(const KGState* game, int player_id,
        int fixed_crop, KGAction* action) {
    const KGPlayer* farm = &game->players[player_id];
    action->hand_count = 0;
    action->market_count = 0;
    action->farmer = (KGUnitAction){KG_OP_PASS, -1, 1};
    action->hand_count = farm->hand_count;
    for (int hand = 0; hand < action->hand_count; hand++) {
        action->hands[hand] = (KGUnitAction){KG_OP_PASS, -1, 1};
    }

    KagBotJob jobs[KG_MAX_TILES];
    int seed_need[KG_NUM_CROPS];
    int job_count = kag_bot_jobs(game, player_id, farm, fixed_crop,
        jobs, seed_need);
    int unfed_animals = 0;
    for (int tile = 0; tile < KG_MAX_TILES; tile++) {
        if (kg_is_animal_tile(&farm->tiles[tile])
                && !farm->tiles[tile].fed_today) unfed_animals++;
    }
    int wheat_pickup_assigned = 0;
    uint64_t claimed[KG_TILE_WORDS] = {0};
    for (int worker = 0; worker < farm->unit_count; worker++) {
        const KGUnitState* unit = &farm->units[worker];
        KGUnitAction* command = worker == 0
            ? &action->farmer : &action->hands[worker - 1];
        /* Daily inventory is dropped at the shed. Reacquire feed before
         * routing to animals; otherwise FEED is an endless silent no-op. */
        if (unfed_animals > 0 && unit->inventory[KG_ITEM_WHEAT] == 0
                && !wheat_pickup_assigned && farm->shed[KG_ITEM_WHEAT] > 0) {
            KGPosition pos = {unit->x, unit->y};
            if (kg_is_shed_adjacent(&pos, game->config.board_size)) {
                int n = farm->shed[KG_ITEM_WHEAT] < unfed_animals
                    ? farm->shed[KG_ITEM_WHEAT] : unfed_animals;
                *command = (KGUnitAction){KG_OP_PICKUP, KG_ITEM_WHEAT, n};
            } else {
                *command = (KGUnitAction){
                    kag_bot_route(farm, unit, 4, 4), -1, 1};
            }
            wheat_pickup_assigned = 1;
            continue;
        }
        int best = -1;
        int best_score = 0x7fffffff;
        for (int job = 0; job < job_count; job++) {
            if (claimed[job >> 6] & (1ULL << (job & 63))) continue;
            int distance = kag_abs((int)unit->x - jobs[job].x)
                + kag_abs((int)unit->y - jobs[job].y);
            int score = jobs[job].priority * 32 + distance;
            if (score < best_score) {
                best = job;
                best_score = score;
            }
        }
        if (best < 0) continue;
        claimed[best >> 6] |= 1ULL << (best & 63);
        const KagBotJob* job = &jobs[best];
        if (unit->x == job->x && unit->y == job->y) {
            *command = (KGUnitAction){job->op, job->arg, 1};
        } else {
            *command = (KGUnitAction){
                kag_bot_route(farm, unit, job->x, job->y), -1, 1};
        }
    }

    int limit = game->config.max_market_orders_per_turn;
    for (int product = 0; product < KG_NUM_PRODUCTS
            && action->market_count < limit; product++) {
        int sell_count = farm->shed[product];
        if (product == KG_ITEM_WHEAT && unfed_animals > 0) {
            sell_count -= unfed_animals;
        }
        if (sell_count > 0) {
            action->market[action->market_count++] =
                (KGMarketOrder){KG_MARKET_SELL, product, sell_count};
        }
    }
    if (game->day >= 28) return;
    int land = kag_popcount((unsigned)farm->unlocked_mask);
    int carried_wheat = 0;
    for (int unit = 0; unit < farm->unit_count; unit++) {
        carried_wheat += farm->units[unit].inventory[KG_ITEM_WHEAT];
    }
    int missing_feed = unfed_animals
        - farm->shed[KG_ITEM_WHEAT] - carried_wheat;
    if (missing_feed > 0 && action->market_count < limit) {
        action->market[action->market_count++] =
            (KGMarketOrder){KG_MARKET_BUY_PRODUCT, KG_ITEM_WHEAT,
                missing_feed};
    }
    if (fixed_crop < 0 && (game->day == 4 || game->day == 9) && land < 3
            && farm->money >= (land == 1 ? 1500 : 3000)
            && action->market_count < limit) {
        action->market[action->market_count++] =
            (KGMarketOrder){KG_MARKET_BUY_LAND, -1, 1};
    }
    for (int crop = 0; crop < KG_NUM_CROPS
            && action->market_count < limit; crop++) {
        int missing = seed_need[crop] - farm->seeds[crop];
        if (missing > 0) {
            action->market[action->market_count++] =
                (KGMarketOrder){KG_MARKET_BUY_SEED, crop, missing};
        }
    }
    int desired_hands = land == 1 ? 4 : land == 2 ? 8 : 12;
    for (int hand = farm->hand_count; hand < desired_hands
            && action->market_count < limit; hand++) {
        action->market[action->market_count++] =
            (KGMarketOrder){KG_MARKET_HIRE, -1, 1};
    }
}

KG_HD static inline void kag_adaptive_market_add(KGAction* action, int limit,
        KGMarketOrder order) {
    if (action->market_count >= limit) return;
    action->market[action->market_count++] = order;
}

KG_HD static inline void kag_adaptive_filter_market(KGAction* action, int keep_land,
        int keep_hire) {
    int kept = 0;
    for (int order = 0; order < action->market_count; order++) {
        KGMarketOrder candidate = action->market[order];
        if ((!keep_land && candidate.op == KG_MARKET_BUY_LAND)
                || (!keep_hire && candidate.op == KG_MARKET_HIRE)) {
            continue;
        }
        action->market[kept++] = candidate;
    }
    action->market_count = kept;
}

#include "kaggriculture_public_bots.h"

/*
 * Native equivalents for the adaptive notebook agents. They share the fast
 * crop/economy planner above, then add the behaviors that distinguish the
 * public variants: livestock maintenance, pasture placement, conservative
 * soil recovery, and a no-risk liquidation shield. These are state-driven
 * policies rather than replayed Python code.
 */
KG_HD static inline void kag_adaptive_action(const KGState* game, int player_id,
        int profile, KGAction* action) {
    if (profile == KAG_ADAPTIVE_THUNDER) {
        kag_thunder_action(game, player_id, action);
        return;
    }
    if (profile >= KAG_ADAPTIVE_HARVEST_PULSE
            && profile < KAG_ADAPTIVE_COUNT) {
        kag_public_action(game, player_id, profile, action);
        return;
    }
    int fixed_crop = profile == KAG_ADAPTIVE_SOIL ? KG_WHEAT : -1;
    kag_bot_action(game, player_id, fixed_crop, action);
    const KGPlayer* farm = &game->players[player_id];
    int market_limit = game->config.max_market_orders_per_turn;
    if (market_limit > KG_MAX_MARKET_ORDERS) market_limit = KG_MAX_MARKET_ORDERS;
    int animals = 0;
    int empty_coop = 0;
    int empty_pasture = 0;
    int structures = 0;
    for (int tile_id = 0; tile_id < KG_MAX_TILES; tile_id++) {
        const KGTile* tile = &farm->tiles[tile_id];
        if (tile->kind == KG_TILE_COOP) {
            structures++;
            if (tile->animal == KG_ANIMAL_INVALID) empty_coop++;
            else animals++;
        } else if (tile->kind == KG_TILE_PASTURE) {
            structures++;
            if (tile->animal == KG_ANIMAL_INVALID) empty_pasture++;
            else animals++;
        }
    }

    int built = 0;
    int structure_target = profile == KAG_ADAPTIVE_KAITO ? 3
        : profile == KAG_ADAPTIVE_FIELDS ? 2 : 0;
    for (int unit = 0; unit < farm->unit_count; unit++) {
        const KGUnitState* unit_state = &farm->units[unit];
        const KGTile* tile = &farm->tiles[kg_tile_index(
            unit_state->x, unit_state->y)];
        KGUnitAction* command = kag_script_unit_action(action, unit);
        KGPosition position = {unit_state->x, unit_state->y};
        if (tile->kind == KG_TILE_WEED) {
            *command = (KGUnitAction){KG_OP_DIG, -1, 1};
        } else if (tile->kind == KG_TILE_PLANT && !tile->watered_today) {
            *command = (KGUnitAction){KG_OP_WATER, -1, 1};
        } else if (kg_is_animal_tile(tile)) {
            if (!tile->fed_today
                    && unit_state->inventory[KG_ITEM_WHEAT] > 0) {
                *command = (KGUnitAction){KG_OP_FEED, -1, 1};
            } else if (tile->yield_units > 0) {
                *command = (KGUnitAction){KG_OP_HARVEST, -1, 1};
            } else if (tile->fertilizer_available) {
                *command = (KGUnitAction){KG_OP_COLLECT_FERTILIZER, -1, 1};
            } else if (tile->fed_today && !tile->cared_today) {
                *command = (KGUnitAction){KG_OP_CARE, -1, 1};
            }
        } else if ((profile == KAG_ADAPTIVE_KAITO
                    || profile == KAG_ADAPTIVE_FIELDS)
                && tile->kind == KG_TILE_EMPTY && !built
                && structures < structure_target) {
            *command = (KGUnitAction){KG_OP_BUILD_PASTURE, -1, 1};
            built = 1;
        } else if ((profile == KAG_ADAPTIVE_KAITO
                    || profile == KAG_ADAPTIVE_FIELDS)
                && (tile->kind == KG_TILE_COOP
                    || tile->kind == KG_TILE_PASTURE)
                && tile->animal == KG_ANIMAL_INVALID) {
            int item = tile->kind == KG_TILE_COOP ? KG_ITEM_GOOSE : KG_ITEM_COW;
            if (unit_state->inventory[item] > 0) {
                *command = (KGUnitAction){KG_OP_PLACE, item, 1};
            } else if (kg_is_shed_adjacent(&position, game->config.board_size)
                    && farm->shed[item] > 0) {
                *command = (KGUnitAction){KG_OP_PICKUP, item, 1};
            }
        }
    }

    if (profile == KAG_ADAPTIVE_SHIELD) {
        kag_adaptive_filter_market(action, 0, 0);
    }
    if (profile == KAG_ADAPTIVE_SOIL) {
        kag_adaptive_filter_market(action, 0, 1);
    }

    if (profile == KAG_ADAPTIVE_KAITO || profile == KAG_ADAPTIVE_FIELDS) {
        int target = profile == KAG_ADAPTIVE_KAITO ? 5 : 3;
        int cows = farm->shed[KG_ITEM_COW];
        int sheep = farm->shed[KG_ITEM_SHEEP];
        if (animals + cows + sheep < target && empty_pasture > 0
                && farm->money >= 900) {
            int item = KG_ITEM_COW;
            int cost = item == KG_ITEM_COW ? 400 : 500;
            if (farm->money >= cost + 250) {
                kag_adaptive_market_add(action, market_limit,
                    (KGMarketOrder){KG_MARKET_BUY_ANIMAL, item, 1});
            }
        }
        if (animals > 0 || cows > 0 || sheep > 0) {
            kag_adaptive_market_add(action, market_limit,
                (KGMarketOrder){KG_MARKET_BUY_PRODUCT, KG_ITEM_WHEAT,
                    animals + cows + sheep + 2});
        }
    }
    if (profile == KAG_ADAPTIVE_SCENARIO
            && game->day >= 8 && game->day <= 18
            && kag_popcount((unsigned)farm->unlocked_mask) < 3
            && farm->money >= 2500) {
        kag_adaptive_market_add(action, market_limit,
            (KGMarketOrder){KG_MARKET_BUY_LAND, -1, 1});
    }
    (void)empty_coop;
    (void)structures;
}

KG_HD static inline float kag_productive_action_credit(const KGState* game,
        const KGPlayer* farm, const KGAction* action) {
    float credit = 0.0f;
    const KGUnitAction* unit_action = &action->farmer;
    for (int unit = 0; unit < farm->unit_count; unit++) {
        if (unit > 0) unit_action = &action->hands[unit - 1];
        KGPolicyUnitSpec spec = {
            unit_action->op, unit_action->arg, unit_action->n,
        };
        if (!kag_unit_action_legal(game, farm, unit, spec)) continue;
        switch (spec.op) {
            case KG_OP_PICKUP:
            case KG_OP_DROP:
                credit += 0.10f;
                break;
            case KG_OP_PLANT:
            case KG_OP_BUILD_COOP:
            case KG_OP_BUILD_PASTURE:
                credit += 1.0f;
                break;
            case KG_OP_WATER:
            case KG_OP_HARVEST:
            case KG_OP_FERTILIZE:
            case KG_OP_FEED:
            case KG_OP_COLLECT_FERTILIZER:
            case KG_OP_CARE:
                credit += 1.0f;
                break;
            case KG_OP_PLACE:
            case KG_OP_DIG:
                credit += 1.0f;
                break;
            default:
                break;
        }
    }
    for (int order = 0; order < action->market_count; order++) {
        if (action->market[order].op == KG_MARKET_SELL) credit += 1.0f;
    }
    return credit;
}

/* A hinge opportunity is driven only by randomized town/shop demand. It is
 * present once the no-production inventory deficit crosses the product's
 * hinge throughput, where the 1.32.7 price curve begins accelerating. */
KG_HD static inline void kag_log_hinge_opportunity(const KGState* game,
        int player, int item, float* fraction, float* no_production_price,
        float* response, float* opportunity_production,
        float* nonopportunity_production, float* opportunity_sold_units,
        float* opportunity_sales_revenue, float* opportunity_sale_price) {
    const KGMarketDef* def = &KG_MARKET_DEFS[item];
    float produced = (float)game->production_product_units[player][item];
    float sold = (float)game->sold_product_units[player][item];
    float revenue = game->sold_product_revenue[player][item];
    int demand = (int)game->exogenous_demand_units[item];
    int opportunity = demand > def->throughput;
    if (!opportunity) {
        *nonopportunity_production += produced;
        return;
    }
    *fraction += 1.0f;
    *no_production_price += (float)kg_market_price(item, def->i0 - demand);
    *response += produced > 0.0f ? 1.0f : 0.0f;
    *opportunity_production += produced;
    *opportunity_sold_units += sold;
    *opportunity_sales_revenue += revenue;
    if (sold > 0.0f) *opportunity_sale_price += revenue / sold;
}

KG_HD static inline void kag_log_actions(Env* env, const KGState* game,
        const KGAction* action) {
    int player = env->bot_first ? 1 : 0;
    const KGPlayer* farm = &game->players[player];
    const KGAction* player_action = &action[player];
    env->log.market_orders += (float)player_action->market_count;
    for (int order = 0; order < player_action->market_count; order++) {
        int op = player_action->market[order].op;
        if (op == KG_MARKET_BUY_SEED) {
            env->log.buy_orders += 1.0f;
            env->log.seed_buy_orders += 1.0f;
        } else if (op == KG_MARKET_BUY_PRODUCT) {
            env->log.buy_orders += 1.0f;
            env->log.product_buy_orders += 1.0f;
        } else if (op == KG_MARKET_BUY_ANIMAL) {
            env->log.buy_orders += 1.0f;
            env->log.animal_buy_orders += 1.0f;
        } else if (op == KG_MARKET_SELL) {
            env->log.sell_orders += 1.0f;
        } else if (op == KG_MARKET_HIRE) {
            env->log.hire_orders += 1.0f;
        } else if (op == KG_MARKET_BUY_LAND) {
            env->log.land_orders += 1.0f;
        }
    }
    for (int unit = 0; unit < farm->unit_count; unit++) {
        const KGUnitAction* command = unit == 0
            ? &player_action->farmer : &player_action->hands[unit - 1];
        if (command->op == KG_OP_PLACE
                && command->arg >= KG_ITEM_GOOSE
                && command->arg <= KG_ITEM_SHEEP) {
            env->log.animal_place_actions += 1.0f;
        } else if (command->op == KG_OP_FEED) {
            env->log.animal_feed_actions += 1.0f;
        } else if (command->op == KG_OP_CARE) {
            env->log.animal_care_actions += 1.0f;
        } else if (command->op == KG_OP_HARVEST) {
            const KGTile* tile = &farm->tiles[kg_tile_index(
                farm->units[unit].x, farm->units[unit].y)];
            if (kg_is_animal_tile(tile)) {
                env->log.animal_harvest_actions += 1.0f;
            }
        } else if (command->op == KG_OP_COLLECT_FERTILIZER) {
            env->log.fertilizer_collect_actions += 1.0f;
        }
    }
}

void puf_init(Env* env, Dict* kwargs) {
    KGConfig config = kag_load_config(env, kwargs);
    kag_script_init();
    env->num_agents = KG_NUM_PLAYERS;
    env->tag = 0;
    env->boundary_reached = 0;
    env->render_enabled = 0;
    env->render_selected_unit = 0;
    env->policy_market_slots = (int)dict_get(kwargs, "policy_market_slots");
    env->policy_max_hands = (int)dict_get(kwargs, "policy_max_hands");
    env->opening_turns = (int)dict_get(kwargs, "opening_turns");
    env->reset_opening_turns = (int)dict_get(kwargs,
        "reset_opening_turns");
    env->reset_opening_min = (int)dict_get(kwargs, "reset_opening_min");
    env->reset_opening_prob = (float)dict_get(kwargs,
        "reset_opening_prob");
    if (env->policy_market_slots < 1
            || env->policy_market_slots > KG_POLICY_MARKET_SLOTS) {
        fprintf(stderr, "policy_market_slots must be in [1, %d]\n",
            KG_POLICY_MARKET_SLOTS);
        exit(1);
    }
    if (env->policy_max_hands < 1 || env->policy_max_hands > KG_MAX_HANDS) {
        fprintf(stderr, "policy_max_hands must be in [1, %d]\n", KG_MAX_HANDS);
        exit(1);
    }
    if (env->opening_turns < 0 || env->opening_turns >= KG_SCRIPT_FRAMES
            || env->reset_opening_turns < 0
            || env->reset_opening_turns >= KG_SCRIPT_FRAMES) {
        fprintf(stderr,
            "opening_turns and reset_opening_turns must be in [0, %d]\n",
            KG_SCRIPT_FRAMES - 1);
        exit(1);
    }
    if (env->reset_opening_prob < 0.0f
            || env->reset_opening_prob > 1.0f) {
        fprintf(stderr, "reset_opening_prob must be in [0, 1]\n");
        exit(1);
    }
    env->episode_returns[0] = 0.0f;
    env->episode_returns[1] = 0.0f;
    env->reward_potential_scale = (float)dict_get(kwargs, "reward_potential_scale");
    env->reward_potential_gamma = (float)dict_get(
        kwargs, "reward_potential_gamma");
    env->reward_money_scale = (float)dict_get(kwargs, "reward_money_scale");
    env->reward_win = (float)dict_get(kwargs, "reward_win");
    env->reward_seed_value = (float)dict_get(kwargs, "reward_seed_value");
    env->reward_product_value = (float)dict_get(kwargs, "reward_product_value");
    env->reward_crop_value = (float)dict_get(kwargs, "reward_crop_value");
    env->reward_animal_value = (float)dict_get(kwargs, "reward_animal_value");
    env->reward_land_value = (float)dict_get(kwargs, "reward_land_value");
    env->reward_margin_scale = (float)dict_get(kwargs,
        "reward_margin_scale");
    env->reward_differential_scale = (float)dict_get(kwargs,
        "reward_differential_scale");
    env->reward_inactivity_threshold = (float)dict_get(kwargs,
        "reward_inactivity_threshold");
    env->reward_neglect_discount = (float)dict_get(
        kwargs, "reward_neglect_discount");
    env->reward_liquidation_days = (float)dict_get(
        kwargs, "reward_liquidation_days");
    env->reward_productive_action = (float)dict_get(
        kwargs, "reward_productive_action");
    env->reward_inactivity = (float)dict_get(kwargs, "reward_inactivity");
    env->reward_neglect_death = (float)dict_get(
        kwargs, "reward_neglect_death");
    float bot_fraction = (float)dict_get(kwargs, "bot_opponent_fraction");
    float pass_fraction = (float)dict_get(kwargs, "bot_pass_fraction");
    float rules_fraction = (float)dict_get(kwargs, "bot_rules_fraction");
    env->bot_first = (int)dict_get(kwargs, "bot_first") != 0;
    env->bot_top_fraction = (float)dict_get(kwargs, "bot_top_fraction");
    env->bot_script_fraction = (float)dict_get(kwargs, "bot_script_fraction");
    env->bot_opponent_fraction = bot_fraction;
    env->bot_adaptive_fraction = (float)dict_get(kwargs,
        "bot_adaptive_fraction");
    uint32_t opponent_hash = 0x9e3779b9u * (env->rng + 1u);
    env->bot_opponent = KAG_BOT_NONE;
    if ((opponent_hash >> 8)
            < (uint32_t)(bot_fraction * 16777216.0f)) {
        if (pass_fraction >= 1.0f) {
            env->bot_opponent = KAG_BOT_PASS;
        } else {
        uint32_t type_hash = opponent_hash * 0x85ebca6bu + 0xc2b2ae35u;
        uint32_t type_value = type_hash >> 8;
        float bot_remainder = 1.0f - env->bot_top_fraction;
        uint32_t top_cut = (uint32_t)(env->bot_top_fraction
            * 16777216.0f);
        uint32_t script_cut = top_cut
            + (uint32_t)(bot_remainder * env->bot_script_fraction
                * 16777216.0f);
        uint32_t rules_cut = script_cut
            + (uint32_t)(bot_remainder
                * (1.0f - env->bot_script_fraction)
                * env->bot_adaptive_fraction * 16777216.0f);
        uint32_t specialist_cut = rules_cut
            + (uint32_t)(bot_remainder
                * (1.0f - env->bot_script_fraction)
                * (1.0f - env->bot_adaptive_fraction)
                * rules_fraction * 16777216.0f);
        if (type_value < top_cut) {
            env->bot_opponent = KAG_BOT_SCRIPT_BASE + KG_SCRIPT_TOP;
        } else if (type_value < script_cut) {
            /* Every non-top tape, including lugovoy and thunder25. */
            env->bot_opponent = KAG_BOT_SCRIPT_BASE
                + (int)(type_hash % (KG_SCRIPT_COUNT - 1));
        } else if (type_value < rules_cut) {
            /* The mature planners: pulse, structured, triad, thunder. */
            env->bot_opponent = KAG_BOT_ADAPTIVE_BASE
                + KAG_ADAPTIVE_HARVEST_PULSE
                + (int)(type_hash
                    % (KAG_ADAPTIVE_COUNT - KAG_ADAPTIVE_HARVEST_PULSE));
        } else if (type_value < specialist_cut) {
            env->bot_opponent = KAG_BOT_MIXED;
        } else {
            int specialist = (int)(type_hash % (KG_NUM_CROPS + 1));
            env->bot_opponent = specialist == 0 ? KAG_BOT_STARTER
                : KAG_BOT_CROP_BASE + specialist - 1;
        }
        }
    }
    memset(&env->log, 0, sizeof(env->log));
    kg_init(&env->game_storage, &config);
    env->reset_opening_rng = (uint32_t)config.seed ^ 0xa511e9b3u;
    for (int player = 0; player < KG_NUM_PLAYERS; player++) {
        env->agents[player].policy = player == 0 ? 0 : 1;
        env->agents[player].action_mask = NULL;
    }
}

void puf_reset(Env* env) {
    kag_reset_with_opening(env, kag_script_tapes);
    for (int player = 0; player < KG_NUM_PLAYERS; player++) {
        env->agents[player].rewards[0] = 0.0f;
        env->agents[player].terminals[0] = 0.0f;
        env->episode_returns[player] = 0.0f;
        env->potential[player] = kag_player_potential(env, player);
    }
    kag_write_all_observations(env);
}

void puf_step(Env* env) {
    KGAction actions[KG_NUM_PLAYERS];
    KGState* game = &env->game_storage;
    float before_potential[KG_NUM_PLAYERS] = {
        env->potential[0], env->potential[1],
    };
    float productive_credit[KG_NUM_PLAYERS] = {0.0f, 0.0f};
    uint32_t before_neglect_deaths[KG_NUM_PLAYERS] = {
        game->neglect_deaths[0], game->neglect_deaths[1],
    };

    for (int player = 0; player < KG_NUM_PLAYERS; player++) {
        kag_decode_action(&actions[player], &env->agents[player], game, player);
        env->agents[player].terminals[0] = 0.0f;
    }
    for (int player = 0; player < KG_NUM_PLAYERS; player++) {
        int bot = env->demo_bots[player];
        if (bot == KAG_BOT_NONE && player == (env->bot_first ? 0 : 1)
                && (env->tag > 0 || env->bot_opponent_fraction >= 1.0f)) {
            bot = env->bot_opponent;
        }
        if (bot == KAG_BOT_PASS) {
            actions[player].farmer = (KGUnitAction){KG_OP_PASS, -1, 1};
            actions[player].hand_count = game->players[player].hand_count;
            for (int hand = 0; hand < actions[player].hand_count; hand++) {
                actions[player].hands[hand] =
                    (KGUnitAction){KG_OP_PASS, -1, 1};
            }
            actions[player].market_count = 0;
        } else if (bot == KAG_BOT_STARTER) {
            kag_starter_action(game, player, &actions[player]);
        } else if (bot >= KAG_BOT_SCRIPT_BASE
                && bot < KAG_BOT_SCRIPT_BASE + KG_SCRIPT_COUNT) {
            int profile = bot - KAG_BOT_SCRIPT_BASE;
            kag_script_action(game, player, profile, &actions[player]);
            kag_script_repair(game, player, profile, &actions[player]);
            if (profile == KG_SCRIPT_TOP && game->step >= 26) {
                /* After the opening, delegate to the strong rules economy so
                 * the tape does not drift on routing/market. The opening and
                 * maturation wait stay on the tape. */
                kag_bot_action(game, player, -1, &actions[player]);
            }
        } else if (bot >= KAG_BOT_ADAPTIVE_BASE
                && bot < KAG_BOT_ADAPTIVE_BASE + KAG_ADAPTIVE_COUNT) {
            kag_adaptive_action(game, player,
                bot - KAG_BOT_ADAPTIVE_BASE, &actions[player]);
        } else if (bot >= KAG_BOT_MIXED) {
            int fixed_crop = bot == KAG_BOT_MIXED
                ? -1 : bot - KAG_BOT_CROP_BASE;
            kag_bot_action(game, player, fixed_crop, &actions[player]);
        }
        if (!(bot >= KAG_BOT_SCRIPT_BASE
                && bot < KAG_BOT_SCRIPT_BASE + KG_SCRIPT_COUNT)
                && !(bot >= KAG_BOT_ADAPTIVE_BASE
                    && bot < KAG_BOT_ADAPTIVE_BASE + KAG_ADAPTIVE_COUNT)) {
            kag_apply_policy_limits(env, &game->players[player],
                &actions[player]);
        }
    }

    kag_log_actions(env, game, &actions[0]);

    if (env->reward_productive_action > 0.0f) {
        for (int player = 0; player < KG_NUM_PLAYERS; player++) {
            productive_credit[player] = kag_productive_action_credit(
                game, &game->players[player], &actions[player]);
        }
    }

    kg_step(game, actions);
    int p0 = game->players[0].money;
    int p1 = game->players[1].money;
    int model_player = env->bot_opponent == KAG_BOT_NONE && env->tag > 0
        ? (env->agents[0].policy == 0 ? 0 : 1)
        : (env->bot_first ? 1 : 0);
    int model_money = model_player ? p1 : p0;
    int opponent_money = model_player ? p0 : p1;
    int done = kg_done(game);
    /* Mark assets to market while play continues, then write every unsold asset
     * down to zero at terminal. Each player's deltas telescope to that player's
     * terminal cash, avoiding uncontrollable reward noise from scripted rivals. */
    env->potential[0] = done ? (float)p0 : kag_player_potential(env, 0);
    env->potential[1] = done ? (float)p1 : kag_player_potential(env, 1);
    float differential = ((env->potential[0] - before_potential[0])
        - (env->potential[1] - before_potential[1]))
        * env->reward_differential_scale;
    for (int player = 0; player < KG_NUM_PLAYERS; player++) {
        float reward = kag_potential_shaping_reward(env,
                before_potential[player], env->potential[player], done)
            + productive_credit[player] * env->reward_productive_action;
        reward -= (game->neglect_deaths[player]
            - before_neglect_deaths[player]) * env->reward_neglect_death;
        reward += player == 0 ? differential : -differential;
        env->agents[player].rewards[0] = reward;
        env->episode_returns[player] += reward;
    }

    if (done) {
        float terminal_potential[KG_NUM_PLAYERS] = {
            kag_player_potential_full(env, 0),
            kag_player_potential_full(env, 1),
        };
        float win0 = p0 > p1 ? 1.0f : (p0 == p1 ? 0.5f : 0.0f);
        float model_win = model_player == 0 ? win0 : 1.0f - win0;
        float outcome = (2.0f * win0 - 1.0f) * env->reward_win;
        /* Margin-aware term: reward proportional to the money gap so a close
         * loss is preferred to a blowout. Normalized by starting money so the
         * scale is comparable across configs. */
        int opp_money = model_player == 0 ? p1 : p0;
        float margin = (float)(model_money - opp_money)
            / (float)game->config.starting_money;
        float margin_term = env->reward_margin_scale * margin;
        float money0_term = kag_terminal_money_reward(env, p0);
        float money1_term = kag_terminal_money_reward(env, p1);
        env->agents[0].rewards[0] += money0_term;
        env->agents[1].rewards[0] += money1_term;
        env->episode_returns[0] += money0_term;
        env->episode_returns[1] += money1_term;
        env->agents[0].rewards[0] += outcome;
        env->agents[1].rewards[0] -= outcome;
        env->episode_returns[0] += outcome;
        env->episode_returns[1] -= outcome;
        if (model_player == 0) {
            env->agents[0].rewards[0] += margin_term;
            env->agents[1].rewards[0] -= margin_term;
            env->episode_returns[0] += margin_term;
            env->episode_returns[1] -= margin_term;
        } else {
            env->agents[0].rewards[0] -= margin_term;
            env->agents[1].rewards[0] += margin_term;
            env->episode_returns[0] -= margin_term;
            env->episode_returns[1] += margin_term;
        }
        if (abs(p0 - game->config.starting_money)
                <= env->reward_inactivity_threshold) {
            env->agents[0].rewards[0] -= env->reward_inactivity;
            env->episode_returns[0] -= env->reward_inactivity;
        }
        if (abs(p1 - game->config.starting_money)
                <= env->reward_inactivity_threshold) {
            env->agents[1].rewards[0] -= env->reward_inactivity;
            env->episode_returns[1] -= env->reward_inactivity;
        }
        env->agents[0].terminals[0] = 1.0f;
        env->agents[1].terminals[0] = 1.0f;
        env->log.perf += model_win;
        env->log.score += terminal_potential[model_player];
        env->log.sweep_score += terminal_potential[model_player];
        env->log.opponent_score += terminal_potential[1 - model_player];
        env->log.money += (float)model_money;
        env->log.opponent_money += (float)opponent_money;
        env->log.gdp += game->production_value[model_player];
        env->log.opponent_gdp += game->production_value[1 - model_player];
        env->log.production_units += game->production_units[model_player];
        env->log.opponent_production_units +=
            game->production_units[1 - model_player];
        env->log.successful_plants += game->planted_crops[model_player];
        env->log.successful_animal_places += game->placed_animals[model_player];
        env->log.sold_units += game->sold_units[model_player];
        env->log.sales_revenue += game->sales_revenue[model_player];
        env->log.bought_units += game->bought_units[model_player];
        env->log.purchase_spend += game->purchase_spend[model_player];
        for (int item = 0; item < KG_NUM_PRODUCTS; item++) {
            int produced = (int)game->production_product_units[model_player][item];
            int sold = (int)game->sold_product_units[model_player][item];
            float revenue = game->sold_product_revenue[model_player][item];
            env->log.ending_shed_units += game->players[model_player].shed[item];
            env->log.ending_shed_value += game->players[model_player].shed[item]
                * game->market.prices[item];
            if (item <= KG_ITEM_MELON) {
                env->log.crop_production_units += produced;
                env->log.crop_sold_units += sold;
                env->log.crop_sales_revenue += revenue;
            } else if (item >= KG_ITEM_EGG && item <= KG_ITEM_WOOL) {
                env->log.animal_production_units += produced;
                env->log.animal_product_sold_units += sold;
                env->log.animal_product_sales_revenue += revenue;
            }
        }
        env->log.strawberry_sold_units +=
            game->sold_product_units[model_player][KG_ITEM_STRAWBERRY];
        env->log.strawberry_sales_revenue +=
            game->sold_product_revenue[model_player][KG_ITEM_STRAWBERRY];
        env->log.milk_sold_units +=
            game->sold_product_units[model_player][KG_ITEM_MILK];
        env->log.milk_sales_revenue +=
            game->sold_product_revenue[model_player][KG_ITEM_MILK];
        kag_log_hinge_opportunity(game, model_player, KG_ITEM_CARROT,
            &env->log.carrot_opportunity_fraction,
            &env->log.carrot_opportunity_no_production_price,
            &env->log.carrot_opportunity_response,
            &env->log.carrot_opportunity_production,
            &env->log.carrot_nonopportunity_production,
            &env->log.carrot_opportunity_sold_units,
            &env->log.carrot_opportunity_sales_revenue,
            &env->log.carrot_opportunity_sale_price);
        kag_log_hinge_opportunity(game, model_player, KG_ITEM_TOMATO,
            &env->log.tomato_opportunity_fraction,
            &env->log.tomato_opportunity_no_production_price,
            &env->log.tomato_opportunity_response,
            &env->log.tomato_opportunity_production,
            &env->log.tomato_nonopportunity_production,
            &env->log.tomato_opportunity_sold_units,
            &env->log.tomato_opportunity_sales_revenue,
            &env->log.tomato_opportunity_sale_price);
        kag_log_hinge_opportunity(game, model_player, KG_ITEM_EGG,
            &env->log.egg_opportunity_fraction,
            &env->log.egg_opportunity_no_production_price,
            &env->log.egg_opportunity_response,
            &env->log.egg_opportunity_production,
            &env->log.egg_nonopportunity_production,
            &env->log.egg_opportunity_sold_units,
            &env->log.egg_opportunity_sales_revenue,
            &env->log.egg_opportunity_sale_price);
        env->log.strawberry_units +=
            game->production_product_units[model_player][KG_ITEM_STRAWBERRY];
        env->log.opponent_strawberry_units +=
            game->production_product_units[1 - model_player][KG_ITEM_STRAWBERRY];
        env->log.strawberry_value +=
            game->production_product_value[model_player][KG_ITEM_STRAWBERRY];
        env->log.opponent_strawberry_value +=
            game->production_product_value[1 - model_player][KG_ITEM_STRAWBERRY];
        env->log.milk_units +=
            game->production_product_units[model_player][KG_ITEM_MILK];
        env->log.opponent_milk_units +=
            game->production_product_units[1 - model_player][KG_ITEM_MILK];
        env->log.milk_value +=
            game->production_product_value[model_player][KG_ITEM_MILK];
        env->log.opponent_milk_value +=
            game->production_product_value[1 - model_player][KG_ITEM_MILK];
        env->log.episode_return += env->episode_returns[model_player];
        env->log.episode_length += (float)game->config.episode_steps;
        env->log.land_purchases += (float)(
            __builtin_popcount((unsigned)game->players[model_player].unlocked_mask) - 1);
        env->log.water_coverage += game->plant_days[model_player] > 0
            ? (float)game->watered_plant_days[model_player]
                / game->plant_days[model_player] : 1.0f;
        env->log.neglect_deaths += (float)game->neglect_deaths[model_player];
        env->log.planting_day_deaths += (float)game->planting_day_deaths[model_player];
        for (int crop = 0; crop < KG_NUM_CROPS; crop++) {
            env->log.unused_seed_value += game->players[model_player].seeds[crop]
                * KG_CROP_DEFS[crop].seed_cost;
        }
        for (int tile = 0; tile < KG_MAX_TILES; tile++) {
            int x = tile % KG_MAX_BOARD_SIZE;
            int y = tile / KG_MAX_BOARD_SIZE;
            const KGTile* t = &game->players[model_player].tiles[tile];
            if (t->kind == KG_TILE_PLANT) {
                env->log.plants_alive += 1.0f;
            } else if (kg_is_animal_tile(t)) {
                env->log.animals_alive += 1.0f;
            } else if (t->kind == KG_TILE_WEED) {
                env->log.weeds += 1.0f;
            }
            if ((x >= 5 || y >= 5)
                    && (t->kind == KG_TILE_PLANT || kg_is_animal_tile(t))) {
                env->log.productive_extra_tiles += 1.0f;
            }
        }
        env->log.win_rate += model_win;
        env->log.draw_rate += p0 == p1 ? 1.0f : 0.0f;
        if (env->bot_opponent == KAG_BOT_NONE && env->tag == 0) {
            env->log.mirror_games += 1.0f;
        } else if (env->bot_opponent == KAG_BOT_NONE) {
            env->log.checkpoint_wins += model_win;
            env->log.checkpoint_draws += p0 == p1 ? 1.0f : 0.0f;
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
            if (profile == KG_SCRIPT_FRONTIER) {
                env->log.script_frontier_games += 1.0f;
            } else if (profile == KG_SCRIPT_V20) {
                env->log.script_v20_games += 1.0f;
            } else if (profile == KG_SCRIPT_MOON) {
                env->log.script_moon_games += 1.0f;
            } else if (profile == KG_SCRIPT_HAMBURGER) {
                env->log.script_hamburger_games += 1.0f;
            } else if (profile == KG_SCRIPT_LUGOVOY) {
                env->log.script_lugovoy_games += 1.0f;
            } else if (profile == KG_SCRIPT_THUNDER25) {
                env->log.script_thunder_games += 1.0f;
            } else if (profile == KG_SCRIPT_TOP) {
                env->log.script_top_games += 1.0f;
            }
        } else if (env->bot_opponent >= KAG_BOT_ADAPTIVE_BASE
                && env->bot_opponent < KAG_BOT_ADAPTIVE_BASE
                    + KAG_ADAPTIVE_COUNT) {
            int profile = env->bot_opponent - KAG_BOT_ADAPTIVE_BASE;
            env->log.adaptive_games += 1.0f;
            if (profile == KAG_ADAPTIVE_HARVEST_PULSE) {
                env->log.adaptive_pulse_games += 1.0f;
            } else if (profile == KAG_ADAPTIVE_STRUCTURED) {
                env->log.adaptive_structured_games += 1.0f;
            } else if (profile == KAG_ADAPTIVE_TRIAD) {
                env->log.adaptive_triad_games += 1.0f;
            } else if (profile == KAG_ADAPTIVE_THUNDER) {
                env->log.adaptive_thunder_games += 1.0f;
            }
        } else {
            env->log.specialist_games += 1.0f;
        }
        env->log.n += 1.0f;
        env->log.reset_games += env->reset_source ? 1.0f : 0.0f;
        if (env->tag > 0) env->boundary_reached = 1;

        /* Auto-reset while preserving the completed transition's reward and
         * terminal flag in PufferLib's externally-owned buffers. */
        kag_reset_with_opening(env, kag_script_tapes);
        env->episode_returns[0] = 0.0f;
        env->episode_returns[1] = 0.0f;
        env->potential[0] = kag_player_potential(env, 0);
        env->potential[1] = kag_player_potential(env, 1);
        kag_write_all_observations(env);
    } else {
        kag_write_all_observations(env);
    }
}

static inline Vector2 kag_v2(int x, int y) {
    return (Vector2){(float)x, (float)y};
}

static inline Rectangle kag_rect(int x, int y, int width, int height) {
    return (Rectangle){(float)x, (float)y, (float)width, (float)height};
}

static inline void kag_draw_crop_icon(int crop, int cx, int cy, int size) {
    if (crop == KG_WHEAT) {
        DrawLineEx(kag_v2(cx, cy + size/3), kag_v2(cx, cy - size/3), 3, (Color){111,75,33,255});
        for (int i = -1; i <= 1; i++) DrawCircle(cx + i*5, cy - size/4 + abs(i)*5, 4, (Color){240,195,55,255});
    } else if (crop == KG_CARROT) {
        DrawTriangle(kag_v2(cx-7, cy-5), kag_v2(cx+7, cy-5), kag_v2(cx, cy+15), (Color){244,125,32,255});
        DrawLine(cx, cy-5, cx-6, cy-14, (Color){52,133,62,255});
        DrawLine(cx, cy-5, cx+7, cy-14, (Color){52,133,62,255});
    } else if (crop == KG_TOMATO) {
        DrawCircle(cx, cy+2, size*.26f, (Color){216,55,45,255});
        DrawTriangle(kag_v2(cx, cy-10), kag_v2(cx-8, cy-2), kag_v2(cx+8, cy-2), (Color){51,125,54,255});
    } else if (crop == KG_STRAWBERRY) {
        DrawTriangle(kag_v2(cx-10, cy-7), kag_v2(cx+10, cy-7), kag_v2(cx, cy+15), (Color){225,50,72,255});
        DrawCircle(cx, cy-6, 7, (Color){225,50,72,255});
        DrawLine(cx, cy-8, cx, cy-15, (Color){45,125,55,255});
    } else {
        DrawCircle(cx, cy, size*.29f, (Color){49,137,70,255});
        DrawCircleLines(cx, cy, size*.29f, (Color){24,84,44,255});
        DrawLine(cx-5, cy-size/4, cx-5, cy+size/4, (Color){140,190,70,255});
        DrawLine(cx+5, cy-size/4, cx+5, cy+size/4, (Color){140,190,70,255});
    }
}

static inline void kag_draw_tile(const KGTile* tile, int px, int py, int cell,
        int checker) {
    Color soil = checker ? (Color){193,143,91,255} : (Color){203,153,99,255};
    if (tile->kind == KG_TILE_LOCKED) {
        DrawRectangle(px, py, cell-1, cell-1, (Color){104,105,100,255});
        for (int d = -cell; d < cell*2; d += 14) {
            DrawLine(px+d, py, px+d-cell, py+cell, (Color){72,74,71,150});
        }
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
        if (tile->yield_units) DrawText(TextFormat("%d", tile->yield_units), px+3, py+2, 11, (Color){56,38,24,255});
    } else if (tile->kind == KG_TILE_WEED) {
        for (int i = -2; i <= 2; i++) DrawLine(cx, cy+12, cx+i*5, cy-10+abs(i)*3, (Color){35,91,42,255});
    } else if (tile->kind == KG_TILE_COOP || tile->kind == KG_TILE_PASTURE) {
        Color building = tile->kind == KG_TILE_COOP ? (Color){205,150,77,255} : (Color){102,157,80,255};
        DrawRectangle(cx-15, cy-12, 30, 25, building);
        DrawTriangle(kag_v2(cx-18, cy-12), kag_v2(cx+18, cy-12), kag_v2(cx, cy-24), (Color){130,67,43,255});
        if (tile->animal >= 0) {
            const char* glyph = tile->animal == KG_GOOSE ? "G" : tile->animal == KG_COW ? "C" : "S";
            DrawCircle(cx, cy, 11, RAYWHITE);
            DrawText(glyph, cx-4, cy-7, 14, (Color){44,39,34,255});
            if (tile->fed_today) DrawCircle(px+cell-8, py+8, 4, (Color){72,176,86,255});
        }
    }
}

static inline void kag_draw_worker(int cx, int cy, int id, int selected) {
    int offset_x = ((id % 3) - 1) * 8;
    int offset_y = ((id / 3) % 3 - 1) * 6;
    cx += offset_x;
    cy += offset_y;
    Color shirt = id == 0 ? (Color){210,58,53,255} : (Color){43,119,194,255};
    DrawCircle(cx, cy-6, 5, (Color){245,200,151,255});
    DrawRectangle(cx-5, cy-1, 10, 12, shirt);
    if (selected) DrawCircleLines(cx, cy+1, 13, GOLD);
}

static inline void kag_render_farm(const KGState* game, int player,
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
    DrawLineEx(kag_v2(origin_x+half*cell, origin_y), kag_v2(origin_x+half*cell, origin_y+10*cell), 3, (Color){91,65,42,255});
    DrawLineEx(kag_v2(origin_x, origin_y+half*cell), kag_v2(origin_x+10*cell, origin_y+half*cell), 3, (Color){91,65,42,255});

    int shed_x = origin_x + half*cell - 18;
    int shed_y = origin_y + half*cell - 18;
    DrawRectangle(shed_x, shed_y, 36, 36, (Color){173,67,47,255});
    DrawTriangle(kag_v2(shed_x-5, shed_y), kag_v2(shed_x+41, shed_y), kag_v2(shed_x+18, shed_y-17), (Color){108,48,36,255});
    DrawRectangle(shed_x+13, shed_y+17, 10, 19, (Color){91,52,35,255});

    for (int unit = farm->unit_count-1; unit >= 0; unit--) {
        const KGUnitState* worker = &farm->units[unit];
        kag_draw_worker(origin_x+worker->x*cell+cell/2,
            origin_y+worker->y*cell+cell/2, unit, selected_unit == unit);
    }
}

static inline void kag_render_inventory(const KGState* game, int player,
        int x, int y, int width, const char* name) {
    const KGPlayer* farm = &game->players[player];
    DrawRectangleRounded(kag_rect(x, y, width, 218), .04f, 6, (Color){240,214,164,255});
    DrawRectangleRoundedLines(kag_rect(x, y, width, 218), .04f, 6, (Color){114,82,47,255});
    DrawText(TextFormat("P%d  %s", player+1, name ? name : "agent"), x+15, y+12, 20, (Color){45,36,27,255});
    DrawText(TextFormat("$%d   hands %d   land %d/4", farm->money,
        farm->hand_count, __builtin_popcount((unsigned)farm->unlocked_mask)), x+15, y+38, 18, (Color){71,49,29,255});
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

static inline void kag_render_town(const KGState* game, int x, int y, int width) {
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

void puf_render(Env* env) {
    KGState* game = &env->game_storage;
    if (!env->render_enabled) return;
    if (!IsWindowReady()) {
        InitWindow(1440, 900, "PufferLib Kaggriculture");
        if (!IsWindowReady()) {
            fprintf(stderr, "Kaggriculture Raylib: could not open a display\n");
            env->render_enabled = 0;
            return;
        }
        SetTargetFPS(12);
    }
    BeginDrawing();
    ClearBackground((Color){132,172,103,255});
    DrawRectangle(0, 0, 1440, 60, (Color){24,42,35,255});
    DrawText("PUFFERLIB KAGGRICULTURE", 24, 16, 25, RAYWHITE);
    DrawText(TextFormat("step %d   SPACE pause   . step   ESC quit", game->step),
        947, 20, 16, (Color){210,226,211,255});

    const char* name0 = env->render_names[0] ? env->render_names[0] : "agent";
    const char* name1 = env->render_names[1] ? env->render_names[1] : "agent";
    DrawText(TextFormat("P1  %s   $%d", name0, game->players[0].money), 20, 72, 20, (Color){39,50,31,255});
    DrawText(TextFormat("P2  %s   $%d", name1, game->players[1].money), 920, 72, 20, (Color){39,50,31,255});
    kag_render_farm(game, 0, 20, 100, 50, env->render_selected_unit);
    kag_render_town(game, 540, 100, 360);
    kag_render_farm(game, 1, 920, 100, 50, -1);
    kag_render_inventory(game, 0, 20, 625, 500, name0);
    kag_render_inventory(game, 1, 920, 625, 500, name1);
    DrawRectangleRounded((Rectangle){540,625,360,218}, .04f, 6, (Color){237,221,183,255});
    DrawText("WHAT TO WATCH", 560, 645, 18, (Color){70,50,33,255});
    DrawText("Workers should leave the center,", 560, 678, 15, (Color){70,50,33,255});
    DrawText("spread across jobs, water crops,", 560, 702, 15, (Color){70,50,33,255});
    DrawText("harvest, and return goods to shed.", 560, 726, 15, (Color){70,50,33,255});
    DrawText("Blue dot = watered/fed today", 560, 765, 14, (Color){70,50,33,255});
    DrawText("Number = held harvest yield", 560, 788, 14, (Color){70,50,33,255});
    DrawText("SPACE pause/resume    PERIOD one turn", 560, 821, 13, (Color){70,50,33,255});
    DrawText("Native C simulator + native C/CUDA policies", 20, 872, 14, (Color){29,55,38,255});
    EndDrawing();
}

void puf_close(Env* env) {
    if (env->render_enabled && IsWindowReady()) CloseWindow();
}

void puf_log(Log* log, Dict* out) {
    dict_set(out, "perf", log->perf);
    dict_set(out, "score", log->score);
    dict_set(out, "sweep_score", log->sweep_score);
    dict_set(out, "opponent_score", log->opponent_score);
    /* Explicit names make downstream reports self-documenting while keeping
     * score/sweep_score as the optimizer-compatible potential aliases. */
    dict_set(out, "potential_score", log->score);
    dict_set(out, "opponent_potential", log->opponent_score);
    dict_set(out, "money", log->money);
    dict_set(out, "opponent_money", log->opponent_money);
    dict_set(out, "gdp", log->gdp);
    dict_set(out, "opponent_gdp", log->opponent_gdp);
    dict_set(out, "production_units", log->production_units);
    dict_set(out, "opponent_production_units",
        log->opponent_production_units);
    dict_set(out, "crop_production_units", log->crop_production_units);
    dict_set(out, "animal_production_units", log->animal_production_units);
    dict_set(out, "successful_plants", log->successful_plants);
    dict_set(out, "successful_animal_places", log->successful_animal_places);
    dict_set(out, "sold_units", log->sold_units);
    dict_set(out, "sales_revenue", log->sales_revenue);
    dict_set(out, "bought_units", log->bought_units);
    dict_set(out, "purchase_spend", log->purchase_spend);
    dict_set(out, "crop_sold_units", log->crop_sold_units);
    dict_set(out, "crop_sales_revenue", log->crop_sales_revenue);
    dict_set(out, "animal_product_sold_units", log->animal_product_sold_units);
    dict_set(out, "animal_product_sales_revenue",
        log->animal_product_sales_revenue);
    dict_set(out, "strawberry_sold_units", log->strawberry_sold_units);
    dict_set(out, "strawberry_sales_revenue", log->strawberry_sales_revenue);
    dict_set(out, "milk_sold_units", log->milk_sold_units);
    dict_set(out, "milk_sales_revenue", log->milk_sales_revenue);
    dict_set(out, "ending_shed_units", log->ending_shed_units);
    dict_set(out, "ending_shed_value", log->ending_shed_value);
    dict_set(out, "carrot_opportunity_fraction", log->carrot_opportunity_fraction);
    dict_set(out, "carrot_opportunity_no_production_price",
        log->carrot_opportunity_no_production_price);
    dict_set(out, "carrot_opportunity_response", log->carrot_opportunity_response);
    dict_set(out, "carrot_opportunity_production",
        log->carrot_opportunity_production);
    dict_set(out, "carrot_nonopportunity_production",
        log->carrot_nonopportunity_production);
    dict_set(out, "carrot_opportunity_sold_units",
        log->carrot_opportunity_sold_units);
    dict_set(out, "carrot_opportunity_sales_revenue",
        log->carrot_opportunity_sales_revenue);
    dict_set(out, "carrot_opportunity_sale_price",
        log->carrot_opportunity_sale_price);
    dict_set(out, "tomato_opportunity_fraction", log->tomato_opportunity_fraction);
    dict_set(out, "tomato_opportunity_no_production_price",
        log->tomato_opportunity_no_production_price);
    dict_set(out, "tomato_opportunity_response", log->tomato_opportunity_response);
    dict_set(out, "tomato_opportunity_production",
        log->tomato_opportunity_production);
    dict_set(out, "tomato_nonopportunity_production",
        log->tomato_nonopportunity_production);
    dict_set(out, "tomato_opportunity_sold_units",
        log->tomato_opportunity_sold_units);
    dict_set(out, "tomato_opportunity_sales_revenue",
        log->tomato_opportunity_sales_revenue);
    dict_set(out, "tomato_opportunity_sale_price",
        log->tomato_opportunity_sale_price);
    dict_set(out, "egg_opportunity_fraction", log->egg_opportunity_fraction);
    dict_set(out, "egg_opportunity_no_production_price",
        log->egg_opportunity_no_production_price);
    dict_set(out, "egg_opportunity_response", log->egg_opportunity_response);
    dict_set(out, "egg_opportunity_production", log->egg_opportunity_production);
    dict_set(out, "egg_nonopportunity_production",
        log->egg_nonopportunity_production);
    dict_set(out, "egg_opportunity_sold_units", log->egg_opportunity_sold_units);
    dict_set(out, "egg_opportunity_sales_revenue",
        log->egg_opportunity_sales_revenue);
    dict_set(out, "egg_opportunity_sale_price", log->egg_opportunity_sale_price);
    dict_set(out, "strawberry_units", log->strawberry_units);
    dict_set(out, "opponent_strawberry_units", log->opponent_strawberry_units);
    dict_set(out, "strawberry_value", log->strawberry_value);
    dict_set(out, "opponent_strawberry_value",
        log->opponent_strawberry_value);
    dict_set(out, "milk_units", log->milk_units);
    dict_set(out, "opponent_milk_units", log->opponent_milk_units);
    dict_set(out, "milk_value", log->milk_value);
    dict_set(out, "opponent_milk_value", log->opponent_milk_value);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "land_purchases", log->land_purchases);
    dict_set(out, "water_coverage", log->water_coverage);
    dict_set(out, "neglect_deaths", log->neglect_deaths);
    dict_set(out, "planting_day_deaths", log->planting_day_deaths);
    dict_set(out, "unused_seed_value", log->unused_seed_value);
    dict_set(out, "productive_extra_tiles", log->productive_extra_tiles);
    float checkpoint_score = log->checkpoint_games > 0.0f
        ? log->checkpoint_wins / log->checkpoint_games : log->win_rate;
    dict_set(out, "slot_0_score", checkpoint_score);
    /* In a two-bank match, player 1's score is the mirror. The env always
     * tracks player 0's win rate, so slot_1 is 1 - slot_0 with draws split. */
    dict_set(out, "slot_1_score", 1.0f - checkpoint_score);
    dict_set(out, "win_rate", log->win_rate);
    dict_set(out, "draw_rate", log->draw_rate);
    dict_set(out, "mirror_fraction", log->mirror_games);
    dict_set(out, "checkpoint_fraction", log->checkpoint_games);
    dict_set(out, "starter_fraction", log->starter_games);
    dict_set(out, "rules_fraction", log->rules_games);
    dict_set(out, "pass_fraction", log->pass_games);
    dict_set(out, "specialist_fraction", log->specialist_games);
    dict_set(out, "script_fraction", log->script_games);
    dict_set(out, "adaptive_fraction", log->adaptive_games);
    dict_set(out, "script_frontier_fraction", log->script_frontier_games);
    dict_set(out, "script_v20_fraction", log->script_v20_games);
    dict_set(out, "script_moon_fraction", log->script_moon_games);
    dict_set(out, "script_hamburger_fraction", log->script_hamburger_games);
    dict_set(out, "script_lugovoy_fraction", log->script_lugovoy_games);
    dict_set(out, "script_thunder_fraction", log->script_thunder_games);
    dict_set(out, "script_top_fraction", log->script_top_games);
    dict_set(out, "adaptive_pulse_fraction", log->adaptive_pulse_games);
    dict_set(out, "adaptive_structured_fraction",
        log->adaptive_structured_games);
    dict_set(out, "adaptive_triad_fraction", log->adaptive_triad_games);
    dict_set(out, "adaptive_thunder_fraction",
        log->adaptive_thunder_games);
    dict_set(out, "reset_fraction", log->reset_games);
    dict_set(out, "market_orders", log->market_orders);
    dict_set(out, "orders_per_turn", log->episode_length > 0.0f
        ? log->market_orders / log->episode_length : 0.0f);
    dict_set(out, "buy_orders", log->buy_orders);
    dict_set(out, "seed_buy_orders", log->seed_buy_orders);
    dict_set(out, "product_buy_orders", log->product_buy_orders);
    dict_set(out, "animal_buy_orders", log->animal_buy_orders);
    dict_set(out, "sell_orders", log->sell_orders);
    dict_set(out, "hire_orders", log->hire_orders);
    dict_set(out, "land_orders", log->land_orders);
    dict_set(out, "animal_place_actions", log->animal_place_actions);
    dict_set(out, "animal_feed_actions", log->animal_feed_actions);
    dict_set(out, "animal_care_actions", log->animal_care_actions);
    dict_set(out, "animal_harvest_actions", log->animal_harvest_actions);
    dict_set(out, "fertilizer_collect_actions",
        log->fertilizer_collect_actions);
    dict_set(out, "plants_alive", log->plants_alive);
    dict_set(out, "animals_alive", log->animals_alive);
    dict_set(out, "weeds", log->weeds);
}
