#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define Log DPLog
#define wrap_pi dp_wrap_pi
#define compute_observations dp_compute_observations
#define add_log dp_add_log
#define init dp_init
#define c_reset dp_c_reset
#define solve_3x3 dp_solve_3x3
#define integrate_physics dp_integrate_physics
#define upright_reward dp_upright_reward
#define c_step dp_c_step
#define c_render dp_c_render
#define c_close dp_c_close
#include "double_pendulum.h"
#undef Log
#undef wrap_pi
#undef compute_observations
#undef add_log
#undef init
#undef c_reset
#undef solve_3x3
#undef integrate_physics
#undef upright_reward
#undef c_step
#undef c_render
#undef c_close

#include "triple_pendulum.h"

static void check(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        assert(condition);
    }
}

static void check_close(float actual, float expected, float tol, const char* message) {
    if (fabsf(actual - expected) > tol) {
        fprintf(stderr, "FAIL: %s actual=%g expected=%g tol=%g\n",
            message, actual, expected, tol);
        assert(fabsf(actual - expected) <= tol);
    }
}

static void make_double(DoublePendulum* env, float obs[DP_OBS_SIZE],
        float actions[1], float rewards[1], float terminals[1]) {
    memset(env, 0, sizeof(*env));
    env->observations = obs;
    env->actions = actions;
    env->rewards = rewards;
    env->terminals = terminals;
    env->num_agents = 1;
    env->rng = 1;
    env->cart_mass = 1.0f;
    env->link1_mass = 0.1f;
    env->link2_mass = 0.1f;
    env->link1_length = 0.5f;
    env->link2_length = 0.5f;
    env->gravity = 9.8f;
    env->force_mag = 10.0f;
    env->dt = 0.02f;
}

static void make_triple(TriplePendulum* env, float obs[TP_OBS_SIZE],
        float actions[1], float rewards[1], float terminals[1]) {
    memset(env, 0, sizeof(*env));
    env->observations = obs;
    env->actions = actions;
    env->rewards = rewards;
    env->terminals = terminals;
    env->num_agents = 1;
    env->rng = 1;
    env->cart_mass = 1.0f;
    env->link_mass[0] = 0.1f;
    env->link_mass[1] = 0.1f;
    env->link_mass[2] = 0.1f;
    env->link_length[0] = 0.45f;
    env->link_length[1] = 0.45f;
    env->link_length[2] = 0.45f;
    env->gravity = 9.8f;
    env->force_mag = 12.0f;
    env->dt = 0.02f;
}

static void test_observations_match_state(void) {
    float obs[TP_OBS_SIZE] = {0};
    float actions[1] = {0};
    float rewards[1] = {0};
    float terminals[1] = {0};
    TriplePendulum env;
    make_triple(&env, obs, actions, rewards, terminals);

    env.x = 3.0f;
    env.x_dot = -2.5f;
    env.theta[0] = 0.25f;
    env.theta[1] = -1.0f;
    env.theta[2] = 2.0f;
    env.theta_dot[0] = 1.5f;
    env.theta_dot[1] = -2.0f;
    env.theta_dot[2] = 3.0f;
    compute_observations(&env);

    check_close(obs[0], env.x / TP_X_THRESHOLD, 1e-6f, "x observation");
    check_close(obs[1], env.x_dot / 5.0f, 1e-6f, "x_dot observation");
    for (int i = 0; i < TP_LINKS; i++) {
        int off = 2 + 3*i;
        check_close(obs[off], sinf(env.theta[i]), 1e-6f, "sin observation");
        check_close(obs[off + 1], cosf(env.theta[i]), 1e-6f, "cos observation");
        check_close(obs[off + 2], env.theta_dot[i] / 8.0f, 1e-6f, "theta_dot observation");
    }
}

static void test_downward_equilibrium_stays_put(void) {
    float obs[TP_OBS_SIZE] = {0};
    float actions[1] = {1};
    float rewards[1] = {0};
    float terminals[1] = {0};
    TriplePendulum env;
    make_triple(&env, obs, actions, rewards, terminals);
    env.x = 0.0f;
    env.x_dot = 0.0f;
    for (int i = 0; i < TP_LINKS; i++) {
        env.theta[i] = M_PI;
        env.theta_dot[i] = 0.0f;
    }

    integrate_physics(&env, 0.0f);

    check_close(env.x, 0.0f, 1e-5f, "downward x");
    check_close(env.x_dot, 0.0f, 1e-5f, "downward x_dot");
    for (int i = 0; i < TP_LINKS; i++) {
        check_close(wrap_pi(env.theta[i] - M_PI), 0.0f, 1e-5f, "downward theta");
        check_close(env.theta_dot[i], 0.0f, 1e-5f, "downward theta_dot");
    }
}

