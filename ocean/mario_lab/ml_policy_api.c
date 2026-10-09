#include "../../src/puffercpu.c"
#include "ml_sim.h"
#include "ml_policy_api.h"

typedef struct { Weights* weights; PufferNet* net; } MLCpu;
void* ml_cpu_load(const char* path, int hidden, int layers) {
    if (hidden < 32 || hidden > 512 || hidden % 32 || layers < 1 || layers > 8) return NULL;
    Weights* weights = load_weights(path);
    int need = ML_OBS_SIZE * hidden + 65 * hidden + layers * 3 * hidden * hidden;
    if (!weights || weights->size - 7 != need) { free(weights); return NULL; }
    for (int i = 0; i < need; i++) if (!isfinite(weights->data[i])) { free(weights); return NULL; }
    MLCpu* cpu = calloc(1, sizeof(MLCpu));
    if (!cpu) { free(weights); return NULL; }
    int sizes[] = {64}; cpu->weights = weights;
    cpu->net = make_puffernet(weights, 1, ML_OBS_SIZE, hidden, layers, sizes, 1);
    return cpu;
}
const float* ml_cpu_logits(void* handle, const float* observations) {
    MLCpu* cpu = handle; PufferNet* net = cpu->net;
    linear(net->encoder, (float*)observations);
    mingru(net->mingru, net->encoder->output);
    linear(net->decoder, net->mingru->output);
    return net->decoder->output;
}
int ml_cpu_action(void* handle, const float* obs, int deterministic, float* button_probs, float* entropy) {
    MLCpu* cpu = handle;
    const float* logits = ml_cpu_logits(cpu, obs);
    float action; multidiscrete(cpu->net->multidiscrete, (float*)logits, &action, deterministic, NULL);
    float largest = logits[0], total = 0, probabilities[64];
    for (int i = 0; i < 64; i++) largest = fmaxf(largest, logits[i]);
    for (int i = 0; i < 64; i++) { probabilities[i] = expf(logits[i] - largest); total += probabilities[i]; }
    if (button_probs) memset(button_probs, 0, 6 * sizeof(float));
    if (entropy) *entropy = 0;
    for (int i = 0; i < 64; i++) {
        float p = probabilities[i] / total;
        if (entropy && p > 0) *entropy -= p * logf(p);
        if (button_probs) for (int bit = 0; bit < 6; bit++) if (i & (1 << bit)) button_probs[bit] += p;
    }
    return (int)action;
}
void ml_cpu_reset(void* handle) {
    MLCpu* cpu = handle; MinGRU* rnn = cpu->net->mingru;
    memset(rnn->state, 0, rnn->hidden_size * rnn->num_layers * sizeof(float));
}
void ml_cpu_free(void* handle) {
    if (!handle) return;
    MLCpu* cpu = handle; free_puffernet(cpu->net); free(cpu->weights); free(cpu);
}
