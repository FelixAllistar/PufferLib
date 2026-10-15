#pragma once
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef POKER_PLAYERS
#define POKER_PLAYERS 6
#endif
#ifndef POKER_BATCH
#define POKER_BATCH 64
#endif
#define POKER_ACTIONS 8
// An aggressor may wait through the rest of this round and the next round's
// prefix: at most 2*(players-1) other actions, plus its own previous action.
#define POKER_HISTORY (2 * POKER_PLAYERS)
#define POKER_OBS (16 + 7 * POKER_PLAYERS + 4 * POKER_HISTORY)
#define POKER_BB 100
#define POKER_DONE 255

// One independent RNG per table; policy sampling uses a different stream.
typedef struct Poker {
    uint64_t rng[POKER_BATCH];
    uint32_t stack[POKER_PLAYERS][POKER_BATCH];
    uint16_t initial[POKER_PLAYERS][POKER_BATCH];
    uint16_t total[POKER_PLAYERS][POKER_BATCH];
    uint16_t bet[POKER_PLAYERS][POKER_BATCH];
    uint32_t reopen[POKER_PLAYERS][POKER_BATCH];
    uint32_t pot[POKER_BATCH];
    uint16_t high[POKER_BATCH], increment[POKER_BATCH];
    uint16_t live[POKER_BATCH], active[POKER_BATCH], pending[POKER_BATCH];
    uint8_t hole[POKER_PLAYERS][2][POKER_BATCH], board[5][POKER_BATCH];
    uint8_t actor[POKER_BATCH], button[POKER_BATCH], street[POKER_BATCH];
    uint16_t decisions[POKER_BATCH];
    uint16_t seen[POKER_PLAYERS][POKER_BATCH];
    uint32_t history[POKER_HISTORY][POKER_BATCH];
} Poker;

uint32_t poker_random(uint64_t* rng) {
    uint64_t x = *rng;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *rng = x;
    return (x * UINT64_C(2685821657736338717)) >> 32;
}

int poker_next(unsigned mask, int seat) {
    unsigned after = mask & ~((1u << (seat + 1)) - 1);
    return __builtin_ctz(after ? after : mask);
}

uint32_t poker_top(unsigned ranks, int n) {
    uint32_t result = 0;
    for (int i = 0; i < n; i++) {
        int r = 31 - __builtin_clz(ranks);
        result = (result << 4) | (r + 2);
        ranks ^= 1u << r;
    }
    return result << (4 * (5 - n));
}

int poker_straight(unsigned ranks) {
    unsigned run = ranks & (ranks >> 1) & (ranks >> 2) & (ranks >> 3) & (ranks >> 4);
    if (run) {
        return 37 - __builtin_clz(run);
    }
    return (ranks & 0x100f) == 0x100f ? 5 : 0;
}

// Exact best-of-seven rank, also valid for five and six cards. No lookup tables.
uint32_t poker_rank(const uint8_t* cards, int n) {
    unsigned one = 0, two = 0, three = 0, four = 0, suits[4] = {0};
    for (int i = 0; i < n; i++) {
        unsigned bit = 1u << (cards[i] >> 2);
        four |= three & bit;
        three |= two & bit;
        two |= one & bit;
        one |= bit;
        suits[cards[i] & 3] |= bit;
    }
    unsigned flush = 0;
    for (int i = 0; i < 4; i++) {
        if (__builtin_popcount(suits[i]) >= 5) {
            flush = suits[i];
        }
    }
    int straight = poker_straight(flush);
    if (straight) {
        return (8u << 24) | (straight << 16);
    }
    if (four) {
        return (7u << 24) | poker_top(four, 1) | (poker_top(one ^ four, 1) >> 4);
    }
    unsigned trip = three ? 1u << (31 - __builtin_clz(three)) : 0;
    if (trip && (two ^ trip)) {
        return (6u << 24) | poker_top(trip, 1) | (poker_top(two ^ trip, 1) >> 4);
    }
    if (flush) {
        return (5u << 24) | poker_top(flush, 5);
    }
    straight = poker_straight(one);
    if (straight) {
        return (4u << 24) | (straight << 16);
    }
    if (trip) {
        return (3u << 24) | poker_top(trip, 1) | (poker_top(one ^ trip, 2) >> 4);
    }
    if (__builtin_popcount(two) >= 2) {
        unsigned pairs = two;
        while (__builtin_popcount(pairs) > 2) {
            pairs &= pairs - 1;
        }
        return (2u << 24) | poker_top(pairs, 2) | (poker_top(one ^ pairs, 1) >> 8);
    }
    if (two) {
        return (1u << 24) | poker_top(two, 1) | (poker_top(one ^ two, 3) >> 4);
    }
    return poker_top(one, 5);
}

