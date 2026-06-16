// Triple pendulum swing-up and balance task with discrete cart forces.

#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "raylib.h"

#define TP_LINKS 3
#define TP_DOF 4
#define TP_OBS_SIZE 11
#define TP_ACTIONS 3
#define TP_MAX_STEPS 800
#define TP_X_THRESHOLD 6.0f
#define TP_WIDTH 900
#define TP_HEIGHT 480
#define TP_SCALE 65.0f

typedef struct Log {
    float perf;
    float score;
    float episode_return;
    float episode_length;
    float x_threshold_termination;
    float max_steps_termination;
    float hold_time;
    float n;
} Log;

typedef struct TriplePendulum {
    float* observations;
    float* actions;
    float* rewards;
    float* terminals;
    int num_agents;
    unsigned int rng;
    Log log;

    float x;
    float x_dot;
    float theta[TP_LINKS];
    float theta_dot[TP_LINKS];
    int tick;
    float episode_return;
    int upright_steps;
    int max_upright_steps;

    float cart_mass;
    float link_mass[TP_LINKS];
    float link_length[TP_LINKS];
    float gravity;
    float force_mag;
    float dt;
} TriplePendulum;

const Color TP_RED = (Color){187, 0, 0, 255};
const Color TP_CYAN = (Color){0, 187, 187, 255};
const Color TP_WHITE = (Color){241, 241, 241, 255};
const Color TP_BACKGROUND = (Color){6, 24, 24, 255};
const Color TP_YELLOW = (Color){245, 197, 66, 255};
const Color TP_GREEN = (Color){70, 210, 120, 255};

static inline float tp_randf(TriplePendulum* env, float lo, float hi) {
    float t = (float)rand_r(&env->rng) / (float)RAND_MAX;
    return lo + t * (hi - lo);
}

static inline float wrap_pi(float x) {
    while (x > M_PI) x -= 2.0f * M_PI;
    while (x < -M_PI) x += 2.0f * M_PI;
    return x;
}

void compute_observations(TriplePendulum* env) {
    env->observations[0] = env->x / TP_X_THRESHOLD;
    env->observations[1] = env->x_dot / 5.0f;
    for (int i = 0; i < TP_LINKS; i++) {
        int off = 2 + 3*i;
        env->observations[off + 0] = sinf(env->theta[i]);
        env->observations[off + 1] = cosf(env->theta[i]);
        env->observations[off + 2] = env->theta_dot[i] / 8.0f;
    }
}

void add_log(TriplePendulum* env, bool x_done, bool timeout) {
    float normalized = env->episode_return / (float)TP_MAX_STEPS;
    env->log.perf += fminf(fmaxf(normalized, 0.0f), 1.0f);
    env->log.score += env->episode_return;
    env->log.episode_return += env->episode_return;
    env->log.episode_length += (float)env->tick;
    env->log.x_threshold_termination += x_done ? 1.0f : 0.0f;
    env->log.max_steps_termination += timeout ? 1.0f : 0.0f;
    env->log.hold_time += (float)env->max_upright_steps;
    env->log.n += 1.0f;
}

void init(TriplePendulum* env) {
    env->num_agents = 1;
}

void c_reset(TriplePendulum* env) {
    env->x = tp_randf(env, -0.04f, 0.04f);
    env->x_dot = tp_randf(env, -0.04f, 0.04f);
    for (int i = 0; i < TP_LINKS; i++) {
        env->theta[i] = M_PI + tp_randf(env, -0.08f, 0.08f);
        env->theta_dot[i] = tp_randf(env, -0.04f, 0.04f);
    }
    env->tick = 0;
    env->episode_return = 0.0f;
    env->upright_steps = 0;
    env->max_upright_steps = 0;
    env->rewards[0] = 0.0f;
    env->terminals[0] = 0.0f;
    compute_observations(env);
}

