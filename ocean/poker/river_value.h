#pragma once

// River expectimax with an uncalibrated default range-policy response proxy.
// An optional, separately guarded historical Slumbot fit changes only the
// first predicted response; it does not identify full strategy or game value.
// Only two live, active players
// with equal contributions before this street are modeled. Other states retain
// the existing policy. One fixed raise size makes the re-raise tree a chain:
// fold/call end the hand, while only a re-raise produces another hero decision.
// All hero decisions aggregate hidden holdings before maximizing their values.
// The river has finite two-card ranges: exact integration is an optional mode.
// Exact mode changes numerical integration. A separate flag below controls
// the optional first-response calibration.
#ifndef POKER_RIVER_EXACT
#define POKER_RIVER_EXACT 0
#endif
#define POKER_RIVER_OPPONENTS (POKER_RIVER_EXACT ? 990 : 128)
#define POKER_RIVER_REFERENCES (POKER_RIVER_EXACT ? 1081 : 128)

#ifndef POKER_RIVER_CALIBRATED
#define POKER_RIVER_CALIBRATED 0
#endif
#if POKER_RIVER_CALIBRATED
#if !POKER_RIVER_EXACT
#error "POKER_RIVER_CALIBRATED requires POKER_RIVER_EXACT=1"
#endif
#include "river_response.h"
#endif

#ifndef POKER_RIVER_DEPTH
#define POKER_RIVER_DEPTH 16
#endif

typedef struct PokerRiverMoney {
    uint32_t pot, high, increment, bet[2], stack[2], reopen[2];
} PokerRiverMoney;

typedef struct PokerRiverModel {
    Poker* game;
    int table, hero, opponent, samples;
#if POKER_RIVER_CALIBRATED
    int calibrated;
#endif
    uint32_t initial_stack[2], initial_pot;
    int8_t outcome[POKER_RIVER_OPPONENTS];
    // Opponent equity versus hypothetical hero holdings after a passive action
    // or a raise. Those reference holdings use only the board and public line.
    double opponent_equity[2][POKER_RIVER_OPPONENTS];
    int32_t root_action;
    double root_passive_value, root_raise_value, root_value;
} PokerRiverModel;

static uint32_t poker_river_call(const PokerRiverMoney* m, int p) {
    uint32_t amount = m->high - m->bet[p];
    return amount < m->stack[p] ? amount : m->stack[p];
}

static int32_t poker_river_raise(const PokerRiverMoney* m, int p) {
    int q = 1 - p;
    uint32_t maximum = m->bet[p] + m->stack[p];
    if (m->high < m->reopen[p] || maximum <= m->high
        || m->bet[q] + m->stack[q] <= m->high) return 0;
    uint32_t target = m->high + 2 * (m->pot + poker_river_call(m, p)) / 3;
    uint32_t minimum = m->high + m->increment;
    if (target < minimum) target = minimum;
    return target < maximum ? target : maximum;
}

static PokerRiverMoney poker_river_pay(PokerRiverMoney m, int p, int32_t action) {
    uint32_t target = action ? (uint32_t)action : m.high;
    uint32_t amount = target - m.bet[p];
    if (amount > m.stack[p]) amount = m.stack[p];
    m.stack[p] -= amount; m.bet[p] += amount; m.pot += amount;
    if (target > m.high) {
        uint32_t increase = target - m.high;
        if (increase > m.increment) m.increment = increase;
        m.high = target;
    }
    m.reopen[p] = m.high + m.increment;
    return m;
}

// Exact terminal incremental chip utility, including folded contributions,
// unequal remaining stacks, returned unmatched chips, and deterministic odd
// chips on a tie. outcome is hero rank compared with the opponent rank.
static double poker_river_showdown(const PokerRiverModel* ctx,
    const PokerRiverMoney* m, int outcome) {
    Poker* g = ctx->game;
    int t = ctx->table, h = ctx->hero, o = ctx->opponent;
    uint32_t extra_h = ctx->initial_stack[0] - m->stack[0];
    uint32_t extra_o = ctx->initial_stack[1] - m->stack[1];
    uint32_t totals[POKER_PLAYERS], lower = 0, payout = 0;
    for (int p = 0; p < POKER_PLAYERS; p++)
        totals[p] = g->total[p][t] + (p == h ? extra_h : p == o ? extra_o : 0);
    while (lower < totals[h]) {
        uint32_t upper = totals[h], pot = 0;
        for (int p = 0; p < POKER_PLAYERS; p++)
            if (totals[p] > lower && totals[p] < upper) upper = totals[p];
        for (int p = 0; p < POKER_PLAYERS; p++)
            if (totals[p] > lower) pot += (totals[p] < upper ? totals[p] : upper) - lower;
        if (totals[o] < upper || outcome > 0) payout += pot;
        else if (!outcome) {
            unsigned winners = (1u << h) | (1u << o);
            payout += pot / 2 + ((pot & 1u) && poker_next(winners, g->button[t]) == h);
        }
        lower = upper;
    }
    return (double)payout - extra_h;
}

