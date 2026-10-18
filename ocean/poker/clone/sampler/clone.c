#include "clone.h"
#include <math.h>
#include <stdatomic.h>
#include <string.h>
#include "bc_sha256.h"

/* Header is supplied as private build input, never as published learned data. */
#ifndef BC_MODEL_HEADER
#define BC_MODEL_HEADER "bc_model_generated.h"
#endif
#include BC_MODEL_HEADER

/* V1 headers may be compiled only as explicit rejection fixtures. Missing
 * v2 tags describe an incompatible model and cannot enter a learned draw. */
#ifndef BC_MODEL_V2_TAGS
#define BC_MODEL_V2_TAGS 0
#define BC_MODEL_SIZE_DOMAIN 0
#define BC_MODEL_DOMAIN_TAG "unversioned"
#define BC_MODEL_SIZE_ENCODING "unversioned"
static const uint16_t bc_model_version_data[1] = {BC_MODEL_VERSION};
static const uint16_t bc_size_domain_data[1] = {0};
#endif

#define BC_FEATURE_CONTRACT_SHA256 "0c998b63b609e1525628b27face7cbbf97bb76396f6060874429ee1dd57cdb36"

_Static_assert(sizeof(BcInput) == 60 * sizeof(int64_t), "BcInput canonical ABI");
_Static_assert(sizeof(double) == sizeof(uint64_t), "model binary64 encoding");

static atomic_int bc_checked_model = 0;

static int bc_verify_model(void) {
    if (BC_MODEL_VERSION != 2 || BC_MODEL_SIZE_DOMAIN != 2 || BC_MODEL_V2_TAGS != 1
        || sizeof(bc_model_version_data) != sizeof(uint16_t) || sizeof(bc_size_domain_data) != sizeof(uint16_t)
        || bc_model_version_data[0] != 2 || bc_size_domain_data[0] != 2
        || strcmp(BC_MODEL_DOMAIN_TAG, "behavior-clone-full-model-v2")
        || strcmp(BC_MODEL_SIZE_ENCODING, "legal_interior_fraction_f64_v2")
        || BC_MODEL_NODE_COUNT < 1 || BC_MODEL_NODE_COUNT > 4064
        || BC_MODEL_TREE_COUNT != 32 || BC_MODEL_FEATURE_COUNT != 64 || BC_MODEL_CELL_COUNT != 480)
        return 0;
    if (sizeof(bc_roots) != 32 * sizeof(int16_t)
        || sizeof(bc_feature) != BC_MODEL_NODE_COUNT * sizeof(int16_t)
        || sizeof(bc_threshold) != BC_MODEL_NODE_COUNT * sizeof(double)
        || sizeof(bc_left) != sizeof(bc_feature) || sizeof(bc_right) != sizeof(bc_feature)
        || sizeof(bc_counts) != BC_MODEL_NODE_COUNT * 5 * sizeof(double)
        || sizeof(bc_quantiles) != BC_MODEL_NODE_COUNT * 8 * sizeof(double)
        || sizeof(bc_support) != 480 || bc_model_numeric_bytes() > 524288) return 0;
    for (int node = 0; node < BC_MODEL_NODE_COUNT; node++) {
        if (!isfinite(bc_threshold[node])) return 0;
        double total = 2.5;
        for (int k = 0; k < 5; k++) {
            double x = bc_counts[5 * node + k];
            if (!isfinite(x) || x < 0) return 0;
            total += x;
        }
        if (!isfinite(total) || total <= 0) return 0;
        for (int k = 0; k < 8; k++) {
            double x = bc_quantiles[8 * node + k];
            if (!isfinite(x) || x < 0 || x > 1
                || (k > 0 && bc_quantiles[8 * node + k - 1] > x)) return 0;
        }
    }
    unsigned char seen[BC_MODEL_NODE_COUNT];
    memset(seen, 0, sizeof(seen));
    int visited = 0;
    for (int tree = 0; tree < 32; tree++) {
        int stack[128], depth[128], length = 1;
        stack[0] = bc_roots[tree];depth[0] = 0;
        while (length) {
            int at = --length, node = stack[at], level = depth[at];
            if (node < 0 || node >= BC_MODEL_NODE_COUNT || level > 6 || seen[node]) return 0;
            seen[node] = 1;visited++;
            if (bc_feature[node] == -1) {
                if (bc_left[node] != -1 || bc_right[node] != -1) return 0;
            } else {
                if (bc_feature[node] < 0 || bc_feature[node] >= 64 || length > 125) return 0;
                stack[length] = bc_left[node];depth[length++] = level + 1;
                stack[length] = bc_right[node];depth[length++] = level + 1;
            }
        }
    }
    if (visited != BC_MODEL_NODE_COUNT) return 0;
    for (int cell = 0; cell < 480; cell++) if (bc_support[cell] > 1) return 0;
    BcSha256 hash;
    bc_hash_sha_domain(&hash, "behavior-clone-full-model-v2");
    bc_hash_sha_u64(&hash, 2);bc_hash_sha_u64(&hash, BC_MODEL_NODE_COUNT);
    bc_hash_sha_u64(&hash, 32);bc_hash_sha_u64(&hash, 64);bc_hash_sha_u64(&hash, 480);
    bc_hash_sha_u64(&hash, bc_model_version_data[0]);bc_hash_sha_u64(&hash, bc_size_domain_data[0]);
    for (int i = 0; i < 32; i++) bc_hash_sha_u64(&hash, (uint64_t)(int64_t)bc_roots[i]);
    for (int i = 0; i < BC_MODEL_NODE_COUNT; i++) bc_hash_sha_u64(&hash, (uint64_t)(int64_t)bc_feature[i]);
    for (int i = 0; i < BC_MODEL_NODE_COUNT; i++) bc_hash_sha_double(&hash, bc_threshold[i]);
    for (int i = 0; i < BC_MODEL_NODE_COUNT; i++) bc_hash_sha_u64(&hash, (uint64_t)(int64_t)bc_left[i]);
    for (int i = 0; i < BC_MODEL_NODE_COUNT; i++) bc_hash_sha_u64(&hash, (uint64_t)(int64_t)bc_right[i]);
    for (int i = 0; i < BC_MODEL_NODE_COUNT * 5; i++) bc_hash_sha_double(&hash, bc_counts[i]);
    for (int i = 0; i < BC_MODEL_NODE_COUNT * 8; i++) bc_hash_sha_double(&hash, bc_quantiles[i]);
    for (int i = 0; i < 480; i++) bc_hash_sha_u64(&hash, bc_support[i]);
    unsigned char result[32];
    bc_hash_sha_finish(&hash, result);
    return !memcmp(result, bc_model_digest, 32);
}

