// Shenanigans 3D standalone viewer (human play + policy watch).
//
// Build from repo root:
//   ./build.sh shenaniguns3d --fast
//
// Usage (run from repo root so config/ + checkpoints/ resolve):
//   ./shenaniguns3d                 # human play
//   ./shenaniguns3d play
//   ./shenaniguns3d watch           # latest checkpoint under checkpoints/shenaniguns3d/
//   ./shenaniguns3d watch latest
//   ./shenaniguns3d watch PATH.bin [--deterministic]
//   ./shenaniguns3d --eval [latest|PATH.bin] [episodes] [--deterministic] [--trace]
//
// Controls (human play):
//   W/S fwd/back, A/D strafe, arrows turn, Space jump, C crouch, R restart
// Watch camera: hold left/right mouse and drag to orbit, wheel zooms, F cycles
// camera modes.

#include "shenaniguns3d.h"
#include "puffercpu.h"

#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

static const char* S3D_ENV_NAME = "shenaniguns3d";
static int s3d_expected_policy_floats(int hidden, int layers);

// Keep the CPU viewer's inference graph identical to the native encoder. The
// trainable implementation lives in src/shenaniguns3d.cu; these constants are
// the shared observation/weight contract for standalone evaluation.
#define S3D_CPU_SCALAR_HIDDEN 32
#define S3D_CPU_DEPTH_CHANNELS 8
#define S3D_CPU_DEPTH_KERNEL 3
#define S3D_CPU_DEPTH_OUT_HEIGHT (DEPTH_MAP_HEIGHT - S3D_CPU_DEPTH_KERNEL + 1)
#define S3D_CPU_DEPTH_OUT_WIDTH DEPTH_MAP_WIDTH
#define S3D_CPU_DEPTH_FEATURES \
    (S3D_CPU_DEPTH_CHANNELS * S3D_CPU_DEPTH_OUT_HEIGHT * S3D_CPU_DEPTH_OUT_WIDTH)
#define S3D_CPU_OCC_CHANNELS 4
#define S3D_CPU_OCC_BIAS_STORAGE 8
#define S3D_CPU_OCC_KERNEL_VERTICAL 2
#define S3D_CPU_OCC_KERNEL_LATERAL 2
#define S3D_CPU_OCC_KERNEL_FORWARD 2
#define S3D_CPU_OCC_OUT_VERTICAL \
    (OCCUPANCY_VERTICAL_BINS - S3D_CPU_OCC_KERNEL_VERTICAL + 1)
#define S3D_CPU_OCC_OUT_LATERAL \
    (OCCUPANCY_LATERAL_BINS - S3D_CPU_OCC_KERNEL_LATERAL + 1)
#define S3D_CPU_OCC_OUT_FORWARD \
    (OCCUPANCY_FORWARD_BINS - S3D_CPU_OCC_KERNEL_FORWARD + 1)
#define S3D_CPU_OCC_FEATURES \
    (S3D_CPU_OCC_CHANNELS * S3D_CPU_OCC_OUT_VERTICAL * \
     S3D_CPU_OCC_OUT_LATERAL * S3D_CPU_OCC_OUT_FORWARD)
#define S3D_CPU_CONCAT \
    (S3D_CPU_SCALAR_HIDDEN + S3D_CPU_DEPTH_FEATURES + S3D_CPU_OCC_FEATURES)

typedef struct S3DPolicy S3DPolicy;
struct S3DPolicy {
    PufferNet* base;
    Weights* weights;
    int hidden;
    float *scalar1_w, *scalar1_b;
    float *scalar2_w, *scalar2_b;
    float *depth_w, *depth_b;
    float *occupancy_w, *occupancy_b;
    float *proj_w, *proj_b;
    float *scalar1_out, *scalar_out, *depth_out, *occupancy_out;
    float *concat, *encoder_out;
};

// ---------------------------------------------------------------------------
// Checkpoint discovery (newest .bin under checkpoints/shenaniguns3d/)
// ---------------------------------------------------------------------------

static void s3d_find_latest(const char* dir, char* out, size_t out_size,
                            time_t* best_time, long long expected_bytes) {
    DIR* dp = opendir(dir);
    if (!dp) return;

    struct dirent* ent = NULL;
    while ((ent = readdir(dp))) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);

        struct stat st;
        if (stat(path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            s3d_find_latest(path, out, out_size, best_time, expected_bytes);
        } else if (S_ISREG(st.st_mode)) {
            size_t n = strlen(path);
            if (n > 4 && strcmp(path + n - 4, ".bin") == 0 &&
                strcmp(ent->d_name, "0000000000000000.bin") != 0 &&
                (expected_bytes <= 0 || (long long)st.st_size == expected_bytes) &&
                st.st_ctime >= *best_time) {
                *best_time = st.st_ctime;
                snprintf(out, out_size, "%s", path);
            }
        }
    }
    closedir(dp);
}

static int s3d_resolve_model(const char* arg, char* out, size_t out_size,
                             long long expected_bytes) {
    if (!arg || !*arg || strcmp(arg, "latest") == 0) {
        char root[2048];
        Ini ini = { 0 };
        puf_ini_load_env(&ini, S3D_ENV_NAME, 0, NULL);
        snprintf(root, sizeof(root), "%s/%s",
                 puf_ini_get_str(&ini, "base", "checkpoint_dir"), S3D_ENV_NAME);
        puf_ini_free(&ini);
        out[0] = 0;
        time_t best = 0;
        s3d_find_latest(root, out, out_size, &best, expected_bytes);
        if (!out[0]) {
            if (expected_bytes > 0) {
                fprintf(stderr,
                        "no compatible checkpoint found in %s (expected %lld bytes); "
                        "the new-observation training run has not saved one yet\n",
                        root, expected_bytes);
            } else {
                fprintf(stderr, "no .bin checkpoints found in %s\n", root);
            }
            return 1;
        }
        return 0;
    }
    snprintf(out, out_size, "%s", arg);
    return 0;
}

// ---------------------------------------------------------------------------
// Policy loading (arch comes from config/<env>.ini [policy])
// ---------------------------------------------------------------------------

