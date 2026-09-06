#include "robocode.h"
#include "puffercpu.h"
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

static void bind_agents(Robocode* env, obs_t* observations,
        float* actions, float* rewards, float* terminals) {
    for (int i = 0; i < env->num_agents; i++) {
        env->agents[i].observations = observations + i * (EGO_FEATURES + OTHER_FEATURES);
        env->agents[i].actions = actions + i * NUM_ACTIONS;
        env->agents[i].rewards = rewards + i;
        env->agents[i].terminals = terminals + i;
        env->agents[i].action_mask = NULL;
        env->agents[i].policy = 0;
    }
}

void performance_test() {
    long test_time = 10;
    Robocode env = {
        .num_agents = 2,
        .num_bots = 0,
        .width = 800,
        .height = 600,
        .reward_damage = 0.01f,
        .reward_spot = 0.001f,
        .bot_policy = 3,  // BOT_WAVE_SURFER
        .max_ticks = 3000,
        .rng = 42,
    };
    allocate_env(&env);
    obs_t observations[2 * (EGO_FEATURES + OTHER_FEATURES)] = {0};
    float actions[2 * NUM_ACTIONS] = {0};
    float rewards[2] = {0};
    float terminals[2] = {0};
    bind_agents(&env, observations, actions, rewards, terminals);
    puf_reset(&env);

    long start = time(NULL);
    int i = 0;
    while (time(NULL) - start < test_time) {
        float* actions = env.agents[0].actions;
        actions[0] = rand_r(&env.rng) % 4;
        actions[1] = rand_r(&env.rng) % 9;
        actions[2] = rand_r(&env.rng) % 11;
        actions[3] = rand_r(&env.rng) % 11;
        actions[4] = (rand_r(&env.rng) % 6) > 4 ? 1.0f : 0.0f;
        puf_step(&env);
        i++;
    }
    long end = time(NULL);
    printf("SPS: %ld\n", (long)i*env.num_agents / (end - start));
    puf_close(&env);
}

void demo(void) {
    Robocode env = {
        .num_agents = 1,
        .num_bots = 1,
        .reward_damage = 0.01,
        .width = 800,
        .height = 600,
        .max_ticks = 512,
    };
    allocate_env(&env);
    obs_t observations[EGO_FEATURES + OTHER_FEATURES] = {0};
    float actions[NUM_ACTIONS] = {0};
    float rewards[1] = {0};
    float terminals[1] = {0};
    bind_agents(&env, observations, actions, rewards, terminals);
    puf_reset(&env);

    env.client = make_client(&env);
    puf_render(&env);

    while (!WindowShouldClose()) {
        float* actions = env.agents[0].actions;
        actions[0] = 2;
        actions[1] = 4;
        actions[2] = 5;
        actions[3] = 5;
        actions[4] = 0;

        if (IsKeyPressed(KEY_ESCAPE)) break;
        if (IsKeyDown(KEY_W)) actions[0] = 3.0f;
        if (IsKeyDown(KEY_S)) actions[0] = 1.0f;
        if (IsKeyDown(KEY_A)) actions[1] = 3.0f;
        if (IsKeyDown(KEY_D)) actions[1] = 5.0f;
        if (IsKeyDown(KEY_Q)) actions[2] = 4.0f;
        if (IsKeyDown(KEY_E)) actions[2] = 6.0f;
        if (IsKeyDown(KEY_LEFT)) actions[3] = 0.0f;
        if (IsKeyDown(KEY_RIGHT)) actions[3] = 8.0f;
        if (IsKeyDown(KEY_SPACE)) actions[4] = 1.0f;

        puf_step(&env);
        puf_render(&env);
    }
    puf_close(&env);
    CloseWindow();
}

// ---- watch mode (mirrors puffer_survivors watch) ----
static const char* RB_ENV_NAME = "robocode";

static int rb_has_suffix(const char* s, const char* suffix) {
    size_t n = strlen(s);
    size_t m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

static void rb_find_latest(const char* dir, char* out, size_t out_size, time_t* best) {
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
            rb_find_latest(path, out, out_size, best);
        } else if (S_ISREG(st.st_mode) && rb_has_suffix(path, ".bin") &&
                st.st_ctime >= *best) {
            *best = st.st_ctime;
            snprintf(out, out_size, "%s", path);
        }
    }
    closedir(dp);
}

