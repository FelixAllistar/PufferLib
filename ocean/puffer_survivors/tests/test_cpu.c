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

static PSConfig test_config_from_ini(void) {
    Ini ini = {0};
    puf_ini_load_env(&ini, "puffer_survivors", 0, NULL);
    PSConfig cfg = ps_config_from_kwargs(puf_ini_section(&ini, "env", 0));
    puf_ini_free(&ini);
    return cfg;
}

int main(void) {
    // The same INI arrays feed both the native CPU binding and the CUDA
    // simulator. Check that list-valued gameplay config reaches PSConfig.
    {
        PSConfig ini_cfg = test_config_from_ini();
        assert(ini_cfg.player_radius > 0.0f);
        assert(ini_cfg.enemy_base_hp[0] > 0.0f);
        assert(ini_cfg.weapon_base_damage[PS_WEAPON_INK] > 0.0f);
        assert(ini_cfg.wave_minimum[PS_WAVE_TABLE_COUNT - 1] > 0);
        assert(ini_cfg.wave_interval[PS_WAVE_TABLE_COUNT - 1] > 0);
    }

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
        terminal_env.cfg = test_config_from_ini();
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
    env.cfg = test_config_from_ini();
    env.cfg.max_steps = 5000;
    env.cfg.player_health = 1000000.0f;
    c_reset(&env);

    assert(PS_OBS_SIZE == 321);
    assert_finite_observation(observations);
    assert(ps_geometry_shape_overlaps_circle(PS_SHAPE_AABB,
        4.0f, 0.0f, 0.0f, 4.65f, 4.65f, 0.5f));
    assert(!ps_geometry_shape_overlaps_circle(PS_SHAPE_AABB,
        5.3f, 0.0f, 0.0f, 4.65f, 4.65f, 0.5f));

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
    env.enemies.shape[boss_slot] = PS_SHAPE_AABB;
    ps_rebuild_grid(&env);
    ps_rebuild_grid(&env);
    assert(env.aabb_count == 1);
    ps_clear_entities(&env);
    ps_compute_observations(&env);
    assert(observations[PS_OBS_BOSS_BASE + PS_BOSS_PRESENT] == 0.0f);

    // Each obstacle bin contains the nearest obstacle's exact relative dx/dy.
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
    assert(fabsf(observations[obstacle_slot] - 3.0f / observe_radius) < 1e-5f);
    assert(fabsf(observations[obstacle_slot + 1] - 1.0f / observe_radius) < 1e-5f);

    // Upgrade cards are exact one-hot IDs, with inactive cards all zero.
    env.pending_upgrade = 1;
    env.offered[0] = PS_UPGRADE_BUBBLE;
    env.offered[1] = PS_UPGRADE_AREA;
    env.offered[2] = PS_UPGRADE_PIERCE;
    ps_compute_observations(&env);
    for (int slot = 0; slot < PS_UPGRADE_SLOTS; slot++) {
        int selected = env.offered[slot];
        for (int type = 0; type < PS_UPGRADE_COUNT; type++) {
            float expected = type == selected ? 1.0f : 0.0f;
            assert(observations[PS_OBS_UPGRADE_BASE + slot * PS_UPGRADE_FEATURES + type] == expected);
        }
    }
    env.pending_upgrade = 0;
    ps_compute_observations(&env);
    for (int i = 0; i < PS_UPGRADE_SLOTS * PS_UPGRADE_FEATURES; i++)
        assert(observations[PS_OBS_UPGRADE_BASE + i] == 0.0f);

    for (int t = 0; t < 20000; t++) {
        actions[0] = (float)((t / 37) % 9);
        actions[1] = (float)((t / 251) % 3);
        c_step(&env);
        assert_finite_observation(observations);
        assert_dense(env.enemies.active, env.enemies.dense, env.enemies.dense_pos, env.enemy_count, env.cfg.enemy_cap);
        assert_dense(env.projectiles.active, env.projectiles.dense, env.projectiles.dense_pos, env.projectile_count, env.cfg.projectile_cap);
        assert_dense(env.drops.active, env.drops.dense, env.drops.dense_pos, env.drop_count, env.cfg.drop_cap);
        assert_dense(env.areas.active, env.areas.dense, env.areas.dense_pos, env.area_count, PS_MAX_AREAS);
        assert_dense(env.moving_obstacles.active, env.moving_obstacles.dense,
            env.moving_obstacles.dense_pos, env.moving_obstacle_count,
            PS_MAX_MOVING_OBSTACLES);
    }

    printf("cpu smoke ok: obs=%d enemies=%d projectiles=%d drops=%d areas=%d\n",
        PS_OBS_SIZE, env.enemy_count, env.projectile_count, env.drop_count, env.area_count);
    return 0;
}