static void s3d_watch_arch(int* hidden, int* layers) {
    Ini ini = { 0 };
    puf_ini_load_env(&ini, S3D_ENV_NAME, 0, NULL);
    *hidden = puf_ini_get_int(&ini, "policy", "hidden_size");
    *layers = puf_ini_get_int(&ini, "policy", "num_layers");
    puf_ini_free(&ini);
}

static void s3d_init_from_config(Shenanigans3D* env) {
    Ini ini = { 0 };
    puf_ini_load_env(&ini, S3D_ENV_NAME, 0, NULL);
    puf_init(env, puf_ini_section(&ini, "env", 0));
    puf_ini_free(&ini);
}

static int s3d_eval_deterministic_from_config(void) {
    Ini ini = { 0 };
    puf_ini_load_env(&ini, S3D_ENV_NAME, 0, NULL);
    int deterministic = puf_ini_get_int(&ini, "base", "eval_deterministic") != 0;
    puf_ini_free(&ini);
    return deterministic;
}

static unsigned int s3d_runtime_seed(void) {
    return (unsigned int)time(NULL) ^ (unsigned int)clock();
}

static void s3d_print_course(const char* label, const Shenanigans3D* env) {
    if (env->course.column_count > 0) {
        fprintf(stderr,
                "course %s columns=%d goal=(%.2f, %.2f)\n",
                label, env->course.column_count,
                (double)env->course.goal_x, (double)env->course.goal_z);
        fprintf(stderr, "  obstacles:");
        for (int i = 0; i < env->course.column_count; i++) {
            const CourseColumn* c = &env->course.columns[i];
            fprintf(stderr, " (%.1f,%.1f r%.1f h%.1f)",
                    (double)c->x, (double)c->z,
                    (double)c->radius, (double)c->height);
        }
        fprintf(stderr, "\n");
        return;
    }
    if (env->course.room_count > 0) {
        fprintf(stderr,
                "course %s hallway rooms=%d doors=%d goal=(%.2f, %.2f) "
                "ceiling_room=%d\n",
                label, env->course.room_count, env->course.room_count - 1,
                (double)env->course.goal_x, (double)env->course.goal_z,
                env->course.ceiling_room);
        fprintf(stderr, "  route:");
        for (int i = 0; i < env->course.room_count; i++)
            fprintf(stderr, " (%.0f,%.0f)", (double)env->course.rooms[i].x,
                    (double)env->course.rooms[i].z);
        fprintf(stderr, "\n  doors:");
        for (int i = 0; i + 1 < env->course.room_count; i++) {
            const CourseDoor* d = &env->course.route_doors[i];
            fprintf(stderr, " (%s %.1f,%.1f w%.1f k%d)",
                    d->axis == 0 ? "X" : "Z", (double)d->x, (double)d->z,
                    (double)d->width, d->kind);
        }
        fprintf(stderr, "\n");
        return;
    }
    fprintf(stderr,
            "course %s doors(z=%.2f/%.2f/%.2f) pit(x=%.2f d=%.2f) "
            "tunnel(%.2f..%.2f c=%.2f) hole(%.2f..%.2f)\n",
            label, (double)env->course.doors[0].z,
            (double)env->course.doors[1].z, (double)env->course.doors[2].z,
            (double)env->course.jump_x, (double)env->course.pit_depth,
            (double)env->course.tunnel_start, (double)env->course.tunnel_end,
            (double)env->course.tunnel_clearance,
            (double)env->course.hole_start, (double)env->course.hole_end);
}

static int s3d_expected_policy_floats(int hidden, int layers) {
    int act_sizes[] = ACT_SIZES;
    int atn_sum = 0;
    for (int i = 0; i < NUM_ATNS; i++) atn_sum += act_sizes[i];
    int encoder =
        S3D_CPU_SCALAR_HIDDEN * OBS_SCALAR_SIZE + S3D_CPU_SCALAR_HIDDEN +
        S3D_CPU_SCALAR_HIDDEN * S3D_CPU_SCALAR_HIDDEN + S3D_CPU_SCALAR_HIDDEN +
        S3D_CPU_DEPTH_CHANNELS * S3D_CPU_DEPTH_KERNEL * S3D_CPU_DEPTH_KERNEL +
        S3D_CPU_DEPTH_CHANNELS +
        S3D_CPU_OCC_CHANNELS * S3D_CPU_OCC_KERNEL_VERTICAL *
            S3D_CPU_OCC_KERNEL_LATERAL * S3D_CPU_OCC_KERNEL_FORWARD +
        S3D_CPU_OCC_BIAS_STORAGE + hidden * S3D_CPU_CONCAT + hidden;
    // spatial encoder + decoder (hidden->atn_sum+value) + mingru stack
    return encoder + (atn_sum + 1) * hidden + layers * 3 * hidden * hidden;
}

static void s3d_reset_mingru(S3DPolicy* net) {
    if (!net || !net->base || !net->base->mingru ||
            !net->base->mingru->state) return;
    size_t count = (size_t)net->base->mingru->num_layers *
                   net->base->mingru->batch_size * net->base->mingru->hidden_size;
    memset(net->base->mingru->state, 0, count * sizeof(float));
}

static void s3d_free_policy(S3DPolicy* net) {
    if (!net) return;
    free(net->scalar1_out);
    free(net->scalar_out);
    free(net->depth_out);
    free(net->occupancy_out);
    free(net->concat);
    free(net->encoder_out);
    free_puffernet(net->base);
    free(net->weights);
    free(net);
}

