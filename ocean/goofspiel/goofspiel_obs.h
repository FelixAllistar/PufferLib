#pragma once

#include "goofspiel_core.h"

#include <stdint.h>
#include <string.h>

#define GS_O_HANDS 0
#define GS_O_REMAINING (GS_O_HANDS + GS_MAX_PLAYERS * GS_MAX_CARDS)
#define GS_O_SCORES (GS_O_REMAINING + GS_MAX_CARDS)
#define GS_O_CURRENT_PRIZE (GS_O_SCORES + 2 * GS_MAX_PLAYERS)
#define GS_O_POT (GS_O_CURRENT_PRIZE + 1)
#define GS_O_ROUND (GS_O_POT + 2)
#define GS_O_LAST_WINNER (GS_O_ROUND + 1)
#define GS_O_ACTIVE_PLAYERS (GS_O_LAST_WINNER + GS_MAX_PLAYERS + 1)
#define GS_O_SELF (GS_O_ACTIVE_PLAYERS + GS_MAX_PLAYERS)
#define GS_COMPACT_OBS_SIZE (GS_O_SELF + GS_MAX_PLAYERS)
#define GS_MAX_POINTS (GS_MAX_CARDS * (GS_MAX_CARDS + 1) / 2)
#define GS_OPEN_SPIEL_OBS_SIZE (GS_MAX_PLAYERS * (GS_MAX_POINTS + 1) \
    + GS_MAX_CARDS * GS_MAX_CARDS \
    + GS_MAX_PLAYERS * GS_MAX_CARDS + GS_MAX_PLAYERS)
#define OBS_SIZE GS_OPEN_SPIEL_OBS_SIZE

#ifndef GS_OBS_T_DEFINED
#define GS_OBS_T_DEFINED
typedef uint8_t obs_t;
#endif

#ifdef __CUDACC__
#define GS_OBS_HD __host__ __device__
#else
#define GS_OBS_HD
#endif

GS_OBS_HD static inline int gs_view_player(
        const GSConfig* cfg, int observer, int view) {
    if (!cfg->egocentric) {
        return view;
    }
    int player = observer + view;
    return player < cfg->num_players ? player : player - cfg->num_players;
}

GS_OBS_HD static inline int gs_view_index(
        const GSConfig* cfg, int observer, int player) {
    if (!cfg->egocentric) {
        return player;
    }
    int view = player - observer;
    return view >= 0 ? view : view + cfg->num_players;
}

// OpenSpiel's perfect-information Goofspiel tensor is:
// [score one-hots][revealed prize sequence][hands]. We append a player
// one-hot; egocentric observations always place self first.
// The tensor is packed at the front of the existing fixed observation buffer,
// so the fast compact observation and its checkpoint shape remain unchanged.
static inline int gs_open_spiel_obs_size(const GSConfig* cfg) {
    int score_bins = cfg->total_points + 1;
    return cfg->num_players * score_bins
        + cfg->num_turns * cfg->num_cards
        + cfg->num_players * cfg->num_cards
        + cfg->num_players;
}

GS_OBS_HD static inline void gs_observe_open_spiel(const GSConfig* cfg,
        const GSState* state, int observer, obs_t* obs) {
    for (int i = 0; i < OBS_SIZE; i++) obs[i] = 0;
    int offset = 0;
    int score_bins = cfg->total_points + 1;

    for (int view = 0; view < cfg->num_players; view++) {
        int player = observer + view;
        if (player >= cfg->num_players) player -= cfg->num_players;
        obs[offset + view * score_bins + state->scores[player]] = 1;
    }
    offset += cfg->num_players * score_bins;

    for (int round = 0; round <= state->round
            && round < cfg->num_turns; round++) {
        obs[offset + round * cfg->num_cards + state->prizes[round]] = 1;
    }
    offset += cfg->num_turns * cfg->num_cards;

    for (int view = 0; view < cfg->num_players; view++) {
        int player = observer + view;
        if (player >= cfg->num_players) player -= cfg->num_players;
        uint16_t hand = state->hands[player];
        if (cfg->information == GS_INFO_HIDDEN_BIDS && player != observer) {
            hand = 0;
        }
        for (int card = 0; card < cfg->num_cards; card++) {
            obs[offset + view * cfg->num_cards + card]
                = (obs_t)((hand >> card) & 1u);
        }
    }
    offset += cfg->num_players * cfg->num_cards;
    obs[offset + (cfg->egocentric ? 0 : observer)] = 1;
}

GS_OBS_HD static inline void gs_observe_player(const GSConfig* cfg,
        const GSState* state, int observer, obs_t* obs) {
    if (cfg->open_spiel_obs) {
        gs_observe_open_spiel(cfg, state, observer, obs);
        return;
    }
    for (int i = 0; i < OBS_SIZE; i++) obs[i] = 0;

    for (int view = 0; view < cfg->num_players; view++) {
        int player = gs_view_player(cfg, observer, view);
        uint16_t hand = state->hands[player];
        if (cfg->information == GS_INFO_HIDDEN_BIDS && player != observer) {
            hand = 0;
        }
        for (int card = 0; card < cfg->num_cards; card++) {
            obs[GS_O_HANDS + view * GS_MAX_CARDS + card]
                = (obs_t)((hand >> card) & 1u);
        }

        int score = state->scores[player];
        obs[GS_O_SCORES + 2 * view] = (obs_t)(score & 15);
        obs[GS_O_SCORES + 2 * view + 1] = (obs_t)(score >> 4);
        obs[GS_O_ACTIVE_PLAYERS + view] = 1;
    }

    for (int card = 0; card < cfg->num_cards; card++) {
        obs[GS_O_REMAINING + card]
            = (obs_t)((state->remaining_prizes >> card) & 1u);
    }
    obs[GS_O_CURRENT_PRIZE] = (obs_t)(state->prizes[state->round] + 1);
    obs[GS_O_POT] = (obs_t)(state->pot & 15);
    obs[GS_O_POT + 1] = (obs_t)(state->pot >> 4);
    obs[GS_O_ROUND] = state->round;

    if (state->last_winner == GS_NO_WINNER) {
        if (state->round > 0) {
            obs[GS_O_LAST_WINNER + GS_MAX_PLAYERS] = 1;
        }
    } else {
        int view = gs_view_index(cfg, observer, state->last_winner);
        obs[GS_O_LAST_WINNER + view] = 1;
    }

    int self = cfg->egocentric ? 0 : observer;
    obs[GS_O_SELF + self] = 1;
}

#undef GS_OBS_HD
