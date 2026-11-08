// CPU forward of the structured Gen 3 encoder and pointer decoder (pokemon3_encoder.cu), float weights in the CUDA
// parameter order. Used by tests/test_net.cu for parity and by showdown/policy_server.c (pk3_cpu_encode -> puffercpu
// MinGRU -> pk3_cpu_decode when the checkpoint is a pokemon3_net one).
#pragma once
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "pk3_net.h"

typedef struct { int in, mid, out, relu_out; const float *w1, *w2; } Pk3CpuMLP;  // w1: mid x AUG(in), w2: out x AUG(mid)
typedef struct { int E, M, H; Pk3CpuMLP move, mon, fusion; } Pk3CpuEnc;
typedef struct { int E, M, H; const float* lin; Pk3CpuMLP query, move, mon; } Pk3CpuDec;

static const float* pk3_cpu_mlp_bind(Pk3CpuMLP* m, int in, int mid, int out, int relu, const float* p) {
    m->in = in; m->mid = mid; m->out = out; m->relu_out = relu;
    m->w1 = p; p += mid * P3_AUG(in);
    m->w2 = p; p += out * P3_AUG(mid);
    return p;
}
// Bind the encoder's weights starting at p; returns the end.
static const float* pk3_cpu_enc_bind(Pk3CpuEnc* e, int E, int M, int H, const float* p) {
    e->E = E; e->M = M; e->H = H;
    p = pk3_cpu_mlp_bind(&e->move, P3_MOVE_IN, M, E, 1, p);
    p = pk3_cpu_mlp_bind(&e->mon, P3_MON_IN(E), M, E, 1, p);
    return pk3_cpu_mlp_bind(&e->fusion, P3_FUSION(E), H, H, 1, p);
}
static const float* pk3_cpu_dec_bind(Pk3CpuDec* d, int E, int M, int H, const float* p) {
    d->E = E; d->M = M; d->H = H;
    d->lin = p; p += (P3_ACTIONS + 1) * H;
    p = pk3_cpu_mlp_bind(&d->query, H, H, 2 * E, 0, p);
    p = pk3_cpu_mlp_bind(&d->move, P3_MOVE_IN, M, E, 0, p);
    return pk3_cpu_mlp_bind(&d->mon, P3_MON_IN(E), M, E, 0, p);
}
// x: m->in values; out: m->out values.
static void pk3_cpu_mlp(const Pk3CpuMLP* m, const float* x, float* out) {
    float mid[1024];
    assert(m->mid <= 1024);
    int ai = P3_AUG(m->in), am = P3_AUG(m->mid);
    for (int i = 0; i < m->mid; i++) {
        const float* w = m->w1 + (long)i * ai;
        float s = w[m->in];
        for (int k = 0; k < m->in; k++) s += w[k] * x[k];
        mid[i] = s > 0 ? s : 0;
    }
    for (int i = 0; i < m->out; i++) {
        const float* w = m->w2 + (long)i * am;
        float s = w[m->mid];
        for (int k = 0; k < m->mid; k++) s += w[k] * mid[k];
        out[i] = m->relu_out && s < 0 ? 0 : s;
    }
}
// Embeddings of Pokemon 0..mons-1 (moves: mons x 4 x E, out: mons x E).
static void pk3_cpu_mons(const Pk3CpuMLP* move, const Pk3CpuMLP* mon, const float* o, int mons, int E,
        float* moves, float* out) {
    float x[P3_MON_IN(256)];
    assert(E <= 256);
    for (int m = 0; m < mons; m++) {
        for (int j = 0; j < 4; j++) {
            for (int f = 0; f < P3_MOVE_IN; f++) x[f] = p3_move_input(o, m, j, f);
            pk3_cpu_mlp(move, x, moves + (m * 4 + j) * E);
        }
        int n = p3_move_count(o, m);
        for (int f = 0; f < P3_BASE; f++) x[f] = p3_mon_base(o, m, f);
        for (int e = 0; e < E; e++) {
            float s = 0;
            for (int j = 0; j < 4; j++) if (p3_move_known(o, m, j)) s += moves[(m * 4 + j) * E + e];
            x[P3_BASE + e] = n ? s / n : 0;
        }
        for (int f = 0; f < 3; f++) x[P3_BASE + E + f] = p3_mon_tail(o, m, f);
        pk3_cpu_mlp(mon, x, out + m * E);
    }
}
// One observation (P3_OBS floats) -> hidden (H floats).
static void pk3_cpu_encode(const Pk3CpuEnc* e, const float* o, float* hidden) {
    int E = e->E;
    float* moves = (float*)malloc(sizeof(float) * P3_MONS * 4 * E);
    float* x = (float*)malloc(sizeof(float) * P3_FUSION(E));
    pk3_cpu_mons(&e->move, &e->mon, o, P3_MONS, E, moves, x);
    memcpy(x + P3_MONS * E, o + P3_SIDE_AT, sizeof(float) * (P3_SIDES + P3_FIELD));
    pk3_cpu_mlp(&e->fusion, x, hidden);
    free(moves); free(x);
}
// hidden (H) + observation -> 128 logits then the value.
static void pk3_cpu_decode(const Pk3CpuDec* d, const float* o, const float* h, float* out) {
    int E = d->E, H = d->H;
    for (int a = 0; a <= P3_ACTIONS; a++) {
        float s = 0;
        for (int k = 0; k < H; k++) s += d->lin[(long)a * H + k] * h[k];
        out[a] = s;
    }
    if (!p3_battle(o)) return;
    float* q = (float*)malloc(sizeof(float) * 2 * E);
    float* moves = (float*)malloc(sizeof(float) * P3_OWN_MOVES * E);
    float* mons = (float*)malloc(sizeof(float) * 6 * E);
    pk3_cpu_mlp(&d->query, h, q);
    pk3_cpu_mons(&d->move, &d->mon, o, 6, E, moves, mons);
    float s = 1.0f / sqrtf((float)E);
    int m = p3_active(o);
    for (int a = 0; a < 10; a++) {
        if (a < 4 && m < 0) continue;
        const float* x = a < 4 ? moves + (m * 4 + a) * E : mons + (a - 4) * E;
        const float* qr = a < 4 ? q : q + E;
        float v = 0;
        for (int e = 0; e < E; e++) v += qr[e] * x[e];
        out[a] += v * s;
    }
    free(q); free(moves); free(mons);
}
