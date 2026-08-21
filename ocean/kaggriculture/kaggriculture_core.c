#include "kaggriculture_core.h"

#include <math.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int seed_cost;
    int first_yield_day;
    int max_yield_day;
    int interval;
    int max_yield;
    int ongoing;
} KGCropDef;

typedef struct {
    int cost;
    int structure;
    int first_yield_day;
    int interval;
    int max_held;
    int product;
} KGAnimalDef;

typedef struct {
    int base;
    int i0;
    int throughput;
    int below_func;
    double below_target;
    int above_func;
    double above_target;
} KGMarketDef;

enum {
    KG_FUNC_LINEAR = 0,
    KG_FUNC_SQ,
    KG_FUNC_SQRT,
    KG_FUNC_LOG,
    KG_FUNC_LOG10,
    KG_FUNC_HINGE,
};

static const char* const KG_CROP_NAMES[KG_NUM_CROPS] = {
    "WHEAT", "CARROT", "TOMATO", "STRAWBERRY", "MELON",
};

static const char* const KG_ANIMAL_NAMES[KG_NUM_ANIMALS] = {
    "GOOSE", "COW", "SHEEP",
};

static const char* const KG_ITEM_NAMES[KG_NUM_ITEMS] = {
    "WHEAT", "CARROT", "TOMATO", "STRAWBERRY", "MELON",
    "EGG", "MILK", "WOOL", "FERTILIZER", "GOOSE", "COW", "SHEEP",
};

static const char* const KG_PRODUCT_NAMES[KG_NUM_PRODUCTS] = {
    "WHEAT", "CARROT", "TOMATO", "STRAWBERRY", "MELON",
    "EGG", "MILK", "WOOL", "FERTILIZER",
};

static const KGCropDef KG_CROP_DEFS_HOST[KG_NUM_CROPS] = {
    {10, 2, 4, 0, 6, 0},
    {20, 2, 3, 0, 4, 0},
    {50, 8, 8, 1, 4, 1},
    {100, 10, 10, 2, 4, 1},
    {80, 10, 12, 0, 6, 0},
};

static const KGAnimalDef KG_ANIMAL_DEFS_HOST[KG_NUM_ANIMALS] = {
    {300, KG_TILE_COOP, 4, 1, 4, KG_ITEM_EGG},
    {400, KG_TILE_PASTURE, 8, 2, 6, KG_ITEM_MILK},
    {500, KG_TILE_PASTURE, 6, 3, 6, KG_ITEM_WOOL},
};

/* Mirrors MARKET_PARAMS in the pinned Kaggle interpreter. */
static const KGMarketDef KG_MARKET_DEFS_HOST[KG_NUM_PRODUCTS] = {
    {25, 10000, 400, KG_FUNC_SQRT, 0.80, KG_FUNC_LOG, 0.20},
    {35, 10000, 450, KG_FUNC_HINGE, 1.00, KG_FUNC_SQRT, 0.70},
    {60, 10000, 200, KG_FUNC_HINGE, 0.40, KG_FUNC_SQRT, 0.60},
    {120, 10000, 100, KG_FUNC_SQRT, 0.70, KG_FUNC_LINEAR, 1.60},
    {250, 10000, 300, KG_FUNC_LOG, 0.20, KG_FUNC_SQ, 3.60},
    {50, 10000, 332, KG_FUNC_HINGE, 0.40, KG_FUNC_LOG, 0.20},
    {160, 10000, 122, KG_FUNC_SQRT, 0.60, KG_FUNC_LINEAR, 1.60},
    {200, 10000, 105, KG_FUNC_LOG, 0.20, KG_FUNC_SQ, 3.20},
    {100, 10000, 200, KG_FUNC_LINEAR, 0.40, KG_FUNC_LINEAR, 0.40},
};

static const char* const KG_SHOP_NAMES[KG_MAX_SHOPS] = {
    /* Alphabetical sorted(SHOPS) order, matching Python's rng.choice. */
    "BAKERY", "BRUNCH_SPOT", "FARMERS_MARKET", "ICE_CREAM_SHOP",
    "PET_CAFE", "PIZZA_SHOP", "SMOOTHIE_SHOP", "YARN_STORE",
};

static const int KG_SHOP_PRODUCTS[KG_MAX_SHOPS][KG_NUM_PRODUCTS] = {
    {KG_ITEM_EGG, KG_ITEM_WHEAT, -1, -1, -1, -1, -1, -1, -1},
    {KG_ITEM_EGG, KG_ITEM_WHEAT, KG_ITEM_STRAWBERRY, -1, -1, -1, -1, -1, -1},
    {KG_ITEM_WHEAT, KG_ITEM_CARROT, KG_ITEM_TOMATO, KG_ITEM_STRAWBERRY,
        -1, -1, -1, -1, -1},
    {KG_ITEM_STRAWBERRY, KG_ITEM_MILK, KG_ITEM_WHEAT, -1, -1, -1, -1, -1, -1},
    {KG_ITEM_CARROT, -1, -1, -1, -1, -1, -1, -1, -1},
    {KG_ITEM_MILK, KG_ITEM_TOMATO, KG_ITEM_WHEAT, -1, -1, -1, -1, -1, -1},
    {KG_ITEM_STRAWBERRY, KG_ITEM_MILK, -1, -1, -1, -1, -1, -1, -1},
    {KG_ITEM_WOOL, -1, -1, -1, -1, -1, -1, -1, -1},
};

#ifdef __CUDACC__
/* Namespace-scope C data is host-only under NVCC unless explicitly marked.
 * These small immutable mirrors are initialized into device constant memory
 * by the CUDA loader; accessors below select the correct address space in
 * each half of a __host__ __device__ compilation. */
static KG_DEVICE KG_CONSTANT KGCropDef KG_CROP_DEFS_DEVICE[KG_NUM_CROPS] = {
    {10, 2, 4, 0, 6, 0},
    {20, 2, 3, 0, 4, 0},
    {50, 8, 8, 1, 4, 1},
    {100, 10, 10, 2, 4, 1},
    {80, 10, 12, 0, 6, 0},
};
static KG_DEVICE KG_CONSTANT KGAnimalDef KG_ANIMAL_DEFS_DEVICE[KG_NUM_ANIMALS] = {
    {300, KG_TILE_COOP, 4, 1, 4, KG_ITEM_EGG},
    {400, KG_TILE_PASTURE, 8, 2, 6, KG_ITEM_MILK},
    {500, KG_TILE_PASTURE, 6, 3, 6, KG_ITEM_WOOL},
};
static KG_DEVICE KG_CONSTANT KGMarketDef KG_MARKET_DEFS_DEVICE[KG_NUM_PRODUCTS] = {
    {25, 10000, 400, KG_FUNC_SQRT, 0.80, KG_FUNC_LOG, 0.20},
    {35, 10000, 450, KG_FUNC_HINGE, 1.00, KG_FUNC_SQRT, 0.70},
    {60, 10000, 200, KG_FUNC_HINGE, 0.40, KG_FUNC_SQRT, 0.60},
    {120, 10000, 100, KG_FUNC_SQRT, 0.70, KG_FUNC_LINEAR, 1.60},
    {250, 10000, 300, KG_FUNC_LOG, 0.20, KG_FUNC_SQ, 3.60},
    {50, 10000, 332, KG_FUNC_HINGE, 0.40, KG_FUNC_LOG, 0.20},
    {160, 10000, 122, KG_FUNC_SQRT, 0.60, KG_FUNC_LINEAR, 1.60},
    {200, 10000, 105, KG_FUNC_LOG, 0.20, KG_FUNC_SQ, 3.20},
    {100, 10000, 200, KG_FUNC_LINEAR, 0.40, KG_FUNC_LINEAR, 0.40},
};
static KG_DEVICE KG_CONSTANT int
KG_SHOP_PRODUCTS_DEVICE[KG_MAX_SHOPS][KG_NUM_PRODUCTS] = {
    {KG_ITEM_EGG, KG_ITEM_WHEAT, -1, -1, -1, -1, -1, -1, -1},
    {KG_ITEM_EGG, KG_ITEM_WHEAT, KG_ITEM_STRAWBERRY, -1, -1, -1, -1, -1, -1},
    {KG_ITEM_WHEAT, KG_ITEM_CARROT, KG_ITEM_TOMATO, KG_ITEM_STRAWBERRY,
        -1, -1, -1, -1, -1},
    {KG_ITEM_STRAWBERRY, KG_ITEM_MILK, KG_ITEM_WHEAT, -1, -1, -1, -1, -1, -1},
    {KG_ITEM_CARROT, -1, -1, -1, -1, -1, -1, -1, -1},
    {KG_ITEM_MILK, KG_ITEM_TOMATO, KG_ITEM_WHEAT, -1, -1, -1, -1, -1, -1},
    {KG_ITEM_STRAWBERRY, KG_ITEM_MILK, -1, -1, -1, -1, -1, -1, -1},
    {KG_ITEM_WOOL, -1, -1, -1, -1, -1, -1, -1, -1},
};
#endif

