#include "retro_policy_cpu.h"

// C inference boundary for the C++ emulator/viewer. Owns checkpoint storage.
typedef struct {
    RetroPolicy* policy;
    Weights* weights;
} RetroCpu;

void* retro_cpu_load(const char* path, int hidden, int layers) {
    Weights* weights = load_weights(path);
    if (!weights) {
        return NULL;
    }
    RetroCpu* cpu = calloc(1, sizeof(RetroCpu));
    cpu->weights = weights;
    cpu->policy = make_retro_policy(weights, hidden, layers);
    return cpu;
}

void retro_cpu_act(void* policy, float* obs, float* action, bool deterministic) {
    RetroCpu* cpu = policy;
    retro_policy_act(cpu->policy, obs, action, deterministic);
}

void retro_cpu_reset(void* policy) {
    RetroPolicy* p = ((RetroCpu*)policy)->policy;
    memset(p->mingru->state, 0,
        p->mingru->hidden_size*p->mingru->num_layers*sizeof(float));
}

void retro_cpu_free(void* policy) {
    if (!policy) {
        return;
    }
    RetroCpu* cpu = policy;
    free_retro_policy(cpu->policy);
    free(cpu->weights);
    free(cpu);
}
