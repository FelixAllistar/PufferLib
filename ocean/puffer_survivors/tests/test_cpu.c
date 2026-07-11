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
    PufferSurvivors env;
    memset(&env, 0, sizeof(env));
    float observations[PS_OBS_SIZE] = {0};
    float actions[2] = {0};
    float rewards[1] = {0};
    float terminals[1] = {0};
    env.observations = observations;
    env.actions = actions;
    env.rewards = rewards;
    env.terminals = terminals;
    env.num_agents = 1;
    env.rng = 1;
    env.cfg = ps_default_config();
    env.cfg.max_steps = 5000;
    env.cfg.player_health = 1000000.0f;
    env.cfg.enemy_obstacle_stride = 2;
    ps_init(&env);
    c_reset(&env);

    assert(PS_OBS_VERSION == 5);
    assert(PS_OBS_SIZE == 344);
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
