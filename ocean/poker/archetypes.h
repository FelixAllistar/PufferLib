#pragma once
// Scripted archetypes for the evaluation panel (2026-10-25). Each punishes a different exploit, so a policy that
// beats all of them (and Rules and Range) plays poker rather than exploiting one opponent:
//   8 maniac:  bets or raises about pot most of the time, otherwise calls; never folds. Punishes folding too much.
//   9 station: always checks or calls; never raises or folds. Punishes bluffing; pays off value bets.
//  10 tight:   plays only strong preflop hands (raises them, folds the rest), then bets made equity and calls only
//              with pot odds. Punishes playing junk and calling down light.
static inline int32_t poker_archetype_raise(Poker* g, int t, uint32_t target) {
    int p = g->actor[t];
    uint32_t minimum = poker_min_raise(g, t), maximum = g->bet[p][t] + g->stack[p][t];
    if (target < minimum) target = minimum;
    return target < maximum ? target : maximum;
}

int32_t poker_maniac_bot(Poker* g, int t, uint64_t* rng) {
    int p = g->actor[t];
    uint32_t call = g->high[t] - g->bet[p][t];
    if (call > g->stack[p][t]) call = g->stack[p][t];
    unsigned roll = poker_random(rng) % 1000;
    if (roll < 700 && poker_can_raise(g, t))
        return poker_archetype_raise(g, t, g->high[t] + (roll < 350 ? 2 * (g->pot[t] + call) / 3 : g->pot[t] + call));
    return 0;
}

int32_t poker_station_bot(Poker* g, int t, uint64_t* rng) {
    (void)g; (void)t; (void)rng;
    return 0;
}

int32_t poker_tight_bot(Poker* g, int t, uint64_t* rng) {
    int p = g->actor[t];
    uint32_t call = g->high[t] - g->bet[p][t];
    if (call > g->stack[p][t]) call = g->stack[p][t];
    if (!g->street[t]) {
        int r0 = g->hole[p][0][t] >> 2, r1 = g->hole[p][1][t] >> 2;
        int hi = r0 > r1 ? r0 : r1, lo = r0 < r1 ? r0 : r1;
        int suited = (g->hole[p][0][t] & 3) == (g->hole[p][1][t] & 3);
        // 88+, AK, AQ, AJs, KQs (ranks 0..12 = deuce..ace).
        int strong = (r0 == r1 && hi >= 6) || (hi == 12 && lo >= 10) || (suited && hi == 12 && lo == 9)
            || (suited && hi == 11 && lo == 10);
        if (!strong) return call ? -1 : 0;
        if (poker_can_raise(g, t) && g->high[t] < 2000)
            return poker_archetype_raise(g, t, g->high[t] <= 100 ? 300 : 3 * g->high[t]);
        return 0;
    }
    uint64_t simulations = ((uint64_t)poker_random(rng) << 32) | poker_random(rng);
    if (!simulations) simulations = 1;
    PokerEquity e;
    poker_equity(g, t, &simulations, 64, 0, NULL, &e);
    if (e.share > .6f && poker_can_raise(g, t))
        return poker_archetype_raise(g, t, g->high[t] + (g->pot[t] + call));
    if (call && e.payout < call) return -1;
    return 0;
}

// 11 trapper (2026-10-28): slow-plays (Doyle Brunson style). Strong hands check and call to let the field bet into
// it, then check-raise the turn or bet/raise big on the river; medium hands call with pot odds; weak hands give up.
// Preflop it limps or flat-calls premiums more often than it raises them. Punishes over-aggression and shoving
// into a range that looks weak, which neither tight (raises its strength) nor station (never raises) does.
int32_t poker_trapper_bot(Poker* g, int t, uint64_t* rng) {
    int p = g->actor[t];
    uint32_t call = g->high[t] - g->bet[p][t];
    if (call > g->stack[p][t]) call = g->stack[p][t];
    unsigned roll = poker_random(rng) % 1000;
    if (!g->street[t]) {
        int r0 = g->hole[p][0][t] >> 2, r1 = g->hole[p][1][t] >> 2;
        int hi = r0 > r1 ? r0 : r1, lo = r0 < r1 ? r0 : r1;
        int suited = (g->hole[p][0][t] & 3) == (g->hole[p][1][t] & 3);
        // Premium: TT+, AK, AQs. Playable: any pair, two broadway cards, suited aces, suited connectors 54s+.
        int premium = (r0 == r1 && hi >= 8) || (hi == 12 && lo == 11) || (suited && hi == 12 && lo == 10);
        int playable = r0 == r1 || lo >= 8 || (suited && hi == 12) || (suited && hi - lo == 1 && lo >= 3);
        if (premium) {
            if (roll < 350 && poker_can_raise(g, t) && g->high[t] < 1500)
                return poker_archetype_raise(g, t, g->high[t] <= 100 ? 300 : 3 * g->high[t]);
            return 0;  // the trap: flat-call any size
        }
        if (playable && call <= 4 * POKER_BB) return 0;
        return call ? -1 : 0;
    }
    uint64_t simulations = ((uint64_t)poker_random(rng) << 32) | poker_random(rng);
    if (!simulations) simulations = 1;
    PokerEquity e;
    poker_equity(g, t, &simulations, 64, 0, NULL, &e);
    int river = g->street[t] == 3;
    if (e.share > .7f) {
        if (poker_can_raise(g, t)) {
            if (river) return poker_archetype_raise(g, t, g->high[t] + (roll < 500 ? 3 * (g->pot[t] + call) / 2 : g->pot[t] + call));
            if (call && g->street[t] == 2 && roll < 600) return poker_archetype_raise(g, t, g->high[t] + (g->pot[t] + call));
            if (!call && roll < 200) return poker_archetype_raise(g, t, g->pot[t] / 3);
        }
        return 0;
    }
    if (e.share > .45f) {
        if (!call && river && roll < 300 && poker_can_raise(g, t))
            return poker_archetype_raise(g, t, g->pot[t] / 2);
        if (call && e.payout < call) return -1;
        return 0;
    }
    if (call && e.payout < call) return -1;
    return 0;
}
