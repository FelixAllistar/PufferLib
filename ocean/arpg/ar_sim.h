#pragma once

// Shared CPU/CUDA gameplay for the arpg scaffold.
//
// The same functions compile against two state layouts:
//   CPU  (ARSim = ARPG):  AoS-in-struct pools, box3d owns movement.
//   CUDA (ARSim = ARCudaSim): SoA across envs, one thread owns one env step,
//        movement is integrated analytically (see README, "Physics backends").
// Gameplay rules — steering targets, damage, rewards, observations — are
// written once below and use the AR_ accessor macros only.

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "ar_constants.h"
#include "ar_log.h"
#include "ar_geometry.h"

#ifdef AR_GPU_SIM
#define AR_SIM_FN static __host__ __device__ __forceinline__
#define ARSim ARCudaSim
#define AR_IDX(sim, i, env) ((i) * (sim)->num_envs + (env))
#define AR_P(sim, env, field) ((sim)->field[env])
#define AR_ENEMY(sim, env, i, f) ((sim)->enemy_##f[AR_IDX(sim, i, env)])
#define AR_PET(sim, env, i, f) ((sim)->pet_##f[AR_IDX(sim, i, env)])
#define AR_OBSTACLE(sim, env, i, f) ((sim)->obstacle_##f[AR_IDX(sim, i, env)])
#define AR_SHARD(sim, env, i, f) ((sim)->shard_##f[AR_IDX(sim, i, env)])
#define AR_BUILD(sim, env, i, f) ((sim)->build_##f[AR_IDX(sim, i, env)])
#define AR_OBS(sim, env) ((sim)->observations + (size_t)(env) * AR_OBS_SIZE)
#define AR_ACTIONS(sim, env) ((sim)->actions + (size_t)(env) * NUM_ATNS)
#define AR_REWARD(sim, env) ((sim)->rewards[env])
#define AR_TERMINAL(sim, env) ((sim)->terminals[env])
#define AR_LOG(sim, env) ((sim)->native_envs[env].log)
// Dungeon floor bytes, contiguous per env: [env * AR_DUN_CELLS, ...].
#define AR_DUN(sim, env) ((sim)->dungeon_floor + (size_t)(env) * AR_DUN_CELLS)
#else
#define AR_SIM_FN static inline
#define ARSim ARPG
#define AR_IDX(sim, i, env) (i)
#define AR_P(sim, env, field) ((sim)->field)
#define AR_ENEMY(sim, env, i, f) ((sim)->enemies.f[i])
#define AR_PET(sim, env, i, f) ((sim)->pets.f[i])
#define AR_OBSTACLE(sim, env, i, f) ((sim)->obstacle_##f[i])
#define AR_SHARD(sim, env, i, f) ((sim)->shard_##f[i])
#define AR_BUILD(sim, env, i, f) ((sim)->build_##f[i])
#define AR_OBS(sim, env) ((sim)->agents[0].observations)
#define AR_ACTIONS(sim, env) ((sim)->agents[0].actions)
#define AR_REWARD(sim, env) ((sim)->agents[0].rewards[0])
#define AR_TERMINAL(sim, env) ((sim)->agents[0].terminals[0])
#define AR_LOG(sim, env) ((sim)->log)
#define AR_DUN(sim, env) ((sim)->dungeon)
#endif

AR_SIM_FN float ar_clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

AR_SIM_FN uint32_t ar_rand_u32(ARSim* sim, int env) {
    (void)env;
    uint32_t x = AR_P(sim, env, rng) ? AR_P(sim, env, rng) : 1u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    AR_P(sim, env, rng) = x ? x : 1u;
    return AR_P(sim, env, rng);
}

AR_SIM_FN float ar_randf(ARSim* sim, int env) {
    return (float)(ar_rand_u32(sim, env) & 0x00ffffffu) / 16777216.0f;
}

// Clamp a point into the playfield, keeping a circle of the given radius
// inside the walls.
AR_SIM_FN void ar_arena_clamp(ARSim* sim, int env, float* x, float* y,
        float radius) {
    float limit = 0.5f * sim->cfg.arena_size - radius;
    *x = ar_clampf(*x, -limit, limit);
    *y = ar_clampf(*y, -limit, limit);
}

// Floor-aware point near (cx, cy): try ring samples, fall back to center.
AR_SIM_FN void ar_floor_near(ARSim* sim, int env, float cx, float cy,
        float radius, float body_r, float* out_x, float* out_y) {
    ARConfig* cfg = &sim->cfg;
    for (int t = 0; t < 12; t++) {
        float a = ar_randf(sim, env) * 2.0f * PI;
        float x = cx + cosf(a) * radius;
        float y = cy + sinf(a) * radius;
        ar_arena_clamp(sim, env, &x, &y, body_r);
        if (ar_geometry_floor(AR_DUN(sim, env), cfg->arena_size, x, y)) {
            *out_x = x;
            *out_y = y;
            return;
        }
    }
    *out_x = cx;
    *out_y = cy;
}

// Wave scaling. Both backends share one difficulty curve.
AR_SIM_FN float ar_wave_hp_scale(ARSim* sim, int env, int wave) {
    float waves = (float)(wave < sim->cfg.enemy_growth_wave_cap
        ? wave : sim->cfg.enemy_growth_wave_cap);
    return 1.0f + sim->cfg.enemy_hp_growth_per_wave * waves;
}

AR_SIM_FN float ar_wave_speed_scale(ARSim* sim, int env, int wave) {
    float waves = (float)(wave < sim->cfg.enemy_growth_wave_cap
        ? wave : sim->cfg.enemy_growth_wave_cap);
    return 1.0f + sim->cfg.enemy_speed_growth_per_wave * waves;
}

// -----------------------------------------------------------------------------
// Enemy pool (dense list + ring slot allocator; mirrors puffer_survivors).
// -----------------------------------------------------------------------------

AR_SIM_FN int ar_enemy_slot_alloc(ARSim* sim, int env) {
    ARConfig* cfg = &sim->cfg;
    for (int k = 0; k < cfg->enemy_cap; k++) {
        int i = AR_P(sim, env, next_enemy_slot);
        AR_P(sim, env, next_enemy_slot) = (i + 1) % cfg->enemy_cap;
        if (!AR_ENEMY(sim, env, i, active)) return i;
    }
    return -1;
}

AR_SIM_FN void ar_free_enemy(ARSim* sim, int env, int slot) {
#ifndef AR_GPU_SIM
    if (B3_IS_NULL(sim->enemy_body[slot]) == false) {
        b3DestroyBody(sim->enemy_body[slot]);
        sim->enemy_body[slot] = b3_nullBodyId;
    }
#endif
    AR_ENEMY(sim, env, slot, active) = 0;
    AR_ENEMY(sim, env, slot, next) = -1;
    int pos = AR_ENEMY(sim, env, slot, dense_pos);
    AR_ENEMY(sim, env, slot, dense_pos) = -1;
    if (pos >= 0) {
        int last = AR_P(sim, env, enemy_count) - 1;
        int moved = AR_ENEMY(sim, env, last, dense);
        AR_ENEMY(sim, env, pos, dense) = moved;
        AR_ENEMY(sim, env, moved, dense_pos) = pos;
        AR_P(sim, env, enemy_count) = last;
    }
}

AR_SIM_FN void ar_free_pet(ARSim* sim, int env, int slot) {
#ifndef AR_GPU_SIM
    if (B3_IS_NULL(sim->pet_body[slot]) == false) {
        b3DestroyBody(sim->pet_body[slot]);
        sim->pet_body[slot] = b3_nullBodyId;
    }
#endif
    AR_PET(sim, env, slot, active) = 0;
    AR_PET(sim, env, slot, target) = -1;
    AR_P(sim, env, pets_alive) -= 1;
}

// Shared enemy-damage path (pet melee, nova, frost). Applies damage rewards
// and kill rewards, and frees the slot on death.
AR_SIM_FN void ar_damage_enemy(ARSim* sim, int env, int slot, float dmg) {
    if (!AR_ENEMY(sim, env, slot, active)) return;
    AR_ENEMY(sim, env, slot, hp) -= dmg;
    AR_P(sim, env, episode_damage_dealt) += dmg;
    float r = dmg * sim->cfg.reward_damage;
    AR_P(sim, env, episode_return) += r;
    AR_P(sim, env, episode_reward_damage) += r;
    AR_REWARD(sim, env) += r;
    if (AR_ENEMY(sim, env, slot, hp) <= 0.0f) {
        AR_P(sim, env, episode_kills) += 1.0f;
        AR_P(sim, env, episode_return) += sim->cfg.reward_kill;
        AR_P(sim, env, episode_reward_kill) += sim->cfg.reward_kill;
        AR_REWARD(sim, env) += sim->cfg.reward_kill;
        AR_P(sim, env, shards) += sim->cfg.kill_shards;
        ar_free_enemy(sim, env, slot);
    }
}

AR_SIM_FN int ar_spawn_enemy(ARSim* sim, int env, int kind, float x, float y,
        float hp_scale, float speed_scale) {
    ARConfig* cfg = &sim->cfg;
    int slot = ar_enemy_slot_alloc(sim, env);
    if (slot < 0) return -1;

    int count = AR_P(sim, env, enemy_count);
    AR_ENEMY(sim, env, count, dense) = slot;
    AR_ENEMY(sim, env, slot, dense_pos) = count;
    AR_P(sim, env, enemy_count) = count + 1;

    AR_ENEMY(sim, env, slot, active) = 1;
    AR_ENEMY(sim, env, slot, type) = (uint8_t)kind;
    AR_ENEMY(sim, env, slot, x) = x;
    AR_ENEMY(sim, env, slot, y) = y;
    AR_ENEMY(sim, env, slot, vx) = 0.0f;
    AR_ENEMY(sim, env, slot, vy) = 0.0f;
    float max_hp = cfg->enemy_base_hp[kind] * hp_scale;
    AR_ENEMY(sim, env, slot, max_hp) = max_hp;
    AR_ENEMY(sim, env, slot, hp) = max_hp;
    AR_ENEMY(sim, env, slot, radius) = cfg->enemy_radius[kind];
    AR_ENEMY(sim, env, slot, speed) = cfg->enemy_base_speed[kind] * speed_scale;
    AR_ENEMY(sim, env, slot, damage) = cfg->enemy_base_damage[kind];
    AR_ENEMY(sim, env, slot, next) = -1;

#ifndef AR_GPU_SIM
    sim->enemy_body[slot] = ar_phys_dynamic_body(sim->world, x, y,
        cfg->enemy_radius[kind]);
#endif
    return slot;
}