KG_HD static inline const KGCropDef* kg_crop_defs(void) {
#ifdef __CUDA_ARCH__
    return KG_CROP_DEFS_DEVICE;
#else
    return KG_CROP_DEFS_HOST;
#endif
}

KG_HD static inline const KGAnimalDef* kg_animal_defs(void) {
#ifdef __CUDA_ARCH__
    return KG_ANIMAL_DEFS_DEVICE;
#else
    return KG_ANIMAL_DEFS_HOST;
#endif
}

KG_HD static inline const KGMarketDef* kg_market_defs(void) {
#ifdef __CUDA_ARCH__
    return KG_MARKET_DEFS_DEVICE;
#else
    return KG_MARKET_DEFS_HOST;
#endif
}

KG_HD static inline int kg_shop_product(int shop, int index) {
#ifdef __CUDA_ARCH__
    return KG_SHOP_PRODUCTS_DEVICE[shop][index];
#else
    return KG_SHOP_PRODUCTS[shop][index];
#endif
}

#define KG_CROP_DEFS (kg_crop_defs())
#define KG_ANIMAL_DEFS (kg_animal_defs())
#define KG_MARKET_DEFS (kg_market_defs())

KG_HD static inline int kg_ctz64(uint64_t value) {
#ifdef __CUDA_ARCH__
    return __ffsll((long long)value) - 1;
#else
    return __builtin_ctzll(value);
#endif
}

/* Python's random.Random uses MT19937.  This is the small subset needed by
 * Kaggriculture's deterministic daily weed/shop stream. */
typedef struct {
    uint32_t mt[624];
    int index;
} KGPythonRandom;

KG_HD static void kg_mt_seed_array(KGPythonRandom* r, const uint32_t* key, int key_len) {
    int i = 1;
    int j = 0;
    int k;
    r->mt[0] = 19650218U;
    for (i = 1; i < 624; i++) {
        r->mt[i] = 1812433253U * (r->mt[i - 1] ^ (r->mt[i - 1] >> 30)) + (uint32_t)i;
    }
    i = 1;
    k = 624 > key_len ? 624 : key_len;
    for (; k > 0; k--) {
        r->mt[i] = (r->mt[i] ^ ((r->mt[i - 1] ^ (r->mt[i - 1] >> 30)) * 1664525U))
            + key[j] + (uint32_t)j;
        i++;
        j++;
        if (i >= 624) {
            r->mt[0] = r->mt[623];
            i = 1;
        }
        if (j >= key_len) {
            j = 0;
        }
    }
    for (k = 623; k > 0; k--) {
        r->mt[i] = (r->mt[i] ^ ((r->mt[i - 1] ^ (r->mt[i - 1] >> 30)) * 1566083941U))
            - (uint32_t)i;
        i++;
        if (i >= 624) {
            r->mt[0] = r->mt[623];
            i = 1;
        }
    }
    r->mt[0] = 0x80000000U;
    r->index = 624;
}

KG_HD static void kg_random_seed(KGPythonRandom* r, uint64_t seed) {
    uint32_t key[2];
    key[0] = (uint32_t)seed;
    key[1] = (uint32_t)(seed >> 32);
    /* CPython strips leading zero words from positive PyLong seeds. */
    kg_mt_seed_array(r, key, key[1] ? 2 : 1);
}

KG_HD static uint32_t kg_random_uint32(KGPythonRandom* r) {
    uint32_t y;
    int kk;
    if (r->index >= 624) {
        for (kk = 0; kk < 624 - 397; kk++) {
            y = (r->mt[kk] & 0x80000000U) | (r->mt[kk + 1] & 0x7fffffffU);
            r->mt[kk] = r->mt[kk + 397] ^ (y >> 1)
                ^ ((y & 1U) ? 0x9908b0dfU : 0U);
        }
        for (; kk < 623; kk++) {
            y = (r->mt[kk] & 0x80000000U) | (r->mt[kk + 1] & 0x7fffffffU);
            r->mt[kk] = r->mt[kk + (397 - 624)] ^ (y >> 1)
                ^ ((y & 1U) ? 0x9908b0dfU : 0U);
        }
        y = (r->mt[623] & 0x80000000U) | (r->mt[0] & 0x7fffffffU);
        r->mt[623] = r->mt[396] ^ (y >> 1)
            ^ ((y & 1U) ? 0x9908b0dfU : 0U);
        r->index = 0;
    }
    y = r->mt[r->index++];
    y ^= y >> 11;
    y ^= (y << 7) & 0x9d2c5680U;
    y ^= (y << 15) & 0xefc60000U;
    y ^= y >> 18;
    return y;
}

KG_HD static double kg_random_double(KGPythonRandom* r) {
    uint32_t a = kg_random_uint32(r) >> 5;
    uint32_t b = kg_random_uint32(r) >> 6;
    return ((double)a * 67108864.0 + (double)b) / 9007199254740992.0;
}

KG_HD static int kg_random_below(KGPythonRandom* r, int n) {
    unsigned int k = 0;
    uint32_t value;
    int x = n;
    if (n <= 1) {
        return 0;
    }
    while (x > 0) {
        k++;
        x >>= 1;
    }
    do {
        value = kg_random_uint32(r) >> (32U - k);
    } while (value >= (uint32_t)n);
    return (int)value;
}

KG_HD static int kg_quadrant(int x, int y, int board_size) {
    int half = board_size / 2;
    if (y < half) {
        return x < half ? 1 : 2; /* NW, NE */
    }
    return x < half ? 4 : 8; /* SW, SE */
}

KG_HD static int kg_tile_index(int x, int y) {
    return y * KG_MAX_BOARD_SIZE + x;
}

KG_HD static int kg_shed_access_count(int board_size, KGPosition out[4]) {
    int half = board_size / 2;
    out[0] = (KGPosition){(uint8_t)(half - 1), (uint8_t)(half - 1)};
    out[1] = (KGPosition){(uint8_t)half, (uint8_t)(half - 1)};
    out[2] = (KGPosition){(uint8_t)(half - 1), (uint8_t)half};
    out[3] = (KGPosition){(uint8_t)half, (uint8_t)half};
    return 4;
}

KG_HD static int kg_is_shed_adjacent(const KGPosition* pos, int board_size) {
    KGPosition access[4];
    int i;
    kg_shed_access_count(board_size, access);
    for (i = 0; i < 4; i++) {
        if (pos->x == access[i].x && pos->y == access[i].y) {
            return 1;
        }
    }
    return 0;
}

KG_HD static KGPosition kg_default_spawn(int board_size) {
    KGPosition access[4];
    int i;
    kg_shed_access_count(board_size, access);
    for (i = 0; i < 4; i++) {
        if (kg_quadrant(access[i].x, access[i].y, board_size) == 1) {
            return access[i];
        }
    }
    return (KGPosition){0, 0};
}

KG_HD static void kg_clear_tile_data(KGTile* tile, int kind) {
    memset(tile, 0, sizeof(*tile));
    tile->kind = kind;
    tile->crop = KG_CROP_INVALID;
    tile->animal = KG_ANIMAL_INVALID;
    tile->max_lifespan_step = -1;
    tile->fertilized_until_day = -1;
}

KG_HD static inline void kg_set_tile_bits(KGPlayer* player, int index, int kind) {
    uint64_t bit = 1ULL << (index & 63);
    uint64_t* word = &player->plant_bits[index >> 6];
    uint64_t* animal_word = &player->animal_bits[index >> 6];
    *word &= ~bit;
    *animal_word &= ~bit;
    if (kind == KG_TILE_PLANT) *word |= bit;
    if (kind == KG_TILE_COOP || kind == KG_TILE_PASTURE) *animal_word |= bit;
}

