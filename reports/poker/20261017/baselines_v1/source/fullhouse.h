#pragma once
// Native port of advitrocks9/fullhouse-bot @ e504793d480b1b975f25258d25939b45c6dbd5a4.
// Copyright (c) 2026 Advit Arora. MIT; see third_party/fullhouse-LICENSE.txt.
// Feature quirks and the five-action abstraction intentionally match upstream.
#include <math.h>
#if defined(__AVX2__)
#include <immintrin.h>
#endif
#include POKER_FULLHOUSE_MODEL

static void fh_linear(float* y, const float* x, const float* w, const float* b,
    int in, int out) {
    for (int o = 0; o < out; o++) {
        float sum = 0;
        int i = 0;
#if defined(__AVX2__)
        __m256 v = _mm256_setzero_ps();
        for (; i + 8 <= in; i += 8)
            v = _mm256_add_ps(v, _mm256_mul_ps(_mm256_loadu_ps(x + i),
                _mm256_loadu_ps(w + o * in + i)));
        float lanes[8];
        _mm256_storeu_ps(lanes, v);
        for (int k = 0; k < 8; k++) sum += lanes[k];
#endif
        for (; i < in; i++) sum += x[i] * w[o * in + i];
        y[o] = sum + b[o];
    }
}

static void fh_norm(float* x, const float* gamma, const float* beta) {
    float mean = 0, var = 0;
    for (int i = 0; i < 256; i++) mean += x[i] / 256;
    for (int i = 0; i < 256; i++) var += (x[i] - mean) * (x[i] - mean) / 256;
    float inv = 1 / sqrtf(var + 1e-5f);
    for (int i = 0; i < 256; i++) {
        float v = gamma[i] * (x[i] - mean) * inv + beta[i];
        x[i] = v > 0 ? v : .01f * v;
    }
}

void poker_fullhouse_forward(const float* x, float* probs) {
    float h[256], z[256], v[128], a[128], value, adv[5];
    fh_linear(h, x, fh_trunk_w0, fh_trunk_b0, 51, 256);
    fh_norm(h, fh_trunk_ln0_g, fh_trunk_ln0_b);
    fh_linear(z, h, fh_trunk_w1, fh_trunk_b1, 256, 256);
    fh_norm(z, fh_trunk_ln1_g, fh_trunk_ln1_b);
    fh_linear(v, z, fh_val_w0, fh_val_b0, 256, 128);
    fh_linear(a, z, fh_adv_w0, fh_adv_b0, 256, 128);
    for (int i = 0; i < 128; i++) {
        if (v[i] < 0) v[i] *= .01f;
        if (a[i] < 0) a[i] *= .01f;
    }
    fh_linear(&value, v, fh_val_w1, fh_val_b1, 128, 1);
    fh_linear(adv, a, fh_adv_w1, fh_adv_b1, 128, 5);
    float mean = 0, count = 0, total = 0;
    for (int i = 0; i < 5; i++) { mean += adv[i] * x[46 + i]; count += x[46 + i]; }
    mean /= count;
    for (int i = 0; i < 5; i++) {
        probs[i] = fmaxf(value + adv[i] - mean, 0) * x[46 + i];
        total += probs[i];
    }
    for (int i = 0; i < 5; i++) probs[i] = total > 0 ? probs[i] / total : x[46 + i] / count;
}

static int fh_half(int x) { // Python round(x / 2): ties to even.
    int a = x / 2;
    return a + ((x & 1) && (a & 1));
}

// eval7 uses the same category/order, with zero-based rank nibbles.
uint32_t poker_fullhouse_rank(const uint8_t* cards, int n) {
    uint32_t r = poker_rank(cards, n), cat = r >> 24, out = cat << 24;
    const int digits[9] = {5, 4, 3, 3, 1, 5, 2, 2, 1};
    for (int i = 0; i < digits[cat]; i++) {
        int shift = 16 - 4 * i;
        out |= (((r >> shift) & 15) - 2) << shift;
    }
    return out;
}

