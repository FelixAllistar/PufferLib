/* Fast exact policy view for offline counterfactual branching.
 *
 * The ordinary Python replay bridge serializes KGState as JSON and then walks
 * that object to reproduce the submission encoder/mask.  That is ideal for
 * parity debugging, but far too expensive when thousands of native branches
 * need a learned PPO continuation.  This translation unit includes the same
 * production adapter used by the trainer and exposes two read-only helpers
 * that write its observation and legality mask directly from a KGState.
 */

#include <stddef.h>
#include <string.h>

#include "kaggriculture.h"

void kg_policy_observation(const KGState* state, int player, unsigned char* output,
        size_t output_size) {
    Env env;
    if (state == NULL || output == NULL || output_size != OBS_SIZE
            || player < 0 || player >= KG_NUM_PLAYERS) {
        return;
    }
    memset(&env, 0, sizeof(env));
    env.game_storage = *state;
    env.agents[player].observations = output;
    /* These limits are the elite policy ABI, matching the normal trainer. */
    env.policy_market_slots = KG_POLICY_MARKET_SLOTS;
    env.policy_max_hands = KG_POLICY_DIRECT_HANDS;
    env.reset_source = 0;
    kag_write_observation(&env, player);
}

void kg_policy_action_mask(const KGState* state, int player, unsigned char* output,
        size_t output_size) {
    Env env;
    if (state == NULL || output == NULL || output_size != KG_POLICY_ACTION_MASK_SIZE
            || player < 0 || player >= KG_NUM_PLAYERS) {
        return;
    }
    memset(&env, 0, sizeof(env));
    env.game_storage = *state;
    env.agents[player].action_mask = output;
    env.policy_market_slots = KG_POLICY_MARKET_SLOTS;
    env.policy_max_hands = KG_POLICY_DIRECT_HANDS;
    kag_write_mask(&env, player);
}

int kg_policy_hand_count(const KGState* state, int player) {
    if (state == NULL || player < 0 || player >= KG_NUM_PLAYERS) return 0;
    return (int)state->players[player].hand_count;
}
