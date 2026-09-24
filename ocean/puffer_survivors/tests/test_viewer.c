#define main ps_viewer_main
#include "../puffer_survivors.c"
#undef main

int main(int argc, char** argv) {
    assert(argc == 2);
    PufferNet* net = ps_load_policy(argv[1]);
    assert(net);
    float observations[OBS_SIZE] = {0};
    float actions[NUM_ATNS] = {0};
    float terminals[1] = {1};
    for (int i = 0; i < OBS_SIZE; i++) {
        observations[i] = (i % 13) / 13.0f;
    }
    ps_forward_policy_argmax(net, observations, actions);
    int sizes[] = ACT_SIZES;
    int offset = 0;
    for (int head = 0; head < NUM_ATNS; head++) {
        int best = 0;
        for (int a = 1; a < sizes[head]; a++) {
            if (net->decoder->output[offset + a] > net->decoder->output[offset + best]) {
                best = a;
            }
        }
        assert(actions[head] == best);
        offset += sizes[head];
    }
    ps_reset_policy_state(net);
    for (int i = 0; i < net->mingru->num_layers * net->mingru->hidden_size; i++) {
        assert(net->mingru->state[i] == 0);
    }
    forward_puffernet(net, observations, actions, NULL, terminals);
    for (int head = 0; head < NUM_ATNS; head++) {
        assert(actions[head] >= 0 && actions[head] < sizes[head]);
    }
    free_puffernet(net);
    free(g_policy_weights);
    puts("Puffer Survivors viewer inference passed");
}
