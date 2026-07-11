#include "ps_cuda_vec.h"
#include "ps_cuda_sim.cuh"

#include <cstdlib>

struct PufferSurvivorsCudaVec {
    PSCudaSim* sim;
};

extern "C" void* ps_survivors_cuda_vec_create(
        int num_envs,
        PSConfig cfg,
        float* observations,
        float* actions,
        float* rewards,
        float* terminals) {
    PufferSurvivorsCudaVec* handle = (PufferSurvivorsCudaVec*)std::calloc(1, sizeof(PufferSurvivorsCudaVec));
    if (handle == nullptr) return nullptr;
    handle->sim = ps_cuda_sim_create(num_envs, cfg, observations, actions, rewards, terminals);
    if (handle->sim == nullptr) {
        std::free(handle);
        return nullptr;
    }
    return handle;
}

extern "C" void ps_survivors_cuda_vec_reset(void* raw, unsigned int seed, void* stream) {
    if (raw == nullptr) return;
    ps_cuda_sim_reset(((PufferSurvivorsCudaVec*)raw)->sim, seed, stream);
}

extern "C" void ps_survivors_cuda_vec_step_range(void* raw, int start, int count, void* stream) {
    if (raw == nullptr) return;
    ps_cuda_sim_step_range(((PufferSurvivorsCudaVec*)raw)->sim, start, count, stream);
}

extern "C" float ps_survivors_cuda_vec_log(void* raw, Log* out, int clear) {
    if (raw == nullptr || out == nullptr) return 0.0f;
    return ps_cuda_sim_log(((PufferSurvivorsCudaVec*)raw)->sim, out, clear, nullptr);
}

extern "C" void ps_survivors_cuda_vec_destroy(void* raw) {
    if (raw == nullptr) return;
    PufferSurvivorsCudaVec* handle = (PufferSurvivorsCudaVec*)raw;
    ps_cuda_sim_destroy(handle->sim);
    std::free(handle);
}