KG_HD static void kg_set_player_tile(KGPlayer* player, int index, int kind) {
    kg_clear_tile_data(&player->tiles[index], kind);
    kg_set_tile_bits(player, index, kind);
}

KG_HD static int kg_is_animal_tile(const KGTile* tile) {
    return tile->animal >= 0 && tile->animal < KG_NUM_ANIMALS
        && (tile->kind == KG_TILE_COOP || tile->kind == KG_TILE_PASTURE);
}

KG_HD static int kg_shed_total(const KGPlayer* player) {
    int total = 0;
    int i;
    for (i = 0; i < KG_NUM_ITEMS; i++) {
        total += player->shed[i];
    }
    return total;
}

KG_HD static void kg_inventory_add(KGUnitState* unit, int item, int n) {
    int i;
    if (item < 0 || item >= KG_NUM_ITEMS || n <= 0) {
        return;
    }
    if (unit->inventory[item] == 0) {
        for (i = 0; i < unit->inventory_order_count; i++) {
            if (unit->inventory_order[i] == item) {
                break;
            }
        }
        if (i == unit->inventory_order_count && i < KG_NUM_ITEMS) {
            unit->inventory_order[unit->inventory_order_count++] = item;
        }
    }
    unit->inventory[item] += n;
}

KG_HD static int kg_inventory_take(KGUnitState* unit, int item, int n) {
    int i;
    if (item < 0 || item >= KG_NUM_ITEMS || n <= 0 || unit->inventory[item] < n) {
        return 0;
    }
    unit->inventory[item] -= n;
    if (unit->inventory[item] == 0) {
        for (i = 0; i < unit->inventory_order_count; i++) {
            if (unit->inventory_order[i] == item) {
                /* A tiny explicit shift is both faster here (at most twelve
                 * bytes) and has identical semantics under C and CUDA. */
                for (int j = i + 1; j < unit->inventory_order_count; j++) {
                    unit->inventory_order[j - 1] = unit->inventory_order[j];
                }
                unit->inventory_order_count--;
                break;
            }
        }
    }
    return 1;
}

KG_HD static void kg_sync_public_positions(KGPlayer* player) {
    int i;
    player->farmer.x = player->units[0].x;
    player->farmer.y = player->units[0].y;
    for (i = 0; i < player->hand_count; i++) {
        player->hands[i].x = player->units[i + 1].x;
        player->hands[i].y = player->units[i + 1].y;
    }
}

KG_HD static void kg_set_unit_position(KGPlayer* player, int idx, int x, int y) {
    player->units[idx].x = x;
    player->units[idx].y = y;
    /* Keep the public mirror in O(1). The old implementation recopied every
     * hand after every move, which becomes quadratic once a farm hires many
     * hands. */
    if (idx == 0) {
        player->farmer.x = x;
        player->farmer.y = y;
    } else if (idx - 1 < KG_MAX_HANDS) {
        player->hands[idx - 1].x = x;
        player->hands[idx - 1].y = y;
    }
}

KG_HD static KGPosition kg_unit_position(const KGPlayer* player, int idx) {
    return (KGPosition){player->units[idx].x, player->units[idx].y};
}

KG_HD static int kg_crop_product(int crop) {
    return crop >= 0 && crop < KG_NUM_CROPS ? crop : KG_ITEM_INVALID;
}

KG_HD static int kg_animal_product(int animal) {
    return animal >= 0 && animal < KG_NUM_ANIMALS
        ? KG_ANIMAL_DEFS[animal].product : KG_ITEM_INVALID;
}

KG_HD static void kg_new_plant(KGPlayer* player, int index, int crop, int day,
        int turns_per_day) {
    KGTile* tile = &player->tiles[index];
    const KGCropDef* def = &KG_CROP_DEFS[crop];
    kg_set_player_tile(player, index, KG_TILE_PLANT);
    tile->crop = crop;
    tile->planted_day = day;
    tile->watered_today = 0;
    /* The official interpreter counts planting itself as the first miss. */
    tile->consecutive_unwatered = 1;
    tile->yield_units = def->ongoing ? 0 : 1;
    tile->max_lifespan_step = def->ongoing ? -1
        : (day + def->max_yield_day + 1) * turns_per_day;
}

KG_HD static void kg_new_animal(KGPlayer* player, int index, int animal, int day) {
    KGTile* tile = &player->tiles[index];
    kg_set_player_tile(player, index, KG_ANIMAL_DEFS[animal].structure);
    tile->kind = KG_ANIMAL_DEFS[animal].structure;
    tile->animal = animal;
    tile->placed_day = day;
    tile->yield_units = 0;
    tile->consecutive_unfed = 0;
    tile->fed_today = 0;
    tile->cared_today = 0;
    tile->fertilizer_available = 0;
    tile->pending_care_bonus = 0;
}

KG_HD static double kg_shape(int func, double x, double T) {
    double u;
    double over;
    if (x < 0.0) {
        x = 0.0;
    }
    switch (func) {
        case KG_FUNC_SQ: return x * x;
        case KG_FUNC_SQRT: return sqrt(x);
        case KG_FUNC_LOG: return log(1.0 + x);
        case KG_FUNC_LOG10: return log10(1.0 + x);
        case KG_FUNC_HINGE:
            /* u + 8*max(0, u-1)^2 with u = x/T, matching the interpreter. */
            if (T <= 0.0) return x;
            u = x / T;
            over = u - 1.0;
            if (over < 0.0) over = 0.0;
            return u + 8.0 * over * over;
        default: return x;
    }
}

KG_HD static int kg_round_even(double value) {
    double lower;
    double fraction;
    if (value <= 0.0) {
        return 0;
    }
    lower = floor(value);
    fraction = value - lower;
    if (fraction > 0.5) {
        return (int)lower + 1;
    }
    if (fraction < 0.5) {
        return (int)lower;
    }
    return ((int)lower & 1) ? (int)lower + 1 : (int)lower;
}

KG_HD static int kg_market_price(int product, int inventory) {
    const KGMarketDef* def = &KG_MARKET_DEFS[product];
    double price;
    if (inventory < def->i0) {
        double amp = def->below_target * def->base
            / kg_shape(def->below_func, def->throughput, def->throughput);
        price = def->base + amp * kg_shape(def->below_func,
            (double)(def->i0 - inventory), def->throughput);
    } else {
        double amp = def->above_target * def->base
            / kg_shape(def->above_func, def->throughput, def->throughput);
        price = def->base - amp * kg_shape(def->above_func,
            (double)(inventory - def->i0), def->throughput);
    }
    price = kg_round_even(price);
    return price < 1 ? 1 : price;
}

KG_HD static void kg_refresh_prices(KGState* state) {
    int i;
    for (i = 0; i < KG_NUM_PRODUCTS; i++) {
        state->market.prices[i] = kg_market_price(i, state->market.inventory[i]);
    }
}

KG_HD void kg_config_default(KGConfig* config) {
    memset(config, 0, sizeof(*config));
    config->episode_steps = 720;
    config->board_size = 10;
    config->starting_money = 3000;
    config->max_market_orders_per_turn = 10;
    config->turns_per_day = 24;
    config->shed_capacity = 100;
    config->weed_spawn_chance = 0.005;
    config->town_shop_unlock_interval = 3;
    config->town_shop_sell_interval = 4;
    config->town_center_sell_interval = 24;
    config->farm_hand_cost_mult = 1;
    config->seed = 0;
}

KG_HD static void kg_normalize_config(KGConfig* config) {
    if (config->episode_steps < 1) config->episode_steps = 1;
    if (config->board_size < 4) config->board_size = 4;
    if (config->board_size > KG_MAX_BOARD_SIZE) config->board_size = KG_MAX_BOARD_SIZE;
    if (config->starting_money < 0) config->starting_money = 0;
    if (config->max_market_orders_per_turn < 1) config->max_market_orders_per_turn = 1;
    if (config->max_market_orders_per_turn > KG_MAX_MARKET_ORDERS) {
        config->max_market_orders_per_turn = KG_MAX_MARKET_ORDERS;
    }
    if (config->turns_per_day < 1) config->turns_per_day = 1;
    if (config->shed_capacity < 1) config->shed_capacity = 1;
    if (config->weed_spawn_chance < 0.0) config->weed_spawn_chance = 0.0;
    if (config->town_shop_unlock_interval < 1) config->town_shop_unlock_interval = 1;
    if (config->town_shop_sell_interval < 1) config->town_shop_sell_interval = 1;
    if (config->town_center_sell_interval < 1) config->town_center_sell_interval = 1;
    if (config->farm_hand_cost_mult < 0) config->farm_hand_cost_mult = 0;
}

