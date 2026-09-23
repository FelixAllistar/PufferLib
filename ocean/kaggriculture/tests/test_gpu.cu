// Build against the actual trainer's sampler and PPO kernels, without its CLI main.
#include "../../../src/pufferl.cu"

void* managed(size_t bytes) {
    void* p = NULL;
    assert(cudaMallocManaged(&p, bytes) == cudaSuccess);
    memset(p, 0, bytes);
    return p;
}

void sync_test(void) {
    cudaError_t error = cudaDeviceSynchronize();
    if (error != cudaSuccess) {
        fprintf(stderr, "CUDA: %s\n", cudaGetErrorString(error));
        abort();
    }
}

__global__ void advance_rng(
    curandStatePhilox4_32_10_t* rng, const int* draws, float* uniforms, int rows) {
    int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row < rows) {
        for (int i = 0; i < draws[row]; i++) {
            uniforms[row * NUM_ATNS + i] = curand_uniform(rng + row);
        }
    }
}

Dict settings(int agents, int seat, int bot) {
    Dict d = {0};
    dict_set(&d, "num_agents", agents);
    dict_set(&d, "learner_seat", seat);
    dict_set(&d, "bot_policy", bot);
    dict_set(&d, "market_slots", 10);
    dict_set(&d, "max_hands", 16);
    dict_set(&d, "land_buy_min_days", 0);
    dict_set(&d, "reward_money", 4.71806717);
    return d;
}

void close_float(float actual, float expected, float tolerance = 2e-5f) {
    if (!isfinite(actual) || !isfinite(expected) ||
        fabsf(actual - expected) > tolerance * (1 + fabsf(expected))) {
        fprintf(stderr, "float mismatch: %.9g != %.9g\n", actual, expected);
        abort();
    }
}

