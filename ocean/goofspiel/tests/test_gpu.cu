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
    for (int policies = 1; policies <= 5; policies += 4) {
        dict_set(vk, "num_policies", policies);
        Env* gpu = puf_vec_create(rows, ek, obs, actions, rewards, terminals);
        int layout[6] = {};
        puf_gpu_setup(gpu, vk, layout, masks);
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
            gs_reset_state(host + e, 0, 0);
        }
        puf_bind_stream(stream);
        puf_reset(gpu);
        assert(cudaStreamSynchronize(stream) == cudaSuccess);
        for (int t = 0; t < 512; t++) {
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
                if (gs_transition(host + e, NULL, 0)) {
                    gs_reset_state(host + e, 0, 0);
                }
            }
            puf_step(gpu);
            assert(cudaStreamSynchronize(stream) == cudaSuccess);
        }
        Env actual[rows / 2];
        assert(cudaMemcpy(actual, gpu, sizeof(actual), cudaMemcpyDeviceToHost) == cudaSuccess);
        for (int e = 0; e < rows / 2; e++) {
            assert(actual[e].rng == host[e].rng);
            assert(memcmp(&actual[e].state, &host[e].state, sizeof(GSState)) == 0);
            assert(memcmp(&actual[e].log, &host[e].log, sizeof(Log)) == 0);
        }
        puf_close(gpu);
    }
    cudaStreamDestroy(stream);
    cudaFree(obs);
    cudaFree(actions);
    cudaFree(rewards);
    cudaFree(terminals);
    cudaFree(masks);
    puts("PASS GPU masks, policy rows, observations, rewards, resets and RNG");
}
