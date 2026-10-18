#include "core.h"

unsigned poker_bot_styles(void) {
    return POKER_BOT_MASK;
}

void poker_aggression(Poker* g, int t, uint32_t* out) {
    out[0] = g->raises[t];
    out[1] = g->last_raiser[t];
}

size_t poker_bytes(void) {
    return sizeof(Poker);
}

void poker_seed(Poker* g, uint64_t seed) {
    memset(g, 0, sizeof(*g));
    for (int t = 0; t < POKER_BATCH; t++) {
        g->rng[t] = seed + t + 1;
        g->actor[t] = POKER_DONE;
    }
}

// Trusted testing/protocol transport only; never passed to a learned policy.
void poker_snapshot(Poker* g, int t, uint32_t* out) {
    uint32_t state[10] = {g->actor[t], g->street[t], g->pot[t], g->high[t],
        g->increment[t], g->live[t], g->active[t], g->pending[t],
        g->button[t], g->decisions[t]};
    memcpy(out, state, sizeof(state));
    for (int p = 0; p < POKER_PLAYERS; p++) {
        uint32_t row[7] = {g->stack[p][t], g->initial[p][t], g->total[p][t],
            g->bet[p][t], g->reopen[p][t], g->hole[p][0][t], g->hole[p][1][t]};
        memcpy(out + 10 + 7 * p, row, sizeof(row));
    }
    for (int i = 0; i < 5; i++) {
        out[10 + 7 * POKER_PLAYERS + i] = g->board[i][t];
    }
}
