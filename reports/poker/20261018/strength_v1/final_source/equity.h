#pragma once

#ifndef POKER_EQUITY_SAMPLES
#define POKER_EQUITY_SAMPLES (POKER_PLAYERS == 2 ? 128 : 32)
#endif

// Optional table-local public statistics. The caller records EVERY public
// action before stepping and begins each hand once. No card/outcome inspection.
// Priors shrink small samples; these are behavioral heuristics, not human data.
typedef struct PokerProfile {
    uint64_t hands, vpip, pfr, faced, folded, post_actions, post_raises;
    double raise_pot_sum;
} PokerProfile;

typedef struct PokerEquity {
    float share, payout, eligible_pot, effective_samples;
} PokerEquity;

void poker_profile_begin(PokerProfile* profiles) {
    if (profiles) for (int p = 0; p < POKER_PLAYERS; p++) profiles[p].hands++;
}

void poker_profile_update(PokerProfile* profiles, Poker* g, int t, int32_t action) {
    if (!profiles || g->actor[t] == POKER_DONE) return;
    int p = g->actor[t], street = g->street[t];
    PokerProfile* h = profiles + p;
    uint32_t call = g->high[t] - g->bet[p][t];
    if (call) { h->faced++; h->folded += action < 0; }
    if (!street) {
        unsigned previous = g->line[p][t] & 3u;
        if ((action > 0 || (action == 0 && call)) && previous < 2) h->vpip++;
        if (action > 0 && previous < 3) h->pfr++;
    } else {
        h->post_actions++;
        if (action > 0) {
            h->post_raises++;
            double fraction = (action - g->high[t]) / (double)(g->pot[t] + call);
            h->raise_pot_sum += fraction < 4 ? fraction : 4;
        }
    }
}

// Approximate action likelihood for sampled cards. It only sees public actions,
// position, the visible board, and optional public table statistics. It is not
// trained or calibrated. The equity computation below is separate from this
// deliberately simple range hypothesis.
double poker_range_weight(Poker* g, int t, int p, uint8_t c0, uint8_t c1,
    const PokerProfile* profiles) {
    int r0 = c0 >> 2, r1 = c1 >> 2;
    int hi = r0 > r1 ? r0 : r1, lo = r0 < r1 ? r0 : r1;
    int suited = (c0 & 3) == (c1 & 3);
    double pre = r0 == r1 ? .48 + .043 * hi
        : .10 + .033 * hi + .016 * lo + .07 * suited + .04 * (hi - lo <= 2);
    if (pre > 1) pre = 1;
    double vpip = .35, pfr = .20, aggression = .25;
    if (profiles) {
        const PokerProfile* h = profiles + p;
        vpip = (h->vpip + 30 * vpip) / (h->hands + 30);
        pfr = (h->pfr + 30 * pfr) / (h->hands + 30);
        aggression = (h->post_raises + 40 * aggression) / (h->post_actions + 40);
    }
    unsigned line = g->line[p][t];
    double weight = 1;
    int position = (p + POKER_PLAYERS - g->button[t]) % POKER_PLAYERS;
    double early = POKER_PLAYERS > 2 && position >= 3 ? .8 : 1;
    if ((line & 3) == 3) {
        double floor = .025 + .35 * pfr;
        double strong = pre * pre * pre * pre;
        weight = floor + (1 - floor) * strong * early;
    } else if ((line & 3) == 2) {
        double floor = .05 + .45 * vpip;
        weight = floor + (1 - floor) * pre * pre * early;
    }
    for (int street = 1; street <= g->street[t]; street++) {
        unsigned action = (line >> (2 * street)) & 3;
        if (action < 2) continue;
        uint8_t cards[7] = {c0, c1};
        unsigned ranks = (1u << r0) | (1u << r1), suits[4] = {0};
        suits[c0 & 3]++; suits[c1 & 3]++;
        int board_hi = 0;
        for (int b = 0; b < street + 2; b++) {
            cards[b + 2] = g->board[b][t];
            int r = cards[b + 2] >> 2;
            if (r > board_hi) board_hi = r;
            ranks |= 1u << r; suits[cards[b + 2] & 3]++;
        }
        uint32_t rank = poker_rank(cards, street + 4);
        int category = rank >> 24;
        double made = category >= 2 ? .85 + .025 * (category - 2) : .08;
        if (category == 1) {
            int pair = ((rank >> 16) & 15) - 2;
            made = (pair == r0 || pair == r1) ? (pair >= board_hi ? .65 : .35) : .12;
        }
        int draw = 0;
        if (street < 3) {
            for (int s = 0; s < 4; s++)
                draw |= suits[s] == 4 && ((c0 & 3) == s || (c1 & 3) == s);
            for (int r = 0; r < 9; r++) draw |= __builtin_popcount((ranks >> r) & 31) == 4;
            draw |= __builtin_popcount(ranks & 0x100f) == 4;
        }
        double likelihood;
        if (action == 3) {
            double floor = .025 + .25 * aggression;
            likelihood = floor + (1 - floor) * made * made + .16 * draw;
        } else likelihood = .15 + .8 * made + .2 * draw;
        weight *= likelihood < 1 ? likelihood : 1;
    }
    return weight;
}

