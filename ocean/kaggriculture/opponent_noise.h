#pragma once

// Opponent-only epsilon curriculum. Counts vector ticks, not episode length,
// wins or replay-reset depth; one tick advances global agent steps by step_scale.
typedef struct KagOpponentNoise {
    float initial, final;
    double decay_steps, step_scale;
    uint64_t ticks;
    unsigned int rng;
} KagOpponentNoise;

KG_HD float kag_opponent_noise_probability(const KagOpponentNoise* schedule) {
    if (schedule->initial == schedule->final) return schedule->initial;
    double progress = (double)schedule->ticks * schedule->step_scale / schedule->decay_steps;
    if (progress >= 1) return schedule->final;
    return (float)(schedule->initial + progress * (schedule->final - schedule->initial));
}

KG_HD unsigned int kag_opponent_noise_random(KagOpponentNoise* schedule) {
    schedule->rng = 1664525u * schedule->rng + 1013904223u;
    return schedule->rng;
}

// Replace the whole frozen action with a legal sequentially sampled macro.
// Preserve conditional unit/market masks, shared budgets, early STOP and inactive
// heads. No mutation of learner actions, logits, logprobs, masks or sampling RNG.
KG_HD void kag_opponent_random_action(KagOpponentNoise* schedule, KagPolicy* policy,
    const KGState* game, int player, float* actions) {
    const int sizes[KAG_ACTION_HEADS] = KAG_ACTION_SIZES;
    unsigned char mask[KG_POLICY_ACTION_MASK_SIZE];
    KagActionMaskState prefix;
    kag_write_mask(policy, game, player, mask);
    kag_action_mask_begin(&prefix, game, policy, player);
    int offset = 0;
    for (int h = 0; h < KAG_ACTION_HEADS; h++) {
        kag_action_mask_before(&prefix, h, mask);
        int selected = 0;
        if (kag_action_head_active(prefix.choices, h)) {
            int legal = 0;
            for (int a = 0; a < sizes[h]; a++) legal += mask[offset + a] != 0;
            assert(legal > 0);
            // 24-bit uniform draw, avoiding correlations in the LCG's low bits.
            int pick = (int)((kag_opponent_noise_random(schedule) >> 8)
                * (1.0 / 16777216.0) * legal);
            for (int a = 0; a < sizes[h]; a++) {
                if (mask[offset + a] && pick-- == 0) {
                    selected = a;
                    break;
                }
            }
            kag_action_mask_commit(&prefix, h, selected);
        }
        actions[h] = (float)selected;
        offset += sizes[h];
    }
}

KG_HD int kag_opponent_noise_replace(KagOpponentNoise* schedule, int policy, float noise) {
    if (policy == 0 || noise <= 0) return 0;
    return (kag_opponent_noise_random(schedule) >> 8) * (1.0 / 16777216.0) < noise;
}