KG_HD void kg_reset(KGState* state) {
    int player_id;
    int x;
    int y;
    int i;
    KGPosition spawn;
    if (state == NULL) {
        return;
    }
    state->step = 0;
    state->day = 0;
    state->hour = 0;
    state->done = 0;
    state->shop_count = 0;
    memset(state->plant_days, 0, sizeof(state->plant_days));
    memset(state->watered_plant_days, 0, sizeof(state->watered_plant_days));
    memset(state->neglect_deaths, 0, sizeof(state->neglect_deaths));
    memset(state->planting_day_deaths, 0,
        sizeof(state->planting_day_deaths));
    memset(state->production_units, 0, sizeof(state->production_units));
    memset(state->production_value, 0, sizeof(state->production_value));
    memset(state->production_product_units, 0,
        sizeof(state->production_product_units));
    memset(state->production_product_value, 0,
        sizeof(state->production_product_value));
    memset(state->planted_crops, 0, sizeof(state->planted_crops));
    memset(state->placed_animals, 0, sizeof(state->placed_animals));
    memset(state->sold_units, 0, sizeof(state->sold_units));
    memset(state->sales_revenue, 0, sizeof(state->sales_revenue));
    memset(state->bought_units, 0, sizeof(state->bought_units));
    memset(state->purchase_spend, 0, sizeof(state->purchase_spend));
    memset(state->sold_product_units, 0, sizeof(state->sold_product_units));
    memset(state->sold_product_revenue, 0,
        sizeof(state->sold_product_revenue));
    memset(state->exogenous_demand_units, 0,
        sizeof(state->exogenous_demand_units));
    for (i = 0; i < KG_NUM_PRODUCTS; i++) {
        state->market.inventory[i] = KG_MARKET_DEFS[i].i0;
        state->market.prices[i] = KG_MARKET_DEFS[i].base;
    }
    spawn = kg_default_spawn(state->config.board_size);
    for (player_id = 0; player_id < KG_NUM_PLAYERS; player_id++) {
        KGPlayer* player = &state->players[player_id];
        memset(player, 0, sizeof(*player));
        player->money = state->config.starting_money;
        player->unlocked_mask = 1;
        player->unit_count = 1;
        player->units[0].x = spawn.x;
        player->units[0].y = spawn.y;
        for (i = 0; i < KG_NUM_ITEMS; i++) {
            player->shed[i] = 0;
        }
        for (i = 0; i < KG_NUM_CROPS; i++) {
            player->seeds[i] = 0;
        }
        for (y = 0; y < state->config.board_size; y++) {
            for (x = 0; x < state->config.board_size; x++) {
                int kind = kg_quadrant(x, y, state->config.board_size) == 1
                    ? KG_TILE_EMPTY : KG_TILE_LOCKED;
                kg_set_player_tile(player, kg_tile_index(x, y), kind);
            }
        }
        kg_sync_public_positions(player);
        state->rng_state[player_id] = (uint32_t)(state->config.seed + (uint64_t)player_id * 747796405U);
    }
}

KG_HD void kg_init(KGState* state, const KGConfig* config) {
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
    kg_config_default(&state->config);
    if (config != NULL) {
        state->config = *config;
    }
    kg_normalize_config(&state->config);
    kg_reset(state);
}

KGState* kg_create(const KGConfig* config) {
    KGState* state = (KGState*)malloc(sizeof(*state));
    if (state == NULL) return NULL;
    kg_init(state, config);
    return state;
}

void kg_destroy(KGState* state) {
    free(state);
}

KG_HD int kg_done(const KGState* state) {
    return state != NULL && state->done;
}

KG_HD static int kg_fib(int n) {
    int a = 1;
    int b = 1;
    int i;
    for (i = 0; i < n; i++) {
        if (b > INT_MAX - a) return INT_MAX;
        int next = a + b;
        a = b;
        b = next;
    }
    return a;
}

KG_HD static int kg_hire_cost(int n, int multiplier) {
    int fib = kg_fib(n);
    if (multiplier <= 0) return 0;
    if (fib > INT_MAX / multiplier) return INT_MAX;
    return fib * multiplier;
}

KG_HD static KGPosition kg_spawn_hand(const KGPlayer* player, int board_size) {
    KGPosition access[4];
    int occupants[4] = {0, 0, 0, 0};
    int i;
    kg_shed_access_count(board_size, access);
    for (i = 0; i < player->unit_count; i++) {
        KGPosition pos = kg_unit_position(player, i);
        int j;
        for (j = 0; j < 4; j++) {
            if (pos.x == access[j].x && pos.y == access[j].y) {
                occupants[j]++;
            }
        }
    }
    {
        int best = 0;
        for (i = 1; i < 4; i++) {
            if (occupants[i] < occupants[best]) {
                best = i;
            }
        }
        return access[best];
    }
}

KG_HD static void kg_do_hire(KGState* state, KGPlayer* player) {
    KGPosition spawn;
    if (player->money < kg_hire_cost(player->hires_today, state->config.farm_hand_cost_mult)) {
        return;
    }
    if (player->hand_count >= KG_MAX_HANDS || player->unit_count >= KG_MAX_UNITS) {
        return;
    }
    player->money -= kg_hire_cost(player->hires_today, state->config.farm_hand_cost_mult);
    player->hires_today++;
    spawn = kg_spawn_hand(player, state->config.board_size);
    player->hands[player->hand_count] = spawn;
    player->units[player->unit_count].x = spawn.x;
    player->units[player->unit_count].y = spawn.y;
    player->unit_count++;
    player->hand_count++;
    kg_sync_public_positions(player);
}

KG_HD static void kg_do_buy_land(KGState* state, KGPlayer* player) {
    static const int prices[3] = {1000, 2000, 4000};
    static const int quadrants[3] = {2, 4, 8};
    int extra = __builtin_popcount((unsigned int)player->unlocked_mask) - 1;
    int x;
    int y;
    if (extra >= 3 || player->money < prices[extra]) {
        return;
    }
    player->money -= prices[extra];
    player->unlocked_mask |= quadrants[extra];
    for (y = 0; y < state->config.board_size; y++) {
        for (x = 0; x < state->config.board_size; x++) {
            int index = kg_tile_index(x, y);
            if (kg_quadrant(x, y, state->config.board_size) == quadrants[extra]
                    && player->tiles[index].kind == KG_TILE_LOCKED) {
                kg_set_player_tile(player, index, KG_TILE_EMPTY);
            }
        }
    }
}

/* kaggle-environments 1.32.3+: movement is bounds-only. Hands may spawn on
 * locked shed-access tiles; blocking LOCKED movement stranded them. Tile
 * operations still no-op on LOCKED (checked after the move switch). */
KG_HD static int kg_unit_can_move_to(const KGPlayer* player, int x, int y, int board_size) {
    (void)player;
    if (x < 0 || y < 0 || x >= board_size || y >= board_size) {
        return 0;
    }
    return 1;
}

KG_HD static void kg_drop_inventory(KGState* state, KGPlayer* player, KGUnitState* unit) {
    int i;
    int item;
    int count;
    int room;
    (void)state;
    for (i = 0; i < unit->inventory_order_count; i++) {
        item = unit->inventory_order[i];
        count = unit->inventory[item];
        if (count <= 0) {
            continue;
        }
        room = state->config.shed_capacity - kg_shed_total(player);
        if (room > 0) {
            int take = count < room ? count : room;
            player->shed[item] += take;
        }
        /* Official end-of-day overflow is discarded, including any remainder. */
        unit->inventory[item] = 0;
    }
    unit->inventory_order_count = 0;
}

