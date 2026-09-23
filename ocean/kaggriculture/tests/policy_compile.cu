#include "../policy.h"

// Compile qualification only. This is not a CUDA runtime/parity test.
__global__ void policy_compile(KGState* games, KagPolicy* policies, float* observations,
    float* choices, unsigned char* masks, KGAction* actions) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    KGState* game = games + i;
    KagPolicy* policy = policies + i;
    float* chosen = choices + i * KAG_ACTION_HEADS;
    unsigned char* mask = masks + i * KG_POLICY_ACTION_MASK_SIZE;
    kag_policy_reset(policy, game, 0);
    kag_write_mask(policy, game, 0, mask);
    kag_write_observation(policy, game, 0, observations + i * KAG_ENTITY_OBS_SIZE);
    KagActionMaskState prefix;
    kag_action_mask_begin(&prefix, game, policy, 0);
    for (int h = 0; h < KAG_ACTION_HEADS; h++) {
        kag_action_mask_before(&prefix, h, mask);
        kag_action_mask_commit(&prefix, h, (int)chosen[h]);
    }
    kag_decode_multi_action(actions + i, chosen, game, 0, policy);
    kag_policy_step(policy, game);
}