static double poker_river_hero_value(PokerRiverModel*, const PokerRiverMoney*,
    const double*, int, int);

static double poker_river_opponent_value(PokerRiverModel* ctx,
    const PokerRiverMoney* m, const double* weights, int depth, int range) {
    uint32_t call = poker_river_call(m, 1);
    PokerRiverMoney called = poker_river_pay(*m, 1, 0);
    int32_t raise = poker_river_raise(m, 1);
    double raised_weights[POKER_RIVER_OPPONENTS], raised_mass = 0, value = 0;
    double payoff[3];
    for (int outcome = -1; outcome <= 1; outcome++)
        payoff[outcome + 1] = poker_river_showdown(ctx, &called, outcome);
    // Opponent cannot win an unmatched part of the hero's bet.
    uint32_t unmatched = called.bet[0] > called.bet[1]
        ? called.bet[0] - called.bet[1] : 0;
    double eligible = called.pot - unmatched;
    double hero_fold_win = ctx->initial_pot + ctx->initial_stack[1] - m->stack[1];
#if POKER_RIVER_CALIBRATED
    int calibrated = POKER_PLAYERS == 2 && ctx->calibrated && !depth;
    PokerRiverResponseContext response = {0};
    if (calibrated) response = poker_river_response_context(call, eligible,
        m->pot, m->stack[1], m->stack[0], raise);
#endif
    for (int i = 0; i < ctx->samples; i++) {
        double q = ctx->opponent_equity[range][i];
        double fold = call && q * eligible < call;
        double r = 0;
#if POKER_RIVER_CALIBRATED
        if (calibrated) {
            double p[3]; poker_river_response_predict(&response, q, p);
            fold = p[0]; r = p[2];
        } else
#endif
        if (!fold && raise) {
            if (q > (call ? .78 : .58)) r = .85;
            else if (!call && q > .25) r = .045;
        }
        double w = weights[i];
        value += w * (fold * hero_fold_win
            + (1 - fold - r) * payoff[ctx->outcome[i] + 1]);
        raised_weights[i] = w * r;
        raised_mass += raised_weights[i];
    }
    if (raised_mass) {
        PokerRiverMoney raised = poker_river_pay(*m, 1, raise);
        value += poker_river_hero_value(ctx, &raised, raised_weights, depth + 1, 1);
    }
    return value;
}

static double poker_river_hero_value(PokerRiverModel* ctx,
    const PokerRiverMoney* m, const double* weights, int depth, int check_closes) {
    double mass = 0;
    for (int i = 0; i < ctx->samples; i++) mass += weights[i];
    uint32_t call = poker_river_call(m, 0);
    double best = -1e100;
    int32_t action = 0;
    if (call) {
        best = -(double)(ctx->initial_stack[0] - m->stack[0]) * mass;
        action = -1;
    }
    PokerRiverMoney called = poker_river_pay(*m, 0, 0);
    double passive = 0;
    if (call || check_closes || !called.stack[0] || !called.stack[1]) {
        double payoff[3];
        for (int outcome = -1; outcome <= 1; outcome++)
            payoff[outcome + 1] = poker_river_showdown(ctx, &called, outcome);
        for (int i = 0; i < ctx->samples; i++)
            passive += weights[i] * payoff[ctx->outcome[i] + 1];
    } else passive = poker_river_opponent_value(ctx, &called, weights, depth, 0);
    if (passive > best) { best = passive; action = 0; }
    double aggressive = -1e100;
    int32_t raise = poker_river_raise(m, 0);
    if (raise && depth < POKER_RIVER_DEPTH) {
        PokerRiverMoney raised = poker_river_pay(*m, 0, raise);
        aggressive = poker_river_opponent_value(ctx, &raised, weights, depth, 1);
        if (aggressive > best) { best = aggressive; action = raise; }
    }
    if (!depth) {
        ctx->root_action = action;
        ctx->root_passive_value = passive / mass;
        ctx->root_raise_value = aggressive / mass;
        ctx->root_value = best / mass;
    }
    return best;
}

#if !POKER_RIVER_EXACT
static void poker_river_pair(uint64_t* rng, const uint8_t* pool, int available,
    uint8_t* first, uint8_t* second) {
    unsigned x, limit = UINT32_MAX - UINT32_MAX % available;
    do { x = poker_random(rng); } while (x >= limit);
    int a = x % available;
    limit = UINT32_MAX - UINT32_MAX % (available - 1);
    do { x = poker_random(rng); } while (x >= limit);
    int b = x % (available - 1);
    if (b >= a) b++;
    *first = pool[a]; *second = pool[b];
}

