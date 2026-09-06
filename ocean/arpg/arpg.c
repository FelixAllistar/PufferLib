/* arpg standalone viewer (human play + policy autoplay).
 *
 * Build from repo root:
 *   ./build.sh arpg --fast
 *
 * Run from repo root so config/arpg.ini resolves. Movement is WASD by
 * default; every binding lives in the [keys] section of config/arpg.ini and
 * accepts multiple comma-separated keys (e.g. "up = W, UP").
 *
 * Usage:
 *   ./arpg                      # autoplay if a checkpoint exists, else human
 *   ./arpg play                 # human (pets always auto - they are scripted in C)
 *   ./arpg watch [latest|PATH.bin] [--deterministic]
 *
 * Default controls:
 *   WASD / arrows   move (screen-relative)
 *   Space           summon selected pet (Z/X/C select wisp/fang/aegis)
 *   Q / E / F       dash / nova / frost
 *   G / V           build totem / wall (costs shards)
 *   1 / 2 / 3 / 4   squad order follow / attack / guard / focus
 *   T               toggle autoplay (policy drives avatar; pets always auto)
 *   R               restart run
 *   H               toggle hitboxes
 *   Esc             quit
 */

#include "arpg.h"
#include "puffercpu.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

static const double AR_FAST_SIM_DT = 1.0 / 60.0;
static const int AR_FAST_MAX_CATCHUP_STEPS = 5;

// ---------------------------------------------------------------------------
// Configurable key bindings.
// ---------------------------------------------------------------------------

typedef struct {
    int keys[4];
    int count;
} ARBinding;

typedef struct {
    const char* name;
    int key;
} ARKeyName;

static const ARKeyName AR_KEY_NAMES[] = {
    {"SPACE", KEY_SPACE},
    {"ESCAPE", KEY_ESCAPE},
    {"ENTER", KEY_ENTER},
    {"KP_ENTER", KEY_KP_ENTER},
    {"TAB", KEY_TAB},
    {"LEFT_SHIFT", KEY_LEFT_SHIFT},
    {"RIGHT_SHIFT", KEY_RIGHT_SHIFT},
    {"LEFT_CONTROL", KEY_LEFT_CONTROL},
    {"LEFT_ALT", KEY_LEFT_ALT},
    {"UP", KEY_UP},
    {"DOWN", KEY_DOWN},
    {"LEFT", KEY_LEFT},
    {"RIGHT", KEY_RIGHT},
    {"ONE", KEY_ONE},
    {"TWO", KEY_TWO},
    {"THREE", KEY_THREE},
    {"FOUR", KEY_FOUR},
};

static int ar_lookup_key(const char* name) {
    for (size_t i = 0; i < sizeof(AR_KEY_NAMES) / sizeof(AR_KEY_NAMES[0]); i++) {
        if (strcmp(AR_KEY_NAMES[i].name, name) == 0) return AR_KEY_NAMES[i].key;
    }
    if (name[0] >= 'A' && name[0] <= 'Z' && name[1] == '\0') {
        return KEY_A + (name[0] - 'A');
    }
    if (name[0] >= '0' && name[0] <= '9' && name[1] == '\0') {
        return KEY_ZERO + (name[0] - '0');
    }
    if (name[0] == 'K' && name[1] == 'P' && name[2] >= '0' && name[2] <= '9'
            && name[3] == '\0') {
        return KEY_KP_0 + (name[2] - '0');
    }
    if (name[0] == 'F' && name[0] != '\0' && name[1] >= '1' && name[1] <= '9'
            && name[2] == '\0') {
        return KEY_F1 + (name[1] - '1');
    }
    return KEY_NULL;
}

