// Standalone play / bench for Bomberman.
//
//   bash build.sh bomberman --fast
//   ./bomberman                          # human vs random
//   ./bomberman watch                    # latest ckpt vs itself (both AI)
//   ./bomberman watch path/to/model.bin  # that ckpt vs itself
//   ./bomberman play  path/to/model.bin  # human vs that ckpt
//   ./bomberman bench
//
// For A vs B selfplay *eval* (headless, many games), use the train binary:
//   ./puffer match bomberman \
//       base.load_model_path=checkpoints/bomberman/.../A.bin \
//       base.load_enemy_model_path=checkpoints/bomberman/.../B.bin \
//       base.num_games=4096
//
// For Raylib watch of a trained policy (same arch as train):
//   bash build.sh bomberman --cpu
//   ./build_cpu bomberman base.load_model_path=latest

#include "bomberman.h"
#include "puffercpu.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static void bind_agents(Env* env, obs_t* observations, float* actions,
        float* rewards, float* terminals) {
    for (int i = 0; i < env->num_agents; i++) {
        env->agents[i].observations = observations + (size_t)i * OBS_SIZE;
        env->agents[i].actions = actions + (size_t)i * NUM_ATNS;
        env->agents[i].rewards = rewards + i;
        env->agents[i].terminals = terminals + i;
        env->agents[i].action_mask = NULL;
        env->agents[i].policy = (i == 0) ? 0 : 1;
    }
}

static int has_suffix(const char* s, const char* suffix) {
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

static void find_latest_bin(const char* dir, char* out, size_t out_size, time_t* best) {
    DIR* dp = opendir(dir);
    if (!dp) return;
    struct dirent* ent;
    while ((ent = readdir(dp))) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            find_latest_bin(path, out, out_size, best);
        } else if (S_ISREG(st.st_mode) && has_suffix(path, ".bin") && st.st_ctime >= *best) {
            *best = st.st_ctime;
            snprintf(out, out_size, "%s", path);
        }
    }
    closedir(dp);
}

static const char* resolve_model_path(const char* arg, char* buf, size_t buf_sz) {
    if (arg && strcmp(arg, "latest") != 0) {
        return arg;
    }
    buf[0] = 0;
    time_t best = 0;
    find_latest_bin("checkpoints/bomberman", buf, buf_sz, &best);
    if (!buf[0]) {
        fprintf(stderr, "No .bin under checkpoints/bomberman/\n");
        fprintf(stderr, "Train first:  ./puffer train bomberman\n");
        return NULL;
    }
    return buf;
}

static int pressed_keyboard_action(void) {
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) return BM_ACT_UP;
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) return BM_ACT_DOWN;
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) return BM_ACT_LEFT;
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) return BM_ACT_RIGHT;
    if (IsKeyPressed(KEY_SPACE)) return BM_ACT_BOMB;
    return BM_ACT_STAY;
}

static int held_keyboard_action(void) {
    int action = BM_ACT_STAY;
    if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) action = BM_ACT_UP;
    if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) action = BM_ACT_DOWN;
    if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) action = BM_ACT_LEFT;
    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) action = BM_ACT_RIGHT;
    if (IsKeyDown(KEY_SPACE)) action = BM_ACT_BOMB;
    return action;
}

static void apply_keyboard_agent0(Env* env, int* pending_action) {
    float* a0 = env->agents[0].actions;
    int held = held_keyboard_action();
    int action = held != BM_ACT_STAY ? held : *pending_action;
    a0[0] = (float)action;

    if (action == BM_ACT_STAY) return;
    if (bm_action_legal(&env->match, 0, action)) {
        *pending_action = BM_ACT_STAY;
    } else if (action < BM_ACT_UP || action > BM_ACT_RIGHT
            || env->match.agents[0].move_cd == 0) {
        // Keep a tapped direction through the one-step movement cooldown, but
        // do not queue bombs or wall collisions for some surprising later time.
        *pending_action = BM_ACT_STAY;
    }
}

// The training/eval sampler masks illegal actions before sampling. Puffercpu's
// generic standalone helper does not accept a mask, so reproduce the same
// masked categorical choice here. Without this, watch mode frequently samples
// cooldown-blocked movement and unavailable bombs and looks artificially jerky.
static void forward_masked_bomberman(PufferNet* net, Env* env,
        float* observations, float* actions) {
    linear(net->encoder, observations);
    mingru(net->mingru, net->encoder->output);
    linear(net->decoder, net->mingru->output);

    const int stride = BM_NUM_ACTIONS + 1; // action logits plus fused value
    for (int agent = 0; agent < env->num_agents; agent++) {
        const float* logits = net->decoder->output + (size_t)agent * stride;
        float max_logit = -INFINITY;
        for (int action = 0; action < BM_NUM_ACTIONS; action++) {
            if (bm_action_legal(&env->match, agent, action)
                    && logits[action] > max_logit) {
                max_logit = logits[action];
            }
        }

        float sum = 0.0f;
        for (int action = 0; action < BM_NUM_ACTIONS; action++) {
            if (bm_action_legal(&env->match, agent, action)) {
                sum += expf(logits[action] - max_logit);
            }
        }

        float target = (rand() / ((float)RAND_MAX + 1.0f)) * sum;
        float cumulative = 0.0f;
        int sampled = BM_ACT_STAY; // stay is always legal for a live agent
        for (int action = 0; action < BM_NUM_ACTIONS; action++) {
            if (!bm_action_legal(&env->match, agent, action)) continue;
            cumulative += expf(logits[action] - max_logit);
            sampled = action;
            if (target < cumulative) break;
        }
        actions[agent] = (float)sampled;
    }
}