static S3DPolicy* s3d_load_policy(const char* path) {
    Weights* weights = load_weights(path);
    if (!weights) {
        fprintf(stderr, "failed to load weights: %s\n", path);
        return NULL;
    }

    int hidden = 64, layers = 2;
    s3d_watch_arch(&hidden, &layers);

    int act_sizes[] = ACT_SIZES;
    int atn_sum = 0;
    for (int i = 0; i < NUM_ATNS; i++) atn_sum += act_sizes[i];
    if (hidden <= 0 || hidden % 8 != 0) {
        fprintf(stderr, "shenaniguns3d CPU loader: hidden size %d must be a positive multiple of 8\n",
                hidden);
        free(weights);
        return NULL;
    }
    int expected = s3d_expected_policy_floats(hidden, layers);
    fprintf(stderr, "watch: %s  (hidden=%d layers=%d)\n", path, hidden, layers);
    if ((long long)(weights->size - 7) != expected) {
        fprintf(stderr,
                "checkpoint/model mismatch: expected %d floats, file has %d\n",
                expected, weights->size - 7);
        free(weights);
        return NULL;
    }

    S3DPolicy* net = (S3DPolicy*)calloc(1, sizeof(S3DPolicy));
    net->base = (PufferNet*)calloc(1, sizeof(PufferNet));
    net->weights = weights;
    net->hidden = hidden;
    net->base->num_agents = 1;
    net->base->obs = (float*)calloc(OBS_SIZE, sizeof(float));
    net->base->num_actions = NUM_ATNS;
    net->base->is_continuous = 0;

    net->scalar1_w = get_weights_aligned(weights,
        S3D_CPU_SCALAR_HIDDEN * OBS_SCALAR_SIZE);
    net->scalar1_b = get_weights_aligned(weights, S3D_CPU_SCALAR_HIDDEN);
    net->scalar2_w = get_weights_aligned(weights,
        S3D_CPU_SCALAR_HIDDEN * S3D_CPU_SCALAR_HIDDEN);
    net->scalar2_b = get_weights_aligned(weights, S3D_CPU_SCALAR_HIDDEN);
    net->depth_w = get_weights_aligned(weights,
        S3D_CPU_DEPTH_CHANNELS * S3D_CPU_DEPTH_KERNEL * S3D_CPU_DEPTH_KERNEL);
    net->depth_b = get_weights_aligned(weights, S3D_CPU_DEPTH_CHANNELS);
    net->occupancy_w = get_weights_aligned(weights,
        S3D_CPU_OCC_CHANNELS * S3D_CPU_OCC_KERNEL_VERTICAL *
        S3D_CPU_OCC_KERNEL_LATERAL * S3D_CPU_OCC_KERNEL_FORWARD);
    net->occupancy_b = get_weights_aligned(weights, S3D_CPU_OCC_BIAS_STORAGE);
    net->proj_w = get_weights_aligned(weights, hidden * S3D_CPU_CONCAT);
    net->proj_b = get_weights_aligned(weights, hidden);

    net->base->decoder = make_linear(weights, 1, hidden, atn_sum + 1);
    net->base->mingru = make_mingru(weights, 1, hidden, layers);
    net->base->multidiscrete = make_multidiscrete(1, act_sizes, NUM_ATNS);
    net->scalar1_out = (float*)calloc(S3D_CPU_SCALAR_HIDDEN, sizeof(float));
    net->scalar_out = (float*)calloc(S3D_CPU_SCALAR_HIDDEN, sizeof(float));
    net->depth_out = (float*)calloc(S3D_CPU_DEPTH_FEATURES, sizeof(float));
    net->occupancy_out = (float*)calloc(S3D_CPU_OCC_FEATURES, sizeof(float));
    net->concat = (float*)calloc(S3D_CPU_CONCAT, sizeof(float));
    net->encoder_out = (float*)calloc((size_t)hidden, sizeof(float));
    if (weights->idx != expected) {
        fprintf(stderr,
                "checkpoint/model mismatch: expected %d floats for hidden=%d "
                "layers=%d, loader consumed %d\n",
                expected, hidden, layers, weights->idx);
        s3d_free_policy(net);
        return NULL;
    }
    return net;
}

