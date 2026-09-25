#include "bomberman.cu"
#include <assert.h>

#define CUDA_OK(call) do { \
    cudaError_t err = (call); \
    if (err != cudaSuccess) { \
        fprintf(stderr, "%s: %s\n", #call, cudaGetErrorString(err)); \
        exit(1); \
    } \
} while (0)

static void run_suite(int agents, int max_ticks, int capture) {
    Ini ini = {};
    puf_ini_load_env(&ini, "bomberman", 0, NULL);
    Dict* kwargs = puf_ini_section(&ini, "env", 0);
    dict_set(kwargs, "reverse_curriculum", 0);
    dict_set(kwargs, "num_agents", agents);
    dict_set(kwargs, "max_ticks", max_ticks);
    const int matches = 4;
    int rows = matches * agents;
    float *obs, *actions, *rewards, *terminals;
    CUDA_OK(cudaMalloc(&obs, rows * OBS_SIZE * sizeof(float)));
    CUDA_OK(cudaMalloc(&actions, rows * sizeof(float)));
    CUDA_OK(cudaMalloc(&rewards, rows * sizeof(float)));
    CUDA_OK(cudaMalloc(&terminals, rows * sizeof(float)));
    Env* shells = puf_vec_create(rows, kwargs, obs, actions, rewards, terminals);
    cudaStream_t stream;
    CUDA_OK(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));
    puf_bind_stream(stream);
    puf_reset(shells);
    CUDA_OK(cudaStreamSynchronize(stream));
    cudaGraph_t graph = NULL;
    cudaGraphExec_t executable = NULL;
    if (capture) {
        CUDA_OK(cudaStreamBeginCapture(stream, cudaStreamCaptureModeGlobal));
        puf_step(shells);
        CUDA_OK(cudaStreamEndCapture(stream, &graph));
        CUDA_OK(cudaGraphInstantiate(&executable, graph, NULL, NULL, 0));
    }
    BMMatch cpu[matches], gpu[matches];
    Log logs[matches] = {};
    for (int mid = 0; mid < matches; mid++)
        bm_reset_match(&cpu[mid], &h_bcfg, (uint32_t)(mid + 1) * 0x9E3779B9u);
    uint32_t rng = 123;
    for (int step = 0; step < 256; step++) {
        float host_actions[matches * BM_MAX_AGENTS];
        float expected_r[matches * BM_MAX_AGENTS], expected_t[matches * BM_MAX_AGENTS];
        for (int mid = 0; mid < matches; mid++) {
            int acts[BM_MAX_AGENTS];
            for (int a = 0; a < agents; a++)
                host_actions[mid * agents + a] = acts[a] = bm_randi(&rng, BM_NUM_ACTIONS);
            bm_step_match(&cpu[mid], &h_bcfg, acts,
                expected_r + mid * agents, expected_t + mid * agents);
            if (cpu[mid].done) {
                int outcome = cpu[mid].winner == 0 ? 1 : cpu[mid].winner > 0 ? -1 : 0;
                bm_log_match(&logs[mid], &cpu[mid], outcome);
                uint32_t seed = cpu[mid].rng ^ (0x85ebca6bu * (uint32_t)(cpu[mid].tick + 1));
                bm_reset_match(&cpu[mid], &h_bcfg, seed);
            }
        }
        CUDA_OK(cudaMemcpyAsync(actions, host_actions, rows * sizeof(float),
            cudaMemcpyHostToDevice, stream));
        if (capture) {
            CUDA_OK(cudaGraphLaunch(executable, stream));
        } else {
            puf_step(shells);
        }
        CUDA_OK(cudaDeviceSynchronize());
        CUDA_OK(cudaMemcpy(gpu, d_matches, sizeof(gpu), cudaMemcpyDeviceToHost));
        assert(memcmp(cpu, gpu, sizeof(cpu)) == 0);
        float actual_r[matches * BM_MAX_AGENTS], actual_t[matches * BM_MAX_AGENTS];
        CUDA_OK(cudaMemcpy(actual_r, rewards, rows * sizeof(float), cudaMemcpyDeviceToHost));
        CUDA_OK(cudaMemcpy(actual_t, terminals, rows * sizeof(float), cudaMemcpyDeviceToHost));
        for (int row = 0; row < rows; row++) {
            assert(fabsf(actual_r[row] - expected_r[row]) < 1e-6f);
            assert(actual_t[row] == expected_t[row]);
            float expected_obs[OBS_SIZE], actual_obs[OBS_SIZE];
            bm_write_obs(&cpu[row / agents], &h_bcfg, row % agents, expected_obs);
            CUDA_OK(cudaMemcpy(actual_obs, obs + row * OBS_SIZE, sizeof(actual_obs), cudaMemcpyDeviceToHost));
            for (int i = 0; i < OBS_SIZE; i++) assert(fabsf(actual_obs[i] - expected_obs[i]) < 1e-6f);
        }
        for (int mid = 0; mid < matches; mid++) {
            int metadata;
            CUDA_OK(cudaMemcpy(&metadata, &shells[mid].num_agents,
                sizeof metadata, cudaMemcpyDeviceToHost));
            assert(metadata == agents);
            Log actual;
            CUDA_OK(cudaMemcpy(&actual, &shells[mid].log, sizeof(actual), cudaMemcpyDeviceToHost));
            for (int i = 0; i < (int)(sizeof(Log) / sizeof(float)); i++)
                assert(fabsf(((float*)&actual)[i] - ((float*)&logs[mid])[i]) < 1e-4f);
            assert(actual.curriculum_stage == BM_CURRICULUM_STAGES * actual.n);
            assert(actual.curriculum_full_game == actual.n);
        }
    }
    if (capture) {
        CUDA_OK(cudaGraphExecDestroy(executable));
        CUDA_OK(cudaGraphDestroy(graph));
    }
    puf_close(shells);
    CUDA_OK(cudaStreamDestroy(stream));
    CUDA_OK(cudaFree(obs));
    CUDA_OK(cudaFree(actions));
    CUDA_OK(cudaFree(rewards));
    CUDA_OK(cudaFree(terminals));
    puf_ini_free(&ini);
    printf("Bomberman CUDA parity: agents=%d max_ticks=%d graph=%d PASS\n",
        agents, max_ticks, capture);
}

int main(void) {
    run_suite(2, 1, 0);
    run_suite(2, 128, 0);
    run_suite(4, 128, 0);
    run_suite(2, 128, 1);
}
