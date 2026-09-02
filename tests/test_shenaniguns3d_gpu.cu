// Differential smoke test for the GPU shenaniguns3d backend.
//
// The CPU controller is the reference implementation. The CUDA backend uses a
// fixed static-box solver, so this test checks the shared reset/sensor contract
// and bounded rollout behavior while reporting the expected physics drift.

#include <cuda_runtime.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define PUFFER_GPU_ENV
#include "../ocean/shenaniguns3d/shenaniguns3d.h"
#include "../ocean/shenaniguns3d/shenaniguns3d.cu"

static void gpu_test_check(cudaError_t error, const char* what) {
    if (error != cudaSuccess) {
        std::fprintf(stderr, "%s: %s\n", what, cudaGetErrorString(error));
        std::exit(1);
    }
}

static float max_observation_error(const obs_t* a, const obs_t* b) {
    float error = 0.0f;
    for (int i = 0; i < OBS_SIZE; i++) {
        float delta = std::fabs((float)a[i] - (float)b[i]);
        if (delta > error) error = delta;
    }
    return error;
}

static void assert_finite(const obs_t* obs, float reward, float terminal,
                          int step) {
    if (!std::isfinite(reward) || !std::isfinite(terminal)) {
        std::fprintf(stderr, "non-finite scalar output at step %d\n", step);
        std::exit(1);
    }
    for (int i = 0; i < OBS_SIZE; i++) {
        if (!std::isfinite((float)obs[i])) {
            std::fprintf(stderr, "non-finite observation at step %d index %d\n",
                         step, i);
            std::exit(1);
        }
    }
}

static bool course_float_equal(float a, float b) {
    return std::fabs(a - b) <= 2.0e-6f;
}

static bool course_equal(const CourseParams* a, const CourseParams* b) {
    if (a->column_count != b->column_count || a->room_count != b->room_count ||
        a->ceiling_room != b->ceiling_room ||
        !course_float_equal(a->jump_x, b->jump_x) ||
        !course_float_equal(a->jump_width, b->jump_width) ||
        !course_float_equal(a->pit_depth, b->pit_depth) ||
        !course_float_equal(a->tunnel_start, b->tunnel_start) ||
        !course_float_equal(a->tunnel_end, b->tunnel_end) ||
        !course_float_equal(a->tunnel_clearance, b->tunnel_clearance) ||
        !course_float_equal(a->hole_start, b->hole_start) ||
        !course_float_equal(a->hole_end, b->hole_end) ||
        !course_float_equal(a->ceiling_clearance, b->ceiling_clearance) ||
        !course_float_equal(a->goal_x, b->goal_x) ||
        !course_float_equal(a->goal_z, b->goal_z) ||
        !course_float_equal(a->goal_y, b->goal_y) ||
        !course_float_equal(a->route_length, b->route_length)) return false;
    for (int i = 0; i < COURSE_DOORS; i++) {
        if (!course_float_equal(a->doors[i].x, b->doors[i].x) ||
            !course_float_equal(a->doors[i].z, b->doors[i].z) ||
            !course_float_equal(a->doors[i].width, b->doors[i].width) ||
            !course_float_equal(a->doors[i].height, b->doors[i].height) ||
            a->doors[i].axis != b->doors[i].axis ||
            a->doors[i].kind != b->doors[i].kind) return false;
    }
    for (int i = 0; i < COURSE_MAX_ROOMS; i++) {
        if (a->rooms[i].gx != b->rooms[i].gx ||
            a->rooms[i].gz != b->rooms[i].gz ||
            !course_float_equal(a->rooms[i].x, b->rooms[i].x) ||
            !course_float_equal(a->rooms[i].z, b->rooms[i].z)) return false;
    }
    for (int i = 0; i < COURSE_MAX_ROOMS - 1; i++) {
        if (!course_float_equal(a->route_doors[i].x, b->route_doors[i].x) ||
            !course_float_equal(a->route_doors[i].z, b->route_doors[i].z) ||
            !course_float_equal(a->route_doors[i].width, b->route_doors[i].width) ||
            !course_float_equal(a->route_doors[i].height, b->route_doors[i].height) ||
            a->route_doors[i].axis != b->route_doors[i].axis ||
            a->route_doors[i].kind != b->route_doors[i].kind) return false;
    }
    for (int i = 0; i < COURSE_MAX_COLUMNS; i++) {
        if (!course_float_equal(a->columns[i].x, b->columns[i].x) ||
            !course_float_equal(a->columns[i].z, b->columns[i].z) ||
            !course_float_equal(a->columns[i].radius, b->columns[i].radius) ||
            !course_float_equal(a->columns[i].height, b->columns[i].height)) return false;
    }
    return true;
}

static void check_course_parity_case(int course_mode, int difficulty, int stage) {
    const int count = 8;
    Dict kwargs = {0};
    dict_set(&kwargs, "course_mode", course_mode);
    dict_set(&kwargs, "course_difficulty", difficulty);
    dict_set(&kwargs, "course_stage", stage);

    Env cpu[count] = {};
    for (int i = 0; i < count; i++) {
        cpu[i].rng = (unsigned int)i;
        puf_init(&cpu[i], &kwargs);
    }
    Env* gpu_envs = puf_envs_create(count, &kwargs);
    S3DNative* native = s3d_find_native(gpu_envs);
    CourseParams gpu_courses[count] = {};
    int gpu_num_boxes[count] = {};
    AABB gpu_boxes[count * MAX_BOXES] = {};
    gpu_test_check(cudaMemcpy(gpu_courses, native->sim.courses,
                              sizeof(gpu_courses), cudaMemcpyDeviceToHost),
                   "copy generated course");
    gpu_test_check(cudaMemcpy(gpu_num_boxes, native->sim.num_boxes,
                              sizeof(gpu_num_boxes), cudaMemcpyDeviceToHost),
                   "copy generated box counts");
    gpu_test_check(cudaMemcpy(gpu_boxes, native->sim.boxes,
                              sizeof(gpu_boxes), cudaMemcpyDeviceToHost),
                   "copy generated boxes");
    for (int i = 0; i < count; i++) {
        if (!course_equal(&cpu[i].course, &gpu_courses[i]) ||
            cpu[i].numBoxes != gpu_num_boxes[i]) {
            std::fprintf(stderr,
                         "course mismatch mode=%d difficulty=%d stage=%d lane=%d\n",
                         course_mode, difficulty, stage, i);
            std::exit(1);
        }
        for (int b = 0; b < gpu_num_boxes[i]; b++) {
            const AABB* cpu_box = &cpu[i].boxes[b];
            const AABB* gpu_box = &gpu_boxes[i * MAX_BOXES + b];
            if (!course_float_equal(cpu_box->cx, gpu_box->cx) ||
                !course_float_equal(cpu_box->cy, gpu_box->cy) ||
                !course_float_equal(cpu_box->cz, gpu_box->cz) ||
                !course_float_equal(cpu_box->hx, gpu_box->hx) ||
                !course_float_equal(cpu_box->hy, gpu_box->hy) ||
                !course_float_equal(cpu_box->hz, gpu_box->hz)) {
                std::fprintf(stderr,
                             "box mismatch mode=%d difficulty=%d stage=%d lane=%d box=%d\n",
                             course_mode, difficulty, stage, i, b);
                std::exit(1);
            }
        }
    }
    puf_envs_close(gpu_envs);
    for (int i = 0; i < count; i++) puf_close(&cpu[i]);
    dict_clear(&kwargs);
}

