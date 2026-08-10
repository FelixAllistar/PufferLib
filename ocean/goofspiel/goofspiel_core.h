#pragma once

#include <stdint.h>
#include <string.h>

#define GS_MAX_PLAYERS 2
#define GS_MAX_CARDS 4          /* fixed 4-card checkpoint/obs layout */
#define GS_MAX_CARDS_EXT 13     /* runtime max for larger training variants */

enum {
    GS_PRIZES_RANDOM = 0,
    GS_PRIZES_ASCENDING = 1,
    GS_PRIZES_DESCENDING = 2,
};

enum {
    GS_INFO_PERFECT = 0,
    GS_INFO_HIDDEN_BIDS = 1,
};

enum {
    GS_RETURN_WIN_LOSS = 0,
    GS_RETURN_POINT_DIFFERENCE = 1,
    GS_RETURN_TOTAL_POINTS = 2,
};

enum {
    GS_TIE_DISCARD = 0,
    GS_TIE_CARRY = 1,
};

enum {
    GS_NO_WINNER = 255,
};

typedef struct {
    uint32_t full_hand;
    uint32_t total_points;
    uint8_t num_players;
    uint8_t num_cards;
    uint8_t num_turns;
    uint8_t prize_order;
    uint8_t information;
    uint8_t egocentric;
    uint8_t return_type;
    uint8_t tie_rule;
    uint8_t auto_forced_last;
    uint8_t open_spiel_obs;
} GSConfig;

typedef struct {
    uint32_t hands[GS_MAX_PLAYERS];
    uint32_t remaining_prizes;
    uint8_t scores[GS_MAX_PLAYERS];
    uint8_t prizes[GS_MAX_CARDS_EXT];
    uint8_t last_bids[GS_MAX_PLAYERS];
    uint8_t round;
    uint8_t pot;
    uint8_t discarded;
    uint8_t last_winner;
} GSState;

typedef struct {
    uint8_t bids[GS_MAX_CARDS_EXT][GS_MAX_PLAYERS];
    uint8_t winners[GS_MAX_CARDS_EXT];
} GSHistory;

static inline int gs_config_valid(const GSConfig* cfg) {
    return cfg->num_players >= 2 && cfg->num_players <= GS_MAX_PLAYERS
        && cfg->num_cards >= 2 && cfg->num_cards <= GS_MAX_CARDS_EXT
        && cfg->num_turns >= 1 && cfg->num_turns <= cfg->num_cards
        && cfg->prize_order <= GS_PRIZES_DESCENDING
        && cfg->information <= GS_INFO_HIDDEN_BIDS
        && cfg->return_type <= GS_RETURN_TOTAL_POINTS
        && cfg->tie_rule <= GS_TIE_CARRY;
}

static inline uint32_t gs_mix32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x | 1u;
}

static inline uint32_t gs_random(uint32_t* rng) {
    uint32_t x = *rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *rng = x;
    return x;
}

// Lemire's bounded mapping. The rejection branch occurs only while shuffling
// at reset and keeps every prize permutation exactly equiprobable.
static inline uint32_t gs_random_bounded(uint32_t* rng, uint32_t bound) {
    uint32_t x = gs_random(rng);
    uint64_t product = (uint64_t)x * bound;
    uint32_t low = (uint32_t)product;
    if (low < bound) {
        uint32_t threshold = (uint32_t)(-bound) % bound;
        while (low < threshold) {
            x = gs_random(rng);
            product = (uint64_t)x * bound;
            low = (uint32_t)product;
        }
    }
    return (uint32_t)(product >> 32);
}

static inline void gs_shuffle_prizes(GSState* state, const GSConfig* cfg,
        uint32_t* rng) {
    for (int i = 0; i < cfg->num_cards; i++) {
        state->prizes[i] = (uint8_t)i;
    }

    if (cfg->prize_order == GS_PRIZES_RANDOM) {
        for (int i = cfg->num_cards - 1; i > 0; i--) {
            int j = (int)gs_random_bounded(rng, (uint32_t)i + 1);
            uint8_t tmp = state->prizes[i];
            state->prizes[i] = state->prizes[j];
            state->prizes[j] = tmp;
        }
    } else if (cfg->prize_order == GS_PRIZES_DESCENDING) {
        for (int i = 0; i < cfg->num_cards; i++) {
            state->prizes[i] = (uint8_t)(cfg->num_cards - i - 1);
        }
    }
}

