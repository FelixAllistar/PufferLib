#pragma once
// PufferNet poker policy inference on the CPU: poker_linear's scalar path, poker_forward and
// poker_league_action's masked softmax sample from poker.h, without the trainer adapter. Used by panel.c and
// copied into the web game (poker_game engine/policy.h, checked bit-identical against web/Policy.bend).
// Checkpoint layout: encoder h*obs, decoder (actions+1)*h, then 3h*h per MinGRU layer. In memory every policy reads
// POKER_PUB inputs (poker_observe_public); checkpoints from before the position block read POKER_OBS, those from
// before the seat tenure block (gen11-gen12) POKER_PUB_V2, and
// poker_policy_read zero-pads their encoder rows so they ignore it.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "core.h"

// Zero-sum critic sizes (poker_critic.cu), here so every loader can read a critic checkpoint as a plain policy: its
// extra critic weights sit right after the decoder matrix and are skipped. Seat block: 39 card / chip features, then
// (2026-11-04) who sits there: learner, frozen model, bot, and the bot's panel type. Older critic checkpoints have 39.
#define POKER_SEAT_PRIV 48
#define POKER_SEAT_PRIV_V1 39
#define POKER_CRITIC_CH 256
#define POKER_CRITIC_XT (5 * 17 + 4 + 1)
static inline long poker_critic_params(int h, int seat) {
    return (long)POKER_CRITIC_CH * seat + (long)POKER_CRITIC_CH * (POKER_CRITIC_XT + h) + 2L * POKER_CRITIC_CH;
}

#define POKER_POLICY_MAX_HIDDEN 1024  // gen10 is 1024x2; the web build copies this file

typedef struct PokerPolicy {
    const float* weights;
    int hidden, layers;
} PokerPolicy;

// Floats of one policy in memory (POKER_PUB inputs).
static inline size_t poker_policy_size(int h, int layers) {
    return (size_t)h * (POKER_PUB + POKER_ACTIONS + 1) + (size_t)layers * 3 * h * h;
}

// Reads a checkpoint of POKER_PUB, POKER_PUB_V2 or POKER_OBS inputs, plain or zero-sum critic, into dst
// (poker_policy_size floats).
// Returns the file's input count, 0 when its size fits no layout of this hidden size and depth.
static int poker_policy_read(const char* path, float* dst, int h, int layers) {
    struct stat st;
    if (stat(path, &st) || st.st_size % sizeof(float)) return 0;
    long n = st.st_size / (long)sizeof(float), decoder = (long)h * (POKER_ACTIONS + 1), recurrent = (long)layers * 3 * h * h;
    const int inputs[3] = {POKER_PUB, POKER_PUB_V2, POKER_OBS};
    const long critics[3] = {0, poker_critic_params(h, POKER_SEAT_PRIV), poker_critic_params(h, POKER_SEAT_PRIV_V1)};
    int in = 0;
    long critic = 0;
    for (int i = 0; i < 3 && !in; i++)
        for (int c = 0; c < 3 && !in; c++)
            if (n == (long)h * inputs[i] + decoder + critics[c] + recurrent) { in = inputs[i]; critic = critics[c]; }
    if (!in) return 0;
    FILE* file = fopen(path, "rb");
    if (!file) return 0;
    size_t ok = 0;
    for (int o = 0; o < h; o++) {
        ok += fread(dst + (size_t)o * POKER_PUB, sizeof(float), in, file);
        memset(dst + (size_t)o * POKER_PUB + in, 0, (POKER_PUB - in) * sizeof(float));
    }
    ok += fread(dst + (size_t)h * POKER_PUB, sizeof(float), decoder, file);
    if (critic) fseek(file, critic * (long)sizeof(float), SEEK_CUR);
    ok += fread(dst + (size_t)h * POKER_PUB + decoder, sizeof(float), recurrent, file);
    fclose(file);
    return ok == (size_t)(h * in + decoder + recurrent) ? in : 0;
}

// Allocating loader for the tools: NULL (with a message) when the file is not an H/L poker policy.
static float* poker_policy_load(const char* path, int h, int layers) {
    float* w = (float*)malloc(poker_policy_size(h, layers) * sizeof(float));
    if (!poker_policy_read(path, w, h, layers)) {
        fprintf(stderr, "%s: not an H%d L%d poker policy (%d, %d or %d inputs)\n", path, h, layers, POKER_OBS,
            POKER_PUB_V2, POKER_PUB);
        free(w);
        return NULL;
    }
    return w;
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

// observations: POKER_PUB floats (poker_observe_public). state: layers*h floats, zeroed at the start of each hand.
static void poker_policy_forward(const PokerPolicy* net, float* state, const float* observations, float* logits) {
    int h = net->hidden;
    float x[POKER_POLICY_MAX_HIDDEN], projected[3 * POKER_POLICY_MAX_HIDDEN];
    const float* weights = net->weights;
    poker_policy_linear(x, observations, weights, POKER_PUB, h);
    const float* recurrent = weights + h * (POKER_PUB + POKER_ACTIONS + 1);
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
    poker_policy_linear(logits, x, weights + h * POKER_PUB, h, POKER_ACTIONS + 1);
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