static void check_course_parity() {
    check_course_parity_case(COURSE_MODE_FIXED, 1, -1);
    check_course_parity_case(COURSE_MODE_RANDOM, 1, -1);
    check_course_parity_case(COURSE_MODE_RANDOM, 2, -1);
    check_course_parity_case(COURSE_MODE_RANDOM_EVERY_RESET, 3, -1);
    for (int stage = COURSE_STAGE_COLUMNS; stage <= COURSE_STAGE_STRESS; stage++)
        check_course_parity_case(COURSE_MODE_FIXED, 2, stage);
    std::printf("shenaniguns3d GPU course and geometry parity PASS\n");
}

static void check_timeout_trace() {
    Dict kwargs = {0};
    dict_set(&kwargs, "course_mode", COURSE_MODE_FIXED);
    dict_set(&kwargs, "course_stage", -1);
    dict_set(&kwargs, "max_ticks", 2);
    dict_set(&kwargs, "reward_progress", 0.0);
    dict_set(&kwargs, "time_cost", 0.25);
    dict_set(&kwargs, "jump_penalty", 0.0);
    dict_set(&kwargs, "crouch_penalty", 0.0);
    dict_set(&kwargs, "reward_head_hit", 0.0);

    Env cpu = {0};
    cpu.rng = 0;
    obs_t cpu_obs[OBS_SIZE] = {};
    float cpu_actions[NUM_ATNS] = {2.0f, 1.0f, 1.0f, 0.0f, 0.0f};
    float cpu_reward = 0.0f;
    float cpu_terminal = 0.0f;
    puf_init(&cpu, &kwargs);
    cpu.agents[0].observations = cpu_obs;
    cpu.agents[0].actions = cpu_actions;
    cpu.agents[0].rewards = &cpu_reward;
    cpu.agents[0].terminals = &cpu_terminal;
    puf_reset(&cpu);

    Env* gpu_envs = puf_envs_create(1, &kwargs);
    S3DNative* native = s3d_find_native(gpu_envs);
    obs_t* gpu_obs = nullptr;
    float* gpu_actions = nullptr;
    float* gpu_rewards = nullptr;
    float* gpu_terminals = nullptr;
    gpu_test_check(cudaMalloc((void**)&gpu_obs, sizeof(obs_t) * OBS_SIZE),
                   "allocate timeout observations");
    gpu_test_check(cudaMalloc((void**)&gpu_actions,
                              sizeof(float) * NUM_ATNS),
                   "allocate timeout actions");
    gpu_test_check(cudaMalloc((void**)&gpu_rewards, sizeof(float)),
                   "allocate timeout rewards");
    gpu_test_check(cudaMalloc((void**)&gpu_terminals, sizeof(float)),
                   "allocate timeout terminals");
    puf_envs_reset(gpu_envs, gpu_obs, gpu_rewards, gpu_terminals, 1);
    gpu_test_check(cudaMemcpy(gpu_actions, cpu_actions,
                              sizeof(cpu_actions), cudaMemcpyHostToDevice),
                   "copy timeout action");
    gpu_test_check(cudaDeviceSynchronize(), "synchronize timeout reset");

    obs_t gpu_host_obs[OBS_SIZE] = {};
    float gpu_reward = 0.0f;
    float gpu_terminal = 0.0f;
    for (int step = 1; step <= 3; step++) {
        puf_step(&cpu);
        puf_envs_step(gpu_envs, gpu_actions, gpu_obs, gpu_rewards,
                      gpu_terminals, 0, 1, 0);
        gpu_test_check(cudaDeviceSynchronize(), "synchronize timeout step");
        gpu_test_check(cudaMemcpy(gpu_host_obs, gpu_obs,
                                  sizeof(gpu_host_obs), cudaMemcpyDeviceToHost),
                       "copy timeout observation");
        gpu_test_check(cudaMemcpy(&gpu_reward, gpu_rewards, sizeof(float),
                                  cudaMemcpyDeviceToHost),
                       "copy timeout reward");
        gpu_test_check(cudaMemcpy(&gpu_terminal, gpu_terminals, sizeof(float),
                                  cudaMemcpyDeviceToHost),
                       "copy timeout terminal");
        assert_finite(gpu_host_obs, gpu_reward, gpu_terminal, step);
        if (std::fabs(cpu_reward - gpu_reward) > 1.0e-6f ||
            cpu_terminal != gpu_terminal ||
            cpu_terminal != (step == 3 ? 1.0f : 0.0f)) {
            std::fprintf(stderr, "timeout trace mismatch at step %d "
                         "cpu=(%.7g,%.1f) gpu=(%.7g,%.1f)\n", step,
                         (double)cpu_reward, (double)cpu_terminal,
                         (double)gpu_reward, (double)gpu_terminal);
            std::exit(1);
        }
        if (step == 3 && max_observation_error(cpu_obs, gpu_host_obs) > 1.0e-3f) {
            std::fprintf(stderr, "post-timeout reset observation mismatch\n");
            std::exit(1);
        }
    }

    Log gpu_log = {};
    gpu_test_check(cudaMemcpy(&gpu_log, &native->sim.envs[0].log,
                              sizeof(gpu_log), cudaMemcpyDeviceToHost),
                   "copy timeout log");
    if (std::fabs(cpu.log.perf - gpu_log.perf) > 1.0e-6f ||
        std::fabs(cpu.log.episode_return - gpu_log.episode_return) > 1.0e-6f ||
        std::fabs(cpu.log.episode_length - gpu_log.episode_length) > 1.0e-6f ||
        std::fabs(cpu.log.score - gpu_log.score) > 1.0e-6f ||
        std::fabs(cpu.log.n - gpu_log.n) > 1.0e-6f) {
        std::fprintf(stderr, "timeout log mismatch\n");
        std::exit(1);
    }
    std::printf("shenaniguns3d GPU timeout trace parity PASS\n");

    puf_envs_close(gpu_envs);
    cudaFree(gpu_obs);
    cudaFree(gpu_actions);
    cudaFree(gpu_rewards);
    cudaFree(gpu_terminals);
    puf_close(&cpu);
    dict_clear(&kwargs);
}

