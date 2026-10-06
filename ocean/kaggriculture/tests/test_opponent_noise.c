#include "../kaggriculture.h"

#ifdef __CUDACC__
#include <cuda_runtime.h>
__global__ void noise_step(Env* envs, int games) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < games) kag_step(envs + i);
}
#endif

static void legal_actions(Env* env, int player, const float* actions) {
    unsigned char mask[KG_POLICY_ACTION_MASK_SIZE];
    KagActionMaskState prefix;
    const int sizes[] = KAG_ACTION_SIZES;
    kag_write_mask(&env->policy, &env->game, player, mask);
    kag_action_mask_begin(&prefix, &env->game, &env->policy, player);
    int offset = 0;
    for (int h = 0; h < KAG_ACTION_HEADS; h++) {
        kag_action_mask_before(&prefix, h, mask);
        int a = (int)actions[h];
        assert(actions[h] == a && a >= 0 && a < sizes[h]);
        if (kag_action_head_active(prefix.choices, h)) {
            assert(mask[offset + a]);
            kag_action_mask_commit(&prefix, h, a);
        } else assert(a == 0);
        offset += sizes[h];
    }
}

int main(void) {
    KagOpponentNoise s = {1, 0, 500000000, 4096, 0, 73};
    assert(kag_opponent_noise_probability(&s) == 1);
    s.ticks = 61035;
    assert(fabsf(kag_opponent_noise_probability(&s) - .5f) < .00001f);
    s.ticks = 122071;
    assert(kag_opponent_noise_probability(&s) == 0);
    unsigned int rng = s.rng;
    assert(!kag_opponent_noise_replace(&s, 0, 1) && s.rng == rng);
    assert(!kag_opponent_noise_replace(&s, 1, 0) && s.rng == rng);
    for (int i = 0; i < 100; i++) assert(kag_opponent_noise_replace(&s, 1, 1));

    Ini ini = {0};
    puf_ini_load_env(&ini, "kaggriculture", 0, NULL);
    Dict* cfg = puf_ini_section(&ini, "env", 0);
    dict_set(cfg, "num_agents", 2);
    dict_set(cfg, "reset_state_prob", 0);
    dict_set(cfg, "opponent_noise_initial", 1);
    dict_set(cfg, "opponent_noise_final", 0);
    dict_set(cfg, "opponent_noise_decay_steps", 4096 * 1024);
    dict_set(cfg, "opponent_noise_step_scale", 4096);
    // Random macros are always valid, including changed money/inventory and
    // all conditional market heads. Decoding advances the independent fixture.
    Env env = {0};
    puf_init(&env, cfg);
    float actions[KAG_ACTION_HEADS];
    for (int t = 0; t < 800; t++) {
        env.game.players[t % 2].money = t % 3 == 0 ? 0 : 100000;
        kag_opponent_random_action(&env.opponent_noise, &env.policy, &env.game, t % 2, actions);
        legal_actions(&env, t % 2, actions);
        KGAction commands[2] = {0};
        kag_decode_multi_action(&commands[t % 2], actions, &env.game, t % 2, &env.policy);
        kg_step(&env.game, commands);
        kag_policy_step(&env.policy, &env.game);
        if (env.game.done) kag_reset_episode(&env);
    }

    // Both physical seat assignments: primary policy is never overwritten.
    const int games = 4;
    Env* host = (Env*)calloc(games, sizeof(Env));
    float* outputs = (float*)calloc(games * 2 * (OBS_SIZE + NUM_ATNS + 2), sizeof(float));
#ifdef __CUDACC__
    Env* gpu = NULL;
    float* gpu_outputs = NULL;
    assert(cudaMallocManaged((void**)&gpu, games * sizeof(Env)) == cudaSuccess);
    assert(cudaMallocManaged((void**)&gpu_outputs,
        games * 2 * (OBS_SIZE + NUM_ATNS + 2) * sizeof(float)) == cudaSuccess);
    memset(gpu_outputs, 0, games * 2 * (OBS_SIZE + NUM_ATNS + 2) * sizeof(float));
#endif
    for (int i = 0; i < games; i++) {
        host[i].rng = i;
        puf_init(host + i, cfg);
        host[i].tag = i < 2 ? 1 : 0;
        host[i].agents[i % 2].policy = i < 2 ? 1 : 0;
        for (int a = 0; a < 2; a++) {
            float* out = outputs + (i * 2 + a) * (OBS_SIZE + NUM_ATNS + 2);
            host[i].agents[a].observations = out;
            host[i].agents[a].actions = out + OBS_SIZE;
            host[i].agents[a].rewards = out + OBS_SIZE + NUM_ATNS;
            host[i].agents[a].terminals = out + OBS_SIZE + NUM_ATNS + 1;
        }
        kag_observe(host + i, true);
#ifdef __CUDACC__
        gpu[i] = host[i];
        for (int a = 0; a < 2; a++) {
            float* out = gpu_outputs + (i * 2 + a) * (OBS_SIZE + NUM_ATNS + 2);
            gpu[i].agents[a].observations = out;
            gpu[i].agents[a].actions = out + OBS_SIZE;
            gpu[i].agents[a].rewards = out + OBS_SIZE + NUM_ATNS;
            gpu[i].agents[a].terminals = out + OBS_SIZE + NUM_ATNS + 1;
        }
#endif
    }
#ifdef __CUDACC__
    cudaStream_t stream;
    cudaGraph_t graph;
    cudaGraphExec_t executable;
    assert(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking) == cudaSuccess);
    assert(cudaStreamBeginCapture(stream, cudaStreamCaptureModeGlobal) == cudaSuccess);
    noise_step<<<1, 32, 0, stream>>>(gpu, games);
    assert(cudaStreamEndCapture(stream, &graph) == cudaSuccess);
    assert(cudaGraphInstantiate(&executable, graph, 0) == cudaSuccess);