// Same already-qualified controller/rules on the CPU; independently reconstruct
// each selected prefix and its ordinary conditional categorical probability.
void sampler_test(int agents, int seat, int split, int graphs) {
    int games = 24, rows = games * agents, cols = KAG_ALL_LOGITS + 1;
    Env* host = (Env*)calloc(games, sizeof(Env));
    Env* device = (Env*)managed(games * sizeof(Env));
    Dict kwargs = settings(agents, seat, 1);
    for (int i = 0; i < games; i++) {
        host[i].rng = i;
        puf_init(host + i, &kwargs);
        host[i].policy.market_slots = 1 + i % 10;
        host[i].policy.max_hands = 1 + i % 16;
        for (int p = 0; p < 2; p++) {
            KGPlayer* farm = &host[i].game.players[p];
            farm->money = i % 4 == 0 ? 0 : 50000;
            farm->hand_count = i % 5;
            farm->unit_count = farm->hand_count + 1;
            for (int u = 0; u < farm->unit_count; u++) {
                farm->units[u].x = u;
                farm->units[u].y = 0;
            }
            for (int crop = 0; crop < KG_NUM_CROPS; crop++) {
                farm->seeds[crop] = 1 + (i + crop) % 4;
                farm->shed[crop] = (i + 2 * crop) % 12;
            }
            kg_new_plant(farm, kg_tile_index(2, 2), KG_STRAWBERRY, 0, 24);
            kg_set_player_tile(farm, kg_tile_index(3, 3), KG_TILE_PASTURE);
            kg_new_animal(farm, kg_tile_index(3, 3), KG_COW, 0);
            kg_sync_public_positions(farm);
        }
        kag_policy_reset(&host[i].policy, &host[i].game, 0);
    }
    memcpy(device, host, games * sizeof(Env));
    precision_t* logits = (precision_t*)managed(rows * cols * sizeof(precision_t));
    precision_t* masks = (precision_t*)managed(rows * KAG_ALL_LOGITS * sizeof(precision_t));
    precision_t* lp = (precision_t*)managed(rows * sizeof(precision_t));
    precision_t* value = (precision_t*)managed(rows * sizeof(precision_t));
    float* actions = (float*)managed(rows * NUM_ATNS * sizeof(float));
    float* dispatched = (float*)managed(rows * NUM_ATNS * sizeof(float));
    int* sizes = (int*)managed(NUM_ATNS * sizeof(int));
    const int action_sizes[] = ACT_SIZES;
    memcpy(sizes, action_sizes, sizeof(action_sizes));
    for (int r = 0; r < rows; r++) {
        for (int j = 0; j < cols; j++) {
            logits[r * cols + j] = from_float(((17 * j + 23 * r) % 83 - 41) / 8.0f);
        }
        // Exercise early STOP, quantity-free HIRE/LAND, and exact product buys.
        for (int slot = 0; slot < 10; slot++) {
            int off = KAG_TASK_LOGITS + slot * KG_POLICY_MARKET_SLOT_MASK_SIZE;
            logits[r * cols + off + (r % 4 != 0)] = from_float(20.0f);
            int command = r % 4 == 1 ? KG_M_HIRE : (r % 4 == 2 ? KG_M_LAND : 10);
            logits[r * cols + off + 2 + command] = from_float(20.0f);
        }
    }
    curandStatePhilox4_32_10_t* rng = (curandStatePhilox4_32_10_t*)managed(rows * sizeof(*rng));
    rng_init<<<1, 256>>>(rng, 123, rows);
    sync_test();
    curandStatePhilox4_32_10_t* expected_rng =
        (curandStatePhilox4_32_10_t*)managed(rows * sizeof(*rng));
    memcpy(expected_rng, rng, rows * sizeof(*rng));
    int* draws = (int*)managed(rows * sizeof(int));
    float* uniforms = (float*)managed(rows * NUM_ATNS * sizeof(float));
    cudaStream_t stream;
    assert(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking) == cudaSuccess);
    if (graphs) {
        assert(cudaStreamBeginCapture(stream, cudaStreamCaptureModeGlobal) == cudaSuccess);
    }
    for (int start = 0; start < rows;) {
        int count = split && start == 0 ? 5 : rows - start;
        Prec dec = {.data = logits + start * cols, .shape = {count, cols}};
        sample_logits<<<1, 256, 0, stream>>>(dec, {}, sizes, actions + start * NUM_ATNS,
            dispatched + start * NUM_ATNS, lp + start, value + start, rng + start,
            masks + start * KAG_ALL_LOGITS, KAG_ALL_LOGITS, device, start);
        start += count;
    }
    cudaGraphExec_t graph_exec = NULL;
    if (graphs) {
        cudaGraph_t graph;
        assert(cudaStreamEndCapture(stream, &graph) == cudaSuccess);
        assert(cudaGraphInstantiate(&graph_exec, graph, 0) == cudaSuccess);
        assert(cudaGraphDestroy(graph) == cudaSuccess);
        assert(cudaGraphLaunch(graph_exec, stream) == cudaSuccess);
    }
    sync_test();
    assert(!memcmp(actions, dispatched, rows * NUM_ATNS * sizeof(float)));
    int inactive_count = 0, quantity_count = 0, hire_count = 0, stop_count = 0;
    for (int r = 0; r < rows; r++) {
        Env* env = host + r / agents;
        int player = agents == 2 ? r % 2 : seat;
        unsigned char mask[KAG_ALL_LOGITS];
        kag_write_mask(&env->policy, &env->game, player, mask);
        KagActionMaskState prefix;
        kag_action_mask_begin(&prefix, &env->game, &env->policy, player);
        int offset = 0;
        double expected_lp = 0;
        for (int h = 0; h < NUM_ATNS; h++) {
            kag_action_mask_before(&prefix, h, mask);
            int active = kag_action_head_active(prefix.choices, h);
            int action = actions[r * NUM_ATNS + h];
            assert(action >= 0 && action < sizes[h]);
            assert(actions[r * NUM_ATNS + h] == action);
            assert(active || action == 0);
            assert(!active || mask[offset + action]);
            double sum = 0;
            for (int a = 0; a < sizes[h]; a++) {
                int legal = active ? mask[offset + a] : a == 0;
                assert(to_float(masks[r * KAG_ALL_LOGITS + offset + a]) == legal);
                if (active && legal) {
                    sum += exp((double)to_float(logits[r * cols + offset + a]));
                }
            }
            if (active) {
                expected_lp += to_float(logits[r * cols + offset + action]) - log(sum);
                draws[r]++;
                kag_action_mask_commit(&prefix, h, action);
                if (h >= 17) {
                    int node = (h - 17) % 3;
                    stop_count += node == 0 && action == 0;
                    hire_count += node == 1 && action == KG_M_HIRE;
                    quantity_count += node == 2;
                }
            } else {
                inactive_count++;
            }
            offset += sizes[h];
        }
        close_float(to_float(lp[r]), to_float(from_float((float)expected_lp)), 3e-5f);
        assert(to_float(value[r]) == to_float(logits[r * cols + cols - 1]));
    }
    assert(inactive_count > 0 && quantity_count > 0 && hire_count > 0 && stop_count > 0);
    advance_rng<<<1, 256>>>(expected_rng, draws, uniforms, rows);
    sync_test();
    // Compare RNG contents including its Philox subsequence/counter, not
    // just a matching action. Ignored market heads consume no draws.
    assert(!memcmp(rng, expected_rng, rows * sizeof(*rng)));
    for (int r = 0; r < rows; r++) {
        int offset = 0, draw = 0;
        for (int h = 0; h < NUM_ATNS; h++) {
            if (kag_action_head_active(actions + r * NUM_ATNS, h)) {
                double sum = 0, lower = 0, chosen = 0;
                int action = actions[r * NUM_ATNS + h];
                for (int j = 0; j < sizes[h]; j++) {
                    if (to_float(masks[r * KAG_ALL_LOGITS + offset + j])) {
                        double mass = exp((double)to_float(logits[r * cols + offset + j]));
                        sum += mass;
                        lower += j < action ? mass : 0;
                        chosen += j == action ? mass : 0;
                    }
                }
                double uniform = uniforms[r * NUM_ATNS + draw++];
                assert(uniform >= lower / sum - 2e-6);
                assert(uniform <= (lower + chosen) / sum + 2e-6);
            }
            offset += sizes[h];
        }
        assert(draw == draws[r]);
    }

    // Use upstream's actual importance/value cache and PPO gradient kernel.
    precision_t* imp = (precision_t*)managed(rows * sizeof(precision_t));
    precision_t* v = (precision_t*)managed(rows * sizeof(precision_t));
    precision_t* adv = (precision_t*)managed(rows * sizeof(precision_t));
    precision_t* ret = (precision_t*)managed(rows * sizeof(precision_t));
    float* logps = (float*)managed(rows * KAG_ALL_LOGITS * sizeof(float));
    float* new_lp = (float*)managed(rows * sizeof(float));
    float* losses = (float*)managed(LOSS_N * sizeof(float));
    float* ent = (float*)managed(sizeof(float));
    *ent = 0.01f;
    for (int r = 0; r < rows; r++) {
        adv[r] = from_float(r % 2 ? 0.7f : -0.7f);
        ret[r] = from_float(1.25f);
    }
    Prec train_dec = {.data = logits, .shape = {rows, 1, cols}};
    cache_imp_and_v<<<1, 256>>>(train_dec, actions, lp, masks, {}, sizes, imp, v, logps, new_lp);
    sync_test();
    double expected_entropy = 0;
    for (int r = 0; r < rows; r++) {
        assert(to_float(from_float(new_lp[r])) == to_float(lp[r]));
        double entropy = 0;
        int offset = 0;
        for (int h = 0; h < NUM_ATNS; h++) {
            int active = kag_action_head_active(actions + r * NUM_ATNS, h);
            for (int a = 0; a < sizes[h]; a++) {
                float l = logps[r * KAG_ALL_LOGITS + offset + a];
                if (!active) {
                    assert(a ? expf(l) == 0 : l == 0);
                }
                entropy -= exp(l) * l;
            }
            offset += sizes[h];
        }
        assert(isfinite(entropy));
        expected_entropy += entropy;
    }
    PPOKernelArgs a = {
        .grad_logits = logps,
        .grad_values_pred = new_lp,
        .logits = logits,
        .values_pred = logits + cols - 1,
        .act_sizes = sizes,
        .action_mask = masks,
        .num_atns = NUM_ATNS,
        .clip_coef = 0.2f,
        .vf_clip_coef = 0.2f,
        .vf_coef = 2,
        .ent_coef = ent,
        .T_seq = 1,
        .A_total = KAG_ALL_LOGITS,
        .N = rows,
    };
    PPOGraphArgs g = {.imp = imp,
        .actions = actions,
        .old_logprobs = lp,
        .advantages = adv,
        .values = v,
        .returns = ret};
    ppo_loss_compute<<<1, PPO_THREADS>>>(losses, a, g);
    sync_test();
    close_float(losses[LOSS_ENT], expected_entropy / rows);
    int nonzero = 0;
    for (int r = 0; r < rows; r++) {
        int offset = 0;
        for (int h = 0; h < NUM_ATNS; h++) {
            int active = kag_action_head_active(actions + r * NUM_ATNS, h);
            for (int j = 0; j < sizes[h]; j++) {
                float grad = logps[r * KAG_ALL_LOGITS + offset + j];
                assert(isfinite(grad));
                if (!active || to_float(masks[r * KAG_ALL_LOGITS + offset + j]) == 0) {
                    assert(grad == 0);
                }
                nonzero += grad != 0;
            }
            offset += sizes[h];
        }
    }
    assert(nonzero > 0);
    for (int i = 0; i < LOSS_N; i++) {
        assert(isfinite(losses[i]));
    }
    printf("sampler/PPO PASS: rows=%d agents=%d seat=%d split=%d graphs=%d "
           "inactive=%d quantity=%d hire=%d stop=%d\n",
        rows, agents, seat, split, graphs, inactive_count, quantity_count, hire_count, stop_count);
}

