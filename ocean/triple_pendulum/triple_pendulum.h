// Triple pendulum swing-up and balance task with discrete cart forces.

#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "raylib.h"

#ifndef TP_LINKS
#define TP_LINKS 3
#endif
#define TP_DOF (TP_LINKS + 1)
#define TP_BASE_OBS_SIZE (2 + 3 * TP_LINKS)
#ifndef TP_EXTRA_OBS
#define TP_EXTRA_OBS 0
#endif
#define TP_OBS_SIZE (TP_BASE_OBS_SIZE + TP_EXTRA_OBS)
#ifndef TP_CONTINUOUS_ACTIONS
#define TP_CONTINUOUS_ACTIONS 0
#endif
#if TP_CONTINUOUS_ACTIONS
#define TP_ACTIONS 1
#else
#define TP_ACTIONS 5
#endif
#define TP_MIN_EPISODE_STEPS 1200
#define TP_MAX_STEPS 2200
#define TP_X_THRESHOLD 6.0f
#define TP_STABLE_HEIGHT 0.92f
#define TP_STABLE_X_THRESHOLD 2.5f
#define TP_GRACE_STEPS 160.0f
#define TP_HOLD_TARGET 500.0f
#define TP_LONG_HOLD_TARGET 1000.0f
#define TP_FAST_SWING_TARGET 100.0f
#define TP_LATE_CATCH_TARGET 220.0f
#define TP_SLOW_TARGET_STEPS 220
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
    float slow_deadline_miss;
    float hold_time;
    float first_stable_step;
    float first_high_step;
    float first_upright_step;
    float first_slow_step;
    float cart_abs_x;
    float stable_rate;
    float episode_max_steps;
    float force_effort;
    float force_switch;
    float soft_action_rate;
    float hard_action_rate;
    float coast_action_rate;
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
    int first_stable_tick;
    int first_high_tick;
    int first_upright_tick;
    int first_slow_tick;
    int stable_steps_total;
    int episode_max_steps;
    float episode_abs_x;
    float episode_force_effort;
    float episode_force_switch;
    int episode_soft_actions;
    int episode_hard_actions;
    int episode_coast_actions;
    float prev_height;
    float prev_force;

    float cart_mass;
    float link_mass[TP_LINKS];
    float link_length[TP_LINKS];
    float gravity;
    float force_mag;
    float dt;
    float catch_weight;
    float smooth_weight;
    float hold_weight;
    float fast_weight;
    float force_penalty;
    float slow_target_steps;
    float action_sensitivity;
    float reward_stage;
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

static inline float tp_clampf(float x, float lo, float hi) {
    return fminf(fmaxf(x, lo), hi);
}