void poker_pay(Poker* g, int t, int p, uint32_t amount) {
    g->stack[p][t] -= amount;
    g->bet[p][t] += amount;
    g->total[p][t] += amount;
    g->pot[t] += amount;
    if (!g->stack[p][t]) {
        g->active[t] &= ~(1u << p);
    }
}

void poker_settle(Poker* g, int t) {
    unsigned live = g->live[t];
    if (!(live & (live - 1))) {
        g->stack[__builtin_ctz(live)][t] += g->pot[t];
    } else {
        uint32_t rank[POKER_PLAYERS] = {0};
        uint8_t cards[7];
        for (int i = 0; i < 5; i++) {
            cards[i + 2] = g->board[i][t];
        }
        for (int p = 0; p < POKER_PLAYERS; p++) {
            if (live & (1u << p)) {
                cards[0] = g->hole[p][0][t];
                cards[1] = g->hole[p][1][t];
                rank[p] = poker_rank(cards, 7);
            }
        }
        uint32_t lower = 0;
        for (;;) {
            uint32_t upper = UINT32_MAX, best = 0;
            unsigned contributors = 0, winners = 0;
            for (int p = 0; p < POKER_PLAYERS; p++) {
                uint32_t c = g->total[p][t];
                if (c > lower) {
                    contributors |= 1u << p;
                    if (live & (1u << p)) {
                        upper = c < upper ? c : upper;
                        if (rank[p] > best) {
                            best = rank[p];
                            winners = 0;
                        }
                        if (rank[p] == best) {
                            winners |= 1u << p;
                        }
                    }
                }
            }
            if (!contributors) {
                break;
            }
            if (!(contributors & (contributors - 1))) {
                winners = contributors; // Return an unmatched bet.
            }
            assert(winners);
            uint32_t chips = 0;
            for (int p = 0; p < POKER_PLAYERS; p++) {
                uint32_t c = g->total[p][t];
                if (c > lower) {
                    chips += (c < upper ? c : upper) - lower;
                }
            }
            uint32_t count = __builtin_popcount(winners);
            uint32_t share = chips / count, odd = chips % count;
            int p = g->button[t];
            for (unsigned left = winners; left; left &= ~(1u << p)) {
                p = poker_next(left, p);
                g->stack[p][t] += share + (odd > 0);
                odd -= odd > 0;
            }
            lower = upper;
        }
        g->street[t] = 3;
    }
    g->pot[t] = 0;
    g->pending[t] = 0;
    g->actor[t] = POKER_DONE;
}

void poker_advance(Poker* g, int t, int last) {
    unsigned live = g->live[t], active = g->active[t];
    if (!(live & (live - 1))) {
        poker_settle(g, t);
        return;
    }
    g->pending[t] &= active;
    if (active && !(active & (active - 1))) {
        int p = __builtin_ctz(active);
        uint32_t covered = 0;
        for (int i = 0; i < POKER_PLAYERS; i++) {
            if (i != p && (live & (1u << i))) {
                covered = g->bet[i][t] > covered ? g->bet[i][t] : covered;
            }
        }
        g->high[t] = covered;
        if (g->bet[p][t] >= g->high[t]) {
            g->pending[t] = 0;
        }
    }
    if (g->pending[t]) {
        g->actor[t] = poker_next(g->pending[t], last);
        return;
    }
    if (g->street[t] == 3 || __builtin_popcount(active) < 2) {
        poker_settle(g, t);
        return;
    }
    g->street[t]++;
    g->high[t] = 0;
    g->increment[t] = POKER_BB;
    for (int p = 0; p < POKER_PLAYERS; p++) {
        g->bet[p][t] = 0;
        g->reopen[p][t] = 0;
    }
    g->pending[t] = active;
    g->actor[t] = poker_next(active, g->button[t]);
}