static void solve_linear(float A[TP_DOF][TP_DOF], float b[TP_DOF], float x[TP_DOF]) {
    for (int i = 0; i < TP_DOF; i++) {
        int pivot = i;
        float best = fabsf(A[i][i]);
        for (int r = i + 1; r < TP_DOF; r++) {
            float v = fabsf(A[r][i]);
            if (v > best) {
                best = v;
                pivot = r;
            }
        }
        if (pivot != i) {
            for (int c = i; c < TP_DOF; c++) {
                float tmp = A[i][c];
                A[i][c] = A[pivot][c];
                A[pivot][c] = tmp;
            }
            float tmp = b[i];
            b[i] = b[pivot];
            b[pivot] = tmp;
        }

        float diag = fabsf(A[i][i]) < 1e-7f ? copysignf(1e-7f, A[i][i]) : A[i][i];
        float inv = 1.0f / diag;
        for (int c = i; c < TP_DOF; c++) A[i][c] *= inv;
        b[i] *= inv;
        for (int r = 0; r < TP_DOF; r++) {
            if (r == i) continue;
            float f = A[r][i];
            for (int c = i; c < TP_DOF; c++) A[r][c] -= f * A[i][c];
            b[r] -= f * b[i];
        }
    }
    for (int i = 0; i < TP_DOF; i++) x[i] = b[i];
}

static inline float distal_mass(TriplePendulum* env, int link) {
    float total = 0.0f;
    for (int i = link; i < TP_LINKS; i++) total += env->link_mass[i];
    return total;
}

void integrate_physics(TriplePendulum* env, float force) {
    float A[TP_DOF][TP_DOF] = {0};
    float b[TP_DOF] = {0};
    float qdd[TP_DOF] = {0};

    A[0][0] = env->cart_mass;
    for (int i = 0; i < TP_LINKS; i++) {
        A[0][0] += env->link_mass[i];
    }

    b[0] = force;
    for (int i = 0; i < TP_LINKS; i++) {
        float mi = distal_mass(env, i);
        float li = env->link_length[i];
        float ti = env->theta[i];
        float wi = env->theta_dot[i];
        A[0][i + 1] = mi * li * cosf(ti);
        A[i + 1][0] = A[0][i + 1];
        b[0] += mi * li * sinf(ti) * wi * wi;
    }

    for (int i = 0; i < TP_LINKS; i++) {
        float mi = distal_mass(env, i);
        float li = env->link_length[i];
        float ti = env->theta[i];
        b[i + 1] = mi * env->gravity * li * sinf(ti);
        for (int j = 0; j < TP_LINKS; j++) {
            float mj = distal_mass(env, i > j ? i : j);
            float lj = env->link_length[j];
            float tj = env->theta[j];
            float wj = env->theta_dot[j];
            A[i + 1][j + 1] = mj * li * lj * cosf(ti - tj);
            if (i != j) {
                b[i + 1] -= mj * li * lj * sinf(ti - tj) * wj * wj;
            }
        }
    }

    solve_linear(A, b, qdd);

    env->x_dot += env->dt * qdd[0];
    env->x_dot = fminf(fmaxf(env->x_dot, -20.0f), 20.0f);
    env->x += env->dt * env->x_dot;

    for (int i = 0; i < TP_LINKS; i++) {
        env->theta_dot[i] += env->dt * qdd[i + 1];
        env->theta_dot[i] = fminf(fmaxf(env->theta_dot[i], -30.0f), 30.0f);
        env->theta[i] = wrap_pi(env->theta[i] + env->dt * env->theta_dot[i]);
    }
}

float upright_reward(TriplePendulum* env, float force) {
    (void)force;
    float tip_y = 0.0f;
    float max_y = 0.0f;
    bool slow = fabsf(env->x_dot) < 1.0f;
    for (int i = 0; i < TP_LINKS; i++) {
        tip_y += env->link_length[i] * cosf(env->theta[i]);
        max_y += env->link_length[i];
        slow = slow && fabsf(env->theta_dot[i]) < 1.5f;
    }

    float height = 0.5f * (tip_y / max_y + 1.0f);
    bool stable = height > 0.9f && slow;
    if (stable) env->upright_steps += 1;
    else env->upright_steps = 0;
    if (env->upright_steps > env->max_upright_steps) {
        env->max_upright_steps = env->upright_steps;
    }

    float hold_bonus = fminf((float)env->upright_steps / 100.0f, 1.0f);
    float reward = 0.5f * height + hold_bonus;
    return fminf(fmaxf(reward, 0.0f), 1.5f);
}

