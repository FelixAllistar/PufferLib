// Opt-in GPU regression against the actual CPU-env trainer's policy layout.
#include "../src/pufferl.cu"

void pattern(Prec tensor, int T, int B, int C) {
    precision_t* host = (precision_t*)malloc(T * B * C * sizeof(precision_t));
    for (int t = 0; t < T; t++) {
        for (int b = 0; b < B; b++) {
            for (int c = 0; c < C; c++) {
                host[(t * B + b) * C + c] = from_float(t * 32 + b);
            }
        }
    }
    cudaMemcpy(tensor.data, host, T * B * C * sizeof(precision_t), cudaMemcpyHostToDevice);
    free(host);
}

void check_rows(Prec tensor, int T, int B, int C, int primary, int stride) {
    precision_t* host = (precision_t*)malloc(T * B * C * sizeof(precision_t));
    cudaMemcpy(host, tensor.data, T * B * C * sizeof(precision_t), cudaMemcpyDeviceToHost);
    for (int b = 0; b < B; b++) {
        int source = b / primary * stride + b % primary;
        for (int t = 0; t < T; t++) {
            for (int c = 0; c < C; c++) {
                assert(to_float(host[(b * T + t) * C + c]) == t * 32 + source);
            }
        }
    }
    free(host);
}

int main(int argc, char** argv) {
    assert(argc == 5);
    int policies = atoi(argv[1]), buffers = atoi(argv[2]);
    int async_mode = atoi(argv[3]), graphs = atoi(argv[4]);
    Ini ini = {};
    puf_ini_load_env(&ini, "chain_reaction", 0, NULL);
    puf_ini_put(&ini, "vec.total_agents", "32");
    dict_set(puf_ini_section(&ini, "vec", 0), "num_buffers", buffers);
    dict_set(puf_ini_section(&ini, "vec", 0), "num_policies", policies);
    puf_ini_put(&ini, "vec.num_threads", "2");
    puf_ini_put(&ini, "vec.hist_policy_percent", "0.5");
    puf_ini_put(&ini, "vec.hist_policy_hidden_size", "32");
    puf_ini_put(&ini, "vec.hist_policy_num_layers", "2");
    puf_ini_put(&ini, "policy.hidden_size", "32");
    puf_ini_put(&ini, "policy.num_layers", "2");
    puf_ini_put(&ini, "env.num_agents", "2");
    puf_ini_put(&ini, "train.horizon", "8");
    puf_ini_put(&ini, "train.minibatch_size", "64");
    puf_ini_put(&ini, "train.reward_clip", "0");
    puf_ini_put(&ini, "base.reset_every_horizon", "0");
    dict_set(puf_ini_section(&ini, "base", 0), "async", async_mode);
    TrainContext context = {.rank = 0, .world_size = 1, .gpu_id = 0};
    PuffeRL* p = create_pufferl(&ini, &context);
    int primary = p->vec->policy_layout[1], stride = 32 / buffers;
    int B = primary * buffers, T = 8;
    printf("train rows: expected=%d actual=%ld policies=%d buffers=%d\n", B,
        p->train_rollouts.observations.shape[0], policies, buffers);
    fflush(stdout);
    assert(p->train_rollouts.observations.shape[0] == B);
    assert(p->train_state.shape[1] == B && p->rollouts.initial_states.shape[2] == B);
    int slot = async_mode;
    p->write_slot = slot;
    precision_t initial[2 * 32 * 32];
    for (int buf = 0; buf < buffers; buf++) {
        Prec state = p->policies[0].buffer_states[buf];
        for (int i = 0; i < 2 * primary * 32; i++) {
            initial[i] = from_float((i / (primary * 32)) * 32 + buf * primary + (i / 32) % primary);
        }
        cudaMemcpy(
            state.data, initial, 2 * primary * 32 * sizeof(precision_t), cudaMemcpyHostToDevice);
        pufferl_forward_step(p, buf, 0, p->default_stream);
    }
    assert(cudaDeviceSynchronize() == cudaSuccess);
    RolloutBuf source = rollout_time_view(&p->rollouts, slot * T, T);
    pattern(source.observations, T, 32, OBS_SIZE);
    pattern(source.values, T, 32, 1);
    pattern(source.logprobs, T, 32, 1);
    pattern(source.rewards, T, 32, 1);
    pattern(source.terminals, T, 32, 1);
    pattern(source.action_mask, T, 32, p->vec->mask_size);
    float actions[8 * 32 * NUM_ATNS];
    for (int i = 0; i < 8 * 32 * NUM_ATNS; i++) {
        actions[i] = 100000 + i / NUM_ATNS;
    }
    cudaMemcpy(source.actions.data, actions, sizeof(actions), cudaMemcpyHostToDevice);
    p->hypers.replay_ratio = 0; // isolate gathering and carry, no model update
    cudaStream_t stream = p->train_stream;
    assert(cudaDeviceSynchronize() == cudaSuccess);
    if (graphs) {
        assert(cudaStreamBeginCapture(stream, cudaStreamCaptureModeGlobal) == cudaSuccess);
    }
    train_epoch_gpu(p, source, slot, stream);
    if (graphs) {
        cudaGraph_t graph;
        cudaGraphExec_t executable;
        assert(cudaStreamEndCapture(stream, &graph) == cudaSuccess);
        assert(cudaGraphInstantiate(&executable, graph, NULL, NULL, 0) == cudaSuccess);
        assert(cudaGraphLaunch(executable, stream) == cudaSuccess);
    }
    assert(cudaDeviceSynchronize() == cudaSuccess);
    RolloutBuf target = p->train_rollouts;
    check_rows(target.observations, T, B, OBS_SIZE, primary, stride);
    check_rows(target.values, T, B, 1, primary, stride);
    check_rows(target.logprobs, T, B, 1, primary, stride);
    check_rows(target.rewards, T, B, 1, primary, stride);
    check_rows(target.terminals, T, B, 1, primary, stride);
    check_rows(target.action_mask, T, B, p->vec->mask_size, primary, stride);
    cudaMemcpy(
        actions, target.actions.data, B * T * NUM_ATNS * sizeof(float), cudaMemcpyDeviceToHost);
    for (int b = 0; b < B; b++) {
        int physical = b / primary * stride + b % primary;
        for (int t = 0; t < T; t++) {
            for (int c = 0; c < NUM_ATNS; c++) {
                assert(actions[(b * T + t) * NUM_ATNS + c] == 100000 + t * 32 + physical);
            }
        }
    }
    precision_t carry[2 * 32 * 32];
    cudaMemcpy(
        carry, p->train_state.data, 2 * B * 32 * sizeof(precision_t), cudaMemcpyDeviceToHost);
    for (int i = 0; i < 2 * B * 32; i++) {
        assert(to_float(carry[i]) == (i / (B * 32)) * 32 + (i / 32) % B);
    }
    close_pufferl(p);
    puf_ini_free(&ini);
    printf("policy row selection PASS\n");
}