static void check_random_reset_parity() {
    const int count = 2;
    Dict kwargs = {0};
    dict_set(&kwargs, "course_mode", COURSE_MODE_RANDOM_EVERY_RESET);
    dict_set(&kwargs, "course_difficulty", 3);
    dict_set(&kwargs, "course_stage", -1);

    Env cpu[count] = {};
    obs_t cpu_obs[count][OBS_SIZE] = {};
    float cpu_actions[count][NUM_ATNS] = {};
    float cpu_rewards[count] = {};
    float cpu_terminals[count] = {};
    for (int i = 0; i < count; i++) {
        cpu[i].rng = (unsigned int)i;
        puf_init(&cpu[i], &kwargs);
        cpu[i].agents[0].observations = cpu_obs[i];
        cpu[i].agents[0].actions = cpu_actions[i];
        cpu[i].agents[0].rewards = &cpu_rewards[i];
        cpu[i].agents[0].terminals = &cpu_terminals[i];
        cpu_actions[i][0] = 2.0f;
        cpu_actions[i][1] = 1.0f;
        cpu_actions[i][2] = 1.0f;
        puf_reset(&cpu[i]);
    }

    Env* gpu_envs = puf_envs_create(count, &kwargs);
    S3DNative* native = s3d_find_native(gpu_envs);
    obs_t* gpu_obs = nullptr;
    float* gpu_actions = nullptr;
    float* gpu_rewards = nullptr;
    float* gpu_terminals = nullptr;
    gpu_test_check(cudaMalloc((void**)&gpu_obs,
                              sizeof(obs_t) * count * OBS_SIZE),
                   "allocate random reset observations");
    gpu_test_check(cudaMalloc((void**)&gpu_actions,
                              sizeof(float) * count * NUM_ATNS),
                   "allocate random reset actions");
    gpu_test_check(cudaMalloc((void**)&gpu_rewards, sizeof(float) * count),
                   "allocate random reset rewards");
    gpu_test_check(cudaMalloc((void**)&gpu_terminals, sizeof(float) * count),
                   "allocate random reset terminals");
    puf_envs_reset(gpu_envs, gpu_obs, gpu_rewards, gpu_terminals, count);
    gpu_test_check(cudaMemcpy(gpu_actions, cpu_actions, sizeof(cpu_actions),
                              cudaMemcpyHostToDevice),
                   "copy random reset actions");
    puf_envs_step(gpu_envs, gpu_actions, gpu_obs, gpu_rewards, gpu_terminals,
                  0, count, 0);
    gpu_test_check(cudaDeviceSynchronize(), "synchronize random reset step");
    for (int i = 0; i < count; i++) puf_step(&cpu[i]);
    for (int i = 0; i < count; i++) puf_reset(&cpu[i]);
    puf_envs_reset(gpu_envs, gpu_obs, gpu_rewards, gpu_terminals, count);
    gpu_test_check(cudaDeviceSynchronize(), "synchronize random course reset");

    CourseParams gpu_courses[count] = {};
    gpu_test_check(cudaMemcpy(gpu_courses, native->sim.courses,
                              sizeof(gpu_courses), cudaMemcpyDeviceToHost),
                   "copy random reset courses");
    for (int i = 0; i < count; i++) {
        if (!course_equal(&cpu[i].course, &gpu_courses[i])) {
            std::fprintf(stderr, "random reset course mismatch at lane %d\n", i);
            std::exit(1);
        }
    }
    std::printf("shenaniguns3d GPU random reset parity PASS\n");

    puf_envs_close(gpu_envs);
    for (int i = 0; i < count; i++) puf_close(&cpu[i]);
    cudaFree(gpu_obs);
    cudaFree(gpu_actions);
    cudaFree(gpu_rewards);
    cudaFree(gpu_terminals);
    dict_clear(&kwargs);
}