static void random_other_agents(Env* env) {
    for (int i = 1; i < env->num_agents; i++) {
        // Bias away from bomb-spam: 5 move/stay, rare bomb
        int r = rand_r(&env->rng) % 12;
        env->agents[i].actions[0] = (float)((r < 10) ? (r % 5) : BM_ACT_BOMB);
    }
}

static Env make_play_env(int num_agents) {
    Env env = {0};
    env.cfg = bm_default_config();
    env.cfg.num_agents = num_agents;
    // Play/watch: long cap so you can see full games (train uses config max_ticks).
    env.cfg.max_ticks = 30000;
    env.num_agents = num_agents;
    env.rng = 42;
    for (int i = 0; i < num_agents; i++) {
        env.agents[i].policy = (i == 0) ? 0 : 1;
    }
    return env;
}

// mode: 0 = human vs random, 1 = human vs net, 2 = net vs net (watch)
static void play_loop(int mode, const char* model_path) {
    int num_agents = 2;
    Env env = make_play_env(num_agents);
    env.hold_on_done = 1; // freeze death frame in play/watch

    obs_t* observations = (obs_t*)calloc((size_t)num_agents * OBS_SIZE, sizeof(obs_t));
    float* actions = (float*)calloc((size_t)num_agents * NUM_ATNS, sizeof(float));
    float* rewards = (float*)calloc((size_t)num_agents, sizeof(float));
    float* terminals = (float*)calloc((size_t)num_agents, sizeof(float));
    bind_agents(&env, observations, actions, rewards, terminals);
    puf_reset(&env);

    PufferNet* net = NULL;
    Weights* weights = NULL;
    int act_sizes[] = ACT_SIZES;
    // Defaults match config/bomberman.ini [policy]; override via env if needed.
    int hidden = 128;
    int layers = 2;
    const char* h_env = getenv("BOMBERMAN_HIDDEN");
    const char* l_env = getenv("BOMBERMAN_LAYERS");
    if (h_env && *h_env) hidden = atoi(h_env);
    if (l_env && *l_env) layers = atoi(l_env);

    if (mode != 0) {
        if (!model_path) {
            fprintf(stderr, "Need a model path (or 'latest')\n");
            goto cleanup;
        }
        weights = load_weights(model_path);
        if (!weights) {
            fprintf(stderr, "Failed to load weights: %s\n", model_path);
            goto cleanup;
        }
        // Same arch as config/bomberman.ini [policy]
        net = make_puffernet(weights, num_agents, OBS_SIZE, hidden, layers,
            act_sizes, NUM_ATNS);
        printf("Loaded policy: %s  (hidden=%d layers=%d)\n", model_path, hidden, layers);
    }

    env.client = NULL;
    puf_render(&env);
    if (!IsWindowReady()) {
        fprintf(stderr, "Bomberman: failed to open Raylib window (DISPLAY=%s)\n",
            getenv("DISPLAY") ? getenv("DISPLAY") : "(unset)");
        goto cleanup;
    }

    if (mode == 0) {
        printf("Human vs RANDOM (no network). Opponent bombs randomly → often suicide.\n");
        printf("  For AI opponent:  ./bomberman play     (loads latest checkpoint)\n");
        printf("  For AI vs AI:     ./bomberman watch\n");
        printf("Controls: WASD/arrows, Space bomb, R reset, Esc quit\n");
    } else if (mode == 1) {
        printf("Human vs checkpoint (latest unless you pass a path). Cyan=you, red=AI.\n");
        printf("Shift=AI controls you too. R reset, Esc quit.\n");
    } else {
        printf("Watch: both agents use checkpoint (latest unless you pass a path).\n");
        printf("R reset, Esc quit.\n");
    }
    printf("Game step ~8 Hz. HUD t=step/max (timeout). Death freezes ~1.5s then new round.\n");
    printf("OBS_SIZE=%d  max_ticks=%d  bomb_timer=%d\n",
        OBS_SIZE, env.cfg.max_ticks, env.cfg.bomb_timer);
    fflush(stdout);

    int frame = 0;
    int freeze_frames = 0;
    int pending_human_action = BM_ACT_STAY;
    // ~8 game-steps per second at 30 render FPS
    const int frames_per_step = 4;
    while (!WindowShouldClose()) {
        int pressed = pressed_keyboard_action();
        if (pressed != BM_ACT_STAY) pending_human_action = pressed;

        if (freeze_frames > 0) {
            freeze_frames--;
            puf_render(&env);
            if (freeze_frames == 0) {
                puf_reset(&env); // leave death board, start next round
            }
            frame++;
            continue;
        }

        if (frame % frames_per_step == 0) {
            int run_net = (mode != 0);
            if (run_net) {
                forward_masked_bomberman(net, &env, observations, actions);
            }
            if (mode == 0) {
                apply_keyboard_agent0(&env, &pending_human_action);
                random_other_agents(&env);
            } else if (mode == 1) {
                float a1 = actions[1];
                if (!IsKeyDown(KEY_LEFT_SHIFT)) {
                    apply_keyboard_agent0(&env, &pending_human_action);
                }
                env.agents[1].actions[0] = a1;
            }

            puf_step(&env);

            // Death frame held (hold_on_done). Pause on the corpse/flame board.
            if (env.match.done) {
                freeze_frames = 45; // ~1.5s at 30 FPS
            }
        }
        puf_render(&env);
        frame++;
    }

cleanup:
    if (env.client) {
        free(env.client);
        env.client = NULL;
    }
    if (IsWindowReady()) CloseWindow();
    if (net) free_puffernet(net);
    if (weights) free(weights);
    free(observations);
    free(actions);
    free(rewards);
    free(terminals);
}