// Missing [keys] entries fall back instead of exiting, so older configs
// without the autoplay row still work.
static ARBinding ar_binding_from_ini_opt(Ini* ini, const char* section,
        const char* name, int fallback) {
    ARBinding binding = {0};
    const char* value = NULL;
    for (int i = 0; i < ini->num_sections; i++) {
        if (strcmp(ini->sections[i].name, section) == 0) {
            DictItem* item = dict_find(&ini->sections[i], name);
            if (item && item->str) value = item->str;
            break;
        }
    }
    if (value == NULL) {
        binding.keys[binding.count++] = fallback;
        return binding;
    }

    // Comma-separated key names with whitespace trimmed, e.g. "up = W, UP".
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s", value);
    size_t i = 0;
    while (buffer[i] != '\0' && binding.count < 4) {
        while (buffer[i] == ' ' || buffer[i] == '\t' || buffer[i] == ',') i++;
        size_t start = i;
        while (buffer[i] != '\0' && buffer[i] != ',') i++;
        size_t end = i;
        while (end > start && (buffer[end - 1] == ' ' || buffer[end - 1] == '\t')) {
            end--;
        }
        char token[32];
        size_t len = end - start < sizeof(token) - 1 ? end - start : sizeof(token) - 1;
        memcpy(token, buffer + start, len);
        token[len] = '\0';
        int key = ar_lookup_key(token);
        if (key != KEY_NULL) binding.keys[binding.count++] = key;
    }
    if (binding.count == 0) binding.keys[binding.count++] = fallback;
    return binding;
}

static int ar_binding_down(ARBinding binding) {
    for (int i = 0; i < binding.count; i++) {
        if (IsKeyDown(binding.keys[i])) return 1;
    }
    return 0;
}

static int ar_binding_pressed(ARBinding binding) {
    for (int i = 0; i < binding.count; i++) {
        if (IsKeyPressed(binding.keys[i])) return 1;
    }
    return 0;
}

typedef struct {
    ARBinding up, down, left, right;
    ARBinding summon;
    ARBinding dash, nova, frost;
    ARBinding build_totem, build_wall;
    ARBinding class_wisp, class_fang, class_aegis;
    ARBinding order_follow, order_attack, order_guard, order_focus;
    ARBinding reset;
    ARBinding hitboxes;
    ARBinding autoplay;
} ARControls;

// Keep the ini alive for the lifetime of the bindings; strings point into it.
static Ini g_controls_ini;

static void ar_load_config(ARPG* env, ARControls* controls) {
    if (!FileExists("config/arpg.ini")) {
        fprintf(stderr,
            "missing config/arpg.ini; run ./arpg from the repository root\n");
        exit(1);
    }
    g_controls_ini = (Ini){0};
    puf_ini_load_env(&g_controls_ini, "arpg", 0, NULL);
    Dict* env_kwargs = puf_ini_section(&g_controls_ini, "env", 0);
    env->cfg = ar_config_from_kwargs(env_kwargs);
    env->show_hitboxes = (int)dict_get(env_kwargs, "show_hitboxes");

    *controls = (ARControls){
        .up = ar_binding_from_ini_opt(&g_controls_ini, "keys", "up", KEY_W),
        .down = ar_binding_from_ini_opt(&g_controls_ini, "keys", "down", KEY_S),
        .left = ar_binding_from_ini_opt(&g_controls_ini, "keys", "left", KEY_A),
        .right = ar_binding_from_ini_opt(&g_controls_ini, "keys", "right", KEY_D),
        .summon = ar_binding_from_ini_opt(&g_controls_ini, "keys", "summon", KEY_SPACE),
        .dash = ar_binding_from_ini_opt(&g_controls_ini, "keys", "dash", KEY_Q),
        .nova = ar_binding_from_ini_opt(&g_controls_ini, "keys", "nova", KEY_E),
        .frost = ar_binding_from_ini_opt(&g_controls_ini, "keys", "frost", KEY_F),
        .build_totem = ar_binding_from_ini_opt(&g_controls_ini, "keys", "build_totem", KEY_G),
        .build_wall = ar_binding_from_ini_opt(&g_controls_ini, "keys", "build_wall", KEY_V),
        .class_wisp = ar_binding_from_ini_opt(&g_controls_ini, "keys", "class_wisp", KEY_Z),
        .class_fang = ar_binding_from_ini_opt(&g_controls_ini, "keys", "class_fang", KEY_X),
        .class_aegis = ar_binding_from_ini_opt(&g_controls_ini, "keys", "class_aegis", KEY_C),
        .order_follow = ar_binding_from_ini_opt(&g_controls_ini, "keys", "order_follow", KEY_ONE),
        .order_attack = ar_binding_from_ini_opt(&g_controls_ini, "keys", "order_attack", KEY_TWO),
        .order_guard = ar_binding_from_ini_opt(&g_controls_ini, "keys", "order_guard", KEY_THREE),
        .order_focus = ar_binding_from_ini_opt(&g_controls_ini, "keys", "order_focus", KEY_FOUR),
        .reset = ar_binding_from_ini_opt(&g_controls_ini, "keys", "reset", KEY_R),
        .hitboxes = ar_binding_from_ini_opt(&g_controls_ini, "keys", "hitboxes", KEY_H),
        .autoplay = ar_binding_from_ini_opt(&g_controls_ini, "keys", "autoplay", KEY_T),
    };
}