static void print_sensor_probe(const char* name, int course_mode,
                              int difficulty, int stage) {
    Dict kwargs = {0};
    dict_set(&kwargs, "course_mode", course_mode);
    dict_set(&kwargs, "course_difficulty", difficulty);
    dict_set(&kwargs, "course_stage", stage);

    Env cpu = {0};
    cpu.rng = 0;
    obs_t cpu_obs[OBS_SIZE] = {};
    float cpu_actions[NUM_ATNS] = {};
    float cpu_reward = 0.0f;
    float cpu_terminal = 0.0f;
    puf_init(&cpu, &kwargs);
    cpu.agents[0].observations = cpu_obs;
    cpu.agents[0].actions = cpu_actions;
    cpu.agents[0].rewards = &cpu_reward;
    cpu.agents[0].terminals = &cpu_terminal;
    puf_reset(&cpu);

    Env* gpu_envs = puf_envs_create(1, &kwargs);
    obs_t* gpu_obs = nullptr;
    float* gpu_rewards = nullptr;
    float* gpu_terminals = nullptr;
    gpu_test_check(cudaMalloc((void**)&gpu_obs, sizeof(obs_t) * OBS_SIZE),
                   "allocate sensor probe observations");
    gpu_test_check(cudaMalloc((void**)&gpu_rewards, sizeof(float)),
                   "allocate sensor probe rewards");
    gpu_test_check(cudaMalloc((void**)&gpu_terminals, sizeof(float)),
                   "allocate sensor probe terminals");
    puf_envs_reset(gpu_envs, gpu_obs, gpu_rewards, gpu_terminals, 1);
    gpu_test_check(cudaDeviceSynchronize(), "synchronize sensor probe reset");
    obs_t gpu_host_obs[OBS_SIZE] = {};
    gpu_test_check(cudaMemcpy(gpu_host_obs, gpu_obs, sizeof(gpu_host_obs),
                              cudaMemcpyDeviceToHost),
                   "copy sensor probe observation");
    float error = max_observation_error(cpu_obs, gpu_host_obs);
    int error_index = 0;
    for (int i = 1; i < OBS_SIZE; i++) {
        if (std::fabs((float)cpu_obs[i] - (float)gpu_host_obs[i]) >
            std::fabs((float)cpu_obs[error_index] -
                      (float)gpu_host_obs[error_index]))
            error_index = i;
    }
    float depth_min = 1.0f, depth_max = 0.0f;
    int occupancy_hits = 0, occupancy_free = 0, occupancy_unknown = 0;
    for (int i = 0; i < DEPTH_MAP_SIZE; i++) {
        float value = gpu_host_obs[DEPTH_MAP_OFFSET + i];
        depth_min = fminf(depth_min, value);
        depth_max = fmaxf(depth_max, value);
    }
    for (int i = 0; i < OCCUPANCY_SIZE; i++) {
        float value = gpu_host_obs[OCCUPANCY_OFFSET + i];
        if (value > 0.75f) occupancy_hits++;
        else if (value > 0.25f) occupancy_free++;
        else occupancy_unknown++;
    }
    std::printf("sensor-probe %-8s error=%.7g index=%d depth=[%.3f,%.3f] "
                "occupancy hit/free/unknown=%d/%d/%d\n", name, (double)error,
                error_index, (double)depth_min, (double)depth_max,
                occupancy_hits, occupancy_free, occupancy_unknown);
    if (error > 1.0e-3f || depth_min >= 1.0f || occupancy_hits == 0 ||
        occupancy_free == 0 || occupancy_unknown == 0) {
        std::fprintf(stderr, "sensor probe failed for %s\n", name);
        std::exit(1);
    }
    puf_envs_close(gpu_envs);
    cudaFree(gpu_obs);
    cudaFree(gpu_rewards);
    cudaFree(gpu_terminals);
    puf_close(&cpu);
    dict_clear(&kwargs);
}

static float sensor_region_error(const obs_t* a, const obs_t* b) {
    float error = 0.0f;
    for (int i = DEPTH_MAP_OFFSET; i < OBS_SIZE; i++) {
        float delta = std::fabs((float)a[i] - (float)b[i]);
        if (delta > error) error = delta;
    }
    return error;
}

static void check_sensor_refresh() {
    Dict fresh_kwargs = {0};
    Dict cached_kwargs = {0};
    for (Dict* kwargs : {&fresh_kwargs, &cached_kwargs}) {
        dict_set(kwargs, "course_mode", COURSE_MODE_FIXED);
        dict_set(kwargs, "course_stage", COURSE_STAGE_CROUCH);
    }
    dict_set(&cached_kwargs, "sensor_depth_interval", 4);
    dict_set(&cached_kwargs, "sensor_occupancy_interval", 8);

    Env* fresh = puf_envs_create(1, &fresh_kwargs);
    Env* cached = puf_envs_create(1, &cached_kwargs);
    obs_t* fresh_obs = nullptr;
    obs_t* cached_obs = nullptr;
    float* fresh_actions = nullptr;
    float* cached_actions = nullptr;
    float* fresh_rewards = nullptr;
    float* cached_rewards = nullptr;
    float* fresh_terminals = nullptr;
    float* cached_terminals = nullptr;
    gpu_test_check(cudaMalloc((void**)&fresh_obs, sizeof(obs_t) * OBS_SIZE),
                   "allocate fresh sensor observations");
    gpu_test_check(cudaMalloc((void**)&cached_obs, sizeof(obs_t) * OBS_SIZE),
                   "allocate cached sensor observations");
    gpu_test_check(cudaMalloc((void**)&fresh_actions, sizeof(float) * NUM_ATNS),
                   "allocate fresh sensor actions");
    gpu_test_check(cudaMalloc((void**)&cached_actions, sizeof(float) * NUM_ATNS),
                   "allocate cached sensor actions");
    gpu_test_check(cudaMalloc((void**)&fresh_rewards, sizeof(float)),
                   "allocate fresh sensor rewards");
    gpu_test_check(cudaMalloc((void**)&cached_rewards, sizeof(float)),
                   "allocate cached sensor rewards");
    gpu_test_check(cudaMalloc((void**)&fresh_terminals, sizeof(float)),
                   "allocate fresh sensor terminals");
    gpu_test_check(cudaMalloc((void**)&cached_terminals, sizeof(float)),
                   "allocate cached sensor terminals");
    puf_envs_reset(fresh, fresh_obs, fresh_rewards, fresh_terminals, 1);
    puf_envs_reset(cached, cached_obs, cached_rewards, cached_terminals, 1);

    const float turn_action[NUM_ATNS] = {4.0f, 1.0f, 1.0f, 0.0f, 0.0f};
    gpu_test_check(cudaMemcpy(fresh_actions, turn_action, sizeof(turn_action),
                              cudaMemcpyHostToDevice),
                   "copy fresh sensor action");
    gpu_test_check(cudaMemcpy(cached_actions, turn_action, sizeof(turn_action),
                              cudaMemcpyHostToDevice),
                   "copy cached sensor action");
    obs_t fresh_host[OBS_SIZE] = {};
    obs_t cached_host[OBS_SIZE] = {};
    float first_error = 0.0f;
    float final_error = 0.0f;
    for (int tick = 1; tick <= 8; tick++) {
        puf_envs_step(fresh, fresh_actions, fresh_obs, fresh_rewards,
                      fresh_terminals, 0, 1, 0);
        puf_envs_step(cached, cached_actions, cached_obs, cached_rewards,
                      cached_terminals, 0, 1, 0);
        gpu_test_check(cudaDeviceSynchronize(), "synchronize sensor refresh step");
        gpu_test_check(cudaMemcpy(fresh_host, fresh_obs, sizeof(fresh_host),
                                  cudaMemcpyDeviceToHost),
                       "copy fresh sensor observation");
        gpu_test_check(cudaMemcpy(cached_host, cached_obs, sizeof(cached_host),
                                  cudaMemcpyDeviceToHost),
                       "copy cached sensor observation");
        float error = sensor_region_error(fresh_host, cached_host);
        if (tick == 1) first_error = error;
        if (tick == 8) final_error = error;
    }
    std::printf("sensor-refresh first_error=%.7g final_error=%.7g\n",
                (double)first_error, (double)final_error);
    if (first_error < 0.05f || final_error > 1.0e-3f) {
        std::fprintf(stderr, "sensor cache refresh probe failed\n");
        std::exit(1);
    }

    puf_envs_close(fresh);
    puf_envs_close(cached);
    cudaFree(fresh_obs);
    cudaFree(cached_obs);
    cudaFree(fresh_actions);
    cudaFree(cached_actions);
    cudaFree(fresh_rewards);
    cudaFree(cached_rewards);
    cudaFree(fresh_terminals);
    cudaFree(cached_terminals);
    dict_clear(&fresh_kwargs);
    dict_clear(&cached_kwargs);
}