// Deck supplies 2*players hole cards followed by five board cards, no burns.
// Predrawing the board has the same distribution; it stays private until dealt.
void poker_reset(Poker* g, int t, int button, const uint32_t* stacks,
    const uint8_t* deck) {
    assert(button < POKER_PLAYERS);
    g->button[t] = button;
    g->street[t] = 0;
    g->decisions[t] = 0;
    g->pot[t] = 0;
    g->high[t] = POKER_BB;
    g->increment[t] = POKER_BB;
    g->live[t] = g->active[t] = (1u << POKER_PLAYERS) - 1;
    for (int p = 0; p < POKER_PLAYERS; p++) {
        assert(stacks[p] > 0 && stacks[p] <= 65535);
        g->stack[p][t] = g->initial[p][t] = stacks[p];
        g->bet[p][t] = g->total[p][t] = g->reopen[p][t] = 0;
        g->seen[p][t] = 0;
        g->hole[p][0][t] = deck[2 * p];
        g->hole[p][1][t] = deck[2 * p + 1];
    }
    for (int i = 0; i < 5; i++) {
        g->board[i][t] = deck[2 * POKER_PLAYERS + i];
    }
    int sb = POKER_PLAYERS == 2 ? button : (button + 1) % POKER_PLAYERS;
    int bb = (sb + 1) % POKER_PLAYERS;
    poker_pay(g, t, sb, stacks[sb] < POKER_BB / 2 ? stacks[sb] : POKER_BB / 2);
    poker_pay(g, t, bb, stacks[bb] < POKER_BB ? stacks[bb] : POKER_BB);
    g->pending[t] = g->active[t];
    poker_advance(g, t, bb);
}

void poker_deal(Poker* g, int t, int button, const uint32_t* stacks) {
    uint8_t deck[2 * POKER_PLAYERS + 5];
    uint64_t used = 0;
    for (int i = 0; i < 2 * POKER_PLAYERS + 5; i++) {
        uint32_t x, card;
        do {
            do {
                x = poker_random(&g->rng[t]);
            } while (x < 48); // Rejection makes reduction modulo 52 unbiased.
            card = x % 52;
        } while (used & (UINT64_C(1) << card));
        used |= UINT64_C(1) << card;
        deck[i] = card;
    }
    poker_reset(g, t, button, stacks, deck);
}

// Exact action ABI: -1 fold, 0 check/call, positive = total street raise-to.
// Generated actions are legal; imports validate once before calling this kernel.
uint32_t poker_min_raise(Poker* g, int t) {
    return g->high[t] + g->increment[t];
}

int poker_can_raise(Poker* g, int t) {
    int p = g->actor[t];
    if (g->high[t] < g->reopen[p][t]
        || g->stack[p][t] + g->bet[p][t] <= g->high[t]) {
        return 0;
    }
    unsigned others = g->active[t] & ~(1u << p);
    while (others) {
        int q = __builtin_ctz(others);
        if (g->stack[q][t] + g->bet[q][t] > g->high[t]) {
            return 1;
        }
        others &= others - 1;
    }
    return 0;
}

unsigned poker_actions(Poker* g, int t, int32_t* amounts) {
    if (g->actor[t] == POKER_DONE) {
        return 0;
    }
    int p = g->actor[t];
    uint32_t call = g->high[t] - g->bet[p][t];
    amounts[0] = -1;
    amounts[1] = 0;
    unsigned mask = 2u | (call > 0);
    if (!poker_can_raise(g, t)) {
        return mask;
    }
    uint32_t maximum = g->bet[p][t] + g->stack[p][t];
    uint32_t minimum = poker_min_raise(g, t), pot = g->pot[t] + call;
    uint32_t values[6] = {minimum, g->high[t] + pot / 3,
        g->high[t] + 2 * pot / 3, g->high[t] + pot,
        g->high[t] + 2 * pot, maximum};
    for (int i = 0; i < 6; i++) {
        uint32_t v = values[i] < minimum ? minimum : values[i];
        v = v > maximum ? maximum : v;
        amounts[i + 2] = v;
        unsigned duplicate = 0;
        for (int j = 2; j < i + 2; j++) {
            duplicate |= amounts[j] == (int32_t)v;
        }
        mask |= (!duplicate) << (i + 2);
    }
    return mask;
}

void poker_step(Poker* g, int t, int32_t action) {
    int p = g->actor[t];
    if (p == POKER_DONE) {
        return;
    }
    unsigned bit = 1u << p;
    g->pending[t] &= ~bit;
    uint32_t event = (p << 16) | (g->street[t] << 20);
    int slot = g->decisions[t] % POKER_HISTORY;
    g->seen[p][t] = g->decisions[t]++;
    if (action < 0) {
        assert(g->bet[p][t] < g->high[t]);
        g->live[t] &= ~bit;
        g->active[t] &= ~bit;
    } else {
        uint32_t target = action ? action : g->high[t];
        event |= (action ? 3 : target > g->bet[p][t] ? 2 : 1) << 22;
        uint32_t pay = target - g->bet[p][t];
        pay = pay < g->stack[p][t] ? pay : g->stack[p][t];
        assert(!action || (poker_can_raise(g, t) && target > g->high[t]
            && (target >= poker_min_raise(g, t) || pay == g->stack[p][t])));
        assert(!action || target <= g->bet[p][t] + g->stack[p][t]);
        poker_pay(g, t, p, pay);
        if (target > g->high[t]) {
            uint32_t increase = target - g->high[t];
            if (increase >= g->increment[t]) {
                g->increment[t] = increase;
            }
            g->high[t] = target;
            g->pending[t] = g->active[t] & ~bit;
        }
        g->reopen[p][t] = g->high[t] + g->increment[t];
    }
    g->history[slot][t] = event | g->bet[p][t];
    poker_advance(g, t, p);
}