int bc_model_validate(void) {
    int state = atomic_load_explicit(&bc_checked_model, memory_order_acquire);
    if (state) return state == 1;
    int valid = bc_verify_model();
    atomic_store_explicit(&bc_checked_model, valid ? 1 : -1, memory_order_release);
    return valid;
}
unsigned bc_runtime_abi_version(void) { return BC_RUNTIME_ABI_VERSION; }
size_t bc_input_bytes(void) { return sizeof(BcInput); }
unsigned bc_model_version(void) { return bc_model_version_data[0]; }
unsigned bc_model_domain_version(void) { return bc_size_domain_data[0]; }
const char *bc_model_domain(void) { return BC_MODEL_DOMAIN_TAG; }
const char *bc_size_encoding(void) { return BC_MODEL_SIZE_ENCODING; }
const char *bc_feature_contract_sha256(void) { return BC_FEATURE_CONTRACT_SHA256; }
const char *bc_model_npz_sha256(void) { return BC_MODEL_NPZ_SHA256; }
const char *bc_model_numeric_sha256(void) { return BC_MODEL_NUMERIC_SHA256; }
int bc_model_nodes(void) { return BC_MODEL_NODE_COUNT; }
size_t bc_model_numeric_bytes(void) {
    return sizeof(bc_model_version_data) + sizeof(bc_size_domain_data)
        + sizeof(bc_roots) + sizeof(bc_feature) + sizeof(bc_threshold) + sizeof(bc_left)
        + sizeof(bc_right) + sizeof(bc_counts) + sizeof(bc_quantiles) + sizeof(bc_support);
}

typedef struct BcDerived {
    int64_t call, denominator, maximum, minimum, opponent_cover, spr_numerator;
    int can_raise, cell;
} BcDerived;

