#pragma once

// Shared preflop scaffold. The range policy includes the focused AK repair;
// the original rules control keeps its historical decisions and RNG sequence.
int32_t poker_rules_preflop(Poker* g, int t, uint64_t* rng, int focused) {
    int p = g->actor[t], r0 = g->hole[p][0][t] >> 2, r1 = g->hole[p][1][t] >> 2;
    int hi = r0 > r1 ? r0 : r1, lo = r0 < r1 ? r0 : r1;
    int suited = (g->hole[p][0][t] & 3) == (g->hole[p][1][t] & 3);
    int strength = r0 == r1 ? 30 + 3 * hi : 2 * hi + lo + 4 * suited + 4 * (hi - lo <= 2);
    unsigned roll = poker_random(rng) % 1000;
    uint32_t call = g->high[t] - g->bet[p][t];
    if (call > g->stack[p][t]) call = g->stack[p][t];
    int threshold = POKER_PLAYERS == 2 ? 17 : 28;
    int fold, raise;
    if (g->high[t] <= 100) {
        fold = strength < threshold && call && roll >= 80;
        raise = strength >= threshold + 4 || (roll < 70 && __builtin_popcount(g->live[t]) <= 3);
    } else {
        int pressure = g->high[t] > 1000 ? 12 : g->high[t] > 400 ? 6 : 0;
        fold = strength < threshold + 7 + pressure && call;
        if (g->high[t] >= 5000)
            fold = !(r0 == r1 && hi >= 10) && !(hi == 12 && lo == 11);
        raise = strength >= 57 || (strength >= 43 && g->high[t] <= 400 && roll < 180);
        if (focused && hi == 12 && lo == 11 && g->high[t] <= 400)
            raise = roll < (suited ? 900u : 650u);
    }
    if (fold) return -1;
    if (raise && poker_can_raise(g, t)) {
        uint32_t target = g->high[t] <= 100 ? 250 : 3 * g->high[t];
        uint32_t minimum = poker_min_raise(g, t), maximum = g->bet[p][t] + g->stack[p][t];
        if (target < minimum) target = minimum;
        return target < maximum ? target : maximum;
    }
    return 0;
}

// The one retained cheap rules control (historical policy ID 1).
int32_t poker_rules_bot(Poker* g, int t, uint64_t* rng) {
    if (!g->street[t]) return poker_rules_preflop(g, t, rng, 0);
    int p = g->actor[t], street = g->street[t];
    int r0 = g->hole[p][0][t] >> 2, r1 = g->hole[p][1][t] >> 2;
    unsigned roll = poker_random(rng) % 1000;
    uint32_t call = g->high[t] - g->bet[p][t];
    if (call > g->stack[p][t]) call = g->stack[p][t];
    uint8_t cards[7] = {g->hole[p][0][t], g->hole[p][1][t]};
    unsigned ranks = 0, suits[4] = {0};
    int board_hi = 0;
    for (int i = 0; i < street + 2; i++) {
        cards[i + 2] = g->board[i][t];
        int rank = cards[i + 2] >> 2;
        if (rank > board_hi) board_hi = rank;
    }
    for (int i = 0; i < street + 4; i++) {
        ranks |= 1u << (cards[i] >> 2); suits[cards[i] & 3]++;
    }
    uint32_t rank = poker_rank(cards, street + 4);
    int category = rank >> 24;
    const int values[9] = {14, 43, 69, 83, 88, 92, 97, 99, 100};
    int strength = values[category];
    if (category == 1) {
        int pair = ((rank >> 16) & 15) - 2;
        strength += pair >= board_hi ? 15 : -8;
        strength += (r0 == pair || r1 == pair) ? 0 : -18;
    }
    if (category == 2) strength -= (r0 < board_hi && r1 < board_hi) ? 10 : 0;
    int draw = 0;
    if (street < 3) {
        for (int s = 0; s < 4; s++)
            draw |= suits[s] == 4 && ((cards[0] & 3) == s || (cards[1] & 3) == s);
        int straight = 0;
        for (int r = 0; r < 9; r++) straight |= __builtin_popcount((ranks >> r) & 31) == 4;
        strength += 12 * draw + 6 * straight;
        draw |= straight;
    }
    int live = __builtin_popcount(g->live[t]);
    strength -= 5 * (live - 2) + 5;
    int price = 100 * call / (g->pot[t] + call);
    if ((call && strength < price + 25 + 8 * (call > g->pot[t] / 2))
        || (call > g->pot[t] && category < 2)) return -1;
    int raise = strength >= 74 || (!call && strength >= 53 && roll < 650)
        || (draw && roll < 130) || (!call && live == 2 && roll < 70);
    if (raise && poker_can_raise(g, t)) {
        uint32_t target = g->high[t] + 2 * (g->pot[t] + call) / 3;
        uint32_t minimum = poker_min_raise(g, t), maximum = g->bet[p][t] + g->stack[p][t];
        if (target < minimum) target = minimum;
        return target < maximum ? target : maximum;
    }
    return 0;
}
