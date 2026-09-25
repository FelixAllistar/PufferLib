#include <cuda_runtime.h>
#include "../kaggriculture.h"

__global__ void step_reward(Env* env) {
    kag_step(env);
    kag_observe(env, false);
}

void compare_float(const float* a, const float* b, int count) {
    for (int i = 0; i < count; i++) {
        assert(isfinite(a[i]) && isfinite(b[i]));
        if (fabsf(a[i] - b[i]) > 2e-5f * (1 + fabsf(a[i]))) {
            fprintf(stderr, "float %d: %.9g != %.9g\n", i, a[i], b[i]);
            abort();
        }
    }
}

int main(void) {
    Ini ini = {};
    puf_ini_load_env(&ini, "kaggriculture", 0, NULL);
    Dict* cfg = puf_ini_section(&ini, "env", 0);
    dict_set(cfg, "reset_state_prob", 0);
    int sizes[] = ACT_SIZES;
    unsigned rng = 123;
    int transitions = 0, terminals_seen = 0;
    for (int agents = 1; agents <= 2; agents++) {
        for (int shaped = 0; shaped <= 1; shaped++) {
            dict_set(cfg, "num_agents", agents);
            dict_set(cfg, "reward_growth_land", shaped);
            dict_set(cfg, "reward_growth_crop", shaped);
            dict_set(cfg, "reward_growth_animal", shaped);
            dict_set(cfg, "reward_alive_daily", shaped);
            dict_set(cfg, "reward_quality_scale", shaped);
            Env *env;
            float *observations, *actions, *rewards, *terminals;
            assert(cudaMallocManaged(&env, 2 * sizeof(Env)) == cudaSuccess);
            assert(cudaMallocManaged(&observations, 4 * OBS_SIZE * sizeof(float)) == cudaSuccess);
            assert(cudaMallocManaged(&actions, 4 * NUM_ATNS * sizeof(float)) == cudaSuccess);
            assert(cudaMallocManaged(&rewards, 4 * sizeof(float)) == cudaSuccess);
            assert(cudaMallocManaged(&terminals, 4 * sizeof(float)) == cudaSuccess);
            memset(env, 0, 2 * sizeof(Env));
            for (int copy = 0; copy < 2; copy++) {
                puf_init(env + copy, cfg);
                env[copy].game.config.episode_steps = 33;
                for (int a = 0; a < agents; a++) {
                    int row = 2 * copy + a;
                    env[copy].agents[a].observations = observations + row * OBS_SIZE;
                    env[copy].agents[a].actions = actions + row * NUM_ATNS;
                    env[copy].agents[a].rewards = rewards + row;
                    env[copy].agents[a].terminals = terminals + row;
                }
                puf_reset(env + copy);
            }
            cudaStream_t stream;
            cudaGraph_t graph;
            cudaGraphExec_t executable;
            assert(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking) == cudaSuccess);
            assert(cudaStreamBeginCapture(stream, cudaStreamCaptureModeGlobal) == cudaSuccess);
            step_reward<<<1, 1, 0, stream>>>(env + 1);
            assert(cudaStreamEndCapture(stream, &graph) == cudaSuccess);
            assert(cudaGraphInstantiate(&executable, graph, NULL, NULL, 0) == cudaSuccess);
            for (int t = 0; t < 512; t++) {
                for (int a = 0; a < agents; a++) {
                    for (int h = 0; h < NUM_ATNS; h++) {
                        rng = rng * 1664525u + 1013904223u;
                        actions[a * NUM_ATNS + h] = (rng >> 8) % sizes[h];
                        actions[(2 + a) * NUM_ATNS + h] = actions[a * NUM_ATNS + h];
                    }
                }
                puf_step(env);
                if (t % 2) {
                    assert(cudaGraphLaunch(executable, stream) == cudaSuccess);
                } else {
                    step_reward<<<1, 1, 0, stream>>>(env + 1);
                }
                assert(cudaStreamSynchronize(stream) == cudaSuccess);
                assert(!memcmp(&env[0].game, &env[1].game, sizeof(KGState)));
                compare_float(rewards, rewards + 2, agents);
                compare_float(terminals, terminals + 2, agents);
                compare_float(observations, observations + 2 * OBS_SIZE, agents * OBS_SIZE);
                compare_float((float*)&env[0].log, (float*)&env[1].log, sizeof(Log) / sizeof(float));
                transitions++;
                terminals_seen += terminals[0] != 0;
            }
            cudaGraphExecDestroy(executable);
            cudaGraphDestroy(graph);
            cudaStreamDestroy(stream);
            cudaFree(env);
            cudaFree(observations);
            cudaFree(actions);
            cudaFree(rewards);
            cudaFree(terminals);
        }
    }
    assert(transitions == 2048 && terminals_seen == 64);
    printf("CPU/GPU rewards, state, observations, resets: %d steps/%d terminals PASS\n",
        transitions, terminals_seen);
    puf_ini_free(&ini);
}
