#pragma once

#include <assert.h>

// Identical cases run natively and in a CUDA kernel.
PS_SIM_FN void ps_test_weapon_areas(PSSim* sim, int env) {
    int obstacle_count = sim->cfg.obstacle_count;
    int area_cap = sim->cfg.area_cap;
    sim->cfg.obstacle_count = 0;
    for (int shape = PS_SHAPE_CIRCLE; shape <= PS_SHAPE_AABB; shape++) {
        for (int area = 0; area <= 1; area++) {
            for (int full = 0; full <= 1; full++) {
                sim->cfg.area_cap = full ? 0 : area_cap;
                ps_reset_core(sim, env, 1);
                for (int e = 0; e < 3; e++) {
                    assert(ps_spawn_enemy(sim, env) == e + 1);
                    PS_ENEMY(sim, env, e, x) = 4.0f;
                    PS_ENEMY(sim, env, e, y) = (float)e;
                    PS_ENEMY(sim, env, e, type) = 0;
                    PS_ENEMY(sim, env, e, shape) = PS_SHAPE_CIRCLE;
                    PS_ENEMY(sim, env, e, radius) = 0.1f;
                    PS_ENEMY(sim, env, e, half_width) = 0.1f;
                    PS_ENEMY(sim, env, e, half_height) = 0.1f;
                    PS_ENEMY(sim, env, e, hp) = e == 1 ? 0.5f : 10.0f;
                    PS_ENEMY(sim, env, e, max_hp) = 10.0f;
                }
                PS_ENEMY(sim, env, 0, shape) = shape;
                ps_rebuild_grid(sim, env);
                float radius = sim->cfg.weapon_base_radius[PS_WEAPON_BUBBLE] * (1 + area);
                ps_spawn_projectile(sim, env, PS_WEAPON_BUBBLE,
                    3.5f, 0.0f, 4.0f, 0.0f, 1.0f, radius, 0.5f, 0, 100);
                ps_update_projectiles(sim, env);
                assert(PS_P(sim, env, projectile_count) == 0);
                assert(PS_ENEMY(sim, env, 0, hp) == 9.0f); // Direct target hit once.
                assert(!PS_ENEMY(sim, env, 1, active));   // Splash can kill/remap dense slots.
                assert(PS_ENEMY(sim, env, 2, hp) == (area ? 9.0f : 10.0f));
                assert(PS_P(sim, env, area_count) == !full);
                assert(PS_P(sim, env, active_ink_count) == 0);
                if (!full) {
                    int a = PS_AREA(sim, env, 0, dense);
                    assert(PS_AREA(sim, env, a, radius) == 4.0f * radius);
                    assert(PS_AREA(sim, env, a, damage) == 0.0f);
                    for (int t = 0; t < PS_BUBBLE_POP_TTL; t++) ps_update_areas(sim, env);
                    assert(PS_P(sim, env, area_count) == 0);
                    assert(PS_ENEMY(sim, env, 0, hp) == 9.0f);
                }
            }
        }
    }
    sim->cfg.area_cap = area_cap;
    for (int level = 1; level <= sim->cfg.weapon_max_level; level++) {
        ps_reset_core(sim, env, 1);
        PS_P(sim, env, area_bonus) = 0.5f;
        int target = ps_spawn_enemy(sim, env) - 1;
        PS_ENEMY(sim, env, target, x) = 4.0f;
        PS_ENEMY(sim, env, target, y) = 0.0f;
        ps_cast_bubble(sim, env, level);
        assert(PS_P(sim, env, projectile_count) == 1 + level / 3);
        int p = PS_PROJECTILE(sim, env, 0, dense);
        float radius = ps_geometry_weapon_radius(&sim->cfg, PS_WEAPON_BUBBLE, level - 1) * 1.5f;
        assert(fabsf(PS_PROJECTILE(sim, env, p, radius) - radius) < 1e-5f);
        ps_clear_entities(sim, env);
        ps_cast_spikes(sim, env, level);
        assert(PS_P(sim, env, projectile_count) == 8 * level);
        int ttl = (int)ceilf(sim->cfg.spike_range / sim->cfg.spike_speed);
        for (int k = 0; k < PS_P(sim, env, projectile_count); k++) {
            p = PS_PROJECTILE(sim, env, k, dense);
            assert(PS_PROJECTILE(sim, env, p, ttl) == ttl);
        }
    }
    // A visual burst must stay at the damage origin, even beside obstacles.
    ps_clear_entities(sim, env);
    sim->cfg.obstacle_count = 1;
    PS_OBSTACLE(sim, env, 0, x) = 0.0f;
    PS_OBSTACLE(sim, env, 0, y) = 0.0f;
    PS_OBSTACLE(sim, env, 0, radius) = 2.0f;
    ps_spawn_area(sim, env, PS_WEAPON_BUBBLE, 1.0f, 0.0f, 1.2f, 0.0f,
        PS_BUBBLE_POP_TTL, PS_BUBBLE_POP_TTL);
    int a = PS_AREA(sim, env, 0, dense);
    assert(PS_AREA(sim, env, a, x) == 1.0f);
    assert(PS_AREA(sim, env, a, y) == 0.0f);
    sim->cfg.obstacle_count = obstacle_count;
    ps_reset_core(sim, env, 1);
}