// Public state and every event since this seat's previous decision, including
// its previous action. Recurrent state belongs to table+seat. No inactive rows.
void poker_observe(Poker* g, int t, int p, float* obs) {
    int board_count = g->street[t] ? g->street[t] + 2 : 0;
    obs[0] = g->street[t] / 3.0f;
    obs[1] = g->pot[t] / 20000.0f;
    obs[2] = g->high[t] / 20000.0f;
    obs[3] = g->increment[t] / 20000.0f;
    obs[4] = ((g->button[t] + POKER_PLAYERS - p) % POKER_PLAYERS)
        / (float)(POKER_PLAYERS - 1);
    obs[5] = __builtin_popcount(g->live[t]) / (float)POKER_PLAYERS;
    obs[6] = g->hole[p][0][t] / 52.0f;
    obs[7] = g->hole[p][1][t] / 52.0f;
    for (int i = 0; i < 5; i++) {
        obs[8 + i] = i < board_count ? g->board[i][t] / 52.0f : 1;
    }
    obs[13] = g->actor[t] == p;
    obs[14] = g->decisions[t] / 1024.0f;
    obs[15] = g->actor[t] == POKER_DONE;
    for (int i = 0; i < POKER_PLAYERS; i++) {
        int seat = (i + p) % POKER_PLAYERS;
        float* row = obs + 16 + 7 * i;
        row[0] = g->stack[seat][t] / 20000.0f;
        row[1] = g->bet[seat][t] / 20000.0f;
        row[2] = g->total[seat][t] / 20000.0f;
        row[3] = g->initial[seat][t] / 20000.0f;
        row[4] = !!(g->live[t] & (1u << seat));
        row[5] = !!(g->active[t] & (1u << seat));
        row[6] = !!(g->pending[t] & (1u << seat));
    }
    float* events = obs + 16 + 7 * POKER_PLAYERS;
    int count = g->decisions[t] - g->seen[p][t];
    assert(g->actor[t] != p || count <= POKER_HISTORY);
    count = count < POKER_HISTORY ? count : POKER_HISTORY;
    for (int i = 0; i < POKER_HISTORY; i++) {
        uint32_t e = i < count ? g->history[(g->decisions[t] - count + i)
            % POKER_HISTORY][t] : 0;
        events[4 * i] = i < count ? (1 + (((e >> 16) & 15) + POKER_PLAYERS - p)
            % POKER_PLAYERS) / (float)POKER_PLAYERS : 0;
        events[4 * i + 1] = ((e >> 20) & 3) / 3.0f;
        events[4 * i + 2] = ((e >> 22) & 3) / 3.0f;
        events[4 * i + 3] = (e & 65535) / 20000.0f;
    }
}