void adapter_test(int agents, int seat, int bot, int graphs) {
    int games = 3, rows = games * agents;
    Dict kwargs = settings(agents, seat, bot);
    obs_t* observations = (obs_t*)managed(rows * OBS_SIZE * sizeof(obs_t));
    float* actions = (float*)managed(rows * NUM_ATNS * sizeof(float));
    float* rewards = (float*)managed(rows * sizeof(float));
    float* terminals = (float*)managed(rows * sizeof(float));
    Env* envs = puf_vec_create(rows, &kwargs, observations, actions, rewards, terminals);
    Env* host = (Env*)calloc(games, sizeof(Env));
    Env* actual = (Env*)calloc(games, sizeof(Env));
    cudaStream_t stream;
    assert(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking) == cudaSuccess);
    puf_bind_stream(stream);
    puf_reset(envs);
    sync_test();
    assert(cudaMemcpy(host, envs, games * sizeof(Env), cudaMemcpyDeviceToHost) == cudaSuccess);
    // A reset must actually reset I/O, not leave garbage or a terminal flag.
    for (int r = 0; r < rows; r++) {
        assert(rewards[r] == 0 && terminals[r] == 0);
    }
    for (int i = 0; i < games; i++) {
        assert(host[i].game.step == 0 && host[i].log.n == 0);
        assert(host[i].game.config.seed == 1664525u * i + 1013904223u);
    }
    // Start one game at a late, cash-rich state to test unclipped environment
    // terminal rewards > 1, auto-reset timing and the next observation.
    host[0].game.step = 717;
    host[0].game.day = 29;
    host[0].game.hour = 21;
    for (int p = 0; p < 2; p++) {
        host[0].game.players[p].money = 100000;
    }
    assert(cudaMemcpy(envs, host, games * sizeof(Env), cudaMemcpyHostToDevice) == cudaSuccess);
    cudaGraphExec_t graph_exec = NULL;
    if (graphs) {
        cudaGraph_t graph;
        assert(cudaStreamBeginCapture(stream, cudaStreamCaptureModeGlobal) == cudaSuccess);
        puf_step(envs);
        assert(cudaStreamEndCapture(stream, &graph) == cudaSuccess);
        assert(cudaGraphInstantiate(&graph_exec, graph, 0) == cudaSuccess);
        assert(cudaGraphDestroy(graph) == cudaSuccess);
    }
    float logits[KAG_ALL_LOGITS], observation[OBS_SIZE];
    unsigned char mask[KAG_ALL_LOGITS];
    unsigned int sampling_rng = 123;
    int finished = 0, above_one = 0;
    for (int t = 0; t < 1440; t++) {
        for (int j = 0; j < KAG_ALL_LOGITS; j++) {
            logits[j] = ((t * 13 + j * 19) % 37 - 18) / 8.0f;
        }
        for (int r = 0; r < rows; r++) {
            Env* env = host + r / agents;
            int player = agents == 2 ? r % 2 : seat;
            kag_sample_cpu_logits(&env->policy, &env->game, player, logits, 0, &sampling_rng,
                actions + r * NUM_ATNS, mask);
        }
        if (graphs) {
            assert(cudaGraphLaunch(graph_exec, stream) == cudaSuccess);
        } else {
            puf_step(envs);
        }
        sync_test();
        assert(
            cudaMemcpy(actual, envs, games * sizeof(Env), cudaMemcpyDeviceToHost) == cudaSuccess);
        for (int i = 0; i < games; i++) {
            Env* env = host + i;
            KGAction pair[2] = {0};
            for (int a = 0; a < agents; a++) {
                int player = agents == 2 ? a : seat;
                kag_decode_multi_action(&pair[player], actions + (i * agents + a) * NUM_ATNS,
                    &env->game, player, &env->policy);
            }
            if (agents == 1 && bot) {
                kg_rule_action(&env->game, 1 - seat, &pair[1 - seat]);
            }
            kg_step(&env->game, pair);
            kag_policy_step(&env->policy, &env->game);
            for (int a = 0; a < agents; a++) {
                int row = i * agents + a, player = agents == 2 ? a : seat;
                assert(terminals[row] == env->game.done);
                float expected_reward = env->game.done
                                            ? env->reward_money *
                                                  (env->game.players[player].money -
                                                      env->policy.history[player].start_cash) /
                                                  env->game.config.starting_money
                                            : 0;
                close_float(rewards[row], expected_reward);
                above_one += rewards[row] > 1;
                if (env->game.done) {
                    env->log.n++;
                    env->log.score += env->game.players[player].money;
                    env->log.episode_return += expected_reward;
                    env->log.episode_length += env->game.step;
                    finished++;
                }
            }
            if (env->game.done) {
                kag_reset_episode(env);
            }
            assert(!memcmp(&actual[i].game, &env->game, sizeof(KGState)));
            assert(actual[i].rng == env->rng);
            close_float(actual[i].log.n, env->log.n);
            close_float(actual[i].log.score, env->log.score);
            close_float(actual[i].log.episode_return, env->log.episode_return);
            close_float(actual[i].log.episode_length, env->log.episode_length);
            for (int a = 0; a < agents; a++) {
                int row = i * agents + a, player = agents == 2 ? a : seat;
                kag_write_observation(&env->policy, &env->game, player, observation);
                for (int j = 0; j < OBS_SIZE; j++) {
                    close_float(observations[row * OBS_SIZE + j], observation[j]);
                }
            }
        }
    }
    assert(finished == 7 * agents && above_one > 0);
    puf_reset(envs);
    sync_test();
    assert(cudaMemcpy(actual, envs, games * sizeof(Env), cudaMemcpyDeviceToHost) == cudaSuccess);
    for (int i = 0; i < games; i++) {
        assert(actual[i].log.n == 0 && actual[i].game.step == 0);
    }
    for (int r = 0; r < rows; r++) {
        assert(rewards[r] == 0 && terminals[r] == 0);
    }
    puf_close(envs);
    printf("adapter PASS: agents=%d seat=%d bot=%d graphs=%d transitions=%d episodes=%d\n", agents,
        seat, bot, graphs, games * 1440, finished);
}