void c_step(TriplePendulum* env) {
    float a = env->actions[0];
    if (!isfinite(a)) a = 1.0f;
    int action = (int)a;
    if ((unsigned)action >= TP_ACTIONS) action = 1;
    float force = 0.0f;
    if (action == 0) force = -env->force_mag;
    else if (action == 2) force = env->force_mag;

    integrate_physics(env, force);
    env->tick += 1;

    bool invalid = !isfinite(env->x) || !isfinite(env->x_dot);
    for (int i = 0; i < TP_LINKS; i++) {
        invalid = invalid || !isfinite(env->theta[i]) || !isfinite(env->theta_dot[i]);
    }
    bool x_done = env->x < -TP_X_THRESHOLD || env->x > TP_X_THRESHOLD;
    bool timeout = env->tick >= TP_MAX_STEPS;
    bool done = invalid || x_done || timeout;
    env->rewards[0] = upright_reward(env, force);
    env->episode_return += env->rewards[0];
    env->terminals[0] = (invalid || x_done) ? 1.0f : 0.0f;

    if (done) {
        add_log(env, invalid || x_done, timeout);
        c_reset(env);
        return;
    }
    compute_observations(env);
}

void c_render(TriplePendulum* env) {
    if (!IsWindowReady()) {
        InitWindow(TP_WIDTH, TP_HEIGHT, "PufferLib Triple Pendulum");
        SetTargetFPS(30);
    }
    if (IsKeyDown(KEY_ESCAPE)) exit(0);
    if (IsKeyPressed(KEY_TAB)) ToggleFullscreen();
    if (!isfinite(env->x)) return;
    for (int i = 0; i < TP_LINKS; i++) {
        if (!isfinite(env->theta[i])) return;
    }

    float rail_y = TP_HEIGHT * 0.74f;
    float cart_x = TP_WIDTH / 2.0f + env->x * TP_SCALE;
    cart_x = fminf(fmaxf(cart_x, 32.0f), TP_WIDTH - 32.0f);
    float cart_y = rail_y - 16.0f;
    float lengths[TP_LINKS];
    for (int i = 0; i < TP_LINKS; i++) {
        lengths[i] = env->link_length[i] * 2.0f * TP_SCALE;
    }
    Vector2 pts[TP_LINKS + 1];
    pts[0] = (Vector2){cart_x, cart_y};
    for (int i = 0; i < TP_LINKS; i++) {
        pts[i + 1] = (Vector2){
            pts[i].x + sinf(env->theta[i]) * lengths[i],
            pts[i].y - cosf(env->theta[i]) * lengths[i],
        };
    }

    BeginDrawing();
    ClearBackground(TP_BACKGROUND);
    DrawLine(0, (int)rail_y, TP_WIDTH, (int)rail_y, TP_CYAN);
    DrawRectangle((int)(cart_x - 28), (int)(cart_y - 12), 56, 24, TP_CYAN);
    Color link_colors[TP_LINKS] = {TP_RED, TP_YELLOW, TP_GREEN};
    for (int i = 0; i < TP_LINKS; i++) {
        DrawLineEx(pts[i], pts[i + 1], 7.0f - (float)i, link_colors[i]);
        DrawCircleV(pts[i], 8.0f, TP_WHITE);
    }
    DrawCircleV(pts[TP_LINKS], 10.0f, TP_WHITE);
    DrawText(TextFormat("steps %d  return %.1f  hold %d/%d",
        env->tick, env->episode_return, env->upright_steps, env->max_upright_steps),
        20, 20, 20, TP_WHITE);
    DrawText(TextFormat("x %.2f  theta %.1f %.1f %.1f",
        env->x,
        env->theta[0] * 180.0f / M_PI,
        env->theta[1] * 180.0f / M_PI,
        env->theta[2] * 180.0f / M_PI),
        20, 48, 20, TP_WHITE);
    EndDrawing();
}

void c_close(TriplePendulum* env) {
    if (IsWindowReady()) {
        CloseWindow();
    }
}
