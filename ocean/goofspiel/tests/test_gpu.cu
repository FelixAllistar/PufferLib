#include "../goofspiel.cu"

void* managed(size_t bytes) {
    void* ptr = NULL;
    assert(cudaMallocManaged(&ptr, bytes) == cudaSuccess);
    memset(ptr, 0, bytes);
    return ptr;
}

int main(void) {
    Ini ini = {};
    puf_ini_load_file(&ini, "config/default.ini");
    puf_ini_load_file(&ini, "config/goofspiel.ini");
    Dict* ek = puf_ini_section(&ini, "env", 0);
    Dict* vk = puf_ini_section(&ini, "vec", 0);
    const int rows = 64;
    obs_t* obs = (obs_t*)managed(rows * OBS_SIZE);
    float* actions = (float*)managed(rows * sizeof(float));
    float* rewards = (float*)managed(rows * sizeof(float));
    float* terminals = (float*)managed(rows * sizeof(float));
    unsigned char* masks = (unsigned char*)managed(rows * GS_NUM_CARDS);
    cudaStream_t stream;
    assert(cudaStreamCreate(&stream) == cudaSuccess);
    for (int scenario = 0; scenario < 4; scenario++) {
        int policies = scenario == 0 ? 1 : 5;
        int exact = scenario >= 2;
        dict_set(vk, "num_policies", policies);
        Env* gpu = puf_vec_create(rows, ek, obs, actions, rewards, terminals);
        int layout[6] = {};
        puf_gpu_setup(gpu, vk, layout, masks);
        gs_exact_count = 0;
        gs_exact_history = 3;
        gs_exact_banks = 2;
        if (exact) {
            gs_cuda_best_response("uniform", &ini, gs_exact_tables, NULL, NULL);
            gs_exact_count = 1;
            gs_gpu_exact_upload();
        }
        assert(layout[policies] == rows);
        assert(layout[1] == (policies == 1 ? rows : 48));
        Env host[rows / 2];
        assert(cudaMemcpy(host, gpu, sizeof(host), cudaMemcpyDeviceToHost) == cudaSuccess);
        obs_t reference_obs[rows * OBS_SIZE] = {};
        float reference_rewards[rows] = {}, reference_terminals[rows] = {};
        unsigned char reference_masks[rows * GS_NUM_CARDS] = {};
        int seen[rows] = {};
        for (int e = 0; e < rows / 2; e++) {
            for (int p = 0; p < 2; p++) {
                Agent* a = host[e].agents + p;
                int row = a->actions - actions;
                assert(row >= layout[a->policy] && row < layout[a->policy + 1]);
                assert(seen[row]++ == 0);
                a->observations = reference_obs + row * OBS_SIZE;
                a->rewards = reference_rewards + row;
                a->terminals = reference_terminals + row;
                a->action_mask = reference_masks + row * GS_NUM_CARDS;
            }
            gs_reset_state(host + e, gs_exact_count, gs_exact_current_prob);
        }
        puf_bind_stream(stream);
        puf_reset(gpu);
        assert(cudaStreamSynchronize(stream) == cudaSuccess);
        cudaGraph_t graph = NULL;
        cudaGraphExec_t executable = NULL;
        if (scenario == 3) {
            assert(cudaStreamBeginCapture(stream, cudaStreamCaptureModeGlobal) == cudaSuccess);
            puf_step(gpu);
            assert(cudaStreamEndCapture(stream, &graph) == cudaSuccess);
            assert(cudaGraphInstantiate(&executable, graph, 0) == cudaSuccess);
        }
        unsigned int selected_tables = 0;
        int overridden_actions = 0;
        for (int t = 0; t < 512; t++) {
            if (exact && t > 0 && t % 128 == 0) {
                uint8_t* allocation = gs_device_actions;
                int slot = gs_exact_count < 3 ? gs_exact_count++ : 0;
                gs_cuda_best_response("uniform", &ini, gs_exact_tables + slot, NULL, NULL);
                // Change root decisions while retaining valid history-indexed responses.
                GSExactTable* table = gs_exact_tables + slot;
                for (uint64_t n = 0; n < table->counts[0]; n++) {
                    table->actions[0][n] = (table->actions[0][n] + 1) % GS_NUM_CARDS;
                }
                gs_gpu_exact_upload();
                assert(gs_device_actions == allocation);
            }
            assert(memcmp(obs, reference_obs, sizeof(reference_obs)) == 0);
            assert(memcmp(masks, reference_masks, sizeof(reference_masks)) == 0);
            assert(memcmp(rewards, reference_rewards, sizeof(reference_rewards)) == 0);
            assert(memcmp(terminals, reference_terminals, sizeof(reference_terminals)) == 0);
            for (int row = 0; row < rows; row++) {
                int a = (row + t) % GS_NUM_CARDS;
                while (!masks[row * GS_NUM_CARDS + a]) {
                    a = (a + 1) % GS_NUM_CARDS;
                }
                actions[row] = a;
            }
            for (int e = 0; e < rows / 2; e++) {
                Env* env = host + e;
                GSExactTable* table = gs_exact_tables + env->exact_table;
                int response = exact && env->tag > 0 && env->tag <= gs_exact_banks
                    && env->exact_depth < table->decisions;
                if (response) {
                    selected_tables |= 1u << env->exact_table;
                    overridden_actions += table->actions[env->exact_depth][env->exact_node]
                        != env->agents[1].actions[0];
                }
                if (gs_transition(env, response ? table->actions[env->exact_depth] : NULL,
                        response ? table->decisions : 0)) {
                    gs_reset_state(env, gs_exact_count, gs_exact_current_prob);
                }
            }
            if (executable) {
                assert(cudaGraphLaunch(executable, stream) == cudaSuccess);
            } else {
                puf_step(gpu);
            }
            assert(cudaStreamSynchronize(stream) == cudaSuccess);
        }
        Env actual[rows / 2];
        assert(cudaMemcpy(actual, gpu, sizeof(actual), cudaMemcpyDeviceToHost) == cudaSuccess);
        for (int e = 0; e < rows / 2; e++) {
            assert(actual[e].rng == host[e].rng);
            assert(memcmp(&actual[e].state, &host[e].state, sizeof(GSState)) == 0);
            assert(memcmp(&actual[e].log, &host[e].log, sizeof(Log)) == 0);
            assert(actual[e].exact_node == host[e].exact_node);
            assert(actual[e].exact_depth == host[e].exact_depth);
            assert(actual[e].exact_table == host[e].exact_table);
        }
        assert(!exact || (selected_tables == 7 && overridden_actions > 0));
        if (executable) {
            cudaGraphExecDestroy(executable);
            cudaGraphDestroy(graph);
        }
        puf_close(gpu);
        for (int i = 0; i < gs_exact_count; i++) {
            gs_exact_table_clear(gs_exact_tables + i);
        }
        gs_exact_count = 0;
    }
    cudaStreamDestroy(stream);
    cudaFree(obs);
    cudaFree(actions);
    cudaFree(rewards);
    cudaFree(terminals);
    cudaFree(masks);
    puts("PASS GPU masks, policy rows, observations, rewards, resets, RNG and exact refresh graphs");
}