KG_HD static void kg_apply_unit_action(KGState* state, KGPlayer* player, int idx,
        const KGUnitAction* action) {
    KGUnitState* unit;
    KGPosition pos;
    KGTile* tile;
    int x;
    int y;
    int player_id;
    if (action == NULL || idx < 0 || idx >= player->unit_count) {
        return;
    }
    unit = &player->units[idx];
    player_id = (int)(player - state->players);
    pos = kg_unit_position(player, idx);
    x = pos.x;
    y = pos.y;
    tile = &player->tiles[kg_tile_index(x, y)];

    switch (action->op) {
        case KG_OP_NORTH:
        case KG_OP_SOUTH:
        case KG_OP_EAST:
        case KG_OP_WEST: {
            static const int dx[4] = {0, 0, 1, -1};
            static const int dy[4] = {-1, 1, 0, 0};
            int move = action->op - KG_OP_NORTH;
            int nx = x + dx[move];
            int ny = y + dy[move];
            if (kg_unit_can_move_to(player, nx, ny, state->config.board_size)) {
                kg_set_unit_position(player, idx, nx, ny);
            }
            return;
        }
        case KG_OP_PASS:
            return;
        default:
            break;
    }

    if (action->op == KG_OP_DROP) {
        int i;
        if (!kg_is_shed_adjacent(&pos, state->config.board_size)) {
            return;
        }
        for (i = 0; i < unit->inventory_order_count; i++) {
            int item = unit->inventory_order[i];
            int count = unit->inventory[item];
            int room = state->config.shed_capacity - kg_shed_total(player);
            int take = count < room ? count : room;
            if (take > 0) {
                player->shed[item] += take;
            }
            unit->inventory[item] = 0;
        }
        unit->inventory_order_count = 0;
        return;
    }

    if (action->op == KG_OP_PICKUP) {
        int item = action->arg;
        int n = action->n > 0 ? action->n : 1;
        int available;
        if (!kg_is_shed_adjacent(&pos, state->config.board_size)
                || item < 0 || item >= KG_NUM_ITEMS) {
            return;
        }
        available = player->shed[item];
        if (n > available) n = available;
        if (n <= 0) return;
        player->shed[item] -= n;
        kg_inventory_add(unit, item, n);
        return;
    }

    if (tile->kind == KG_TILE_LOCKED) {
        return;
    }

    if (action->op == KG_OP_PLANT) {
        int crop = action->arg;
        if (crop < 0 || crop >= KG_NUM_CROPS || tile->kind != KG_TILE_EMPTY
                || player->seeds[crop] <= 0) {
            return;
        }
        player->seeds[crop]--;
        kg_new_plant(player, kg_tile_index(x, y), crop, state->day,
            state->config.turns_per_day);
        state->planted_crops[player_id]++;
        return;
    }

    if (action->op == KG_OP_WATER) {
        if (tile->kind != KG_TILE_PLANT || tile->watered_today) {
            return;
        }
        tile->watered_today = 1;
        if (!KG_CROP_DEFS[tile->crop].ongoing) {
            int age = state->day - tile->planted_day;
            int start = (KG_CROP_DEFS[tile->crop].max_yield_day + 1) / 2;
            if (age >= start && age <= KG_CROP_DEFS[tile->crop].max_yield_day) {
                int bonus = tile->fertilized_until_day >= state->day ? 2 : 1;
                tile->yield_units += bonus;
                if (tile->yield_units > KG_CROP_DEFS[tile->crop].max_yield) {
                    tile->yield_units = KG_CROP_DEFS[tile->crop].max_yield;
                }
            }
        }
        return;
    }

    if (action->op == KG_OP_HARVEST) {
        if (tile->yield_units <= 0) {
            return;
        }
        if (tile->kind == KG_TILE_PLANT) {
            const KGCropDef* crop = &KG_CROP_DEFS[tile->crop];
            int item;
            int units;
            if (state->day - tile->planted_day < crop->first_yield_day) {
                return;
            }
            item = kg_crop_product(tile->crop);
            units = tile->yield_units;
            state->production_units[player_id] += (uint32_t)units;
            state->production_value[player_id] +=
                (float)units * state->market.prices[item];
            state->production_product_units[player_id][item] +=
                (uint32_t)units;
            state->production_product_value[player_id][item] +=
                (float)units * state->market.prices[item];
            kg_inventory_add(unit, item, units);
            tile->yield_units = 0;
            if (!crop->ongoing) {
                kg_set_player_tile(player, kg_tile_index(x, y), KG_TILE_EMPTY);
            }
        } else if (tile->animal >= 0 && tile->animal < KG_NUM_ANIMALS) {
            int item = kg_animal_product(tile->animal);
            int units = tile->yield_units;
            state->production_units[player_id] += (uint32_t)units;
            state->production_value[player_id] +=
                (float)units * state->market.prices[item];
            state->production_product_units[player_id][item] +=
                (uint32_t)units;
            state->production_product_value[player_id][item] +=
                (float)units * state->market.prices[item];
            kg_inventory_add(unit, item, units);
            tile->yield_units = 0;
        }
        return;
    }

    if (action->op == KG_OP_FERTILIZE) {
        if (tile->kind != KG_TILE_PLANT || !kg_inventory_take(unit, KG_ITEM_FERTILIZER, 1)) {
            return;
        }
        if (tile->fertilized_until_day < state->day + 2) {
            tile->fertilized_until_day = state->day + 2;
        }
        return;
    }

    if (action->op == KG_OP_DIG) {
        if (tile->kind != KG_TILE_EMPTY && tile->kind != KG_TILE_LOCKED
                && !kg_is_animal_tile(tile)) {
            kg_set_player_tile(player, kg_tile_index(x, y), KG_TILE_EMPTY);
        }
        return;
    }

    if (action->op == KG_OP_BUILD_COOP || action->op == KG_OP_BUILD_PASTURE) {
        int desired = action->op == KG_OP_BUILD_COOP ? KG_TILE_COOP : KG_TILE_PASTURE;
        if (tile->kind == KG_TILE_EMPTY) {
            kg_set_player_tile(player, kg_tile_index(x, y), desired);
        }
        return;
    }

    if (action->op == KG_OP_PLACE) {
        int item = action->arg;
        if (item >= KG_ITEM_GOOSE && item <= KG_ITEM_SHEEP
                && tile->kind == KG_ANIMAL_DEFS[item - KG_ITEM_GOOSE].structure
                && tile->animal == KG_ANIMAL_INVALID) {
            int animal = item - KG_ITEM_GOOSE;
            if (kg_inventory_take(unit, item, 1)) {
                kg_new_animal(player, kg_tile_index(x, y), animal, state->day);
                state->placed_animals[player_id]++;
            }
            return;
        }
        if (kg_is_shed_adjacent(&pos, state->config.board_size)
                && item >= 0 && item < KG_NUM_ITEMS) {
            int n = action->n > 0 ? action->n : 1;
            int room = state->config.shed_capacity - kg_shed_total(player);
            if (n > unit->inventory[item]) n = unit->inventory[item];
            if (n > room) n = room;
            if (n > 0 && kg_inventory_take(unit, item, n)) {
                player->shed[item] += n;
            }
        }
        return;
    }

    if (action->op == KG_OP_FEED) {
        if (!kg_is_animal_tile(tile) || tile->fed_today) {
            return;
        }
        if (kg_inventory_take(unit, KG_ITEM_WHEAT, 1)) {
            tile->fed_today = 1;
        }
        return;
    }

    if (action->op == KG_OP_COLLECT_FERTILIZER) {
        if (!kg_is_animal_tile(tile) || !tile->fertilizer_available) {
            return;
        }
        tile->fertilizer_available = 0;
        kg_inventory_add(unit, KG_ITEM_FERTILIZER, 1);
        return;
    }

    if (action->op == KG_OP_CARE) {
        if (kg_is_animal_tile(tile) && !tile->cared_today) {
            tile->cared_today = 1;
        }
    }
}

