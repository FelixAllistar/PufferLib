#include <cuda_runtime.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../puffer_survivors.h"
#include "../puffer_survivors.cu"

#define CUDA_CHECK(call) do { \
    cudaError_t err__ = (call); \
    if (err__ != cudaSuccess) { \
        std::fprintf(stderr, "CUDA error %s:%d: %s\n", \
            __FILE__, __LINE__, cudaGetErrorString(err__)); \
        std::exit(1); \
    } \
} while (0)

static void usage(const char* argv0) {
    std::fprintf(stderr,
        "usage: %s [envs=5120] [steps=2000] [warmup=200] [repeats=3] "
        "[--stats] [--stress] [--stress-churn] [section.key=value ...]\n"
        "example: %s 5120 3000 300 3 --stats env.moving_obstacle_cap=0\n"
        "         %s 5120 1000 100 1 --stress\n"
        "         %s 5120 1000 100 1 --stress-churn\n",
        argv0, argv0, argv0, argv0);
}

static void fill_actions(float* actions, int envs) {
    for (int i = 0; i < envs; i++) {
        // Keep several movement directions without per-step H2D action
        // traffic contaminating this benchmark.
        actions[(size_t)i * NUM_ATNS + 0] = (float)(1 + (i % 8));
        actions[(size_t)i * NUM_ATNS + 1] = 0.0f;
    }
}

static void bind_io(PSCudaSim* sim, obs_t* observations, float* actions,
        float* rewards, float* terminals) {
    sim->observations = (float*)observations;
    sim->actions = actions;
    sim->rewards = rewards;
    sim->terminals = terminals;
}

// Fill the already-allocated pools with a stable synthetic worst-case state.
// This deliberately uses the same SoA fields as the live simulator, so the
// profiler can separate algorithmic cost from spawn/death/reset policy.
__device__ static int stress_gcd(int a, int b) {
    while (b != 0) {
        int remainder = a % b;
        a = b;
        b = remainder;
    }
    return a;
}