// -----------------------------------------------------------------------------
// Pets.
// -----------------------------------------------------------------------------

AR_SIM_FN int ar_summon_pet(ARSim* sim, int env, int cls) {
    ARConfig* cfg = &sim->cfg;
    if (cls < 0 || cls >= AR_PET_CLASS_COUNT) cls = AR_PET_WISP;
    if (AR_P(sim, env, pets_alive) >= cfg->pet_cap) return -1;

    int slot = -1;
    for (int i = 0; i < AR_MAX_PETS; i++) {
        if (!AR_PET(sim, env, i, active)) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;

    // Spawn on the player's flank so the pet does not start inside them.
    // Rejection-sample a floor cell near the player (dungeon-aware).
    float side = (AR_P(sim, env, facing_left) ? -1.0f : 1.0f);
    float x = AR_P(sim, env, px)
        + side * (cfg->player_radius + cfg->pet_radius[cls] + 0.4f);
    float y = AR_P(sim, env, py);
    ar_arena_clamp(sim, env, &x, &y, cfg->pet_radius[cls]);
    if (!ar_geometry_floor(AR_DUN(sim, env), cfg->arena_size, x, y)) {
        float ox = x, oy = y;
        x = AR_P(sim, env, px);
        y = AR_P(sim, env, py);
        for (int t = 0; t < 8; t++) {
            float a = ar_randf(sim, env) * 2.0f * PI;
            float tx = AR_P(sim, env, px) + cosf(a) * 1.5f;
            float ty = AR_P(sim, env, py) + sinf(a) * 1.5f;
            ar_arena_clamp(sim, env, &tx, &ty, cfg->pet_radius[cls]);
            if (ar_geometry_floor(AR_DUN(sim, env), cfg->arena_size, tx, ty)) {
                x = tx;
                y = ty;
                break;
            }
        }
        if (!ar_geometry_floor(AR_DUN(sim, env), cfg->arena_size, x, y)) {
            x = ox;
            y = oy;
        }
    }

    AR_PET(sim, env, slot, active) = 1;
    AR_PET(sim, env, slot, kind) = (uint8_t)cls;
    AR_PET(sim, env, slot, x) = x;
    AR_PET(sim, env, slot, y) = y;
    AR_PET(sim, env, slot, vx) = 0.0f;
    AR_PET(sim, env, slot, vy) = 0.0f;
    AR_PET(sim, env, slot, max_hp) = cfg->pet_health[cls];
    AR_PET(sim, env, slot, hp) = cfg->pet_health[cls];
    AR_PET(sim, env, slot, spd) = cfg->pet_speed[cls];
    AR_PET(sim, env, slot, dmg) = cfg->pet_damage[cls];
    AR_PET(sim, env, slot, rad) = cfg->pet_radius[cls];
    AR_PET(sim, env, slot, cd) = 0.0f;
    AR_PET(sim, env, slot, age) = 0.0f;
    AR_PET(sim, env, slot, invuln) = cfg->pet_invuln_steps;
    AR_PET(sim, env, slot, target) = -1;
    AR_PET(sim, env, slot, attacking) = 0;
    AR_P(sim, env, pets_alive) += 1;

#ifndef AR_GPU_SIM
    sim->pet_body[slot] = ar_phys_dynamic_body(sim->world, x, y,
        cfg->pet_radius[cls]);
#endif
    AR_P(sim, env, episode_summons) += 1.0f;
    float r = cfg->reward_summon;
    AR_P(sim, env, episode_return) += r;
    AR_P(sim, env, episode_reward_summon) += r;
    AR_REWARD(sim, env) += r;
    return slot;
}

// Nearest active enemy to a point within max_range2. Linear scan over the
// dense list; enemy_cap is small enough that a grid is not worth it here.
AR_SIM_FN int ar_nearest_enemy(ARSim* sim, int env, float x, float y,
        float max_range2) {
    int best = -1;
    float best_d2 = max_range2;
    int count = AR_P(sim, env, enemy_count);
    for (int k = 0; k < count; k++) {
        int i = AR_ENEMY(sim, env, k, dense);
        float d2 = ar_geometry_dist2(x, y, AR_ENEMY(sim, env, i, x),
            AR_ENEMY(sim, env, i, y));
        if (d2 < best_d2) {
            best_d2 = d2;
            best = i;
        }
    }
    return best;
}

// -----------------------------------------------------------------------------
// Steering. Velocities are written here; the move authority applies them.
// -----------------------------------------------------------------------------

AR_SIM_FN void ar_steer_player(ARSim* sim, int env) {
    ARConfig* cfg = &sim->cfg;
    int move = (int)ar_clampf(AR_ACTIONS(sim, env)[0], 0.0f,
        (float)(AR_MOVE_ACTION_COUNT - 1));
    // Screen-relative (camera never rotates): 0 idle, 1 up, 2 down, 3 left,
    // 4 right, 5 up-left, 6 up-right, 7 down-left, 8 down-right. The 2:1 iso
    // projection maps screen-up to world (-1,-1), hence the rotated table.
    const float dx_tbl[AR_MOVE_ACTION_COUNT] = {0.0f,
        -0.70710678f, 0.70710678f, -0.70710678f, 0.70710678f,
        -1.0f, 0.0f, 0.0f, 1.0f};
    const float dy_tbl[AR_MOVE_ACTION_COUNT] = {0.0f,
        -0.70710678f, 0.70710678f, 0.70710678f, -0.70710678f,
        0.0f, -1.0f, 1.0f, 0.0f};
    float vx = dx_tbl[move];
    float vy = dy_tbl[move];
    float tm = ar_tile_speed(ar_tile_at(AR_DUN(sim, env), cfg->arena_size,
        AR_P(sim, env, px), AR_P(sim, env, py)));
    AR_P(sim, env, pvx) = vx * cfg->player_speed * tm;
    AR_P(sim, env, pvy) = vy * cfg->player_speed * tm;
    if (vx > 0.01f) AR_P(sim, env, facing_left) = 0;
    if (vx < -0.01f) AR_P(sim, env, facing_left) = 1;
}

AR_SIM_FN void ar_steer_pets(ARSim* sim, int env) {
    ARConfig* cfg = &sim->cfg;
    float px = AR_P(sim, env, px);
    float py = AR_P(sim, env, py);
    int order = AR_P(sim, env, order);
    if (order < 0 || order >= AR_ORDER_COUNT) order = AR_ORDER_FOLLOW;
    float aggro2 = cfg->pet_aggro_range * cfg->pet_aggro_range;
    float leash2 = cfg->pet_leash_range * cfg->pet_leash_range;
    if (order == AR_ORDER_ATTACK) {
        aggro2 *= 2.25f;  // 1.5x aggro radius
        leash2 *= 4.0f;   // 2x leash radius
    }
    float stop2 = cfg->pet_attack_range * 0.8f;
    stop2 *= stop2;
    float follow2 = cfg->pet_follow_distance * cfg->pet_follow_distance;
    // Focus order: everyone converges on the player's nearest enemy
    // (one tick stale, refreshed in observations).
    int focus = (order == AR_ORDER_FOCUS) ? AR_P(sim, env, nearest_enemy) : -1;
    if (focus >= 0 && !AR_ENEMY(sim, env, focus, active)) focus = -1;

    for (int i = 0; i < AR_MAX_PETS; i++) {
        AR_PET(sim, env, i, attacking) = 0;
        if (!AR_PET(sim, env, i, active)) continue;
        float x = AR_PET(sim, env, i, x);
        float y = AR_PET(sim, env, i, y);
        float spd = AR_PET(sim, env, i, spd);

        int target = -1;
        if (focus >= 0) {
            target = focus;
        } else {
            // Validate the current target every tick (slots recycle freely).
            target = AR_PET(sim, env, i, target);
            if (target >= 0 && !AR_ENEMY(sim, env, target, active)) target = -1;
            if (target >= 0
                    && ar_geometry_dist2(x, y, AR_ENEMY(sim, env, target, x),
                        AR_ENEMY(sim, env, target, y)) > aggro2) {
                target = -1;
            }
            if (target < 0) {
                target = ar_nearest_enemy(sim, env, x, y, aggro2);
            }
            // Pets abandon combat when the fight drags them away from the player.
            if (target >= 0
                    && ar_geometry_dist2(px, py, AR_ENEMY(sim, env, target, x),
                        AR_ENEMY(sim, env, target, y)) > leash2) {
                target = -1;
            }
            if (order == AR_ORDER_GUARD && target >= 0) {
                // Guard: hold ground, only strike what comes into reach.
                float gr = cfg->pet_attack_range * 2.0f
                    + AR_PET(sim, env, i, rad);
                if (ar_geometry_dist2(x, y, AR_ENEMY(sim, env, target, x),
                        AR_ENEMY(sim, env, target, y)) > gr * gr) {
                    target = -1;
                }
            }
        }
        AR_PET(sim, env, i, target) = target;

        float vx = 0.0f, vy = 0.0f;
        if (target >= 0) {
            float dx = AR_ENEMY(sim, env, target, x) - x;
            float dy = AR_ENEMY(sim, env, target, y) - y;
            float d2 = dx * dx + dy * dy;
            if (d2 > stop2) {
                float d = sqrtf(fmaxf(d2, 0.0001f));
                vx = dx / d * spd;
                vy = dy / d * spd;
            }
        } else if (ar_geometry_dist2(x, y, px, py) > follow2) {
            float dx = px - x;
            float dy = py - y;
            float d = sqrtf(fmaxf(dx * dx + dy * dy, 0.0001f));
            vx = dx / d * spd;
            vy = dy / d * spd;
        }
        float tm = ar_tile_speed(ar_tile_at(AR_DUN(sim, env),
            cfg->arena_size, x, y));
        AR_PET(sim, env, i, vx) = vx * tm;
        AR_PET(sim, env, i, vy) = vy * tm;
    }
}

AR_SIM_FN void ar_steer_enemies(ARSim* sim, int env) {
    float px = AR_P(sim, env, px);
    float py = AR_P(sim, env, py);
    int count = AR_P(sim, env, enemy_count);
    for (int k = 0; k < count; k++) {
        int i = AR_ENEMY(sim, env, k, dense);
        float dx = px - AR_ENEMY(sim, env, i, x);
        float dy = py - AR_ENEMY(sim, env, i, y);
        float d2 = dx * dx + dy * dy;
        if (d2 < 0.0001f) {
            AR_ENEMY(sim, env, i, vx) = 0.0f;
            AR_ENEMY(sim, env, i, vy) = 0.0f;
            continue;
        }
        float d = sqrtf(d2);
        float speed = AR_ENEMY(sim, env, i, speed);
        if (AR_ENEMY(sim, env, i, slow_timer) > 0) {
            speed *= sim->cfg.frost_slow_mult;
        }
        speed *= ar_tile_speed(ar_tile_at(AR_DUN(sim, env),
            sim->cfg.arena_size, AR_ENEMY(sim, env, i, x),
            AR_ENEMY(sim, env, i, y)));
        AR_ENEMY(sim, env, i, vx) = dx / d * speed;
        AR_ENEMY(sim, env, i, vy) = dy / d * speed;
    }
}

// Dungeon wall collision for every mover. Shared by both move authorities:
// the GPU path calls it after its analytic pass, the CPU path after reading
// positions back from box3d (and re-syncs the bodies).
AR_SIM_FN void ar_dungeon_collide(ARSim* sim, int env) {
    ARConfig* cfg = &sim->cfg;
    const uint8_t* dun = AR_DUN(sim, env);
    float x = AR_P(sim, env, px), y = AR_P(sim, env, py);
    if (ar_geometry_collide_dungeon(dun, cfg->arena_size, &x, &y,
            cfg->player_radius)) {
        AR_P(sim, env, px) = x;
        AR_P(sim, env, py) = y;
#ifndef AR_GPU_SIM
        ar_phys_teleport(sim->player_body, x, y);
#endif
    }
    for (int i = 0; i < AR_MAX_PETS; i++) {
        if (!AR_PET(sim, env, i, active)) continue;
        float ex = AR_PET(sim, env, i, x), ey = AR_PET(sim, env, i, y);
        if (ar_geometry_collide_dungeon(dun, cfg->arena_size, &ex, &ey,
                AR_PET(sim, env, i, rad))) {
            AR_PET(sim, env, i, x) = ex;
            AR_PET(sim, env, i, y) = ey;
#ifndef AR_GPU_SIM
            ar_phys_teleport(sim->pet_body[i], ex, ey);
#endif
        }
    }
    int count = AR_P(sim, env, enemy_count);
    for (int k = 0; k < count; k++) {
        int i = AR_ENEMY(sim, env, k, dense);
        float ex = AR_ENEMY(sim, env, i, x), ey = AR_ENEMY(sim, env, i, y);
        if (ar_geometry_collide_dungeon(dun, cfg->arena_size, &ex, &ey,
                AR_ENEMY(sim, env, i, radius))) {
            AR_ENEMY(sim, env, i, x) = ex;
            AR_ENEMY(sim, env, i, y) = ey;
#ifndef AR_GPU_SIM
            ar_phys_teleport(sim->enemy_body[i], ex, ey);
#endif
        }
    }
    // Player buildings are solid ground clutter.
    for (int b = 0; b < AR_MAX_BUILDINGS; b++) {
        if (!AR_BUILD(sim, env, b, active)) continue;
        float bx = AR_BUILD(sim, env, b, x);
        float by = AR_BUILD(sim, env, b, y);
        float br = AR_BUILD(sim, env, b, rad);
        float x = AR_P(sim, env, px), y = AR_P(sim, env, py);
        if (ar_geometry_push_out_circle(&x, &y, bx, by,
                br + cfg->player_radius)) {
            AR_P(sim, env, px) = x;
            AR_P(sim, env, py) = y;
#ifndef AR_GPU_SIM
            ar_phys_teleport(sim->player_body, x, y);
#endif
        }
        for (int i = 0; i < AR_MAX_PETS; i++) {
            if (!AR_PET(sim, env, i, active)) continue;
            float ex = AR_PET(sim, env, i, x), ey = AR_PET(sim, env, i, y);
            if (ar_geometry_push_out_circle(&ex, &ey, bx, by,
                    br + AR_PET(sim, env, i, rad))) {
                AR_PET(sim, env, i, x) = ex;
                AR_PET(sim, env, i, y) = ey;
#ifndef AR_GPU_SIM
                ar_phys_teleport(sim->pet_body[i], ex, ey);
#endif
            }
        }
        for (int k = 0; k < count; k++) {
            int i = AR_ENEMY(sim, env, k, dense);
            float ex = AR_ENEMY(sim, env, i, x), ey = AR_ENEMY(sim, env, i, y);
            if (ar_geometry_push_out_circle(&ex, &ey, bx, by,
                    br + AR_ENEMY(sim, env, i, radius))) {
                AR_ENEMY(sim, env, i, x) = ex;
                AR_ENEMY(sim, env, i, y) = ey;
#ifndef AR_GPU_SIM
                ar_phys_teleport(sim->enemy_body[i], ex, ey);
#endif
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Move authority. GPU: integrate + analytic separation. CPU: box3d.
// -----------------------------------------------------------------------------

#ifdef AR_GPU_SIM

// Push two circles apart, half the penetration on each side, so each
// unordered pair resolves exactly once.
AR_SIM_FN int ar_separate_half(float* ax, float* ay, float* bx, float* by,
        float radius) {
    float dx = *bx - *ax;
    float dy = *by - *ay;
    if (dx >= radius || dx <= -radius || dy >= radius || dy <= -radius) return 0;
    float d2 = dx * dx + dy * dy;
    if (d2 >= radius * radius) return 0;

    float d = sqrtf(fmaxf(d2, 0.0001f));
    float push = (radius - d) * 0.5f;
    float nx = dx / d;
    float ny = dy / d;
    *ax -= nx * push;
    *ay -= ny * push;
    *bx += nx * push;
    *by += ny * push;
    return 1;
}

AR_SIM_FN void ar_gpu_move_authority(ARSim* sim, int env) {
    ARConfig* cfg = &sim->cfg;
    float half = 0.5f * cfg->arena_size;

    // Integrate.
    AR_P(sim, env, px) += AR_P(sim, env, pvx) * AR_DT;
    AR_P(sim, env, py) += AR_P(sim, env, pvy) * AR_DT;
    for (int i = 0; i < AR_MAX_PETS; i++) {
        if (!AR_PET(sim, env, i, active)) continue;
        AR_PET(sim, env, i, x) += AR_PET(sim, env, i, vx) * AR_DT;
        AR_PET(sim, env, i, y) += AR_PET(sim, env, i, vy) * AR_DT;
    }
    int count = AR_P(sim, env, enemy_count);
    for (int k = 0; k < count; k++) {
        int i = AR_ENEMY(sim, env, k, dense);
        AR_ENEMY(sim, env, i, x) += AR_ENEMY(sim, env, i, vx) * AR_DT;
        AR_ENEMY(sim, env, i, y) += AR_ENEMY(sim, env, i, vy) * AR_DT;
    }

    // Enemy/enemy separation through a whole-arena uniform grid. The half
    // neighborhood (self, +x, -y, +x-y) visits every unordered cell pair once;
    // same-cell pairs are ordered by slot id.
    int* head = sim->grid_head;
    int* next = sim->enemy_next;
    for (int c = 0; c < AR_GRID_CELLS; c++) head[AR_IDX(sim, c, env)] = -1;
    for (int k = 0; k < count; k++) {
        int i = AR_ENEMY(sim, env, k, dense);
        int cell = ar_geometry_cell(cfg, AR_ENEMY(sim, env, i, x),
            AR_ENEMY(sim, env, i, y));
        next[AR_IDX(sim, i, env)] = head[AR_IDX(sim, cell, env)];
        head[AR_IDX(sim, cell, env)] = i;
    }
    static const int neighbor_dx[4] = {1, -1, 0, 1};
    static const int neighbor_dy[4] = {0, 1, 1, 1};
    for (int k = 0; k < count; k++) {
        int i = AR_ENEMY(sim, env, k, dense);
        float ix = AR_ENEMY(sim, env, i, x);
        float iy = AR_ENEMY(sim, env, i, y);
        int gx = (int)(((ix + half) / cfg->arena_size) * (float)AR_GRID_W);
        int gy = (int)(((iy + half) / cfg->arena_size) * (float)AR_GRID_H);
        float ir = AR_ENEMY(sim, env, i, radius);
        for (int n = -1; n < 4; n++) {
            int cx = gx, cy = gy;
            if (n >= 0) {
                cx = gx + neighbor_dx[n];
                cy = gy + neighbor_dy[n];
                if (cx < 0 || cx >= AR_GRID_W || cy < 0 || cy >= AR_GRID_H) {
                    continue;
                }
            }
            for (int j = head[AR_IDX(sim, cy * AR_GRID_W + cx, env)]; j >= 0;
                    j = next[AR_IDX(sim, j, env)]) {
                if (j == i || (n < 0 && j <= i)) continue;
                float jx = AR_ENEMY(sim, env, j, x);
                float jy = AR_ENEMY(sim, env, j, y);
                ar_separate_half(&ix, &iy, &jx, &jy,
                    ir + AR_ENEMY(sim, env, j, radius));
                AR_ENEMY(sim, env, j, x) = jx;
                AR_ENEMY(sim, env, j, y) = jy;
            }
        }
        AR_ENEMY(sim, env, i, x) = ix;
        AR_ENEMY(sim, env, i, y) = iy;
    }

    // Player and pets yield to enemies (the full push lands on the smaller
    // party, mirroring what contact impulses do on the CPU path).
    for (int k = 0; k < count; k++) {
        int i = AR_ENEMY(sim, env, k, dense);
        float ex = AR_ENEMY(sim, env, i, x);
        float ey = AR_ENEMY(sim, env, i, y);
        float x = AR_P(sim, env, px);
        float y = AR_P(sim, env, py);
        if (ar_geometry_push_out_circle(&x, &y, ex, ey,
                AR_ENEMY(sim, env, i, radius) + cfg->player_radius)) {
            AR_P(sim, env, px) = x;
            AR_P(sim, env, py) = y;
        }
        for (int p = 0; p < AR_MAX_PETS; p++) {
            if (!AR_PET(sim, env, p, active)) continue;
            float pet_x = AR_PET(sim, env, p, x);
            float pet_y = AR_PET(sim, env, p, y);
            if (ar_geometry_push_out_circle(&pet_x, &pet_y, ex, ey,
                    AR_ENEMY(sim, env, i, radius) + AR_PET(sim, env, p, rad))) {
                AR_PET(sim, env, p, x) = pet_x;
                AR_PET(sim, env, p, y) = pet_y;
            }
        }
    }

    // Static pillars push everything out; walls clamp.
    for (int o = 0; o < cfg->obstacle_count; o++) {
        if (!AR_OBSTACLE(sim, env, o, active)) continue;
        float ox = AR_OBSTACLE(sim, env, o, x);
        float oy = AR_OBSTACLE(sim, env, o, y);

        float x = AR_P(sim, env, px);
        float y = AR_P(sim, env, py);
        if (ar_geometry_push_out_circle(&x, &y, ox, oy,
                AR_OBSTACLE(sim, env, o, radius) + cfg->player_radius)) {
            AR_P(sim, env, px) = x;
            AR_P(sim, env, py) = y;
        }
        for (int p = 0; p < AR_MAX_PETS; p++) {
            if (!AR_PET(sim, env, p, active)) continue;
            float pet_x = AR_PET(sim, env, p, x);
            float pet_y = AR_PET(sim, env, p, y);
            if (ar_geometry_push_out_circle(&pet_x, &pet_y, ox, oy,
                    AR_OBSTACLE(sim, env, o, radius)
                        + AR_PET(sim, env, p, rad))) {
                AR_PET(sim, env, p, x) = pet_x;
                AR_PET(sim, env, p, y) = pet_y;
            }
        }
        for (int k = 0; k < count; k++) {
            int i = AR_ENEMY(sim, env, k, dense);
            float ex = AR_ENEMY(sim, env, i, x);
            float ey = AR_ENEMY(sim, env, i, y);
            if (ar_geometry_push_out_circle(&ex, &ey, ox, oy,
                    AR_OBSTACLE(sim, env, o, radius)
                        + AR_ENEMY(sim, env, i, radius))) {
                AR_ENEMY(sim, env, i, x) = ex;
                AR_ENEMY(sim, env, i, y) = ey;
            }
        }
    }
    ar_arena_clamp(sim, env, &AR_P(sim, env, px), &AR_P(sim, env, py),
        cfg->player_radius);
    for (int p = 0; p < AR_MAX_PETS; p++) {
        if (!AR_PET(sim, env, p, active)) continue;
        ar_arena_clamp(sim, env, &AR_PET(sim, env, p, x),
            &AR_PET(sim, env, p, y), AR_PET(sim, env, p, rad));
    }
    for (int k = 0; k < count; k++) {
        int i = AR_ENEMY(sim, env, k, dense);
        ar_arena_clamp(sim, env, &AR_ENEMY(sim, env, i, x),
            &AR_ENEMY(sim, env, i, y), AR_ENEMY(sim, env, i, radius));
    }
    ar_dungeon_collide(sim, env);
}

#endif  // AR_GPU_SIM

AR_SIM_FN void ar_move_authority(ARSim* sim, int env) {
#ifdef AR_GPU_SIM
    ar_gpu_move_authority(sim, env);
#else
    ar_phys_set_velocity(sim->player_body, AR_P(sim, env, pvx),
        AR_P(sim, env, pvy));
    for (int i = 0; i < AR_MAX_PETS; i++) {
        if (!AR_PET(sim, env, i, active)) continue;
        ar_phys_set_velocity(sim->pet_body[i], AR_PET(sim, env, i, vx),
            AR_PET(sim, env, i, vy));
    }
    int count = AR_P(sim, env, enemy_count);
    for (int k = 0; k < count; k++) {
        int i = AR_ENEMY(sim, env, k, dense);
        ar_phys_set_velocity(sim->enemy_body[i], AR_ENEMY(sim, env, i, vx),
            AR_ENEMY(sim, env, i, vy));
    }
    ar_phys_step(sim->world);

    float x, y;
    ar_phys_position(sim->player_body, &x, &y);
    AR_P(sim, env, px) = x;
    AR_P(sim, env, py) = y;
    for (int i = 0; i < AR_MAX_PETS; i++) {
        if (!AR_PET(sim, env, i, active)) continue;
        ar_phys_position(sim->pet_body[i], &x, &y);
        AR_PET(sim, env, i, x) = x;
        AR_PET(sim, env, i, y) = y;
    }
    for (int k = 0; k < count; k++) {
        int i = AR_ENEMY(sim, env, k, dense);
        ar_phys_position(sim->enemy_body[i], &x, &y);
        AR_ENEMY(sim, env, i, x) = x;
        AR_ENEMY(sim, env, i, y) = y;
    }
    ar_dungeon_collide(sim, env);
#endif
}

// -----------------------------------------------------------------------------
// Combat.
// -----------------------------------------------------------------------------

AR_SIM_FN void ar_combat(ARSim* sim, int env) {
    ARConfig* cfg = &sim->cfg;

    // Pet melee attacks.
    for (int p = 0; p < AR_MAX_PETS; p++) {
        if (!AR_PET(sim, env, p, active)) continue;
        float cd = AR_PET(sim, env, p, cd) - AR_DT;
        AR_PET(sim, env, p, cd) = cd > 0.0f ? cd : 0.0f;

        int target = AR_PET(sim, env, p, target);
        if (target < 0 || !AR_ENEMY(sim, env, target, active)) continue;
        float reach = cfg->pet_attack_range + AR_PET(sim, env, p, rad)
            + AR_ENEMY(sim, env, target, radius);
        float d2 = ar_geometry_dist2(AR_PET(sim, env, p, x),
            AR_PET(sim, env, p, y), AR_ENEMY(sim, env, target, x),
            AR_ENEMY(sim, env, target, y));
        if (d2 > reach * reach || AR_PET(sim, env, p, cd) > 0.0f) continue;

        AR_PET(sim, env, p, attacking) = 1;
        AR_PET(sim, env, p, cd) = cfg->pet_attack_cooldown;
        ar_damage_enemy(sim, env, target, AR_PET(sim, env, p, dmg));
    }

    // Frost slow ticks down once per step.
    {
        int count = AR_P(sim, env, enemy_count);
        for (int k = 0; k < count; k++) {
            int i = AR_ENEMY(sim, env, k, dense);
            if (AR_ENEMY(sim, env, i, slow_timer) > 0) {
                AR_ENEMY(sim, env, i, slow_timer) -= 1;
            }
        }
    }

    // Enemy contact damage. The player's invulnerability window deduplicates
    // a swarming horde into one hit per window.
    if (AR_P(sim, env, invuln_timer) > 0) {
        AR_P(sim, env, invuln_timer) -= 1;
    } else {
        int count = AR_P(sim, env, enemy_count);
        for (int k = 0; k < count; k++) {
            int i = AR_ENEMY(sim, env, k, dense);
            float touch = cfg->player_radius + AR_ENEMY(sim, env, i, radius);
            if (ar_geometry_dist2(AR_P(sim, env, px), AR_P(sim, env, py),
                    AR_ENEMY(sim, env, i, x), AR_ENEMY(sim, env, i, y))
                    >= touch * touch) {
                continue;
            }
            float dmg = AR_ENEMY(sim, env, i, damage);
            AR_P(sim, env, hp) -= dmg;
            AR_P(sim, env, episode_damage_taken) += dmg;
            AR_P(sim, env, invuln_timer) = cfg->invuln_steps;
            float r = dmg * cfg->reward_hurt;
            AR_P(sim, env, episode_return) += r;
            AR_P(sim, env, episode_reward_hurt) += r;
            AR_REWARD(sim, env) += r;
            break;
        }
    }

    // Enemies grind pets down on contact; pets flash the same invulnerability
    // window the player has.
    int count = AR_P(sim, env, enemy_count);
    for (int p = 0; p < AR_MAX_PETS; p++) {
        if (!AR_PET(sim, env, p, active)) continue;
        if (AR_PET(sim, env, p, invuln) > 0) {
            AR_PET(sim, env, p, invuln) -= 1;
            continue;
        }
        for (int k = 0; k < count; k++) {
            int i = AR_ENEMY(sim, env, k, dense);
            float touch = AR_PET(sim, env, p, rad) + AR_ENEMY(sim, env, i, radius);
            if (ar_geometry_dist2(AR_PET(sim, env, p, x), AR_PET(sim, env, p, y),
                    AR_ENEMY(sim, env, i, x), AR_ENEMY(sim, env, i, y))
                    >= touch * touch) {
                continue;
            }
            AR_PET(sim, env, p, hp) -= AR_ENEMY(sim, env, i, damage);
            AR_PET(sim, env, p, invuln) = cfg->pet_invuln_steps;
            if (AR_PET(sim, env, p, hp) <= 0.0f) {
                AR_P(sim, env, episode_pets_lost) += 1.0f;
                float r = cfg->reward_pet_lose;
                AR_P(sim, env, episode_return) += r;
                AR_REWARD(sim, env) += r;
                ar_free_pet(sim, env, p);
            }
            break;
        }
    }
}

// -----------------------------------------------------------------------------
// RTS economy: harvestable shards, summon/build costs, totem pulses.
// -----------------------------------------------------------------------------

AR_SIM_FN int ar_shard_spawn_at(ARSim* sim, int env, float x, float y,
        float value) {
    for (int i = 0; i < AR_MAX_SHARDS; i++) {
        if (AR_SHARD(sim, env, i, active)) continue;
        AR_SHARD(sim, env, i, active) = 1;
        AR_SHARD(sim, env, i, x) = x;
        AR_SHARD(sim, env, i, y) = y;
        AR_SHARD(sim, env, i, value) = value;
        AR_P(sim, env, shard_count) += 1;
        return i;
    }
    return -1;
}

AR_SIM_FN void ar_shard_scatter(ARSim* sim, int env, int n, float min_dist) {
    ARConfig* cfg = &sim->cfg;
    for (int k = 0; k < n; k++) {
        for (int t = 0; t < 10; t++) {
            float limit = 0.5f * cfg->arena_size - 1.0f;
            float x = (ar_randf(sim, env) * 2.0f - 1.0f) * limit;
            float y = (ar_randf(sim, env) * 2.0f - 1.0f) * limit;
            if (!ar_geometry_floor(AR_DUN(sim, env), cfg->arena_size, x, y)) {
                continue;
            }
            float dx = x - AR_P(sim, env, px), dy = y - AR_P(sim, env, py);
            if (dx * dx + dy * dy < min_dist * min_dist) continue;
            float value = ar_randf(sim, env) < 0.2f ? 3.0f : 1.0f;
            ar_shard_spawn_at(sim, env, x, y, value);
            break;
        }
    }
}

// Vacuum pickups by the player and pets.
AR_SIM_FN void ar_pickup(ARSim* sim, int env) {
    for (int i = 0; i < AR_MAX_SHARDS; i++) {
        if (!AR_SHARD(sim, env, i, active)) continue;
        float sx = AR_SHARD(sim, env, i, x);
        float sy = AR_SHARD(sim, env, i, y);
        int got = 0;
        {
            float touch = sim->cfg.player_radius + 0.7f;
            if (ar_geometry_dist2(sx, sy, AR_P(sim, env, px),
                    AR_P(sim, env, py)) < touch * touch) {
                got = 1;
            }
        }
        if (!got) {
            for (int p = 0; p < AR_MAX_PETS; p++) {
                if (!AR_PET(sim, env, p, active)) continue;
                float touch = AR_PET(sim, env, p, rad) + 0.7f;
                if (ar_geometry_dist2(sx, sy, AR_PET(sim, env, p, x),
                        AR_PET(sim, env, p, y)) < touch * touch) {
                    got = 1;
                    break;
                }
            }
        }
        if (got) {
            AR_SHARD(sim, env, i, active) = 0;
            AR_P(sim, env, shard_count) -= 1;
            AR_P(sim, env, shards) += AR_SHARD(sim, env, i, value);
        }
    }
}

AR_SIM_FN int ar_build(ARSim* sim, int env, int kind) {
    ARConfig* cfg = &sim->cfg;
    if (kind < 0 || kind >= AR_BUILD_KIND_COUNT) return -1;
    if (AR_P(sim, env, builds_alive) >= AR_MAX_BUILDINGS) return -1;
    if (AR_P(sim, env, shards) < cfg->build_cost[kind]) return -1;
    int slot = -1;
    for (int i = 0; i < AR_MAX_BUILDINGS; i++) {
        if (!AR_BUILD(sim, env, i, active)) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;
    // Footprint in front of the player; must be walkable ground.
    float dx = AR_P(sim, env, facing_left) ? -1.0f : 1.0f;
    float x = AR_P(sim, env, px) + dx * 1.4f;
    float y = AR_P(sim, env, py);
    ar_arena_clamp(sim, env, &x, &y, cfg->build_radius[kind]);
    if (!ar_geometry_floor(AR_DUN(sim, env), cfg->arena_size, x, y)) return -1;
    AR_P(sim, env, shards) -= cfg->build_cost[kind];
    AR_BUILD(sim, env, slot, active) = 1;
    AR_BUILD(sim, env, slot, kind) = (uint8_t)kind;
    AR_BUILD(sim, env, slot, x) = x;
    AR_BUILD(sim, env, slot, y) = y;
    AR_BUILD(sim, env, slot, max_hp) = cfg->build_hp[kind];
    AR_BUILD(sim, env, slot, hp) = cfg->build_hp[kind];
    AR_BUILD(sim, env, slot, rad) = cfg->build_radius[kind];
    AR_BUILD(sim, env, slot, flash) = 0.0f;
    AR_BUILD(sim, env, slot, cd) = 0.0f;
    AR_BUILD(sim, env, slot, hurtcd) = 0.0f;
    AR_P(sim, env, builds_alive) += 1;
    return slot;
}

// Totem damage pulses.
AR_SIM_FN void ar_totems(ARSim* sim, int env) {
    ARConfig* cfg = &sim->cfg;
    for (int b = 0; b < AR_MAX_BUILDINGS; b++) {
        if (!AR_BUILD(sim, env, b, active)) continue;
        if (AR_BUILD(sim, env, b, flash) > 0.0f) {
            AR_BUILD(sim, env, b, flash) -= AR_DT;
        }
        if (AR_BUILD(sim, env, b, kind) != AR_BUILD_TOTEM) continue;
        float cd = AR_BUILD(sim, env, b, cd) - AR_DT;
        AR_BUILD(sim, env, b, cd) = cd > 0.0f ? cd : 0.0f;
        if (cd > 0.0f) continue;
        AR_BUILD(sim, env, b, cd) = cfg->totem_period;
        AR_BUILD(sim, env, b, flash) = 0.4f;
        float r2 = cfg->totem_radius * cfg->totem_radius;
        for (int k = AR_P(sim, env, enemy_count) - 1; k >= 0; k--) {
            int i = AR_ENEMY(sim, env, k, dense);
            if (ar_geometry_dist2(AR_BUILD(sim, env, b, x),
                    AR_BUILD(sim, env, b, y), AR_ENEMY(sim, env, i, x),
                    AR_ENEMY(sim, env, i, y)) <= r2) {
                ar_damage_enemy(sim, env, i, cfg->totem_damage);
            }
        }
    }
}

// Enemies chew through buildings on contact.
AR_SIM_FN void ar_building_contact(ARSim* sim, int env) {
    int count = AR_P(sim, env, enemy_count);
    for (int b = 0; b < AR_MAX_BUILDINGS; b++) {
        if (!AR_BUILD(sim, env, b, active)) continue;
        if (AR_BUILD(sim, env, b, hurtcd) > 0.0f) {
            AR_BUILD(sim, env, b, hurtcd) -= AR_DT;
            continue;
        }
        for (int k = 0; k < count; k++) {
            int i = AR_ENEMY(sim, env, k, dense);
            float touch = AR_BUILD(sim, env, b, rad)
                + AR_ENEMY(sim, env, i, radius);
            if (ar_geometry_dist2(AR_BUILD(sim, env, b, x),
                    AR_BUILD(sim, env, b, y), AR_ENEMY(sim, env, i, x),
                    AR_ENEMY(sim, env, i, y)) >= touch * touch) {
                continue;
            }
            AR_BUILD(sim, env, b, hp) -= AR_ENEMY(sim, env, i, damage);
            AR_BUILD(sim, env, b, hurtcd) = 0.5f;
            if (AR_BUILD(sim, env, b, hp) <= 0.0f) {
                AR_BUILD(sim, env, b, active) = 0;
                AR_P(sim, env, builds_alive) -= 1;
            }
            break;
        }
    }
}

// -----------------------------------------------------------------------------
// Spawning and waves.
// -----------------------------------------------------------------------------

AR_SIM_FN int ar_current_wave(ARSim* sim, int env) {
    return AR_P(sim, env, tick) / sim->cfg.wave_length_steps;
}

AR_SIM_FN void ar_pick_spawn_position(ARSim* sim, int env, float radius,
        float* out_x, float* out_y) {
    ARConfig* cfg = &sim->cfg;
    ar_floor_near(sim, env, AR_P(sim, env, px), AR_P(sim, env, py),
        cfg->enemy_spawn_radius, radius, out_x, out_y);
}

AR_SIM_FN void ar_spawning(ARSim* sim, int env) {
    ARConfig* cfg = &sim->cfg;
    int wave = ar_current_wave(sim, env);
    int interval = cfg->spawn_interval - wave * cfg->spawn_interval_per_wave;
    if (interval < cfg->spawn_min_interval) interval = cfg->spawn_min_interval;

    int timer = AR_P(sim, env, spawn_timer) - 1;
    if (timer > 0) {
        AR_P(sim, env, spawn_timer) = timer;
        return;
    }
    AR_P(sim, env, spawn_timer) = interval;

    float hp_scale = ar_wave_hp_scale(sim, env, wave);
    float speed_scale = ar_wave_speed_scale(sim, env, wave);
    for (int n = 0; n < cfg->spawn_batch; n++) {
        if (AR_P(sim, env, enemy_count) >= cfg->enemy_cap) return;
        float x, y;
        ar_pick_spawn_position(sim, env, cfg->enemy_radius[AR_ENEMY_GRUNT],
            &x, &y);
        // Grunts first; brutes join from enemy_kind_switch_wave onward.
        int kind = AR_ENEMY_GRUNT;
        if (wave >= cfg->enemy_kind_switch_wave && ar_randf(sim, env) < 0.25f) {
            kind = AR_ENEMY_BRUTE;
        }
        ar_spawn_enemy(sim, env, kind, x, y, hp_scale, speed_scale);
    }
}

// -----------------------------------------------------------------------------
// Observations.
// -----------------------------------------------------------------------------

AR_SIM_FN void ar_compute_observations(ARSim* sim, int env) {
    ARConfig* cfg = &sim->cfg;
    float* obs = AR_OBS(sim, env);
    for (int i = 0; i < AR_OBS_SIZE; i++) obs[i] = 0.0f;
    float half = 0.5f * cfg->arena_size;

    float* player = obs + AR_OBS_PLAYER_BASE;
    player[0] = AR_P(sim, env, px) / half;
    player[1] = AR_P(sim, env, py) / half;
    player[2] = AR_P(sim, env, pvx) / cfg->player_speed;
    player[3] = AR_P(sim, env, pvy) / cfg->player_speed;
    player[4] = AR_P(sim, env, hp) / AR_P(sim, env, max_hp);
    player[5] = cfg->summon_cooldown > 0.0f
        ? AR_P(sim, env, summon_cd) / cfg->summon_cooldown : 0.0f;
    player[6] = cfg->pet_cap > 0
        ? (float)AR_P(sim, env, pets_alive) / (float)cfg->pet_cap : 0.0f;
    player[7] = cfg->enemy_cap > 0
        ? (float)AR_P(sim, env, enemy_count) / (float)cfg->enemy_cap : 0.0f;
    player[8] = (float)AR_P(sim, env, order)
        / (float)(AR_ORDER_COUNT - 1);
    player[9] = cfg->dash_cooldown > 0.0f
        ? AR_P(sim, env, dash_cd) / cfg->dash_cooldown : 0.0f;
    player[10] = cfg->nova_cooldown > 0.0f
        ? AR_P(sim, env, nova_cd) / cfg->nova_cooldown : 0.0f;
    player[11] = cfg->frost_cooldown > 0.0f
        ? AR_P(sim, env, frost_cd) / cfg->frost_cooldown : 0.0f;
    player[12] = ar_clampf(AR_P(sim, env, shards) / 50.0f, 0.0f, 1.0f);
    player[13] = (float)AR_P(sim, env, builds_alive)
        / (float)AR_MAX_BUILDINGS;

    float* pets_out = obs + AR_OBS_PET_BASE;
    for (int p = 0; p < AR_MAX_PETS; p++) {
        float* slot = pets_out + p * AR_PET_FEATURES;
        slot[0] = AR_PET(sim, env, p, active) ? 1.0f : 0.0f;
        if (!AR_PET(sim, env, p, active)) continue;
        slot[1] = (AR_PET(sim, env, p, x) - AR_P(sim, env, px)) / half;
        slot[2] = (AR_PET(sim, env, p, y) - AR_P(sim, env, py)) / half;
        slot[3] = AR_PET(sim, env, p, hp) / AR_PET(sim, env, p, max_hp);
        slot[4] = cfg->pet_attack_cooldown > 0.0f
            ? AR_PET(sim, env, p, cd) / cfg->pet_attack_cooldown : 0.0f;
        slot[5] = AR_PET(sim, env, p, attacking) ? 1.0f : 0.0f;
        int target = AR_PET(sim, env, p, target);
        slot[6] = (target >= 0 && AR_ENEMY(sim, env, target, active)) ? 1.0f : 0.0f;
        slot[7] = (float)AR_PET(sim, env, p, kind)
            / (float)(AR_PET_CLASS_COUNT - 1);
    }

    // Nearest-enemy slots by distance (insertion into a fixed top-K).
    int slot_index[AR_ENEMY_SLOTS];
    float slot_d2[AR_ENEMY_SLOTS];
    for (int s = 0; s < AR_ENEMY_SLOTS; s++) {
        slot_index[s] = -1;
        slot_d2[s] = 1e30f;
    }
    int count = AR_P(sim, env, enemy_count);
    for (int k = 0; k < count; k++) {
        int i = AR_ENEMY(sim, env, k, dense);
        float d2 = ar_geometry_dist2(AR_P(sim, env, px), AR_P(sim, env, py),
            AR_ENEMY(sim, env, i, x), AR_ENEMY(sim, env, i, y));
        for (int s = 0; s < AR_ENEMY_SLOTS; s++) {
            if (d2 < slot_d2[s]) {
                for (int t = AR_ENEMY_SLOTS - 1; t > s; t--) {
                    slot_index[t] = slot_index[t - 1];
                    slot_d2[t] = slot_d2[t - 1];
                }
                slot_index[s] = i;
                slot_d2[s] = d2;
                break;
            }
        }
    }
    AR_P(sim, env, nearest_enemy) = slot_index[0];

    float* enemies_out = obs + AR_OBS_ENEMY_BASE;
    for (int s = 0; s < AR_ENEMY_SLOTS; s++) {
        float* slot = enemies_out + s * AR_ENEMY_FEATURES;
        int i = slot_index[s];
        if (i < 0) continue;
        slot[0] = (AR_ENEMY(sim, env, i, x) - AR_P(sim, env, px)) / half;
        slot[1] = (AR_ENEMY(sim, env, i, y) - AR_P(sim, env, py)) / half;
        slot[2] = AR_ENEMY(sim, env, i, hp) / AR_ENEMY(sim, env, i, max_hp);
        slot[3] = (float)AR_ENEMY(sim, env, i, type)
            / (float)(AR_ENEMY_KIND_COUNT - 1);
        slot[4] = AR_ENEMY(sim, env, i, slow_timer) > 0 ? 1.0f : 0.0f;
    }
}

// -----------------------------------------------------------------------------
// Episode lifecycle.
// -----------------------------------------------------------------------------

AR_SIM_FN void ar_end_episode(ARSim* sim, int env, int success) {
    ARConfig* cfg = &sim->cfg;
    float terminal = success ? cfg->reward_success : cfg->reward_death;
    AR_P(sim, env, episode_return) += terminal;
    AR_P(sim, env, episode_reward_terminal) += terminal;
    AR_REWARD(sim, env) += terminal;
    AR_TERMINAL(sim, env) = 1.0f;

    Log* log = &AR_LOG(sim, env);
    log->perf += success ? 1.0f : 0.0f;
    log->score += AR_P(sim, env, episode_kills);
    log->episode_return += AR_P(sim, env, episode_return);
    log->episode_length += (float)AR_P(sim, env, tick);
    log->reward_survival += AR_P(sim, env, episode_reward_survival);
    log->reward_kill += AR_P(sim, env, episode_reward_kill);
    log->reward_damage += AR_P(sim, env, episode_reward_damage);
    log->reward_hurt += AR_P(sim, env, episode_reward_hurt);
    log->reward_summon += AR_P(sim, env, episode_reward_summon);
    log->reward_terminal += AR_P(sim, env, episode_reward_terminal);
    log->kills += AR_P(sim, env, episode_kills);
    log->summons += AR_P(sim, env, episode_summons);
    log->pets_lost += AR_P(sim, env, episode_pets_lost);
    log->pets_alive += (float)AR_P(sim, env, pets_alive);
    log->damage_dealt += AR_P(sim, env, episode_damage_dealt);
    log->damage_taken += AR_P(sim, env, episode_damage_taken);
    log->enemies_alive += (float)AR_P(sim, env, enemy_count);
    log->wave += (float)ar_current_wave(sim, env);
    log->hp += AR_P(sim, env, hp);
    log->success += success ? 1.0f : 0.0f;
    log->n += 1.0f;
}

// -----------------------------------------------------------------------------
// Procedural open-world terrain: layered value-noise elevation + moisture,
// a meandering river, sandy shores, and a guaranteed spawn clearing.
// Cells hold ARTile values in AR_DUN.
// -----------------------------------------------------------------------------

AR_SIM_FN uint32_t ar_hash_xy(uint32_t seed, int x, int y) {
    uint32_t h = seed + (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

AR_SIM_FN float ar_vnoise(uint32_t seed, float x, float y) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float xf = x - (float)xi, yf = y - (float)yi;
    float u = xf * xf * (3.0f - 2.0f * xf);
    float v = yf * yf * (3.0f - 2.0f * yf);
    float a = (float)(ar_hash_xy(seed, xi, yi) & 0xffffffu) / 16777216.0f;
    float b = (float)(ar_hash_xy(seed, xi + 1, yi) & 0xffffffu) / 16777216.0f;
    float c = (float)(ar_hash_xy(seed, xi, yi + 1) & 0xffffffu) / 16777216.0f;
    float d = (float)(ar_hash_xy(seed, xi + 1, yi + 1) & 0xffffffu)
        / 16777216.0f;
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}

AR_SIM_FN float ar_fbm(uint32_t seed, float x, float y) {
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    for (int o = 0; o < 3; o++) {
        sum += amp * ar_vnoise(seed + (uint32_t)o * 1013904223u, x, y);
        norm += amp;
        amp *= 0.5f;
        x *= 2.03f;
        y *= 2.03f;
    }
    return sum / norm;
}

AR_SIM_FN void ar_gen_dungeon(ARSim* sim, int env, float* spawn_x,
        float* spawn_y) {
    ARConfig* cfg = &sim->cfg;
    uint8_t* dun = AR_DUN(sim, env);
    AR_P(sim, env, dungeon_seed) = ar_rand_u32(sim, env);
    uint32_t s = AR_P(sim, env, dungeon_seed);

    // Layer 1+2: elevation and moisture fields -> base tiles.
    for (int gy = 0; gy < AR_DUN_H; gy++) {
        for (int gx = 0; gx < AR_DUN_W; gx++) {
            float nx = (float)gx / (float)AR_DUN_W;
            float ny = (float)gy / (float)AR_DUN_H;
            float e = ar_fbm(s, nx * 3.0f, ny * 3.0f);
            float m = ar_fbm(s ^ 0x9e3779b9u, nx * 3.0f + 7.0f, ny * 3.0f);
            uint8_t t = AR_TILE_GRASS;
            if (e < 0.34f) t = AR_TILE_DEEP;
            else if (e < 0.42f) t = AR_TILE_SAND;
            else {
                if (m > 0.60f) t = AR_TILE_FOREST;
                if (e > 0.72f) t = AR_TILE_ROCK;
            }
            dun[gy * AR_DUN_W + gx] = t;
        }
    }

    // Layer 3: meandering river carving through everything.
    {
        float phase = ar_randf(sim, env) * 6.2831853f;
        float amp = 3.0f + ar_randf(sim, env) * 3.0f;
        for (int gy = 0; gy < AR_DUN_H; gy++) {
            float rx = AR_DUN_W * 0.5f + amp * sinf(gy * 0.28f + phase)
                + 1.5f * sinf(gy * 0.75f + phase * 2.0f);
            for (int gx = 0; gx < AR_DUN_W; gx++) {
                float d = fabsf((float)gx - rx);
                if (d < 1.0f) dun[gy * AR_DUN_W + gx] = AR_TILE_DEEP;
                else if (d < 2.0f) dun[gy * AR_DUN_W + gx] = AR_TILE_SHALLOW;
                else if (d < 3.0f
                        && dun[gy * AR_DUN_W + gx] != AR_TILE_ROCK) {
                    dun[gy * AR_DUN_W + gx] = AR_TILE_SAND;
                }
            }
        }
    }

    // Border ring is always rock; center spawn clearing is always grass.
    for (int i = 0; i < AR_DUN_W; i++) {
        dun[i] = AR_TILE_ROCK;
        dun[(AR_DUN_H - 1) * AR_DUN_W + i] = AR_TILE_ROCK;
        dun[i * AR_DUN_W] = AR_TILE_ROCK;
        dun[i * AR_DUN_W + AR_DUN_W - 1] = AR_TILE_ROCK;
    }
    for (int gy = 11; gy <= 21; gy++) {
        for (int gx = 11; gx <= 21; gx++) {
            int dx = gx - 16, dy = gy - 16;
            if (dx * dx + dy * dy <= 25
                    && dun[gy * AR_DUN_W + gx] != AR_TILE_ROCK) {
                dun[gy * AR_DUN_W + gx] = AR_TILE_GRASS;
            }
        }
    }
    // Spawn pad itself is always grass.
    for (int gy = 15; gy <= 17; gy++) {
        for (int gx = 15; gx <= 17; gx++) {
            dun[gy * AR_DUN_W + gx] = AR_TILE_GRASS;
        }
    }

    float half = 0.5f * cfg->arena_size;
    float cell = cfg->arena_size / (float)AR_DUN_W;
    *spawn_x = -half + 16.5f * cell;
    *spawn_y = -half + 16.5f * cell;
}

// Static pillar obstacles, re-rolled every episode. Kept clear of the player
// spawn and of each other.
AR_SIM_FN void ar_place_pillars(ARSim* sim, int env) {
    ARConfig* cfg = &sim->cfg;
    int placed = 0;
    for (int attempt = 0; attempt < cfg->obstacle_count * 16
            && placed < cfg->obstacle_count; attempt++) {
        float radius = cfg->obstacle_radius_min
            + ar_randf(sim, env) * (cfg->obstacle_radius_max
                - cfg->obstacle_radius_min);
        float limit = 0.5f * cfg->arena_size - radius - 0.5f;
        float x = (ar_randf(sim, env) * 2.0f - 1.0f) * limit;
        float y = (ar_randf(sim, env) * 2.0f - 1.0f) * limit;
        if (!ar_geometry_floor(AR_DUN(sim, env), cfg->arena_size, x, y)) {
            continue;
        }
        float dxs = x - AR_P(sim, env, px);
        float dys = y - AR_P(sim, env, py);
        float center = cfg->obstacle_center_clearance + radius;
        if (dxs * dxs + dys * dys < center * center) continue;

        int ok = 1;
        for (int o = 0; o < placed; o++) {
            float gap = radius + AR_OBSTACLE(sim, env, o, radius) + 1.0f;
            if (ar_geometry_dist2(x, y, AR_OBSTACLE(sim, env, o, x),
                    AR_OBSTACLE(sim, env, o, y)) < gap * gap) {
                ok = 0;
                break;
            }
        }
        if (!ok) continue;

        AR_OBSTACLE(sim, env, placed, active) = 1;
        AR_OBSTACLE(sim, env, placed, x) = x;
        AR_OBSTACLE(sim, env, placed, y) = y;
        AR_OBSTACLE(sim, env, placed, radius) = radius;
#ifndef AR_GPU_SIM
        sim->obstacle_body[placed] = ar_phys_static_circle(sim->world, x, y,
            radius);
#endif
        placed++;
    }
}

// Decode the move head into a unit direction (0,0 when idle).
// Screen-relative table, shared with ar_steer_player above.
AR_SIM_FN void ar_move_dir(int move, float* dx, float* dy) {
    if (move < 0 || move >= AR_MOVE_ACTION_COUNT) move = 0;
    const float dx_tbl[AR_MOVE_ACTION_COUNT] = {0.0f,
        -0.70710678f, 0.70710678f, -0.70710678f, 0.70710678f,
        -1.0f, 0.0f, 0.0f, 1.0f};
    const float dy_tbl[AR_MOVE_ACTION_COUNT] = {0.0f,
        -0.70710678f, 0.70710678f, 0.70710678f, -0.70710678f,
        0.0f, -1.0f, 1.0f, 0.0f};
    *dx = dx_tbl[move];
    *dy = dy_tbl[move];
}

AR_SIM_FN void ar_abilities(ARSim* sim, int env) {
    ARConfig* cfg = &sim->cfg;
    if (AR_P(sim, env, dash_cd) > 0.0f) AR_P(sim, env, dash_cd) -= AR_DT;
    if (AR_P(sim, env, nova_cd) > 0.0f) AR_P(sim, env, nova_cd) -= AR_DT;
    if (AR_P(sim, env, frost_cd) > 0.0f) AR_P(sim, env, frost_cd) -= AR_DT;
    if (AR_P(sim, env, fx_nova) > 0.0f) AR_P(sim, env, fx_nova) -= AR_DT;
    if (AR_P(sim, env, fx_frost) > 0.0f) AR_P(sim, env, fx_frost) -= AR_DT;
    if (AR_P(sim, env, fx_dash) > 0.0f) AR_P(sim, env, fx_dash) -= AR_DT;

    int ab = (int)ar_clampf(AR_ACTIONS(sim, env)[3], 0.0f,
        (float)(AR_ABILITY_ACTION_COUNT - 1));
    if (ab == AR_ABILITY_DASH && AR_P(sim, env, dash_cd) <= 0.0f) {
        float dx, dy;
        ar_move_dir((int)ar_clampf(AR_ACTIONS(sim, env)[0], 0.0f,
            (float)(AR_MOVE_ACTION_COUNT - 1)), &dx, &dy);
        if (dx == 0.0f && dy == 0.0f) {
            dx = AR_P(sim, env, facing_left) ? -1.0f : 1.0f;
            dy = 0.0f;
        }
        // Blink: fly through enemies, stop at walls. Sample forward and keep
        // the farthest fitting point so the dash never clips into rock.
        float sx = AR_P(sim, env, px), sy = AR_P(sim, env, py);
        float bx = sx, by = sy;
        for (float d = 0.5f; d <= cfg->dash_distance + 0.001f; d += 0.5f) {
            float tx = sx + dx * d, ty = sy + dy * d;
            ar_arena_clamp(sim, env, &tx, &ty, cfg->player_radius);
            float cx = tx, cy = ty;
            if (ar_geometry_collide_dungeon(AR_DUN(sim, env),
                    cfg->arena_size, &cx, &cy, cfg->player_radius)) {
                break;
            }
            bx = tx;
            by = ty;
        }
        AR_P(sim, env, px) = bx;
        AR_P(sim, env, py) = by;
        AR_P(sim, env, pvx) = dx * 2.0f;
        AR_P(sim, env, pvy) = dy * 2.0f;
#ifndef AR_GPU_SIM
        ar_phys_teleport(sim->player_body, bx, by);
#endif
        int iframes = (int)(cfg->dash_iframes / AR_DT);
        if (AR_P(sim, env, invuln_timer) < iframes) {
            AR_P(sim, env, invuln_timer) = iframes;
        }
        AR_P(sim, env, dash_cd) = cfg->dash_cooldown;
        AR_P(sim, env, fx_dash) = 0.35f;
    } else if (ab == AR_ABILITY_NOVA && AR_P(sim, env, nova_cd) <= 0.0f) {
        AR_P(sim, env, nova_cd) = cfg->nova_cooldown;
        AR_P(sim, env, fx_nova) = 0.4f;
        float r2 = cfg->nova_radius * cfg->nova_radius;
        for (int k = AR_P(sim, env, enemy_count) - 1; k >= 0; k--) {
            int i = AR_ENEMY(sim, env, k, dense);
            if (ar_geometry_dist2(AR_P(sim, env, px), AR_P(sim, env, py),
                    AR_ENEMY(sim, env, i, x),
                    AR_ENEMY(sim, env, i, y)) <= r2) {
                ar_damage_enemy(sim, env, i, cfg->nova_damage);
            }
        }
    } else if (ab == AR_ABILITY_FROST && AR_P(sim, env, frost_cd) <= 0.0f) {
        AR_P(sim, env, frost_cd) = cfg->frost_cooldown;
        AR_P(sim, env, fx_frost) = 0.5f;
        // Aim at the player's nearest enemy, else facing direction.
        float ax = AR_P(sim, env, facing_left) ? -1.0f : 1.0f, ay = 0.0f;
        int ne = AR_P(sim, env, nearest_enemy);
        if (ne >= 0 && AR_ENEMY(sim, env, ne, active)) {
            float dx = AR_ENEMY(sim, env, ne, x) - AR_P(sim, env, px);
            float dy = AR_ENEMY(sim, env, ne, y) - AR_P(sim, env, py);
            float d = sqrtf(dx * dx + dy * dy);
            if (d > 0.0001f) {
                ax = dx / d;
                ay = dy / d;
            }
        }
        float cos_half = cosf(cfg->frost_half_angle);
        float r2 = cfg->frost_range * cfg->frost_range;
        int slow_ticks = (int)(cfg->frost_slow_time / AR_DT);
        for (int k = AR_P(sim, env, enemy_count) - 1; k >= 0; k--) {
            int i = AR_ENEMY(sim, env, k, dense);
            float dx = AR_ENEMY(sim, env, i, x) - AR_P(sim, env, px);
            float dy = AR_ENEMY(sim, env, i, y) - AR_P(sim, env, py);
            float d2 = dx * dx + dy * dy;
            if (d2 > r2 || d2 < 0.0001f) continue;
            float d = sqrtf(d2);
            if ((dx * ax + dy * ay) / d < cos_half) continue;
            AR_ENEMY(sim, env, i, slow_timer) = slow_ticks;
            ar_damage_enemy(sim, env, i, cfg->frost_damage);
        }
    }
}

AR_SIM_FN void ar_reset_env(ARSim* sim, int env) {
    ARConfig* cfg = &sim->cfg;

#ifndef AR_GPU_SIM
    // Fresh world per episode: walls, player, and pillars are re-created, and
    // every enemy/pet body from the previous episode dies with the old world.
    if (B3_IS_NULL(sim->world) == false) {
#ifdef _OPENMP
#pragma omp critical(ar_world_lifecycle)
#endif
        b3DestroyWorld(sim->world);
        sim->world = b3_nullWorldId;
    }
    sim->world = ar_phys_create_world(cfg);
    for (int i = 0; i < AR_MAX_ENEMIES; i++) sim->enemy_body[i] = b3_nullBodyId;
    for (int i = 0; i < AR_MAX_PETS; i++) sim->pet_body[i] = b3_nullBodyId;
    for (int i = 0; i < AR_MAX_OBSTACLES; i++) {
        sim->obstacle_body[i] = b3_nullBodyId;
    }
#endif

    AR_P(sim, env, px) = 0.0f;
    AR_P(sim, env, py) = 0.0f;
    AR_P(sim, env, pvx) = 0.0f;
    AR_P(sim, env, pvy) = 0.0f;
    AR_P(sim, env, max_hp) = cfg->player_health;
    AR_P(sim, env, hp) = cfg->player_health;
    AR_P(sim, env, facing_left) = 0;
    AR_P(sim, env, summon_cd) = 0.0f;
    AR_P(sim, env, dash_cd) = 0.0f;
    AR_P(sim, env, nova_cd) = 0.0f;
    AR_P(sim, env, frost_cd) = 0.0f;
    AR_P(sim, env, fx_nova) = 0.0f;
    AR_P(sim, env, fx_frost) = 0.0f;
    AR_P(sim, env, fx_dash) = 0.0f;
    AR_P(sim, env, order) = AR_ORDER_FOLLOW;
    AR_P(sim, env, invuln_timer) = 0;
    AR_P(sim, env, tick) = 0;

    // Dungeon first: spawn, pillars, and movers all depend on the floor.
    {
        float sx, sy;
        ar_gen_dungeon(sim, env, &sx, &sy);
        AR_P(sim, env, px) = sx;
        AR_P(sim, env, py) = sy;
    }

    for (int i = 0; i < AR_MAX_PETS; i++) {
        AR_PET(sim, env, i, active) = 0;
        AR_PET(sim, env, i, kind) = AR_PET_WISP;
        AR_PET(sim, env, i, x) = 0.0f;
        AR_PET(sim, env, i, y) = 0.0f;
        AR_PET(sim, env, i, vx) = 0.0f;
        AR_PET(sim, env, i, vy) = 0.0f;
        AR_PET(sim, env, i, hp) = 0.0f;
        AR_PET(sim, env, i, max_hp) = cfg->pet_health[AR_PET_WISP];
        AR_PET(sim, env, i, spd) = 0.0f;
        AR_PET(sim, env, i, dmg) = 0.0f;
        AR_PET(sim, env, i, rad) = 0.0f;
        AR_PET(sim, env, i, cd) = 0.0f;
        AR_PET(sim, env, i, age) = 0.0f;
        AR_PET(sim, env, i, invuln) = 0;
        AR_PET(sim, env, i, target) = -1;
        AR_PET(sim, env, i, attacking) = 0;
    }
    AR_P(sim, env, pets_alive) = 0;

    for (int i = 0; i < cfg->enemy_cap; i++) {
        AR_ENEMY(sim, env, i, active) = 0;
        AR_ENEMY(sim, env, i, dense_pos) = -1;
        AR_ENEMY(sim, env, i, next) = -1;
        AR_ENEMY(sim, env, i, slow_timer) = 0;
    }
    AR_P(sim, env, enemy_count) = 0;
    AR_P(sim, env, next_enemy_slot) = 0;

    for (int i = 0; i < AR_MAX_OBSTACLES; i++) {
        AR_OBSTACLE(sim, env, i, active) = 0;
        AR_OBSTACLE(sim, env, i, x) = 0.0f;
        AR_OBSTACLE(sim, env, i, y) = 0.0f;
        AR_OBSTACLE(sim, env, i, radius) = 0.0f;
    }
    ar_place_pillars(sim, env);

    AR_P(sim, env, spawn_timer) = cfg->spawn_interval;
    AR_P(sim, env, nearest_enemy) = -1;

    AR_P(sim, env, shards) = cfg->start_shards;
    for (int i = 0; i < AR_MAX_SHARDS; i++) {
        AR_SHARD(sim, env, i, active) = 0;
        AR_SHARD(sim, env, i, x) = 0.0f;
        AR_SHARD(sim, env, i, y) = 0.0f;
        AR_SHARD(sim, env, i, value) = 0.0f;
    }
    AR_P(sim, env, shard_count) = 0;
    for (int i = 0; i < AR_MAX_BUILDINGS; i++) {
        AR_BUILD(sim, env, i, active) = 0;
        AR_BUILD(sim, env, i, kind) = AR_BUILD_TOTEM;
        AR_BUILD(sim, env, i, x) = 0.0f;
        AR_BUILD(sim, env, i, y) = 0.0f;
        AR_BUILD(sim, env, i, hp) = 0.0f;
        AR_BUILD(sim, env, i, max_hp) = 1.0f;
        AR_BUILD(sim, env, i, rad) = 0.0f;
        AR_BUILD(sim, env, i, flash) = 0.0f;
        AR_BUILD(sim, env, i, cd) = 0.0f;
        AR_BUILD(sim, env, i, hurtcd) = 0.0f;
    }
    AR_P(sim, env, builds_alive) = 0;
    ar_shard_scatter(sim, env, 14, 3.0f);

    AR_P(sim, env, episode_return) = 0.0f;
    AR_P(sim, env, episode_reward_survival) = 0.0f;
    AR_P(sim, env, episode_reward_kill) = 0.0f;
    AR_P(sim, env, episode_reward_damage) = 0.0f;
    AR_P(sim, env, episode_reward_hurt) = 0.0f;
    AR_P(sim, env, episode_reward_summon) = 0.0f;
    AR_P(sim, env, episode_reward_terminal) = 0.0f;
    AR_P(sim, env, episode_kills) = 0.0f;
    AR_P(sim, env, episode_summons) = 0.0f;
    AR_P(sim, env, episode_pets_lost) = 0.0f;
    AR_P(sim, env, episode_damage_dealt) = 0.0f;
    AR_P(sim, env, episode_damage_taken) = 0.0f;
    AR_P(sim, env, episode_peak_enemies) = 0.0f;
    AR_P(sim, env, episode_min_hp) = 1e30f;

#ifndef AR_GPU_SIM
    sim->player_body = ar_phys_dynamic_body(sim->world, AR_P(sim, env, px),
        AR_P(sim, env, py), cfg->player_radius);
#endif

    ar_spawning(sim, env);
    ar_compute_observations(sim, env);
}

AR_SIM_FN void ar_step_env(ARSim* sim, int env) {
    ARConfig* cfg = &sim->cfg;
    AR_REWARD(sim, env) = 0.0f;
    AR_TERMINAL(sim, env) = 0.0f;
    AR_P(sim, env, tick) += 1;

    // 1. Actions: movement + summoning (class) + squad order + ability.
    ar_steer_player(sim, env);
    if (AR_P(sim, env, summon_cd) > 0.0f) {
        AR_P(sim, env, summon_cd) -= AR_DT;
    }
    int wants_summon = (int)ar_clampf(AR_ACTIONS(sim, env)[1], 0.0f,
        (float)(AR_SUMMON_ACTION_COUNT - 1));
    if (wants_summon >= 1 && AR_P(sim, env, summon_cd) <= 0.0f
            && AR_P(sim, env, shards)
                >= cfg->summon_cost[wants_summon - 1]) {
        if (ar_summon_pet(sim, env, wants_summon - 1) >= 0) {
            AR_P(sim, env, shards) -= cfg->summon_cost[wants_summon - 1];
            AR_P(sim, env, summon_cd) = cfg->summon_cooldown;
        }
    }
    int order = (int)ar_clampf(AR_ACTIONS(sim, env)[2], 0.0f,
        (float)(AR_ORDER_COUNT - 1));
    AR_P(sim, env, order) = order;
    int wants_build = (int)ar_clampf(AR_ACTIONS(sim, env)[4], 0.0f,
        (float)(AR_BUILD_ACTION_COUNT - 1));
    if (wants_build >= 1) {
        ar_build(sim, env, wants_build - 1);
    }
    ar_abilities(sim, env);

    // 2. Steering targets, then movement.
    ar_steer_pets(sim, env);
    ar_steer_enemies(sim, env);
    ar_move_authority(sim, env);
    ar_pickup(sim, env);

    // 3. Combat and deaths.
    ar_combat(sim, env);
    ar_totems(sim, env);
    ar_building_contact(sim, env);

    // 4. Survival tick, waves, and stats.
    float r = cfg->reward_survival;
    AR_P(sim, env, episode_return) += r;
    AR_P(sim, env, episode_reward_survival) += r;
    AR_REWARD(sim, env) += r;
    ar_spawning(sim, env);
    // Shard trickle keeps the economy alive on long runs.
    if (cfg->shard_trickle_period > 0.0f
            && AR_P(sim, env, shard_count) < cfg->shard_trickle_cap
            && AR_P(sim, env, tick)
                % (int)(cfg->shard_trickle_period / AR_DT) == 0) {
        float x, y;
        ar_floor_near(sim, env, AR_P(sim, env, px), AR_P(sim, env, py),
            cfg->enemy_spawn_radius * 0.5f, 0.3f, &x, &y);
        ar_shard_spawn_at(sim, env, x, y, 1.0f);
    }
    for (int p = 0; p < AR_MAX_PETS; p++) {
        if (AR_PET(sim, env, p, active)) {
            AR_PET(sim, env, p, age) += AR_DT;
        }
    }
    if ((float)AR_P(sim, env, enemy_count)
            > AR_P(sim, env, episode_peak_enemies)) {
        AR_P(sim, env, episode_peak_enemies) = (float)AR_P(sim, env, enemy_count);
    }
    if (AR_P(sim, env, hp) < AR_P(sim, env, episode_min_hp)) {
        AR_P(sim, env, episode_min_hp) = AR_P(sim, env, hp);
    }

    // 5. Terminal checks and the fresh observation.
    if (AR_P(sim, env, hp) <= 0.0f) {
        ar_end_episode(sim, env, 0);
    } else if (AR_P(sim, env, tick) >= cfg->max_steps) {
        ar_end_episode(sim, env, 1);
    }
    ar_compute_observations(sim, env);
}