// ---------------------------------------------------------------------------
// Input -> actions.
// ---------------------------------------------------------------------------

static int ar_read_move_mask(ARControls* controls) {
    int mask = 0;
    if (ar_binding_down(controls->up)) mask |= 1;
    if (ar_binding_down(controls->down)) mask |= 2;
    if (ar_binding_down(controls->left)) mask |= 4;
    if (ar_binding_down(controls->right)) mask |= 8;
    return mask;
}

// Action layout mirrors ar_steer_player (screen-relative): 0 idle, 1 up,
// 2 down, 3 left, 4 right, 5 up-left, 6 up-right, 7 down-left, 8 down-right.
static float ar_read_move_action(int mask) {
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

// ---------------------------------------------------------------------------
// Policy autoplay (slice 2: PPO avatar, scripted pets).
// ---------------------------------------------------------------------------

static int ar_has_suffix(const char* s, const char* suffix) {
    size_t n = strlen(s);
    size_t m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

static void ar_find_latest(const char* dir, char* out, size_t out_size,
        time_t* best) {
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
            ar_find_latest(path, out, out_size, best);
        } else if (S_ISREG(st.st_mode) && ar_has_suffix(path, ".bin") &&
                st.st_ctime >= *best) {
            *best = st.st_ctime;
            snprintf(out, out_size, "%s", path);
        }
    }
    closedir(dp);
}

// Returns 0 with out filled, -1 when nothing found (caller falls back).
static int ar_try_latest(char* out, size_t out_size) {
    out[0] = 0;
    time_t best = 0;
    ar_find_latest("checkpoints/arpg", out, out_size, &best);
    return out[0] ? 0 : -1;
}

static void ar_print_usage(const char* argv0) {
    fprintf(stderr,
        "usage:\n"
        "  %s                      autoplay if a checkpoint exists, else human\n"
        "  %s play                 human (pets always auto)\n"
        "  %s watch [latest|PATH.bin] [--deterministic]\n",
        argv0, argv0, argv0);
}

static int ar_expected_floats(int hidden, int layers) {
    int act_sizes[] = ACT_SIZES;
    int atn_sum = 0;
    for (int i = 0; i < NUM_ATNS; i++) atn_sum += act_sizes[i];
    return hidden * AR_OBS_SIZE + (atn_sum + 1) * hidden
        + layers * 3 * hidden * hidden;
}

static void ar_reset_policy(PufferNet* net) {
    if (!net || !net->mingru || !net->mingru->state) return;
    int n = net->mingru->num_layers * net->mingru->batch_size
        * net->mingru->hidden_size;
    memset(net->mingru->state, 0, (size_t)n * sizeof(float));
}

