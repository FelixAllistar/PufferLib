#pragma once
#include "river_cfr.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RCFR_MAX_N 1081
#define RCFR_MAX_NODES 4096
#define RCFR_MONEY_LIMIT INT64_C(1000000000)

typedef struct Node {
    RcfrNodeInfo info;
    double *regret, *sum;
} Node;

struct RcfrGame {
    int n, count, capacity, depth, dense;
    size_t bytes;
    uint64_t iterations;
    RcfrHand *hand;
    double *prior[2], *ones;
    double z, bb;
    Node *node;
    int *order, *lo, *hi, *card_order, *clo, *chi;
    int card_start[52], card_len[52], prefix_start[52];
    double *prefix, *card_prefix, *work, *root_values;
};

static int fail(char *error, size_t length, const char *message) {
    if (error && length) snprintf(error, length, "%s", message);
    return 0;
}

static void *allocate(RcfrGame *g, size_t count, size_t size) {
    if (size && count > SIZE_MAX / size) return NULL;
    void *p = calloc(count, size);
    if (p) g->bytes += count * size;
    return p;
}

static int disjoint(const RcfrHand *a, const RcfrHand *b) {
    return a->c0 != b->c0 && a->c0 != b->c1 && a->c1 != b->c0 && a->c1 != b->c1;
}

static int64_t minimum(int64_t a, int64_t b) { return a < b ? a : b; }
static int64_t maximum(int64_t a, int64_t b) { return a > b ? a : b; }

static int valid_public(const RcfrPublic *s) {
    if (!s || s->actor < 0 || s->actor > 1 || s->pending < 1 || s->pending > 3
        || !(s->pending & (1 << s->actor)) || s->pot <= 0 || s->pot > RCFR_MONEY_LIMIT
        || s->high < 0 || s->high > RCFR_MONEY_LIMIT || s->increment <= 0
        || s->increment > RCFR_MONEY_LIMIT || !isfinite(s->chips_per_bb) || s->chips_per_bb <= 0) return 0;
    for (int p = 0; p < 2; p++) {
        if (s->total[p] < 0 || s->total[p] > RCFR_MONEY_LIMIT || s->bet[p] < 0
            || s->bet[p] > s->total[p] || s->bet[p] > s->high || s->stack[p] < 0
            || s->stack[p] > RCFR_MONEY_LIMIT || s->reopen[p] < 0
            || s->reopen[p] > 4 * RCFR_MONEY_LIMIT
            || (s->stack[p] == 0 && (s->pending & (1 << p)))) return 0;
    }
    if (s->stack[s->actor] == 0 || s->total[0] + s->total[1] != s->pot
        || maximum(s->bet[0], s->bet[1]) != s->high) return 0;
    int call = s->high > s->bet[s->actor];
    if (call && s->pending != (1 << s->actor)) return 0;
    if (!call && s->stack[1 - s->actor] == 0) return 0;
    return 1;
}

static int64_t raise_to(const RcfrPublic *s) {
    int p = s->actor, q = 1 - p;
    int64_t limit = s->bet[p] + s->stack[p];
    if (s->high < s->reopen[p] || limit <= s->high
        || s->bet[q] + s->stack[q] <= s->high) return 0;
    int64_t call = minimum(s->high - s->bet[p], s->stack[p]);
    int64_t target = s->high + 2 * (s->pot + call) / 3;
    target = maximum(target, s->high + s->increment);
    return minimum(target, limit);
}