static int rb_resolve_model(const char* arg, char* out, size_t out_size) {
    if (!arg || !*arg || strcmp(arg, "latest") == 0) {
        const char* root = "checkpoints/robocode";
        out[0] = 0;
        time_t best = 0;
        rb_find_latest(root, out, out_size, &best);
        if (!out[0]) {
            fprintf(stderr, "no .bin checkpoints found in %s\n", root);
            return -1;
        }
        return 0;
    }
    snprintf(out, out_size, "%s", arg);
    return 0;
}

static void rb_print_usage(const char* argv0) {
    fprintf(stderr,
        "usage:\n"
        "  %s                 you (blue) vs latest model (blue)\n"
        "  %s play [latest|PATH.bin] [--deterministic] [--seed N] [section.key=value ...]\n"
        "  %s watch [latest|PATH.bin] [--deterministic] [--vs-bot] [--bot-policy N] [--seed N] [section.key=value ...]\n"
        "\n"
        "play: you drive slot 0 (WASD move/turn, Q/E gun, LEFT/RIGHT radar, SPACE fire, R restart, H hide radar).\n"
        "watch default: selfplay (2 policy agents, 0 bots). --vs-bot: 1 policy vs 1 scripted bot.\n"
        "Run from the repo root. Policy arch comes from config/robocode.ini [policy].\n",
        argv0, argv0, argv0);
}

static int rb_expected_floats(int hidden, int layers) {
    int act_sizes[] = ACT_SIZES;
    int atn_sum = 0;
    for (int i = 0; i < NUM_ATNS; i++) atn_sum += act_sizes[i];
    return hidden * OBS_SIZE + (atn_sum + 1) * hidden + layers * 3 * hidden * hidden;
}

static void rb_reset_state(PufferNet* net) {
    if (!net || !net->mingru || !net->mingru->state) return;
    int n = net->mingru->num_layers * net->mingru->batch_size * net->mingru->hidden_size;
    memset(net->mingru->state, 0, (size_t)n * sizeof(float));
}

static void rb_forward_argmax(PufferNet* net, float* obs, float* atn) {
    linear(net->encoder, obs);
    mingru(net->mingru, net->encoder->output);
    linear(net->decoder, net->mingru->output);
    argmax_multidiscrete(net->multidiscrete, net->decoder->output, atn);
}