static void s3d_cpu_forward(S3DPolicy* net, const float* observations) {
    int hidden = net->hidden;
    const float* input = observations;

    for (int o = 0; o < S3D_CPU_SCALAR_HIDDEN; o++) {
        float sum = net->scalar1_b[o];
        for (int i = 0; i < OBS_SCALAR_SIZE; i++)
            sum += input[i] * net->scalar1_w[o * OBS_SCALAR_SIZE + i];
        net->scalar1_out[o] = fmaxf(0.0f, sum);
    }
    for (int o = 0; o < S3D_CPU_SCALAR_HIDDEN; o++) {
        float sum = net->scalar2_b[o];
        for (int i = 0; i < S3D_CPU_SCALAR_HIDDEN; i++)
            sum += net->scalar1_out[i] *
                   net->scalar2_w[o * S3D_CPU_SCALAR_HIDDEN + i];
        net->scalar_out[o] = fmaxf(0.0f, sum);
    }

    for (int oc = 0; oc < S3D_CPU_DEPTH_CHANNELS; oc++) {
        for (int oh = 0; oh < S3D_CPU_DEPTH_OUT_HEIGHT; oh++) {
            for (int ow = 0; ow < S3D_CPU_DEPTH_OUT_WIDTH; ow++) {
                float sum = net->depth_b[oc];
                for (int kh = 0; kh < S3D_CPU_DEPTH_KERNEL; kh++) {
                    for (int kw = 0; kw < S3D_CPU_DEPTH_KERNEL; kw++) {
                        int iw = (ow + kw - S3D_CPU_DEPTH_KERNEL / 2) %
                                 DEPTH_MAP_WIDTH;
                        if (iw < 0) iw += DEPTH_MAP_WIDTH;
                        sum += input[DEPTH_MAP_OFFSET +
                                     (oh + kh) * DEPTH_MAP_WIDTH + iw] *
                               net->depth_w[(oc * S3D_CPU_DEPTH_KERNEL + kh) *
                                            S3D_CPU_DEPTH_KERNEL + kw];
                    }
                }
                int out_idx = ((oc * S3D_CPU_DEPTH_OUT_HEIGHT + oh) *
                               S3D_CPU_DEPTH_OUT_WIDTH + ow);
                net->depth_out[out_idx] = fmaxf(0.0f, sum);
            }
        }
    }

    for (int oc = 0; oc < S3D_CPU_OCC_CHANNELS; oc++) {
        for (int ov = 0; ov < S3D_CPU_OCC_OUT_VERTICAL; ov++) {
            for (int ol = 0; ol < S3D_CPU_OCC_OUT_LATERAL; ol++) {
                for (int of = 0; of < S3D_CPU_OCC_OUT_FORWARD; of++) {
                    float sum = net->occupancy_b[oc];
                    for (int kv = 0; kv < S3D_CPU_OCC_KERNEL_VERTICAL; kv++) {
                        for (int kl = 0; kl < S3D_CPU_OCC_KERNEL_LATERAL; kl++) {
                            for (int kf = 0; kf < S3D_CPU_OCC_KERNEL_FORWARD; kf++) {
                                int in_idx = ((ov + kv) * OCCUPANCY_LATERAL_BINS +
                                              ol + kl) * OCCUPANCY_FORWARD_BINS +
                                             of + kf;
                                int w_idx = (kv * S3D_CPU_OCC_KERNEL_LATERAL + kl) *
                                            S3D_CPU_OCC_KERNEL_FORWARD + kf;
                                sum += input[OCCUPANCY_OFFSET + in_idx] *
                                       net->occupancy_w[oc *
                                           S3D_CPU_OCC_KERNEL_VERTICAL *
                                           S3D_CPU_OCC_KERNEL_LATERAL *
                                           S3D_CPU_OCC_KERNEL_FORWARD + w_idx];
                            }
                        }
                    }
                    int out_idx = (((oc * S3D_CPU_OCC_OUT_VERTICAL + ov) *
                                    S3D_CPU_OCC_OUT_LATERAL + ol) *
                                   S3D_CPU_OCC_OUT_FORWARD + of);
                    net->occupancy_out[out_idx] = fmaxf(0.0f, sum);
                }
            }
        }
    }

    memcpy(net->concat, net->scalar_out,
           S3D_CPU_SCALAR_HIDDEN * sizeof(float));
    memcpy(net->concat + S3D_CPU_SCALAR_HIDDEN, net->depth_out,
           S3D_CPU_DEPTH_FEATURES * sizeof(float));
    memcpy(net->concat + S3D_CPU_SCALAR_HIDDEN + S3D_CPU_DEPTH_FEATURES,
           net->occupancy_out, S3D_CPU_OCC_FEATURES * sizeof(float));
    for (int o = 0; o < hidden; o++) {
        float sum = net->proj_b[o];
        for (int c = 0; c < S3D_CPU_CONCAT; c++)
            sum += net->concat[c] * net->proj_w[o * S3D_CPU_CONCAT + c];
        net->encoder_out[o] = fmaxf(0.0f, sum);
    }
    mingru(net->base->mingru, net->encoder_out);
    linear(net->base->decoder, net->base->mingru->output);
}

// Stochastic watch: sample each discrete head from softmaxed logits.
static void s3d_sample_policy(S3DPolicy* net, unsigned int* rng, float* actions) {
    float* logits = net->base->decoder->output; // [atn_sum][..., +value]
    int act_sizes[] = ACT_SIZES;
    int off = 0;
    for (int h = 0; h < NUM_ATNS; h++) {
        float maxl = -INFINITY;
        for (int k = 0; k < act_sizes[h]; k++)
            if (logits[off + k] > maxl) maxl = logits[off + k];
        float sum = 0.0f;
        for (int k = 0; k < act_sizes[h]; k++) {
            logits[off + k] = expf(logits[off + k] - maxl);
            sum += logits[off + k];
        }
        float r = ((float)rand_r(rng) / ((float)RAND_MAX + 1.0f)) * sum;
        float acc = 0.0f;
        int pick = act_sizes[h] - 1;
        for (int k = 0; k < act_sizes[h]; k++) {
            acc += logits[off + k];
            if (r <= acc) { pick = k; break; }
        }
        actions[h] = (float)pick;
        off += act_sizes[h];
    }
}

static void s3d_print_usage(const char* argv0) {
    fprintf(stderr,
        "usage:\n"
        "  %s                          # human play\n"
        "  %s play\n"
        "  %s watch [latest|PATH.bin] [--deterministic]\n"
        "  %s --eval [latest|PATH.bin] [episodes] [--deterministic] [--trace]\n"
        "  %s --check                  # scripted solvability run\n"
        "  %s --random-check           # randomized course audit\n"
         "  %s --sense-check            # depth/occupancy ray sensor audit\n"
        "  %s --bench                  # headless steps/sec\n",
        argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0);
}

// ---------------------------------------------------------------------------
// Modes
// ---------------------------------------------------------------------------

// Collision audit: try to phase through two walls. Each probe walks straight
// into geometry that should block. PASS = position clamped at the expected
// face; FAIL = we ended up past it.
static void collision_audit() {
    struct Probe {
        const char* name;
        float x, z, yaw;
        float blockedAt, pastX;
    };
    struct Probe probes[] = {
        // stand-walk into crouch tunnel ceiling face (x=12)
        { "tunnel-ceiling (stand)", 10.0f, 0.0f, 0.0f, 11.80f, 12.10f },
        // walk into doorway side jamb at z=1.0 (face x~5.6)
        { "doorway-jamb", 4.0f, 1.0f, 0.0f, 5.60f, 6.40f },
    };

    for (int pi = 0; pi < 2; pi++) {
        struct Probe* pr = &probes[pi];
        Shenanigans3D env = { .max_ticks = 600, .rng = 3 };
        allocate_env(&env);
        obs_t observations[OBS_SIZE] = { 0 };
        float actions[NUM_ATNS] = { 0 };
        float rewards[1] = { 0 };
        float terminals[1] = { 0 };
        env.agents[0].observations = observations;
        env.agents[0].actions = actions;
        env.agents[0].rewards = rewards;
        env.agents[0].terminals = terminals;
        env.agents[0].action_mask = NULL;
        env.agents[0].policy = 0;
        puf_reset(&env);
        b3Body_SetTransform(env.ch.body, (b3Pos){ pr->x, 1.0f, pr->z }, b3Quat_identity);
        b3Body_SetLinearVelocity(env.ch.body, (b3Vec3){ 0, 0, 0 });
        env.yaw = pr->yaw;

        for (int tick = 0; tick < 300; tick++) {
            actions[0] = 2.0f;
            actions[1] = 2.0f;
            actions[2] = 1.0f;
            actions[3] = 0.0f;
            actions[4] = 0.0f;
            puf_step(&env);
        }

        float fx = feet_x(&env);
        bool blocked = fx < pr->blockedAt + 0.15f;
        bool phased = fx > pr->pastX;
        printf("%-24s final=(%7.3f, %5.2f, %6.3f)  %s\n", pr->name, (double)fx,
               (double)feet_y(&env), (double)feet_z(&env),
               phased ? "PHASED!! (FAIL)" : (blocked ? "blocked (PASS)" : "no progress??"));
        puf_close(&env);
    }
}

