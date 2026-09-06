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

void kg_policy_observation_mode(const KGState* state, int player,
        int macro_mode, unsigned char* output, size_t output_size) {
    Env env;
    if (state == NULL || output == NULL || output_size != OBS_SIZE
            || player < 0 || player >= KG_NUM_PLAYERS
            || macro_mode < 0 || macro_mode > KAG_MACRO_MODE_TASKS) {
        return;
    }
    memset(&env, 0, sizeof(env));
    env.game_storage = *state;
    env.agents[player].observations = output;
    env.macro_mode = macro_mode;
    env.frozen_macro_mode = -1;
    env.macro_decision_interval = 1;
    env.macro_score_scale = 10000.0f;
    /* These limits are the elite policy ABI, matching the normal trainer. */
    env.policy_market_slots = KG_POLICY_MARKET_SLOTS;
    env.policy_max_hands = KG_POLICY_DIRECT_HANDS;
    env.reset_source = 0;
    kag_write_observation(&env, player);
}

void kg_policy_observation(const KGState* state, int player,
        unsigned char* output, size_t output_size) {
    kg_policy_observation_mode(state, player, 0, output, output_size);
}

void kg_policy_action_mask_mode(const KGState* state, int player,
        int macro_mode, unsigned char* output, size_t output_size) {
    Env env;
    if (state == NULL || output == NULL || output_size != KG_POLICY_ACTION_MASK_SIZE
            || player < 0 || player >= KG_NUM_PLAYERS
            || macro_mode < 0 || macro_mode > KAG_MACRO_MODE_TASKS) {
        return;
    }
    memset(&env, 0, sizeof(env));
    env.game_storage = *state;
    env.agents[player].action_mask = output;
    env.macro_mode = macro_mode;
    env.frozen_macro_mode = -1;
    env.macro_decision_interval = 1;
    env.macro_score_scale = 10000.0f;
    env.policy_market_slots = KG_POLICY_MARKET_SLOTS;
    env.policy_max_hands = KG_POLICY_DIRECT_HANDS;
    kag_write_mask(&env, player);
}

void kg_policy_action_mask(const KGState* state, int player,
        unsigned char* output, size_t output_size) {
    kg_policy_action_mask_mode(state, player, 0, output, output_size);
}

int kg_policy_hand_count(const KGState* state, int player) {
    if (state == NULL || player < 0 || player >= KG_NUM_PLAYERS) return 0;
    return (int)state->players[player].hand_count;
}

/* Offline parity oracle: execute the production task decoder directly. */
int kg_policy_macro_action(const KGState* state, int player,
        float* requests, KGAction* output) {
    if (!state || !requests || !output || player < 0 || player >= KG_NUM_PLAYERS)
        return 0;
    Env env = {0};
    Agent agent = {0};
    env.game_storage = *state;
    env.macro_mode = KAG_MACRO_MODE_STRUCTURED;
    env.frozen_macro_mode = -1;
    env.macro_decision_interval = 1;
    env.policy_max_hands = KG_POLICY_DIRECT_HANDS;
    agent.actions = requests;
    kag_decode_macro_action(output, &agent, state, player, &env);
    return 1;
}

int kg_policy_task_action(const KGState* state, int player,
        float* requests, KGAction* output) {
    if (!state || !requests || !output || player < 0 || player >= KG_NUM_PLAYERS)
        return 0;
    Agent agent = {0};
    agent.actions = requests;
    kag_decode_task_action(output, &agent, state, player);
    return 1;
}
