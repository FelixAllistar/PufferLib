#pragma once
// Players for the poker evaluation tools (lineup.c, fingerprint.c; 2026-11-05): bots by name or models
// NAME=PATH:HIDDEN:LAYERS (any layout policy.h reads), and a policy.h forward with an AVX2 linear (logits match
// policy.h only to rounding).
#include <stdlib.h>
#include "policy.h"
#ifdef __AVX2__
#include <immintrin.h>
#endif

#define MAX_PLAYERS 32
typedef struct { char name[64]; int bot; float* w; int h, l; } Player;
static Player players[MAX_PLAYERS];
static int nplayers;


static void linear(float* out, const float* x, const float* w, int in, int n) {
    for (int o = 0; o < n; o++) {
        const float* row = w + (size_t)o * in;
        int i = 0;
        float sum = 0;
#ifdef __AVX2__
        __m256 acc = _mm256_setzero_ps();
        for (; i + 8 <= in; i += 8) acc = _mm256_fmadd_ps(_mm256_loadu_ps(x + i), _mm256_loadu_ps(row + i), acc);
        float lanes[8];
        _mm256_storeu_ps(lanes, acc);
        for (int k = 0; k < 8; k++) sum += lanes[k];
#endif
        for (; i < in; i++) sum += x[i] * row[i];
        out[o] = sum;
    }
}

// policy.h's poker_policy_forward with the fast linear.
static void forward(const Player* m, float* state, const float* obs, float* logits) {
    int h = m->h;
    float x[POKER_POLICY_MAX_HIDDEN], projected[3 * POKER_POLICY_MAX_HIDDEN];
    linear(x, obs, m->w, POKER_PUB, h);
    const float* recurrent = m->w + (size_t)h * (POKER_PUB + POKER_ACTIONS + 1);
    for (int l = 0; l < m->l; l++) {
        linear(projected, x, recurrent + (size_t)l * 3 * h * h, h, 3 * h);
        for (int i = 0; i < h; i++) {
            float value = projected[i] >= 0 ? projected[i] + 0.5f : 1.0f / (1.0f + expf(-projected[i]));
            float gate = 1.0f / (1.0f + expf(-projected[h + i]));
            float highway = 1.0f / (1.0f + expf(-projected[2 * h + i]));
            float s = state[l * h + i];
            s += gate * (value - s);
            state[l * h + i] = s;
            x[i] = highway * s + (1 - highway) * x[i];
        }
    }
    linear(logits, x, m->w + (size_t)h * POKER_PUB, h, POKER_ACTIONS + 1);
}

static uint64_t mix(uint64_t x) {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

static int add_player(const char* spec) {
    static const char* bots[] = {"rules", "fullhouse", "range", "tight", "trapper"};  // maniac, station deleted
    static const int ids[] = {1, 4, 7, 10, 11};
    Player* p = &players[nplayers];
    for (int i = 0; i < 5; i++) if (!strcmp(spec, bots[i])) {
        if (!poker_bot_supported(ids[i])) { fprintf(stderr, "%s: not in this build\n", spec); return 0; }
        snprintf(p->name, sizeof(p->name), "%s", spec); p->bot = ids[i]; nplayers++; return 1;
    }
    char path[1024];
    const char* eq = strchr(spec, '=');
    if (!eq || sscanf(eq + 1, "%1023[^:]:%d:%d", path, &p->h, &p->l) != 3) { fprintf(stderr, "bad player %s\n", spec); return 0; }
    snprintf(p->name, sizeof(p->name), "%.*s", (int)(eq - spec), spec);
    p->w = poker_policy_load(path, p->h, p->l);
    if (!p->w) return 0;
    nplayers++;
    return 1;
}

