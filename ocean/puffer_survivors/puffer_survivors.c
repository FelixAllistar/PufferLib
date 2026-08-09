/* Puffer Survivors standalone viewer (human play + policy watch).
 *
 * Build from repo root:
 *   ./build.sh puffer_survivors --fast
 *
 * Usage (run from repo root so config/ + checkpoints/ resolve):
 *   ./puffer_survivors                 # human play
 *   ./puffer_survivors play
 *   ./puffer_survivors watch           # latest checkpoint under checkpoints/puffer_survivors/
 *   ./puffer_survivors watch latest
 *   ./puffer_survivors watch PATH.bin
 *
 * Controls:
 *   WASD / arrows  move (human play; diagonals supported)
 *   A/D / arrows    select upgrade when offered (human)
 *   Space / Enter   confirm selected upgrade (human)
 *   1 / 2 / 3      select and confirm upgrade (numpad works too)
 *   R              restart run
 *   H              toggle hitboxes
 *   Q              cycle FX quality
 *   Esc            quit
 */

// Keep the experimental manual-player timing and renderer changes out of the
// normal training/eval build. build.sh puffer_survivors --fast compiles this
// standalone translation unit, while the training binary does not define it.
#define PS_FAST_RENDER 1
#include "puffer_survivors.h"
#include "puffercpu.h"

#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

static const double PS_FAST_SIM_DT = 1.0 / 60.0;
static const int PS_FAST_MAX_CATCHUP_STEPS = 5;
static const char* PS_ENV_NAME = "puffer_survivors";

static void ps_fast_record_metrics(PufferSurvivors* env, float frame_dt,
    double update_start, int steps, double sim_accumulator) {
    PSClient* client = ps_client(env);
    client->fast_frame_ms = frame_dt * 1000.0f;
    client->fast_update_ms = (float)((GetTime() - update_start) * 1000.0);
    client->fast_steps = steps;
    client->fast_render_alpha = ps_clampf(
        (float)(sim_accumulator / PS_FAST_SIM_DT), 0.0f, 1.0f);
}

static void ps_fast_sync_render_state(PufferSurvivors* env) {
    PSClient* client = ps_client(env);
    client->fast_previous_px = env->px;
    client->fast_previous_py = env->py;
    client->fast_interp_init = 1;
    client->fast_hit_time = 0.0f;
    client->fast_last_invuln_timer = env->invuln_timer;
}

static void ps_fast_prepare_step(PufferSurvivors* env) {
    PSClient* client = ps_client(env);
    client->fast_previous_px = env->px;
    client->fast_previous_py = env->py;
    client->fast_interp_init = 1;
}

static int read_move_held_mask(void) {
    int mask = 0;
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) mask |= 1;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) mask |= 2;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) mask |= 4;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) mask |= 8;
    return mask;
}

static float read_move_action(int mask) {
    int up = (mask & 1) != 0;
    int down = (mask & 2) != 0;
    int left = (mask & 4) != 0;
    int right = (mask & 8) != 0;

    if (up && left) return 5.0f;
    if (up && right) return 6.0f;
    if (down && left) return 7.0f;
    if (down && right) return 8.0f;
    if (up) return 1.0f;
    if (down) return 2.0f;
    if (left) return 3.0f;
    if (right) return 4.0f;
    return 0.0f;
}

static int read_upgrade_direct_pick(void) {
    if (IsKeyPressed(KEY_ONE) || IsKeyPressed(KEY_KP_1)) return 0;
    if (IsKeyPressed(KEY_TWO) || IsKeyPressed(KEY_KP_2)) return 1;
    if (IsKeyPressed(KEY_THREE) || IsKeyPressed(KEY_KP_3)) return 2;
    return -1;
}

static int read_upgrade_input(int* selection) {
    int direct = read_upgrade_direct_pick();
    if (direct >= 0) {
        *selection = direct;
        return *selection;
    }

    if (IsKeyPressed(KEY_A) || IsKeyPressed(KEY_LEFT)) {
        *selection = (*selection + PS_UPGRADE_SLOTS - 1) % PS_UPGRADE_SLOTS;
    } else if (IsKeyPressed(KEY_D) || IsKeyPressed(KEY_RIGHT)) {
        *selection = (*selection + 1) % PS_UPGRADE_SLOTS;
    }

    if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
        return *selection;
    }
    return -1;
}