__global__ static void sensor_overlap_probe_kernel(S3DGpuSim sim, float* out) {
    if (blockIdx.x == 0 && threadIdx.x == 0) {
        S3DVec origin = s3d_v(1.0f, -0.5f, 0.0f);
        S3DVec direction = s3d_v(1.0f, 0.0f, 0.0f);
        out[0] = s3d_ray_distance(sim, 0, origin, direction, 1.0f);
        out[1] = s3d_horizontal_ray_distance(sim, 0, origin, direction, 1.0f);
    }
}

static void check_sensor_initial_overlap() {
    Dict kwargs = {0};
    dict_set(&kwargs, "course_mode", COURSE_MODE_FIXED);

    Env cpu = {0};
    cpu.rng = 0;
    puf_init(&cpu, &kwargs);
    // The fixed-course floor occupies y[-2, 0]. A non-player origin inside it
    // must be reported as blocked, while a ray beginning in the player body
    // must ignore the player's own shapes.
    float cpu_static = cast_ray_dist(&cpu, 1.0f, -0.5f, 0.0f,
                                     1.0f, 0.0f, 0.0f, 1.0f);
    float cpu_own = cast_ray_dist(&cpu, 0.0f, 1.0f, 0.0f,
                                  1.0f, 0.0f, 0.0f, 1.0f);
    if (cpu_static > 1.0e-6f || cpu_own < 0.999f) {
        std::fprintf(stderr,
                     "CPU sensor initial-overlap probe failed static=%.7g own=%.7g\n",
                     (double)cpu_static, (double)cpu_own);
        std::exit(1);
    }

    Env* gpu_envs = puf_envs_create(1, &kwargs);
    S3DNative* native = s3d_find_native(gpu_envs);
    float* gpu_out = nullptr;
    gpu_test_check(cudaMalloc((void**)&gpu_out, sizeof(float) * 2),
                   "allocate sensor overlap probe");
    sensor_overlap_probe_kernel<<<1, 1>>>(native->sim, gpu_out);
    gpu_test_check(cudaGetLastError(), "launch sensor overlap probe");
    gpu_test_check(cudaDeviceSynchronize(), "synchronize sensor overlap probe");
    float gpu_out_host[2] = {};
    gpu_test_check(cudaMemcpy(gpu_out_host, gpu_out, sizeof(gpu_out_host),
                              cudaMemcpyDeviceToHost),
                   "copy sensor overlap probe");
    if (gpu_out_host[0] > 1.0e-6f || gpu_out_host[1] > 1.0e-6f) {
        std::fprintf(stderr,
                     "GPU sensor initial-overlap probe failed ray=%.7g horizontal=%.7g\n",
                     (double)gpu_out_host[0], (double)gpu_out_host[1]);
        std::exit(1);
    }
    std::printf("shenaniguns3d sensor initial-overlap filtering PASS\n");

    cudaFree(gpu_out);
    puf_envs_close(gpu_envs);
    puf_close(&cpu);
    dict_clear(&kwargs);
}

static void check_jump_penalty_config() {
    Dict kwargs = {0};
    dict_set(&kwargs, "time_cost", 0.25);
    dict_set(&kwargs, "jump_penalty", -0.75);

    Env cpu = {0};
    cpu.rng = 0;
    puf_init(&cpu, &kwargs);
    S3DGpuConfig gpu = s3d_gpu_config(&kwargs);
    if (std::fabs(cpu.jump_penalty + 0.75f) > 1.0e-6f ||
        std::fabs(gpu.jump_penalty + 0.75f) > 1.0e-6f) {
        std::fprintf(stderr, "jump penalty configuration mismatch\n");
        std::exit(1);
    }
    std::printf("shenaniguns3d jump penalty configuration PASS\n");

    puf_close(&cpu);
    dict_clear(&kwargs);
}

