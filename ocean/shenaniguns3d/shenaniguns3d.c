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

// ---------------------------------------------------------------------------
// Checkpoint discovery (newest .bin under checkpoints/shenaniguns3d/)
// ---------------------------------------------------------------------------

static void s3d_find_latest(const char* dir, char* out, size_t out_size,
                            time_t* best_time) {
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
            s3d_find_latest(path, out, out_size, best_time);
        } else if (S_ISREG(st.st_mode)) {
            size_t n = strlen(path);
            if (n > 4 && strcmp(path + n - 4, ".bin") == 0 && st.st_ctime >= *best_time) {
                *best_time = st.st_ctime;
                snprintf(out, out_size, "%s", path);
            }
        }
    }
    closedir(dp);
}

static int s3d_resolve_model(const char* arg, char* out, size_t out_size) {
    if (!arg || !*arg || strcmp(arg, "latest") == 0) {
        char root[2048];
        snprintf(root, sizeof(root), "checkpoints/%s", S3D_ENV_NAME);
        out[0] = 0;
        time_t best = 0;
        s3d_find_latest(root, out, out_size, &best);
        if (!out[0]) {
            fprintf(stderr, "no .bin checkpoints found in %s\n", root);
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

static int s3d_expected_policy_floats(int hidden, int layers) {
    int act_sizes[] = ACT_SIZES;
    int atn_sum = 0;
    for (int i = 0; i < NUM_ATNS; i++) atn_sum += act_sizes[i];
    // encoder (obs->hidden) + decoder (hidden->atn_sum+value) + mingru stack
    return hidden * OBS_SIZE + (atn_sum + 1) * hidden + layers * 3 * hidden * hidden;
}

static void s3d_reset_mingru(PufferNet* net) {
    if (!net || !net->mingru || !net->mingru->state) return;
    size_t count = (size_t)net->mingru->num_layers * net->mingru->batch_size *
                   net->mingru->hidden_size;
    memset(net->mingru->state, 0, count * sizeof(float));
}

// Weights blob must outlive the net (layer matrices alias into it).
static PufferNet* s3d_load_policy(const char* path) {
    Weights* weights = load_weights(path);
    if (!weights) {
        fprintf(stderr, "failed to load weights: %s\n", path);
        return NULL;
    }

    int hidden = 64, layers = 2;
    s3d_watch_arch(&hidden, &layers);

    int act_sizes[] = ACT_SIZES;
    int expected = s3d_expected_policy_floats(hidden, layers);
    fprintf(stderr, "watch: %s  (hidden=%d layers=%d)\n", path, hidden, layers);

    PufferNet* net =
        make_puffernet(weights, 1, OBS_SIZE, hidden, layers, act_sizes, NUM_ATNS);
    if (weights->idx != expected) {
        fprintf(stderr,
                "checkpoint/model mismatch: expected %d floats for hidden=%d "
                "layers=%d, loader consumed %d\n",
                expected, hidden, layers, weights->idx);
        free_puffernet(net);
        free(weights);
        return NULL;
    }
    return net;
}

// Stochastic watch: sample each discrete head from softmaxed logits.
static void s3d_sample_policy(PufferNet* net, unsigned int* rng, float* actions) {
    float* logits = net->decoder->output; // [atn_sum][..., +value]
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
        "  %s --check                  # scripted solvability run\n"
        "  %s --bench                  # headless steps/sec\n",
        argv0, argv0, argv0, argv0, argv0);
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
    Shenanigans3D env = { .max_ticks = 100000, .rng = 42 };
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
        actions[0] = 2.0f;
        actions[1] = 2.0f;
        actions[2] = 1.0f;
        actions[3] = 0.0f;
        float fx = (float)pd_char_feet_position(&env.ch).x;
        actions[4] = (fx > 11.5f && fx < 16.5f) ? 1.0f : 0.0f;

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

static void demo(PufferNet* net, int deterministic) {
    Shenanigans3D env = { .max_ticks = 100000, .rng = 1 };
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

    puf_render(&env);
    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_R)) {
            puf_reset(&env);
            s3d_reset_mingru(net);
        }

        if (net) {
            linear(net->encoder, observations);
            mingru(net->mingru, net->encoder->output);
            linear(net->decoder, net->mingru->output);
            if (deterministic)
                argmax_multidiscrete(net->multidiscrete, net->decoder->output, actions);
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
        if (*terminals > 0.5f) s3d_reset_mingru(net); // fresh memory next episode
        puf_render(&env);
    }
    puf_close(&env);
    if (IsWindowReady()) CloseWindow();
}

int main(int argc, char** argv) {
    const char* model_arg = NULL;
    int deterministic = 0;
    PufferNet* net = NULL;

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
            if (s3d_resolve_model(model_arg, path, sizeof(path)) != 0) return 1;
            net = s3d_load_policy(path);
            if (!net) return 1;
        } else if (strcmp(argv[1], "play") == 0 || strcmp(argv[1], "--play") == 0) {
            // human play
        } else if (strcmp(argv[1], "--bench") == 0) {
            performance_test();
            return 0;
        } else if (strcmp(argv[1], "--collide") == 0) {
            collision_audit();
            return 0;
        } else if (strcmp(argv[1], "--check") == 0) {
            scripted_check();
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
    if (net) {
        free_puffernet(net);
    }
    return 0;
}
