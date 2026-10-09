#define ML_HEADLESS
#include "../mario_lab.cu"
#include <chrono>

__global__ void scripted_actions(const MLState* states, float* actions, int count) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < count) actions[i] = (float)ml_script_action(&states[i]);
}
int main(int argc, char** argv) {
    int n = argc > 1 ? atoi(argv[1]) : 4096, steps = argc > 2 ? atoi(argv[2]) : 512;
    if (n < 1 || n > 65536 || steps < 1 || steps > 100000) return 2;
    Ini ini = {}; Dict defaults = {0}; Dict* cfg = &defaults;
    if (argc > 3) { puf_ini_load_file(&ini, argv[3]); cfg = puf_ini_section(&ini, "env", 0); }
    float *obs, *actions, *rewards, *terminals;
    ML_CUDA(cudaMalloc(&obs, (size_t)n * ML_OBS_SIZE * sizeof(float)));
    ML_CUDA(cudaMalloc(&actions, n * sizeof(float)));
    ML_CUDA(cudaMalloc(&rewards, n * sizeof(float)));
    ML_CUDA(cudaMalloc(&terminals, n * sizeof(float)));
    Env* envs = puf_vec_create(n, cfg, obs, actions, rewards, terminals);
    puf_reset(envs);
    for (int t = 0; t < 64; t++) {
        scripted_actions<<<(n+127)/128, 128>>>(ml_states, actions, n); puf_step(envs);
    }
    ML_CUDA(cudaDeviceSynchronize());
    auto start = std::chrono::steady_clock::now();
    for (int t = 0; t < steps; t++) {
        scripted_actions<<<(n+127)/128, 128>>>(ml_states, actions, n); puf_step(envs);
    }
    ML_CUDA(cudaDeviceSynchronize());
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    cudaDeviceProp props; ML_CUDA(cudaGetDeviceProperties(&props, 0));
    Env* host = (Env*)malloc((size_t)n * sizeof(Env));
    ML_CUDA(cudaMemcpy(host, envs, (size_t)n * sizeof(Env), cudaMemcpyDeviceToHost));
    double episodes = 0, clears = 0;
    for (int i = 0; i < n; i++) { episodes += host[i].log.n; clears += host[i].log.clears; }
    printf("{\"gpu\":\"%s\",\"envs\":%d,\"steps\":%d,\"seconds\":%.6f,\"decisions_per_second\":%.0f,"
        "\"frames_per_decision\":1,\"warmup_steps\":64,\"seed\":%u,\"split\":%u,\"course\":%d,"
        "\"difficulty\":%d,\"length\":%d,\"max_frames\":%d,\"physics_mode\":%d,"
        "\"generator_mode\":%d,\"length_min\":%d,"
        "\"observations\":%d,\"state_bytes\":%zu,\"episodes_including_warmup\":%.0f,"
        "\"scripted_clears_including_warmup\":%.0f,\"includes_policy_training\":false}\n",
        props.name, n, steps, seconds, (double)n*steps/seconds,
        ml_host_config.seed, ml_host_config.split, ml_host_config.course, ml_host_config.difficulty,
        ml_host_config.length, ml_host_config.max_frames, ml_host_config.physics_mode,
        ml_host_config.generator_mode, ml_host_config.length_min, ML_OBS_SIZE, sizeof(MLState), episodes, clears);
    free(host); puf_close(envs);
    ML_CUDA(cudaFree(obs)); ML_CUDA(cudaFree(actions)); ML_CUDA(cudaFree(rewards)); ML_CUDA(cudaFree(terminals));
    puf_ini_free(&ini);
}