int main() {
    check_course_parity();
    check_timeout_trace();
    check_random_reset_parity();
    print_sensor_probe("fixed", COURSE_MODE_FIXED, 1, -1);
    print_sensor_probe("columns", COURSE_MODE_FIXED, 2, COURSE_STAGE_COLUMNS);
    print_sensor_probe("crouch", COURSE_MODE_FIXED, 2, COURSE_STAGE_CROUCH);
    print_sensor_probe("stress", COURSE_MODE_FIXED, 2, COURSE_STAGE_STRESS);
    check_sensor_refresh();
    check_sensor_initial_overlap();
    check_jump_penalty_config();
    Dict kwargs = {0};

    Env cpu = {0};
    cpu.rng = 0;
    puf_init(&cpu, &kwargs);
    obs_t cpu_obs[OBS_SIZE] = {0};
    float cpu_actions[NUM_ATNS] = {2.0f, 1.0f, 1.0f, 0.0f, 0.0f};
    float cpu_reward = 0.0f;
    float cpu_terminal = 0.0f;
    cpu.agents[0].observations = cpu_obs;
    cpu.agents[0].actions = cpu_actions;
    cpu.agents[0].rewards = &cpu_reward;
    cpu.agents[0].terminals = &cpu_terminal;
    puf_reset(&cpu);

    Env* gpu_envs = puf_envs_create(1, &kwargs);
    obs_t* gpu_obs = nullptr;
    float* gpu_actions = nullptr;
    float* gpu_rewards = nullptr;
    float* gpu_terminals = nullptr;
    gpu_test_check(cudaMalloc((void**)&gpu_obs, sizeof(gpu_obs[0]) * OBS_SIZE),
                   "allocate GPU observations");
    gpu_test_check(cudaMalloc((void**)&gpu_actions,
                              sizeof(gpu_actions[0]) * NUM_ATNS),
                   "allocate GPU actions");
    gpu_test_check(cudaMalloc((void**)&gpu_rewards, sizeof(gpu_rewards[0])),
                   "allocate GPU rewards");
    gpu_test_check(cudaMalloc((void**)&gpu_terminals, sizeof(gpu_terminals[0])),
                   "allocate GPU terminals");
    puf_envs_reset(gpu_envs, gpu_obs, gpu_rewards, gpu_terminals, 1);
    gpu_test_check(cudaDeviceSynchronize(), "synchronize GPU reset");
    S3DNative* native = s3d_find_native(gpu_envs);

    obs_t gpu_host_obs[OBS_SIZE] = {0};
    float gpu_reward = 0.0f;
    float gpu_terminal = 0.0f;
    gpu_test_check(cudaMemcpy(gpu_host_obs, gpu_obs,
                              sizeof(gpu_host_obs), cudaMemcpyDeviceToHost),
                   "copy GPU reset observation");
    gpu_test_check(cudaMemcpy(&gpu_reward, gpu_rewards, sizeof(gpu_reward),
                              cudaMemcpyDeviceToHost),
                   "copy GPU reset reward");
    gpu_test_check(cudaMemcpy(&gpu_terminal, gpu_terminals,
                              sizeof(gpu_terminal), cudaMemcpyDeviceToHost),
                   "copy GPU reset terminal");
    assert_finite(gpu_host_obs, gpu_reward, gpu_terminal, 0);
    float reset_error = max_observation_error(cpu_obs, gpu_host_obs);
    if (reset_error > 1.0e-3f) {
        int index = 0;
        for (int i = 1; i < OBS_SIZE; i++) {
            if (std::fabs((float)cpu_obs[i] - (float)gpu_host_obs[i]) >
                std::fabs((float)cpu_obs[index] - (float)gpu_host_obs[index]))
                index = i;
        }
        std::fprintf(stderr,
                     "reset observation drift %.7g at index %d cpu=%.7g gpu=%.7g\n",
                     (double)reset_error, index, (double)cpu_obs[index],
                     (double)gpu_host_obs[index]);
        std::exit(1);
    }

    float max_error = reset_error;
    int max_error_index = 0;
    float max_position_error = 0.0f;
    unsigned char gpu_crouched = 0;
    int num_boxes = 0;
    gpu_test_check(cudaMemcpy(&num_boxes, native->sim.num_boxes,
                              sizeof(num_boxes), cudaMemcpyDeviceToHost),
                   "copy GPU box count");
    AABB host_boxes[MAX_BOXES] = {0};
    gpu_test_check(cudaMemcpy(host_boxes, native->sim.boxes,
                              sizeof(host_boxes), cudaMemcpyDeviceToHost),
                   "copy GPU course boxes");
    for (int step = 0; step < 120; step++) {
        if (step == 12) cpu_actions[1] = 2.0f;
        if (step == 36) cpu_actions[0] = 4.0f;
        if (step == 48) cpu_actions[4] = 1.0f;
        if (step == 64) cpu_actions[4] = 0.0f;
        if (step == 80) cpu_actions[3] = 1.0f;
        if (step == 81) cpu_actions[3] = 0.0f;

        gpu_test_check(cudaMemcpy(gpu_actions, cpu_actions,
                                  sizeof(cpu_actions), cudaMemcpyHostToDevice),
                       "copy GPU action");
        puf_step(&cpu);
        puf_envs_step(gpu_envs, gpu_actions, gpu_obs, gpu_rewards,
                      gpu_terminals, 0, 1, 0);
        gpu_test_check(cudaDeviceSynchronize(), "synchronize GPU step");
        gpu_test_check(cudaMemcpy(gpu_host_obs, gpu_obs,
                                  sizeof(gpu_host_obs), cudaMemcpyDeviceToHost),
                       "copy GPU observation");
        gpu_test_check(cudaMemcpy(&gpu_reward, gpu_rewards, sizeof(gpu_reward),
                                  cudaMemcpyDeviceToHost),
                       "copy GPU reward");
        gpu_test_check(cudaMemcpy(&gpu_terminal, gpu_terminals,
                                  sizeof(gpu_terminal), cudaMemcpyDeviceToHost),
                       "copy GPU terminal");
        assert_finite(gpu_host_obs, gpu_reward, gpu_terminal, step + 1);

        float error = max_observation_error(cpu_obs, gpu_host_obs);
        if (error > max_error) {
            max_error = error;
            max_error_index = 0;
            for (int i = 1; i < OBS_SIZE; i++) {
                if (std::fabs((float)cpu_obs[i] - (float)gpu_host_obs[i]) >
                    std::fabs((float)cpu_obs[max_error_index] -
                              (float)gpu_host_obs[max_error_index]))
                    max_error_index = i;
            }
        }
        float gpu_px = 0.0f, gpu_py = 0.0f, gpu_pz = 0.0f;
        unsigned char gpu_on_ground = 0;
        gpu_test_check(cudaMemcpy(&gpu_px, native->sim.px,
                                  sizeof(gpu_px), cudaMemcpyDeviceToHost),
                       "copy GPU x");
        gpu_test_check(cudaMemcpy(&gpu_py, native->sim.py,
                                  sizeof(gpu_py), cudaMemcpyDeviceToHost),
                       "copy GPU y");
        gpu_test_check(cudaMemcpy(&gpu_pz, native->sim.pz,
                                  sizeof(gpu_pz), cudaMemcpyDeviceToHost),
                       "copy GPU z");
        gpu_test_check(cudaMemcpy(&gpu_crouched, native->sim.crouched,
                                  sizeof(gpu_crouched), cudaMemcpyDeviceToHost),
                       "copy GPU stance");
        gpu_test_check(cudaMemcpy(&gpu_on_ground, native->sim.on_ground,
                                  sizeof(gpu_on_ground), cudaMemcpyDeviceToHost),
                       "copy GPU ground state");
        if ((gpu_crouched != 0) != pd_char_is_crouched(&cpu.ch) ||
            (gpu_on_ground != 0) != cpu.ch.onGround) {
            std::fprintf(stderr, "stance/ground mismatch at step %d\n", step + 1);
            std::exit(1);
        }
        b3Pos cpu_pos = pd_char_feet_position(&cpu.ch);
        float gpu_height = gpu_crouched ? S3D_GPU_CROUCH_HEIGHT :
                                         S3D_GPU_STAND_HEIGHT;
        float gpu_feet_y = gpu_py - gpu_height * 0.5f;
        float position_error = std::sqrt(
            (gpu_px - (float)cpu_pos.x) * (gpu_px - (float)cpu_pos.x) +
            (gpu_feet_y - (float)cpu_pos.y) * (gpu_feet_y - (float)cpu_pos.y) +
            (gpu_pz - (float)cpu_pos.z) * (gpu_pz - (float)cpu_pos.z));
        if (position_error > max_position_error)
            max_position_error = position_error;
        float half_x = S3D_GPU_CAPSULE_RADIUS;
        float half_y = gpu_height * 0.5f;
        for (int b = 0; b < num_boxes; b++) {
            float overlap_x = half_x + host_boxes[b].hx -
                              std::fabs(gpu_px - host_boxes[b].cx);
            float overlap_y = half_y + host_boxes[b].hy -
                              std::fabs(gpu_py - host_boxes[b].cy);
            float overlap_z = half_x + host_boxes[b].hz -
                              std::fabs(gpu_pz - host_boxes[b].cz);
            if (overlap_x > 0.01f && overlap_y > 0.01f && overlap_z > 0.01f) {
                std::fprintf(stderr, "GPU character penetrated box %d at step %d\n",
                             b, step + 1);
                std::exit(1);
            }
        }
        if (gpu_terminal > 0.5f || cpu_terminal > 0.5f) {
            if (gpu_terminal <= 0.5f || cpu_terminal <= 0.5f) {
                std::fprintf(stderr, "terminal mismatch at step %d\n", step + 1);
                std::exit(1);
            }
            break;
        }
    }

    // Move only the goal metadata to the spawn point for a deterministic
    // success transition. The collision geometry remains unchanged, so this
    // isolates goal ordering and reset behavior from movement drift.
    cpu.course.goal_x = 0.0f;
    cpu.course.goal_z = 0.0f;
    cpu.course.goal_y = 0.0f;
    CourseParams goal_course = {};
    gpu_test_check(cudaMemcpy(&goal_course, native->sim.courses,
                              sizeof(goal_course), cudaMemcpyDeviceToHost),
                   "copy goal course");
    goal_course.goal_x = 0.0f;
    goal_course.goal_z = 0.0f;
    goal_course.goal_y = 0.0f;
    gpu_test_check(cudaMemcpy(native->sim.courses, &goal_course,
                              sizeof(goal_course), cudaMemcpyHostToDevice),
                   "set goal course");
    cpu_reward = 0.0f;
    cpu_terminal = 0.0f;
    puf_reset(&cpu);
    puf_envs_reset(gpu_envs, gpu_obs, gpu_rewards, gpu_terminals, 1);
    gpu_test_check(cudaDeviceSynchronize(), "synchronize goal reset");
    std::memset(cpu_actions, 0, sizeof(cpu_actions));
    cpu_actions[0] = 2.0f;
    cpu_actions[1] = 1.0f;
    cpu_actions[2] = 1.0f;
    gpu_test_check(cudaMemcpy(gpu_actions, cpu_actions,
                              sizeof(cpu_actions), cudaMemcpyHostToDevice),
                   "copy goal action");
    puf_step(&cpu);
    puf_envs_step(gpu_envs, gpu_actions, gpu_obs, gpu_rewards,
                  gpu_terminals, 0, 1, 0);
    gpu_test_check(cudaDeviceSynchronize(), "synchronize goal step");
    gpu_test_check(cudaMemcpy(&gpu_reward, gpu_rewards, sizeof(float),
                              cudaMemcpyDeviceToHost),
                   "copy goal reward");
    gpu_test_check(cudaMemcpy(&gpu_terminal, gpu_terminals, sizeof(float),
                              cudaMemcpyDeviceToHost),
                   "copy goal terminal");
    gpu_test_check(cudaMemcpy(gpu_host_obs, gpu_obs,
                              sizeof(gpu_host_obs), cudaMemcpyDeviceToHost),
                   "copy goal observation");
    if (cpu_terminal != 1.0f || gpu_terminal != 1.0f ||
        std::fabs(cpu_reward - gpu_reward) > 1.0e-6f ||
        max_observation_error(cpu_obs, gpu_host_obs) > 1.0e-3f) {
        std::fprintf(stderr, "goal trace mismatch cpu=(%.7g,%.1f) "
                     "gpu=(%.7g,%.1f)\n", (double)cpu_reward,
                     (double)cpu_terminal, (double)gpu_reward,
                     (double)gpu_terminal);
        std::exit(1);
    }
    Log goal_gpu_log = {};
    gpu_test_check(cudaMemcpy(&goal_gpu_log, &native->sim.envs[0].log,
                              sizeof(goal_gpu_log), cudaMemcpyDeviceToHost),
                   "copy goal log");
    if (std::fabs(cpu.log.perf - goal_gpu_log.perf) > 1.0e-6f ||
        std::fabs(cpu.log.episode_return - goal_gpu_log.episode_return) > 1.0e-6f ||
        std::fabs(cpu.log.episode_length - goal_gpu_log.episode_length) > 1.0e-6f ||
        std::fabs(cpu.log.score - goal_gpu_log.score) > 1.0e-6f ||
        std::fabs(cpu.log.n - goal_gpu_log.n) > 1.0e-6f) {
        std::fprintf(stderr, "goal log mismatch\n");
        std::exit(1);
    }
    std::printf("shenaniguns3d GPU goal trace parity PASS\n");

    cpu.course.goal_x = GOAL_X;
    cpu.course.goal_z = GOAL_Z;
    cpu.course.goal_y = GOAL_Y;
    goal_course.goal_x = GOAL_X;
    goal_course.goal_z = GOAL_Z;
    goal_course.goal_y = GOAL_Y;
    gpu_test_check(cudaMemcpy(native->sim.courses, &goal_course,
                              sizeof(goal_course), cudaMemcpyHostToDevice),
                   "restore goal course");

    // Reset from an airborne state. This catches stale jump cooldown and
    // grounded flags, as well as reset APIs that leave old outputs visible.
    puf_reset(&cpu);
    puf_envs_reset(gpu_envs, gpu_obs, gpu_rewards, gpu_terminals, 1);
    gpu_test_check(cudaDeviceSynchronize(), "synchronize reset before jump");
    cpu_reward = 0.0f;
    cpu_terminal = 0.0f;
    std::memset(cpu_actions, 0, sizeof(cpu_actions));
    cpu_actions[0] = 2.0f;
    cpu_actions[1] = 1.0f;
    cpu_actions[2] = 1.0f;
    gpu_test_check(cudaMemcpy(gpu_actions, cpu_actions,
                              sizeof(cpu_actions), cudaMemcpyHostToDevice),
                   "copy landing action");
    puf_step(&cpu);
    puf_envs_step(gpu_envs, gpu_actions, gpu_obs, gpu_rewards,
                  gpu_terminals, 0, 1, 0);
    gpu_test_check(cudaDeviceSynchronize(), "synchronize landing before jump");
    cpu_actions[3] = 1.0f;
    gpu_test_check(cudaMemcpy(gpu_actions, cpu_actions,
                              sizeof(cpu_actions), cudaMemcpyHostToDevice),
                   "copy jump action");
    puf_step(&cpu);
    puf_envs_step(gpu_envs, gpu_actions, gpu_obs, gpu_rewards,
                  gpu_terminals, 0, 1, 0);
    gpu_test_check(cudaDeviceSynchronize(), "synchronize airborne state");

    cpu_reward = 0.0f;
    cpu_terminal = 0.0f;
    puf_reset(&cpu);
    puf_envs_reset(gpu_envs, gpu_obs, gpu_rewards, gpu_terminals, 1);
    gpu_test_check(cudaDeviceSynchronize(), "synchronize airborne reset");
    gpu_test_check(cudaMemcpy(gpu_host_obs, gpu_obs,
                              sizeof(gpu_host_obs), cudaMemcpyDeviceToHost),
                   "copy airborne reset observation");
    gpu_test_check(cudaMemcpy(&gpu_reward, gpu_rewards, sizeof(gpu_reward),
                              cudaMemcpyDeviceToHost),
                   "copy reset reward");
    gpu_test_check(cudaMemcpy(&gpu_terminal, gpu_terminals,
                              sizeof(gpu_terminal), cudaMemcpyDeviceToHost),
                   "copy reset terminal");
    assert_finite(gpu_host_obs, gpu_reward, gpu_terminal, 0);
    if (max_observation_error(cpu_obs, gpu_host_obs) > 1.0e-3f ||
        gpu_reward != 0.0f || gpu_terminal != 0.0f ||
        cpu.ch.onGround || cpu.ch.jumpCooldown != 0.0f) {
        std::fprintf(stderr, "airborne reset state mismatch\n");
        std::exit(1);
    }
    std::printf("shenaniguns3d GPU airborne reset parity PASS\n");

    // Reset while crouched as well. The feet must return to the same spawn
    // height instead of inheriting the stance transition offset.
    cpu_actions[3] = 0.0f;
    cpu_actions[4] = 0.0f;
    gpu_test_check(cudaMemcpy(gpu_actions, cpu_actions,
                              sizeof(cpu_actions), cudaMemcpyHostToDevice),
                   "copy crouch reset landing action");
    puf_step(&cpu);
    puf_envs_step(gpu_envs, gpu_actions, gpu_obs, gpu_rewards,
                  gpu_terminals, 0, 1, 0);
    gpu_test_check(cudaDeviceSynchronize(), "synchronize crouch reset landing");
    cpu_actions[4] = 1.0f;
    gpu_test_check(cudaMemcpy(gpu_actions, cpu_actions,
                              sizeof(cpu_actions), cudaMemcpyHostToDevice),
                   "copy crouch action");
    puf_step(&cpu);
    puf_envs_step(gpu_envs, gpu_actions, gpu_obs, gpu_rewards,
                  gpu_terminals, 0, 1, 0);
    gpu_test_check(cudaDeviceSynchronize(), "synchronize crouch state");
    if (!pd_char_is_crouched(&cpu.ch)) {
        std::fprintf(stderr, "CPU crouch setup failed\n");
        std::exit(1);
    }
    cpu_reward = 0.0f;
    cpu_terminal = 0.0f;
    puf_reset(&cpu);
    puf_envs_reset(gpu_envs, gpu_obs, gpu_rewards, gpu_terminals, 1);
    gpu_test_check(cudaDeviceSynchronize(), "synchronize crouched reset");
    gpu_test_check(cudaMemcpy(gpu_host_obs, gpu_obs,
                              sizeof(gpu_host_obs), cudaMemcpyDeviceToHost),
                   "copy crouched reset observation");
    gpu_test_check(cudaMemcpy(&gpu_crouched, native->sim.crouched,
                              sizeof(gpu_crouched), cudaMemcpyDeviceToHost),
                   "copy crouched reset stance");
    if (pd_char_is_crouched(&cpu.ch) || gpu_crouched != 0 ||
        max_observation_error(cpu_obs, gpu_host_obs) > 1.0e-3f) {
        std::fprintf(stderr, "crouched reset state mismatch\n");
        std::exit(1);
    }
    std::printf("shenaniguns3d GPU crouched reset parity PASS\n");

    if (max_position_error > 0.75f) {
        std::fprintf(stderr, "GPU physics drift %.7g exceeds tolerance\n",
                     (double)max_position_error);
        std::exit(1);
    }

    std::printf("shenaniguns3d GPU differential reset_error=%.7g "
                "max_obs_error=%.7g(index=%d) max_position_error=%.7g PASS\n",
                (double)reset_error, (double)max_error,
                max_error_index, (double)max_position_error);

    puf_envs_close(gpu_envs);
    cudaFree(gpu_obs);
    cudaFree(gpu_actions);
    cudaFree(gpu_rewards);
    cudaFree(gpu_terminals);
    puf_close(&cpu);
    return 0;
}
