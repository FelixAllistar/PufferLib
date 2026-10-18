#pragma once
#include "river_cfr_core.h"
#include "river_cfr_rank.h"

// Optional HU experiment. The whole public game is solved before an actual
// holding is read. Its certificate concerns this supplied-prior, single-size
// game; rebuilding heuristic priors at later decisions is not safe resolving.
#define POKER_CFR_HANDS 1081
static const double poker_cfr_range_alpha = 0.42089278763026228;

typedef struct PokerCfrInputs {
    RcfrPublic state;
    RcfrHand hand[POKER_CFR_HANDS];
    double prior[2][POKER_CFR_HANDS];
} PokerCfrInputs;

enum PokerCfrStatus {
    POKER_CFR_UNSUPPORTED = 0, POKER_CFR_ACCEPTED = 1,
    POKER_CFR_CREATE_FAILED = 2, POKER_CFR_ITERATION_FAILED = 3,
    POKER_CFR_CERTIFICATE_FAILED = 4, POKER_CFR_ROW_FAILED = 5,
    POKER_CFR_NOT_CONVERGED = 6
};

typedef struct PokerCfrResult {
    RcfrCertificate certificate;
    double average[3 * POKER_CFR_HANDS];
    int64_t action[3];
    size_t allocated_bytes;
    int status, checkpoint, action_count, nodes;
} PokerCfrResult;

// Only public fields enter this copy. Actual hole cards, deal RNG, profiles,
// observation history and unused tables never enter either prior factor.
static int poker_cfr_make_inputs(const Poker* g, int t,
    const PokerProfile* profiles, PokerCfrInputs* out) {
    if (!g || !out || profiles || t < 0 || t >= POKER_BATCH
        || g->street[t] != 3 || g->actor[t] > 1 || g->button[t] > 1
        || g->live[t] != 3 || g->active[t] != 3) return 0;
    for (int p = 0; p < 2; p++) {
        if (g->initial[p][t] != 20000 || !g->stack[p][t]
            || g->total[p][t] < g->bet[p][t]
            || (uint64_t)g->total[p][t] + g->stack[p][t] != 20000) return 0;
    }
    if (g->total[0][t] - g->bet[0][t]
        != g->total[1][t] - g->bet[1][t]) return 0;
    uint64_t board_mask = 0;
    for (int b = 0; b < 5; b++) {
        unsigned card = g->board[b][t];
        if (card >= 52 || (board_mask & (UINT64_C(1) << card))) return 0;
        board_mask |= UINT64_C(1) << card;
    }
    memset(out, 0, sizeof(*out));
    RcfrPublic* s = &out->state;
    s->pot = g->pot[t]; s->high = g->high[t];
    s->increment = g->increment[t]; s->pending = g->pending[t];
    s->actor = g->actor[t]; s->chips_per_bb = POKER_BB;
    for (int p = 0; p < 2; p++) {
        s->total[p] = g->total[p][t]; s->bet[p] = g->bet[p][t];
        s->stack[p] = g->stack[p][t]; s->reopen[p] = g->reopen[p][t];
    }
    if (!valid_public(s)) return 0;
    Poker public_view = {0};
    public_view.street[0] = 3; public_view.button[0] = g->button[t];
    uint8_t cards[7];
    for (int b = 0; b < 5; b++)
        public_view.board[b][0] = cards[b + 2] = g->board[b][t];
    for (int p = 0; p < 2; p++) public_view.line[p][0] = g->line[p][t];
    int n = 0;
    for (int c0 = 0; c0 < 52; c0++) {
        if (board_mask & (UINT64_C(1) << c0)) continue;
        for (int c1 = c0 + 1; c1 < 52; c1++) {
            if (board_mask & (UINT64_C(1) << c1)) continue;
            cards[0] = c0; cards[1] = c1;
            out->hand[n].c0 = c0; out->hand[n].c1 = c1;
            out->hand[n].rank = poker_rank(cards, 7);
            for (int p = 0; p < 2; p++) {
                double weight = poker_range_weight(&public_view, 0, p, c0, c1, NULL);
                double factor = pow(weight, poker_cfr_range_alpha);
                if (!isfinite(factor) || factor <= 0) return 0;
                out->prior[p][n] = factor;
            }
            n++;
        }
    }
    return n == POKER_CFR_HANDS;
}

static int poker_cfr_certificate_usable(const RcfrCertificate* c) {
    const double values[] = {c->value_chips, c->upper_chips, c->lower_chips,
        c->nashconv_chips, c->gap_over_two_chips, c->value_bb, c->upper_bb,
        c->lower_bb, c->nashconv_bb, c->gap_over_two_bb, c->chance_normalizer};
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); i++)
        if (!isfinite(values[i])) return 0;
    const double tolerance = 1e-8;
    return c->chance_normalizer > 0 && c->nashconv_chips >= -tolerance
        && c->value_chips >= c->lower_chips - tolerance
        && c->value_chips <= c->upper_chips + tolerance;
}