void dump_tensor(const char* directory, const char* name, Prec tensor) {
    long n = numel(tensor.shape);
    precision_t* host = (precision_t*)malloc(n * sizeof(precision_t));
    float* values = (float*)malloc(n * sizeof(float));
    assert(cudaMemcpy(host, tensor.data, n * sizeof(precision_t), cudaMemcpyDeviceToHost) ==
           cudaSuccess);
    for (long i = 0; i < n; i++) {
        values[i] = to_float(host[i]);
    }
    char path[2048];
    snprintf(path, sizeof(path), "%s/%s.f32", directory, name);
    FILE* file = fopen(path, "wb");
    assert(file && fwrite(values, sizeof(float), n, file) == n);
    fclose(file);
    free(values);
    free(host);
}

// Exercise the custom vtables independently of the unchanged recurrent core.
// Python reconstructs packing, matrix products and every gradient independently.
void network_test(int hidden, int graphs, int loss, const char* directory) {
    int rows = 5;
    cublas_init_handle();
    Arch arch = build_arch(OBS_SIZE, hidden, 1, KAG_ALL_LOGITS, false, 1);
    void* enc = arch.encoder.create_weights(&arch.encoder);
    void* dec = arch.decoder.create_weights(&arch.decoder);
    assert(!((DecoderWeights*)dec)->continuous);
    assert(!((DecoderWeights*)dec)->logstd.data);
    assert(((DecoderWeights*)dec)->hidden_dim == hidden);
    assert(((DecoderWeights*)dec)->output_dim == KAG_ALL_LOGITS);
    Allocator params = {}, grads = {}, acts = {};
    arch.encoder.reg_params(enc, &params);
    arch.decoder.reg_params(dec, &params);
    KagEncoderActs ea = {};
    KagDecoderActs da = {};
    arch.encoder.reg_train(enc, &ea, &acts, &grads, rows);
    arch.decoder.reg_train(dec, &da, &acts, &grads, rows);
    Prec obs = {.shape = {rows, OBS_SIZE}};
    Float gl = {.shape = {rows, KAG_ALL_LOGITS}}, gv = {.shape = {rows, 1}};
    alloc_register(&acts, &obs);
    alloc_register(&acts, &gl);
    alloc_register(&acts, &gv);
    alloc_create(&params);
    alloc_create(&grads);
    alloc_create(&acts);
    assert(params.total_bytes == grads.total_bytes);
    assert(params.total_bytes == params.total_elems * sizeof(precision_t));
    cudaStream_t stream;
    assert(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking) == cudaSuccess);
    ulong seed = 73;
    arch.encoder.init_weights(enc, &seed, stream);
    arch.decoder.init_weights(dec, &seed, stream);
    precision_t host_obs[5 * OBS_SIZE];
    float host_gl[5 * KAG_ALL_LOGITS], host_gv[5];
    for (int i = 0; i < rows * OBS_SIZE; i++) {
        host_obs[i] = from_float(((i * 37) % 127 - 63) / 32.0f);
    }
    for (int i = 0; i < rows * KAG_ALL_LOGITS; i++) {
        host_gl[i] = loss == 2 ? 0 : ((i * 17) % 31 - 15) / 128.0f;
    }
    for (int i = 0; i < rows; i++) {
        host_gv[i] = loss == 1 ? 0 : (i - 2) / 16.0f;
    }
    cudaMemcpy(obs.data, host_obs, sizeof(host_obs), cudaMemcpyHostToDevice);
    cudaMemcpy(gl.data, host_gl, sizeof(host_gl), cudaMemcpyHostToDevice);
    cudaMemcpy(gv.data, host_gv, sizeof(host_gv), cudaMemcpyHostToDevice);
    sync_test();
    if (graphs) {
        assert(cudaStreamBeginCapture(stream, cudaStreamCaptureModeGlobal) == cudaSuccess);
    }
    Prec encoded = arch.encoder.forward(enc, &ea, obs, stream);
    Prec decoded = arch.decoder.forward(dec, &da, encoded, stream);
    Prec upstream = arch.decoder.backward(dec, &da, gl, {}, gv, stream);
    arch.encoder.backward(enc, &ea, upstream, stream);
    if (graphs) {
        cudaGraph_t graph;
        cudaGraphExec_t executable;
        assert(cudaStreamEndCapture(stream, &graph) == cudaSuccess);
        assert(cudaGraphInstantiate(&executable, graph, NULL, NULL, 0) == cudaSuccess);
        assert(cudaGraphLaunch(executable, stream) == cudaSuccess);
        sync_test();
        cudaGraphExecDestroy(executable);
        cudaGraphDestroy(graph);
    }
    sync_test();
    dump_tensor(directory, "weights", {(precision_t*)params.mem, {params.total_elems}});
    dump_tensor(directory, "gradients", {(precision_t*)grads.mem, {grads.total_elems}});
    dump_tensor(directory, "observations", obs);
    dump_tensor(directory, "encoded", encoded);
    dump_tensor(directory, "decoded", decoded);
    dump_tensor(directory, "upstream", upstream);
    printf("network PASS: precision_bytes=%zu hidden=%d graphs=%d loss=%d\n", sizeof(precision_t),
        hidden, graphs, loss);
    cudaFree(params.mem);
    cudaFree(grads.mem);
    cudaFree(acts.mem);
    free(params.regs);
    free(grads.regs);
    free(acts.regs);
    free(enc);
    free(dec);
    cudaStreamDestroy(stream);
}