static int add_node(RcfrGame *g, const RcfrPublic *s, int kind, int folder, int depth) {
    if (g->count == RCFR_MAX_NODES || depth > RCFR_MAX_NODES) return -1;
    if (g->count == g->capacity) {
        int next = g->capacity ? 2 * g->capacity : 16;
        Node *p = realloc(g->node, (size_t)next * sizeof(*p));
        if (!p) return -1;
        memset(p + g->capacity, 0, (size_t)(next - g->capacity) * sizeof(*p));
        g->bytes += (size_t)(next - g->capacity) * sizeof(*p);
        g->node = p; g->capacity = next;
    }
    int index = g->count++;
    Node *v = &g->node[index];
    v->info.state = *s; v->info.kind = kind; v->info.folder = folder;
    for (int a = 0; a < 3; a++) v->info.child[a] = -1;
    if (depth > g->depth) g->depth = depth;
    if (kind) {
        v->info.state.actor = -1; v->info.state.pending = 0;
        if (kind == 2) {
            double u0 = folder == 0 ? -(double)s->total[0] : (double)s->total[1];
            for (int k = 0; k < 3; k++) v->info.payoff0[k] = u0;
        } else {
            double contested = (double)minimum(s->total[0], s->total[1]);
            v->info.payoff0[0] = -contested;
            v->info.payoff0[1] = 0;
            v->info.payoff0[2] = contested;
        }
        return index;
    }
    int p = s->actor, q = 1 - p;
    int64_t call = s->high - s->bet[p], raise = raise_to(s);
    int ac = 0;
    if (call > 0) v->info.action[ac++] = -1;
    v->info.action[ac++] = 0;
    if (raise) v->info.action[ac++] = raise;
    v->info.action_count = ac;
    v->regret = allocate(g, (size_t)ac * g->n, sizeof(double));
    v->sum = allocate(g, (size_t)ac * g->n, sizeof(double));
    if (!v->regret || !v->sum) return -1;
    for (int a = 0; a < ac; a++) {
        int64_t action = g->node[index].info.action[a];
        RcfrPublic next = *s;
        int terminal = 0, folded = -1;
        next.pending &= ~(1 << p);
        if (action < 0) {
            terminal = 2; folded = p;
        } else {
            int64_t target = action ? action : s->high;
            int64_t amount = minimum(target - s->bet[p], s->stack[p]);
            next.stack[p] -= amount; next.bet[p] += amount;
            next.total[p] += amount; next.pot += amount;
            if (target > s->high) {
                next.increment = maximum(s->increment, target - s->high);
                next.high = target;
                next.pending = next.stack[q] ? (1 << q) : 0;
            }
            next.reopen[p] = next.high + next.increment;
            if (action == 0 && call > 0) terminal = 1;
            else {
                if (!next.stack[0]) next.pending &= ~1;
                if (!next.stack[1]) next.pending &= ~2;
                if (!next.pending) terminal = 1;
                else {
                    next.actor = q;
                    if (!next.stack[p] && next.bet[q] >= next.bet[p]) terminal = 1;
                }
            }
        }
        int child = add_node(g, &next, terminal, folded, depth + 1);
        if (child < 0) return -1;
        g->node[index].info.child[a] = child;
    }
    return index;
}

static int lower_bound(const RcfrGame *g, const int *indices, int n, uint32_t rank, int inclusive) {
    int lo = 0, hi = n;
    while (lo < hi) {
        int m = lo + (hi - lo) / 2;
        uint32_t r = g->hand[indices[m]].rank;
        if (r < rank || (inclusive && r == rank)) lo = m + 1;
        else hi = m;
    }
    return lo;
}

static int initialize_cdf(RcfrGame *g) {
    int n = g->n;
    g->order = allocate(g, n, sizeof(int));
    g->lo = allocate(g, n, sizeof(int)); g->hi = allocate(g, n, sizeof(int));
    g->card_order = allocate(g, 2u * n, sizeof(int));
    g->clo = allocate(g, 2u * n, sizeof(int)); g->chi = allocate(g, 2u * n, sizeof(int));
    g->prefix = allocate(g, n + 1u, sizeof(double));
    g->card_prefix = allocate(g, 2u * n + 52u, sizeof(double));
    if (!g->order || !g->lo || !g->hi || !g->card_order || !g->clo || !g->chi || !g->prefix || !g->card_prefix) return 0;
    /* Deterministic insertion sort is one-time N<=1081 setup, not terminal work. */
    for (int h = 0; h < n; h++) {
        int p = h;
        while (p && g->hand[g->order[p - 1]].rank > g->hand[h].rank) {
            g->order[p] = g->order[p - 1]; p--;
        }
        g->order[p] = h;
    }
    int at = 0, prefix_at = 0;
    for (int c = 0; c < 52; c++) {
        g->card_start[c] = at; g->prefix_start[c] = prefix_at;
        for (int k = 0; k < n; k++) {
            int h = g->order[k];
            if (g->hand[h].c0 == c || g->hand[h].c1 == c) g->card_order[at++] = h;
        }
        g->card_len[c] = at - g->card_start[c];
        prefix_at += g->card_len[c] + 1;
    }
    if (at != 2 * n || prefix_at != 2 * n + 52) return 0;
    for (int h = 0; h < n; h++) {
        uint32_t r = g->hand[h].rank;
        g->lo[h] = lower_bound(g, g->order, n, r, 0);
        g->hi[h] = lower_bound(g, g->order, n, r, 1);
        int card[2] = {g->hand[h].c0, g->hand[h].c1};
        for (int k = 0; k < 2; k++) {
            int c = card[k], base = g->prefix_start[c];
            const int *indices = g->card_order + g->card_start[c];
            g->clo[2 * h + k] = base + lower_bound(g, indices, g->card_len[c], r, 0);
            g->chi[2 * h + k] = base + lower_bound(g, indices, g->card_len[c], r, 1);
        }
    }
    return 1;
}

