#ifndef PUFFER_SURVIVORS_GPU_CU
#define PUFFER_SURVIVORS_GPU_CU

#ifndef PUFFER_GPU_ENV
#error "puffer_survivors.cu requires build.sh puffer_survivors --gpu"
#endif

#include "cuda/ps_cuda_sim.cu"

static PSCudaSim* ps_native_sim = nullptr;

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

static Env* puf_envs_create(int total_agents, Dict* env_kwargs) {
    if (ps_native_sim != nullptr) {
        fprintf(stderr, "Puffer Survivors supports one native GPU VecEnv per process\n");
        exit(1);
    }

    ps_native_sim = (PSCudaSim*)calloc(1, sizeof(PSCudaSim));
    ps_cuda_alloc(ps_native_sim, total_agents, ps_config_from_kwargs(env_kwargs));

    Env* envs = nullptr;
    cudaMalloc((void**)&envs, (size_t)total_agents * sizeof(Env));
    cudaMemset(envs, 0, (size_t)total_agents * sizeof(Env));
    ps_native_sim->native_envs = envs;
    return envs;
}

static void puf_envs_reset(Env* envs, obs_t* observations, float* rewards,
        float* terminals, int total_agents) {
    (void)envs;
    if (ps_native_sim == nullptr || ps_native_sim->num_envs != total_agents) {
        fprintf(stderr, "Puffer Survivors native GPU reset received the wrong VecEnv\n");
        exit(1);
    }
    ps_native_bind_io(ps_native_sim, observations, nullptr, rewards, terminals);
    ps_cuda_reset_all(ps_native_sim, 1u);
}

static void puf_envs_step(Env* envs, const float* actions, obs_t* observations,
        float* rewards, float* terminals, int start, int count, cudaStream_t stream) {
    (void)envs;
    ps_native_sim->observations = (float*)observations;
    ps_native_sim->actions = (float*)actions;
    ps_native_sim->rewards = rewards;
    ps_native_sim->terminals = terminals;
    ps_cuda_step_range(ps_native_sim, start, count, stream);
    int blocks = (ps_native_sim->num_envs + PS_CUDA_BLOCK_SIZE - 1) / PS_CUDA_BLOCK_SIZE;
    ps_pack_episode_logs_kernel<<<blocks, PS_CUDA_BLOCK_SIZE, 0, stream>>>(*ps_native_sim);
}

static void puf_envs_close(Env* envs) {
    if (ps_native_sim != nullptr) {
        ps_cuda_free(ps_native_sim);
        free(ps_native_sim);
        ps_native_sim = nullptr;
    }
    cudaFree(envs);
}

#endif
