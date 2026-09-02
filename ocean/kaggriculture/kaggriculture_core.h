#ifndef PUFFERLIB_KAGGRICULTURE_CORE_H
#define PUFFERLIB_KAGGRICULTURE_CORE_H

/*
 * Rule-level native Kaggriculture simulator.
 *
 * The public competition engine is Python, but its state machine is small
 * enough to keep a native copy for high-throughput self-play.  This core uses
 * the same structured action vocabulary as the competition.  The PufferLib
 * policy adapter is intentionally kept separate: it can change observation
 * and macro-action encodings without changing the rules tested here.
 */

#include <stddef.h>
#include <stdint.h>

/* The rule core is compiled by both a normal C compiler and NVCC.  Keep the
 * public state/action ABI plain C while allowing the exact same transition
 * functions to run on the host and device. */
#if defined(__CUDACC__)
#define KG_HD __host__ __device__
#define KG_DEVICE __device__
#define KG_CONSTANT __constant__
#else
#define KG_HD
#define KG_DEVICE
#define KG_CONSTANT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define KG_NUM_PLAYERS 2
/* Kaggriculture's competition board is fixed at 10x10. Keeping the native
 * state at that exact shape avoids carrying a 32x32 farm through every cache
 * line of every parallel environment. */
#define KG_MAX_BOARD_SIZE 10
#define KG_MAX_TILES (KG_MAX_BOARD_SIZE * KG_MAX_BOARD_SIZE)
#define KG_TILE_WORDS ((KG_MAX_TILES + 63) / 64)
/* 24 turns x 10 default market orders is the maximum number of HIRE commands
 * accepted in a default day. Keep rule capacity independent of policy shape. */
#define KG_MAX_HANDS 240
#define KG_MAX_MARKET_ORDERS 32
#define KG_MAX_UNITS (KG_MAX_HANDS + 1)
#define KG_NUM_CROPS 5
#define KG_NUM_ANIMALS 3
#define KG_NUM_PRODUCTS 9
#define KG_NUM_ITEMS (KG_NUM_PRODUCTS + KG_NUM_ANIMALS)
#define KG_MAX_SHOPS 8
#define KG_STATE_SERIALIZATION_VERSION 1U

enum KGTileKind {
    KG_TILE_EMPTY = 0,
    KG_TILE_LOCKED,
    KG_TILE_WEED,
    KG_TILE_COOP,
    KG_TILE_PASTURE,
    KG_TILE_PLANT,
    KG_TILE_ANIMAL,
};

enum KGCrop {
    KG_WHEAT = 0,
    KG_CARROT,
    KG_TOMATO,
    KG_STRAWBERRY,
    KG_MELON,
    KG_CROP_INVALID = -1,
};

enum KGAnimal {
    KG_GOOSE = 0,
    KG_COW,
    KG_SHEEP,
    KG_ANIMAL_INVALID = -1,
};

/* Item IDs use the order exposed by the official observation. */
enum KGItem {
    KG_ITEM_WHEAT = 0,
    KG_ITEM_CARROT,
    KG_ITEM_TOMATO,
    KG_ITEM_STRAWBERRY,
    KG_ITEM_MELON,
    KG_ITEM_EGG,
    KG_ITEM_MILK,
    KG_ITEM_WOOL,
    KG_ITEM_FERTILIZER,
    KG_ITEM_GOOSE,
    KG_ITEM_COW,
    KG_ITEM_SHEEP,
    KG_ITEM_INVALID = -1,
};

enum KGUnitOp {
    KG_OP_PASS = 0,
    KG_OP_NORTH,
    KG_OP_SOUTH,
    KG_OP_EAST,
    KG_OP_WEST,
    KG_OP_PICKUP,
    KG_OP_DROP,
    KG_OP_PLANT,
    KG_OP_WATER,
    KG_OP_HARVEST,
    KG_OP_FERTILIZE,
    KG_OP_BUILD_COOP,
    KG_OP_BUILD_PASTURE,
    KG_OP_DIG,
    KG_OP_PLACE,
    KG_OP_FEED,
    KG_OP_COLLECT_FERTILIZER,
    KG_OP_CARE,
};

enum KGMarketOp {
    KG_MARKET_BUY_SEED = 0,
    KG_MARKET_BUY_PRODUCT,
    KG_MARKET_BUY_ANIMAL,
    KG_MARKET_SELL,
    KG_MARKET_HIRE,
    KG_MARKET_BUY_LAND,
};

typedef struct {
    int op;
    int arg;
    int n;
} KGUnitAction;

typedef struct {
    int op;
    int item;
    int n;
} KGMarketOrder;

typedef struct {
    KGUnitAction farmer;
    KGUnitAction hands[KG_MAX_HANDS];
    int hand_count;
    KGMarketOrder market[KG_MAX_MARKET_ORDERS];
    int market_count;
} KGAction;

typedef struct {
    int episode_steps;
    int board_size;
    int starting_money;
    int max_market_orders_per_turn;
    int turns_per_day;
    int shed_capacity;
    double weed_spawn_chance;
    int town_shop_unlock_interval;
    int town_shop_sell_interval;
    int town_center_sell_interval;
    int farm_hand_cost_mult;
    uint64_t seed;
} KGConfig;

typedef struct {
    int16_t max_lifespan_step;
    int8_t crop;
    int8_t animal;
    int8_t planted_day;
    int8_t placed_day;
    int8_t fertilized_until_day;
    uint8_t kind;
    uint8_t watered_today;
    uint8_t consecutive_unwatered;
    uint8_t yield_units;
    uint8_t consecutive_unfed;
    uint8_t fed_today;
    uint8_t cared_today;
    uint8_t fertilizer_available;
    uint8_t pending_care_bonus;
} KGTile;