static void dense_terminal(const RcfrGame *g, const RcfrNodeInfo *t, int player, const double *w, double *out) {
    for (int h = 0; h < g->n; h++) {
        double value = 0;
        for (int o = 0; o < g->n; o++) if (disjoint(&g->hand[h], &g->hand[o])) {
            int cmp = (g->hand[h].rank > g->hand[o].rank) - (g->hand[h].rank < g->hand[o].rank);
            double payoff = player == 0 ? t->payoff0[cmp + 1] : -t->payoff0[1 - cmp];
            value += w[o] * payoff;
        }
        out[h] = value;
    }
}

static void cdf_terminal(RcfrGame *g, const RcfrNodeInfo *t, int player, const double *w, double *out) {
    int n = g->n;
    g->prefix[0] = 0;
    for (int k = 0; k < n; k++) g->prefix[k + 1] = g->prefix[k] + w[g->order[k]];
    for (int c = 0; c < 52; c++) {
        int base = g->prefix_start[c], start = g->card_start[c];
        g->card_prefix[base] = 0;
        for (int k = 0; k < g->card_len[c]; k++)
            g->card_prefix[base + k + 1] = g->card_prefix[base + k] + w[g->card_order[start + k]];
    }
    double payoff[3];
    for (int k = 0; k < 3; k++) payoff[k] = player == 0 ? t->payoff0[k] : -t->payoff0[2 - k];
    for (int h = 0; h < n; h++) {
        int c = g->hand[h].c0, d = g->hand[h].c1;
        double mass = g->prefix[n] - g->card_prefix[g->prefix_start[c] + g->card_len[c]]
            - g->card_prefix[g->prefix_start[d] + g->card_len[d]] + w[h];
        double less = g->prefix[g->lo[h]] - g->card_prefix[g->clo[2 * h]] - g->card_prefix[g->clo[2 * h + 1]];
        double le = g->prefix[g->hi[h]] - g->card_prefix[g->chi[2 * h]] - g->card_prefix[g->chi[2 * h + 1]] + w[h];
        out[h] = (mass - le) * payoff[0] + (le - less) * payoff[1] + less * payoff[2];
    }
}

static void strategy(const RcfrGame *g, const Node *node, int average, double *out) {
    int n = g->n, ac = node->info.action_count;
    const double *s = average ? node->sum : node->regret;
    for (int h = 0; h < n; h++) {
        double total = 0;
        for (int a = 0; a < ac; a++) total += s[a * n + h];
        for (int a = 0; a < ac; a++) out[a * n + h] = total > 0 ? s[a * n + h] / total : 1. / ac;
    }
}

static void terminal(RcfrGame *g, const RcfrNodeInfo *t, int player, const double *reach, double *out) {
    if (g->dense) dense_terminal(g, t, player, reach, out);
    else cdf_terminal(g, t, player, reach, out);
}

static void update(RcfrGame *g, int index, int player, const double *own, const double *other,
    double *out, int depth, double weight) {
    Node *node = &g->node[index];
    if (node->info.kind) { terminal(g, &node->info, player, other, out); return; }
    int n = g->n, ac = node->info.action_count;
    double *q = g->work + (size_t)depth * 7 * n;
    double *sigma = q + 3 * n, *reach = sigma + 3 * n;
    strategy(g, node, 0, sigma);
    memset(out, 0, (size_t)n * sizeof(double));
    if (node->info.state.actor == player) {
        for (int a = 0; a < ac; a++) {
            for (int h = 0; h < n; h++) {
                reach[h] = own[h] * sigma[a * n + h];
                node->sum[a * n + h] += weight * reach[h];
            }
            update(g, node->info.child[a], player, reach, other, q + a * n, depth + 1, weight);
            for (int h = 0; h < n; h++) out[h] += sigma[a * n + h] * q[a * n + h];
        }
        for (int a = 0; a < ac; a++) for (int h = 0; h < n; h++) {
            double regret = node->regret[a * n + h] + q[a * n + h] - out[h];
            node->regret[a * n + h] = regret > 0 ? regret : 0;
        }
    } else {
        for (int a = 0; a < ac; a++) {
            for (int h = 0; h < n; h++) reach[h] = other[h] * sigma[a * n + h];
            update(g, node->info.child[a], player, own, reach, q + a * n, depth + 1, weight);
            for (int h = 0; h < n; h++) out[h] += q[a * n + h];
        }
    }
}

