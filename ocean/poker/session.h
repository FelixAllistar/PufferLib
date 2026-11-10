#pragma once
// Real cash sessions for the poker probes (2026-11-10): panel.c, exploit.c, allin.c and escalation.c. The user: "always
// memory, always simming real life cash games, walking in and playing real, not fake one hand and forget ... never
// again". Same table rules as lineup.c and the gen13 env (poker.h poker_table_next): stacks carry over between hands,
// every model keeps its recurrent memory across hands, and the seat tenure block counts the hands each occupant has
// been at the table. A seat that busts (under 2 BB) or grows past POKER_SESSION_CEILING walks out and a new player
// buys in: memory and tenure reset for that seat only. After `hands` hands the session ends and everyone sits down
// fresh. A hand's result is still stack after minus stack before, so per-hand BB means need no change.
#include "policy.h"

#define POKER_SESSION_CEILING (POKER_MAX_START / POKER_PLAYERS)  // poker.h's POKER_LIFE_CAP
#define POKER_SESSION_HANDS 1000                                 // a gen13 table life

typedef float PokerMemory[4 * POKER_POLICY_MAX_HIDDEN];

typedef struct {
    uint32_t stack[POKER_PLAYERS], buyin;
    long hands, left, buyins;  // buyins: walk-ins after the first sit-down, all seats
} PokerSession;

static inline PokerSession poker_session(uint32_t buyin_bb, long hands) {
    PokerSession s = {{0}, buyin_bb * POKER_BB, hands > 0 ? hands : POKER_SESSION_HANDS, 0, 0};
    assert(s.buyin <= POKER_SESSION_CEILING);
    return s;
}

static inline void poker_session_seat(PokerSession* s, Poker* g, PokerMemory* memory, int p) {
    s->stack[p] = s->buyin;
    if (memory) memset(memory[p], 0, sizeof(PokerMemory));
    g->tenure[p][0] = 0;
}

// Before each deal: a new session seats everyone fresh. Returns the stacks to deal with. Returns 1 when this hand
// starts a new session (so a caller can reseat its own per-seat state, such as bot styles).
static inline int poker_session_begin(PokerSession* s, Poker* g, PokerMemory* memory) {
    int fresh = s->left <= 0;
    if (fresh) {
        s->left = s->hands;
        for (int p = 0; p < POKER_PLAYERS; p++) poker_session_seat(s, g, memory, p);
    }
    s->left--;
    return fresh;
}

// After the hand: carry stacks; busted or over-the-ceiling seats walk out and a new player walks in. Returns a bit
// mask of the seats that changed occupant.
static inline unsigned poker_session_end(PokerSession* s, Poker* g, PokerMemory* memory) {
    unsigned changed = 0;
    for (int p = 0; p < POKER_PLAYERS; p++) {
        s->stack[p] = g->stack[p][0];
        if (s->stack[p] < 2 * POKER_BB || s->stack[p] > POKER_SESSION_CEILING) {
            poker_session_seat(s, g, memory, p);
            s->buyins++;
            changed |= 1u << p;
        } else if (g->tenure[p][0] < 65535) {
            g->tenure[p][0]++;
        }
    }
    return changed;
}
