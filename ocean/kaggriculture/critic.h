#pragma once
#include "policy.h"

// Training-only, same-state seat views. The actor ABI remains observation v3.
// The experimental binary appends these features; the stock binary does not.
#define KAG_CRITIC_FEATURES 128
#ifdef KAG_WITH_PAIRED_CRITIC
#define KAG_TRAIN_OBS_SIZE (KAG_ENTITY_OBS_SIZE + 2 * KAG_CRITIC_FEATURES)
#else
#define KAG_TRAIN_OBS_SIZE KAG_ENTITY_OBS_SIZE
#endif

KG_HD void kag_critic_features(const float* observation, float* out) {
#ifdef KAG_DIRECT_POLICY
    memcpy(out, observation, KAG_CRITIC_FEATURES * sizeof(float));
#else
    // Cash, clock, land, hiring, inventory and controller/global fields.
    memcpy(out, observation, 64 * sizeof(float));
    // Own plot state: unlocked, empty/weeds, plants/animals, quality/care/yield.
    for (int plot = 0; plot < 4; plot++) {
        const float* row = observation + KAG_PLOT_OFFSET + plot * KAG_PLOT_FEATURES;
        for (int feature = 0; feature < 12; feature++) {
            out[64 + 12 * plot + feature] = row[5 + feature];
        }
    }
    for (int item = 0; item < KAG_PRODUCT_COUNT; item++) {
        const float* row = observation + KAG_PRODUCT_OFFSET + item * KAG_PRODUCT_FEATURES;
        out[112 + item] = row[9];  // market price
    }
    for (int crop = 0; crop < 5; crop++) {
        out[121 + crop] = observation[KAG_PRODUCT_OFFSET + crop * KAG_PRODUCT_FEATURES + 12];
    }
    out[126] = out[127] = 0;
    for (int unit = 0; unit < KAG_WORKER_COUNT; unit++) {
        const float* row = observation + KAG_WORKER_OFFSET + unit * KAG_WORKER_FEATURES;
        out[126] += row[16];  // cargo
        out[127] += row[31];  // workers adjacent to shed
    }
    out[127] /= KAG_WORKER_COUNT;
#endif
}

KG_HD float kag_wld_reward(int money, int opponent_money) {
    return money > opponent_money ? 1.0f : money < opponent_money ? -1.0f : 0.0f;
}

KG_HD float kag_paired_value(float own, float opponent) {
    // Exactly 2*softmax([own, opponent])[0]-1, evaluated stably.
    return tanhf(0.5f * (own - opponent));
}

KG_HD float kag_paired_value_derivative(float value) {
    return 0.5f * (1.0f - value * value);
}