KG_HD static void kg_daily_refresh_plants(KGState* state, KGPlayer* player, int current_day) {
    int next_day = current_day + 1;
    int player_id = (int)(player - state->players);
    for (int word = 0; word < KG_TILE_WORDS; word++) {
        uint64_t bits = player->plant_bits[word];
        while (bits != 0) {
            int index = word * 64 + kg_ctz64(bits);
            int x = index % KG_MAX_BOARD_SIZE;
            int y = index / KG_MAX_BOARD_SIZE;
            KGTile* tile;
            const KGCropDef* def;
            int days_since_first;
            int production_count;
            int fertilized;
            int was_watered;
            bits &= bits - 1;
            if (x >= state->config.board_size || y >= state->config.board_size) continue;
            tile = &player->tiles[index];
            if (tile->kind != KG_TILE_PLANT) continue;
            was_watered = tile->watered_today;
            state->plant_days[player_id]++;
            state->watered_plant_days[player_id] += was_watered != 0;
            if (was_watered) tile->consecutive_unwatered = 0;
            else tile->consecutive_unwatered++;
            tile->watered_today = 0;
            if (tile->consecutive_unwatered >= 2) {
                if (tile->planted_day == current_day) {
                    state->planting_day_deaths[player_id]++;
                } else {
                    state->neglect_deaths[player_id]++;
                }
                kg_set_player_tile(player, index, KG_TILE_WEED);
                continue;
            }
            def = &KG_CROP_DEFS[tile->crop];
            if (!def->ongoing) continue;
            days_since_first = next_day - tile->planted_day - def->first_yield_day;
            if (days_since_first < 0 || days_since_first % def->interval != 0) continue;
            production_count = days_since_first / def->interval + 1;
            if (production_count > def->max_yield) continue;
            fertilized = was_watered && tile->fertilized_until_day >= current_day;
            tile->yield_units += fertilized ? 2 : 1;
            if (tile->yield_units > def->max_yield) tile->yield_units = def->max_yield;
            if (production_count == def->max_yield) {
                tile->max_lifespan_step = (next_day + 1) * state->config.turns_per_day;
            }
        }
    }
}

KG_HD static void kg_daily_refresh_animals(KGState* state, KGPlayer* player, int day) {
    int next_day = day + 1;
    for (int word = 0; word < KG_TILE_WORDS; word++) {
        uint64_t bits = player->animal_bits[word];
        while (bits != 0) {
            int index = word * 64 + kg_ctz64(bits);
            int x = index % KG_MAX_BOARD_SIZE;
            int y = index / KG_MAX_BOARD_SIZE;
            KGTile* tile;
            const KGAnimalDef* def;
            int days_since_first;
            bits &= bits - 1;
            if (x >= state->config.board_size || y >= state->config.board_size) continue;
            tile = &player->tiles[index];
            if (!kg_is_animal_tile(tile)) continue;
            if (tile->fed_today) tile->consecutive_unfed = 0;
            else tile->consecutive_unfed++;
            if (tile->consecutive_unfed >= 2) {
                int structure = KG_ANIMAL_DEFS[tile->animal].structure;
                kg_set_player_tile(player, index, structure);
                continue;
            }
            def = &KG_ANIMAL_DEFS[tile->animal];
            days_since_first = next_day - tile->placed_day - def->first_yield_day;
            if (days_since_first >= 0 && days_since_first % def->interval == 0) {
                int bonus = tile->fed_today ? tile->pending_care_bonus : 0;
                tile->yield_units += 1 + bonus;
                if (tile->yield_units > def->max_held) {
                    tile->yield_units = def->max_held;
                }
                tile->pending_care_bonus = 0;
            }
            if (tile->cared_today && tile->fed_today) {
                tile->pending_care_bonus += 1;
            }
            tile->fertilizer_available = 1;
            tile->fed_today = 0;
            tile->cared_today = 0;
        }
    }
}

KG_HD static void kg_decay_plants(KGState* state, KGPlayer* player, int step) {
    for (int word = 0; word < KG_TILE_WORDS; word++) {
        uint64_t bits = player->plant_bits[word];
        while (bits != 0) {
            int index = word * 64 + kg_ctz64(bits);
            int x = index % KG_MAX_BOARD_SIZE;
            int y = index / KG_MAX_BOARD_SIZE;
            KGTile* tile;
            bits &= bits - 1;
            if (x >= state->config.board_size || y >= state->config.board_size) continue;
            tile = &player->tiles[index];
            if (tile->kind != KG_TILE_PLANT || tile->max_lifespan_step < 0
                    || step < tile->max_lifespan_step
                    || (step - tile->max_lifespan_step) % 2 != 0) continue;
            if (tile->yield_units > 0) tile->yield_units--;
            if (tile->yield_units == 0) {
                kg_set_player_tile(player, index, KG_TILE_WEED);
            }
        }
    }
}

KG_HD static void kg_spawn_weeds(KGState* state, KGPlayer* player, KGPythonRandom* rng) {
    int x;
    int y;
    for (y = 0; y < state->config.board_size; y++) {
        for (x = 0; x < state->config.board_size; x++) {
            KGTile* tile = &player->tiles[kg_tile_index(x, y)];
            if (tile->kind == KG_TILE_EMPTY) {
                double draw = kg_random_double(rng);
                if (draw < state->config.weed_spawn_chance) {
                    kg_set_player_tile(player, kg_tile_index(x, y), KG_TILE_WEED);
                }
            }
        }
    }
}

KG_HD static void kg_end_of_day(KGState* state, int current_day) {
    KGPythonRandom rng;
    int player_id;
    uint64_t day_seed = state->config.seed * 1000003ULL ^ (uint64_t)current_day;
    kg_random_seed(&rng, day_seed);
    for (player_id = 0; player_id < KG_NUM_PLAYERS; player_id++) {
        KGPlayer* player = &state->players[player_id];
        int i;
        kg_daily_refresh_plants(state, player, current_day);
        kg_daily_refresh_animals(state, player, current_day);
        kg_spawn_weeds(state, player, &rng);
        for (i = 0; i < player->unit_count; i++) {
            kg_drop_inventory(state, player, &player->units[i]);
        }
        {
            KGPosition spawn = kg_default_spawn(state->config.board_size);
            memset(&player->units[0], 0, sizeof(player->units[0]));
            player->units[0].x = spawn.x;
            player->units[0].y = spawn.y;
        }
        player->hand_count = 0;
        player->unit_count = 1;
        player->hires_today = 0;
        kg_sync_public_positions(player);
    }
    {
        int next_day = current_day + 1;
        if (next_day > 0 && next_day % state->config.town_shop_unlock_interval == 0
                && state->shop_count < KG_MAX_SHOPS) {
            int shop = kg_random_below(&rng, KG_MAX_SHOPS);
            state->unlocked_shops[state->shop_count++] = shop;
        }
    }
}

KG_HD static int kg_quote_valid(int op, int item) {
    if (op == KG_MARKET_SELL) return item >= 0 && item < KG_NUM_PRODUCTS;
    if (op == KG_MARKET_BUY_PRODUCT) return item == KG_ITEM_WHEAT || item == KG_ITEM_FERTILIZER;
    if (op == KG_MARKET_BUY_SEED) return item >= 0 && item < KG_NUM_CROPS;
    if (op == KG_MARKET_BUY_ANIMAL) return item >= KG_ITEM_GOOSE && item <= KG_ITEM_SHEEP;
    return 0;
}

KG_HD static int kg_commit_unit(KGState* state, int player_id, int op, int item, int price) {
    KGPlayer* player = &state->players[player_id];
    if (op == KG_MARKET_SELL) {
        if (player->shed[item] <= 0) return 0;
        player->shed[item]--;
        player->money += price;
        state->sold_units[player_id]++;
        state->sales_revenue[player_id] += (float)price;
        state->sold_product_units[player_id][item]++;
        state->sold_product_revenue[player_id][item] += (float)price;
        if (price > 1) state->market.inventory[item]++;
        return 1;
    }
    if (op == KG_MARKET_BUY_PRODUCT) {
        if (player->money < price
                || kg_shed_total(player) >= state->config.shed_capacity) return 0;
        player->money -= price;
        player->shed[item]++;
        state->bought_units[player_id]++;
        state->purchase_spend[player_id] += (float)price;
        state->market.inventory[item]--;
        return 1;
    }
    if (op == KG_MARKET_BUY_SEED) {
        if (player->money < price) return 0;
        player->money -= price;
        player->seeds[item]++;
        state->bought_units[player_id]++;
        state->purchase_spend[player_id] += (float)price;
        return 1;
    }
    if (op == KG_MARKET_BUY_ANIMAL) {
        if (player->money < price
                || kg_shed_total(player) >= state->config.shed_capacity) return 0;
        player->money -= price;
        player->shed[item]++;
        state->bought_units[player_id]++;
        state->purchase_spend[player_id] += (float)price;
        return 1;
    }
    return 0;
}

