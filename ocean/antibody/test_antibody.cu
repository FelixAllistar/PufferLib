#define _POSIX_C_SOURCE 200809L

#include "antibody.cu"

typedef struct {
    int run_perf;
    int perf_only;
    double perf_seconds;
    int perf_pool;
} CliArgs;

static int g_failures = 0;

static const float PERF_EXPECTED_MIN[SANITY_QUERIES] = {
    -92.8199996948f,
    -92.8199996948f,
    -92.8199996948f,
    -92.8199996948f,
    -92.8199996948f,
    -92.8199996948f,
    -94.2300033569f,
    -92.8199996948f,
};

static const int32_t PERF_LOW_ARGMIN[SANITY_QUERIES] = {0, 0, 0, 0, 0, 0, 0, 0};

static const int32_t PERF_MED_ARGMIN[SANITY_QUERIES] = {
    34999, 34999, 34999, 34999, 34999, 34999, 34999, 34999,
};

static uint32_t float_bits(float value) {
    union {
        float f32;
        uint32_t u32;
    } bits;
    bits.f32 = value;
    return bits.u32;
}

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

static void expect_true(int condition, const char* label) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", label);
        g_failures += 1;
    }
}

static void expect_int(int actual, int expected, const char* label) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s actual=%d expected=%d\n", label, actual, expected);
        g_failures += 1;
    }
}

static void expect_u32(uint32_t actual, uint32_t expected, const char* label) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s actual=%u expected=%u\n", label, actual, expected);
        g_failures += 1;
    }
}

static void expect_float_close(float actual, float expected, const char* label) {
    float diff = fabsf(actual - expected);
    if (diff > 1e-6f) {
        fprintf(stderr,
            "FAIL: %s actual=%0.9f expected=%0.9f diff=%0.9f bits=(0x%08x vs 0x%08x)\n",
            label,
            actual,
            expected,
            diff,
            float_bits(actual),
            float_bits(expected));
        g_failures += 1;
    }
}

static void expect_int_array(const int32_t* actual, const int32_t* expected, int count, const char* label) {
    int i;
    for (i = 0; i < count; i++) {
        if (actual[i] != expected[i]) {
            fprintf(stderr, "FAIL: %s[%d] actual=%d expected=%d\n", label, i, actual[i], expected[i]);
            g_failures += 1;
            return;
        }
    }
}

static void expect_float_array(const float* actual, const float* expected, int count, const char* label) {
    int i;
    for (i = 0; i < count; i++) {
        float diff = fabsf(actual[i] - expected[i]);
        if (diff > 1e-6f) {
            fprintf(stderr,
                "FAIL: %s[%d] actual=%0.9f expected=%0.9f diff=%0.9f bits=(0x%08x vs 0x%08x)\n",
                label,
                i,
                actual[i],
                expected[i],
                diff,
                float_bits(actual[i]),
                float_bits(expected[i]));
            g_failures += 1;
            return;
        }
    }
}