typedef struct {
    uint8_t x;
    uint8_t y;
} KGPosition;

typedef struct {
    uint8_t x;
    uint8_t y;
    int inventory[KG_NUM_ITEMS];
    uint8_t inventory_order[KG_NUM_ITEMS];
    uint8_t inventory_order_count;
} KGUnitState;

typedef struct {
    int money;
    KGTile tiles[KG_MAX_TILES];
    /* Hot daily rules iterate only live plants/animals. The tile payload stays
     * byte-addressable for exact observation/parity; these bitsets are merely
     * indexes and are updated whenever a tile changes kind. */
    uint64_t plant_bits[KG_TILE_WORDS];
    uint64_t animal_bits[KG_TILE_WORDS];
    KGPosition farmer;
    KGPosition hands[KG_MAX_HANDS];
    uint8_t hand_count;
    uint8_t unlocked_mask;
    uint8_t hires_today;
    int shed[KG_NUM_ITEMS];
    int seeds[KG_NUM_CROPS];
    KGUnitState units[KG_MAX_UNITS];
    uint8_t unit_count;
} KGPlayer;

typedef struct {
    int inventory[KG_NUM_PRODUCTS];
    int prices[KG_NUM_PRODUCTS];
} KGMarket;

typedef struct KGState {
    KGConfig config;
    int step;
    int day;
    int hour;
    int done;
    KGPlayer players[KG_NUM_PLAYERS];
    KGMarket market;
    int unlocked_shops[KG_MAX_SHOPS];
    int shop_count;
    uint32_t rng_state[KG_NUM_PLAYERS];
    uint32_t plant_days[KG_NUM_PLAYERS];
    uint32_t watered_plant_days[KG_NUM_PLAYERS];
    uint32_t neglect_deaths[KG_NUM_PLAYERS];
    uint32_t planting_day_deaths[KG_NUM_PLAYERS];
    /* Realized production is counted when a crop/animal yield is harvested.
     * Units exclude purchased goods; value is marked at the market price at
     * harvest, so this is a GDP-like production measure rather than sales
     * (which would count resale loops). */
    uint32_t production_units[KG_NUM_PLAYERS];
    float production_value[KG_NUM_PLAYERS];
    uint32_t production_product_units[KG_NUM_PLAYERS][KG_NUM_PRODUCTS];
    float production_product_value[KG_NUM_PLAYERS][KG_NUM_PRODUCTS];
    /* Realized lifecycle and market transactions. Unlike action diagnostics,
     * these increment only after an operation successfully commits. */
    uint32_t planted_crops[KG_NUM_PLAYERS];
    uint32_t placed_animals[KG_NUM_PLAYERS];
    uint32_t sold_units[KG_NUM_PLAYERS];
    float sales_revenue[KG_NUM_PLAYERS];
    uint32_t bought_units[KG_NUM_PLAYERS];
    float purchase_spend[KG_NUM_PLAYERS];
    uint32_t sold_product_units[KG_NUM_PLAYERS][KG_NUM_PRODUCTS];
    float sold_product_revenue[KG_NUM_PLAYERS][KG_NUM_PRODUCTS];
    /* Demand removed by shops/town independently of either player's trades.
     * This permits a no-production counterfactual opportunity label. */
    uint32_t exogenous_demand_units[KG_NUM_PRODUCTS];
} KGState;

KG_HD void kg_config_default(KGConfig* config);
KGState* kg_create(const KGConfig* config);
KG_HD void kg_init(KGState* state, const KGConfig* config);
void kg_destroy(KGState* state);
KG_HD void kg_reset(KGState* state);
KG_HD void kg_step(KGState* state, const KGAction actions[KG_NUM_PLAYERS]);
KG_HD int kg_done(const KGState* state);
/* Small read-only accessors used by offline counterfactual evaluation. */
KG_HD int kg_state_step(const KGState* state);
KG_HD int kg_player_money(const KGState* state, int player);
/* Deterministic reactive baseline used by offline PvP branching.  It only
 * reads the state and writes one legal-shaped action; it is not part of the
 * live PPO policy ABI.  The explicit liquidation window lets offline profiles
 * vary this rule while keeping the default symbol stable for callers. */
KG_HD void kg_rule_action_ex(const KGState* state, int player, int liquidation_steps,
    KGAction* output);
KG_HD void kg_rule_action(const KGState* state, int player, KGAction* output);
/* Complete, resumable snapshots for offline reset banks. The payload is the
 * native POD layout, so consumers must require both this schema version and
 * the exact serialized size before loading it. */
size_t kg_state_serialized_size(void);
uint32_t kg_state_serialization_version(void);
int kg_state_serialize(const KGState* state, void* output, size_t output_size);
int kg_state_deserialize(KGState* state, const void* input, size_t input_size);
const char* kg_snapshot_json(const KGState* state);
void kg_free_string(const char* value);
/* Exact elite PPO observation/mask views without JSON serialization.  These
 * helpers are used by the offline learned-opponent batcher and intentionally
 * share the production kag_write_observation/kag_write_mask implementation. */
void kg_policy_observation(const KGState* state, int player, unsigned char* output,
    size_t output_size);
void kg_policy_action_mask(const KGState* state, int player, unsigned char* output,
    size_t output_size);
int kg_policy_hand_count(const KGState* state, int player);

/* String/enum helpers used by the parity harness and agent adapter. */
int kg_crop_from_name(const char* name);
int kg_animal_from_name(const char* name);
int kg_item_from_name(const char* name);
int kg_shop_from_name(const char* name);
const char* kg_crop_name(int crop);
const char* kg_animal_name(int animal);
const char* kg_item_name(int item);
const char* kg_shop_name(int shop);

#ifdef __cplusplus
}
#endif

#endif