static int bc_between(int64_t value, int64_t lower, int64_t upper) {
    return value >= lower && value <= upper;
}
static int64_t bc_min(int64_t a, int64_t b) { return a < b ? a : b; }
static int64_t bc_max(int64_t a, int64_t b) { return a > b ? a : b; }

static int bc_prepare(const BcInput *s, BcDerived *d) {
    if (!s || !d || s->abi_version != 1 || s->players != 2 || s->actor != 0
        || !bc_between(s->street, 0, 2) || !bc_between(s->actor_button, 0, 1)
        || !bc_between(s->pot, 0, 40000) || !bc_between(s->high, 0, 20000)
        || !bc_between(s->increment, 1, 20000) || !bc_between(s->raises, 0, 4)
        || !bc_between(s->last_raiser, -1, 1) || !bc_between(s->decisions, 0, 65535)
        || s->history_count != bc_min(s->decisions, 4)
        || s->board_count != (s->street == 0 ? 0 : s->street + 2)) return 0;
    for (int p = 0; p < 2; p++) {
        if (!bc_between(s->stack[p], 0, 20000) || !bc_between(s->initial[p], 0, 20000)
            || !bc_between(s->total[p], 0, 20000) || !bc_between(s->bet[p], 0, 20000)
            || !bc_between(s->reopen[p], 0, 40000) || s->live[p] != 1
            || !bc_between(s->active[p], 0, 1) || !bc_between(s->pending[p], 0, 1)) return 0;
        if (s->bet[p] > s->total[p] || s->stack[p] + s->total[p] != s->initial[p]
            || (s->pending[p] && !s->active[p])) return 0;
        for (int street = 0; street < 3; street++) {
            if (!bc_between(s->line[p][street], 0, 3)
                || (street > s->street && s->line[p][street] != 0)) return 0;
        }
    }
    if (!s->active[0] || !s->stack[0] || !s->pending[0]
        || s->active[1] != (s->stack[1] > 0) || s->line[0][s->street] != 0
        || s->pot != s->total[0] + s->total[1] || s->high != bc_max(s->bet[0], s->bet[1])) return 0;
    for (int i = 0; i < s->history_count; i++) {
        const BcEvent *e = s->history + i;
        if (!bc_between(e->seat, 0, 1) || !bc_between(e->street, 0, s->street)
            || !bc_between(e->kind, 1, 3) || !bc_between(e->bet, 0, 20000)) return 0;
    }
    for (int i = (int)s->history_count; i < 4; i++) {
        const BcEvent *e = s->history + i;
        if (e->seat || e->street || e->kind || e->bet) return 0;
    }
    uint64_t occupied = 0;
    for (int i = 0; i < 2; i++) {
        if (!bc_between(s->own[i], 0, 51) || (occupied & (UINT64_C(1) << s->own[i]))) return 0;
        occupied |= UINT64_C(1) << s->own[i];
    }
    for (int i = 0; i < 4; i++) {
        if (i >= s->board_count) {
            if (s->board[i] != -1) return 0;
        } else {
            if (!bc_between(s->board[i], 0, 51) || (occupied & (UINT64_C(1) << s->board[i]))) return 0;
            occupied |= UINT64_C(1) << s->board[i];
        }
    }
    d->call = bc_min(s->high - s->bet[0], s->stack[0]);
    d->denominator = bc_max(1, s->pot + d->call);
    d->maximum = s->bet[0] + s->stack[0];
    d->opponent_cover = s->bet[1] + s->stack[1];
    d->can_raise = s->high >= s->reopen[0] && d->maximum > s->high
        && s->active[1] && d->opponent_cover > s->high;
    d->minimum = d->can_raise ? bc_min(s->high + s->increment, d->maximum) : 0;
    d->spr_numerator = bc_min(s->stack[0] - d->call, s->stack[1]);
    int price = !d->call ? 0 : 100 * d->call <= 20 * d->denominator ? 1
        : 100 * d->call <= 33 * d->denominator ? 2 : 2 * d->call <= d->denominator ? 3 : 4;
    int spr = 2 * d->spr_numerator <= d->denominator ? 0 : d->spr_numerator <= 2 * d->denominator ? 1
        : d->spr_numerator <= 8 * d->denominator ? 2 : 3;
    d->cell = (((((int)s->street * 2 + (int)s->actor_button) * 2 + (d->call > 0)) * 2
        + d->can_raise) * 5 + price) * 4 + spr;
    return 1;
}