void performance_test(void) {
    Env env = make_play_env(4);
    obs_t* observations = (obs_t*)calloc((size_t)env.num_agents * OBS_SIZE, sizeof(obs_t));
    float* actions = (float*)calloc((size_t)env.num_agents * NUM_ATNS, sizeof(float));
    float* rewards = (float*)calloc((size_t)env.num_agents, sizeof(float));
    float* terminals = (float*)calloc((size_t)env.num_agents, sizeof(float));
    bind_agents(&env, observations, actions, rewards, terminals);
    puf_reset(&env);

    long start = time(NULL);
    long steps = 0;
    while (time(NULL) - start < 3) {
        for (int a = 0; a < env.num_agents; a++) {
            env.agents[a].actions[0] = (float)(rand_r(&env.rng) % BM_NUM_ACTIONS);
        }
        puf_step(&env);
        steps++;
    }
    long dt = time(NULL) - start;
    if (dt < 1) dt = 1;
    printf("Bomberman CPU SPS: %ld (matches/s=%ld)\n",
        steps * env.num_agents / dt, steps / dt);
    printf("OBS_SIZE=%d n=%.0f slot0=%.3f wins=%.0f\n",
        OBS_SIZE, env.log.n, env.log.n > 0 ? env.log.slot_0_score / env.log.n : 0.0f,
        env.log.wins);

    free(observations);
    free(actions);
    free(rewards);
    free(terminals);
}

static void usage(const char* argv0) {
    fprintf(stderr,
        "usage:\n"
        "  %s                  human vs RANDOM (no AI load)\n"
        "  %s play [model]     human vs checkpoint (default: latest .bin)\n"
        "  %s watch [model]    checkpoint vs itself (default: latest .bin)\n"
        "  %s bench            headless SPS\n"
        "\n"
        "Plain '%s' does NOT load a model — use play/watch for that.\n"
        "Play/watch max_ticks=30000 (long games).\n"
        "\n"
        "Headless A vs B:\n"
        "  ./puffer match bomberman \\\n"
        "      base.load_model_path=PATH_A \\\n"
        "      base.load_enemy_model_path=PATH_B \\\n"
        "      base.num_games=4096\n",
        argv0, argv0, argv0, argv0, argv0);
}

int main(int argc, char** argv) {
    char path_buf[4096];
    if (argc < 2) {
        play_loop(0, NULL);
        return 0;
    }
    if (strcmp(argv[1], "bench") == 0) {
        performance_test();
        return 0;
    }
    if (strcmp(argv[1], "play") == 0) {
        const char* p = resolve_model_path(argc > 2 ? argv[2] : "latest", path_buf, sizeof(path_buf));
        if (!p) return 1;
        play_loop(1, p);
        return 0;
    }
    if (strcmp(argv[1], "watch") == 0) {
        const char* p = resolve_model_path(argc > 2 ? argv[2] : "latest", path_buf, sizeof(path_buf));
        if (!p) return 1;
        play_loop(2, p);
        return 0;
    }
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        usage(argv[0]);
        return 0;
    }
    usage(argv[0]);
    return 1;
}
