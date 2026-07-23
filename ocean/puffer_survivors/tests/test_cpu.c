#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define PS_DEBUG_COUNTS 1
#include "../puffer_survivors.h"

static void assert_finite_observation(const float* obs) {
    for (int i = 0; i < PS_OBS_SIZE; i++) {
        assert(isfinite(obs[i]));
    }
}

static void assert_dense(const uint8_t* active, const int* dense, const int* dense_pos, int count, int cap) {
    int seen = 0;
    for (int i = 0; i < cap; i++) {
        if (active[i]) {
            seen++;
            assert(dense_pos[i] >= 0 && dense_pos[i] < count);
            assert(dense[dense_pos[i]] == i);
        } else {
            assert(dense_pos[i] == -1);
        }
    }
    assert(seen == count);
    for (int k = 0; k < count; k++) {
        int i = dense[k];
        assert(i >= 0 && i < cap);
        assert(active[i]);
        assert(dense_pos[i] == k);
    }
}

int main(void) {
    // A time-limit success must have identical, configurable reward semantics
    // on CPU and CUDA. Keep all unrelated shaping disabled for this check.
    {
        PufferSurvivors terminal_env;
        memset(&terminal_env, 0, sizeof(terminal_env));
        float terminal_obs[PS_OBS_SIZE] = {0};
        float terminal_actions[2] = {0};
        float terminal_rewards[1] = {0};
        float terminal_flags[1] = {0};
        terminal_env.agents[0].observations = terminal_obs;
        terminal_env.agents[0].actions = terminal_actions;
        terminal_env.agents[0].rewards = terminal_rewards;
        terminal_env.agents[0].terminals = terminal_flags;
        terminal_env.num_agents = 1;
        terminal_env.rng = 1;
        terminal_env.cfg = ps_default_config();
        terminal_env.cfg.max_steps = 1;
        terminal_env.cfg.player_health = 1000000.0f;
        terminal_env.cfg.reward_xp = 0.0f;
        terminal_env.cfg.reward_kill = 0.0f;
        terminal_env.cfg.reward_damage = 0.0f;
        terminal_env.cfg.reward_survival = 0.125f;
        terminal_env.cfg.reward_hurt = 0.0f;
        terminal_env.cfg.reward_death = -0.5f;
        terminal_env.cfg.reward_success = 0.75f;
        terminal_env.cfg.reward_pickup = 0.0f;
        terminal_env.cfg.reward_levelup = 0.0f;
        terminal_env.cfg.obstacle_penalty = 0.0f;
        ps_init(&terminal_env);
        c_reset(&terminal_env);
        c_step(&terminal_env);
        assert(terminal_flags[0] == 1.0f);
        assert(fabsf(terminal_rewards[0] - 0.875f) < 1e-5f);
        assert(terminal_env.log.n == 1.0f);
        assert(terminal_env.log.perf == 1.0f);
        assert(terminal_env.log.survived == 1.0f);
        assert(fabsf(terminal_env.log.reward_survival - 0.125f) < 1e-5f);
        assert(fabsf(terminal_env.log.reward_terminal - 0.75f) < 1e-5f);
        assert(fabsf(terminal_env.log.episode_return -
            (terminal_env.log.reward_survival + terminal_env.log.reward_terminal)) < 1e-5f);
        assert(fabsf(terminal_env.log.move_counts[0] - 1.0f) < 1e-5f);
        assert(terminal_env.log.move_counts[1] == 0.0f);
    }

    PufferSurvivors env;
    memset(&env, 0, sizeof(env));
    float observations[PS_OBS_SIZE] = {0};
    float actions[2] = {0};
    float rewards[1] = {0};
    float terminals[1] = {0};
    env.agents[0].observations = observations;
    env.agents[0].actions = actions;
    env.agents[0].rewards = rewards;
    env.agents[0].terminals = terminals;
    env.num_agents = 1;
    env.rng = 1;
    env.cfg = ps_default_config();
    env.cfg.max_steps = 5000;
    env.cfg.player_health = 1000000.0f;
    env.cfg.enemy_obstacle_stride = 2;
    ps_init(&env);
    c_reset(&env);

    assert(PS_OBS_VERSION == 9);
    assert(PS_OBS_SIZE == 396);
    assert_finite_observation(observations);

    // Explicit boss slot: exact relative coordinates plus boss-specific state.
    ps_clear_entities(&env);
    int boss_slot = ps_spawn_enemy(&env) - 1;
    assert(boss_slot >= 0);
    env.enemies.type[boss_slot] = PS_ENEMY_BOSS_FLAG;
    env.enemies.x[boss_slot] = env.px + 5.0f;
    env.enemies.y[boss_slot] = env.py - 2.0f;
    env.enemies.vx[boss_slot] = -0.05f;
    env.enemies.vy[boss_slot] = 0.0f;
    env.enemies.hp[boss_slot] = 50.0f;
    env.enemies.max_hp[boss_slot] = 100.0f;
    ps_compute_observations(&env);
    assert(observations[PS_OBS_BOSS_BASE + PS_BOSS_PRESENT] == 1.0f);
    assert(observations[PS_OBS_BOSS_BASE + PS_BOSS_DX] > 0.0f);
    assert(observations[PS_OBS_BOSS_BASE + PS_BOSS_DY] < 0.0f);
    assert(fabsf(observations[PS_OBS_BOSS_BASE + PS_BOSS_HP_FRACTION] - 0.5f) < 1e-5f);
    ps_clear_entities(&env);
    ps_compute_observations(&env);
    assert(observations[PS_OBS_BOSS_BASE + PS_BOSS_PRESENT] == 0.0f);

    // V9 preserves the legacy obstacle density/proximity map and appends exact
    // dx/dy in stable spatial bins. V6 remains a valid legacy prefix mode.
    env.cfg.observation_version = 9;
    env.cfg.obstacle_count = 2;
    env.obstacles.x[0] = env.px + 3.0f;
    env.obstacles.y[0] = env.py + 1.0f;
    env.obstacles.radius[0] = 0.7f;
    env.obstacles.x[1] = env.px - 8.0f;
    env.obstacles.y[1] = env.py - 2.0f;
    env.obstacles.radius[1] = 0.9f;
    ps_compute_observations(&env);
    float observe_radius = env.cfg.arena_size * 0.45f;
    int sector = ps_obs_sector(3.0f, 1.0f);
    int ring = ps_obs_ring_d2(10.0f, observe_radius * observe_radius);
    int obstacle_slot = PS_OBS_OBSTACLE_BASE
        + (ring * PS_SECTORS + sector) * PS_OBSTACLE_CHANNELS;
    assert(observations[obstacle_slot] > 0.0f);
    assert(observations[obstacle_slot + 1] > 0.0f);
    int exact_slot = PS_OBS_EXACT_OBSTACLE_BASE
        + 2 * (ring * PS_SECTORS + sector);
    assert(fabsf(observations[exact_slot] - 3.0f / observe_radius) < 1e-5f);
    assert(fabsf(observations[exact_slot + 1] - 1.0f / observe_radius) < 1e-5f);
    env.cfg.observation_version = 6;
    ps_compute_observations(&env);
    assert(observations[obstacle_slot] > 0.0f);
    assert(observations[obstacle_slot + 1] > 0.0f);
    assert(observations[exact_slot] == 0.0f);
    env.cfg.observation_version = 9;

    for (int t = 0; t < 20000; t++) {
        actions[0] = (float)((t / 37) % 9);
        actions[1] = (float)((t / 251) % 3);
        c_step(&env);
        assert_finite_observation(observations);
        assert_dense(env.enemies.active, env.enemies.dense, env.enemies.dense_pos, env.enemy_count, env.cfg.enemy_cap);
        assert_dense(env.projectiles.active, env.projectiles.dense, env.projectiles.dense_pos, env.projectile_count, env.cfg.projectile_cap);
        assert_dense(env.drops.active, env.drops.dense, env.drops.dense_pos, env.drop_count, env.cfg.drop_cap);
        assert_dense(env.areas.active, env.areas.dense, env.areas.dense_pos, env.area_count, PS_MAX_AREAS);
    }

    printf("cpu smoke ok: obs=%d enemies=%d projectiles=%d drops=%d areas=%d\n",
        PS_OBS_SIZE, env.enemy_count, env.projectile_count, env.drop_count, env.area_count);
    return 0;
}