// args after "watch", e.g. watch latest --vs-bot
static int rb_watch(const char* argv0, int argc, char** argv) {
    const char* model_arg = "latest";
    int model_set = 0;
    int deterministic = 0;
    int vs_bot = 0;
    int bot_policy = -1;
    unsigned int seed = 1;
    char* overrides[64];
    int n_overrides = 0;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--deterministic") == 0) {
            deterministic = 1;
        } else if (strcmp(argv[i], "--vs-bot") == 0) {
            vs_bot = 1;
        } else if (strcmp(argv[i], "--bot-policy") == 0) {
            if (++i >= argc) { rb_print_usage(argv0); return 1; }
            bot_policy = atoi(argv[i]);
        } else if (strcmp(argv[i], "--seed") == 0) {
            if (++i >= argc) { rb_print_usage(argv0); return 1; }
            seed = (unsigned int)atoi(argv[i]);
        } else if (argv[i][0] == '-' && strchr(argv[i], '=') == NULL) {
            fprintf(stderr, "unknown watch argument '%s'\n", argv[i]);
            rb_print_usage(argv0);
            return 1;
        } else if (!model_set && argv[i][0] != '-') {
            model_arg = argv[i];
            model_set = 1;
        } else {
            if (n_overrides >= (int)(sizeof(overrides) / sizeof(overrides[0]))) {
                fprintf(stderr, "too many overrides\n");
                return 1;
            }
            overrides[n_overrides++] = argv[i];
        }
    }

    char model_path[4096];
    if (rb_resolve_model(model_arg, model_path, sizeof(model_path)) != 0) return 1;

    Ini ini = {0};
    puf_ini_load_env(&ini, RB_ENV_NAME, n_overrides, overrides);
    Dict* env_kwargs = puf_ini_section(&ini, "env", 0);

    int num_agents = vs_bot ? 1 : 2;
    int num_bots = vs_bot ? 1 : 0;
    dict_set(env_kwargs, "num_agents", (double)num_agents);
    dict_set(env_kwargs, "num_bots", (double)num_bots);
    if (bot_policy >= 0) dict_set(env_kwargs, "bot_policy", (double)bot_policy);

    int hidden = puf_ini_get_int(&ini, "policy", "hidden_size");
    int layers = puf_ini_get_int(&ini, "policy", "num_layers");

    Env env = {0};
    env.rng = seed ? seed : 1u;
    puf_init(&env, env_kwargs);

    int n = env.num_agents;
    obs_t* observations = (obs_t*)calloc((size_t)n * OBS_SIZE, sizeof(obs_t));
    float* actions = (float*)calloc((size_t)n * NUM_ACTIONS, sizeof(float));
    float* rewards = (float*)calloc((size_t)n, sizeof(float));
    float* terminals = (float*)calloc((size_t)n, sizeof(float));
    for (int i = 0; i < n; i++) {
        env.agents[i].observations = observations + i * OBS_SIZE;
        env.agents[i].actions = actions + i * NUM_ACTIONS;
        env.agents[i].rewards = rewards + i;
        env.agents[i].terminals = terminals + i;
        env.agents[i].action_mask = NULL;
        env.agents[i].policy = 0;
    }

    Weights* w = load_weights(model_path);
    if (!w) {
        fprintf(stderr, "failed to load weights: %s\n", model_path);
        puf_ini_free(&ini);
        return 1;
    }

    int act_sizes[] = ACT_SIZES;
    int expected = rb_expected_floats(hidden, layers);
    fprintf(stderr, "watch: %s (hidden=%d layers=%d agents=%d bots=%d %s)\n",
        model_path, hidden, layers, env.num_agents, env.num_bots,
        deterministic ? "argmax" : "sample");

    PufferNet* net = make_puffernet(w, n, OBS_SIZE, hidden, layers,
        act_sizes, NUM_ATNS);
    if (w->idx != expected) {
        fprintf(stderr,
            "checkpoint/model mismatch: expected %d floats for hidden=%d layers=%d, loader consumed %d\n",
            expected, hidden, layers, w->idx);
        free_puffernet(net);
        free(w);
        puf_ini_free(&ini);
        return 1;
    }

    puf_reset(&env);
    puf_render(&env);

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_R)) {
            puf_reset(&env);
            rb_reset_state(net);
        }
        if (IsKeyPressed(KEY_H)) {
            g_robocode_hide_radar = !g_robocode_hide_radar;
        }
        if (deterministic) {
            rb_forward_argmax(net, observations, actions);
        } else {
            forward_puffernet(net, observations, actions);
        }
        puf_step(&env);
        for (int i = 0; i < n; i++) {
            if (terminals[i] > 0.5f) { rb_reset_state(net); break; }
        }
        puf_render(&env);
    }

    puf_close(&env);
    free_puffernet(net);
    free(w);
    free(observations);
    free(actions);
    free(rewards);
    free(terminals);
    puf_ini_free(&ini);
    return 0;
}