static int bc_scope(const BcInput *s) {
    return s && s->abi_version == 1 && s->players == 2 && s->policy == 7 && s->samples == 0
        && s->has_profiles == 0 && s->initial[0] == 20000 && s->initial[1] == 20000;
}

static unsigned bc_popcount(unsigned value) { return (unsigned)__builtin_popcount(value); }
static int bc_straight(unsigned ranks) {
    for (int high = 12; high >= 4; high--) {
        unsigned window = 31u << (high - 4);
        if ((ranks & window) == window) return high + 2;
    }
    return (ranks & 0x100fu) == 0x100fu ? 5 : 0;
}
static int bc_straight_window(unsigned ranks) {
    unsigned best = bc_popcount(ranks & 0x100fu);
    for (int low = 0; low < 9; low++) {
        unsigned count = bc_popcount(ranks & (31u << low));
        if (count > best) best = count;
    }
    return (int)best;
}
static uint32_t bc_pack_top(unsigned mask, int count) {
    uint32_t packed = 0;
    int shift = 16;
    for (int rank = 12; rank >= 0 && count; rank--) if (mask & (1u << rank)) {
        packed |= (uint32_t)(rank + 2) << shift;
        shift -= 4;count--;
    }
    return packed;
}
static uint32_t bc_rank(const int count[13], const unsigned suits[4], unsigned ranks) {
    unsigned flush = 0;
    for (int suit = 0; suit < 4; suit++) if (bc_popcount(suits[suit]) >= 5) {
        flush = suits[suit];break;
    }
    int straight = bc_straight(flush);
    if (straight) return (8u << 24) | ((uint32_t)straight << 16);
    int quad = -1, trip = -1, first_pair = -1, second_pair = -1;
    for (int rank = 12; rank >= 0; rank--) {
        if (count[rank] == 4 && quad < 0) quad = rank;
        if (count[rank] >= 3 && trip < 0) trip = rank;
        if (count[rank] >= 2) {
            if (first_pair < 0) first_pair = rank;
            else if (second_pair < 0) second_pair = rank;
        }
    }
    if (quad >= 0) return (7u << 24) | ((uint32_t)(quad + 2) << 16)
        | (bc_pack_top(ranks & ~(1u << quad), 1) >> 4);
    if (trip >= 0) {
        int pair = first_pair == trip ? second_pair : first_pair;
        if (pair >= 0) return (6u << 24) | ((uint32_t)(trip + 2) << 16) | ((uint32_t)(pair + 2) << 12);
    }
    if (flush) return (5u << 24) | bc_pack_top(flush, 5);
    straight = bc_straight(ranks);
    if (straight) return (4u << 24) | ((uint32_t)straight << 16);
    if (trip >= 0) return (3u << 24) | ((uint32_t)(trip + 2) << 16)
        | (bc_pack_top(ranks & ~(1u << trip), 2) >> 4);
    if (second_pair >= 0) return (2u << 24) | ((uint32_t)(first_pair + 2) << 16)
        | ((uint32_t)(second_pair + 2) << 12)
        | (bc_pack_top(ranks & ~(1u << first_pair) & ~(1u << second_pair), 1) >> 8);
    if (first_pair >= 0) return (1u << 24) | ((uint32_t)(first_pair + 2) << 16)
        | (bc_pack_top(ranks & ~(1u << first_pair), 3) >> 4);
    return bc_pack_top(ranks, 5);
}