// Self-normalized importance sampling of complete unknown deals. Uniform
// proposals enforce card removal jointly across ALL opponents and the runout.
// Range weights are evaluated on visible streets, never sampled future cards.
// Fixed sample count bounds evaluator work; ESS exposes weight concentration.
// Payout layers include folded money and exclude pots the hero cannot win.
void poker_equity(Poker* g, int t, uint64_t* rng, int samples, int ranges,
    const PokerProfile* profiles, PokerEquity* out) {
    int hero = g->actor[t], board_n = g->street[t] ? g->street[t] + 2 : 0;
    assert(hero < POKER_PLAYERS && samples > 0);
    uint64_t known = (UINT64_C(1) << g->hole[hero][0][t])
        | (UINT64_C(1) << g->hole[hero][1][t]);
    uint8_t cards[7] = {g->hole[hero][0][t], g->hole[hero][1][t]};
    for (int i = 0; i < board_n; i++) {
        cards[i + 2] = g->board[i][t]; known |= UINT64_C(1) << cards[i + 2];
    }
    uint8_t pool[52], opponents[POKER_PLAYERS];
    int available = 0, n = 0;
    for (int c = 0; c < 52; c++) if (!(known & (UINT64_C(1) << c))) pool[available++] = c;
    for (int p = 0; p < POKER_PLAYERS; p++)
        if (p != hero && (g->live[t] & (1u << p))) opponents[n++] = p;
    uint32_t call = g->high[t] - g->bet[hero][t];
    if (call > g->stack[hero][t]) call = g->stack[hero][t];
    uint32_t totals[POKER_PLAYERS];
    for (int p = 0; p < POKER_PLAYERS; p++) totals[p] = g->total[p][t] + (p == hero ? call : 0);
    uint32_t pots[POKER_PLAYERS], contenders[POKER_PLAYERS], lower = 0, eligible = 0;
    int layers = 0;
    while (lower < totals[hero]) {
        uint32_t upper = totals[hero], pot = 0, mask = 0;
        for (int p = 0; p < POKER_PLAYERS; p++)
            if (totals[p] > lower && totals[p] < upper) upper = totals[p];
        for (int p = 0; p < POKER_PLAYERS; p++) {
            if (totals[p] > lower) pot += (totals[p] < upper ? totals[p] : upper) - lower;
            if (totals[p] >= upper && (g->live[t] & (1u << p))) mask |= 1u << p;
        }
        pots[layers] = pot; contenders[layers++] = mask; eligible += pot; lower = upper;
    }
    double weights = 0, weights2 = 0, shares = 0, payouts = 0;
    for (int sample = 0; sample < samples; sample++) {
        uint8_t deck[52]; memcpy(deck, pool, available);
        int draws = 5 - board_n + 2 * n;
        for (int i = 0; i < draws; i++) {
            unsigned left = available - i, x, limit = UINT32_MAX - UINT32_MAX % left;
            do { x = poker_random(rng); } while (x >= limit);
            int j = i + x % left;
            uint8_t c = deck[i]; deck[i] = deck[j]; deck[j] = c;
        }
        int cursor = 0;
        for (int i = board_n; i < 5; i++) cards[i + 2] = deck[cursor++];
        cards[0] = g->hole[hero][0][t]; cards[1] = g->hole[hero][1][t];
        uint32_t ranks[POKER_PLAYERS] = {0};
        ranks[hero] = poker_rank(cards, 7);
        double weight = 1;
        for (int i = 0; i < n; i++) {
            int p = opponents[i];
            cards[0] = deck[cursor++]; cards[1] = deck[cursor++];
            ranks[p] = poker_rank(cards, 7);
            if (ranges) weight *= poker_range_weight(g, t, p, cards[0], cards[1], profiles);
        }
        int ties = 1, beaten = 0;
        for (int i = 0; i < n; i++) {
            ties += ranks[opponents[i]] == ranks[hero];
            beaten |= ranks[opponents[i]] > ranks[hero];
        }
        double payout = 0;
        for (int k = 0; k < layers; k++) {
            int tied = 1, lost = 0;
            for (int i = 0; i < n; i++) if (contenders[k] & (1u << opponents[i])) {
                tied += ranks[opponents[i]] == ranks[hero];
                lost |= ranks[opponents[i]] > ranks[hero];
            }
            if (!lost) payout += pots[k] / (double)tied;
        }
        weights += weight; weights2 += weight * weight;
        shares += weight * (beaten ? 0 : 1.0 / ties);
        payouts += weight * payout;
    }
    out->share = shares / weights; out->payout = payouts / weights;
    out->eligible_pot = eligible; out->effective_samples = weights * weights / weights2;
}