static inline float tp_smoothstep(float edge0, float edge1, float x) {
    float t = tp_clampf((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static inline float tp_height(TriplePendulum* env) {
    float tip_y = 0.0f;
    float max_y = 0.0f;
    for (int i = 0; i < TP_LINKS; i++) {
        tip_y += env->link_length[i] * cosf(env->theta[i]);
        max_y += env->link_length[i];
    }
    return tp_clampf(0.5f * (tip_y / max_y + 1.0f), 0.0f, 1.0f);
}

static inline float tp_action_force(TriplePendulum* env, int action) {
    if ((unsigned)action >= TP_ACTIONS) action = 2;
    if (action == 0) return -env->force_mag;
    if (action == 1) return -0.5f * env->force_mag;
    if (action == 3) return 0.5f * env->force_mag;
    if (action == 4) return env->force_mag;
    return 0.0f;
}

static inline float tp_continuous_force(TriplePendulum* env, float action) {
    if (!isfinite(action)) action = 0.0f;
    float sensitivity = env->action_sensitivity > 0.0f ? env->action_sensitivity : 1.0f;
    return tanhf(action * sensitivity) * env->force_mag;
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
#if TP_EXTRA_OBS >= 1
    float force_mag = env->force_mag > 1e-6f ? env->force_mag : 1.0f;
    env->observations[TP_BASE_OBS_SIZE] = env->prev_force / force_mag;
#endif
#if TP_EXTRA_OBS >= 2
    float slow_target = env->slow_target_steps > 1.0f ? env->slow_target_steps : 1.0f;
    env->observations[TP_BASE_OBS_SIZE + 1] = tp_clampf((float)env->tick / slow_target, 0.0f, 4.0f);
#endif
#if TP_EXTRA_OBS >= 3
    float max_steps = env->episode_max_steps > 1 ? (float)env->episode_max_steps : 1.0f;
    env->observations[TP_BASE_OBS_SIZE + 2] = tp_clampf((float)env->tick / max_steps, 0.0f, 1.0f);
#endif
}

void add_log(TriplePendulum* env, bool x_done, bool timeout) {
    float normalized = env->episode_return / (float)env->episode_max_steps;
    bool slow_deadline_miss = env->tick >= TP_SLOW_TARGET_STEPS
        && (env->first_slow_tick < 0 || env->first_slow_tick > TP_SLOW_TARGET_STEPS);
    env->log.perf += fminf(fmaxf(normalized, 0.0f), 1.0f);
    env->log.score += env->episode_return;
    env->log.episode_return += env->episode_return;
    env->log.episode_length += (float)env->tick;
    env->log.x_threshold_termination += x_done ? 1.0f : 0.0f;
    env->log.max_steps_termination += timeout ? 1.0f : 0.0f;
    env->log.slow_deadline_miss += slow_deadline_miss ? 1.0f : 0.0f;
    env->log.hold_time += (float)env->max_upright_steps;
    env->log.first_stable_step += env->first_stable_tick >= 0
        ? (float)env->first_stable_tick
        : (float)env->episode_max_steps;
    env->log.first_high_step += env->first_high_tick >= 0
        ? (float)env->first_high_tick
        : (float)env->episode_max_steps;
    env->log.first_upright_step += env->first_upright_tick >= 0
        ? (float)env->first_upright_tick
        : (float)env->episode_max_steps;
    env->log.first_slow_step += env->first_slow_tick >= 0
        ? (float)env->first_slow_tick
        : (float)env->episode_max_steps;
    env->log.cart_abs_x += env->tick > 0 ? env->episode_abs_x / (float)env->tick : 0.0f;
    env->log.stable_rate += env->tick > 0
        ? (float)env->stable_steps_total / (float)env->tick
        : 0.0f;
    env->log.episode_max_steps += (float)env->episode_max_steps;
    env->log.force_effort += env->tick > 0
        ? env->episode_force_effort / (float)env->tick
        : 0.0f;
    env->log.force_switch += env->tick > 0
        ? env->episode_force_switch / (float)env->tick
        : 0.0f;
    env->log.soft_action_rate += env->tick > 0
        ? (float)env->episode_soft_actions / (float)env->tick
        : 0.0f;
    env->log.hard_action_rate += env->tick > 0
        ? (float)env->episode_hard_actions / (float)env->tick
        : 0.0f;
    env->log.coast_action_rate += env->tick > 0
        ? (float)env->episode_coast_actions / (float)env->tick
        : 0.0f;
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
    env->first_stable_tick = -1;
    env->first_high_tick = -1;
    env->first_upright_tick = -1;
    env->first_slow_tick = -1;
    env->stable_steps_total = 0;
    env->episode_max_steps = TP_MIN_EPISODE_STEPS
        + (int)(rand_r(&env->rng) % (TP_MAX_STEPS - TP_MIN_EPISODE_STEPS + 1));
    env->episode_abs_x = 0.0f;
    env->episode_force_effort = 0.0f;
    env->episode_force_switch = 0.0f;
    env->episode_soft_actions = 0;
    env->episode_hard_actions = 0;
    env->episode_coast_actions = 0;
    env->prev_height = tp_height(env);
    env->prev_force = 0.0f;
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
    float angular_speed = 0.0f;
    bool slow = fabsf(env->x_dot) < 1.0f;
    for (int i = 0; i < TP_LINKS; i++) {
        angular_speed += fabsf(env->theta_dot[i]);
        slow = slow && fabsf(env->theta_dot[i]) < 1.5f;
    }

    float height = tp_height(env);
    float upward_progress = tp_clampf((height - env->prev_height) * 25.0f, 0.0f, 1.0f);
    env->prev_height = height;
    float center = 1.0f - tp_clampf(fabsf(env->x) / TP_X_THRESHOLD, 0.0f, 1.0f);
    float cart_slow = 1.0f - tp_clampf(fabsf(env->x_dot) / 5.0f, 0.0f, 1.0f);
    float angular_slow = 1.0f - tp_clampf((angular_speed / (float)TP_LINKS) / 3.0f, 0.0f, 1.0f);
    float force_effort = tp_clampf(fabsf(force) / env->force_mag, 0.0f, 1.0f);
    float force_switch = tp_clampf(fabsf(force - env->prev_force) / (2.0f * env->force_mag), 0.0f, 1.0f);
    float approach_gate = tp_smoothstep(0.45f, 0.86f, height);
    float upright_gate = tp_smoothstep(0.72f, TP_STABLE_HEIGHT, height);
    float catch_quality = approach_gate * center * cart_slow * angular_slow;
    float stability_quality = upright_gate * center * cart_slow * angular_slow;
    bool high = height > 0.86f;
    bool upright_position = height > TP_STABLE_HEIGHT && fabsf(env->x) < TP_STABLE_X_THRESHOLD;
    bool catch_slow = high && angular_slow > 0.65f && cart_slow > 0.60f;
    bool stable = upright_position && slow;
    if (stable) env->upright_steps += 1;
    else env->upright_steps = 0;
    if (stable) env->stable_steps_total += 1;
    if (env->upright_steps > env->max_upright_steps) {
        env->max_upright_steps = env->upright_steps;
    }
    if (stable && env->first_stable_tick < 0) {
        env->first_stable_tick = env->tick;
    }
    if (high && env->first_high_tick < 0) {
        env->first_high_tick = env->tick;
    }
    if (upright_position && env->first_upright_tick < 0) {
        env->first_upright_tick = env->tick;
    }
    if (catch_slow && env->first_slow_tick < 0) {
        env->first_slow_tick = env->tick;
    }

    float hold_bonus = stable
        ? tp_clampf((float)env->upright_steps / TP_HOLD_TARGET, 0.0f, 1.0f)
        : 0.0f;
    float long_hold_bonus = tp_clampf(
        ((float)env->upright_steps - 100.0f) / TP_LONG_HOLD_TARGET,
        0.0f,
        1.0f);
    float target = fmaxf(env->slow_target_steps, 80.0f);
    float high_target = fmaxf(45.0f, target - 90.0f);
    float upright_target = fmaxf(high_target + 25.0f, target - 40.0f);
    float stable_target = fmaxf(upright_target + 25.0f, target + 25.0f);

    float fast_bonus = 0.0f;
    if (stable && env->first_stable_tick >= 0) {
        fast_bonus = tp_clampf(
            (target - (float)env->first_stable_tick) / target,
            0.0f,
            1.0f);
    }

    float swing_window = 1.0f - tp_smoothstep(high_target, stable_target, (float)env->tick);
    float catch_window = tp_smoothstep(high_target, upright_target, (float)env->tick)
        * (1.0f - tp_smoothstep(target, stable_target + 60.0f, (float)env->tick));
    float catch_pressure = tp_smoothstep(high_target, target, (float)env->tick);
    float overdue_pressure = tp_smoothstep(upright_target, stable_target, (float)env->tick);
    float late_pressure = tp_smoothstep(stable_target, (float)env->episode_max_steps, (float)env->tick);
    float drift = fabsf(env->x) / TP_X_THRESHOLD;
    float late_instability_penalty = late_pressure * (stable ? 0.0f : 1.0f);
    float late_drift_penalty = late_pressure * drift * drift;
    float overdue_instability_penalty = overdue_pressure * (stable ? 0.0f : 1.0f);
    float overdue_motion_penalty = overdue_pressure * (1.0f - angular_slow);
    bool has_caught = env->first_stable_tick >= 0;
    bool has_high = env->first_high_tick >= 0;
    bool has_upright = env->first_upright_tick >= 0;
    float catch_delay = has_caught
        ? (float)env->first_stable_tick
        : (float)env->tick;
    float late_catch_penalty = tp_smoothstep(
        target,
        stable_target + 70.0f,
        catch_delay);
    bool has_slowed = env->first_slow_tick >= 0;
    float slow_delay = has_slowed
        ? (float)env->first_slow_tick
        : (float)env->tick;
    float late_slow_penalty = tp_smoothstep(upright_target, stable_target, slow_delay);
    float high_delay = has_high ? (float)env->first_high_tick : (float)env->tick;
    float upright_delay = has_upright ? (float)env->first_upright_tick : (float)env->tick;
    float late_high_penalty = tp_smoothstep(high_target, upright_target, high_delay);
    float late_upright_penalty = tp_smoothstep(upright_target, stable_target, upright_delay);
    float early_high_bonus = has_high
        ? tp_clampf((high_target - (float)env->first_high_tick) / fmaxf(high_target, 1.0f), 0.0f, 1.0f)
        : 0.0f;
    float early_upright_bonus = has_upright
        ? tp_clampf((upright_target - (float)env->first_upright_tick) / fmaxf(upright_target - high_target, 1.0f), 0.0f, 1.0f)
        : 0.0f;
    float early_slow_bonus = has_slowed
        ? tp_clampf((target - (float)env->first_slow_tick) / fmaxf(target - upright_target, 1.0f), 0.0f, 1.0f)
        : 0.0f;
    float pre_high_delay_penalty = has_high ? 0.0f : late_high_penalty;
    float pre_upright_delay_penalty = has_upright ? 0.0f : late_upright_penalty;
    float pre_catch_delay_penalty = has_caught ? 0.0f : late_catch_penalty;
    float pre_slow_delay_penalty = has_slowed ? 0.0f : late_slow_penalty;
    float hold_speed_discount = has_caught ? 1.0f - 0.55f * late_catch_penalty : 1.0f;
    float missed_approach_penalty = overdue_pressure * (1.0f - approach_gate);
    float catch_motion_penalty = tp_smoothstep(high_target, upright_target, (float)env->tick)
        * upright_gate * (1.0f - angular_slow);
    float catch_speed_penalty = catch_window * upright_gate * (
        0.65f * (1.0f - angular_slow) +
        0.35f * (1.0f - cart_slow)
    );
    float catch_force_penalty = catch_window * upright_gate * force_effort;
    float slow_catch_bonus = early_slow_bonus * approach_gate * center;
    float slow_overdue_pressure = has_slowed ? 0.0f
        : tp_smoothstep(target, target + 90.0f, (float)env->tick);
    float post_high_pressure = 0.0f;
    if (has_high && !has_slowed) {
        float since_high = (float)(env->tick - env->first_high_tick);
        post_high_pressure = tp_smoothstep(25.0f, 75.0f, since_high);
    }
    float post_high_motion_penalty = post_high_pressure * upright_gate * (
        0.65f * (1.0f - angular_slow) +
        0.35f * (1.0f - cart_slow)
    );
    float post_high_force_penalty = post_high_pressure * upright_gate * force_effort;
    float high_not_upright_penalty = catch_pressure * approach_gate * (1.0f - upright_gate);
    float upright_not_slow_penalty = catch_pressure * upright_gate * (
        0.70f * (1.0f - angular_slow) +
        0.30f * (1.0f - cart_slow)
    );
    float catch_switch_penalty = catch_window * approach_gate * force_switch;
    float post_catch_pressure = has_caught ? tp_smoothstep(0.0f, 160.0f, (float)env->upright_steps) : 0.0f;
    float post_catch_drift_penalty = post_catch_pressure * (stable ? 0.0f : 1.0f) * upright_gate;
    float shaped_stability = stable
        ? stability_quality
        : swing_window * stability_quality;
    if (env->reward_stage < 0.5f) {
        float drift_penalty = drift * drift;
        float starter_reward =
            0.42f * height +
            0.55f * upward_progress +
            0.08f * (1.0f - approach_gate) * tp_clampf(angular_speed / 10.0f, 0.0f, 1.0f) * center +
            0.16f * approach_gate * center +
            0.40f * stability_quality +
            (stable ? 0.85f : 0.0f) +
            0.30f * hold_bonus -
            0.08f * drift_penalty -
            env->force_penalty * 0.010f * force_effort -
            env->force_penalty * 0.015f * force_switch;
        return tp_clampf(starter_reward, -0.2f, 2.4f);
    }
    float reward =
        swing_window * (
            0.08f * height +
            0.16f * upward_progress +
            0.12f * approach_gate * center +
            0.16f * catch_quality
        ) +
        env->catch_weight * catch_window * approach_gate * center * (
            0.45f * angular_slow +
            0.25f * cart_slow +
            0.55f * upright_gate +
            0.40f * early_slow_bonus
        ) +
        env->hold_weight * hold_speed_discount * (
            0.45f * shaped_stability +
            (stable ? 0.95f : 0.0f) +
            0.85f * hold_bonus +
            0.50f * long_hold_bonus
        ) +
        env->catch_weight * 0.25f * slow_catch_bonus +
        env->fast_weight * (
            0.16f * early_high_bonus * approach_gate +
            0.22f * early_upright_bonus * upright_gate +
            0.40f * early_slow_bonus * catch_slow +
            0.45f * fast_bonus
        ) -
        0.12f * pre_high_delay_penalty -
        0.22f * pre_upright_delay_penalty -
        0.25f * pre_catch_delay_penalty -
        0.55f * pre_slow_delay_penalty -
        0.25f * missed_approach_penalty -
        0.18f * catch_motion_penalty -
        env->catch_weight * 0.20f * high_not_upright_penalty -
        env->smooth_weight * 0.45f * catch_speed_penalty -
        env->smooth_weight * 0.45f * upright_not_slow_penalty -
        env->smooth_weight * 0.35f * post_high_motion_penalty -
        0.45f * slow_overdue_pressure * approach_gate -
        0.12f * catch_switch_penalty -
        env->force_penalty * 0.18f * catch_force_penalty -
        env->force_penalty * 0.12f * post_high_force_penalty -
        env->force_penalty * 0.025f * force_effort -
        env->force_penalty * 0.035f * force_switch -
        0.30f * post_catch_drift_penalty -
        0.25f * overdue_instability_penalty -
        0.12f * overdue_motion_penalty -
        0.35f * late_instability_penalty -
        0.35f * late_drift_penalty;
    return tp_clampf(reward, -1.1f, 3.2f);
}

void c_step(TriplePendulum* env) {
#if TP_CONTINUOUS_ACTIONS
    float force = tp_continuous_force(env, env->actions[0]);
#else
    float a = env->actions[0];
    if (!isfinite(a)) a = 2.0f;
    int action = (int)a;
    float force = tp_action_force(env, action);
#endif
    float force_effort = tp_clampf(fabsf(force) / env->force_mag, 0.0f, 1.0f);
    env->episode_force_effort += force_effort;
    env->episode_force_switch += tp_clampf(
        fabsf(force - env->prev_force) / (2.0f * env->force_mag), 0.0f, 1.0f);
    if (force_effort > 0.75f) env->episode_hard_actions += 1;
    else if (force_effort > 0.25f) env->episode_soft_actions += 1;
    else env->episode_coast_actions += 1;

    integrate_physics(env, force);
    env->tick += 1;
    env->episode_abs_x += fabsf(env->x);

    bool invalid = !isfinite(env->x) || !isfinite(env->x_dot);
    for (int i = 0; i < TP_LINKS; i++) {
        invalid = invalid || !isfinite(env->theta[i]) || !isfinite(env->theta_dot[i]);
    }
    bool x_done = env->x < -TP_X_THRESHOLD || env->x > TP_X_THRESHOLD;
    env->rewards[0] = upright_reward(env, force);
    if (invalid || x_done) {
#if TP_CONTINUOUS_ACTIONS
        float elapsed = tp_clampf((float)env->tick / (float)env->episode_max_steps, 0.0f, 1.0f);
        env->rewards[0] -= 3.0f + 7.0f * (1.0f - elapsed);
#else
        env->rewards[0] -= 1.5f;
#endif
    }
    bool timeout = env->tick >= env->episode_max_steps;
    bool done = invalid || x_done || timeout;
    env->prev_force = force;
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
        InitWindow(TP_WIDTH, TP_HEIGHT, "PufferLib Pendulum Chain");
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
    Color link_colors[] = {TP_RED, TP_YELLOW, TP_GREEN, TP_CYAN, TP_WHITE};
    int num_colors = (int)(sizeof(link_colors) / sizeof(link_colors[0]));
    for (int i = 0; i < TP_LINKS; i++) {
        DrawLineEx(pts[i], pts[i + 1], fmaxf(3.0f, 7.0f - (float)i), link_colors[i % num_colors]);
        DrawCircleV(pts[i], 8.0f, TP_WHITE);
    }
    DrawCircleV(pts[TP_LINKS], 10.0f, TP_WHITE);
    DrawText(TextFormat("steps %d  return %.1f  hold %d/%d",
        env->tick, env->episode_return, env->upright_steps, env->max_upright_steps),
        20, 20, 20, TP_WHITE);
    char theta_text[160];
    int used = snprintf(theta_text, sizeof(theta_text), "x %.2f  theta", env->x);
    for (int i = 0; i < TP_LINKS && used < (int)sizeof(theta_text); i++) {
        used += snprintf(theta_text + used, sizeof(theta_text) - (size_t)used,
            " %.1f", env->theta[i] * 180.0f / M_PI);
    }
    DrawText(theta_text, 20, 48, 20, TP_WHITE);
    EndDrawing();
}

void c_close(TriplePendulum* env) {
    if (IsWindowReady()) {
        CloseWindow();
    }
}
