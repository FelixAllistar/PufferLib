#pragma once

#include "../ps_config.h"
#include "../ps_log.h"

#ifdef __cplusplus
extern "C" {
#endif

void* ps_survivors_cuda_vec_create(
    int num_envs,
    PSConfig cfg,
    float* observations,
    float* actions,
    float* rewards,
    float* terminals);
void ps_survivors_cuda_vec_reset(void* handle, unsigned int seed, void* stream);
void ps_survivors_cuda_vec_step_range(void* handle, int start, int count, void* stream);
float ps_survivors_cuda_vec_log(void* handle, Log* out, int clear);
void ps_survivors_cuda_vec_destroy(void* handle);

#ifdef __cplusplus
}
#endif
