// Lossless tokenization of the compact, two-player egocentric observation.
// Rank tokens contain public card availability; the final token contains all
// remaining compact fields. No simulator state or hidden information is read.
#pragma once
#include "goofspiel_obs.h"

constexpr int GS_TRANSFORMER_TOKENS = GS_NUM_CARDS + 1;
constexpr int GS_TRANSFORMER_FEATURES = 6 + GS_COMPACT_OBS_SIZE - GS_O_SCORES;

template<class T> __host__ __device__ float gs_transformer_feature(
        const T* observation, int token, int feature) {
    if (token < GS_NUM_CARDS) {
        switch (feature) {
            case 0: return float(token + 1) / GS_NUM_CARDS;
            case 2: return float(observation[GS_O_HANDS + token]);
            case 3: return float(observation[GS_O_HANDS + GS_NUM_CARDS + token]);
            case 4: return float(observation[GS_O_REMAINING + token]);
            case 5: return float(observation[GS_O_CURRENT_PRIZE]) == token + 1;
            default: return 0.f;
        }
    }
    if (feature == 1) return 1.f;
    if (feature < 6) return 0.f;
    int offset = GS_O_SCORES + feature - 6;
    float value = float(observation[offset]);
    if (offset < GS_O_CURRENT_PRIZE || offset == GS_O_POT || offset == GS_O_POT + 1)
        return value / 15.f;
    if (offset == GS_O_CURRENT_PRIZE || offset == GS_O_ROUND)
        return value / GS_NUM_CARDS;
    return value;
}

template<class T> __global__ void gs_transformer_tokenize(
        const T* observations, T* features, int batch) {
    int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= batch * GS_TRANSFORMER_TOKENS * GS_TRANSFORMER_FEATURES) return;
    int feature = index % GS_TRANSFORMER_FEATURES;
    int row = index / GS_TRANSFORMER_FEATURES;
    features[index] = T(gs_transformer_feature(observations +
        (row / GS_TRANSFORMER_TOKENS) * OBS_SIZE, row % GS_TRANSFORMER_TOKENS, feature));
}