static void ps_fast_set_upgrade_selection(PufferSurvivors* env, int selection) {
    PSClient* client = ps_client(env);
    client->fast_upgrade_selection = selection;
}

static int ps_has_suffix(const char* s, const char* suffix) {
    size_t n = strlen(s);
    size_t m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

static void ps_find_latest_checkpoint(const char* dir, char* out, size_t out_size,
        time_t* best_time) {
    DIR* dp = opendir(dir);
    if (!dp) return;

    struct dirent* ent = NULL;
    while ((ent = readdir(dp))) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);

        struct stat st;
        if (stat(path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            ps_find_latest_checkpoint(path, out, out_size, best_time);
        } else if (S_ISREG(st.st_mode) && ps_has_suffix(path, ".bin") &&
                st.st_ctime >= *best_time) {
            *best_time = st.st_ctime;
            snprintf(out, out_size, "%s", path);
        }
    }
    closedir(dp);
}

static int ps_resolve_model_path(const char* arg, char* out, size_t out_size) {
    if (!arg || !*arg || strcmp(arg, "latest") == 0) {
        const char* root = "checkpoints/puffer_survivors";
        out[0] = 0;
        time_t best = 0;
        ps_find_latest_checkpoint(root, out, out_size, &best);
        if (!out[0]) {
            fprintf(stderr, "no .bin checkpoints found in %s\n", root);
            return -1;
        }
        return 0;
    }
    snprintf(out, out_size, "%s", arg);
    return 0;
}

static void ps_print_usage(const char* argv0) {
    fprintf(stderr,
        "usage:\n"
        "  %s                 human play (new renderer)\n"
        "  %s play            human play\n"
        "  %s watch [latest|PATH.bin] [--deterministic]\n"
        "\n"
        "Run from the repo root. Watch reads policy settings from\n"
        "config/puffer_survivors.ini.\n",
        argv0, argv0, argv0);
}

// Weight blob outlives the net layers (Linear weight ptrs alias into it).
static Weights* g_policy_weights = NULL;

static void ps_watch_policy_arch(int* hidden, int* layers) {
    Ini ini = {0};
    puf_ini_load_env(&ini, PS_ENV_NAME, 0, NULL);
    *hidden = puf_ini_get_int(&ini, "policy", "hidden_size");
    *layers = puf_ini_get_int(&ini, "policy", "num_layers");
    puf_ini_free(&ini);
}

static int ps_expected_policy_floats(int hidden, int layers) {
    int act_sizes[] = ACT_SIZES;
    int atn_sum = 0;
    for (int i = 0; i < NUM_ATNS; i++) atn_sum += act_sizes[i];
    return hidden * PS_OBS_SIZE
        + (atn_sum + 1) * hidden
        + layers * 3 * hidden * hidden;
}

static void ps_reset_policy_state(PufferNet* net) {
    if (!net || !net->mingru || !net->mingru->state) return;
    int count = net->mingru->num_layers
        * net->mingru->batch_size * net->mingru->hidden_size;
    memset(net->mingru->state, 0, (size_t)count * sizeof(float));
}

static PufferNet* ps_load_policy(const char* path) {
    Weights* weights = load_weights(path);
    if (!weights) {
        fprintf(stderr, "failed to load weights: %s\n", path);
        return NULL;
    }

    int act_sizes[] = ACT_SIZES;
    int num_actions = (int)(sizeof(act_sizes) / sizeof(act_sizes[0]));
    int hidden = 16;
    int layers = 2;
    ps_watch_policy_arch(&hidden, &layers);
    int expected_floats = ps_expected_policy_floats(hidden, layers);

    fprintf(stderr, "watch: %s  (hidden=%d layers=%d)\n",
        path, hidden, layers);

    PufferNet* net = make_puffernet(weights, 1, PS_OBS_SIZE, hidden, layers,
        act_sizes, num_actions);
    if (weights->idx != expected_floats) {
        fprintf(stderr,
            "checkpoint/model mismatch: expected %d floats for hidden=%d layers=%d, "
            "loader consumed %d\n",
            expected_floats, hidden, layers, weights->idx);
        free_puffernet(net);
        free(weights);
        return NULL;
    }
    g_policy_weights = weights;
    return net;
}