KG_HD static void kg_process_market(KGState* state, const KGAction actions[KG_NUM_PLAYERS]) {
    int max_orders = state->config.max_market_orders_per_turn;
    int max_len = 0;
    int i;
    for (i = 0; i < KG_NUM_PLAYERS; i++) {
        int count = actions[i].market_count;
        if (count > max_orders) count = max_orders;
        if (count > max_len) max_len = count;
    }
    for (i = 0; i < max_len; i++) {
        int active[KG_NUM_PLAYERS] = {0, 0};
        KGMarketOrder orders[KG_NUM_PLAYERS];
        int market_changed = 0;
        int player_id;
        for (player_id = 0; player_id < KG_NUM_PLAYERS; player_id++) {
            int count = actions[player_id].market_count;
            if (count > max_orders) count = max_orders;
            if (i >= count) continue;
            orders[player_id] = actions[player_id].market[i];
            if (orders[player_id].op == KG_MARKET_HIRE) {
                kg_do_hire(state, &state->players[player_id]);
            } else if (orders[player_id].op == KG_MARKET_BUY_LAND) {
                kg_do_buy_land(state, &state->players[player_id]);
            } else if (orders[player_id].n > 0
                    && kg_quote_valid(orders[player_id].op, orders[player_id].item)) {
                active[player_id] = 1;
            }
        }
        for (;;) {
            int committed_any = 0;
            int quoted[KG_NUM_PLAYERS] = {0, 0};
            for (player_id = 0; player_id < KG_NUM_PLAYERS; player_id++) {
                if (!active[player_id] || orders[player_id].n <= 0) continue;
                if (orders[player_id].op == KG_MARKET_SELL) {
                    quoted[player_id] = kg_market_price(orders[player_id].item,
                        state->market.inventory[orders[player_id].item]);
                } else if (orders[player_id].op == KG_MARKET_BUY_PRODUCT) {
                    quoted[player_id] = kg_market_price(orders[player_id].item,
                        state->market.inventory[orders[player_id].item] - 1);
                } else if (orders[player_id].op == KG_MARKET_BUY_SEED) {
                    quoted[player_id] = KG_CROP_DEFS[orders[player_id].item].seed_cost;
                } else if (orders[player_id].op == KG_MARKET_BUY_ANIMAL) {
                    quoted[player_id] = KG_ANIMAL_DEFS[orders[player_id].item - KG_ITEM_GOOSE].cost;
                }
            }
            for (player_id = 0; player_id < KG_NUM_PLAYERS; player_id++) {
                if (!active[player_id] || orders[player_id].n <= 0) continue;
                if (kg_commit_unit(state, player_id, orders[player_id].op,
                        orders[player_id].item, quoted[player_id])) {
                    orders[player_id].n--;
                    if (orders[player_id].op == KG_MARKET_SELL
                            || orders[player_id].op == KG_MARKET_BUY_PRODUCT) {
                        market_changed = 1;
                    }
                    committed_any = 1;
                } else {
                    active[player_id] = 0;
                }
            }
            if (!committed_any) break;
        }
        if (market_changed) kg_refresh_prices(state);
    }
}

KG_HD static void kg_town_consume(KGState* state, int step) {
    int shop;
    int j;
    int market_changed = 0;
    if (step % state->config.town_shop_sell_interval == 0) {
        for (shop = 0; shop < state->shop_count; shop++) {
            int id = state->unlocked_shops[shop];
            int products = 0;
            while (products < KG_NUM_PRODUCTS && kg_shop_product(id, products) >= 0) products++;
            int multiplier = products == 1 ? 2 : 1;
            for (j = 0; j < products; j++) {
                int product = kg_shop_product(id, j);
                state->market.inventory[product] -= multiplier;
                state->exogenous_demand_units[product] += (uint32_t)multiplier;
            }
            market_changed = 1;
        }
    }
    if (step % state->config.town_center_sell_interval == 0) {
        for (j = 0; j < KG_NUM_PRODUCTS - 1; j++) {
            state->market.inventory[j] -= 1;
            state->exogenous_demand_units[j]++;
        }
        market_changed = 1;
    }
    if (market_changed) kg_refresh_prices(state);
}

KG_HD static void kg_validate_plant_atomic(const KGAction* action, const KGPlayer* player,
        int blocked[KG_NUM_CROPS]) {
    int demand[KG_NUM_CROPS] = {0, 0, 0, 0, 0};
    int i;
    memset(blocked, 0, sizeof(int) * KG_NUM_CROPS);
    if (action->farmer.op == KG_OP_PLANT && action->farmer.arg >= 0
            && action->farmer.arg < KG_NUM_CROPS) {
        demand[action->farmer.arg]++;
    }
    for (i = 0; i < action->hand_count && i < KG_MAX_HANDS; i++) {
        if (action->hands[i].op == KG_OP_PLANT && action->hands[i].arg >= 0
                && action->hands[i].arg < KG_NUM_CROPS) {
            demand[action->hands[i].arg]++;
        }
    }
    for (i = 0; i < KG_NUM_CROPS; i++) {
        if (demand[i] > player->seeds[i]) blocked[i] = 1;
    }
}

KG_HD void kg_step(KGState* state, const KGAction actions[KG_NUM_PLAYERS]) {
    int player_id;
    int old_step;
    int old_day;
    if (state == NULL || actions == NULL || state->done) return;
    old_step = state->step;
    old_day = state->day;
    for (player_id = 0; player_id < KG_NUM_PLAYERS; player_id++) {
        KGPlayer* player = &state->players[player_id];
        int blocked[KG_NUM_CROPS];
        int i;
        kg_validate_plant_atomic(&actions[player_id], player, blocked);
        {
            KGUnitAction farmer = actions[player_id].farmer;
            if (!(farmer.op == KG_OP_PLANT && farmer.arg >= 0 && farmer.arg < KG_NUM_CROPS
                    && blocked[farmer.arg])) {
                kg_apply_unit_action(state, player, 0, &farmer);
            }
        }
        for (i = 0; i < actions[player_id].hand_count && i < player->hand_count
                && i < KG_MAX_HANDS; i++) {
            KGUnitAction hand = actions[player_id].hands[i];
            if (!(hand.op == KG_OP_PLANT && hand.arg >= 0 && hand.arg < KG_NUM_CROPS
                    && blocked[hand.arg])) {
                kg_apply_unit_action(state, player, i + 1, &hand);
            }
        }
    }
    kg_process_market(state, actions);
    kg_town_consume(state, old_step);
    for (player_id = 0; player_id < KG_NUM_PLAYERS; player_id++) {
        kg_decay_plants(state, &state->players[player_id], old_step);
    }
    if ((old_step + 1) % state->config.turns_per_day == 0) {
        kg_end_of_day(state, old_day);
    }
    state->step = old_step + 1;
    state->day = state->step / state->config.turns_per_day;
    state->hour = state->step % state->config.turns_per_day;
    if (old_step >= state->config.episode_steps - 2) {
        state->done = 1;
    }
}

static void kg_json_init(char** data, size_t* length, size_t* capacity) {
    *capacity = 4096;
    *length = 0;
    *data = (char*)malloc(*capacity);
    if (*data != NULL) (*data)[0] = 0;
}

static void kg_json_append(char** data, size_t* length, size_t* capacity,
        const char* format, ...) {
    va_list args;
    int needed;
    if (*data == NULL) return;
    va_start(args, format);
    needed = vsnprintf(*data + *length, *capacity - *length, format, args);
    va_end(args);
    if (needed < 0) return;
    if ((size_t)needed + *length >= *capacity) {
        size_t new_capacity = *capacity;
        while ((size_t)needed + *length >= new_capacity) new_capacity *= 2;
        *data = (char*)realloc(*data, new_capacity);
        if (*data == NULL) return;
        *capacity = new_capacity;
        va_start(args, format);
        vsnprintf(*data + *length, *capacity - *length, format, args);
        va_end(args);
    }
    *length += (size_t)needed;
}

