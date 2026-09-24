#include "robocode.h"
#include "puffercpu.c"

void rb_find_latest(const char* directory, char* path, time_t* newest) {
    DIR* dir = opendir(directory);
    if (!dir) return;
    struct dirent* entry;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        char child[4096];
        snprintf(child, sizeof(child), "%s/%s", directory, entry->d_name);
        struct stat st;
        if (stat(child, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            rb_find_latest(child, path, newest);
        } else {
            size_t length = strlen(child);
            if (S_ISREG(st.st_mode) && length >= 4
                    && strcmp(child + length - 4, ".bin") == 0 && st.st_ctime >= *newest) {
                *newest = st.st_ctime;
                snprintf(path, 4096, "%s", child);
            }
        }
    }
    closedir(dir);
}

void rb_forward(PufferNet* net, float* observations, float* actions,
        float* terminals, int deterministic) {
    if (!deterministic) {
        forward_puffernet(net, observations, actions, NULL, terminals);
        return;
    }
    mingru_zero_term(net->mingru, terminals);
    linear(net->encoder, observations);
    mingru(net->mingru, net->encoder->output);
    linear(net->decoder, net->mingru->output);
    multidiscrete(net->multidiscrete, net->decoder->output, actions, 1, NULL);
}

void rb_usage(const char* name) {
    fprintf(stderr,
        "usage: %s play|watch [latest|MODEL.bin] [--deterministic] [--seed N]\n"
        "       [--vs-bot] [--bot-policy N] [--section.key=value ...]\n"
        "No arguments: play against latest checkpoint. Watch defaults to mirror play.\n"
        "--vs-bot and --bot-policy select a scripted opponent in watch mode.\n"
        "Controls: WASD move/turn, Q/E gun, arrows radar, Space fire, R reset, H radar.\n"
        "Architecture and environment defaults come from config/robocode.ini.\n",
        name);
}

int main(int argc, char** argv) {
    int play = argc < 2 || strcmp(argv[1], "play") == 0;
    if (argc > 1 && !play && strcmp(argv[1], "watch") != 0) {
        rb_usage(argv[0]);
        return strcmp(argv[1], "--help") && strcmp(argv[1], "-h")
            && strcmp(argv[1], "help") ? 1 : 0;
    }
    const char* model = "latest";
    int deterministic = 0, vs_bot = 0, bot = -1, seed = 1;
    char* overrides[argc];
    int override_count = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--deterministic") == 0) {
            deterministic = 1;
        } else if (strcmp(argv[i], "--vs-bot") == 0) {
            vs_bot = 1;
        } else if (strcmp(argv[i], "--seed") == 0 || strcmp(argv[i], "--bot-policy") == 0) {
            int is_seed = strcmp(argv[i], "--seed") == 0;
            if (++i == argc) {
                rb_usage(argv[0]);
                return 1;
            }
            if (is_seed) {
                seed = atoi(argv[i]);
            } else {
                bot = atoi(argv[i]);
            }
        } else if (strchr(argv[i], '=')) {
            overrides[override_count++] = argv[i];
        } else if (argv[i][0] != '-') {
            model = argv[i];
        } else {
            rb_usage(argv[0]);
            return 1;
        }
    }
    assert(!play || (!vs_bot && bot < 0));
    char latest[4096] = {0};
    if (strcmp(model, "latest") == 0) {
        time_t newest = 0;
        rb_find_latest("checkpoints/robocode", latest, &newest);
        if (!latest[0]) {
            fprintf(stderr, "No checkpoints under checkpoints/robocode\n");
            return 1;
        }
        model = latest;
    }
    Ini ini = {0};
    puf_ini_load_env(&ini, "robocode", override_count, overrides);
    Dict* kwargs = puf_ini_section(&ini, "env", 0);
    dict_set(kwargs, "num_agents", vs_bot ? 1 : 2);
    dict_set(kwargs, "num_bots", vs_bot ? 1 : 0);
    if (bot >= 0) {
        dict_set(kwargs, "bot_policy", bot);
    }
    int hidden = puf_ini_get(&ini, "policy", "hidden_size");
    int layers = puf_ini_get(&ini, "policy", "num_layers");
    int sizes[] = ACT_SIZES;
    int logits = 1;
    for (int i = 0; i < NUM_ATNS; i++) {
        logits += sizes[i];
    }
    int expected = ((hidden * OBS_SIZE + 7) / 8) * 8
        + ((hidden * logits + 7) / 8) * 8
        + layers * ((3 * hidden * hidden + 7) / 8) * 8;
    Weights* weights = load_weights(model);
    assert(weights && weights->size - 7 == expected && "checkpoint architecture mismatch");
    Env env = {0};
    env.rng = seed ? seed : 1;
    puf_init(&env, kwargs);
    obs_t observations[2 * OBS_SIZE] = {0};
    float actions[2 * NUM_ATNS] = {0}, rewards[2] = {0}, terminals[2] = {0};
    for (int i = 0; i < env.num_agents; i++) {
        env.agents[i].observations = observations + i * OBS_SIZE;
        env.agents[i].actions = actions + i * NUM_ATNS;
        env.agents[i].rewards = rewards + i;
        env.agents[i].terminals = terminals + i;
        env.agents[i].action_mask = NULL;
        env.agents[i].policy = 0;
    }
    PufferNet* net = make_puffernet(weights, env.num_agents, OBS_SIZE, hidden, layers,
        sizes, NUM_ATNS);
    fprintf(stderr, "%s: %s hidden=%d layers=%d agents=%d bots=%d (%s)\n",
        play ? "play" : "watch", model, hidden, layers, env.num_agents, env.num_bots,
        deterministic ? "argmax" : "sample");
    puf_reset(&env);
    puf_render(&env);
    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_R)) {
            puf_reset(&env);
            for (int i = 0; i < env.num_agents; i++) {
                terminals[i] = 1;
            }
        }
        if (IsKeyPressed(KEY_H)) {
            g_robocode_hide_radar = !g_robocode_hide_radar;
        }
        rb_forward(net, observations, actions, terminals, deterministic);
        if (play) {
            actions[0] = 2; actions[1] = 4; actions[2] = 5;
            actions[3] = 5; actions[4] = 0;
            if (IsKeyDown(KEY_W)) {
                actions[0] = 3;
            }
            if (IsKeyDown(KEY_S)) {
                actions[0] = 1;
            }
            if (IsKeyDown(KEY_A)) {
                actions[1] = 3;
            }
            if (IsKeyDown(KEY_D)) {
                actions[1] = 5;
            }
            if (IsKeyDown(KEY_Q)) {
                actions[2] = 4;
            }
            if (IsKeyDown(KEY_E)) {
                actions[2] = 6;
            }
            if (IsKeyDown(KEY_LEFT)) {
                actions[3] = 0;
            }
            if (IsKeyDown(KEY_RIGHT)) {
                actions[3] = 8;
            }
            if (IsKeyDown(KEY_SPACE)) {
                actions[4] = 1;
            }
        }
        puf_step(&env);
        puf_render(&env);
    }
    puf_close(&env);
    free_puffernet(net);
    free(weights);
    puf_ini_free(&ini);
    return 0;
}