void checkpoint_test(const char* checkpoint, int graphs, const char* data, const char* directory) {
    int steps = 32, hidden = 256, layers = 2;
    cublas_init_handle();
    Arch arch = build_arch(OBS_SIZE, hidden, layers, KAG_ALL_LOGITS, false, 1);
    Allocator params = {}, acts = {};
    Weights weights = weights_create(&arch, &params);
    Activations activation = arch_reg_rollout(&arch, weights, &acts, 1);
    Prec observations = {.shape = {steps, OBS_SIZE}};
    Prec state = {.shape = {layers, 1, hidden}};
    Prec outputs = {.shape = {steps, KAG_ALL_LOGITS + 1}};
    Prec states = {.shape = {steps, layers, hidden}};
    alloc_register(&acts, &observations);
    alloc_register(&acts, &state);
    alloc_register(&acts, &outputs);
    alloc_register(&acts, &states);
    alloc_create(&params);
    alloc_create(&acts);
    Prec flat = {.data = (precision_t*)params.mem, .shape = {params.total_elems}};
    Float master;
    master_weights_setup(&master, &flat, false, 0);
    FILE* file = fopen(checkpoint, "rb");
    assert(file && fseek(file, 0, SEEK_END) == 0);
    assert(ftell(file) == params.total_elems * sizeof(float));
    fclose(file);
    puf_load_weights_into(master, flat, 0, checkpoint);
    float* host = (float*)malloc(steps * OBS_SIZE * sizeof(float));
    precision_t* packed = (precision_t*)malloc(steps * OBS_SIZE * sizeof(precision_t));
    file = fopen(data, "rb");
    uint32_t header[22];
    assert(file && fread(header, sizeof(header), 1, file) == 1);
    assert(header[1] == 3 && header[3] == OBS_SIZE && header[8] == sizeof(float));
    assert(header[9] == 3 && header[10] == 5 && header[11] == 2 && header[12] == 2);
    assert(fread(host, sizeof(float), steps * OBS_SIZE, file) == steps * OBS_SIZE);
    fclose(file);
    for (int i = 0; i < steps * OBS_SIZE; i++) {
        packed[i] = from_float(host[i]);
    }
    cudaMemcpy(
        observations.data, packed, steps * OBS_SIZE * sizeof(precision_t), cudaMemcpyHostToDevice);
    cudaStream_t stream;
    assert(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking) == cudaSuccess);
    sync_test();
    if (graphs) {
        assert(cudaStreamBeginCapture(stream, cudaStreamCaptureModeGlobal) == cudaSuccess);
    }
    for (int t = 0; t < steps; t++) {
        if (t % 16 == 0) {
            cudaMemsetAsync(state.data, 0, layers * hidden * sizeof(precision_t), stream);
        }
        Prec obs = {.data = observations.data + t * OBS_SIZE, .shape = {1, OBS_SIZE}};
        Prec output = arch_forward(&arch, weights, activation, obs, state, stream);
        cudaMemcpyAsync(outputs.data + t * (KAG_ALL_LOGITS + 1), output.data,
            (KAG_ALL_LOGITS + 1) * sizeof(precision_t), cudaMemcpyDeviceToDevice, stream);
        cudaMemcpyAsync(states.data + t * layers * hidden, state.data,
            layers * hidden * sizeof(precision_t), cudaMemcpyDeviceToDevice, stream);
    }
    if (graphs) {
        cudaGraph_t graph;
        cudaGraphExec_t executable;
        assert(cudaStreamEndCapture(stream, &graph) == cudaSuccess);
        assert(cudaGraphInstantiate(&executable, graph, NULL, NULL, 0) == cudaSuccess);
        assert(cudaGraphLaunch(executable, stream) == cudaSuccess);
        sync_test();
        cudaGraphExecDestroy(executable);
        cudaGraphDestroy(graph);
    }
    sync_test();
    dump_tensor(directory, "weights", flat);
    dump_tensor(directory, "observations", observations);
    dump_tensor(directory, "decoded", outputs);
    dump_tensor(directory, "states", states);
    printf("checkpoint PASS: precision_bytes=%zu parameters=%ld\n", sizeof(precision_t),
        params.total_elems);
    free(host);
    free(packed);
}

