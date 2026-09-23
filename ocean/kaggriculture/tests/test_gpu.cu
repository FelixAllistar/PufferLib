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
                float expected_reward = env->game.done ? env->reward_money *
                        (env->game.players[player].money - env->policy.history[player].start_cash) /
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

int main(int argc, char** argv) {
    assert(argc == 6);
    if (!strcmp(argv[1], "sampler")) {
        sampler_test(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]));
    } else {
        assert(!strcmp(argv[1], "adapter"));
        adapter_test(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]));
    }
    return 0;
}
