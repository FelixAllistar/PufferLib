#define _POSIX_C_SOURCE 200809L

#include "antibody.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_failures = 0;

typedef struct {
    int run_perf;
    int perf_only;
    double perf_seconds;
    int perf_pool;
} AdiosCliArgs;

static uint32_t float_bits(float value) {
    union {
        float f32;
        uint32_t u32;
    } bits;
    bits.f32 = value;
    return bits.u32;
}

static double adios_now_seconds(void) {
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
    for (int i = 0; i < count; i++) {
        if (actual[i] != expected[i]) {
            fprintf(stderr,
                "FAIL: %s[%d] actual=%d expected=%d\n",
                label,
                i,
                actual[i],
                expected[i]);
            g_failures += 1;
            return;
        }
    }
}

static void expect_float_array(const float* actual, const float* expected, int count, const char* label) {
    for (int i = 0; i < count; i++) {
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

static void test_rng(void) {
    AdiosKey key = adios_prng_seed(0u);
    AdiosKey split_keys[3];
    adios_prng_split_n(key, split_keys, 3);

    const uint32_t expected_split[3][2] = {
        {1797259609u, 2579123966u},
        {928981903u, 3453687069u},
        {4146024105u, 2718843009u},
    };
    for (int i = 0; i < 3; i++) {
        char label0[64];
        char label1[64];
        snprintf(label0, sizeof(label0), "split[%d].k0", i);
        snprintf(label1, sizeof(label1), "split[%d].k1", i);
        expect_u32(split_keys[i].k0, expected_split[i][0], label0);
        expect_u32(split_keys[i].k1, expected_split[i][1], label1);
    }

    const float expected_uniform[4] = {
        0.9476670027f,
        0.9785798788f,
        0.3322914839f,
        0.4686684608f,
    };
    float actual_uniform[4];
    for (int i = 0; i < 4; i++) {
        actual_uniform[i] = adios_uniform_f32_at(key, (uint64_t)i);
    }
    expect_float_array(actual_uniform, expected_uniform, 4, "uniform");

    const int32_t expected_randint[8] = {9, 0, 12, 13, 11, 7, 12, 3};
    int32_t actual_randint[8];
    for (int i = 0; i < 8; i++) {
        actual_randint[i] = adios_randint_i32_at(key, (uint64_t)i, 0, 20);
    }
    expect_int_array(actual_randint, expected_randint, 8, "randint");

    const int32_t expected_bernoulli[8] = {0, 0, 0, 0, 0, 1, 0, 0};
    int32_t actual_bernoulli[8];
    for (int i = 0; i < 8; i++) {
        actual_bernoulli[i] = adios_bernoulli_f32_at(key, (uint64_t)i, 0.25f);
    }
    expect_int_array(actual_bernoulli, expected_bernoulli, 8, "bernoulli");

    const float probs[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    expect_int(adios_choice_p(key, probs, 4), 0, "choice_p");
}

static void test_utils_and_binding(Adios* adios) {
    const int32_t expected_viral_target[ADIOS_ANTIBODY_LEN] = {0, 8, 16, 4, 5, 13, 4, 9, 4, 7, 7};
    expect_int_array(adios->viral_target, expected_viral_target, ADIOS_ANTIBODY_LEN, "viral_target");

    char viral_target_str[ADIOS_ANTIBODY_LEN + 1];
    adios_convert_array_to_aa(adios->viral_target, ADIOS_ANTIBODY_LEN, viral_target_str);
    expect_true(strcmp(viral_target_str, "CARLVQLGLYY") == 0, "convert_array_to_aa");

    AdiosBindingResult full_bind = {0};
    adios_bind(adios, &adios->full_pose_set, adios->viral_target, ADIOS_ANTIBODY_LEN, adios->antigen_array, ADIOS_ANTIGEN_LEN, NULL, &full_bind);
    expect_float_close(full_bind.min_value, -92.8199996948f, "full_bind.min");
    expect_int(full_bind.argmin, 1313888, "full_bind.argmin");

    AdiosBindingResult top_bind = {0};
    adios_bind(adios, &adios->top_pose_set, adios->viral_target, ADIOS_ANTIBODY_LEN, adios->antigen_array, ADIOS_ANTIGEN_LEN, NULL, &top_bind);
    expect_float_close(top_bind.min_value, -92.8199996948f, "top_bind.min");
    expect_int(top_bind.argmin, 0, "top_bind.argmin");

    AdiosBindingResult med_bind = {0};
    adios_bind(adios, &adios->med_pose_set, adios->viral_target, ADIOS_ANTIBODY_LEN, adios->antigen_array, ADIOS_ANTIGEN_LEN, NULL, &med_bind);
    expect_float_close(med_bind.min_value, -92.8199996948f, "med_bind.min");
    expect_int(med_bind.argmin, 34999, "med_bind.argmin");
}

static void test_mutate(Adios* adios) {
    int32_t x[3 * ADIOS_ANTIGEN_LEN];
    for (int row = 0; row < 3; row++) {
        memcpy(&x[row * ADIOS_ANTIGEN_LEN], adios->antigen_array, ADIOS_ANTIGEN_LEN * sizeof(int32_t));
    }

    int32_t out[3 * ADIOS_ANTIGEN_LEN];
    adios_mutate(adios_prng_seed(123u), x, 3, ADIOS_ANTIGEN_LEN, 1.0f / (float)ADIOS_ANTIGEN_LEN, out);

    const int32_t expected_prefix[20] = {
        11, 7, 11, 1, 0, 10, 9, 18, 2, 18,
        5, 5, 18, 15, 3, 8, 15, 10, 13, 17,
    };
    expect_int_array(&out[0], expected_prefix, 20, "mutate.row0");
    expect_int_array(&out[ADIOS_ANTIGEN_LEN], expected_prefix, 20, "mutate.row1");
    expect_int_array(&out[2 * ADIOS_ANTIGEN_LEN], expected_prefix, 20, "mutate.row2");
}

static void test_single_shape_run(Adios* adios) {
    AdiosKey seed = adios_prng_seed(10201u);
    AdiosKey split_keys[2];
    adios_prng_split_n(seed, split_keys, 2);

    int32_t antibody[ADIOS_ANTIBODY_LEN];
    const int32_t expected_antibody[ADIOS_ANTIBODY_LEN] = {17, 11, 6, 6, 7, 13, 8, 13, 17, 13, 17};
    for (int i = 0; i < ADIOS_ANTIBODY_LEN; i++) {
        antibody[i] = adios_randint_i32_at(split_keys[0], (uint64_t)i, 0, 20);
    }
    expect_int_array(antibody, expected_antibody, ADIOS_ANTIBODY_LEN, "single_shape.antibody");

    AdiosShapeRunResult result = adios_single_shape_run(
        adios,
        split_keys[1],
        antibody,
        &adios->top_pose_set,
        adios->antigen_array,
        ADIOS_ANTIGEN_LEN,
        adios->viral_target,
        ADIOS_ANTIBODY_LEN,
        adios->antibody_antitarget_array,
        ADIOS_ANTIGEN_LEN,
        8,
        ADIOS_DEFAULT_SHAPE_PARAMS);

    expect_float_close(result.binding_penalty, -73.5100021362f, "single_shape.binding_penalty");
    expect_int(result.ab_t_m_pose_index, 10, "single_shape.bind_index");

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

    expect_float_array(result.ag_performances, expected_ag_performances, 8, "single_shape.ag_performances");
    expect_float_array(result.best_fitness, expected_best_fitness, 8, "single_shape.best_fitness");
    expect_int_array(result.best_members, expected_member_prefix, 32, "single_shape.best_member0_head32");

    int32_t actual_target_poses[8];
    int32_t actual_antitarget_poses[8];
    float actual_target_values[8];
    float actual_antitarget_values[8];
    for (int i = 0; i < 8; i++) {
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

static void fill_benchmark_antigens(Adios* adios, int pool_size) {
    for (int i = 0; i < pool_size; i++) {
        memcpy(&adios->population_a[i * ADIOS_ANTIGEN_LEN], adios->antigen_array, ADIOS_ANTIGEN_LEN * sizeof(int32_t));
    }
    adios_mutate(
        adios_prng_seed(424242u),
        adios->population_a,
        pool_size,
        ADIOS_ANTIGEN_LEN,
        1.0f / (float)ADIOS_ANTIGEN_LEN,
        adios->population_a);
}

static double run_binding_perf(
        Adios* adios,
        const char* name,
        const AdiosPoseSet* pose_set,
        int pool_size,
        double target_seconds) {
    if (pool_size > adios->max_population_size) {
        fprintf(stderr, "perf pool exceeds allocation for %s\n", name);
        return 0.0;
    }

    fill_benchmark_antigens(adios, pool_size);

    volatile double sink = 0.0;
    long long calcs = 0;
    AdiosBindingResult binding = {0};

    for (int i = 0; i < pool_size; i++) {
        adios_bind(
            adios,
            pose_set,
            adios->viral_target,
            ADIOS_ANTIBODY_LEN,
            &adios->population_a[i * ADIOS_ANTIGEN_LEN],
            ADIOS_ANTIGEN_LEN,
            NULL,
            &binding);
        sink += (double)binding.min_value + (double)binding.argmin * 1e-9;
    }

    double start = adios_now_seconds();
    double elapsed = 0.0;
    do {
        for (int i = 0; i < pool_size; i++) {
            adios_bind(
                adios,
                pose_set,
                adios->viral_target,
                ADIOS_ANTIBODY_LEN,
                &adios->population_a[i * ADIOS_ANTIGEN_LEN],
                ADIOS_ANTIGEN_LEN,
                NULL,
                &binding);
            sink += (double)binding.min_value + (double)binding.argmin * 1e-9;
        }
        calcs += pool_size;
        elapsed = adios_now_seconds() - start;
    } while (elapsed < target_seconds);

    double throughput = (double)calcs / elapsed;
    printf(
        "%s binding: %.2f calcs/s (%lld calcs in %.3fs, pose_count=%d, pool=%d, sink=%0.3f)\n",
        name,
        throughput,
        calcs,
        elapsed,
        pose_set->count,
        pool_size,
        sink);
    return throughput;
}

static void run_perf_suite(Adios* adios, double target_seconds, int base_pool) {
    int low_pool = base_pool;
    int med_pool = base_pool;
    int full_pool = base_pool < 16 ? base_pool : 16;

    printf(
        "adios binding perf target=%.2fs low_pool=%d med_pool=%d full_pool=%d\n",
        target_seconds,
        low_pool,
        med_pool,
        full_pool);
    run_binding_perf(adios, "low_res", &adios->top_pose_set, low_pool, target_seconds);
    run_binding_perf(adios, "med_res", &adios->med_pose_set, med_pool, target_seconds);
    run_binding_perf(adios, "full_res", &adios->full_pose_set, full_pool, target_seconds);
}

static void print_usage(const char* argv0) {
    fprintf(stderr,
        "Usage: %s [--perf] [--perf-only] [--perf-seconds N] [--perf-pool N]\n",
        argv0);
}

static AdiosCliArgs parse_args(int argc, char** argv) {
    AdiosCliArgs args;
    args.run_perf = 0;
    args.perf_only = 0;
    args.perf_seconds = 1.0;
    args.perf_pool = 4096;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--perf") == 0) {
            args.run_perf = 1;
        } else if (strcmp(argv[i], "--perf-only") == 0) {
            args.run_perf = 1;
            args.perf_only = 1;
        } else if (strcmp(argv[i], "--perf-seconds") == 0) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                exit(2);
            }
            i += 1;
            args.perf_seconds = atof(argv[i]);
        } else if (strcmp(argv[i], "--perf-pool") == 0) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                exit(2);
            }
            i += 1;
            args.perf_pool = atoi(argv[i]);
        } else {
            print_usage(argv[0]);
            exit(2);
        }
    }

    if (args.perf_pool < 1) {
        args.perf_pool = 1;
    }
    if (args.perf_seconds <= 0.0) {
        args.perf_seconds = 1.0;
    }
    return args;
}

int main(int argc, char** argv) {
    AdiosCliArgs args = parse_args(argc, argv);
    int max_population = args.run_perf && args.perf_pool > 32 ? args.perf_pool : 32;

    Adios* adios = adios_create(max_population, ADIOS_ANTIGEN_LEN, 16);
    if (adios == NULL) {
        fprintf(stderr, "Failed to initialize adios\n");
        return 1;
    }

    adios_init_reference_values(adios);

    if (!args.perf_only) {
        test_rng();
        test_utils_and_binding(adios);
        test_mutate(adios);
        test_single_shape_run(adios);
    }

    if (args.run_perf) {
        run_perf_suite(adios, args.perf_seconds, args.perf_pool);
    }

    adios_free(adios);

    if (g_failures > 0) {
        fprintf(stderr, "adios tests failed: %d\n", g_failures);
        return 1;
    }

    if (!args.run_perf || !args.perf_only) {
        printf("adios tests passed\n");
    }
    return 0;
}