void reward_clip_test(float clip, int graphs) {
    Ini ini = {};
    puf_ini_load_env(&ini, "kaggriculture", 0, NULL);
    puf_ini_put(&ini, "vec.total_agents", "8");
    puf_ini_put(&ini, "vec.num_buffers", "1");
    puf_ini_put(&ini, "vec.num_policies", "1");
    puf_ini_put(&ini, "base.async", "0");
    puf_ini_put(&ini, "base.load_model_path", "None");
    puf_ini_put(&ini, "selfplay.enabled", "0");
    puf_ini_put(&ini, "policy.hidden_size", "32");
    puf_ini_put(&ini, "policy.num_layers", "1");
    puf_ini_put(&ini, "train.horizon", "8");
    puf_ini_put(&ini, "train.minibatch_size", "64");
    dict_set(puf_ini_section(&ini, "train", 0), "reward_clip", clip);
    TrainContext context = {.rank = 0, .world_size = 1, .gpu_id = 0};
    PuffeRL* p = create_pufferl(&ini, &context);
    assert(p->hypers.reward_clip == clip);
    // Isolate the real transpose/clipping stage, without optimizer updates.
    p->hypers.replay_ratio = 0;
    precision_t input[64], output[64];
    for (int i = 0; i < 64; i++) {
        input[i] = from_float((i - 32) / 4.0f);
    }
    cudaMemcpy(p->rollouts.rewards.data, input, sizeof(input), cudaMemcpyHostToDevice);
    sync_test();
    cudaStream_t stream = p->train_stream;
    if (graphs) {
        assert(cudaStreamBeginCapture(stream, cudaStreamCaptureModeGlobal) == cudaSuccess);
    }
    train_epoch_gpu(p, p->rollouts, 0, stream);
    if (graphs) {
        cudaGraph_t graph;
        cudaGraphExec_t executable;
        assert(cudaStreamEndCapture(stream, &graph) == cudaSuccess);
        assert(cudaGraphInstantiate(&executable, graph, NULL, NULL, 0) == cudaSuccess);
        assert(cudaGraphLaunch(executable, stream) == cudaSuccess);
        sync_test();
        cudaGraphExecDestroy(executable);
        cudaGraphDestroy(graph);
    }
    sync_test();
    cudaMemcpy(output, p->train_rollouts.rewards.data, sizeof(output), cudaMemcpyDeviceToHost);
    for (int b = 0; b < 8; b++) {
        for (int t = 0; t < 8; t++) {
            float expected = to_float(input[t * 8 + b]);
            if (clip > 0) {
                expected = fmaxf(-clip, fminf(clip, expected));
            }
            assert(to_float(output[b * 8 + t]) == expected);
        }
    }
    close_pufferl(p);
    puf_ini_free(&ini);
    printf("reward clip PASS: clip=%g graphs=%d\n", clip, graphs);
}

int main(int argc, char** argv) {
    assert(argc == 6);
    if (!strcmp(argv[1], "sampler")) {
        sampler_test(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]));
    } else if (!strcmp(argv[1], "network")) {
        network_test(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), argv[5]);
    } else if (!strcmp(argv[1], "checkpoint")) {
        checkpoint_test(argv[2], atoi(argv[3]), argv[4], argv[5]);
    } else if (!strcmp(argv[1], "reward_clip")) {
        reward_clip_test(atof(argv[2]), atoi(argv[3]));
    } else {
        assert(!strcmp(argv[1], "adapter"));
        adapter_test(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]));
    }
    return 0;
}
