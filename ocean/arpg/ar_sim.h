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
#define AR_OBS(sim, env) ((sim)->observations + (size_t)(env) * AR_OBS_SIZE)
#define AR_ACTIONS(sim, env) ((sim)->actions + (size_t)(env) * NUM_ATNS)
#define AR_REWARD(sim, env) ((sim)->rewards[env])
#define AR_TERMINAL(sim, env) ((sim)->terminals[env])
#define AR_LOG(sim, env) ((sim)->native_envs[env].log)
#else
#define AR_SIM_FN static inline
#define ARSim ARPG
#define AR_IDX(sim, i, env) (i)
#define AR_P(sim, env, field) ((sim)->field)
#define AR_ENEMY(sim, env, i, f) ((sim)->enemies.f[i])
#define AR_PET(sim, env, i, f) ((sim)->pets.f[i])
#define AR_OBSTACLE(sim, env, i, f) ((sim)->obstacle_##f[i])
#define AR_OBS(sim, env) ((sim)->agents[0].observations)
#define AR_ACTIONS(sim, env) ((sim)->agents[0].actions)
#define AR_REWARD(sim, env) ((sim)->agents[0].rewards[0])
#define AR_TERMINAL(sim, env) ((sim)->agents[0].terminals[0])
#define AR_LOG(sim, env) ((sim)->log)
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

AR_SIM_FN int ar_summon_pet(ARSim* sim, int env) {
    ARConfig* cfg = &sim->cfg;
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
    float side = (AR_P(sim, env, facing_left) ? -1.0f : 1.0f);
    float x = AR_P(sim, env, px)
        + side * (cfg->player_radius + cfg->pet_radius + 0.2f);
    float y = AR_P(sim, env, py);
    ar_arena_clamp(sim, env, &x, &y, cfg->pet_radius);

    AR_PET(sim, env, slot, active) = 1;
    AR_PET(sim, env, slot, x) = x;
    AR_PET(sim, env, slot, y) = y;
    AR_PET(sim, env, slot, vx) = 0.0f;
    AR_PET(sim, env, slot, vy) = 0.0f;
    AR_PET(sim, env, slot, max_hp) = cfg->pet_health;
    AR_PET(sim, env, slot, hp) = cfg->pet_health;
    AR_PET(sim, env, slot, cd) = 0.0f;
    AR_PET(sim, env, slot, age) = 0.0f;
    AR_PET(sim, env, slot, invuln) = cfg->pet_invuln_steps;
    AR_PET(sim, env, slot, target) = -1;
    AR_PET(sim, env, slot, attacking) = 0;
    AR_P(sim, env, pets_alive) += 1;

#ifndef AR_GPU_SIM
    sim->pet_body[slot] = ar_phys_dynamic_body(sim->world, x, y,
        cfg->pet_radius);
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
    float vx = 0.0f, vy = 0.0f;
    if (move >= 1) {
        int up = (move == 1 || move == 5 || move == 6);
        int down = (move == 2 || move == 7 || move == 8);
        int left = (move == 3 || move == 5 || move == 7);
        int right = (move == 4 || move == 6 || move == 8);
        vx = (float)((right ? 1 : 0) - (left ? 1 : 0));
        vy = (float)((up ? 1 : 0) - (down ? 1 : 0));
        float len = sqrtf(vx * vx + vy * vy);
        vx /= len;
        vy /= len;
    }
    AR_P(sim, env, pvx) = vx * cfg->player_speed;
    AR_P(sim, env, pvy) = vy * cfg->player_speed;
    if (vx > 0.01f) AR_P(sim, env, facing_left) = 0;
    if (vx < -0.01f) AR_P(sim, env, facing_left) = 1;
}

