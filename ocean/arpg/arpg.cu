#ifndef ARPG_GPU_CU
#define ARPG_GPU_CU

#ifndef PUFFER_GPU_ENV
#error "arpg.cu requires build.sh arpg --gpu"
#endif

#include "cuda/ar_cuda_sim.cu"

struct ARNativeVec {
    Env* envs;
    ARCudaSim* sim;
};

static ARNativeVec ar_native_vecs[8];

static void ar_native_bind_io(ARCudaSim* sim, obs_t* observations,
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

static ARCudaSim* ar_native_find(Env* envs) {
    for (int i = 0; i < 8; i++) {
        if (ar_native_vecs[i].envs == envs) return ar_native_vecs[i].sim;
    }
    return nullptr;
}

static void ar_native_register(Env* envs, ARCudaSim* sim) {
    for (int i = 0; i < 8; i++) {
        if (ar_native_vecs[i].envs == nullptr) {
            ar_native_vecs[i].envs = envs;
            ar_native_vecs[i].sim = sim;
            return;
        }
    }
    exit(1);
}

static void ar_native_unregister(Env* envs) {
    for (int i = 0; i < 8; i++) {
        if (ar_native_vecs[i].envs == envs) {
            ar_native_vecs[i].envs = nullptr;
            ar_native_vecs[i].sim = nullptr;
            return;
        }
    }
}

ARCudaSim* ar_cuda_get_sim(Env* envs) {
    return ar_native_find(envs);
}

static Env* puf_envs_create(int total_agents, Dict* env_kwargs) {
    ARCudaSim* sim = (ARCudaSim*)calloc(1, sizeof(ARCudaSim));
    ar_cuda_alloc(sim, total_agents, ar_config_from_kwargs(env_kwargs));

    Env* envs = nullptr;
    cudaMalloc((void**)&envs, (size_t)total_agents * sizeof(Env));
    cudaMemset(envs, 0, (size_t)total_agents * sizeof(Env));
    sim->native_envs = envs;
    ar_native_register(envs, sim);
    return envs;
}

static void puf_envs_reset(Env* envs, obs_t* observations, float* rewards,
        float* terminals, int total_agents) {
    (void)total_agents;
    ARCudaSim* sim = ar_native_find(envs);
    ar_native_bind_io(sim, observations, nullptr, rewards, terminals);
    ar_cuda_reset_all(sim, 1u);
}

static void puf_envs_step(Env* envs, const float* actions, obs_t* observations,
        float* rewards, float* terminals, int start, int count,
        cudaStream_t stream) {
    ARCudaSim* sim = ar_native_find(envs);
    sim->observations = (float*)observations;
    sim->actions = (float*)actions;
    sim->rewards = rewards;
    sim->terminals = terminals;
    ar_cuda_step_range(sim, start, count, stream);
}

static void puf_envs_close(Env* envs) {
    ARCudaSim* sim = ar_native_find(envs);
    ar_cuda_free(sim);
    free(sim);
    cudaFree(envs);
    ar_native_unregister(envs);
}

#endif