__global__ static void seed_stress_kernel(PSCudaSim sim,
        int enemy_capacity, int projectile_count, int drop_count, int area_count,
        int churn) {
    int env = blockIdx.x * blockDim.x + threadIdx.x;
    if (env >= sim.num_envs) return;

    int active_enemies = enemy_capacity;
    if (churn) {
        active_enemies = (enemy_capacity * 3) / 4;
        if (active_enemies < 1 && enemy_capacity > 0) active_enemies = 1;
    }
    sim.enemy_count[env] = active_enemies;

    int stride = 1;
    int offset = 0;
    if (churn && enemy_capacity > 1) {
        stride = 97 + 2 * (env % 7);
        stride %= enemy_capacity;
        if (stride == 0) stride = 1;
        while (stress_gcd(stride, enemy_capacity) != 1) {
            stride++;
            if (stride >= enemy_capacity) stride = 1;
        }
        offset = (env * 31 + 17) % enemy_capacity;
    }

    for (int k = 0; k < active_enemies; k++) {
        int slot = churn && enemy_capacity > 1
            ? (offset + k * stride) % enemy_capacity
            : k;
        int e = PS_EIDX(sim, slot, env);
        float angle = 0.0245436926f * (float)((k * 97) & 255);
        float radius = 9.0f + 2.0f * (float)(k & 3);
        sim.enemy_active[e] = 1;
        sim.enemy_type[e] = (uint8_t)(k & PS_ENEMY_KIND_MASK);
        sim.enemy_shape[e] = PS_SHAPE_CIRCLE;
        sim.enemy_x[e] = cosf(angle) * radius;
        sim.enemy_y[e] = sinf(angle) * radius;
        sim.enemy_vx[e] = 0.0f;
        sim.enemy_vy[e] = 0.0f;
        sim.enemy_hp[e] = 1000000000.0f;
        sim.enemy_max_hp[e] = 1000000000.0f;
        sim.enemy_radius[e] = 0.45f;
        sim.enemy_bound_radius[e] = 0.45f;
        sim.enemy_half_width[e] = 0.45f;
        sim.enemy_half_height[e] = 0.45f;
        sim.enemy_speed[e] = 0.0f;
        sim.enemy_damage[e] = 0.0f;
        sim.enemy_next[e] = -1;
        sim.enemy_dense_pos[e] = k;
        sim.enemy_dense[PS_EIDX(sim, k, env)] = slot;
    }

    sim.projectile_count[env] = projectile_count;
    for (int i = 0; i < projectile_count; i++) {
        int p = PS_PIDX(sim, i, env);
        float angle = 0.0245436926f * (float)((i * 53 + 17) & 255);
        sim.projectile_active[p] = 1;
        sim.projectile_type[p] = PS_WEAPON_BUBBLE;
        sim.projectile_x[p] = cosf(angle) * 18.0f;
        sim.projectile_y[p] = sinf(angle) * 18.0f;
        sim.projectile_vx[p] = 0.0f;
        sim.projectile_vy[p] = 0.0f;
        sim.projectile_damage[p] = 0.0f;
        sim.projectile_radius[p] = 0.05f;
        sim.projectile_ttl[p] = 1000000000;
        sim.projectile_pierce[p] = 0;
        sim.projectile_dense_pos[p] = i;
        sim.projectile_dense[PS_PIDX(sim, i, env)] = i;
    }

    sim.drop_count[env] = drop_count;
    for (int i = 0; i < drop_count; i++) {
        int d = PS_DIDX(sim, i, env);
        float angle = 0.0245436926f * (float)((i * 71 + 31) & 255);
        sim.drop_active[d] = 1;
        sim.drop_type[d] = 0;
        sim.drop_x[d] = cosf(angle) * 20.0f;
        sim.drop_y[d] = sinf(angle) * 20.0f;
        sim.drop_value[d] = 1.0f;
        sim.drop_dense_pos[d] = i;
        sim.drop_dense[PS_DIDX(sim, i, env)] = i;
    }

    sim.area_count[env] = area_count;
    for (int i = 0; i < area_count; i++) {
        int a = PS_AIDX(sim, i, env);
        float angle = 0.0245436926f * (float)((i * 83 + 47) & 255);
        sim.area_active[a] = 1;
        sim.area_type[a] = PS_WEAPON_WHIRLPOOL;
        sim.area_x[a] = cosf(angle) * 20.0f;
        sim.area_y[a] = sinf(angle) * 20.0f;
        sim.area_radius[a] = 0.5f;
        sim.area_damage[a] = 0.0f;
        sim.area_ttl[a] = 1000000000;
        sim.area_tick_rate[a] = 1000000000;
        sim.area_tick_timer[a] = 1000000000;
        sim.area_dense_pos[a] = i;
        sim.area_dense[PS_AIDX(sim, i, env)] = i;
    }

    sim.active_ink_count[env] = 0;
    sim.moving_obstacle_count[env] = 0;
    sim.grid_touched_count[env] = 0;
    sim.aabb_count[env] = 0;
    sim.nearest_enemy[env] = -1;
    sim.nearest_enemy_d2[env] = 1e30f;
    for (int i = 0; i < PS_WEAPON_COUNT; i++) {
        int w = PS_WIDX(sim, i, env);
        sim.weapon_level[w] = 0;
        sim.weapon_cd[w] = 1000000000.0f;
        sim.weapon_active[w] = 0.0f;
    }
}

static void seed_stress_state(PSCudaSim* sim, int num_envs, bool churn) {
    int blocks = (num_envs + PS_CUDA_BLOCK_SIZE - 1) / PS_CUDA_BLOCK_SIZE;
    seed_stress_kernel<<<blocks, PS_CUDA_BLOCK_SIZE>>>(*sim,
        sim->cfg.enemy_cap,
        sim->cfg.projectile_cap,
        sim->cfg.drop_cap,
        sim->cfg.area_cap,
        churn ? 1 : 0);
    CUDA_CHECK(cudaGetLastError());
}

