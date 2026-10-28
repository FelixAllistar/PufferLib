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
// Card features (gen1, 2026-10-25): each hole card's rank and suit one-hot, pair and suited flags, the board's rank
// and suit counts, and the made-hand class postflop. Cards were one scalar id/52 each before, and gen0 learned to
// ignore them. Then rule hints: preflop strength (the rules bot's score), pot odds, call/stack, stack/pot, flush and
// straight draws, and whether the hole cards improve on the board. Changes checkpoint compatibility (gen0 has 106
// inputs at six seats).
#define POKER_CARD_FEATURES (2 * 17 + 2 + 17 + 9 + 7)
#define POKER_OBS (16 + 7 * POKER_PLAYERS + 4 * POKER_HISTORY + POKER_CARD_FEATURES)
#define POKER_BB 100
// Small blind; the web's $0.02 / $0.05 micro stakes build with -DPOKER_SB=40. Training keeps half the big blind.
#ifndef POKER_SB
#define POKER_SB (POKER_BB / 2)
#endif
#define POKER_DONE 255

// The web cash table can carry deep stacks across hands. Default training
// builds retain their compact layout and historical event encoding.
#ifdef POKER_WIDE_CHIPS
typedef uint32_t PokerAmount;
typedef uint64_t PokerEvent;
#define POKER_EVENT_SHIFT 32
#define POKER_MAX_START 10000000u
#else
typedef uint16_t PokerAmount;
typedef uint32_t PokerEvent;
#define POKER_EVENT_SHIFT 16
#define POKER_MAX_START 65535u
#endif
#define POKER_EVENT_AMOUNT ((((PokerEvent)1) << POKER_EVENT_SHIFT) - 1)

// One independent RNG per table; policy sampling uses a different stream.
typedef struct Poker {
    uint64_t rng[POKER_BATCH];
    uint32_t stack[POKER_PLAYERS][POKER_BATCH];
    PokerAmount initial[POKER_PLAYERS][POKER_BATCH];
    PokerAmount total[POKER_PLAYERS][POKER_BATCH];
    PokerAmount bet[POKER_PLAYERS][POKER_BATCH];
    uint32_t reopen[POKER_PLAYERS][POKER_BATCH];
    uint32_t pot[POKER_BATCH];
    PokerAmount high[POKER_BATCH], increment[POKER_BATCH];
    uint16_t live[POKER_BATCH], active[POKER_BATCH], pending[POKER_BATCH];
    uint8_t hole[POKER_PLAYERS][2][POKER_BATCH], board[5][POKER_BATCH];
    uint8_t actor[POKER_BATCH], button[POKER_BATCH], street[POKER_BATCH];
    // Public street-local aggression, retained beyond the observation ring.
    uint8_t raises[POKER_BATCH], last_raiser[POKER_BATCH];
    // Strongest public action on each street: 0 none, 1 check, 2 call, 3 raise.
    // Two bits per street; used by range baselines without reading private cards.
    uint8_t line[POKER_PLAYERS][POKER_BATCH];
    uint16_t decisions[POKER_BATCH];
    uint16_t seen[POKER_PLAYERS][POKER_BATCH];
    PokerEvent history[POKER_HISTORY][POKER_BATCH];
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
    g->raises[t] = 0;
    g->last_raiser[t] = POKER_DONE;
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
    g->raises[t] = 0;
    g->last_raiser[t] = POKER_DONE;
    g->decisions[t] = 0;
    g->pot[t] = 0;
    g->high[t] = POKER_BB;
    g->increment[t] = POKER_BB;
    g->live[t] = g->active[t] = (1u << POKER_PLAYERS) - 1;
    for (int p = 0; p < POKER_PLAYERS; p++) {
        assert(stacks[p] > 0 && stacks[p] <= POKER_MAX_START);
        g->stack[p][t] = g->initial[p][t] = stacks[p];
        g->bet[p][t] = g->total[p][t] = g->reopen[p][t] = 0;
        g->seen[p][t] = 0;
        g->line[p][t] = 0;
        g->hole[p][0][t] = deck[2 * p];
        g->hole[p][1][t] = deck[2 * p + 1];
    }
    for (int i = 0; i < 5; i++) {
        g->board[i][t] = deck[2 * POKER_PLAYERS + i];
    }
    int sb = POKER_PLAYERS == 2 ? button : (button + 1) % POKER_PLAYERS;
    int bb = (sb + 1) % POKER_PLAYERS;
    poker_pay(g, t, sb, stacks[sb] < POKER_SB ? stacks[sb] : POKER_SB);
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
    PokerEvent event = ((PokerEvent)p << POKER_EVENT_SHIFT)
        | ((PokerEvent)g->street[t] << (POKER_EVENT_SHIFT + 4));
    int slot = g->decisions[t] % POKER_HISTORY;
    g->seen[p][t] = g->decisions[t]++;
    if (action < 0) {
        assert(g->bet[p][t] < g->high[t]);
        g->live[t] &= ~bit;
        g->active[t] &= ~bit;
    } else {
        uint32_t target = action ? action : g->high[t];
        unsigned kind = action ? 3 : target > g->bet[p][t] ? 2 : 1;
        unsigned shift = 2 * g->street[t];
        if (kind > ((g->line[p][t] >> shift) & 3u))
            g->line[p][t] = (g->line[p][t] & ~(3u << shift)) | (kind << shift);
        event |= (PokerEvent)kind << (POKER_EVENT_SHIFT + 6);
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
                if (g->raises[t] < 4) g->raises[t]++;
                g->last_raiser[t] = p;
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
        PokerEvent e = i < count ? g->history[(g->decisions[t] - count + i)
            % POKER_HISTORY][t] : 0;
        events[4 * i] = i < count ? (1 + (((e >> POKER_EVENT_SHIFT) & 15) + POKER_PLAYERS - p)
            % POKER_PLAYERS) / (float)POKER_PLAYERS : 0;
        events[4 * i + 1] = ((e >> (POKER_EVENT_SHIFT + 4)) & 3) / 3.0f;
        events[4 * i + 2] = ((e >> (POKER_EVENT_SHIFT + 6)) & 3) / 3.0f;
        events[4 * i + 3] = (e & POKER_EVENT_AMOUNT) / 20000.0f;
    }
    float* cards = events + 4 * POKER_HISTORY;
    memset(cards, 0, POKER_CARD_FEATURES * sizeof(float));
    uint8_t hand[7] = {g->hole[p][0][t], g->hole[p][1][t]};
    for (int c = 0; c < 2; c++) {
        cards[17 * c + (hand[c] >> 2)] = 1;
        cards[17 * c + 13 + (hand[c] & 3)] = 1;
    }
    cards[34] = (hand[0] >> 2) == (hand[1] >> 2);
    cards[35] = (hand[0] & 3) == (hand[1] & 3);
    for (int i = 0; i < board_count; i++) {
        hand[2 + i] = g->board[i][t];
        cards[36 + (hand[2 + i] >> 2)] += 0.25f;
        cards[49 + (hand[2 + i] & 3)] += 0.2f;
    }
    uint32_t made = board_count ? poker_rank(hand, board_count + 2) : 0;
    if (board_count) cards[53 + (made >> 24)] = 1;
    int r0 = hand[0] >> 2, r1 = hand[1] >> 2, hi = r0 > r1 ? r0 : r1, lo = r0 < r1 ? r0 : r1;
    int suited = (hand[0] & 3) == (hand[1] & 3);
    cards[62] = (r0 == r1 ? 30 + 3 * hi : 2 * hi + lo + 4 * suited + 4 * (hi - lo <= 2)) / 66.0f;
    uint32_t call = g->high[t] > g->bet[p][t] ? g->high[t] - g->bet[p][t] : 0;
    if (call > g->stack[p][t]) call = g->stack[p][t];
    cards[63] = g->pot[t] + call ? (float)call / (g->pot[t] + call) : 0;
    cards[64] = g->stack[p][t] ? (float)call / g->stack[p][t] : 1;
    float spr = g->pot[t] ? (float)g->stack[p][t] / g->pot[t] : 10;
    cards[65] = spr < 10 ? spr / 10 : 1;
    if (board_count && board_count < 5) {
        int suits[4] = {0};
        unsigned ranks = 0;
        for (int i = 0; i < board_count + 2; i++) { suits[hand[i] & 3]++; ranks |= 1u << (hand[i] >> 2); }
        cards[66] = (suits[hand[0] & 3] == 4 || suits[hand[1] & 3] == 4) && (made >> 24) < 5;
        unsigned low = (ranks << 1) | ((ranks >> 12) & 1);  // bit 0: the ace playing low, bit k+1: rank k
        int run = 0;  // four of five consecutive ranks (open-ended or gutshot)
        for (int r = 0; r + 5 <= 14; r++) run |= __builtin_popcount((low >> r) & 31) == 4;
        cards[67] = run && (made >> 24) < 4;
    }
    if (board_count >= 5) cards[68] = made > poker_rank(hand + 2, 5);
    else if (board_count) cards[68] = (made >> 24) > 0 && (r0 == r1 || ((made >> 16) & 15) - 2 == r0 || ((made >> 16) & 15) - 2 == r1);
}

