#define TP_LINKS 4
#include "../triple_pendulum/triple_pendulum.h"

#define OBS_SIZE TP_OBS_SIZE
#define NUM_ATNS 1
#define ACT_SIZES {TP_ACTIONS}
#define OBS_TENSOR_T FloatTensor

#define Env TriplePendulum
#include "vecenv.h"

static void assert_positive_finite(float value, const char* name) {
    if (!isfinite(value) || value <= 0.0f) {
        fprintf(stderr, "quad_pendulum: %s must be finite and > 0, got %g\n", name, value);
        assert(false);
    }
}

static void assert_finite(float value, const char* name) {
    if (!isfinite(value)) {
        fprintf(stderr, "quad_pendulum: %s must be finite, got %g\n", name, value);
        assert(false);
    }
}

void my_init(Env* env, Dict* kwargs) {
    env->num_agents = 1;
    env->cart_mass = dict_get(kwargs, "cart_mass")->value;
    for (int i = 0; i < TP_LINKS; i++) {
        char name[32];
        snprintf(name, sizeof(name), "link%d_mass", i + 1);
        env->link_mass[i] = dict_get(kwargs, name)->value;
        snprintf(name, sizeof(name), "link%d_length", i + 1);
        env->link_length[i] = dict_get(kwargs, name)->value;
    }
    env->gravity = dict_get(kwargs, "gravity")->value;
    env->force_mag = dict_get(kwargs, "force_mag")->value;
    env->dt = dict_get(kwargs, "dt")->value;
    env->catch_weight = dict_get(kwargs, "catch_weight")->value;
    env->smooth_weight = dict_get(kwargs, "smooth_weight")->value;
    env->hold_weight = dict_get(kwargs, "hold_weight")->value;
    env->fast_weight = dict_get(kwargs, "fast_weight")->value;
    env->force_penalty = dict_get(kwargs, "force_penalty")->value;
    env->slow_target_steps = dict_get(kwargs, "slow_target_steps")->value;
    env->action_sensitivity = dict_get(kwargs, "action_sensitivity")->value;
    DictItem* reward_stage = dict_get_unsafe(kwargs, "reward_stage");
    env->reward_stage = reward_stage ? reward_stage->value : 1.0f;

    assert_positive_finite(env->cart_mass, "cart_mass");
    for (int i = 0; i < TP_LINKS; i++) {
        char name[32];
        snprintf(name, sizeof(name), "link%d_mass", i + 1);
        assert_positive_finite(env->link_mass[i], name);
        snprintf(name, sizeof(name), "link%d_length", i + 1);
        assert_positive_finite(env->link_length[i], name);
    }
    assert_finite(env->gravity, "gravity");
    assert_positive_finite(env->force_mag, "force_mag");
    assert_positive_finite(env->dt, "dt");
    assert_finite(env->catch_weight, "catch_weight");
    assert_finite(env->smooth_weight, "smooth_weight");
    assert_finite(env->hold_weight, "hold_weight");
    assert_finite(env->fast_weight, "fast_weight");
    assert_finite(env->force_penalty, "force_penalty");
    assert_positive_finite(env->slow_target_steps, "slow_target_steps");
    assert_positive_finite(env->action_sensitivity, "action_sensitivity");
    assert_finite(env->reward_stage, "reward_stage");

    init(env);
}

void my_log(Log* log, Dict* out) {
    dict_set(out, "score", log->score);
    dict_set(out, "perf", log->perf);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "x_threshold_termination", log->x_threshold_termination);
    dict_set(out, "max_steps_termination", log->max_steps_termination);
    dict_set(out, "slow_deadline_miss", log->slow_deadline_miss);
    dict_set(out, "hold_time", log->hold_time);
    dict_set(out, "first_stable_step", log->first_stable_step);
    dict_set(out, "first_high_step", log->first_high_step);
    dict_set(out, "first_upright_step", log->first_upright_step);
    dict_set(out, "first_slow_step", log->first_slow_step);
    dict_set(out, "cart_abs_x", log->cart_abs_x);
    dict_set(out, "stable_rate", log->stable_rate);
    dict_set(out, "episode_max_steps", log->episode_max_steps);
    dict_set(out, "force_effort", log->force_effort);
    dict_set(out, "force_switch", log->force_switch);
    dict_set(out, "soft_action_rate", log->soft_action_rate);
    dict_set(out, "hard_action_rate", log->hard_action_rate);
    dict_set(out, "coast_action_rate", log->coast_action_rate);
    dict_set(out, "n", log->n);
}