static float run_pass(Env* envs, int num_envs, int steps, int warmup,
        bool include_wrapper, obs_t* observations, float* actions,
        float* rewards, float* terminals, bool stress, bool churn) {
    PSCudaSim* sim = ps_cuda_get_sim(envs);
    puf_envs_reset(envs, observations, rewards, terminals, num_envs);
    bind_io(sim, observations, actions, rewards, terminals);
    if (stress) seed_stress_state(sim, num_envs, churn);

    for (int i = 0; i < warmup; i++) {
        if (include_wrapper) {
            puf_envs_step(envs, actions, observations, rewards, terminals,
                0, num_envs, 0);
        } else {
            ps_cuda_step_range(sim, 0, num_envs, 0);
        }
    }
    CUDA_CHECK(cudaDeviceSynchronize());

    cudaEvent_t start, end;
    CUDA_CHECK(cudaEventCreate(&start));
    CUDA_CHECK(cudaEventCreate(&end));
    CUDA_CHECK(cudaEventRecord(start, 0));
    for (int i = 0; i < steps; i++) {
        if (include_wrapper) {
            puf_envs_step(envs, actions, observations, rewards, terminals,
                0, num_envs, 0);
        } else {
            ps_cuda_step_range(sim, 0, num_envs, 0);
        }
    }
    CUDA_CHECK(cudaEventRecord(end, 0));
    CUDA_CHECK(cudaEventSynchronize(end));

    float ms = 0.0f;
    CUDA_CHECK(cudaEventElapsedTime(&ms, start, end));
    CUDA_CHECK(cudaEventDestroy(start));
    CUDA_CHECK(cudaEventDestroy(end));
    return ms;
}

static void print_population_snapshot(const char* label, int step, int envs,
        const int* enemy, const int* projectile, const int* drop,
        const int* area, const int* ink, const int* moving) {
    long long sums[6] = {0};
    int maxes[6] = {0};
    const int* values[] = {enemy, projectile, drop, area, ink, moving};
    for (int e = 0; e < envs; e++) {
        for (int k = 0; k < 6; k++) {
            int value = values[k][e];
            sums[k] += value;
            if (value > maxes[k]) maxes[k] = value;
        }
    }
    std::printf("%s step=%d avg(enemy/projectile/drop/area/ink/moving)=%.1f/%.1f/%.1f/%.1f/%.1f/%.1f "
        "max=%d/%d/%d/%d/%d/%d\n",
        label, step,
        (double)sums[0] / envs, (double)sums[1] / envs,
        (double)sums[2] / envs, (double)sums[3] / envs,
        (double)sums[4] / envs, (double)sums[5] / envs,
        maxes[0], maxes[1], maxes[2], maxes[3], maxes[4], maxes[5]);
}

static void trace_populations(Env* envs, int num_envs, int steps,
        obs_t* observations, float* actions, float* rewards, float* terminals) {
    PSCudaSim* sim = ps_cuda_get_sim(envs);
    int* enemy = (int*)std::malloc(sizeof(int) * (size_t)num_envs);
    int* projectile = (int*)std::malloc(sizeof(int) * (size_t)num_envs);
    int* drop = (int*)std::malloc(sizeof(int) * (size_t)num_envs);
    int* area = (int*)std::malloc(sizeof(int) * (size_t)num_envs);
    int* ink = (int*)std::malloc(sizeof(int) * (size_t)num_envs);
    int* moving = (int*)std::malloc(sizeof(int) * (size_t)num_envs);
    if (!enemy || !projectile || !drop || !area || !ink || !moving) {
        std::fprintf(stderr, "population trace allocation failed\n");
        std::free(enemy); std::free(projectile); std::free(drop);
        std::free(area); std::free(ink); std::free(moving);
        return;
    }

    puf_envs_reset(envs, observations, rewards, terminals, num_envs);
    bind_io(sim, observations, actions, rewards, terminals);
    int interval = steps / 10;
    if (interval < 100) interval = 100;
    for (int step = 1; step <= steps; step++) {
        ps_cuda_step_range(sim, 0, num_envs, 0);
        if (step % interval != 0 && step != steps) continue;
        CUDA_CHECK(cudaDeviceSynchronize());
        CUDA_CHECK(cudaMemcpy(enemy, sim->enemy_count,
            sizeof(int) * (size_t)num_envs, cudaMemcpyDeviceToHost));
        CUDA_CHECK(cudaMemcpy(projectile, sim->projectile_count,
            sizeof(int) * (size_t)num_envs, cudaMemcpyDeviceToHost));
        CUDA_CHECK(cudaMemcpy(drop, sim->drop_count,
            sizeof(int) * (size_t)num_envs, cudaMemcpyDeviceToHost));
        CUDA_CHECK(cudaMemcpy(area, sim->area_count,
            sizeof(int) * (size_t)num_envs, cudaMemcpyDeviceToHost));
        CUDA_CHECK(cudaMemcpy(ink, sim->active_ink_count,
            sizeof(int) * (size_t)num_envs, cudaMemcpyDeviceToHost));
        CUDA_CHECK(cudaMemcpy(moving, sim->moving_obstacle_count,
            sizeof(int) * (size_t)num_envs, cudaMemcpyDeviceToHost));
        print_population_snapshot("population", step, num_envs,
            enemy, projectile, drop, area, ink, moving);
    }

    std::free(enemy); std::free(projectile); std::free(drop);
    std::free(area); std::free(ink); std::free(moving);
}