static void evaluate(RcfrGame *g, int index, int player, int best_response,
    const double *other, double *out, int depth) {
    const Node *node = &g->node[index];
    if (node->info.kind) { terminal(g, &node->info, player, other, out); return; }
    int n = g->n, ac = node->info.action_count;
    double *q = g->work + (size_t)depth * 7 * n;
    double *sigma = q + 3 * n, *reach = sigma + 3 * n;
    strategy(g, node, 1, sigma);
    memset(out, 0, (size_t)n * sizeof(double));
    if (node->info.state.actor == player) {
        for (int a = 0; a < ac; a++) {
            evaluate(g, node->info.child[a], player, best_response, other, q + a * n, depth + 1);
            for (int h = 0; h < n; h++) {
                if (best_response) { if (!a || q[a * n + h] > out[h]) out[h] = q[a * n + h]; }
                else out[h] += sigma[a * n + h] * q[a * n + h];
            }
        }
    } else {
        for (int a = 0; a < ac; a++) {
            for (int h = 0; h < n; h++) reach[h] = other[h] * sigma[a * n + h];
            evaluate(g, node->info.child[a], player, best_response, reach, q + a * n, depth + 1);
            for (int h = 0; h < n; h++) out[h] += q[a * n + h];
        }
    }
}

void rcfr_free(RcfrGame *g) {
    if (!g) return;
    for (int i = 0; i < g->count; i++) { free(g->node[i].regret); free(g->node[i].sum); }
    free(g->node); free(g->hand); free(g->prior[0]); free(g->prior[1]); free(g->ones);
    free(g->order); free(g->lo); free(g->hi); free(g->card_order); free(g->clo); free(g->chi);
    free(g->prefix); free(g->card_prefix); free(g->work); free(g->root_values); free(g);
}

RcfrGame *rcfr_create(const RcfrHand *hands, int n, const double *prior0,
    const double *prior1, const RcfrPublic *root, char *error, size_t error_size) {
    if (!hands || !prior0 || !prior1 || n < 2 || n > RCFR_MAX_N || !valid_public(root)) {
        fail(error, error_size, "Invalid hand count, public state, or required input"); return NULL;
    }
    for (int h = 0; h < n; h++) {
        if (hands[h].c0 >= hands[h].c1 || hands[h].c1 >= 52
            || !isfinite(prior0[h]) || !isfinite(prior1[h]) || prior0[h] < 0 || prior1[h] < 0) {
            fail(error, error_size, "Invalid holding or prior factor"); return NULL;
        }
        for (int j = 0; j < h; j++) if (hands[h].c0 == hands[j].c0 && hands[h].c1 == hands[j].c1) {
            fail(error, error_size, "Duplicate holding"); return NULL;
        }
    }
    RcfrGame *g = calloc(1, sizeof(*g));
    if (!g) { fail(error, error_size, "Allocation failed"); return NULL; }
    g->bytes = sizeof(*g); g->n = n; g->bb = root->chips_per_bb;
    g->hand = allocate(g, n, sizeof(*g->hand));
    g->prior[0] = allocate(g, n, sizeof(double)); g->prior[1] = allocate(g, n, sizeof(double));
    g->ones = allocate(g, n, sizeof(double)); g->root_values = allocate(g, n, sizeof(double));
    if (!g->hand || !g->prior[0] || !g->prior[1] || !g->ones || !g->root_values) goto error;
    memcpy(g->hand, hands, (size_t)n * sizeof(*hands));
    memcpy(g->prior[0], prior0, (size_t)n * sizeof(double));
    memcpy(g->prior[1], prior1, (size_t)n * sizeof(double));
    for (int h = 0; h < n; h++) g->ones[h] = 1;
    if (!initialize_cdf(g)) goto error;
    /* O(N) compatible mass; no payoff matrix or private-pair traversal. */
    double total = 0, cards[52] = {0};
    for (int h = 0; h < n; h++) {
        total += prior1[h]; cards[hands[h].c0] += prior1[h]; cards[hands[h].c1] += prior1[h];
    }
    for (int h = 0; h < n; h++)
        g->z += prior0[h] * (total - cards[hands[h].c0] - cards[hands[h].c1] + prior1[h]);
    if (!isfinite(g->z) || g->z <= 0) {
        fail(error, error_size, "Zero or nonfinite compatible prior mass"); rcfr_free(g); return NULL;
    }
    if (add_node(g, root, 0, -1, 0) != 0) goto error;
    g->work = allocate(g, (size_t)(g->depth + 1) * 7 * n, sizeof(double));
    if (!g->work) goto error;
    if (error && error_size) error[0] = 0;
    return g;
error:
    fail(error, error_size, "Allocation or complete-tree resource limit failure"); rcfr_free(g); return NULL;
}