static void ar_forward_argmax(PufferNet* net, float* obs, float* atn) {
    linear(net->encoder, obs);
    mingru(net->mingru, net->encoder->output);
    linear(net->decoder, net->mingru->output);
    argmax_multidiscrete(net->multidiscrete, net->decoder->output, atn);
}

static PufferNet* ar_load_policy(const char* path, Weights** out_w) {
    Weights* w = load_weights(path);
    if (!w) {
        fprintf(stderr, "failed to load weights: %s\n", path);
        return NULL;
    }
    int act_sizes[] = ACT_SIZES;
    int hidden = puf_ini_get_int(&g_controls_ini, "policy", "hidden_size");
    int layers = puf_ini_get_int(&g_controls_ini, "policy", "num_layers");
    int expected = ar_expected_floats(hidden, layers);
    fprintf(stderr, "autoplay: %s (hidden=%d layers=%d)\n", path, hidden, layers);
    PufferNet* net = make_puffernet(w, 1, AR_OBS_SIZE, hidden, layers,
        act_sizes, NUM_ATNS);
    if (w->idx != expected) {
        fprintf(stderr,
            "checkpoint/model mismatch: expected %d floats for hidden=%d layers=%d, loader consumed %d\n",
            expected, hidden, layers, w->idx);
        free_puffernet(net);
        free(w);
        return NULL;
    }
    *out_w = w;
    return net;
}