static void bc_fill_features(const BcInput *s, const BcDerived *d, double f[64]) {
    f[0] = s->street / 2.0;f[1] = (double)s->actor_button;f[2] = s->pot / 40000.0;
    f[3] = s->stack[0] / 20000.0;f[4] = s->stack[1] / 20000.0;
    f[5] = s->bet[0] / 20000.0;f[6] = s->bet[1] / 20000.0;
    f[7] = s->total[0] / 20000.0;f[8] = s->total[1] / 20000.0;
    f[9] = s->high / 20000.0;f[10] = s->increment / 20000.0;f[11] = d->call / 20000.0;
    f[12] = (double)d->call / d->denominator;
    double spr = (double)d->spr_numerator / d->denominator;
    f[13] = (spr < 16.0 ? spr : 16.0) / 16.0;
    f[14] = d->can_raise;f[15] = d->minimum / 20000.0;
    f[16] = d->can_raise ? d->maximum / 20000.0 : 0.0;
    f[17] = d->opponent_cover / 20000.0;f[18] = s->raises / 4.0;
    f[19] = (s->last_raiser + 1) / 2.0;f[20] = s->board_count / 4.0;
    f[21] = (double)s->active[1];f[22] = s->reopen[0] / 40000.0;
    f[23] = bc_min(s->decisions, 128) / 128.0;
    for (int i = 0; i < 4; i++) {
        double *out = f + 24 + 4 * i;
        if (i < s->history_count) {
            const BcEvent *e = s->history + i;
            out[0] = (e->seat + 1) / 2.0;out[1] = e->street / 3.0;
            out[2] = e->kind / 3.0;out[3] = e->bet / 20000.0;
        } else {
            out[0] = out[1] = out[2] = out[3] = 0.0;
        }
    }
    for (int p = 0; p < 2; p++) for (int street = 0; street < 3; street++)
        f[40 + p * 3 + street] = s->line[p][street] / 3.0;
    int own0 = (int)s->own[0] >> 2, own1 = (int)s->own[1] >> 2;
    int board_count[13] = {0}, rank_count[13] = {0}, board_suits[4] = {0}, suit_count[4] = {0};
    unsigned board_mask = 0, combined_mask = 0, suits[4] = {0};
    int board_high = 0, board_low = 12;
    for (int i = 0; i < 2; i++) {
        int card = (int)s->own[i], rank = card >> 2, suit = card & 3;
        rank_count[rank]++;suit_count[suit]++;suits[suit] |= 1u << rank;combined_mask |= 1u << rank;
    }
    for (int i = 0; i < s->board_count; i++) {
        int card = (int)s->board[i], rank = card >> 2, suit = card & 3;
        board_count[rank]++;rank_count[rank]++;board_suits[suit]++;suit_count[suit]++;
        board_mask |= 1u << rank;combined_mask |= 1u << rank;suits[suit] |= 1u << rank;
        if (rank > board_high) board_high = rank;
        if (rank < board_low) board_low = rank;
    }
    if (!s->board_count) board_low = 0;
    int max_rank = 0, max_board_suit = 0, max_suit = 0;
    for (int rank = 0; rank < 13; rank++) if (board_count[rank] > max_rank) max_rank = board_count[rank];
    for (int suit = 0; suit < 4; suit++) {
        if (board_suits[suit] > max_board_suit) max_board_suit = board_suits[suit];
        if (suit_count[suit] > max_suit) max_suit = suit_count[suit];
    }
    uint32_t made = s->board_count ? bc_rank(rank_count, suits, combined_mask) : 0;
    f[46] = bc_max(own0, own1) / 12.0;f[47] = bc_min(own0, own1) / 12.0;
    f[48] = own0 == own1;f[49] = (s->own[0] & 3) == (s->own[1] & 3);
    f[50] = (own0 > own1 ? own0 - own1 : own1 - own0) / 12.0;
    f[51] = (made >> 24) / 8.0;f[52] = made / 268435456.0;
    f[53] = board_high / 12.0;f[54] = board_low / 12.0;
    f[55] = bc_popcount(board_mask) / 4.0;f[56] = max_rank / 4.0;
    f[57] = max_board_suit / 4.0;f[58] = max_suit / 6.0;
    f[59] = bc_straight_window(combined_mask) / 5.0;
    f[60] = s->board_count ? ((own0 > board_high) + (own1 > board_high)) / 2.0 : 0.0;
    f[61] = ((board_count[own0] > 0) + (board_count[own1] > 0)) / 2.0;
    f[62] = bc_max(board_suits[s->own[0] & 3], board_suits[s->own[1] & 3]) / 4.0;
    f[63] = bc_straight_window(board_mask) / 5.0;
}

