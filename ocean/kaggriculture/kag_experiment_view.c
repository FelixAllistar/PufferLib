/* Read-only diagnostic extension. Separate shared library; no trainer ABI change. */
#include "kag_policy_view.c"

/* One context per episode retains previous intent/quantity/quadrant exactly
 * as the native trainer does. Legacy stateless exports remain for old callers. */
void* kg_experiment_context_create(int obs0, int obs1, int executor0, int executor1) {
    if ((unsigned)obs0>1 || (unsigned)obs1>1
            || (unsigned)executor0>1 || (unsigned)executor1>1) return NULL;
    Env* env = calloc(1, sizeof(*env));
    if (!env) return NULL;
    env->macro_mode = 2;
#ifdef KAG_MACRO_MODE_TASKS
    env->frozen_macro_mode = -1;
#endif
    env->macro_decision_interval = 1;
    env->macro_score_scale = 10000;
    env->policy_market_slots = KG_POLICY_MARKET_SLOTS;
    env->policy_max_hands = KG_MAX_HANDS;
    env->observation_version = obs0;
    env->frozen_observation_version = obs1;
    env->macro_executor_version = executor0;
    env->frozen_macro_executor_version = executor1;
    env->agents[1].policy = 1;
    return env;
}

void kg_experiment_context_free(void* context) { free(context); }

void kg_experiment_context_view(void* context, const KGState* state, int player,
        unsigned char* observation, unsigned char* mask) {
    Env* env = context;
    env->game_storage = *state;
    env->agents[player].observations = observation;
    env->agents[player].action_mask = mask;
    kag_write_observation(env, player);
    kag_write_mask(env, player);
}

void kg_experiment_context_action(void* context, const KGState* state, int player,
        float* requests, KGAction* output) {
    Env* env = context;
    env->game_storage = *state;
    env->agents[player].actions = requests;
    kag_decode_macro_action(output, &env->agents[player], state, player, env);
}

void kg_experiment_observation(const KGState* state, int player, int version,
        unsigned char* output) {
    Env env = {0};
    env.game_storage = *state;
    env.agents[player].observations = output;
    env.macro_mode = 2;
    env.observation_version = version;
    env.frozen_observation_version = -1;
    env.macro_decision_interval = 1;
    env.macro_score_scale = 10000;
    env.policy_market_slots = KG_POLICY_MARKET_SLOTS;
    env.policy_max_hands = KG_MAX_HANDS;
    kag_write_observation(&env, player);
}

void kg_experiment_metrics(const KGState* state, int player, double* out) {
    const KGPlayer* farm = &state->players[player];
    int cows = 0, ready_milk = 0, extra = 0;
    for (int tile = 0; tile < KG_MAX_TILES; tile++) {
        const KGTile* t = &farm->tiles[tile];
        /* The animal bitset includes empty housing; inspect the payload. */
        int animal = kg_is_animal_tile(t);
        int plant = (farm->plant_bits[tile / 64] >> (tile % 64)) & 1;
        if (animal && t->animal == KG_COW) {
            cows++;
            ready_milk += t->yield_units;
        }
        if ((tile % KG_MAX_BOARD_SIZE >= 5 || tile / KG_MAX_BOARD_SIZE >= 5) && (animal || plant)) extra++;
    }
    out[0] = farm->money;
    out[1] = kag_popcount(farm->unlocked_mask);
    out[2] = kag_live_tiles(farm, 0);
    out[3] = kag_live_tiles(farm, 1);
    out[4] = cows;
    out[5] = state->production_product_units[player][KG_ITEM_MILK];
    out[6] = ready_milk;
    out[7] = extra;
    out[8] = state->sales_revenue[player];
    out[9] = state->purchase_spend[player];
    out[10] = state->neglect_deaths[player];
    out[11] = farm->hand_count;
}

void kg_experiment_mask(const KGState* state, int player, unsigned char* output) {
    Env env = {0};
    env.game_storage = *state;
    env.agents[player].action_mask = output;
    env.macro_mode = 2;
    env.macro_decision_interval = 1;
    env.policy_market_slots = KG_POLICY_MARKET_SLOTS;
    env.policy_max_hands = KG_MAX_HANDS;
    kag_write_mask(&env, player);
}

void kg_experiment_action(const KGState* state, int player, float* requests, KGAction* output) {
    Env env = {0};
    Agent agent = {0};
    env.game_storage = *state;
    env.macro_mode = 2;
    env.macro_decision_interval = 1;
    env.policy_max_hands = KG_MAX_HANDS;
    agent.actions = requests;
    kag_decode_macro_action(output, &agent, state, player, &env);
}