static void performance_test() {
    long testTime = 10;
    Shenanigans3D env = {
        .max_ticks = 100000,
        .rng = 42,
        .course_mode = COURSE_MODE_RANDOM_EVERY_RESET,
        .course_difficulty = 3,
    };
    allocate_env(&env);
    obs_t observations[OBS_SIZE] = { 0 };
    float actions[NUM_ATNS] = { 0 };
    float rewards[1] = { 0 };
    float terminals[1] = { 0 };
    env.agents[0].observations = observations;
    env.agents[0].actions = actions;
    env.agents[0].rewards = rewards;
    env.agents[0].terminals = terminals;
    env.agents[0].action_mask = NULL;
    env.agents[0].policy = 0;
    puf_reset(&env);

    long start = time(NULL);
    long steps = 0;
    while (time(NULL) - start < testTime) {
        actions[0] = rand_r(&env.rng) % 5;
        actions[1] = rand_r(&env.rng) % 3;
        actions[2] = rand_r(&env.rng) % 3;
        actions[3] = (rand_r(&env.rng) % 16) == 0 ? 1.0f : 0.0f;
        actions[4] = (rand_r(&env.rng) % 32) == 0 ? 1.0f : 0.0f;
        puf_step(&env);
        steps++;
    }
    printf("SPS: %ld\n", steps / testTime);
    printf("perf=%.1f score=%.1f n=%.0f\n", (double)env.log.perf,
           (double)env.log.score, (double)env.log.n);
    puf_close(&env);
}

static void sensor_audit() {
    Shenanigans3D env = {
        .max_ticks = 600,
        .rng = 17,
        .course_mode = COURSE_MODE_RANDOM,
        .course_difficulty = 3,
    };
    allocate_env(&env);
    obs_t observations[OBS_SIZE] = { 0 };
    float actions[NUM_ATNS] = { 0 };
    float rewards[1] = { 0 };
    float terminals[1] = { 0 };
    env.agents[0].observations = observations;
    env.agents[0].actions = actions;
    env.agents[0].rewards = rewards;
    env.agents[0].terminals = terminals;
    env.agents[0].action_mask = NULL;
    env.agents[0].policy = 0;
    puf_reset(&env);

    float depth_min = 1.0f, depth_max = 0.0f;
    int occupancy_hits = 0, occupancy_free = 0, occupancy_unknown = 0;
    for (int i = 0; i < DEPTH_MAP_SIZE; i++) {
        depth_min = fminf(depth_min, observations[DEPTH_MAP_OFFSET + i]);
        depth_max = fmaxf(depth_max, observations[DEPTH_MAP_OFFSET + i]);
    }
    for (int i = 0; i < OCCUPANCY_SIZE; i++) {
        float value = observations[OCCUPANCY_OFFSET + i];
        if (value > 0.75f) occupancy_hits++;
        else if (value > 0.25f) occupancy_free++;
        else occupancy_unknown++;
    }

    printf("sensor-check: obs=%d depth=[%.3f, %.3f] occupancy_hits=%d/%d "
           "free=%d unknown=%d\n",
           OBS_SIZE, (double)depth_min, (double)depth_max,
           occupancy_hits, OCCUPANCY_SIZE, occupancy_free, occupancy_unknown);
    if (depth_min >= 1.0f || occupancy_hits == 0 || occupancy_free == 0 ||
            occupancy_unknown == 0) exit(1);
    puf_close(&env);
}

static void crouch_door_audit() {
    Shenanigans3D env = {
        .max_ticks = 600,
        .rng = 17,
        .course_mode = COURSE_MODE_RANDOM,
        .course_difficulty = 3,
    };
    allocate_env(&env);
    obs_t observations[OBS_SIZE] = { 0 };
    float actions[NUM_ATNS] = { 0 };
    float rewards[1] = { 0 };
    float terminals[1] = { 0 };
    env.agents[0].observations = observations;
    env.agents[0].actions = actions;
    env.agents[0].rewards = rewards;
    env.agents[0].terminals = terminals;
    env.agents[0].action_mask = NULL;
    env.agents[0].policy = 0;
    puf_reset(&env);

    int low_index = -1;
    for (int i = 0; i + 1 < env.course.room_count; i++) {
        if (env.course.route_doors[i].kind == HALL_DOOR_LOW) {
            low_index = i;
            break;
        }
    }
    if (low_index < 0) {
        fprintf(stderr, "crouch-check: generated course has no low door\n");
        puf_close(&env);
        exit(1);
    }

    CourseDoor* door = &env.course.route_doors[low_index];
    CourseRoom* room_a = &env.course.rooms[low_index];
    CourseRoom* room_b = &env.course.rooms[low_index + 1];
    float dx = room_b->x > room_a->x ? 1.0f : 0.0f;
    float dz = room_b->z > room_a->z ? 1.0f :
               (room_b->z < room_a->z ? -1.0f : 0.0f);
    float start_x = door->x - dx * 1.5f;
    float start_z = door->z - dz * 1.5f;
    float yaw = atan2f(dz, dx);

    b3Body_SetTransform(env.ch.body, (b3Pos){ start_x, 1.0f, start_z },
                        b3Quat_identity);
    b3Body_SetLinearVelocity(env.ch.body, (b3Vec3){ 0, 0, 0 });
    env.yaw = yaw;
    for (int tick = 0; tick < 120; tick++) {
        actions[0] = 2.0f;
        actions[1] = 2.0f;
        actions[2] = 1.0f;
        actions[3] = 0.0f;
        actions[4] = 0.0f;
        puf_step(&env);
    }
    float stand_progress = (feet_x(&env) - start_x) * dx +
                           (feet_z(&env) - start_z) * dz;

    b3Body_SetTransform(env.ch.body, (b3Pos){ start_x, 1.0f, start_z },
                        b3Quat_identity);
    b3Body_SetLinearVelocity(env.ch.body, (b3Vec3){ 0, 0, 0 });
    env.yaw = yaw;
    env.tick = 0;
    env.depth_valid = false;
    env.occupancy_valid = false;
    for (int tick = 0; tick < 120; tick++) {
        actions[0] = 2.0f;
        actions[1] = 2.0f;
        actions[2] = 1.0f;
        actions[3] = 0.0f;
        actions[4] = 1.0f;
        puf_step(&env);
    }
    float crouch_progress = (feet_x(&env) - start_x) * dx +
                            (feet_z(&env) - start_z) * dz;
    bool stand_blocked = stand_progress < 1.25f;
    bool crouch_crossed = crouch_progress > 1.85f;
    printf("crouch-check: low-door height=%.2f stand=%.2f crouch=%.2f %s\n",
           (double)door->height, (double)stand_progress,
           (double)crouch_progress,
           stand_blocked && crouch_crossed ? "PASS" : "FAIL");
    puf_close(&env);
    if (!stand_blocked || !crouch_crossed) exit(1);
}

