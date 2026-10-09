#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void* ml_cpu_load(const char* path, int hidden, int layers);
const float* ml_cpu_logits(void* cpu, const float* observations);
int ml_cpu_action(void* cpu, const float* observations, int deterministic, float* button_probs, float* entropy);
void ml_cpu_reset(void* cpu);
void ml_cpu_free(void* cpu);
#ifdef __cplusplus
}
#endif