static void test_upright_equilibrium_stays_put(void) {
    float obs[TP_OBS_SIZE] = {0};
    float actions[1] = {1};
    float rewards[1] = {0};
    float terminals[1] = {0};
    TriplePendulum env;
    make_triple(&env, obs, actions, rewards, terminals);
    env.x = 0.0f;
    env.x_dot = 0.0f;
    for (int i = 0; i < TP_LINKS; i++) {
        env.theta[i] = 0.0f;
        env.theta_dot[i] = 0.0f;
    }

    integrate_physics(&env, 0.0f);

    check_close(env.x, 0.0f, 1e-5f, "upright x");
    check_close(env.x_dot, 0.0f, 1e-5f, "upright x_dot");
    for (int i = 0; i < TP_LINKS; i++) {
        check_close(env.theta[i], 0.0f, 1e-5f, "upright theta");
        check_close(env.theta_dot[i], 0.0f, 1e-5f, "upright theta_dot");
    }
}

static void test_reward_sanity(void) {
    float obs[TP_OBS_SIZE] = {0};
    float actions[1] = {1};
    float rewards[1] = {0};
    float terminals[1] = {0};
    TriplePendulum env;
    make_triple(&env, obs, actions, rewards, terminals);

    env.x_dot = 0.0f;
    for (int i = 0; i < TP_LINKS; i++) {
        env.theta[i] = 0.0f;
        env.theta_dot[i] = 0.0f;
    }
    float upright = upright_reward(&env, 0.0f);
    check(upright > 0.5f, "upright reward should be high");
    check(env.upright_steps == 1, "upright streak increments");

    env.upright_steps = 0;
    for (int i = 0; i < TP_LINKS; i++) env.theta[i] = M_PI;
    float downward = upright_reward(&env, 0.0f);
    check(downward < 0.01f, "downward reward should be near zero");
}

static void test_random_rollout_stays_finite(void) {
    float obs[TP_OBS_SIZE] = {0};
    float actions[1] = {1};
    float rewards[1] = {0};
    float terminals[1] = {0};
    TriplePendulum env;
    make_triple(&env, obs, actions, rewards, terminals);
    c_reset(&env);

    for (int step = 0; step < 10000; step++) {
        actions[0] = (float)(rand_r(&env.rng) % TP_ACTIONS);
        c_step(&env);
        check(isfinite(env.x), "finite x");
        check(isfinite(env.x_dot), "finite x_dot");
        for (int i = 0; i < TP_LINKS; i++) {
            check(isfinite(env.theta[i]), "finite theta");
            check(isfinite(env.theta_dot[i]), "finite theta_dot");
        }
        for (int i = 0; i < TP_OBS_SIZE; i++) {
            check(isfinite(obs[i]), "finite observation");
        }
        check(rewards[0] >= 0.0f && rewards[0] <= 1.5f, "reward bounds");
    }
}

static void test_near_zero_third_link_approximates_double(void) {
    float d_obs[DP_OBS_SIZE] = {0};
    float d_actions[1] = {1};
    float d_rewards[1] = {0};
    float d_terminals[1] = {0};
    DoublePendulum d;
    make_double(&d, d_obs, d_actions, d_rewards, d_terminals);

    float t_obs[TP_OBS_SIZE] = {0};
    float t_actions[1] = {1};
    float t_rewards[1] = {0};
    float t_terminals[1] = {0};
    TriplePendulum t;
    make_triple(&t, t_obs, t_actions, t_rewards, t_terminals);

    t.cart_mass = d.cart_mass;
    t.link_mass[0] = d.link1_mass;
    t.link_mass[1] = d.link2_mass;
    t.link_mass[2] = 1e-5f;
    t.link_length[0] = d.link1_length;
    t.link_length[1] = d.link2_length;
    t.link_length[2] = 0.5f;
    t.gravity = d.gravity;
    t.dt = d.dt;

    d.x = t.x = 0.1f;
    d.x_dot = t.x_dot = -0.2f;
    d.theta1 = t.theta[0] = 2.5f;
    d.theta2 = t.theta[1] = -2.2f;
    t.theta[2] = 1.1f;
    d.theta1_dot = t.theta_dot[0] = 0.6f;
    d.theta2_dot = t.theta_dot[1] = -0.4f;
    t.theta_dot[2] = 0.2f;

    dp_integrate_physics(&d, 3.0f);
    integrate_physics(&t, 3.0f);

    check_close(t.x, d.x, 2e-3f, "double reduction x");
    check_close(t.x_dot, d.x_dot, 2e-3f, "double reduction x_dot");
    check_close(t.theta[0], d.theta1, 2e-3f, "double reduction theta1");
    check_close(t.theta_dot[0], d.theta1_dot, 2e-3f, "double reduction theta1_dot");
    check_close(t.theta[1], d.theta2, 2e-3f, "double reduction theta2");
    check_close(t.theta_dot[1], d.theta2_dot, 2e-3f, "double reduction theta2_dot");
}

int main(void) {
    test_observations_match_state();
    test_downward_equilibrium_stays_put();
    test_upright_equilibrium_stays_put();
    test_reward_sanity();
    test_random_rollout_stays_finite();
    test_near_zero_third_link_approximates_double();
    printf("triple_pendulum physics tests passed\n");
    return 0;
}