// Our fixed rule baseline. Style 0 = initial, 1 = conservative, 2 = positional.
// Scores below are heuristics, not calibrated equity estimates.
int32_t poker_bot(Poker* g, int t, uint64_t* rng, int style) {
    int p = g->actor[t], street = g->street[t];
    int r0 = g->hole[p][0][t] >> 2, r1 = g->hole[p][1][t] >> 2;
    int hi = r0 > r1 ? r0 : r1, lo = r0 < r1 ? r0 : r1;
    int suited = (g->hole[p][0][t] & 3) == (g->hole[p][1][t] & 3);
    int live = __builtin_popcount(g->live[t]);
    uint32_t roll = poker_random(rng) % 1000;
    uint32_t call = g->high[t] - g->bet[p][t];
    call = call < g->stack[p][t] ? call : g->stack[p][t];
    int strength = 2 * hi + lo + 4 * suited + 4 * (hi - lo <= 2);
    if (r0 == r1) {
        strength = 30 + 3 * hi;
    }
    int raise = 0, fold = 0;
    if (!street) {
        int late = (p + POKER_PLAYERS - g->button[t]) % POKER_PLAYERS;
        int threshold = POKER_PLAYERS == 2 ? 13 : 24;
        threshold += style == 1 ? 4 : 0;
        threshold -= style == 2 && late == 0 ? 5 : 0;
        if (g->high[t] <= 100) {
            fold = strength < threshold && call && roll >= 80;
            raise = strength >= threshold + 4 || (roll < 70 && live <= 3);
        } else {
            int pressure = g->high[t] > 1000 ? 12 : g->high[t] > 400 ? 6 : 0;
            fold = strength < threshold + 7 + pressure && call;
            if (g->high[t] >= 5000) {
                fold = !(r0 == r1 && hi >= 10) && !(hi == 12 && lo == 11);
            }
            raise = strength >= 57 || (strength >= 43 && g->high[t] <= 400 && roll < 180);
        }
    } else {
        uint8_t cards[7] = {g->hole[p][0][t], g->hole[p][1][t]};
        unsigned ranks = 0, suit_count[4] = {0};
        int board_hi = 0;
        for (int i = 0; i < street + 2; i++) {
            cards[i + 2] = g->board[i][t];
            int rank = cards[i + 2] >> 2;
            board_hi = rank > board_hi ? rank : board_hi;
        }
        for (int i = 0; i < street + 4; i++) {
            ranks |= 1u << (cards[i] >> 2);
            suit_count[cards[i] & 3]++;
        }
        uint32_t rank = poker_rank(cards, street + 4);
        int category = rank >> 24;
        int values[9] = {14, 43, 69, 83, 88, 92, 97, 99, 100};
        strength = values[category];
        if (category == 1) {
            int pair = ((rank >> 16) & 15) - 2;
            strength += pair >= board_hi ? 15 : -8;
            strength += (r0 == pair || r1 == pair) ? 0 : -18;
        }
        if (category == 2) {
            strength -= (r0 < board_hi && r1 < board_hi) ? 10 : 0;
        }
        int draw = 0;
        if (street < 3) {
            for (int i = 0; i < 4; i++) {
                draw |= suit_count[i] == 4 && ((cards[0] & 3) == i || (cards[1] & 3) == i);
            }
            int straight_draw = 0;
            for (int i = 0; i < 9; i++) {
                straight_draw |= __builtin_popcount((ranks >> i) & 31) == 4;
            }
            strength += 12 * draw + 6 * straight_draw;
            draw |= straight_draw;
        }
        strength -= 5 * (live - 2);
        strength -= style == 1 ? 5 : 0;
        int price = 100 * call / (g->pot[t] + call);
        fold = call && strength < price + 25 + 8 * (call > g->pot[t] / 2);
        if (style && call > g->pot[t] && category < 2) {
            fold = 1;
        }
        raise = strength >= 74 || (!call && strength >= 53 && roll < 650)
            || (draw && roll < 130) || (!call && live == 2 && roll < 70);
    }
    if (fold) {
        return -1;
    }
    if (raise && poker_can_raise(g, t)) {
        uint32_t target = street ? g->high[t] + 2 * (g->pot[t] + call) / 3
            : g->high[t] <= 100 ? 250 : 3 * g->high[t];
        uint32_t minimum = poker_min_raise(g, t);
        uint32_t maximum = g->stack[p][t] + g->bet[p][t];
        target = target < minimum ? minimum : target;
        return target < maximum ? target : maximum;
    }
    return 0;
}

// Dense acting-seat requests for a self-play collector. The caller owns policy
// grouping and recurrent state keyed by (table,seat), and resets terminal tables
// only after consuming every seat's payoff. No inference for inactive seats.
int poker_requests(Poker* g, int tables, uint16_t* ids, uint8_t* seats,
    float* observations, uint8_t* masks, int32_t* amounts) {
    int count = 0;
    for (int t = 0; t < tables; t++) {
        int p = g->actor[t];
        if (p == POKER_DONE) {
            continue;
        }
        ids[count] = t;
        seats[count] = p;
        poker_observe(g, t, p, observations + count * POKER_OBS);
        unsigned mask = poker_actions(g, t, amounts + count * POKER_ACTIONS);
        for (int a = 0; a < POKER_ACTIONS; a++) {
            masks[count * POKER_ACTIONS + a] = (mask >> a) & 1;
        }
        count++;
    }
    return count;
}

void poker_respond(Poker* g, int count, const uint16_t* ids, const int32_t* actions,
    uint8_t* terminals, float* rewards) {
    for (int i = 0; i < count; i++) {
        int t = ids[i];
        poker_step(g, t, actions[i]);
        int done = g->actor[t] == POKER_DONE;
        terminals[i] = done;
        for (int p = 0; p < POKER_PLAYERS; p++) {
            rewards[p * count + i] = done
                ? ((int32_t)g->stack[p][t] - (int32_t)g->initial[p][t]) / 100.0f : 0;
        }
    }
}