int32_t poker_equity_bot(Poker* g, int t, uint64_t* rng, int samples,
    const PokerProfile* profiles) {
    if (!g->street[t]) return poker_rules_preflop(g, t, rng, 1);
    // One independent simulation seed per decision keeps action RNG consumption
    // the same across 32/128-sample variants and never advances the deal RNG.
    uint64_t simulations = (uint64_t)poker_random(rng) << 32;
    simulations |= poker_random(rng);
    if (!simulations) simulations = 1;
    unsigned roll = poker_random(rng) % 1000;
    PokerEquity e;
    poker_equity(g, t, &simulations, samples ? samples : POKER_EQUITY_SAMPLES,
        1, profiles, &e);
    int p = g->actor[t], live = __builtin_popcount(g->live[t]);
    uint32_t call = g->high[t] - g->bet[p][t];
    if (call > g->stack[p][t]) call = g->stack[p][t];
    // A small realization margin is still a policy heuristic. Showdown equity
    // does not model future bets, future folds, or optimal action value.
    float margin = g->street[t] == 3 ? 0 : .025f;
    if (call && e.payout < call + margin * e.eligible_pot) return -1;
    float value = call ? (live == 2 ? .78f : .68f) : (live == 2 ? .58f : .48f);
    int raise = e.share > value && roll < 850;
    if (!call && live == 2 && e.share > .25f && roll < 45) raise = 1;
    if (raise && poker_can_raise(g, t)) {
        uint32_t target = g->high[t] + 2 * (g->pot[t] + call) / 3;
        uint32_t minimum = poker_min_raise(g, t), maximum = g->bet[p][t] + g->stack[p][t];
        if (target < minimum) target = minimum;
        return target < maximum ? target : maximum;
    }
    return 0;
}
