/* arpg standalone viewer (human play).
 *
 * Build from repo root:
 *   ./build.sh arpg --fast
 *
 * Run from repo root so config/arpg.ini resolves. Movement is WASD by
 * default; every binding lives in the [keys] section of config/arpg.ini and
 * accepts multiple comma-separated keys (e.g. "up = W, UP").
 *
 * Default controls:
 *   WASD / arrows   move
 *   Space / E       summon pet
 *   R               restart run
 *   H               toggle hitboxes
 *   Esc             quit
 */

#include "arpg.h"

#include <stdio.h>
#include <string.h>

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

static ARBinding ar_binding_from_ini(Ini* ini, const char* section,
        const char* name, int fallback) {
    ARBinding binding = {0};
    const char* value = ini != NULL ? puf_ini_get_str(ini, section, name) : NULL;
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
    ARBinding reset;
    ARBinding hitboxes;
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
        .up = ar_binding_from_ini(&g_controls_ini, "keys", "up", KEY_W),
        .down = ar_binding_from_ini(&g_controls_ini, "keys", "down", KEY_S),
        .left = ar_binding_from_ini(&g_controls_ini, "keys", "left", KEY_A),
        .right = ar_binding_from_ini(&g_controls_ini, "keys", "right", KEY_D),
        .summon = ar_binding_from_ini(&g_controls_ini, "keys", "summon", KEY_SPACE),
        .reset = ar_binding_from_ini(&g_controls_ini, "keys", "reset", KEY_R),
        .hitboxes = ar_binding_from_ini(&g_controls_ini, "keys", "hitboxes", KEY_H),
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

// Action layout mirrors ar_steer_player: 0 idle, 1 N, 2 S, 3 W, 4 E,
// 5 NW, 6 NE, 7 SW, 8 SE.
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

int main(void) {
    float observations[AR_OBS_SIZE] = {0};
    float actions[NUM_ATNS] = {0};
    float rewards[1] = {0};
    float terminals[1] = {0};

    ARPG env = {0};
    env.num_agents = 1;
    env.rng = 1u;
    env.agents[0].observations = observations;
    env.agents[0].actions = actions;
    env.agents[0].rewards = rewards;
    env.agents[0].terminals = terminals;
    env.agents[0].action_mask = NULL;
    env.agents[0].policy = 0;

    ARControls controls;
    ar_load_config(&env, &controls);
    c_reset(&env);
    c_render(&env);

    double sim_accumulator = 0.0;
    while (!WindowShouldClose()) {
        double frame_start = GetTime();
        float frame_dt = GetFrameTime();
        if (frame_dt <= 0.0f) frame_dt = (float)AR_FAST_SIM_DT;
        if (frame_dt > 0.10f) frame_dt = 0.10f;
        sim_accumulator += frame_dt;

        if (ar_binding_pressed(controls.reset)) {
            c_reset(&env);
            sim_accumulator = 0.0;
        }
        if (ar_binding_pressed(controls.hitboxes)) {
            env.show_hitboxes = !env.show_hitboxes;
        }

        int mask = ar_read_move_mask(&controls);
        actions[0] = ar_read_move_action(mask);
        actions[1] = ar_binding_pressed(controls.summon) ? 1.0f : 0.0f;

        int steps = 0;
        while (sim_accumulator >= AR_FAST_SIM_DT
                && steps < AR_FAST_MAX_CATCHUP_STEPS) {
            c_step(&env);
            steps++;
            sim_accumulator -= AR_FAST_SIM_DT;
            // Summon is a one-shot action; movement stays held across steps.
            actions[1] = 0.0f;
            if (env.agents[0].terminals[0] > 0.0f) {
                c_reset(&env);
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
    puf_ini_free(&g_controls_ini);
    return 0;
}
