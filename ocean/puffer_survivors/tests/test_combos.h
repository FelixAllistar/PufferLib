#pragma once

#include <assert.h>

// Passive caps, combos and the scorecard band. Runs natively and in a CUDA kernel.

// One enemy at (x, y) with a small circle footprint, standing still.
PS_SIM_FN int ps_test_place_enemy(PSSim* sim, int env, float x, float y, float hp, uint8_t type) {
    int e = ps_spawn_enemy(sim, env) - 1;
    assert(e >= 0);
    PS_ENEMY(sim, env, e, x) = x;
    PS_ENEMY(sim, env, e, y) = y;
    PS_ENEMY(sim, env, e, type) = type;
    PS_ENEMY(sim, env, e, shape) = PS_SHAPE_CIRCLE;
    PS_ENEMY(sim, env, e, radius) = 0.1f;
    PS_ENEMY(sim, env, e, half_width) = 0.1f;
    PS_ENEMY(sim, env, e, half_height) = 0.1f;
    PS_ENEMY(sim, env, e, hp) = hp;
    PS_ENEMY(sim, env, e, max_hp) = hp;
    PS_ENEMY(sim, env, e, speed) = 0.0f;
    return e;
}

PS_SIM_FN void ps_test_combos(PSSim* sim, int env) {
    int obstacle_count = sim->cfg.obstacle_count;
    sim->cfg.obstacle_count = 0;
    int max_passive = sim->cfg.upgrade_passive_max_level;
    assert(max_passive == 5);

    // 1. Passive cap.
    ps_reset_core(sim, env, 1);
    for (int i = 0; i < max_passive; i++) {
        assert(ps_upgrade_available(sim, env, PS_UPGRADE_SPEED));
        ps_apply_upgrade_effect(sim, env, PS_UPGRADE_SPEED);
    }
    assert(PS_W(sim, env, PS_UPGRADE_SPEED - PS_WEAPON_COUNT, passive_level) == max_passive);
    assert(!ps_upgrade_available(sim, env, PS_UPGRADE_SPEED));
    assert(fabsf(PS_P(sim, env, speed_bonus) - max_passive * sim->cfg.upgrade_speed_bonus) < 1e-5f);
    // Everything capped: a level-up heals and offers nothing.
    for (int w = 0; w < PS_WEAPON_COUNT; w++) PS_W(sim, env, w, weapon_level) = sim->cfg.weapon_max_level;
    for (int p = 0; p < PS_PASSIVE_COUNT; p++) PS_W(sim, env, p, passive_level) = max_passive;
    for (int t = 0; t < PS_UPGRADE_COUNT; t++) assert(!ps_upgrade_available(sim, env, t));
    PS_P(sim, env, max_hp) = 10.0f;
    PS_P(sim, env, hp) = 1.0f;
    PS_P(sim, env, xp) = ps_xp_threshold(sim, env);
    int level = PS_P(sim, env, level);
    ps_update_drops(sim, env);
    assert(PS_P(sim, env, level) == level + 1);
    assert(PS_P(sim, env, pending_upgrade) == 0);
    assert(PS_P(sim, env, queued_upgrades) == 0);
    assert(PS_P(sim, env, hp) == fminf(10.0f, 1.0f + sim->cfg.health_heal));
    // Two types left: the third card repeats the first.
    PS_W(sim, env, PS_UPGRADE_MAGNET - PS_WEAPON_COUNT, passive_level) = 0;
    PS_W(sim, env, PS_UPGRADE_PIERCE - PS_WEAPON_COUNT, passive_level) = max_passive - 1;
    for (int trial = 0; trial < 8; trial++) {
        PS_P(sim, env, pending_upgrade) = 0;
        ps_offer_upgrades(sim, env);
        assert(PS_P(sim, env, pending_upgrade) == 1);
        int a = PS_W(sim, env, 0, offered), b = PS_W(sim, env, 1, offered);
        assert(a != b);
        assert(a == PS_UPGRADE_MAGNET || a == PS_UPGRADE_PIERCE);
        assert(b == PS_UPGRADE_MAGNET || b == PS_UPGRADE_PIERCE);
        assert(PS_W(sim, env, 2, offered) == a);
    }

    // 2. Spikes shot count at levels 1 and 8.
    assert(sim->cfg.spikes_count_base == 8 && sim->cfg.spikes_count_per_level == 4);
    ps_reset_core(sim, env, 1);
    ps_cast_spikes(sim, env, 1);
    assert(PS_P(sim, env, projectile_count) == 8);
    ps_clear_entities(sim, env);
    ps_cast_spikes(sim, env, 8);
    assert(PS_P(sim, env, projectile_count) == 36);

    // 3. Whirlpool pulls a normal enemy in from 1.5 R, not a boss.
    for (int boss = 0; boss <= 1; boss++) {
        ps_reset_core(sim, env, 1);
        float R = ps_geometry_weapon_radius(&sim->cfg, PS_WEAPON_WHIRLPOOL, 0);
        int e = ps_test_place_enemy(sim, env, 1.5f * R, 0.0f, 100.0f,
            boss ? PS_ENEMY_BOSS_FLAG : 0);
        ps_rebuild_grid(sim, env);
        ps_cast_whirlpool(sim, env, 1);
        assert(PS_ENEMY(sim, env, e, hp) == 100.0f);
        assert(PS_ENEMY(sim, env, e, y) == 0.0f);
        if (boss) {
            assert(PS_ENEMY(sim, env, e, x) == 1.5f * R);
            assert(PS_ENEMY(sim, env, e, pulled) == 0.0f);
        } else {
            float expect = fmaxf(0.9f * R, 1.5f * R - sim->cfg.whirlpool_pull);
            assert(fabsf(PS_ENEMY(sim, env, e, x) - expect) < 1e-4f);
            assert(PS_ENEMY(sim, env, e, pulled) == (float)sim->cfg.whirlpool_pull_ttl);
        }
    }

    // 4. Sonar sets off an oil pool on its edge; an enemy outside the pulse but inside
    // the blast takes the scaled damage exactly once.
    ps_reset_core(sim, env, 1);
    {
        float S = ps_geometry_weapon_radius(&sim->cfg, PS_WEAPON_SONAR, 1);
        float oil_r = 1.0f, oil_damage = 0.5f;
        ps_spawn_area(sim, env, PS_WEAPON_INK, S + 0.5f, 0.0f, oil_r, oil_damage, 500, 8);
        assert(PS_P(sim, env, active_ink_count) == 1);
        int oil = PS_AREA(sim, env, 0, dense);
        int e = ps_test_place_enemy(sim, env, S + 1.5f, 0.0f, 100.0f, 0);
        ps_rebuild_grid(sim, env);
        ps_cast_sonar(sim, env, 1);
        assert(PS_ENEMY(sim, env, e, hp) == 100.0f);
        assert(PS_P(sim, env, active_ink_count) == 0);
        assert(PS_AREA(sim, env, oil, type) == PS_AREA_DETONATION);
        assert(PS_AREA(sim, env, oil, radius) == oil_r * sim->cfg.oil_detonate_radius_scale);
        float blast = oil_damage * sim->cfg.oil_detonate_damage_scale;
        assert(PS_AREA(sim, env, oil, damage) == blast);
        int delay = 1 + sim->cfg.oil_detonate_delay;
        assert(PS_AREA(sim, env, oil, tick_timer) == delay);
        assert(PS_AREA(sim, env, oil, ttl) == delay + sim->cfg.oil_detonate_ttl);
        for (int t = 1; t <= delay + sim->cfg.oil_detonate_ttl; t++) {
            ps_update_areas(sim, env);
            assert(PS_ENEMY(sim, env, e, hp) == (t < delay ? 100.0f : 100.0f - blast));
        }
        assert(!PS_AREA(sim, env, oil, active));
        assert(PS_P(sim, env, active_ink_count) == 0);
    }

    // 5. A frozen normal enemy stands still; a frozen boss moves at 1 - frost_boss_slow.
    for (int boss = 0; boss <= 1; boss++) {
        ps_reset_core(sim, env, 1);
        int e = ps_test_place_enemy(sim, env, 10.0f, 0.0f, 100.0f, boss ? PS_ENEMY_BOSS_FLAG : 0);
        PS_ENEMY(sim, env, e, speed) = 0.1f;
        PS_ENEMY(sim, env, e, slow) = 5.0f;
        ps_update_enemies(sim, env);
        float expect = boss ? 10.0f - 0.1f * (1.0f - sim->cfg.frost_boss_slow) : 10.0f;
        assert(fabsf(PS_ENEMY(sim, env, e, x) - expect) < 1e-5f);
        assert(PS_ENEMY(sim, env, e, slow) == 4.0f);
    }

    // 6. Shatter chain: three frozen enemies 1.0 apart, each dying 4 ticks after the last.
    ps_reset_core(sim, env, 1);
    {
        float shatter = sim->cfg.shatter_damage;
        assert(sim->cfg.shatter_delay == 4);
        int e[3];
        for (int i = 0; i < 3; i++) {
            e[i] = ps_test_place_enemy(sim, env, 3.0f + (float)i, 0.0f, 0.5f * shatter, 0);
            PS_ENEMY(sim, env, e[i], slow) = 100.0f;
        }
        ps_rebuild_grid(sim, env);
        ps_damage_enemy(sim, env, e[0], 1000.0f);
        assert(!PS_ENEMY(sim, env, e[0], active));
        assert(PS_P(sim, env, area_count) == 1);
        // The first kill was a direct hit; only the two the shatters make are combo kills.
        assert(PS_P(sim, env, episode_combo_kills) == 0.0f);
        for (int t = 1; t <= 8; t++) {
            ps_update_areas(sim, env);
            assert(PS_ENEMY(sim, env, e[1], active) == (t < 4));
            assert(PS_ENEMY(sim, env, e[2], active) == (t < 8));
        }
        assert(PS_P(sim, env, episode_combo_kills) == 2.0f);
        assert(PS_P(sim, env, damage_source) == -1);
    }

    // 7. Scorecard band.
    assert(ps_fun_band(0.15f, 0.15f, 0.5f, 0.3f) == 1.0f);
    assert(ps_fun_band(0.5f, 0.15f, 0.5f, 0.3f) == 1.0f);
    assert(fabsf(ps_fun_band(0.8f, 0.15f, 0.5f, 0.3f) - expf(-1.0f)) < 1e-5f);
    assert(fabsf(ps_fun_band(-0.15f, 0.15f, 0.5f, 0.3f) - expf(-1.0f)) < 1e-5f);

    sim->cfg.obstacle_count = obstacle_count;
    ps_reset_core(sim, env, 1);
}
