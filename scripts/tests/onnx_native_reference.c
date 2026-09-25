#include "puffercpu.c"

void reference(const char* checkpoint, int batch, int obs_size, int hidden,
        int layers, int actions, float* observations, float* state,
        float* output, float* next_state) {
    Weights* weights = load_weights(checkpoint);
    assert(weights);
    int sizes[] = {actions};
    PufferNet* net = make_puffernet(weights, batch, obs_size, hidden, layers, sizes, 1);
    memcpy(net->mingru->state, state, layers * batch * hidden * sizeof(float));
    linear(net->encoder, observations);
    mingru(net->mingru, net->encoder->output);
    linear(net->decoder, net->mingru->output);
    memcpy(output, net->decoder->output, batch * (actions + 1) * sizeof(float));
    memcpy(next_state, net->mingru->state, layers * batch * hidden * sizeof(float));
    free_puffernet(net);
    free(weights);
}