#endif
    for (int t = 0; t < 1025; t++) {
        for (int i = 0; i < games; i++) kag_step(host + i);
#ifdef __CUDACC__
        if (t % 2) assert(cudaGraphLaunch(executable, stream) == cudaSuccess);
        else noise_step<<<1, 32>>>(gpu, games);
        assert(cudaDeviceSynchronize() == cudaSuccess);
        for (int i = 0; i < games; i++) {
            assert(!memcmp(&host[i].game, &gpu[i].game, sizeof(KGState)));
            assert(!memcmp(&host[i].policy, &gpu[i].policy, sizeof(KagPolicy)));
            assert(host[i].opponent_noise.ticks == gpu[i].opponent_noise.ticks);
            assert(host[i].opponent_noise.rng == gpu[i].opponent_noise.rng);
        }
#endif
        for (int i = 0; i < games; i++) {
            assert(host[i].opponent_noise.ticks == (uint64_t)t + 1);
            for (int a = 0; a < 2; a++) {
                for (int h = 0; h < NUM_ATNS; h++) assert(host[i].agents[a].actions[h] == 0);
            }
            if (i >= 2) assert(host[i].opponent_noise.rng == ((unsigned int)i ^ 0x9e3779b9u));
        }
    }
    // Reset must not rewind the run-wide schedule.
    kag_reset_episode(host);
    assert(host[0].opponent_noise.ticks == 1025);
    assert(kag_opponent_noise_probability(&host[0].opponent_noise) == 0);
    // Evaluation clamps training settings off in the native API as well.
    kag_configure_potential(&ini, "eval");
    assert(puf_ini_get(&ini, "env", "opponent_noise_initial") == 0);
    assert(puf_ini_get(&ini, "env", "opponent_noise_final") == 0);
    free(host);
    free(outputs);
#ifdef __CUDACC__
    assert(cudaGraphExecDestroy(executable) == cudaSuccess);
    assert(cudaGraphDestroy(graph) == cudaSuccess);
    assert(cudaStreamDestroy(stream) == cudaSuccess);
    assert(cudaFree(gpu) == cudaSuccess);
    assert(cudaFree(gpu_outputs) == cudaSuccess);
#endif
    puf_ini_free(&ini);
    puts("Opponent-only legal macro noise, linear step schedule, reset persistence and eval clamp PASS");
#ifdef __CUDACC__
    puts("CPU/GPU and CUDA graph parity PASS");
#endif
}