static int headless_eval(S3DPolicy* net, int episodes, int deterministic, int trace) {
    Shenanigans3D env = {
        // Native trace row zero is initialized with rng=0. Keep ordinary
        // evaluation's historical seed independent from parity tracing.
        .rng = trace ? 0 : 1000,
    };
    s3d_init_from_config(&env);
    unsigned int policy_rng = 1001;
    obs_t observations[OBS_SIZE] = { 0 };
    float actions[NUM_ATNS] = { 0 };
    float rewards[1] = { 0 };
    float terminals[1] = { 0 };
    unsigned char trace_mask[15];
    memset(trace_mask, 1, sizeof(trace_mask));
    env.agents[0].observations = observations;
    env.agents[0].actions = actions;
    env.agents[0].rewards = rewards;
    env.agents[0].terminals = terminals;
    env.agents[0].action_mask = trace ? trace_mask : NULL;
    env.agents[0].policy = 0;
    puf_reset(&env);
    s3d_reset_mingru(net);

    int successes = 0;
    for (int episode = 0; episode < episodes; episode++) {
        bool ended = false;
        int steps = 0;
        for (int tick = 0; tick <= env.max_ticks; tick++) {
            s3d_cpu_forward(net, observations);
            float trace_logits[16];
            if (trace) {
                memcpy(trace_logits, net->base->decoder->output,
                       sizeof(trace_logits));
            }
            if (deterministic) {
                argmax_multidiscrete(net->base->multidiscrete,
                                     net->base->decoder->output, actions);
            } else {
                s3d_sample_policy(net, &policy_rng, actions);
            }
            float trace_obs[OBS_SIZE];
            if (trace) memcpy(trace_obs, observations, sizeof(trace_obs));
            puf_step(&env);
            if (trace && tick < 12) {
                printf("trace ep=%d t=%d obs=", episode + 1, tick);
                for (int i = 0; i < OBS_SIZE; i++)
                    printf("%s%.9g", i == 0 ? "" : ",", (double)trace_obs[i]);
                printf(" mask=");
                for (int i = 0; i < (int)sizeof(trace_mask); i++)
                    printf("%s%d", i == 0 ? "" : ",", trace_mask[i] != 0);
                printf(" logits=");
                for (int i = 0; i < 16; i++)
                    printf("%s%.9g", i == 0 ? "" : ",", (double)trace_logits[i]);
                printf(" value=%.9g state=", (double)trace_logits[15]);
                int state_count = net->base->mingru->num_layers *
                                  net->base->mingru->hidden_size;
                for (int i = 0; i < state_count; i++)
                    printf("%s%.9g", i == 0 ? "" : ",",
                           (double)net->base->mingru->state[i]);
                printf(" actions=%.0f,%.0f,%.0f,%.0f,%.0f reward=%.9g terminal=%.0f next_obs=",
                       (double)actions[0], (double)actions[1], (double)actions[2],
                       (double)actions[3], (double)actions[4], (double)rewards[0],
                       (double)terminals[0]);
                for (int i = 0; i < OBS_SIZE; i++)
                    printf("%s%.9g", i == 0 ? "" : ",", (double)observations[i]);
                putchar('\n');
            }
            steps = tick + 1;
            if (*terminals > 0.5f) {
                ended = true;
                if (env.lastGoal) successes++;
                break;
            }
        }
        if (!ended) {
            fprintf(stderr, "eval: episode %d did not terminate\n", episode + 1);
            puf_close(&env);
            return 1;
        }
        if (!env.lastGoal) {
            fprintf(stderr,
                    "eval: episode %d failed after %d steps at (%.2f, %.2f, %.2f)\n",
                    episode + 1, steps, (double)feet_x(&env), (double)feet_y(&env),
                    (double)feet_z(&env));
        }
        s3d_reset_mingru(net);
    }

    float n = env.log.n > 0.0f ? env.log.n : 1.0f;
    printf("eval: %d/%d success avg_length=%.1f avg_score=%.2f avg_return=%.3f\n",
           successes, episodes, (double)(env.log.episode_length / n),
           (double)(env.log.score / n), (double)(env.log.episode_return / n));
    puf_close(&env);
    return successes == episodes ? 0 : 1;
}