int rcfr_iterate(RcfrGame *g, uint64_t iterations) {
    if (!g || iterations > UINT64_C(1000000000) || g->iterations > UINT64_C(1000000000) - iterations) return 0;
    for (uint64_t k = 0; k < iterations; k++) {
        double t = (double)(g->iterations + 1);
        for (int player = 0; player < 2; player++)
            update(g, 0, player, g->ones, g->prior[1 - player], g->root_values, 0, t);
        g->iterations++;
    }
    return 1;
}

int rcfr_certificate(RcfrGame *g, RcfrCertificate *out) {
    if (!g || !out) return 0;
    double values[3] = {0};
    for (int k = 0; k < 3; k++) {
        int player = k == 2;
        evaluate(g, 0, player, k != 0, g->prior[1 - player], g->root_values, 0);
        for (int h = 0; h < g->n; h++) values[k] += g->prior[player][h] * g->root_values[h];
        values[k] /= g->z;
        if (!isfinite(values[k])) return 0;
    }
    double upper = values[1], lower = -values[2], gap = upper - lower;
    RcfrCertificate result = {.value_chips=values[0], .upper_chips=upper, .lower_chips=lower,
        .nashconv_chips=gap, .gap_over_two_chips=gap / 2,
        .value_bb=values[0]/g->bb, .upper_bb=upper/g->bb, .lower_bb=lower/g->bb,
        .nashconv_bb=gap/g->bb, .gap_over_two_bb=(gap/g->bb)/2,
        .chance_normalizer=g->z, .iterations=g->iterations};
    /* A finite chip certificate can overflow an extreme user-supplied BB unit.
     * Failure must leave the caller's output untouched, never claim validity. */
    if (!isfinite(result.nashconv_chips) || !isfinite(result.gap_over_two_chips)
        || !isfinite(result.value_bb) || !isfinite(result.upper_bb) || !isfinite(result.lower_bb)
        || !isfinite(result.nashconv_bb) || !isfinite(result.gap_over_two_bb)) return 0;
    *out = result;
    return 1;
}

int rcfr_hand_count(const RcfrGame *g) { return g ? g->n : 0; }
int rcfr_node_count(const RcfrGame *g) { return g ? g->count : 0; }
int rcfr_max_depth(const RcfrGame *g) { return g ? g->depth : 0; }
size_t rcfr_allocated_bytes(const RcfrGame *g) { return g ? g->bytes : 0; }
int rcfr_node_info(const RcfrGame *g, int node, RcfrNodeInfo *out) {
    if (!g || !out || node < 0 || node >= g->count) return 0;
    *out = g->node[node].info; return 1;
}
int rcfr_average(const RcfrGame *g, int node, double *out) {
    if (!g || !out || node < 0 || node >= g->count || g->node[node].info.kind) return 0;
    strategy(g, &g->node[node], 1, out); return 1;
}
int rcfr_regrets(const RcfrGame *g, int node, double *out) {
    if (!g || !out || node < 0 || node >= g->count || g->node[node].info.kind) return 0;
    memcpy(out, g->node[node].regret, (size_t)g->n * g->node[node].info.action_count * sizeof(double)); return 1;
}
int rcfr_test_dense_backend(RcfrGame *g, int enabled) {
    if (!g || (enabled != 0 && enabled != 1)) return 0;
    g->dense = enabled; return 1;
}
int rcfr_terminal_values(RcfrGame *g, int node, int player, const double *reach, double *out, int dense) {
    if (!g || !reach || !out || node < 0 || node >= g->count || !g->node[node].info.kind
        || player < 0 || player > 1 || (dense != 0 && dense != 1)) return 0;
    for (int h = 0; h < g->n; h++) if (!isfinite(reach[h]) || reach[h] < 0) return 0;
    if (dense) dense_terminal(g, &g->node[node].info, player, reach, out);
    else cdf_terminal(g, &g->node[node].info, player, reach, out);
    return 1;
}

