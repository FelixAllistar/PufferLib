#define ML_HEADLESS
#include "../mario_lab.cu"
#include <assert.h>
#include <vector>

static void test_parity(int graph_capture, int max_frames, int practice_task = 0, int short_goals = 1, int weighted_roots = 0, int physics_mode = 0, int full = 0) {
    const int n = 17;
    Dict cfg = {0}; dict_set(&cfg, "max_frames", max_frames);
    if (weighted_roots) { dict_set(&cfg, "mix_walkers", 35); dict_set(&cfg, "mix_gaps", 7); }
    if (full) {
        dict_set(&cfg, "generator_mode", 1); dict_set(&cfg, "length_min", 256); dict_set(&cfg, "length", ML_MAX_WIDTH);
    }
    if (physics_mode) {
        dict_set(&cfg, "physics_mode", physics_mode); dict_set(&cfg, "accel", 14); dict_set(&cfg, "run_speed", 640);
        dict_set(&cfg, "friction", 13); dict_set(&cfg, "jump_speed", 1024); dict_set(&cfg, "gravity_hold", 32);
        dict_set(&cfg, "max_fall", 1024); dict_set(&cfg, "hold_frames", 60);
        dict_set(&cfg, "player_width", 10); dict_set(&cfg, "player_height", 12);
        dict_set(&cfg, "pipe_min_height", 2); dict_set(&cfg, "pipe_max_height", 4); dict_set(&cfg, "stair_height", 8);
    }
    if (practice_task) {
        dict_set(&cfg, "practice_prob", 1); dict_set(&cfg, "practice_frames", max_frames);
        dict_set(&cfg, "practice_gap", practice_task == ML_TASK_GAP);
        dict_set(&cfg, "practice_entry", practice_task == ML_TASK_ENTRY);
        dict_set(&cfg, "practice_exit", practice_task == ML_TASK_EXIT);
        dict_set(&cfg, "practice_route", practice_task == ML_TASK_ROUTE);
        dict_set(&cfg, "practice_short_goals", short_goals);
    }
    float *obs, *actions, *rewards, *terminals;
    ML_CUDA(cudaMalloc(&obs, n * ML_OBS_SIZE * sizeof(float)));
    ML_CUDA(cudaMalloc(&actions, n * sizeof(float)));
    ML_CUDA(cudaMalloc(&rewards, n * sizeof(float)));
    ML_CUDA(cudaMalloc(&terminals, n * sizeof(float)));
    Env* envs = puf_vec_create(n, &cfg, obs, actions, rewards, terminals);
    cudaStream_t stream; ML_CUDA(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));
    puf_bind_stream(stream); puf_reset(envs); ML_CUDA(cudaStreamSynchronize(stream));
    std::vector<MLState> cpu(n), gpu(n); std::vector<Env> shells(n);
    unsigned int rng[n]; Log logs[n] = {};
    for (int i = 0; i < n; i++) {
        rng[i] = ml_initial_seed(&ml_host_config, i);
        ml_reset(&cpu[i], &ml_host_config, ml_rand(&rng[i]));
    }
    if (max_frames > 1 && !practice_task) {
        // Exercise both room transfers and all terminal outcomes, including
        // cases that short random trajectories almost never discover.
        MLConfig pipe_config = ml_host_config; pipe_config.course = ML_UNDERGROUND;
        ml_reset(&cpu[0], &pipe_config, 73);
        cpu[0].x = cpu[0].entry_x * ML_TILE + 4 * ML_FP;
        cpu[0].y = cpu[0].entry_y * ML_TILE; cpu[0].frontier[0] = cpu[0].x;
        cpu[1] = cpu[0]; ml_enter_room(&cpu[1], 1); cpu[1].transition_frames = 0;
        cpu[1].x = cpu[1].exit_x * ML_TILE - ML_PLAYER_W;
        cpu[1].frontier[1] = cpu[1].x;
        cpu[2].x = cpu[2].goal_x; cpu[2].pipe_returns = 1;
        cpu[3].y = (ML_HEIGHT + 3) * ML_TILE; cpu[3].grounded = 0;
        if (full) {
            // Exercise distant terrain/enemies rather than only the first 256
            // frames near the root; verify the extra storage and local slots.
            cpu[4].x = 240 * ML_TILE; cpu[4].frontier[0] = cpu[4].x;
            ml_add_enemy(&cpu[4], 242, 0);
            cpu[5].x = (cpu[5].widths[0] - 30) * ML_TILE; cpu[5].frontier[0] = cpu[5].x;
            for (int i = 0; i < n; i++) ml_visible_enemies(&cpu[i]);
        }
        ML_CUDA(cudaMemcpy(ml_states, cpu.data(), n * sizeof(MLState), cudaMemcpyHostToDevice));
    }
    cudaGraph_t graph = NULL; cudaGraphExec_t executable = NULL;
    if (graph_capture) {
        ML_CUDA(cudaStreamBeginCapture(stream, cudaStreamCaptureModeGlobal));
        puf_step(envs); ML_CUDA(cudaStreamEndCapture(stream, &graph));
        ML_CUDA(cudaGraphInstantiate(&executable, graph, NULL, NULL, 0));
    }
    std::vector<float> observations(n * ML_OBS_SIZE);
    uint32_t action_rng = 123;
    for (int step = 0; step < 256; step++) {
        float a[n], expected_r[n], expected_t[n], actual_r[n], actual_t[n];
        for (int i = 0; i < n; i++) {
            a[i] = i % 2 ? (float)(ml_rand(&action_rng) % 64) : (float)ml_script_action(&cpu[i]);
            if (max_frames > 1 && !practice_task && step == 0 && i == 1) a[i] = ML_RIGHT;
            expected_r[i] = ml_step(&cpu[i], &ml_host_config, (int)a[i]);
            expected_t[i] = (float)(cpu[i].status != ML_RUNNING);
            if (expected_t[i]) {
                ml_log_episode(&logs[i], &cpu[i]);
                ml_reset(&cpu[i], &ml_host_config, ml_rand(&rng[i]));
            }
        }
        ML_CUDA(cudaMemcpyAsync(actions, a, sizeof(a), cudaMemcpyHostToDevice, stream));
        if (graph_capture) ML_CUDA(cudaGraphLaunch(executable, stream)); else puf_step(envs);
        ML_CUDA(cudaStreamSynchronize(stream));
        ML_CUDA(cudaMemcpy(gpu.data(), ml_states, n * sizeof(MLState), cudaMemcpyDeviceToHost));
        ML_CUDA(cudaMemcpy(shells.data(), envs, n * sizeof(Env), cudaMemcpyDeviceToHost));
        ML_CUDA(cudaMemcpy(actual_r, rewards, sizeof(actual_r), cudaMemcpyDeviceToHost));
        ML_CUDA(cudaMemcpy(actual_t, terminals, sizeof(actual_t), cudaMemcpyDeviceToHost));
        ML_CUDA(cudaMemcpy(observations.data(), obs, observations.size() * sizeof(float), cudaMemcpyDeviceToHost));
        for (int i = 0; i < n; i++) {
            assert(fabsf(gpu[i].episode_return - cpu[i].episode_return) < 1e-4f);
            gpu[i].episode_return = cpu[i].episode_return;
            if (memcmp(&cpu[i], &gpu[i], sizeof(MLState))) {
                const unsigned char* expected = (const unsigned char*)&cpu[i];
                const unsigned char* actual = (const unsigned char*)&gpu[i];
                for (size_t b = 0; b < sizeof(MLState); b++) if (expected[b] != actual[b]) {
                    fprintf(stderr, "state mismatch: step=%d env=%d byte=%zu cpu=%u gpu=%u graph=%d physics=%d full=%d\n",
                        step, i, b, expected[b], actual[b], graph_capture, physics_mode, full); break;
                }
                assert(0 && "CPU/CUDA state mismatch");
            }
            assert(shells[i].num_agents == 1 && shells[i].rng == rng[i]);
            for (int j = 0; j < (int)(sizeof(Log) / sizeof(float)); j++)
                assert(fabsf(((float*)&shells[i].log)[j] - ((float*)&logs[i])[j]) < 1e-3f);
            assert(fabsf(expected_r[i] - actual_r[i]) < 1e-5f && actual_t[i] == expected_t[i]);
            for (int j = 0; j < ML_OBS_SIZE; j++)
                assert(fabsf(observations[i * ML_OBS_SIZE + j] - ml_observation_at(&cpu[i], &ml_host_config, j)) < 1e-6f);
        }
    }
    if (graph_capture) { ML_CUDA(cudaGraphExecDestroy(executable)); ML_CUDA(cudaGraphDestroy(graph)); }
    puf_close(envs); ML_CUDA(cudaStreamDestroy(stream));
    ML_CUDA(cudaFree(obs)); ML_CUDA(cudaFree(actions)); ML_CUDA(cudaFree(rewards)); ML_CUDA(cudaFree(terminals));
    dict_clear(&cfg);
    printf("CUDA parity PASS: 17 environments, 256 decisions, max_frames=%d, graph=%d, practice=%d, short_goals=%d, weighted_roots=%d, physics=%d, full=%d\n",
        max_frames, graph_capture, practice_task, short_goals, weighted_roots, physics_mode, full);
}
int main(void) {
    setbuf(stdout, NULL);
    Env probe = {}; puf_init(&probe, NULL); puf_close(&probe);
    test_parity(0, 1); test_parity(0, 137); test_parity(1, 137);
    for (int task = ML_TASK_GAP; task <= ML_TASK_ROUTE; task++) test_parity(1, 137, task);
    test_parity(1, 137, ML_TASK_GAP, 0);
    test_parity(1, 137, 0, 1, 1);
    test_parity(1, 137, 0, 1, 1, 1);
    test_parity(1, 137, ML_TASK_GAP, 1, 0, 1);
    test_parity(1, 137, ML_TASK_ROUTE, 1, 0, 1);
    test_parity(1, 137, 0, 1, 1, 2);
    test_parity(1, 137, ML_TASK_GAP, 1, 0, 2);
    test_parity(1, 137, ML_TASK_ROUTE, 1, 0, 2);
    test_parity(0, 137, 0, 1, 1, 2, 1);
    test_parity(1, 137, 0, 1, 1, 2, 1);
    test_parity(1, 137, ML_TASK_GAP, 1, 0, 2, 1);
    test_parity(1, 137, ML_TASK_ROUTE, 1, 0, 2, 1);
}