void poker_fullhouse_features(Poker* g, int t, float* x, int32_t* amounts) {
    assert(POKER_PLAYERS <= 6);
    memset(x, 0, 51 * sizeof(float));
    int p = g->actor[t], street = g->street[t], nb = street ? street + 2 : 0;
    uint8_t cards[7] = {g->hole[p][0][t], g->hole[p][1][t]};
    int r0 = cards[0] >> 2, r1 = cards[1] >> 2;
    int hi = r0 > r1 ? r0 : r1, lo = r0 < r1 ? r0 : r1;
    int s0 = cards[0] & 3, s1 = cards[1] & 3;
    x[0] = r0 == r1 ? fh_paired[r0] : s0 == s1 ? fh_suited[13 * hi + lo] : fh_offsuit[13 * hi + lo];
    unsigned ranks = (1u << r0) | (1u << r1), board_ranks = 0;
    int suits[4] = {0}, board_suits[4] = {0}, over = 0, close = 0;
    suits[s0]++; suits[s1]++;
    for (int i = 0; i < nb; i++) {
        int c = cards[i + 2] = g->board[i][t], r = c >> 2;
        ranks |= 1u << r; board_ranks |= 1u << r;
        suits[c & 3]++; board_suits[c & 3]++; over += r > hi;
        for (int j = 0; j < i; j++) close += abs(r - (cards[j + 2] >> 2)) <= 2;
    }
    if (nb) x[1] = poker_fullhouse_rank(cards, nb + 2) / 135004160.0;
    if (nb && street < 3) {
        int max_suit = 0;
        for (int s = 1; s < 4; s++) if (suits[s] > suits[max_suit]) max_suit = s;
        int fd = suits[max_suit] == 4, hero_hi = -1, all_hi = -1;
        for (int i = 0; i < nb + 2; i++) if ((cards[i] & 3) == max_suit) {
            int r = cards[i] >> 2;
            if (r > all_hi) all_hi = r;
            if (i < 2 && r > hero_hi) hero_hi = r;
        }
        unsigned missing = 0;
        for (int i = 0; i < 9; i++) {
            unsigned window = 31u << i;
            if (__builtin_popcount(ranks & window) == 4) missing |= window & ~ranks;
        }
        if (__builtin_popcount(ranks & 0x100f) == 4) missing |= 0x100f & ~ranks;
        int outs = __builtin_popcount(missing);
        x[2] = fd; x[3] = outs >= 2; x[4] = outs == 1;
        x[5] = (9 * fd + 4 * outs - (fd ? (outs < 2 ? outs : 2) : 0)) / 20.0;
        x[6] = fd && hero_hi >= 0 && hero_hi == all_hi;
    }
    x[7] = hi / 12.0; x[8] = lo / 12.0; x[9] = s0 == s1; x[10] = r0 == r1;
    x[11] = hi == lo ? 0 : (hi - lo - 1) / 12.0;
    if (nb) {
        x[12] = __builtin_popcount(board_ranks) < nb;
        for (int s = 0; s < 4; s++) if (board_suits[s] == nb) x[13] = 1;
        x[14] = over / 5.0; x[15] = 2.0 * close / (nb * (nb - 1));
        for (int i = 0; i < 9; i++) if (__builtin_popcount((board_ranks >> i) & 31) >= 3) x[16] = 1;
        if (__builtin_popcount(board_ranks & 0x100f) >= 3) x[16] = 1;
    }
    x[17 + street] = 1;
    x[21 + (p + POKER_PLAYERS - g->button[t]) % POKER_PLAYERS] = 1;
    int pot = g->pot[t], stack = g->stack[p][t], call = g->high[t] - g->bet[p][t];
    int min_stack = stack, opp[POKER_PLAYERS] = {0}, nopp = 0;
    for (int q = 0; q < POKER_PLAYERS; q++) if (g->live[t] & (1u << q)) {
        if ((int)g->stack[q][t] < min_stack) min_stack = g->stack[q][t];
        if (q != p) {
            int v = g->stack[q][t], j = nopp++;
            while (j && opp[j - 1] < v) { opp[j] = opp[j - 1]; j--; }
            opp[j] = v;
            if (g->bet[q][t] / 10000.0 > x[43]) x[43] = g->bet[q][t] / 10000.0;
            if (g->total[q][t] / 10000.0 > x[44]) x[44] = g->total[q][t] / 10000.0;
        }
    }
    x[27] = pot / 10000.0; x[28] = stack / 10000.0;
    x[29] = fmin((double)min_stack / (pot > 0 ? pot : 1), 10) / 10;
    x[30] = call ? (double)call / (pot + call) : 0;
    for (int i = 0; i < nopp; i++) x[31 + i] = opp[i] / 10000.0;
    x[36] = g->raises[t] / 4.0; x[37] = g->last_raiser[t] != POKER_DONE;
    x[38] = g->last_raiser[t] == p; x[39] = g->bet[p][t] / 10000.0;
    x[40] = g->total[p][t] / 10000.0; x[41] = (double)g->high[t] / (pot > 0 ? pot : 1);
    x[42] = (double)call / (stack > 0 ? stack : 1);
    x[45] = __builtin_popcount(g->live[t]) / 6.0;
    x[46] = call > 0; x[47] = 1;
    amounts[0] = -1; amounts[1] = 0;
    int after = pot + call, half = fh_half(after), room = stack - call;
    amounts[2] = g->high[t] + half; amounts[3] = g->high[t] + after;
    amounts[4] = stack + g->bet[p][t];
    if (poker_can_raise(g, t)) {
        x[48] = half >= g->increment[t] && room > half;
        x[49] = after >= g->increment[t] && room > after;
        int eff = opp[0] > 0 && opp[0] < stack ? opp[0] : stack;
        x[50] = eff < 4 * (pot > 0 ? pot : 1);
    }
}

