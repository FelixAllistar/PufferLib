#pragma once

#include "../ps_config.h"
#include "../ps_log.h"

struct PSCudaSim;

PSCudaSim* ps_cuda_sim_create(
    int num_envs,
    PSConfig cfg,
    float* observations,
    float* actions,
    float* rewards,
    float* terminals);
void ps_cuda_sim_reset(PSCudaSim* sim, unsigned int seed, void* stream);
void ps_cuda_sim_step_range(PSCudaSim* sim, int start, int count, void* stream);
float ps_cuda_sim_log(PSCudaSim* sim, Log* out, int clear, void* stream);
void ps_cuda_sim_destroy(PSCudaSim* sim);