#endif

static int poker_river_action_value(Poker* g, int t, uint64_t* rng, int samples,
    const PokerProfile* profiles, int32_t* action, PokerRiverModel* diagnostics) {
    int h = g->actor[t];
    if (g->street[t] != 3 || __builtin_popcount(g->live[t]) != 2
        || g->active[t] != g->live[t]) return 0;
    int o = __builtin_ctz(g->live[t] & ~(1u << h));
    uint32_t prior = g->total[h][t] - g->bet[h][t];
    if (prior != (uint32_t)(g->total[o][t] - g->bet[o][t])) return 0;
    for (int p = 0; p < POKER_PLAYERS; p++)
        if (p != h && p != o && g->total[p][t] > prior) return 0;
    assert(samples > 0 && samples <= 128);
    int reference_samples = POKER_RIVER_EXACT ? 1081 : samples;
    int opponent_samples = POKER_RIVER_EXACT ? 990 : samples;
    (void)rng;
    PokerRiverModel ctx = {.game=g, .table=t, .hero=h, .opponent=o, .samples=opponent_samples,
        .initial_stack={g->stack[h][t],g->stack[o][t]}, .initial_pot=g->pot[t],
#if POKER_RIVER_CALIBRATED
        // Calibrated on 200BB heads-up play without public table profiles.
        .calibrated=POKER_PLAYERS == 2 && !profiles && g->initial[h][t] == 20000
            && g->initial[o][t] == 20000,
#endif
    };
    PokerRiverMoney money = {.pot=g->pot[t], .high=g->high[t], .increment=g->increment[t],
        .bet={g->bet[h][t],g->bet[o][t]}, .stack={g->stack[h][t],g->stack[o][t]},
        .reopen={g->reopen[h][t],g->reopen[o][t]}};
    uint64_t board_mask = 0;
    uint8_t cards[7], board_pool[52], opponent_pool[52];
    int board_count = 0, opponent_count = 0;
    for (int b = 0; b < 5; b++) {
        cards[b + 2] = g->board[b][t];
        board_mask |= UINT64_C(1) << cards[b + 2];
    }
    cards[0] = g->hole[h][0][t]; cards[1] = g->hole[h][1][t];
    uint32_t hero_rank = poker_rank(cards, 7);
    uint64_t hero_mask = (UINT64_C(1) << cards[0]) | (UINT64_C(1) << cards[1]);
    for (int c = 0; c < 52; c++) if (!(board_mask & (UINT64_C(1) << c))) {
        board_pool[board_count++] = c;
        if (!(hero_mask & (UINT64_C(1) << c))) opponent_pool[opponent_count++] = c;
    }
    // Build a public-only range view: no actual hole cards are copied. Hero
    // reference draws intentionally include the real hero cards among all 47
    // board-compatible cards. Only each hypothetical opponent holding blocks
    // those reference hands below, as it would in that opponent's own view.
    Poker public_view = {0};
    public_view.street[0] = 2; public_view.button[0] = g->button[t];
    for (int b = 0; b < 5; b++) public_view.board[b][0] = g->board[b][t];
    unsigned passive_kind = money.high > money.bet[0] ? 2 : 1;
    unsigned old_kind = g->line[h][t] >> 6;
    if (passive_kind < old_kind) passive_kind = old_kind;
    uint64_t reference_mask[POKER_RIVER_REFERENCES];
    uint32_t reference_rank[POKER_RIVER_REFERENCES];
    uint8_t reference_cards[POKER_RIVER_REFERENCES][2];
    double reference_weight[2][POKER_RIVER_REFERENCES];
#if POKER_RIVER_EXACT
    int reference_a = 0, reference_b = 1;
#endif
    for (int j = 0; j < reference_samples; j++) {
#if POKER_RIVER_EXACT
        cards[0] = board_pool[reference_a]; cards[1] = board_pool[reference_b];
        if (++reference_b == board_count) reference_b = ++reference_a + 1;
#else
        poker_river_pair(rng, board_pool, board_count, &cards[0], &cards[1]);
#endif
        reference_mask[j] = (UINT64_C(1) << cards[0]) | (UINT64_C(1) << cards[1]);
        reference_cards[j][0] = cards[0]; reference_cards[j][1] = cards[1];
        reference_rank[j] = poker_rank(cards, 7);
        public_view.line[h][0] = g->line[h][t] & 63u;
        double prior_weight = poker_range_weight(&public_view, 0, h, cards[0], cards[1], profiles);
        double aggression = .25;
        if (profiles) aggression = (profiles[h].post_raises + 40 * aggression)
            / (profiles[h].post_actions + 40);
        int board_hi = 0;
        for (int b = 0; b < 5; b++)
            if ((cards[b + 2] >> 2) > board_hi) board_hi = cards[b + 2] >> 2;
        for (int k = 0; k < 2; k++)
            reference_weight[k][j] = prior_weight * poker_range_rank_likelihood(
                reference_rank[j], cards[0] >> 2, cards[1] >> 2, board_hi, 0,
                k ? 3u : passive_kind, aggression);
    }
    // Rank CDF answers each showdown-equity query in logarithmic time. Lists
    // of reference hands containing each card remove the opponent's blockers.
    // Exact mode integrates the complete reference range; sampled mode uses
    // the same weighted draws as the previous estimator.
    int order[POKER_RIVER_REFERENCES], head[52], next[2 * POKER_RIVER_REFERENCES];
    for (int c = 0; c < 52; c++) head[c] = -1;
    for (int j = 0; j < reference_samples; j++) {
        int at = j;
        while (at && reference_rank[order[at - 1]] > reference_rank[j]) {
            order[at] = order[at - 1]; at--;
        }
        order[at] = j;
        for (int card = 0; card < 2; card++) {
            int c = reference_cards[j][card], node = 2 * j + card;
            next[node] = head[c]; head[c] = node;
        }
    }
    double prefix[2][POKER_RIVER_REFERENCES + 1] = {{0}};
    for (int j = 0; j < reference_samples; j++) for (int k = 0; k < 2; k++)
        prefix[k][j + 1] = prefix[k][j] + reference_weight[k][order[j]];
    double weights[POKER_RIVER_OPPONENTS];
#if POKER_RIVER_EXACT
    int opponent_a = 0, opponent_b = 1;
#endif
    for (int i = 0; i < opponent_samples; i++) {
#if POKER_RIVER_EXACT
        cards[0] = opponent_pool[opponent_a]; cards[1] = opponent_pool[opponent_b];
        if (++opponent_b == opponent_count) opponent_b = ++opponent_a + 1;
#else
        poker_river_pair(rng, opponent_pool, opponent_count, &cards[0], &cards[1]);
#endif
        uint32_t rank = poker_rank(cards, 7);
        ctx.outcome[i] = (hero_rank > rank) - (hero_rank < rank);
        public_view.line[o][0] = g->line[o][t] & 63u;
        double prior_weight = poker_range_weight(&public_view, 0, o, cards[0], cards[1], profiles);
        double aggression = .25;
        if (profiles) aggression = (profiles[o].post_raises + 40 * aggression)
            / (profiles[o].post_actions + 40);
        int board_hi = 0;
        for (int b = 0; b < 5; b++)
            if ((cards[b + 2] >> 2) > board_hi) board_hi = cards[b + 2] >> 2;
        weights[i] = prior_weight * poker_range_rank_likelihood(rank, cards[0] >> 2,
            cards[1] >> 2, board_hi, 0, g->line[o][t] >> 6, aggression);
        int low = 0, high = reference_samples;
        while (low < high) {
            int mid = (low + high) / 2;
            if (reference_rank[order[mid]] < rank) low = mid + 1;
            else high = mid;
        }
        int below = low; high = reference_samples;
        while (low < high) {
            int mid = (low + high) / 2;
            if (reference_rank[order[mid]] <= rank) low = mid + 1;
            else high = mid;
        }
        int tied = low;
        double blocked_mass[2] = {0}, blocked_share[2] = {0};
        for (int card = 0; card < 2; card++) {
            for (int node = head[cards[card]]; node != -1; node = next[node]) {
                int j = node / 2;
                if (card && (reference_mask[j] & (UINT64_C(1) << cards[0]))) continue;
                double won = rank > reference_rank[j] ? 1 : rank == reference_rank[j] ? .5 : 0;
                for (int k = 0; k < 2; k++) {
                    blocked_mass[k] += reference_weight[k][j];
                    blocked_share[k] += reference_weight[k][j] * won;
                }
            }
        }
        for (int k = 0; k < 2; k++) {
            double mass = prefix[k][reference_samples] - blocked_mass[k];
            double share = prefix[k][below] + .5 * (prefix[k][tied] - prefix[k][below]) - blocked_share[k];
            double q = mass > 0 ? share / mass : .5;
            ctx.opponent_equity[k][i] = q < 0 ? 0 : q > 1 ? 1 : q;
        }
    }
    int check_closes = !(g->pending[t] & ~(1u << h));
    poker_river_hero_value(&ctx, &money, weights, 0, check_closes);
    *action = ctx.root_action;
    if (diagnostics) *diagnostics = ctx;
    return 1;
}