static void scripted_course_action(Shenanigans3D* env, float* actions) {
    float fx = feet_x(env);
    float fz = feet_z(env);

    if (env->course.room_count > 1) {
        CourseParams* p = &env->course;
        float route = hallway_progress(env);
        int doorIndex = (int)((route + HALL_ROOM_SPACING * 0.5f) /
                              HALL_ROOM_SPACING);
        if (doorIndex < 0) doorIndex = 0;
        if (doorIndex >= p->room_count - 1) doorIndex = p->room_count - 1;

        float targetX = p->goal_x;
        float targetZ = p->goal_z;
        CourseDoor* door = NULL;
        if (doorIndex < p->room_count - 1) {
            door = &p->route_doors[doorIndex];
            targetX = door->x;
            targetZ = door->z;
        }

        float dx = targetX - fx, dz = targetZ - fz;
        float bearing = atan2f(dz, dx) - env->yaw;
        while (bearing > (float)M_PI) bearing -= 2.0f * (float)M_PI;
        while (bearing < -(float)M_PI) bearing += 2.0f * (float)M_PI;
        actions[0] = bearing > 0.18f ? 4.0f :
                     (bearing > 0.04f ? 3.0f :
                      (bearing < -0.18f ? 0.0f :
                       (bearing < -0.04f ? 1.0f : 2.0f)));
        actions[1] = 2.0f;
        float lateral = -sinf(env->yaw) * dx + cosf(env->yaw) * dz;
        actions[2] = lateral > 0.18f ? 2.0f : (lateral < -0.18f ? 0.0f : 1.0f);
        actions[3] = door && door->kind == HALL_DOOR_JUMP &&
                     sqrtf(dx * dx + dz * dz) < 1.35f ? 1.0f : 0.0f;
        int roomIndex = (int)(route / HALL_ROOM_SPACING + 0.5f);
        if (roomIndex < 0) roomIndex = 0;
        if (roomIndex >= p->room_count) roomIndex = p->room_count - 1;
        bool enteringCeiling = door && doorIndex + 1 == p->ceiling_room &&
                               sqrtf(dx * dx + dz * dz) < 2.2f;
        actions[4] = (door && door->kind == HALL_DOOR_LOW) ||
                     roomIndex == p->ceiling_room || enteringCeiling ? 1.0f : 0.0f;
        return;
    }

    float target_z = 0.0f;
    for (int i = 0; i < COURSE_DOORS; i++) {
        if (fx < env->course.doors[i].x + 0.5f) {
            target_z = env->course.doors[i].z;
            break;
        }
    }

    actions[0] = 2.0f;
    actions[1] = 2.0f;
    actions[2] = fz < target_z - 0.12f ? 2.0f :
                 (fz > target_z + 0.12f ? 0.0f : 1.0f);
    actions[3] = (fx > env->course.jump_x - 0.8f &&
                  fx < env->course.jump_x + 0.6f) ? 1.0f : 0.0f;
    actions[4] = (fx > env->course.tunnel_start - 1.0f &&
                  fx < env->course.tunnel_end + 0.5f) ? 1.0f : 0.0f;
}

static void scripted_check() {
    Shenanigans3D env = { .max_ticks = 3600, .rng = 7 };
    allocate_env(&env);
    obs_t observations[OBS_SIZE] = { 0 };
    float actions[NUM_ATNS] = { 0 };
    float rewards[1] = { 0 };
    float terminals[1] = { 0 };
    env.agents[0].observations = observations;
    env.agents[0].actions = actions;
    env.agents[0].rewards = rewards;
    env.agents[0].terminals = terminals;
    env.agents[0].action_mask = NULL;
    env.agents[0].policy = 0;
    puf_reset(&env);

    for (int tick = 0; tick < 3600; tick++) {
        scripted_course_action(&env, actions);

        puf_step(&env);
        if (env.lastGoal && *terminals > 0.5f) {
            printf("GOAL REACHED at global t=%d\n", tick);
            puf_close(&env);
            return;
        }
    }
    printf("FAILED to reach goal. final x=%.2f y=%.2f\n",
           (double)feet_x(&env), (double)feet_y(&env));
    puf_close(&env);
}

static void randomized_check() {
    int passed = 0;
    const int trials = 32;
    const int episodes = 3;
    for (int i = 0; i < trials; i++) {
        Shenanigans3D env = {
            .max_ticks = 2400,
            .rng = (unsigned int)(1000 + i * 97),
            .course_mode = COURSE_MODE_RANDOM_EVERY_RESET,
            .course_difficulty = 3,
        };
        allocate_env(&env);
        obs_t observations[OBS_SIZE] = { 0 };
        float actions[NUM_ATNS] = { 0 };
        float rewards[1] = { 0 };
        float terminals[1] = { 0 };
        env.agents[0].observations = observations;
        env.agents[0].actions = actions;
        env.agents[0].rewards = rewards;
        env.agents[0].terminals = terminals;
        env.agents[0].action_mask = NULL;
        env.agents[0].policy = 0;
        puf_reset(&env);

        bool all_passed = true;
        bool changed = false;
        for (int episode = 0; episode < episodes; episode++) {
            CourseParams before = env.course;
            bool ended = false;
            int fail_tick = 0;
            float fail_x = 0.0f, fail_y = 0.0f, fail_z = 0.0f;
            for (int tick = 0; tick < env.max_ticks; tick++) {
                scripted_course_action(&env, actions);
                fail_tick = tick;
                fail_x = feet_x(&env);
                fail_y = feet_y(&env);
                fail_z = feet_z(&env);
                puf_step(&env);
                if (*terminals > 0.5f) {
                    ended = true;
                    break;
                }
            }
            if (!ended || !env.lastGoal) {
                all_passed = false;
                fprintf(stderr, "variant %d episode %d failed: pos=(%.2f, %.2f, %.2f) t=%d\n",
                        i, episode, (double)fail_x, (double)fail_y,
                        (double)fail_z, fail_tick);
                s3d_print_course("failed", &env);
                break;
            }
            if (episode + 1 < episodes &&
                memcmp(&before, &env.course, sizeof(CourseParams)) != 0) {
                changed = true;
            }
        }
        if (all_passed && changed) passed++;
        puf_close(&env);
    }
    printf("randomized-check: %d/%d variants passed %d episodes with reset changes\n",
           passed, trials, episodes);
    if (passed != trials) exit(1);
}