static void kg_json_tile(char** data, size_t* length, size_t* capacity, const KGTile* tile) {
    int kind = tile->kind;
    if (kind == KG_TILE_EMPTY) {
        kg_json_append(data, length, capacity, "null");
    } else if (kind == KG_TILE_LOCKED) {
        kg_json_append(data, length, capacity, "\"LOCKED\"");
    } else if (kind == KG_TILE_WEED) {
        kg_json_append(data, length, capacity, "{\"kind\":\"WEED\"}");
    } else if ((kind == KG_TILE_COOP || kind == KG_TILE_PASTURE) && tile->animal < 0) {
        kg_json_append(data, length, capacity, "{\"kind\":\"%s\"}",
            kind == KG_TILE_COOP ? "COOP" : "PASTURE");
    } else if (kind == KG_TILE_PLANT) {
        kg_json_append(data, length, capacity,
            "{\"kind\":\"PLANT\",\"crop\":\"%s\",\"planted_day\":%d,"
            "\"watered_today\":%s,\"consecutive_unwatered\":%d,\"yield_units\":%d,"
            "\"max_lifespan_step\":%d,\"fertilized_until_day\":%d}",
            kg_crop_name(tile->crop), tile->planted_day, tile->watered_today ? "true" : "false",
            tile->consecutive_unwatered, tile->yield_units, tile->max_lifespan_step,
            tile->fertilized_until_day);
    } else if (kg_is_animal_tile(tile)) {
        kg_json_append(data, length, capacity,
            "{\"kind\":\"%s\",\"animal\":\"%s\",\"placed_day\":%d,"
            "\"yield_units\":%d,\"consecutive_unfed\":%d,\"fed_today\":%s,"
            "\"cared_today\":%s,\"fertilizer_available\":%s,\"pending_care_bonus\":%d}",
            tile->kind == KG_TILE_COOP ? "COOP" : "PASTURE", kg_animal_name(tile->animal),
            tile->placed_day, tile->yield_units, tile->consecutive_unfed,
            tile->fed_today ? "true" : "false", tile->cared_today ? "true" : "false",
            tile->fertilizer_available ? "true" : "false", tile->pending_care_bonus);
    } else {
        kg_json_append(data, length, capacity, "null");
    }
}

static void kg_json_player(char** data, size_t* length, size_t* capacity,
        const KGState* state, int player_id) {
    const KGPlayer* player = &state->players[player_id];
    int x;
    int y;
    int i;
    kg_json_append(data, length, capacity, "{\"money\":%d.0,\"tiles\":[", player->money);
    for (y = 0; y < state->config.board_size; y++) {
        if (y) kg_json_append(data, length, capacity, ",");
        kg_json_append(data, length, capacity, "[");
        for (x = 0; x < state->config.board_size; x++) {
            if (x) kg_json_append(data, length, capacity, ",");
            kg_json_tile(data, length, capacity, &player->tiles[kg_tile_index(x, y)]);
        }
        kg_json_append(data, length, capacity, "]");
    }
    kg_json_append(data, length, capacity, "],\"farmer\":[%d,%d],\"hands\":[",
        player->farmer.x, player->farmer.y);
    for (i = 0; i < player->hand_count; i++) {
        if (i) kg_json_append(data, length, capacity, ",");
        kg_json_append(data, length, capacity, "[%d,%d]", player->hands[i].x, player->hands[i].y);
    }
    kg_json_append(data, length, capacity, "],\"unlocked_quadrants\":[");
    {
        static const int bits[4] = {1, 2, 4, 8};
        static const char* names[4] = {"NW", "NE", "SW", "SE"};
        int first = 1;
        for (i = 0; i < 4; i++) {
            if ((player->unlocked_mask & bits[i]) != 0) {
                if (!first) kg_json_append(data, length, capacity, ",");
                kg_json_append(data, length, capacity, "\"%s\"", names[i]);
                first = 0;
            }
        }
    }
    kg_json_append(data, length, capacity, "],\"hires_today\":%d}", player->hires_today);
}

static void kg_json_private(char** data, size_t* length, size_t* capacity,
        const KGPlayer* player) {
    int i;
    kg_json_append(data, length, capacity, "{\"shed\":{");
    for (i = 0; i < KG_NUM_ITEMS; i++) {
        if (i) kg_json_append(data, length, capacity, ",");
        kg_json_append(data, length, capacity, "\"%s\":%d", KG_ITEM_NAMES[i], player->shed[i]);
    }
    kg_json_append(data, length, capacity, "},\"seeds\":{");
    for (i = 0; i < KG_NUM_CROPS; i++) {
        if (i) kg_json_append(data, length, capacity, ",");
        kg_json_append(data, length, capacity, "\"%s\":%d", KG_CROP_NAMES[i], player->seeds[i]);
    }
    kg_json_append(data, length, capacity, "},\"inventories\":[");
    for (i = 0; i < player->unit_count; i++) {
        int j;
        if (i) kg_json_append(data, length, capacity, ",");
        kg_json_append(data, length, capacity, "{");
        for (j = 0; j < player->units[i].inventory_order_count; j++) {
            int item = player->units[i].inventory_order[j];
            if (j) kg_json_append(data, length, capacity, ",");
            kg_json_append(data, length, capacity, "\"%s\":%d", KG_ITEM_NAMES[item],
                player->units[i].inventory[item]);
        }
        kg_json_append(data, length, capacity, "}");
    }
    kg_json_append(data, length, capacity, "]}");
}

const char* kg_snapshot_json(const KGState* state) {
    char* data;
    size_t length;
    size_t capacity;
    int i;
    if (state == NULL) return NULL;
    kg_json_init(&data, &length, &capacity);
    kg_json_append(&data, &length, &capacity, "{\"step\":%d,\"day\":%d,\"hour\":%d,\"done\":%s,\"farms\":[",
        state->step, state->day, state->hour, state->done ? "true" : "false");
    for (i = 0; i < KG_NUM_PLAYERS; i++) {
        if (i) kg_json_append(&data, &length, &capacity, ",");
        kg_json_player(&data, &length, &capacity, state, i);
    }
    kg_json_append(&data, &length, &capacity, "],\"privates\":[");
    for (i = 0; i < KG_NUM_PLAYERS; i++) {
        if (i) kg_json_append(&data, &length, &capacity, ",");
        kg_json_private(&data, &length, &capacity, &state->players[i]);
    }
    kg_json_append(&data, &length, &capacity, "],\"market\":{\"inventory\":{");
    for (i = 0; i < KG_NUM_PRODUCTS; i++) {
        if (i) kg_json_append(&data, &length, &capacity, ",");
        kg_json_append(&data, &length, &capacity, "\"%s\":%d", KG_PRODUCT_NAMES[i],
            state->market.inventory[i]);
    }
    kg_json_append(&data, &length, &capacity, "},\"prices\":{");
    for (i = 0; i < KG_NUM_PRODUCTS; i++) {
        if (i) kg_json_append(&data, &length, &capacity, ",");
        kg_json_append(&data, &length, &capacity, "\"%s\":%d", KG_PRODUCT_NAMES[i],
            state->market.prices[i]);
    }
    kg_json_append(&data, &length, &capacity, "}},\"town\":{\"unlocked_shops\":[");
    for (i = 0; i < state->shop_count; i++) {
        if (i) kg_json_append(&data, &length, &capacity, ",");
        kg_json_append(&data, &length, &capacity, "\"%s\"", KG_SHOP_NAMES[state->unlocked_shops[i]]);
    }
    kg_json_append(&data, &length, &capacity, "]}}");
    return data;
}

void kg_free_string(const char* value) {
    free((void*)value);
}

int kg_crop_from_name(const char* name) {
    int i;
    for (i = 0; i < KG_NUM_CROPS; i++) if (name && strcmp(name, KG_CROP_NAMES[i]) == 0) return i;
    return KG_CROP_INVALID;
}

int kg_animal_from_name(const char* name) {
    int i;
    for (i = 0; i < KG_NUM_ANIMALS; i++) if (name && strcmp(name, KG_ANIMAL_NAMES[i]) == 0) return i;
    return KG_ANIMAL_INVALID;
}

int kg_item_from_name(const char* name) {
    int i;
    for (i = 0; i < KG_NUM_ITEMS; i++) if (name && strcmp(name, KG_ITEM_NAMES[i]) == 0) return i;
    return KG_ITEM_INVALID;
}

int kg_shop_from_name(const char* name) {
    int i;
    for (i = 0; i < KG_MAX_SHOPS; i++) if (name && strcmp(name, KG_SHOP_NAMES[i]) == 0) return i;
    return -1;
}

const char* kg_crop_name(int crop) {
    return crop >= 0 && crop < KG_NUM_CROPS ? KG_CROP_NAMES[crop] : "";
}

const char* kg_animal_name(int animal) {
    return animal >= 0 && animal < KG_NUM_ANIMALS ? KG_ANIMAL_NAMES[animal] : "";
}

const char* kg_item_name(int item) {
    return item >= 0 && item < KG_NUM_ITEMS ? KG_ITEM_NAMES[item] : "";
}

const char* kg_shop_name(int shop) {
    return shop >= 0 && shop < KG_MAX_SHOPS ? KG_SHOP_NAMES[shop] : "";
}