// Watch-only inference: keep training stochastic, but make the visible policy
// deterministic by choosing the highest-logit action in each discrete head.
static void ps_forward_policy_argmax(PufferNet* net, float* observations,
        float* actions) {
    linear(net->encoder, observations);
    mingru(net->mingru, net->encoder->output);
    linear(net->decoder, net->mingru->output);
    if (net->is_continuous) {
        _gaussian_mean(net->decoder->output, actions,
            net->num_agents, net->num_actions);
    } else {
        argmax_multidiscrete(net->multidiscrete, net->decoder->output, actions);
    }
}

static void ps_apply_env_config(Env* env) {
    if (!FileExists("config/puffer_survivors.ini")) {
        fprintf(stderr,
            "missing config/puffer_survivors.ini; run ./puffer_survivors from the repository root\n");
        exit(1);
    }

    Ini ini = {0};
    puf_ini_load_env(&ini, PS_ENV_NAME, 0, NULL);
    Dict* env_kwargs = puf_ini_section(&ini, "env", 0);
    env->cfg = ps_config_from_kwargs(env_kwargs);
    env->show_hitboxes = (int)ps_kwarg(env_kwargs, "show_hitboxes");
    puf_ini_free(&ini);
}

int main(int argc, char** argv) {
    int watch_mode = 0;
    int watch_deterministic = 0;
    const char* model_arg = NULL;

    if (argc >= 2) {
        if (strcmp(argv[1], "watch") == 0) {
            watch_mode = 1;
            model_arg = "latest";
            int model_arg_set = 0;
            for (int i = 2; i < argc; i++) {
                if (strcmp(argv[i], "--deterministic") == 0) {
                    watch_deterministic = 1;
                } else if (!model_arg_set && argv[i][0] != '-') {
                    model_arg = argv[i];
                    model_arg_set = 1;
                } else {
                    fprintf(stderr, "unknown watch argument '%s'\n", argv[i]);
                    ps_print_usage(argv[0]);
                    return 1;
                }
            }
        } else if (strcmp(argv[1], "play") == 0) {
            // human play (default)
        } else if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "-h") == 0
                || strcmp(argv[1], "--help") == 0) {
            ps_print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unknown mode '%s'\n", argv[1]);
            ps_print_usage(argv[0]);
            return 1;
        }
    }

    PufferNet* net = NULL;
    if (watch_mode) {
        char path[4096];
        if (ps_resolve_model_path(model_arg, path, sizeof(path)) != 0) return 1;
        net = ps_load_policy(path);
        if (!net) return 1;
    }

    float observations[PS_OBS_SIZE] = {0};
    float actions[NUM_ATNS] = {0};
    float rewards[1] = {0};
    float terminals[1] = {0};

    Env env = {0};
    env.num_agents = 1;
    env.rng = 1u;
    env.agents[0].observations = observations;
    env.agents[0].actions = actions;
    env.agents[0].rewards = rewards;
    env.agents[0].terminals = terminals;
    env.agents[0].action_mask = NULL;
    env.agents[0].policy = 0;

    ps_apply_env_config(&env);
    c_reset(&env);
    c_render(&env);
    ps_fast_sync_render_state(&env);

    double sim_accumulator = 0.0;
    int upgrade_selection = 0;
    int movement_lock_active = 0;
    int movement_lock_mask = 0;
    while (!WindowShouldClose()) {
        double update_start = GetTime();
        float frame_dt = GetFrameTime();
        if (frame_dt <= 0.0f) frame_dt = (float)PS_FAST_SIM_DT;
        if (frame_dt > 0.10f) frame_dt = 0.10f;
        sim_accumulator += frame_dt;

        if (IsKeyPressed(KEY_R)) {
            c_reset(&env);
            if (net) ps_reset_policy_state(net);
            sim_accumulator = 0.0;
            upgrade_selection = 0;
            movement_lock_active = 0;
            movement_lock_mask = 0;
            ps_fast_sync_render_state(&env);
        }

        int pending_at_frame_start = env.pending_upgrade;

        if (watch_mode) {
            // Policy drives every sim step, including upgrade picks.
            // No human freeze — matches train-time action cadence.
            if (env.pending_upgrade) {
                // Still advance at fixed dt; policy chooses the card.
                // (one-shot upgrade action consumed inside the step loop)
            }
        } else {
            int movement_mask = read_move_held_mask();
            if (movement_lock_active && movement_mask != movement_lock_mask) {
                // A release or a new held-key combination is an intentional
                // change, so movement may resume immediately after it.
                movement_lock_active = 0;
            }
            actions[0] = movement_lock_active ? 0.0f : read_move_action(movement_mask);

            if (env.pending_upgrade) {
                int picked = read_upgrade_input(&upgrade_selection);
                if (picked < 0) {
                    // Freeze the sim until the player chooses a card.
                    sim_accumulator = 0.0;
                    ps_fast_set_upgrade_selection(&env, upgrade_selection);
                    ps_fast_record_metrics(&env, frame_dt, update_start, 0, sim_accumulator);
                    c_render(&env);
                    continue;
                }
                actions[1] = (float)picked;
                // Do not turn a navigation key that is still held into an
                // accidental movement step after the card is confirmed. The
                // player only needs to release or change the held state once.
                movement_lock_active = movement_mask != 0;
                movement_lock_mask = movement_mask;
                actions[0] = movement_lock_active ? 0.0f : read_move_action(movement_mask);
                // Consume the upgrade immediately instead of waiting for the
                // next accumulator boundary.
                sim_accumulator = PS_FAST_SIM_DT;
            } else {
                actions[1] = 0.0f;
            }
        }

        int steps = 0;
        int upgrade_confirmed = !watch_mode && pending_at_frame_start && actions[1] >= 0.0f;
        while (sim_accumulator >= PS_FAST_SIM_DT
            && steps < PS_FAST_MAX_CATCHUP_STEPS
            && (watch_mode || !env.pending_upgrade || upgrade_confirmed)) {
            if (watch_mode) {
                if (watch_deterministic) {
                    ps_forward_policy_argmax(net, observations, actions);
                } else {
                    forward_puffernet(net, observations, actions);
                }
                if (!env.pending_upgrade) actions[1] = 0.0f;
                ps_fast_set_upgrade_selection(&env, (int)actions[1]);
            }
            ps_fast_prepare_step(&env);
            c_step(&env);
            steps++;
            sim_accumulator -= PS_FAST_SIM_DT;
            // Upgrade choices are one-shot actions. Movement remains held
            // across fixed steps in the same rendered frame (human mode).
            if (!watch_mode) actions[1] = 0.0f;
            upgrade_confirmed = 0;
            if (env.agents[0].terminals[0] > 0.0f) {
                if (net) ps_reset_policy_state(net);
                // Auto-reset in watch so you can leave it running.
                if (watch_mode) {
                    c_reset(&env);
                    ps_reset_policy_state(net);
                }
                ps_fast_sync_render_state(&env);
                sim_accumulator = 0.0;
                break;
            }
            if (env.pending_upgrade && !watch_mode) sim_accumulator = 0.0;
        }

        if (!watch_mode && env.pending_upgrade && steps > 0) upgrade_selection = 0;
        if (!watch_mode) ps_fast_set_upgrade_selection(&env, upgrade_selection);
        ps_fast_record_metrics(&env, frame_dt, update_start, steps, sim_accumulator);
        c_render(&env);
    }

    c_close(&env);
    if (net) free_puffernet(net);
    free(g_policy_weights);
    g_policy_weights = NULL;
    return 0;
}