static void demo(S3DPolicy* net, int deterministic) {
    unsigned int course_seed = s3d_runtime_seed();
    Shenanigans3D env = { .max_ticks = 100000, .rng = course_seed };
    s3d_init_from_config(&env);
    env.max_ticks = 100000;
    if (env.course_mode >= COURSE_MODE_RANDOM) {
        fprintf(stderr, "course seed=%u\n", course_seed);
        s3d_print_course("initial", &env);
    }
    obs_t observations[OBS_SIZE] = { 0 };
    float actions[NUM_ATNS] = { 0 };
    float rewards[1] = { 0 };
    float terminals[1] = { 0 };
    env.agents[0].observations = observations;
    env.agents[0].actions = actions;
    env.agents[0].rewards = rewards;
    env.agents[0].terminals = terminals;
    env.agents[0].action_mask = NULL;
    env.agents[0].policy = 0;
    puf_reset(&env);

    puf_render(&env);
    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_R)) {
            puf_reset(&env);
            s3d_reset_mingru(net);
        }

        if (net) {
            s3d_cpu_forward(net, observations);
            if (deterministic)
                argmax_multidiscrete(net->base->multidiscrete,
                                     net->base->decoder->output, actions);
            else
                s3d_sample_policy(net, &env.rng, actions);
        } else {
            actions[0] = 2.0f;
            actions[1] = 1.0f;
            actions[2] = 1.0f;
            actions[3] = 0.0f;
            actions[4] = 0.0f;
            if (IsKeyDown(KEY_LEFT)) actions[0] = 0.0f;
            if (IsKeyDown(KEY_RIGHT)) actions[0] = 4.0f;
            if (IsKeyDown(KEY_W)) actions[1] = 2.0f;
            if (IsKeyDown(KEY_S)) actions[1] = 0.0f;
            if (IsKeyDown(KEY_D)) actions[2] = 2.0f;
            if (IsKeyDown(KEY_A)) actions[2] = 0.0f;
            if (IsKeyDown(KEY_SPACE)) actions[3] = 1.0f;
            if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_C)) actions[4] = 1.0f;
        }

        puf_step(&env);
        if (*terminals > 0.5f) {
            if (env.course_mode == COURSE_MODE_RANDOM_EVERY_RESET)
                s3d_print_course("reset", &env);
            s3d_reset_mingru(net); // fresh memory next episode
        }
        puf_render(&env);
    }
    puf_close(&env);
    if (IsWindowReady()) CloseWindow();
}

int main(int argc, char** argv) {
    const char* model_arg = NULL;
    int deterministic = 0;
    S3DPolicy* net = NULL;

    if (argc >= 2) {
        if (strcmp(argv[1], "watch") == 0) {
            model_arg = "latest";
            for (int i = 2; i < argc; i++) {
                if (strcmp(argv[i], "--deterministic") == 0) deterministic = 1;
                else if (argv[i][0] != '-' && model_arg != NULL &&
                         strcmp(model_arg, "latest") == 0 && i == 2)
                    model_arg = argv[i];
                else if (argv[i][0] != '-' && i == 2) model_arg = argv[i];
                else { s3d_print_usage(argv[0]); return 1; }
            }
            char path[4096];
            int hidden = 0, layers = 0;
            s3d_watch_arch(&hidden, &layers);
            long long expected_bytes = (long long)s3d_expected_policy_floats(hidden, layers) *
                                       (long long)sizeof(float);
            if (s3d_resolve_model(model_arg, path, sizeof(path), expected_bytes) != 0)
                return 1;
            net = s3d_load_policy(path);
            if (!net) return 1;
        } else if (strcmp(argv[1], "--eval") == 0) {
            model_arg = "latest";
            int episodes = 65;
            int eval_deterministic = -1;
            int trace = 0;
            int positional = 0;
            for (int i = 2; i < argc; i++) {
                if (strcmp(argv[i], "--deterministic") == 0) {
                    eval_deterministic = 1;
                } else if (strcmp(argv[i], "--trace") == 0) {
                    trace = 1;
                } else if (argv[i][0] == '-') {
                    s3d_print_usage(argv[0]);
                    return 1;
                } else if (positional == 0) {
                    model_arg = argv[i];
                    positional++;
                } else if (positional == 1) {
                    episodes = atoi(argv[i]);
                    if (episodes <= 0) {
                        fprintf(stderr, "episodes must be positive\n");
                        return 1;
                    }
                    positional++;
                } else {
                    s3d_print_usage(argv[0]);
                    return 1;
                }
            }
            char path[4096];
            int hidden = 0, layers = 0;
            s3d_watch_arch(&hidden, &layers);
            long long expected_bytes = (long long)s3d_expected_policy_floats(hidden, layers) *
                                       (long long)sizeof(float);
            if (s3d_resolve_model(model_arg, path, sizeof(path), expected_bytes) != 0)
                return 1;
            net = s3d_load_policy(path);
            if (!net) return 1;
            if (eval_deterministic < 0)
                eval_deterministic = s3d_eval_deterministic_from_config();
            int result = headless_eval(net, episodes, eval_deterministic, trace);
            s3d_free_policy(net);
            return result;
        } else if (strcmp(argv[1], "play") == 0 || strcmp(argv[1], "--play") == 0) {
            // human play
        } else if (strcmp(argv[1], "--bench") == 0) {
            performance_test();
            return 0;
        } else if (strcmp(argv[1], "--collide") == 0) {
            collision_audit();
            return 0;
        } else if (strcmp(argv[1], "--sense-check") == 0) {
            sensor_audit();
            crouch_door_audit();
            return 0;
        } else if (strcmp(argv[1], "--check") == 0) {
            scripted_check();
            return 0;
        } else if (strcmp(argv[1], "--random-check") == 0) {
            randomized_check();
            return 0;
        } else if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "-h") == 0 ||
                   strcmp(argv[1], "--help") == 0) {
            s3d_print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unknown mode '%s'\n", argv[1]);
            s3d_print_usage(argv[0]);
            return 1;
        }
    }

    demo(net, deterministic);
    s3d_free_policy(net);
    return 0;
}