AR_SIM_FN void ar_steer_pets(ARSim* sim, int env) {
    ARConfig* cfg = &sim->cfg;
    float px = AR_P(sim, env, px);
    float py = AR_P(sim, env, py);
    float aggro2 = cfg->pet_aggro_range * cfg->pet_aggro_range;
    float leash2 = cfg->pet_leash_range * cfg->pet_leash_range;
    float stop2 = cfg->pet_attack_range * 0.8f;
    stop2 *= stop2;
    float follow2 = cfg->pet_follow_distance * cfg->pet_follow_distance;

    for (int i = 0; i < AR_MAX_PETS; i++) {
        AR_PET(sim, env, i, attacking) = 0;
        if (!AR_PET(sim, env, i, active)) continue;
        float x = AR_PET(sim, env, i, x);
        float y = AR_PET(sim, env, i, y);

        // Validate the current target every tick (slots recycle freely).
        int target = AR_PET(sim, env, i, target);
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
        AR_PET(sim, env, i, target) = target;

        float vx = 0.0f, vy = 0.0f;
        if (target >= 0) {
            float dx = AR_ENEMY(sim, env, target, x) - x;
            float dy = AR_ENEMY(sim, env, target, y) - y;
            float d2 = dx * dx + dy * dy;
            if (d2 > stop2) {
                float d = sqrtf(fmaxf(d2, 0.0001f));
                vx = dx / d * cfg->pet_speed;
                vy = dy / d * cfg->pet_speed;
            }
        } else if (ar_geometry_dist2(x, y, px, py) > follow2) {
            float dx = px - x;
            float dy = py - y;
            float d = sqrtf(fmaxf(dx * dx + dy * dy, 0.0001f));
            vx = dx / d * cfg->pet_speed;
            vy = dy / d * cfg->pet_speed;
        }
        AR_PET(sim, env, i, vx) = vx;
        AR_PET(sim, env, i, vy) = vy;
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
        AR_ENEMY(sim, env, i, vx) = dx / d * speed;
        AR_ENEMY(sim, env, i, vy) = dy / d * speed;
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
                    AR_ENEMY(sim, env, i, radius) + cfg->pet_radius)) {
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
                    AR_OBSTACLE(sim, env, o, radius) + cfg->pet_radius)) {
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
            &AR_PET(sim, env, p, y), cfg->pet_radius);
    }
    for (int k = 0; k < count; k++) {
        int i = AR_ENEMY(sim, env, k, dense);
        ar_arena_clamp(sim, env, &AR_ENEMY(sim, env, i, x),
            &AR_ENEMY(sim, env, i, y), AR_ENEMY(sim, env, i, radius));
    }
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
        float reach = cfg->pet_attack_range + cfg->pet_radius
            + AR_ENEMY(sim, env, target, radius);
        float d2 = ar_geometry_dist2(AR_PET(sim, env, p, x),
            AR_PET(sim, env, p, y), AR_ENEMY(sim, env, target, x),
            AR_ENEMY(sim, env, target, y));
        if (d2 > reach * reach || AR_PET(sim, env, p, cd) > 0.0f) continue;

        AR_PET(sim, env, p, attacking) = 1;
        AR_PET(sim, env, p, cd) = cfg->pet_attack_cooldown;
        AR_ENEMY(sim, env, target, hp) -= cfg->pet_damage;
        AR_P(sim, env, episode_damage_dealt) += cfg->pet_damage;
        float r = cfg->pet_damage * cfg->reward_damage;
        AR_P(sim, env, episode_return) += r;
        AR_P(sim, env, episode_reward_damage) += r;
        AR_REWARD(sim, env) += r;

        if (AR_ENEMY(sim, env, target, hp) <= 0.0f) {
            AR_P(sim, env, episode_kills) += 1.0f;
            AR_P(sim, env, episode_return) += cfg->reward_kill;
            AR_P(sim, env, episode_reward_kill) += cfg->reward_kill;
            AR_REWARD(sim, env) += cfg->reward_kill;
            ar_free_enemy(sim, env, target);
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
            float touch = cfg->pet_radius + AR_ENEMY(sim, env, i, radius);
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
// Spawning and waves.
// -----------------------------------------------------------------------------

AR_SIM_FN int ar_current_wave(ARSim* sim, int env) {
    return AR_P(sim, env, tick) / sim->cfg.wave_length_steps;
}

AR_SIM_FN void ar_pick_spawn_position(ARSim* sim, int env, float radius,
        float* out_x, float* out_y) {
    ARConfig* cfg = &sim->cfg;
    float angle = ar_randf(sim, env) * 2.0f * PI;
    float x = AR_P(sim, env, px) + cosf(angle) * cfg->enemy_spawn_radius;
    float y = AR_P(sim, env, py) + sinf(angle) * cfg->enemy_spawn_radius;
    ar_arena_clamp(sim, env, &x, &y, radius);
    *out_x = x;
    *out_y = y;
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

// Static pillar obstacles, re-rolled every episode. Kept clear of the player
// spawn and of each other.
AR_SIM_FN void ar_place_pillars(ARSim* sim, int env) {
    ARConfig* cfg = &sim->cfg;
    int placed = 0;
    for (int attempt = 0; attempt < cfg->obstacle_count * 8
            && placed < cfg->obstacle_count; attempt++) {
        float radius = cfg->obstacle_radius_min
            + ar_randf(sim, env) * (cfg->obstacle_radius_max
                - cfg->obstacle_radius_min);
        float limit = 0.5f * cfg->arena_size - radius - 0.5f;
        float x = (ar_randf(sim, env) * 2.0f - 1.0f) * limit;
        float y = (ar_randf(sim, env) * 2.0f - 1.0f) * limit;
        float center = cfg->obstacle_center_clearance + radius;
        if (x * x + y * y < center * center) continue;

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
    AR_P(sim, env, invuln_timer) = 0;
    AR_P(sim, env, tick) = 0;

    for (int i = 0; i < AR_MAX_PETS; i++) {
        AR_PET(sim, env, i, active) = 0;
        AR_PET(sim, env, i, x) = 0.0f;
        AR_PET(sim, env, i, y) = 0.0f;
        AR_PET(sim, env, i, vx) = 0.0f;
        AR_PET(sim, env, i, vy) = 0.0f;
        AR_PET(sim, env, i, hp) = 0.0f;
        AR_PET(sim, env, i, max_hp) = cfg->pet_health;
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
    sim->player_body = ar_phys_dynamic_body(sim->world, 0.0f, 0.0f,
        cfg->player_radius);
#endif

    ar_spawning(sim, env);
    ar_compute_observations(sim, env);
}

AR_SIM_FN void ar_step_env(ARSim* sim, int env) {
    ARConfig* cfg = &sim->cfg;
    AR_REWARD(sim, env) = 0.0f;
    AR_TERMINAL(sim, env) = 0.0f;
    AR_P(sim, env, tick) += 1;

    // 1. Actions: movement + summoning.
    ar_steer_player(sim, env);
    if (AR_P(sim, env, summon_cd) > 0.0f) {
        AR_P(sim, env, summon_cd) -= AR_DT;
    }
    int wants_summon = (int)ar_clampf(AR_ACTIONS(sim, env)[1], 0.0f,
        (float)(AR_SUMMON_ACTION_COUNT - 1));
    if (wants_summon >= 1 && AR_P(sim, env, summon_cd) <= 0.0f) {
        if (ar_summon_pet(sim, env) >= 0) {
            AR_P(sim, env, summon_cd) = cfg->summon_cooldown;
        }
    }

    // 2. Steering targets, then movement.
    ar_steer_pets(sim, env);
    ar_steer_enemies(sim, env);
    ar_move_authority(sim, env);

    // 3. Combat and deaths.
    ar_combat(sim, env);

    // 4. Survival tick, waves, and stats.
    float r = cfg->reward_survival;
    AR_P(sim, env, episode_return) += r;
    AR_P(sim, env, episode_reward_survival) += r;
    AR_REWARD(sim, env) += r;
    ar_spawning(sim, env);
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
