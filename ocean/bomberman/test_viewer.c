#define main bomberman_viewer_main
#include "bomberman.c"
#undef main

int main(int argc, char** argv) {
    assert(argc == 2);
    BMConfig cfg;
    int hidden, layers;
    char config_path[4096];
    load_model_config(argv[1], &cfg, &hidden, &layers, config_path, sizeof(config_path));
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
    for (int step = 0; step < 256; step++) {
        forward_masked_bomberman(joint, &env, observations, actions, 0);
        for (int i = 0; i < 2; i++) {
            assert(bm_action_legal(&env.match, i, (int)actions[i]));
            forward_masked_bomberman(single[i], &env, observations, actions, i);
            assert(bm_action_legal(&env.match, i, (int)actions[i]));
            for (int j = 0; j < BM_NUM_ACTIONS + 1; j++) {
                float joint_logit = joint->decoder->output[i * (BM_NUM_ACTIONS + 1) + j];
                assert(fabsf(joint_logit - single[i]->decoder->output[j]) < 1e-5f);
            }
        }
        puf_step(&env);
        if (terminals[0] || terminals[1]) {
            reset_policy_state(joint);
            reset_policy_state(single[0]);
            reset_policy_state(single[1]);
        }
    }
    reset_policy_state(joint);
    for (int i = 0; i < 2 * hidden * layers; i++) {
        assert(joint->mingru->state[i] == 0);
    }
    free_puffernet(joint);
    free_puffernet(single[0]);
    free_puffernet(single[1]);
    free(weights);
    puts("Bomberman viewer: legal sampling, separate/joint logits, carry reset passed");
}
