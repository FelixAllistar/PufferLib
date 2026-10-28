#pragma once
// PufferNet poker policy inference on the CPU: poker_linear's scalar path, poker_forward and
// poker_league_action's masked softmax sample from poker.h, without the trainer adapter. Used by panel.c and
// copied into the web game (poker_game engine/policy.h, checked bit-identical against web/Policy.bend).
// Checkpoint layout: encoder h*obs, decoder (actions+1)*h, then 3h*h per MinGRU layer.
#include <math.h>
#include "core.h"

#define POKER_POLICY_MAX_HIDDEN 256  // the web build copies this file; its networks stay 128x3

typedef struct PokerPolicy {
    const float* weights;
    int hidden, layers;
} PokerPolicy;

static inline size_t poker_policy_size(int h, int layers) {
    return (size_t)h * (POKER_OBS + POKER_ACTIONS + 1) + (size_t)layers * 3 * h * h;
}

static void poker_policy_linear(float* out, const float* x, const float* weights, int in, int n) {
    for (int o = 0; o < n; o++) {
        float sum = 0;
        for (int i = 0; i < in; i++) {
            sum += x[i] * weights[o * in + i];
        }
        out[o] = sum;
    }
}

// state: layers*h floats, zeroed at the start of each hand.
static void poker_policy_forward(const PokerPolicy* net, float* state, const float* observations, float* logits) {
    int h = net->hidden;
    float x[POKER_POLICY_MAX_HIDDEN], projected[3 * POKER_POLICY_MAX_HIDDEN];
    const float* weights = net->weights;
    poker_policy_linear(x, observations, weights, POKER_OBS, h);
    const float* recurrent = weights + h * (POKER_OBS + POKER_ACTIONS + 1);
    for (int l = 0; l < net->layers; l++) {
        poker_policy_linear(projected, x, recurrent + l * 3 * h * h, h, 3 * h);
        for (int i = 0; i < h; i++) {
            float value = projected[i] >= 0 ? projected[i] + 0.5f
                : 1.0f / (1.0f + expf(-projected[i]));
            float gate = 1.0f / (1.0f + expf(-projected[h + i]));
            float highway = 1.0f / (1.0f + expf(-projected[2 * h + i]));
            float s = state[l * h + i];
            s += gate * (value - s);
            state[l * h + i] = s;
            x[i] = highway * s + (1 - highway) * x[i];
        }
    }
    poker_policy_linear(logits, x, weights + h * POKER_OBS, h, POKER_ACTIONS + 1);
}

// Masked softmax sample over legal actions (bit a of mask) from one 32-bit draw; returns the action index.
static int poker_policy_sample(const float* logits_in, unsigned mask, uint32_t random) {
    float logits[POKER_ACTIONS];
    float maximum = -INFINITY, total = 0;
    for (int a = 0; a < POKER_ACTIONS; a++) {
        if ((mask & (1u << a)) && logits_in[a] > maximum) {
            maximum = logits_in[a];
        }
    }
    for (int a = 0; a < POKER_ACTIONS; a++) {
        logits[a] = mask & (1u << a) ? expf(logits_in[a] - maximum) : 0;
        total += logits[a];
    }
    float sample = (random >> 8) * 0x1p-24f * total;
    int selected = 31 - __builtin_clz(mask);
    for (int a = 0; a < POKER_ACTIONS; a++) {
        sample -= logits[a];
        if (sample < 0) {
            selected = a;
            break;
        }
    }
    return selected;
}