static void verify_bind_against_jax(
        const char* name,
        const PoseSet* pose_set,
        const int8_t* padded_antibody,
        const int8_t* padded_antigens,
        int pool_size,
        float* out_min,
        int32_t* out_argmin,
        const float* expected_min,
        const int32_t* expected_argmin) {
    int checks = pool_size < SANITY_QUERIES ? pool_size : SANITY_QUERIES;
    float* gpu_min = (float*)malloc((size_t)checks * sizeof(float));
    int32_t* gpu_argmin = (int32_t*)malloc((size_t)checks * sizeof(int32_t));
    int i;

    if (gpu_min == NULL || gpu_argmin == NULL) {
        fprintf(stderr, "failed to allocate bind sanity buffers\n");
        exit(1);
    }

    bind_batch(pose_set, padded_antibody, padded_antigens, checks, out_min, out_argmin);
    CHECK(cudaDeviceSynchronize());
    CHECK(cudaMemcpy(gpu_min, out_min, (size_t)checks * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK(cudaMemcpy(gpu_argmin, out_argmin, (size_t)checks * sizeof(int32_t), cudaMemcpyDeviceToHost));

    for (i = 0; i < checks; i++) {
        if (fabsf(expected_min[i] - gpu_min[i]) > 1e-5f || expected_argmin[i] != gpu_argmin[i]) {
            fprintf(stderr,
                "%s bind mismatch at query %d: expected=(%0.9f,%d) gpu=(%0.9f,%d)\n",
                name,
                i,
                expected_min[i],
                expected_argmin[i],
                gpu_min[i],
                gpu_argmin[i]);
            exit(1);
        }
    }

    printf("%s bind sanity passed for %d queries\n", name, checks);
    free(gpu_min);
    free(gpu_argmin);
}

static void test_rng(void) {
    const uint32_t expected_split[3][2] = {
        {1797259609u, 2579123966u},
        {928981903u, 3453687069u},
        {4146024105u, 2718843009u},
    };
    const float expected_uniform[4] = {
        0.9476670027f,
        0.9785798788f,
        0.3322914839f,
        0.4686684608f,
    };
    const int32_t expected_randint[8] = {9, 0, 12, 13, 11, 7, 12, 3};
    const int32_t expected_bernoulli[8] = {0, 0, 0, 0, 0, 1, 0, 0};
    RngTestOutput host_out;
    int i;

    run_rng_test(&host_out);

    for (i = 0; i < 3; i++) {
        char label0[64];
        char label1[64];
        snprintf(label0, sizeof(label0), "split[%d].k0", i);
        snprintf(label1, sizeof(label1), "split[%d].k1", i);
        expect_u32(host_out.split[i * 2 + 0], expected_split[i][0], label0);
        expect_u32(host_out.split[i * 2 + 1], expected_split[i][1], label1);
    }
    expect_float_array(host_out.uniform, expected_uniform, 4, "uniform");
    expect_int_array(host_out.randint, expected_randint, 8, "randint");
    expect_int_array(host_out.bernoulli, expected_bernoulli, 8, "bernoulli");
    expect_int(host_out.choice, 0, "choice_p");
}

static void test_utils_and_binding(State* state) {
    HostData* data = state->data;
    const int32_t expected_viral_target[ADIOS_ANTIBODY_LEN] = {0, 8, 16, 4, 5, 13, 4, 9, 4, 7, 7};
    float binding_value;
    int32_t binding_index;
    char viral_target_str[ADIOS_ANTIBODY_LEN + 1];

    expect_int_array(data->viral_target, expected_viral_target, ADIOS_ANTIBODY_LEN, "viral_target");
    adios_convert_array_to_aa(data->viral_target, ADIOS_ANTIBODY_LEN, viral_target_str);
    expect_true(strcmp(viral_target_str, "CARLVQLGLYY") == 0, "convert_array_to_aa");

    bind_single(state, &state->full_pose_set, data->viral_target, ADIOS_ANTIBODY_LEN,
        data->antigen_array, ADIOS_ANTIGEN_LEN, &binding_value, &binding_index);
    expect_float_close(binding_value, -92.8199996948f, "full_bind.min");
    expect_int(binding_index, 1313888, "full_bind.argmin");

    bind_single(state, &state->top_pose_set, data->viral_target, ADIOS_ANTIBODY_LEN,
        data->antigen_array, ADIOS_ANTIGEN_LEN, &binding_value, &binding_index);
    expect_float_close(binding_value, -92.8199996948f, "top_bind.min");
    expect_int(binding_index, 0, "top_bind.argmin");

    bind_single(state, &state->med_pose_set, data->viral_target, ADIOS_ANTIBODY_LEN,
        data->antigen_array, ADIOS_ANTIGEN_LEN, &binding_value, &binding_index);
    expect_float_close(binding_value, -92.8199996948f, "med_bind.min");
    expect_int(binding_index, 34999, "med_bind.argmin");
}

static void test_mutate(State* state) {
    const int32_t expected_prefix[20] = {
        11, 7, 11, 1, 0, 10, 9, 18, 2, 18,
        5, 5, 18, 15, 3, 8, 15, 10, 13, 17,
    };
    int8_t host_population[3 * PADDED_ANTIGEN_LEN];
    int32_t actual_row[20];
    int row;
    int col;
    fill_population_from_sequence(host_population, 3, state->data->antigen_array, ADIOS_ANTIGEN_LEN);
    CHECK(cudaMemcpy(state->population_a, host_population, sizeof(host_population), cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(state->single_antigen, host_population, PADDED_ANTIGEN_LEN * sizeof(int8_t), cudaMemcpyHostToDevice));
    mutate_population(adios_prng_seed(123u), state->single_antigen, state->population_a, 3,
        ADIOS_ANTIGEN_LEN, 1.0f / (float)ADIOS_ANTIGEN_LEN);
    CHECK(cudaMemcpy(host_population, state->population_a, sizeof(host_population), cudaMemcpyDeviceToHost));

    for (row = 0; row < 3; row++) {
        char label[32];
        for (col = 0; col < 20; col++) {
            actual_row[col] = (int32_t)host_population[(size_t)row * PADDED_ANTIGEN_LEN + col + 1];
        }
        snprintf(label, sizeof(label), "mutate.row%d", row);
        expect_int_array(actual_row, expected_prefix, 20, label);
    }
}

static void test_single_shape_run(State* state) {
    HostData* data = state->data;
    AdiosKey seed = adios_prng_seed(10201u);
    AdiosKey split_keys[2];
    int32_t antibody[ADIOS_ANTIBODY_LEN];
    const int32_t expected_antibody[ADIOS_ANTIBODY_LEN] = {17, 11, 6, 6, 7, 13, 8, 13, 17, 13, 17};
    int i;
    AdiosShapeRunResult result;
    const float expected_ag_performances[8] = {
        95.3199920654f,
        95.3199920654f,
        96.0399932861f,
        96.0399932861f,
        96.7999954224f,
        97.1200027466f,
        97.1200027466f,
        97.1200027466f,
    };
    const float expected_best_fitness[8] = {
        21.8099899292f,
        21.8099899292f,
        22.5299911499f,
        22.5299911499f,
        23.2899932861f,
        23.6100006104f,
        23.6100006104f,
        23.6100006104f,
    };
    const int32_t expected_target_poses[8] = {0, 0, 0, 0, 107, 119, 119, 119};
    const int32_t expected_antitarget_poses[8] = {10, 10, 10, 10, 10, 10, 10, 10};
    const float expected_target_values[8] = {
        -92.8199996948f,
        -92.8199996948f,
        -93.5400009155f,
        -93.5400009155f,
        -91.9100036621f,
        -93.6400070190f,
        -93.6400070190f,
        -93.6400070190f,
    };
    const float expected_antitarget_values[8] = {
        -71.0100097656f,
        -71.0100097656f,
        -71.0100097656f,
        -71.0100097656f,
        -68.6200103760f,
        -70.0300064087f,
        -70.0300064087f,
        -70.0300064087f,
    };
    const int32_t expected_member_prefix[32] = {
        11, 7, 11, 1, 0, 10, 9, 18,
        2, 18, 5, 5, 18, 15, 3, 8,
        15, 10, 13, 17, 9, 10, 3, 5,
        3, 16, 5, 13, 7, 15, 9, 14,
    };
    int32_t actual_target_poses[8];
    int32_t actual_antitarget_poses[8];
    float actual_target_values[8];
    float actual_antitarget_values[8];

    adios_prng_split_n(seed, split_keys, 2);
    for (i = 0; i < ADIOS_ANTIBODY_LEN; i++) {
        antibody[i] = adios_randint_i32_at(split_keys[0], (uint64_t)i, 0, 20);
    }
    expect_int_array(antibody, expected_antibody, ADIOS_ANTIBODY_LEN, "single_shape.antibody");

    result = single_shape_run(
        state, split_keys[1], antibody, &state->top_pose_set, data->antigen_array,
        ADIOS_ANTIGEN_LEN, data->viral_target, ADIOS_ANTIBODY_LEN,
        data->antibody_antitarget_array, ADIOS_ANTIGEN_LEN, 8, ADIOS_DEFAULT_SHAPE_PARAMS);

    expect_float_close(result.binding_penalty, -73.5100021362f, "single_shape.binding_penalty");
    expect_int(result.ab_t_m_pose_index, 10, "single_shape.bind_index");
    expect_float_array(result.ag_performances, expected_ag_performances, 8, "single_shape.ag_performances");
    expect_float_array(result.best_fitness, expected_best_fitness, 8, "single_shape.best_fitness");
    expect_int_array(result.best_members, expected_member_prefix, 32, "single_shape.best_member0_head32");

    for (i = 0; i < 8; i++) {
        actual_target_poses[i] = result.best_member_extra_info[i].poses_target;
        actual_antitarget_poses[i] = result.best_member_extra_info[i].poses_antitarget;
        actual_target_values[i] = result.best_member_extra_info[i].binding_values_target;
        actual_antitarget_values[i] = result.best_member_extra_info[i].binding_values_antitarget;
    }

    expect_int_array(actual_target_poses, expected_target_poses, 8, "single_shape.target_poses");
    expect_int_array(actual_antitarget_poses, expected_antitarget_poses, 8, "single_shape.antitarget_poses");
    expect_float_array(actual_target_values, expected_target_values, 8, "single_shape.target_values");
    expect_float_array(actual_antitarget_values, expected_antitarget_values, 8, "single_shape.antitarget_values");
}

static double run_bind_benchmark(const char* name, const PoseSet* pose_set,
        const int8_t* padded_antibody, const int8_t* padded_antigens,
        int pool_size, float* out_min, int32_t* out_argmin, double seconds) {
    long long queries = 0;
    double elapsed = 0.0;
    double start;
    int i;
    float* host_min;
    int32_t* host_argmin;
    double sink = 0.0;

    for (i = 0; i < 10; i++) {
        bind_batch(pose_set, padded_antibody, padded_antigens, pool_size, out_min, out_argmin);
    }
    CHECK(cudaDeviceSynchronize());

    start = now_seconds();
    do {
        bind_batch(pose_set, padded_antibody, padded_antigens, pool_size, out_min, out_argmin);
        CHECK(cudaDeviceSynchronize());
        queries += pool_size;
        elapsed = now_seconds() - start;
    } while (elapsed < seconds);

    host_min = (float*)malloc((size_t)pool_size * sizeof(float));
    host_argmin = (int32_t*)malloc((size_t)pool_size * sizeof(int32_t));
    if (host_min == NULL || host_argmin == NULL) {
        fprintf(stderr, "failed to allocate benchmark output buffers\n");
        exit(1);
    }
    CHECK(cudaMemcpy(host_min, out_min, (size_t)pool_size * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK(cudaMemcpy(host_argmin, out_argmin, (size_t)pool_size * sizeof(int32_t), cudaMemcpyDeviceToHost));
    for (i = 0; i < pool_size; i++) {
        sink += (double)host_min[i] + 1e-9 * (double)host_argmin[i];
    }
    free(host_min);
    free(host_argmin);

    printf(
        "%s raw-cuda binding: %.2f queries/s (%.2f pose-checks/s, %lld queries in %.3fs, pose_count=%d, pool=%d, sink=%0.3f)\n",
        name,
        (double)queries / elapsed,
        ((double)queries / elapsed) * (double)pose_set->count,
        queries,
        elapsed,
        pose_set->count,
        pool_size,
        sink);
    return (double)queries / elapsed;
}

static void run_perf(State* state, int pool_size, double seconds) {
    HostData* data = state->data;
    int8_t padded_antibody[PADDED_ANTIBODY_LEN];

    make_padded_sequence(data->viral_target, ADIOS_ANTIBODY_LEN, PADDED_ANTIBODY_LEN, padded_antibody);
    CHECK(cudaMemcpy(state->padded_antibody, padded_antibody, sizeof(padded_antibody), cudaMemcpyHostToDevice));
    load_single_antigen(state, data->antigen_array, ADIOS_ANTIGEN_LEN);
    fill_population_device(state->single_antigen, state->population_a, pool_size);
    mutate_population(
        adios_prng_seed(424242u),
        state->single_antigen,
        state->population_a,
        pool_size,
        ADIOS_ANTIGEN_LEN,
        1.0f / (float)ADIOS_ANTIGEN_LEN);

    verify_bind_against_jax(
        "low_res",
        &state->top_pose_set,
        state->padded_antibody,
        state->population_a,
        pool_size,
        state->target_binding_values,
        state->target_pose_indices,
        PERF_EXPECTED_MIN,
        PERF_LOW_ARGMIN);
    verify_bind_against_jax(
        "med_res",
        &state->med_pose_set,
        state->padded_antibody,
        state->population_a,
        pool_size,
        state->target_binding_values,
        state->target_pose_indices,
        PERF_EXPECTED_MIN,
        PERF_MED_ARGMIN);

    printf("perf target=%.2fs pool=%d\n", seconds, pool_size);
    run_bind_benchmark(
        "low_res",
        &state->top_pose_set,
        state->padded_antibody,
        state->population_a,
        pool_size,
        state->target_binding_values,
        state->target_pose_indices,
        seconds);
    run_bind_benchmark(
        "med_res",
        &state->med_pose_set,
        state->padded_antibody,
        state->population_a,
        pool_size,
        state->target_binding_values,
        state->target_pose_indices,
        seconds);
}

static CliArgs parse_args(int argc, char** argv) {
    CliArgs args;
    int i;
    args.run_perf = 0;
    args.perf_only = 0;
    args.perf_seconds = 3.0;
    args.perf_pool = 2048;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--perf") == 0) {
            args.run_perf = 1;
        } else if (strcmp(argv[i], "--perf-only") == 0) {
            args.run_perf = 1;
            args.perf_only = 1;
        } else if (strcmp(argv[i], "--perf-seconds") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "missing value for --perf-seconds\n");
                exit(2);
            }
            args.perf_seconds = atof(argv[++i]);
        } else if (strcmp(argv[i], "--perf-pool") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "missing value for --perf-pool\n");
                exit(2);
            }
            args.perf_pool = atoi(argv[++i]);
        } else {
            fprintf(stderr, "usage: %s [--perf] [--perf-only] [--perf-seconds T] [--perf-pool N]\n", argv[0]);
            exit(2);
        }
    }

    if (args.perf_pool < 1) {
        args.perf_pool = 1;
    }
    if (args.perf_seconds <= 0.0) {
        args.perf_seconds = 3.0;
    }
    return args;
}

int main(int argc, char** argv) {
    CliArgs args = parse_args(argc, argv);
    State* state;
    CHECK(cudaSetDevice(0));

    state = make_state(args.perf_pool > 32 ? args.perf_pool : 32, 16);
    if (state == NULL) {
        fprintf(stderr, "failed to create state\n");
        return 1;
    }

    if (!args.perf_only) {
        test_rng();
        test_utils_and_binding(state);
        test_mutate(state);
        test_single_shape_run(state);
    }
    if (args.run_perf) {
        run_perf(state, args.perf_pool, args.perf_seconds);
    }

    free_state(state);

    if (g_failures > 0) {
        fprintf(stderr, "tests failed: %d\n", g_failures);
        return 1;
    }
    if (!args.run_perf || !args.perf_only) {
        printf("tests passed\n");
    }
    return 0;
}