// Public-only solve: neither a Poker pointer, an actual holding, nor RNG is an
// input. All root rows are materialized and checked before private row lookup.
static int poker_cfr_solve_public(const PokerCfrInputs* in, PokerCfrResult* out) {
    if (!in || !out) return 0;
    memset(out, 0, sizeof(*out));
    RcfrGame* game = rcfr_create(in->hand, POKER_CFR_HANDS,
        in->prior[0], in->prior[1], &in->state, NULL, 0);
    if (!game) { out->status = POKER_CFR_CREATE_FAILED; return 0; }
    out->allocated_bytes = rcfr_allocated_bytes(game);
    out->nodes = rcfr_node_count(game);
    const int checkpoints[] = {100, 200, 400, 800, 1000};
    int previous = 0, accepted = 0;
    for (unsigned i = 0; i < sizeof(checkpoints) / sizeof(checkpoints[0]); i++) {
        out->checkpoint = checkpoints[i];
        if (!rcfr_iterate(game, checkpoints[i] - previous)) {
            out->status = POKER_CFR_ITERATION_FAILED; break;
        }
        previous = checkpoints[i];
        if (!rcfr_certificate(game, &out->certificate)
            || out->certificate.iterations != (uint64_t)checkpoints[i]
            || !poker_cfr_certificate_usable(&out->certificate)) {
            out->status = POKER_CFR_CERTIFICATE_FAILED; break;
        }
        if (out->certificate.nashconv_bb <= 0.05) { accepted = 1; break; }
        out->status = POKER_CFR_NOT_CONVERGED;
    }
    if (accepted) {
        RcfrNodeInfo root;
        if (!rcfr_node_info(game, 0, &root) || root.kind
            || root.action_count < 1 || root.action_count > 3
            || !rcfr_average(game, 0, out->average)) accepted = 0;
        else {
            out->action_count = root.action_count;
            for (int a = 0; a < root.action_count; a++) {
                if (root.action[a] < -1 || root.action[a] > INT32_MAX) accepted = 0;
                out->action[a] = root.action[a];
            }
            for (int h = 0; h < POKER_CFR_HANDS; h++) {
                double sum = 0;
                for (int a = 0; a < root.action_count; a++) {
                    double probability = out->average[a * POKER_CFR_HANDS + h];
                    if (!isfinite(probability) || probability < 0 || probability > 1)
                        accepted = 0;
                    sum += probability;
                }
                if (!isfinite(sum) || fabs(sum - 1) > 1e-12) accepted = 0;
            }
        }
        out->status = accepted ? POKER_CFR_ACCEPTED : POKER_CFR_ROW_FAILED;
    }
    rcfr_free(game);
    return accepted;
}

// A completed public solve is a prerequisite for the sole private operation.
// Every failure returns before consuming even one local simulation RNG draw.
static int poker_cfr_select_row(const PokerCfrInputs* in, const PokerCfrResult* out,
    uint8_t c0, uint8_t c1, uint64_t* simulations, int32_t* action) {
    if (!in || !out || !simulations || !action || out->status != POKER_CFR_ACCEPTED
        || c0 == c1 || c0 >= 52 || c1 >= 52) return 0;
    if (c0 > c1) { uint8_t tmp = c0; c0 = c1; c1 = tmp; }
    int h = 0;
    while (h < POKER_CFR_HANDS && (in->hand[h].c0 != c0 || in->hand[h].c1 != c1)) h++;
    if (h == POKER_CFR_HANDS) return 0;
    double draw = poker_random(simulations) / 4294967296.0, cumulative = 0;
    for (int a = 0; a < out->action_count; a++) {
        cumulative += out->average[a * POKER_CFR_HANDS + h];
        if (draw < cumulative || a + 1 == out->action_count) {
            *action = (int32_t)out->action[a]; return 1;
        }
    }
    return 0;
}

static int poker_cfr_action(const Poker* g, int t, uint64_t* simulations,
    const PokerProfile* profiles, int32_t* action) {
    PokerCfrInputs in;
    PokerCfrResult out;
    if (!poker_cfr_make_inputs(g, t, profiles, &in)
        || !poker_cfr_solve_public(&in, &out)) return 0;
    int actor = in.state.actor;
    return poker_cfr_select_row(&in, &out, g->hole[actor][0][t],
        g->hole[actor][1][t], simulations, action);
}