int main(int argc, char** argv) {
    int want_watch = 0;
    int want_play = 0;
    int deterministic = 0;
    const char* model_arg = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "watch") == 0) want_watch = 1;
        else if (strcmp(argv[i], "play") == 0) want_play = 1;
        else if (strcmp(argv[i], "--deterministic") == 0) deterministic = 1;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0
                || strcmp(argv[i], "help") == 0) {
            ar_print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') model_arg = argv[i];
        else {
            fprintf(stderr, "unknown argument '%s'\n", argv[i]);
            ar_print_usage(argv[0]);
            return 1;
        }
    }
    if (want_watch && want_play) {
        fprintf(stderr, "pick one of play or watch\n");
        return 1;
    }

    float observations[AR_OBS_SIZE] = {0};
    float actions[NUM_ATNS] = {0};
    float rewards[1] = {0};
    float terminals[1] = {0};

    ARPG env = {0};
    env.num_agents = 1;
    // Fresh dungeon every launch (training uses vector seeds instead).
    env.rng = (uint32_t)(GetTime() * 1000000.0) ^ 0x9e3779b9u;
    if (!env.rng) env.rng = 1u;
    env.agents[0].observations = observations;
    env.agents[0].actions = actions;
    env.agents[0].rewards = rewards;
    env.agents[0].terminals = terminals;
    env.agents[0].action_mask = NULL;
    env.agents[0].policy = 0;

    ARControls controls;
    ar_load_config(&env, &controls);

    // Policy setup: watch always loads; default mode autoplays when a
    // checkpoint exists (idle-game behavior); play stays human.
    PufferNet* net = NULL;
    Weights* weights = NULL;
    int autoplay = 0;
    if (want_watch || (!want_play && !want_watch)) {
        char path[4096];
        if (model_arg && strcmp(model_arg, "latest") != 0) {
            snprintf(path, sizeof(path), "%s", model_arg);
        } else if (ar_try_latest(path, sizeof(path)) != 0) {
            if (want_watch) {
                fprintf(stderr, "no .bin checkpoints in checkpoints/arpg/\n");
                return 1;
            }
            fprintf(stderr, "no checkpoint yet - human play (train to unlock autoplay)\n");
        } else if (want_play) {
            path[0] = 0;
        }
        if (path[0]) {
            net = ar_load_policy(path, &weights);
            if (!net && want_watch) return 1;
            autoplay = (net != NULL);
        }
    }

    c_reset(&env);
    c_render(&env);

    double sim_accumulator = 0.0;
    int human_order = AR_ORDER_FOLLOW;
    env.pick_class = 0;
    while (!WindowShouldClose()) {
        double frame_start = GetTime();
        float frame_dt = GetFrameTime();
        if (frame_dt <= 0.0f) frame_dt = (float)AR_FAST_SIM_DT;
        if (frame_dt > 0.10f) frame_dt = 0.10f;
        sim_accumulator += frame_dt;

        if (ar_binding_pressed(controls.reset)) {
            c_reset(&env);
            if (net) ar_reset_policy(net);
            sim_accumulator = 0.0;
        }
        if (ar_binding_pressed(controls.hitboxes)) {
            env.show_hitboxes = !env.show_hitboxes;
        }
        if (net && ar_binding_pressed(controls.autoplay)) {
            autoplay = !autoplay;
            fprintf(stderr, "autoplay %s\n", autoplay ? "ON" : "OFF");
        }
        if (autoplay) {
            env.pick_class = -1;  // HUD shows POLICY
        } else if (env.pick_class < 0) {
            env.pick_class = 0;
        }

        if (!autoplay) {
            int mask = ar_read_move_mask(&controls);
            actions[0] = ar_read_move_action(mask);
            if (ar_binding_pressed(controls.class_wisp)) env.pick_class = AR_PET_WISP;
            if (ar_binding_pressed(controls.class_fang)) env.pick_class = AR_PET_FANG;
            if (ar_binding_pressed(controls.class_aegis)) env.pick_class = AR_PET_AEGIS;
            if (ar_binding_pressed(controls.order_follow)) human_order = AR_ORDER_FOLLOW;
            if (ar_binding_pressed(controls.order_attack)) human_order = AR_ORDER_ATTACK;
            if (ar_binding_pressed(controls.order_guard)) human_order = AR_ORDER_GUARD;
            if (ar_binding_pressed(controls.order_focus)) human_order = AR_ORDER_FOCUS;
            actions[1] = ar_binding_pressed(controls.summon)
                ? (float)(env.pick_class + 1)
                : 0.0f;
            actions[2] = (float)human_order;
            actions[3] = 0.0f;
            if (ar_binding_pressed(controls.dash)) actions[3] = 1.0f;
            else if (ar_binding_pressed(controls.nova)) actions[3] = 2.0f;
            else if (ar_binding_pressed(controls.frost)) actions[3] = 3.0f;
            actions[4] = 0.0f;
            if (ar_binding_pressed(controls.build_totem)) actions[4] = 1.0f;
            else if (ar_binding_pressed(controls.build_wall)) actions[4] = 2.0f;
        }

        int steps = 0;
        while (sim_accumulator >= AR_FAST_SIM_DT
                && steps < AR_FAST_MAX_CATCHUP_STEPS) {
            if (autoplay && net) {
                if (deterministic) ar_forward_argmax(net, observations, actions);
                else forward_puffernet(net, observations, actions);
            }
            c_step(&env);
            steps++;
            sim_accumulator -= AR_FAST_SIM_DT;
            // Human summon/ability/build are one-shot; order stays held.
            if (!autoplay) {
                actions[1] = 0.0f;
                actions[3] = 0.0f;
                actions[4] = 0.0f;
            }
            if (env.agents[0].terminals[0] > 0.0f) {
                if (net) ar_reset_policy(net);
                if (want_watch && net) {
                    c_reset(&env);  // leave it running like survivors watch
                } else {
                    c_reset(&env);
                }
                sim_accumulator = 0.0;
                break;
            }
        }

        c_render(&env);
        // Pin the loop to the sim rate; rendering already spent some budget.
        double spare = AR_FAST_SIM_DT - (GetTime() - frame_start);
        if (spare > 0.0) {
            WaitTime(spare);
        }
    }

    c_close(&env);
    if (net) free_puffernet(net);
    if (weights) free(weights);
    puf_ini_free(&g_controls_ini);
    return 0;
}