// args after "play", e.g. play latest --deterministic. You drive slot 0,
// latest model drives slot 1. Same keyboard map as the old demo().
static int rb_play(const char* argv0, int argc, char** argv) {
    const char* model_arg = "latest";
    int model_set = 0;
    int deterministic = 0;
    unsigned int seed = 1;
    char* overrides[64];
    int n_overrides = 0;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--deterministic") == 0) {
            deterministic = 1;
        } else if (strcmp(argv[i], "--seed") == 0) {
            if (++i >= argc) { rb_print_usage(argv0); return 1; }
            seed = (unsigned int)atoi(argv[i]);
        } else if (argv[i][0] == '-' && strchr(argv[i], '=') == NULL) {
            fprintf(stderr, "unknown play argument '%s'\n", argv[i]);
            rb_print_usage(argv0);
            return 1;
        } else if (!model_set && argv[i][0] != '-') {
            model_arg = argv[i];
            model_set = 1;
        } else {
            if (n_overrides >= (int)(sizeof(overrides) / sizeof(overrides[0]))) {
                fprintf(stderr, "too many overrides\n");
                return 1;
            }
            overrides[n_overrides++] = argv[i];
        }
    }

    char model_path[4096];
    if (rb_resolve_model(model_arg, model_path, sizeof(model_path)) != 0) return 1;

    Ini ini = {0};
    puf_ini_load_env(&ini, RB_ENV_NAME, n_overrides, overrides);
    Dict* env_kwargs = puf_ini_section(&ini, "env", 0);
    dict_set(env_kwargs, "num_agents", 2.0);
    dict_set(env_kwargs, "num_bots", 0.0);

    int hidden = puf_ini_get_int(&ini, "policy", "hidden_size");
    int layers = puf_ini_get_int(&ini, "policy", "num_layers");

    Env env = {0};
    env.rng = seed ? seed : 1u;
    puf_init(&env, env_kwargs);

    const int n = 2;
    obs_t* observations = (obs_t*)calloc((size_t)n * OBS_SIZE, sizeof(obs_t));
    float* actions = (float*)calloc((size_t)n * NUM_ACTIONS, sizeof(float));
    float* rewards = (float*)calloc((size_t)n, sizeof(float));
    float* terminals = (float*)calloc((size_t)n, sizeof(float));
    for (int i = 0; i < n; i++) {
        env.agents[i].observations = observations + i * OBS_SIZE;
        env.agents[i].actions = actions + i * NUM_ACTIONS;
        env.agents[i].rewards = rewards + i;
        env.agents[i].terminals = terminals + i;
        env.agents[i].action_mask = NULL;
        env.agents[i].policy = 0;
    }

    Weights* w = load_weights(model_path);
    if (!w) {
        fprintf(stderr, "failed to load weights: %s\n", model_path);
        puf_ini_free(&ini);
        return 1;
    }
    int act_sizes[] = ACT_SIZES;
    int expected = rb_expected_floats(hidden, layers);
    fprintf(stderr, "play: you (slot 0) vs %s (slot 1, hidden=%d layers=%d %s)\n",
        model_path, hidden, layers, deterministic ? "argmax" : "sample");
    fprintf(stderr, "controls: W/S accel, A/D turn, Q/E gun, LEFT/RIGHT radar, SPACE fire, R restart, H hide radar\n");

    PufferNet* net = make_puffernet(w, n, OBS_SIZE, hidden, layers,
        act_sizes, NUM_ATNS);
    if (w->idx != expected) {
        fprintf(stderr,
            "checkpoint/model mismatch: expected %d floats for hidden=%d layers=%d, loader consumed %d\n",
            expected, hidden, layers, w->idx);
        free_puffernet(net);
        free(w);
        puf_ini_free(&ini);
        return 1;
    }

    puf_reset(&env);
    puf_render(&env);

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_R)) {
            puf_reset(&env);
            rb_reset_state(net);
        }
        if (IsKeyPressed(KEY_H)) {
            g_robocode_hide_radar = !g_robocode_hide_radar;
        }
        if (deterministic) {
            rb_forward_argmax(net, observations, actions);
        } else {
            forward_puffernet(net, observations, actions);
        }
        // Overwrite slot 0 with human input (same map as old demo).
        float* me = actions;
        me[0] = 2; me[1] = 4; me[2] = 5; me[3] = 5; me[4] = 0;
        if (IsKeyDown(KEY_W)) me[0] = 3.0f;
        if (IsKeyDown(KEY_S)) me[0] = 1.0f;
        if (IsKeyDown(KEY_A)) me[1] = 3.0f;
        if (IsKeyDown(KEY_D)) me[1] = 5.0f;
        if (IsKeyDown(KEY_Q)) me[2] = 4.0f;
        if (IsKeyDown(KEY_E)) me[2] = 6.0f;
        if (IsKeyDown(KEY_LEFT)) me[3] = 0.0f;
        if (IsKeyDown(KEY_RIGHT)) me[3] = 8.0f;
        if (IsKeyDown(KEY_SPACE)) me[4] = 1.0f;

        puf_step(&env);
        for (int i = 0; i < n; i++) {
            if (terminals[i] > 0.5f) { rb_reset_state(net); break; }
        }
        puf_render(&env);
    }

    puf_close(&env);
    free_puffernet(net);
    free(w);
    free(observations);
    free(actions);
    free(rewards);
    free(terminals);
    puf_ini_free(&ini);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        return rb_play(argv[0], 0, NULL);
    }
    if (strcmp(argv[1], "play") == 0) {
        return rb_play(argv[0], argc - 2, argv + 2);
    }
    if (strcmp(argv[1], "watch") == 0) {
        return rb_watch(argv[0], argc - 2, argv + 2);
    }
    if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "-h") == 0 ||
            strcmp(argv[1], "--help") == 0) {
        rb_print_usage(argv[0]);
        return 0;
    }
    fprintf(stderr, "unknown mode '%s'\n", argv[1]);
    rb_print_usage(argv[0]);
    return 1;
}