#ifdef POKER_FULLHOUSE_MODEL
#include "fullhouse.h"
#define POKER_BOT_MASK (146u | 0x700u)
#else
#define POKER_BOT_MASK (130u | 0x700u)
#endif
// Stable IDs: 1 original rules, 4 Fullhouse, 7 current range policy; 8 maniac, 9 station, 10 tight
// (archetypes.h, the evaluation panel).
// Removed experiment IDs remain in frozen report sources and old binaries.
#define POKER_BOT_COUNT 11
#include "rules.h"
#include "equity.h"
#include "archetypes.h"

int poker_bot_supported(int policy) {
    return policy >= 0 && policy < POKER_BOT_COUNT && (POKER_BOT_MASK & (1u << policy));
}

int32_t poker_decide(Poker* g, int t, uint64_t* rng, int policy, int samples,
    const PokerProfile* profiles) {
    assert(poker_bot_supported(policy));
    assert(samples == 0 || samples == 32 || samples == 64 || samples == 128);
    if (policy == 7) return poker_equity_bot(g, t, rng, samples, profiles);
    if (policy == 8) return poker_maniac_bot(g, t, rng);
    if (policy == 9) return poker_station_bot(g, t, rng);
    if (policy == 10) return poker_tight_bot(g, t, rng);
#ifdef POKER_FULLHOUSE_MODEL
    if (policy == 4) return poker_fullhouse_bot(g, t, rng);
#endif
    return poker_rules_bot(g, t, rng);
}

int32_t poker_bot(Poker* g, int t, uint64_t* rng, int policy) {
    return poker_decide(g, t, rng, policy, 0, NULL);
}

int32_t poker_bot_with_history(Poker* g, int t, uint64_t* rng, int policy,
    const PokerProfile* profiles) {
    return poker_decide(g, t, rng, policy, 0, profiles);
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
