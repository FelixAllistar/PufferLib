#ifndef PUFFER_SURVIVORS_GPU_CU
#define PUFFER_SURVIVORS_GPU_CU

#ifndef PUFFER_GPU_ENV
#error "puffer_survivors.cu requires build.sh puffer_survivors --gpu"
#endif

#include "cuda/ps_cuda_sim.cu"

struct PSNativeVec {
    Env* envs;
    PSCudaSim* sim;
};

static PSNativeVec ps_native_vecs[8];

static void ps_native_bind_io(PSCudaSim* sim, obs_t* observations,
        float* actions, float* rewards, float* terminals) {
    if (sim->owns_io) {
        cudaFree(sim->observations);
        cudaFree(sim->actions);
        cudaFree(sim->rewards);
        cudaFree(sim->terminals);
        sim->owns_io = 0;
    }
    sim->observations = (float*)observations;
    sim->actions = actions;
    sim->rewards = rewards;
    sim->terminals = terminals;
}

static PSCudaSim* ps_native_find(Env* envs) {
    for (int i = 0; i < 8; i++) {
        if (ps_native_vecs[i].envs == envs) return ps_native_vecs[i].sim;
    }
    return nullptr;
}

static void ps_native_register(Env* envs, PSCudaSim* sim) {
    for (int i = 0; i < 8; i++) {
        if (ps_native_vecs[i].envs == nullptr) {
            ps_native_vecs[i].envs = envs;
            ps_native_vecs[i].sim = sim;
            return;
        }
    }
    exit(1);
}

static void ps_native_unregister(Env* envs) {
    for (int i = 0; i < 8; i++) {
        if (ps_native_vecs[i].envs == envs) {
            ps_native_vecs[i].envs = nullptr;
            ps_native_vecs[i].sim = nullptr;
            return;
        }
    }
}

PSCudaSim* ps_cuda_get_sim(Env* envs) {
    return ps_native_find(envs);
}

static Env* puf_envs_create(int total_agents, Dict* env_kwargs) {
    PSCudaSim* sim = (PSCudaSim*)calloc(1, sizeof(PSCudaSim));
    ps_cuda_alloc(sim, total_agents, ps_config_from_kwargs(env_kwargs));

    Env* envs = nullptr;
    cudaMalloc((void**)&envs, (size_t)total_agents * sizeof(Env));
    cudaMemset(envs, 0, (size_t)total_agents * sizeof(Env));
    sim->native_envs = envs;
    ps_native_register(envs, sim);
    return envs;
}

static void puf_envs_reset(Env* envs, obs_t* observations, float* rewards,
        float* terminals, int total_agents) {
    (void)total_agents;
    PSCudaSim* sim = ps_native_find(envs);
    ps_native_bind_io(sim, observations, nullptr, rewards, terminals);
    ps_cuda_reset_all(sim, 1u);
}

static void puf_envs_step(Env* envs, const float* actions, obs_t* observations,
        float* rewards, float* terminals, int start, int count, cudaStream_t stream) {
    PSCudaSim* sim = ps_native_find(envs);
    sim->observations = (float*)observations;
    sim->actions = (float*)actions;
    sim->rewards = rewards;
    sim->terminals = terminals;
    ps_cuda_step_range(sim, start, count, stream);
}

static void puf_envs_close(Env* envs) {
    PSCudaSim* sim = ps_native_find(envs);
    ps_cuda_free(sim);
    free(sim);
    cudaFree(envs);
    ps_native_unregister(envs);
}

#endif
