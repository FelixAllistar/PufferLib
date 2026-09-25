#define main bomberman_viewer_main
#ifdef BM_LEGACY_VIEWER
#include BM_LEGACY_VIEWER
#else
#include "bomberman.c"
#endif
#undef main

static uint64_t trace_bytes(uint64_t trace, const void* data, size_t size) {
    const unsigned char* bytes = data;
    for (size_t i = 0; i < size; i++) {
        trace = (trace ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return trace;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    BMConfig cfg;
    int hidden, layers;
    char config_path[4096];
    load_model_config(argv[1], &cfg, &hidden, &layers, config_path, sizeof(config_path));
    cfg.max_ticks = 32;
    srand(123);
    uint64_t trace = UINT64_C(14695981039346656037);
    int resets = 0;
    Env env = make_play_env(2, &cfg);
    float observations[2 * OBS_SIZE];
    float actions[2] = {0}, rewards[2] = {0}, terminals[2] = {0};
    bind_agents(&env, observations, actions, rewards, terminals);
    puf_reset(&env);
    int sizes[] = ACT_SIZES;
    Weights* weights = load_weights(argv[1]);
    assert(weights);
    PufferNet* joint = make_puffernet(weights, 2, OBS_SIZE, hidden, layers, sizes, NUM_ATNS);
    PufferNet* single[2];
    for (int i = 0; i < 2; i++) {
        weights->idx = 0;
        single[i] = make_puffernet(weights, 1, OBS_SIZE, hidden, layers, sizes, NUM_ATNS);
    }
    for (int step = 0; step < 1024; step++) {
#ifdef BM_LEGACY_VIEWER
        forward_masked_bomberman(joint, &env, observations, actions);
#else
        forward_masked_bomberman(joint, &env, observations, actions, 0);
#endif
        trace = trace_bytes(trace, observations, sizeof(observations));
        trace = trace_bytes(trace, joint->decoder->output,
            2 * (BM_NUM_ACTIONS + 1) * sizeof(float));
        trace = trace_bytes(trace, actions, sizeof(actions));
        for (int i = 0; i < 2; i++) {
            assert(bm_action_legal(&env.match, i, (int)actions[i]));
#ifdef BM_LEGACY_VIEWER
            forward_masked_agent(single[i], &env, i, observations, actions);
#else
            forward_masked_bomberman(single[i], &env, observations, actions, i);
#endif
            assert(bm_action_legal(&env.match, i, (int)actions[i]));
            for (int j = 0; j < BM_NUM_ACTIONS + 1; j++) {
                float joint_logit = joint->decoder->output[i * (BM_NUM_ACTIONS + 1) + j];
                assert(fabsf(joint_logit - single[i]->decoder->output[j]) < 1e-5f);
            }
        }
        trace = trace_bytes(trace, actions, sizeof(actions));
        puf_step(&env);
        trace = trace_bytes(trace, rewards, sizeof(rewards));
        trace = trace_bytes(trace, terminals, sizeof(terminals));
        if (terminals[0] || terminals[1]) {
            resets++;
            reset_policy_state(joint);
            reset_policy_state(single[0]);
            reset_policy_state(single[1]);
        }
    }
    assert(resets >= 32);
    reset_policy_state(joint);
    for (int i = 0; i < 2 * hidden * layers; i++) {
        assert(joint->mingru->state[i] == 0);
    }
    free_puffernet(joint);
    free_puffernet(single[0]);
    free_puffernet(single[1]);
    free(weights);
    printf("Bomberman viewer trace: %016llx steps=1024 resets=%d H=%d L=%d\n",
        (unsigned long long)trace, resets, hidden, layers);
    puts("Bomberman viewer: legal sampling, separate/joint logits, carry reset passed");
}