int32_t poker_fullhouse_bot(Poker* g, int t, uint64_t* rng) {
    int p = g->actor[t], call = g->high[t] - g->bet[p][t];
    int stack = g->stack[p][t], max_opp = 0;
    if (!g->street[t]) {
        for (int q = 0; q < POKER_PLAYERS; q++) if (q != p && (g->live[t] & (1u << q)))
            if ((int)g->stack[q][t] > max_opp) max_opp = g->stack[q][t];
        int eff = stack < max_opp ? stack : max_opp;
        if (eff <= 1200) {
            int a = g->hole[p][0][t], b = g->hole[p][1][t];
            int hi = (a >> 2) > (b >> 2) ? a >> 2 : b >> 2;
            int lo = (a >> 2) < (b >> 2) ? a >> 2 : b >> 2;
            int bits = fh_push_ranges[2 * (13 * hi + lo) + ((a & 3) == (b & 3))];
            int chosen = 0;
            if (call) {
                if (bits & 1) chosen = 1;
                else {
                    int cheap = (double)call / (g->pot[t] + call) < .35;
                    int mask = eff <= 600 ? (cheap ? 16 : 8) : eff <= 1000 ? (cheap ? 32 : 16) : 32;
                    return bits & mask ? 0 : -1;
                }
            } else {
                int pos = (p + POKER_PLAYERS - g->button[t]) % POKER_PLAYERS;
                int mask = POKER_PLAYERS == 2 ? (pos == 0 ? 4 : 32)
                    : pos == 0 || pos == 1 ? 4 : pos == 2 ? 32 : pos == 5 ? 2 : 1;
                chosen = !!(bits & mask);
            }
            return chosen && poker_can_raise(g, t) ? stack + g->bet[p][t] : 0;
        }
    }
    float x[51], probs[5]; int32_t amounts[5];
    poker_fullhouse_features(g, t, x, amounts);
    poker_fullhouse_forward(x, probs);
    double sample = poker_random(rng) / 4294967296.0, sum = 0;
    int last = 1;
    for (int i = 0; i < 5; i++) if (probs[i] > 0) {
        sum += probs[i]; last = i;
        if (sample < sum) return amounts[i];
    }
    return amounts[last];
}