#ifdef PS_CUDA_PROFILE
static void profile_pass(Env* envs, int num_envs, int steps, int warmup,
        obs_t* observations, float* actions, float* rewards, float* terminals,
        bool stress, bool churn) {
    PSCudaSim* sim = ps_cuda_get_sim(envs);
    puf_envs_reset(envs, observations, rewards, terminals, num_envs);
    bind_io(sim, observations, actions, rewards, terminals);
    if (stress) seed_stress_state(sim, num_envs, churn);
    for (int i = 0; i < warmup; i++) {
        ps_cuda_step_range(sim, 0, num_envs, 0);
    }
    CUDA_CHECK(cudaDeviceSynchronize());
    CUDA_CHECK(cudaMemset(sim->profile_cycles, 0,
        sizeof(unsigned long long) * (size_t)num_envs * PS_PROFILE_STAGE_COUNT));
    for (int i = 0; i < steps; i++) {
        ps_cuda_step_range(sim, 0, num_envs, 0);
    }
    CUDA_CHECK(cudaDeviceSynchronize());

    unsigned long long* cycles = (unsigned long long*)std::malloc(
        sizeof(unsigned long long) * (size_t)num_envs * PS_PROFILE_STAGE_COUNT);
    if (!cycles) {
        std::fprintf(stderr, "profile allocation failed\n");
        return;
    }
    CUDA_CHECK(cudaMemcpy(cycles, sim->profile_cycles,
        sizeof(unsigned long long) * (size_t)num_envs * PS_PROFILE_STAGE_COUNT,
        cudaMemcpyDeviceToHost));

    cudaDeviceProp prop = {};
    CUDA_CHECK(cudaGetDeviceProperties(&prop, 0));
    const char* names[PS_PROFILE_STAGE_COUNT] = {
        "total", "movement", "wave_spawns", "enemies", "grid",
        "weapons", "projectiles", "drops", "observations"
    };
    double totals[PS_PROFILE_STAGE_COUNT] = {0.0};
    unsigned long long maxes[PS_PROFILE_STAGE_COUNT] = {0};
    for (int stage = 0; stage < PS_PROFILE_STAGE_COUNT; stage++) {
        for (int env = 0; env < num_envs; env++) {
            unsigned long long value = cycles[(size_t)stage * num_envs + env];
            totals[stage] += (double)value;
            if (value > maxes[stage]) maxes[stage] = value;
        }
    }
    std::printf("profile clock_khz=%d samples=%d\n", prop.clockRate, steps);
    for (int stage = 0; stage < PS_PROFILE_STAGE_COUNT; stage++) {
        double avg_us = totals[stage] * 1000.0
            / ((double)num_envs * steps * prop.clockRate);
        double max_us = (double)maxes[stage] * 1000.0
            / ((double)steps * prop.clockRate);
        std::printf("profile stage=%s avg_us=%.3f max_env_step_us=%.3f\n",
            names[stage], avg_us, max_us);
    }
    double attributed = 0.0;
    for (int stage = PS_PROFILE_MOVEMENT; stage < PS_PROFILE_STAGE_COUNT; stage++) {
        attributed += totals[stage];
    }
    double other_us = (totals[PS_PROFILE_TOTAL] - attributed) * 1000.0
        / ((double)num_envs * steps * prop.clockRate);
    std::printf("profile stage=other avg_us=%.3f\n", other_us);
    std::free(cycles);
}
#endif