int bc_features(const BcInput *input, double features[64], int *cell) {
    BcDerived state;
    if (!features || !bc_prepare(input, &state)) return 0;
    bc_fill_features(input, &state, features);
    if (cell) *cell = state.cell;
    return 1;
}
int bc_supported(const BcInput *input) {
    BcDerived state;
    if (!bc_scope(input) || !bc_prepare(input, &state) || !bc_model_validate()) return -1;
    return bc_support[state.cell];
}

static int bc_leaf(const double features[64], int tree) {
    int node = bc_roots[tree];
    while (bc_feature[node] != -1) {
        node = features[bc_feature[node]] <= bc_threshold[node] ? bc_left[node] : bc_right[node];
    }
    return node;
}
static void bc_probabilities(int node, const BcDerived *state, double result[5]) {
    const int legal[5] = {state->call > 0, 1, state->can_raise && state->minimum < state->maximum,
        state->can_raise, state->can_raise && state->minimum + 1 <= state->maximum - 1};
    double sum = 0;
    for (int k = 0; k < 5; k++) {
        result[k] = legal[k] ? bc_counts[node * 5 + k] + 0.5 : 0.0;
        sum += result[k];
    }
    for (int k = 0; k < 5; k++) result[k] /= sum;
}
int bc_diagnostic(const BcInput *input, double probabilities[5]) {
    BcDerived state;
    double features[64];
    if (!probabilities || !bc_scope(input) || !bc_prepare(input, &state) || !bc_model_validate()) return 0;
    bc_fill_features(input, &state, features);
    for (int k = 0; k < 5; k++) probabilities[k] = 0.0;
    for (int tree = 0; tree < 32; tree++) {
        double values[5];
        bc_probabilities(bc_leaf(features, tree), &state, values);
        for (int k = 0; k < 5; k++) probabilities[k] += values[k];
    }
    for (int k = 0; k < 5; k++) probabilities[k] /= 32.0;
    return 1;
}

static uint32_t bc_random(uint64_t *rng) {
    uint64_t x = *rng;
    x ^= x >> 12;x ^= x << 25;x ^= x >> 27;*rng = x;
    return (uint32_t)((x * UINT64_C(2685821657736338717)) >> 32);
}
static int bc_action_kernel(const BcInput *input, uint64_t *rng, int32_t *action, int bypass_support) {
    BcDerived state;
    double features[64];
    if (!rng || !action || !bc_scope(input) || !bc_prepare(input, &state) || !bc_model_validate()) return 0;
    /* Test builds bypass only this verdict. The volatile read preserves the
     * same support lookup in the bounded cost helper after compiler inlining. */
    const volatile uint8_t *support = bc_support;
    int supported = support[state.cell];
    if (!supported && !bypass_support) return 0;
    bc_fill_features(input, &state, features);
    /* No guard/fallback is reachable after these draws. Model/input bounds
     * guarantee every route, CDF, bounded fraction and integer decode is total. */
    uint32_t first = bc_random(rng), second = bc_random(rng), third = bc_random(rng);
    int node = bc_leaf(features, (int)(first & 31));
    double probability[5];bc_probabilities(node, &state, probability);
    double u = second / 4294967296.0, cumulative = 0.0;
    int chosen = 1;
    for (int k = 0; k < 5; k++) if (probability[k] > 0) chosen = k;
    for (int k = 0; k < 5; k++) {
        cumulative += probability[k];
        if (u < cumulative) {chosen = k;break;}
    }
    int64_t result;
    if (chosen == 0) result = -1;
    else if (chosen == 1) result = 0;
    else if (chosen == 2) result = state.minimum;
    else if (chosen == 3) result = state.maximum;
    else {
        double z = bc_quantiles[node * 8 + (third >> 29)];
        int64_t lower = state.minimum + 1, width = state.maximum - state.minimum - 2;
        double product = (double)width * z;
        result = lower + (int64_t)floor(product + 0.5);
    }
    *action = (int32_t)result;
    return 1;
}
int bc_try_action(const BcInput *input, uint64_t *rng, int32_t *action) {
    return bc_action_kernel(input, rng, action, 0);
}
#ifdef BC_TESTING
int bc_test_action(const BcInput *input, uint64_t *rng, int32_t *action) {
    return bc_action_kernel(input, rng, action, 1);
}
#endif
