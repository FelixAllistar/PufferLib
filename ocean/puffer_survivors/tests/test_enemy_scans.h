#pragma once

#include <assert.h>

// Exercise both scan modes with holes containing stale boss/collision data.
// This runs unchanged on the CPU and inside the CUDA test kernel.
PS_SIM_FN void ps_test_enemy_scans(PSSim* sim, int env) {
    int enemy_cap = sim->cfg.enemy_cap;
    int obstacle_count = sim->cfg.obstacle_count;
    sim->cfg.enemy_cap = 8;
    sim->cfg.obstacle_count = 0;
    for (int count = 3; count <= 5; count++) {
        ps_reset_core(sim, env, 1);
        for (int i = 0; i < 8; i++) {
            assert(ps_spawn_enemy(sim, env) == i + 1);
            PS_ENEMY(sim, env, i, x) = 10.0f + (float)i;
            PS_ENEMY(sim, env, i, y) = 0.0f;
            PS_ENEMY(sim, env, i, speed) = 0.0f;
            PS_ENEMY(sim, env, i, type) = 0;
            PS_ENEMY(sim, env, i, shape) = PS_SHAPE_CIRCLE;
        }
        PS_ENEMY(sim, env, 0, x) = 0.01f;
        PS_ENEMY(sim, env, 0, speed) = 0.1f;
        PS_ENEMY(sim, env, 0, type) = PS_ENEMY_BOSS_FLAG;
        PS_ENEMY(sim, env, 0, shape) = PS_SHAPE_AABB;
        PS_ENEMY(sim, env, 0, half_width) = 1.0f;
        PS_ENEMY(sim, env, 0, half_height) = 1.0f;
        for (int i = 0; i < 8 - count; i++) ps_deactivate_enemy(sim, env, i);

        ps_compute_observations(sim, env);
        float* obs = (float*)PS_OBS(sim, env);
        assert(obs[8] == (float)count / 8.0f);
        assert(obs[PS_OBS_BOSS_BASE + PS_BOSS_PRESENT] == 0.0f);
        assert(ps_nearest_enemy(sim, env, 20.0f) == 8 - count);
        ps_rebuild_grid(sim, env);
        assert(PS_P(sim, env, aabb_count) == 0);
        int entries = 0;
        for (int cell = 0; cell < PS_GRID_CELLS; cell++) {
            for (int i = PS_GRID(sim, env, cell); i >= 0;
                    i = PS_ENEMY(sim, env, i, next)) {
                assert(PS_ENEMY(sim, env, i, active));
                assert(++entries <= count);
            }
        }
        assert(entries == count);
        float hp = PS_P(sim, env, hp);
        ps_update_enemies(sim, env);
        assert(PS_P(sim, env, hp) == hp);
        assert(PS_ENEMY(sim, env, 0, x) == 0.01f);
        assert(PS_P(sim, env, nearest_enemy) == 8 - count);
    }
    sim->cfg.enemy_cap = enemy_cap;
    sim->cfg.obstacle_count = obstacle_count;
    ps_reset_core(sim, env, 1);
}