int main(int argc, char** argv) {
    if (argc > 1 && (!std::strcmp(argv[1], "-h") || !std::strcmp(argv[1], "--help"))) {
        usage(argv[0]);
        return 0;
    }
    int num_envs = argc > 1 ? std::atoi(argv[1]) : 5120;
    int steps = argc > 2 ? std::atoi(argv[2]) : 2000;
    int warmup = argc > 3 ? std::atoi(argv[3]) : 200;
    int repeats = argc > 4 ? std::atoi(argv[4]) : 3;
    if (num_envs < 1 || steps < 1 || warmup < 0 || repeats < 1) {
        usage(argv[0]);
        return 2;
    }

    Ini ini = {};
    int override_start = 5;
    bool stats = false;
    bool stress = false;
    bool churn = false;
    int override_count = 0;
    char** override_argv = nullptr;
    if (argc > override_start) {
        override_argv = (char**)std::malloc(sizeof(char*) * (size_t)(argc - override_start));
        for (int i = override_start; i < argc; i++) {
            if (!std::strcmp(argv[i], "--stats")) stats = true;
            else if (!std::strcmp(argv[i], "--stress")) stress = true;
            else if (!std::strcmp(argv[i], "--stress-churn")) {
                stress = true;
                churn = true;
            }
            else override_argv[override_count++] = argv[i];
        }
    }
    // Remaining arguments use the normal puffer section.key=value override
    // syntax, so the benchmark can compare one gameplay path at a time.
    puf_ini_load_env(&ini, "puffer_survivors", override_count, override_argv);
    std::free(override_argv);
    Dict* cfg = puf_ini_section(&ini, "env", 0);
    // Keep the benchmark populated instead of measuring episode reset policy.
    dict_set(cfg, "max_steps", 1000000000.0);
    dict_set(cfg, "player_health", 1000000000.0);

    Env* envs = puf_envs_create(num_envs, cfg);
    obs_t* observations = nullptr;
    float* actions = nullptr;
    float* rewards = nullptr;
    float* terminals = nullptr;
    CUDA_CHECK(cudaMalloc((void**)&observations,
        sizeof(obs_t) * (size_t)num_envs * OBS_SIZE));
    CUDA_CHECK(cudaMalloc((void**)&actions,
        sizeof(float) * (size_t)num_envs * NUM_ATNS));
    CUDA_CHECK(cudaMalloc((void**)&rewards, sizeof(float) * (size_t)num_envs));
    CUDA_CHECK(cudaMalloc((void**)&terminals, sizeof(float) * (size_t)num_envs));

    float* host_actions = (float*)std::malloc(
        sizeof(float) * (size_t)num_envs * NUM_ATNS);
    if (!host_actions) {
        std::fprintf(stderr, "host allocation failed\n");
        return 1;
    }
    fill_actions(host_actions, num_envs);
    CUDA_CHECK(cudaMemcpy(actions, host_actions,
        sizeof(float) * (size_t)num_envs * NUM_ATNS,
        cudaMemcpyHostToDevice));

    std::printf("gpu_sim_bench envs=%d steps=%d warmup=%d repeats=%d obs=%d\n",
        num_envs, steps, warmup, repeats, OBS_SIZE);
    for (int repeat = 0; repeat < repeats; repeat++) {
        float raw_ms = run_pass(envs, num_envs, steps, warmup, false,
            observations, actions, rewards, terminals, stress, churn);
        float wrapped_ms = run_pass(envs, num_envs, steps, warmup, true,
            observations, actions, rewards, terminals, stress, churn);
        double samples = (double)num_envs * (double)steps;
        double raw_sps = samples / ((double)raw_ms * 1e-3);
        double wrapped_sps = samples / ((double)wrapped_ms * 1e-3);
        double wrapper_us = (double)(wrapped_ms - raw_ms) * 1000.0 / steps;
        std::printf("repeat=%d raw_sim_ms=%.3f raw_sps=%.1f "
            "wrapped_ms=%.3f wrapped_sps=%.1f "
            "wrapper_us_per_step=%.3f\n",
            repeat, raw_ms, raw_sps, wrapped_ms, wrapped_sps, wrapper_us);
    }
    if (stats) {
        trace_populations(envs, num_envs, steps, observations, actions,
            rewards, terminals);
    }
#ifdef PS_CUDA_PROFILE
    profile_pass(envs, num_envs, steps, warmup, observations, actions,
        rewards, terminals, stress, churn);
#endif

    std::free(host_actions);
    puf_envs_close(envs);
    CUDA_CHECK(cudaFree(observations));
    CUDA_CHECK(cudaFree(actions));
    CUDA_CHECK(cudaFree(rewards));
    CUDA_CHECK(cudaFree(terminals));
    puf_ini_free(&ini);
    return 0;
}