static inline void gs_reset(GSState* state, GSHistory* history,
        const GSConfig* cfg, uint32_t* rng) {
    memset(state, 0, sizeof(*state));
    memset(history, 0, sizeof(*history));
    memset(history->winners, GS_NO_WINNER, sizeof(history->winners));

    for (int p = 0; p < cfg->num_players; p++) {
        state->hands[p] = cfg->full_hand;
    }
    state->remaining_prizes = cfg->full_hand;
    state->last_winner = GS_NO_WINNER;
    gs_shuffle_prizes(state, cfg, rng);
    state->remaining_prizes &= ~(1u << state->prizes[0]);
}

static inline void gs_resolve_round(GSState* state, GSHistory* history,
        const GSConfig* cfg, const uint8_t* bids) {
    int round = state->round;
    int high_bid = -1;
    int high_count = 0;
    int winner = 0;

    for (int p = 0; p < cfg->num_players; p++) {
        int bid = bids[p];
        state->hands[p] &= ~(1u << bid);
        state->last_bids[p] = (uint8_t)bid;
        history->bids[round][p] = (uint8_t)(bid + 1);
        if (bid > high_bid) {
            high_bid = bid;
            high_count = 1;
            winner = p;
        } else if (bid == high_bid) {
            high_count++;
        }
    }

    int prize = state->prizes[round] + 1;
    if (high_count == 1) {
        state->scores[winner] = (uint8_t)(state->scores[winner]
            + state->pot + prize);
        state->pot = 0;
        state->last_winner = (uint8_t)winner;
        history->winners[round] = (uint8_t)winner;
    } else {
        state->last_winner = GS_NO_WINNER;
        if (cfg->tie_rule == GS_TIE_CARRY) {
            state->pot = (uint8_t)(state->pot + prize);
        } else {
            state->discarded = (uint8_t)(state->discarded + prize);
        }
    }

    state->round++;
    if (state->round < cfg->num_turns) {
        int next_prize = state->prizes[state->round];
        state->remaining_prizes &= ~(1u << next_prize);
    }
}

// Returns one when the game is terminal. The final forced card is resolved in
// the same call because it contains no decision and should not consume policy
// inference.
static inline int gs_step(GSState* state, GSHistory* history,
        const GSConfig* cfg, const uint8_t* bids) {
    gs_resolve_round(state, history, cfg, bids);

    if (cfg->auto_forced_last && cfg->num_turns == cfg->num_cards
            && state->round == cfg->num_turns - 1) {
        uint8_t forced[GS_MAX_PLAYERS];
        for (int p = 0; p < cfg->num_players; p++) {
            forced[p] = (uint8_t)__builtin_ctz(state->hands[p]);
        }
        gs_resolve_round(state, history, cfg, forced);
    }

    if (state->round < cfg->num_turns) {
        return 0;
    }
    if (state->pot) {
        state->discarded = (uint8_t)(state->discarded + state->pot);
        state->pot = 0;
    }
    return 1;
}

// Exact OpenSpiel-compatible terminal utilities. Puffer normalization is kept
// in the adapter so the game core remains an exact rules reference.
static inline void gs_returns(const GSState* state, const GSConfig* cfg,
        float* returns) {
    if (cfg->return_type == GS_RETURN_TOTAL_POINTS) {
        for (int p = 0; p < cfg->num_players; p++) {
            returns[p] = state->scores[p];
        }
        return;
    }

    float sum = 0.0f;
    int max_score = -1;
    int winners = 0;
    for (int p = 0; p < cfg->num_players; p++) {
        sum += state->scores[p];
        if (state->scores[p] > max_score) {
            max_score = state->scores[p];
            winners = 1;
        } else if (state->scores[p] == max_score) {
            winners++;
        }
    }

    if (cfg->return_type == GS_RETURN_POINT_DIFFERENCE) {
        float mean = sum / cfg->num_players;
        for (int p = 0; p < cfg->num_players; p++) {
            returns[p] = state->scores[p] - mean;
        }
        return;
    }

    if (winners == cfg->num_players) {
        memset(returns, 0, (size_t)cfg->num_players * sizeof(float));
        return;
    }

    int losers = cfg->num_players - winners;
    for (int p = 0; p < cfg->num_players; p++) {
        returns[p] = state->scores[p] == max_score
            ? 1.0f / winners : -1.0f / losers;
    }
}

static inline int gs_points_revealed(const GSState* state) {
    int points = 0;
    for (int r = 0; r < state->round; r++) {
        points += state->prizes[r] + 1;
    }
    return points;
}
