#pragma once
// Poker state sets (2026-10-28): mid-hand snapshots that training hands can start from, so the learner gets
// turn/river and big-pot experience that shove-or-fold self-play never reaches (the "loss minimization" failure).
// gen_states.c plays cash sessions between every bot and policy we have and captures decision points; a state is
// stored already rotated so the deciding seat is seat 0 (the learner seat), with each seat's creator recorded.
// A state file is a flat array of PokerState records (no header); each record carries POKER_STATE_MAGIC, which
// encodes the record size, so a file from a build with a different layout (POKER_WIDE_CHIPS) fails to load.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core.h"

#define POKER_STATE_MAGIC (0x50530000u | (uint32_t)sizeof(PokerState))
// Creator ids: bot ids as in poker_decide (1 rules, 4 Fullhouse, 7 range, 8 maniac, 9 station, 10 tight,
// 11 trapper); 100 + k for policy k of the generator's --policy list.
#define POKER_STATE_POLICY 100

typedef struct PokerState {
    uint32_t magic;
    uint8_t creator[POKER_PLAYERS];
    uint8_t street, actor, button, raises, last_raiser, pad;
    uint32_t stack[POKER_PLAYERS], reopen[POKER_PLAYERS], pot;
    PokerAmount initial[POKER_PLAYERS], total[POKER_PLAYERS], bet[POKER_PLAYERS], high, increment;
    uint16_t live, active, pending, decisions;
    uint16_t seen[POKER_PLAYERS];
    uint8_t hole[POKER_PLAYERS][2], board[5], line[POKER_PLAYERS];
    PokerEvent history[POKER_HISTORY];
} PokerState;

static inline unsigned poker_state_rotate_mask(unsigned mask, int q) {
    unsigned out = 0;
    for (int i = 0; i < POKER_PLAYERS; i++)
        if (mask & (1u << ((i + q) % POKER_PLAYERS))) out |= 1u << i;
    return out;
}

// Snapshot table t at the current actor's decision, rotated so that seat becomes seat 0. styles[p]: creator id of
// table seat p. Events older than the history window are zeroed (poker_observe never reads them).
static void poker_state_capture(const Poker* g, int t, const uint8_t* styles, PokerState* s) {
    int q = g->actor[t];
    assert(q != POKER_DONE);
    memset(s, 0, sizeof(*s));
    s->magic = POKER_STATE_MAGIC;
    for (int i = 0; i < POKER_PLAYERS; i++) {
        int p = (i + q) % POKER_PLAYERS;
        s->creator[i] = styles[p];
        s->stack[i] = g->stack[p][t];
        s->reopen[i] = g->reopen[p][t];
        s->initial[i] = g->initial[p][t];
        s->total[i] = g->total[p][t];
        s->bet[i] = g->bet[p][t];
        s->seen[i] = g->seen[p][t];
        s->hole[i][0] = g->hole[p][0][t];
        s->hole[i][1] = g->hole[p][1][t];
        s->line[i] = g->line[p][t];
    }
    s->street = g->street[t];
    s->actor = 0;
    s->button = (g->button[t] + POKER_PLAYERS - q) % POKER_PLAYERS;
    s->raises = g->raises[t];
    s->last_raiser = g->last_raiser[t] == POKER_DONE ? POKER_DONE
        : (g->last_raiser[t] + POKER_PLAYERS - q) % POKER_PLAYERS;
    s->pot = g->pot[t];
    s->high = g->high[t];
    s->increment = g->increment[t];
    s->live = poker_state_rotate_mask(g->live[t], q);
    s->active = poker_state_rotate_mask(g->active[t], q);
    s->pending = poker_state_rotate_mask(g->pending[t], q);
    s->decisions = g->decisions[t];
    for (int i = 0; i < 5; i++) s->board[i] = g->board[i][t];
    int valid = g->decisions[t] < POKER_HISTORY ? g->decisions[t] : POKER_HISTORY;
    for (int k = 0; k < valid; k++) {
        int slot = (g->decisions[t] - 1 - k) % POKER_HISTORY;
        PokerEvent e = g->history[slot][t];
        int p = (int)((e >> POKER_EVENT_SHIFT) & 15);
        e &= ~((PokerEvent)15 << POKER_EVENT_SHIFT);
        s->history[slot] = e | ((PokerEvent)((p + POKER_PLAYERS - q) % POKER_PLAYERS) << POKER_EVENT_SHIFT);
    }
}

// Load a state onto table t; seat 0 is to act. Board cards not yet dealt are redrawn from the cards no seat holds
// (table RNG), so one state plays out many runouts; every hole card stays as captured.
static void poker_state_restore(Poker* g, int t, const PokerState* s) {
    assert(s->magic == POKER_STATE_MAGIC);
    for (int p = 0; p < POKER_PLAYERS; p++) {
        g->stack[p][t] = s->stack[p];
        g->reopen[p][t] = s->reopen[p];
        g->initial[p][t] = s->initial[p];
        g->total[p][t] = s->total[p];
        g->bet[p][t] = s->bet[p];
        g->seen[p][t] = s->seen[p];
        g->hole[p][0][t] = s->hole[p][0];
        g->hole[p][1][t] = s->hole[p][1];
        g->line[p][t] = s->line[p];
    }
    g->street[t] = s->street;
    g->actor[t] = s->actor;
    g->button[t] = s->button;
    g->raises[t] = s->raises;
    g->last_raiser[t] = s->last_raiser;
    g->pot[t] = s->pot;
    g->high[t] = s->high;
    g->increment[t] = s->increment;
    g->live[t] = s->live;
    g->active[t] = s->active;
    g->pending[t] = s->pending;
    g->decisions[t] = s->decisions;
    for (int k = 0; k < POKER_HISTORY; k++) g->history[k][t] = s->history[k];
    int dealt = s->street ? s->street + 2 : 0;
    uint64_t used = 0;
    for (int p = 0; p < POKER_PLAYERS; p++) used |= (UINT64_C(1) << s->hole[p][0]) | (UINT64_C(1) << s->hole[p][1]);
    for (int i = 0; i < dealt; i++) {
        g->board[i][t] = s->board[i];
        used |= UINT64_C(1) << s->board[i];
    }
    for (int i = dealt; i < 5; i++) {
        uint32_t x, card;
        do {
            do x = poker_random(&g->rng[t]); while (x < 48);
            card = x % 52;
        } while (used & (UINT64_C(1) << card));
        used |= UINT64_C(1) << card;
        g->board[i][t] = card;
    }
}

typedef struct PokerStateSet {
    PokerState* states;
    size_t count;
} PokerStateSet;

static PokerStateSet* poker_state_load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "poker: cannot open state file %s\n", path); abort(); }
    fseek(f, 0, SEEK_END);
    long bytes = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (bytes <= 0 || bytes % sizeof(PokerState)) {
        fprintf(stderr, "poker: %s is not a state file for this build (%ld bytes, record %zu)\n", path, bytes, sizeof(PokerState));
        abort();
    }
    PokerStateSet* set = (PokerStateSet*)calloc(1, sizeof(PokerStateSet));
    set->count = bytes / sizeof(PokerState);
    set->states = (PokerState*)malloc(bytes);
    if (fread(set->states, sizeof(PokerState), set->count, f) != set->count || set->states[0].magic != POKER_STATE_MAGIC) {
        fprintf(stderr, "poker: %s failed to load or has the wrong layout\n", path);
        abort();
    }
    fclose(f);
    return set;
}
