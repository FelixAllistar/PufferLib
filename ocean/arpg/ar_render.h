#pragma once

// Raylib isometric top-down renderer for human play. Included only on the CPU
// path (the GPU train binary never sees this file). Rendered gameplay geometry
// (radii, arena) comes from ARConfig, exactly like the sim uses it.

#include "ar_constants.h"

// Isometric projection: 2:1 diamonds, one world unit per tile step.
#define AR_ISO_W 30.0f
#define AR_ISO_H (AR_ISO_W * 0.5f)

// Client-side juice pools (visual only, never touch the sim).
#define AR_PART_MAX 256
#define AR_DMG_MAX 48

typedef struct ARParticle {
    float x, y, vx, vy, life, maxlife, size;
    Color color;
} ARParticle;

typedef struct ARDmgNum {
    float x, y, value, life;
} ARDmgNum;

typedef struct ARClient {
    float cam_x, cam_y;   // world position the camera eases toward
    float off_x, off_y;   // screen offset derived from cam_x/cam_y
    float zoom;
    int show_hitboxes;
    float time;
    float trauma;         // screen shake energy 0..1
    int part_head;
    int dmg_head;
    ARParticle parts[AR_PART_MAX];
    ARDmgNum dmgs[AR_DMG_MAX];
    float enemy_hp_prev[AR_MAX_ENEMIES];
    uint8_t enemy_prev_active[AR_MAX_ENEMIES];
    float player_hp_prev;
    float fx_nova_prev, fx_frost_prev, fx_dash_prev;
    uint8_t pet_prev_active[AR_MAX_PETS];
    uint8_t primed;       // hp snapshot initialized
} ARClient;

static inline ARClient* ar_client(ARPG* env) {
    if (env->client == NULL) {
        env->client = calloc(1, sizeof(ARClient));
        ARClient* client = (ARClient*)env->client;
        client->cam_x = env->px;
        client->cam_y = env->py;
        client->zoom = 1.0f;
        client->show_hitboxes = env->show_hitboxes;
    }
    return (ARClient*)env->client;
}

// World (x, y, z height) -> screen, centered on the camera target.
static inline Vector2 ar_iso(ARClient* client, float x, float y, float z) {
    Vector2 out;
    out.x = (x - y) * AR_ISO_W + client->off_x;
    out.y = (x + y) * AR_ISO_H - z * AR_ISO_W * 0.9f + client->off_y;
    return out;
}

static inline void ar_draw_shadow(ARClient* client, float x, float y,
        float radius) {
    Vector2 p = ar_iso(client, x, y, 0.0f);
    DrawEllipse((int)p.x, (int)p.y, radius * AR_ISO_W * 0.8f,
        radius * AR_ISO_W * 0.4f, (Color){0, 0, 0, 70});
}

// A ground diamond centered at world (x, y) with half-extent `half` units.
// NOTE: llvmpipe + raylib 5.5 drops >0-winding DrawTriangles in this frame,
// so all tris below use <0 (screen-space clockwise) winding.
static inline void ar_draw_diamond(ARClient* client, float x, float y,
        float half, Color color) {
    Vector2 top = ar_iso(client, x, y - half, 0.0f);
    Vector2 right = ar_iso(client, x + half, y, 0.0f);
    Vector2 bottom = ar_iso(client, x, y + half, 0.0f);
    Vector2 left = ar_iso(client, x - half, y, 0.0f);
    DrawTriangle(top, bottom, right, color);
    DrawTriangle(top, left, bottom, color);
}

// Extruded pillar: top diamond lifted by height plus two visible side faces.
static inline void ar_draw_pillar(ARClient* client, float x, float y,
        float radius) {
    float h = radius * 2.4f;
    Vector2 top_t = ar_iso(client, x, y - radius, h);
    Vector2 top_r = ar_iso(client, x + radius, y, h);
    Vector2 top_b = ar_iso(client, x, y + radius, h);
    Vector2 top_l = ar_iso(client, x - radius, y, h);
    Vector2 base_b = ar_iso(client, x, y + radius, 0.0f);
    Vector2 base_r = ar_iso(client, x + radius, y, 0.0f);
    Vector2 base_l = ar_iso(client, x - radius, y, 0.0f);

    Color side_a = (Color){72, 70, 78, 255};
    Color side_b = (Color){56, 54, 62, 255};
    DrawTriangle(top_l, base_b, top_b, side_a);
    DrawTriangle(top_l, base_l, base_b, side_a);
    DrawTriangle(top_b, base_r, top_r, side_b);
    DrawTriangle(top_b, base_b, base_r, side_b);
    DrawTriangle(top_t, top_b, top_r, (Color){104, 102, 112, 255});
    DrawTriangle(top_t, top_l, top_b, (Color){92, 90, 100, 255});
}

static inline void ar_draw_hp_bar(ARClient* client, float x, float y, float z,
        float frac, Color color) {
    Vector2 p = ar_iso(client, x, y, z);
    if (frac > 1.0f) frac = 1.0f;
    if (frac < 0.0f) frac = 0.0f;
    DrawRectangle((int)p.x - 15, (int)p.y - 4, 30, 4, (Color){0, 0, 0, 160});
    DrawRectangle((int)p.x - 14, (int)p.y - 3, (int)(28.0f * frac), 2, color);
}

static inline void ar_draw_player(ARClient* client, ARPG* env) {
    float z = 1.6f;
    Vector2 p = ar_iso(client, env->px, env->py, z);
    // Robe: teardrop from shoulder point down to the shadow.
    Vector2 hem = ar_iso(client, env->px, env->py, 0.0f);
    Color robe = (env->invuln_timer > 0 && ((int)(client->time * 20.0f) & 1))
        ? (Color){168, 130, 255, 140}
        : (Color){124, 88, 214, 255};
    DrawTriangle(p, hem, (Vector2){hem.x + 11.0f, hem.y}, robe);
    DrawTriangle(p, (Vector2){hem.x - 11.0f, hem.y}, hem, robe);
    DrawCircleV(p, 6.5f, (Color){241, 213, 178, 255});   // head
    DrawCircleV(p, 6.5f, (Color){60, 44, 96, 120});      // hood shading
    DrawCircleLines((int)p.x, (int)p.y, 6.5f, (Color){40, 28, 64, 255});
}

static inline void ar_draw_pet(ARClient* client, ARPG* env, int slot) {
    float x = env->pets.x[slot];
    float y = env->pets.y[slot];
    int kind = env->pets.kind[slot];
    float pulse = 1.0f + 0.15f * sinf(client->time * 6.0f + (float)slot);
    float z = 1.3f + 0.15f * sinf(client->time * 3.0f + (float)slot * 1.7f);
    Vector2 p = ar_iso(client, x, y, z);
    Color outer, core, ring;
    float r = 6.0f;
    if (kind == AR_PET_FANG) {
        outer = (Color){255, 150, 70, 230};
        core = (Color){255, 235, 220, 255};
        ring = (Color){255, 150, 70, 90};
        r = 5.0f;
    } else if (kind == AR_PET_AEGIS) {
        outer = (Color){130, 170, 255, 230};
        core = (Color){235, 244, 255, 255};
        ring = (Color){130, 170, 255, 90};
        r = 7.5f;
    } else {
        outer = (Color){120, 236, 255, 230};
        core = (Color){235, 255, 255, 255};
        ring = (Color){120, 236, 255, 90};
    }
    DrawCircleV(p, r * pulse, outer);
    DrawCircleV(p, r * 0.5f * pulse, core);
    DrawCircleLines((int)p.x, (int)p.y, (r + 2.0f) * pulse, ring);

    // Summon flash: expanding ring right after the pet appears.
    if (env->pets.age[slot] < 0.45f) {
        float t = env->pets.age[slot] / 0.45f;
        Vector2 base = ar_iso(client, x, y, 0.0f);
        DrawEllipseLines((int)base.x, (int)base.y, 26.0f * t, 13.0f * t,
            (Color){120, 236, 255, (unsigned char)(200.0f * (1.0f - t))});
    }

    // Attack swipe toward the current target.
    if (env->pets.attacking[slot] && env->pets.target[slot] >= 0
            && env->enemies.active[env->pets.target[slot]]) {
        int t = env->pets.target[slot];
        Vector2 tp = ar_iso(client, env->enemies.x[t], env->enemies.y[t], 8.0f);
        DrawLineEx(p, tp, 2.0f, (Color){235, 255, 255, 180});
    }
}

static inline void ar_draw_enemy(ARClient* client, ARPG* env, int slot) {
    float x = env->enemies.x[slot];
    float y = env->enemies.y[slot];
    float radius = env->enemies.radius[slot];
    int brute = env->enemies.type[slot] == AR_ENEMY_BRUTE;
    float z = radius * 3.0f;
    Vector2 p = ar_iso(client, x, y, z);
    Color body = brute ? (Color){156, 52, 44, 255} : (Color){206, 82, 60, 255};
    Color belly = brute ? (Color){196, 96, 82, 255} : (Color){232, 132, 100, 255};
    if (env->enemies.slow_timer[slot] > 0) {
        // Frost tint: lerp toward ice blue.
        body = (Color){(unsigned char)(body.r * 0.4f + 150 * 0.6f),
            (unsigned char)(body.g * 0.4f + 220 * 0.6f),
            (unsigned char)(body.b * 0.4f + 255 * 0.6f), 255};
        belly = (Color){(unsigned char)(belly.r * 0.4f + 190 * 0.6f),
            (unsigned char)(belly.g * 0.4f + 235 * 0.6f),
            (unsigned char)(belly.b * 0.4f + 255 * 0.6f), 255};
    }
    DrawCircleV(p, radius * AR_ISO_W * 0.85f, body);
    DrawCircleV(p, radius * AR_ISO_W * 0.5f, belly);
    // Eyes track the player.
    Vector2 eyes = ar_iso(client, env->px, env->py, z);
    float dx = eyes.x - p.x, dy = eyes.y - p.y;
    float len = sqrtf(dx * dx + dy * dy);
    if (len > 1.0f) {
        dx /= len;
        dy /= len;
    }
    float er = radius * AR_ISO_W * 0.85f;
    DrawCircleV((Vector2){p.x + dx * er * 0.45f - 3.0f, p.y + dy * er * 0.3f - 2.0f},
        2.2f, (Color){20, 12, 12, 255});
    DrawCircleV((Vector2){p.x + dx * er * 0.45f + 3.0f, p.y + dy * er * 0.3f - 2.0f},
        2.2f, (Color){20, 12, 12, 255});
    ar_draw_hp_bar(client, x, y, z + 1.2f,
        env->enemies.hp[slot] / env->enemies.max_hp[slot],
        brute ? (Color){220, 70, 60, 255} : (Color){240, 130, 90, 255});
}

// Painter's-order entity record: draw farther entities (smaller x+y) first.
typedef struct ARDrawable {
    float depth;
    int kind;    // 0 pillar, 1 enemy, 2 pet, 3 player, 4 rock, 6 tree,
                 // 7 building, 8 shard
    int index;
} ARDrawable;

static inline void ar_draw_hitbox(ARClient* client, float x, float y,
        float radius, Color color) {
    Vector2 p = ar_iso(client, x, y, 0.0f);
    DrawEllipseLines((int)p.x, (int)p.y, radius * AR_ISO_W,
        radius * AR_ISO_W * 0.5f, color);
}

static inline uint32_t ar_hash3(uint32_t s, int x, int y) {
    uint32_t h = s ^ (uint32_t)(x * 374761393u + y * 668265263u);
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static inline void ar_spawn_burst(ARClient* client, float x, float y, int n,
        float speed, float size, Color color) {
    for (int i = 0; i < n; i++) {
        ARParticle* pt = &client->parts[client->part_head];
        client->part_head = (client->part_head + 1) % AR_PART_MAX;
        float a = ((float)rand() / (float)RAND_MAX) * 2.0f * PI;
        float v = speed * (0.4f + 0.6f * ((float)rand() / (float)RAND_MAX));
        pt->x = x;
        pt->y = y;
        pt->vx = cosf(a) * v;
        pt->vy = sinf(a) * v;
        pt->life = pt->maxlife = 0.35f + 0.3f * ((float)rand() / (float)RAND_MAX);
        pt->size = size;
        pt->color = color;
    }
}

static inline void ar_spawn_dmg(ARClient* client, float x, float y,
        float value) {
    ARDmgNum* d = &client->dmgs[client->dmg_head];
    client->dmg_head = (client->dmg_head + 1) % AR_DMG_MAX;
    d->x = x;
    d->y = y;
    d->value = value;
    d->life = 0.7f;
}

static inline void ar_add_trauma(ARClient* client, float t) {
    client->trauma += t;
    if (client->trauma > 1.0f) client->trauma = 1.0f;
}

static inline void c_render(ARPG* env) {
    ARClient* client = ar_client(env);
    if (!IsWindowReady()) {
        InitWindow(1280, 720, "arpg");
        SetTargetFPS(60);
    }
    float dt = GetFrameTime();
    client->time += dt;
    client->show_hitboxes = env->show_hitboxes;

    // Events -> juice: hp deltas become particles, numbers, and shake.
    // Slots recycle, so edges reset the snapshot (good enough visually).
    if (!client->primed) {
        client->player_hp_prev = env->hp;
        for (int i = 0; i < env->cfg.enemy_cap; i++) {
            client->enemy_prev_active[i] = env->enemies.active[i];
            client->enemy_hp_prev[i] = env->enemies.hp[i];
        }
        for (int i = 0; i < AR_MAX_PETS; i++) {
            client->pet_prev_active[i] = env->pets.active[i];
        }
        client->primed = 1;
    } else {
        if (env->hp < client->player_hp_prev - 0.001f) {
            ar_spawn_burst(client, env->px, env->py, 10, 5.0f, 3.0f,
                (Color){230, 70, 60, 255});
            ar_add_trauma(client, 0.35f);
        }
        client->player_hp_prev = env->hp;
        for (int i = 0; i < env->cfg.enemy_cap; i++) {
            if (env->enemies.active[i] && client->enemy_prev_active[i]) {
                float drop = client->enemy_hp_prev[i] - env->enemies.hp[i];
                if (drop > 0.001f) {
                    ar_spawn_dmg(client, env->enemies.x[i], env->enemies.y[i],
                        drop);
                    ar_spawn_burst(client, env->enemies.x[i],
                        env->enemies.y[i], 4, 3.5f, 2.5f,
                        (Color){255, 210, 140, 255});
                }
                client->enemy_hp_prev[i] = env->enemies.hp[i];
            } else if (!env->enemies.active[i] && client->enemy_prev_active[i]) {
                ar_spawn_burst(client, env->enemies.x[i], env->enemies.y[i],
                    12, 5.0f, 3.0f, (Color){255, 150, 80, 255});
                ar_add_trauma(client, 0.12f);
            } else if (env->enemies.active[i]) {
                client->enemy_hp_prev[i] = env->enemies.hp[i];
            }
            client->enemy_prev_active[i] = env->enemies.active[i];
        }
        for (int i = 0; i < AR_MAX_PETS; i++) {
            if (!env->pets.active[i] && client->pet_prev_active[i]) {
                ar_spawn_burst(client, env->pets.x[i], env->pets.y[i], 10,
                    4.0f, 3.0f, (Color){120, 236, 255, 255});
            }
            client->pet_prev_active[i] = env->pets.active[i];
        }
        if (env->fx_nova > 0.0f && client->fx_nova_prev <= 0.0f) {
            ar_spawn_burst(client, env->px, env->py, 26, 9.0f, 3.5f,
                (Color){255, 200, 110, 255});
            ar_add_trauma(client, 0.5f);
        }
        if (env->fx_frost > 0.0f && client->fx_frost_prev <= 0.0f) {
            ar_add_trauma(client, 0.2f);
        }
        if (env->fx_dash > 0.0f && client->fx_dash_prev <= 0.0f) {
            ar_spawn_burst(client, env->px, env->py, 8, 4.0f, 2.5f,
                (Color){200, 170, 255, 255});
        }
    }
    client->fx_nova_prev = env->fx_nova;
    client->fx_frost_prev = env->fx_frost;
    client->fx_dash_prev = env->fx_dash;

    // Particles integrate in world space.
    for (int i = 0; i < AR_PART_MAX; i++) {
        ARParticle* pt = &client->parts[i];
        if (pt->life <= 0.0f) continue;
        pt->life -= dt;
        pt->x += pt->vx * dt;
        pt->y += pt->vy * dt;
        pt->vx *= (1.0f - 3.0f * dt);
        pt->vy *= (1.0f - 3.0f * dt);
    }
    for (int i = 0; i < AR_DMG_MAX; i++) {
        if (client->dmgs[i].life > 0.0f) {
            client->dmgs[i].life -= dt;
            client->dmgs[i].y += 1.2f * dt;
        }
    }

    // Smooth camera follow, then derive the screen offset from the camera.
    float follow = dt * 6.0f;
    if (follow > 1.0f) follow = 1.0f;
    client->cam_x += (env->px - client->cam_x) * follow;
    client->cam_y += (env->py - client->cam_y) * follow;
    client->off_x = 0.5f * (float)GetScreenWidth()
        - (client->cam_x - client->cam_y) * AR_ISO_W;
    client->off_y = 0.42f * (float)GetScreenHeight()
        - (client->cam_x + client->cam_y) * AR_ISO_H;
    // Screen shake: trauma^2 scaled, decaying.
    if (client->trauma > 0.0f) {
        client->trauma -= dt * 1.4f;
        if (client->trauma < 0.0f) client->trauma = 0.0f;
        float sh = client->trauma * client->trauma * 14.0f;
        float sa = ((float)rand() / (float)RAND_MAX) * 2.0f * PI;
        client->off_x += cosf(sa) * sh;
        client->off_y += sinf(sa) * sh;
    }

    BeginDrawing();
    ClearBackground((Color){16, 14, 22, 255});

    float half = 0.5f * env->cfg.arena_size;
    float cell = env->cfg.arena_size / (float)AR_DUN_W;
    // Camera-space cull bounds (u = x-y across, v = x+y down).
    float cu = client->cam_x - client->cam_y;
    float cv = client->cam_x + client->cam_y;
    float ru = (float)GetScreenWidth() * 0.5f / AR_ISO_W + 3.0f;
    float rv = (float)GetScreenHeight() / AR_ISO_H + 12.0f;

    // Terrain floor: one diamond per visible cell, tinted by tile type.
    for (int gy = 0; gy < AR_DUN_H; gy++) {
        for (int gx = 0; gx < AR_DUN_W; gx++) {
            uint8_t t = env->dungeon[gy * AR_DUN_W + gx];
            float wx = -half + (gx + 0.5f) * cell;
            float wy = -half + (gy + 0.5f) * cell;
            float u = wx - wy, v = wx + wy;
            if (u < cu - ru || u > cu + ru || v < cv - rv || v > cv + rv) {
                continue;
            }
            if (t == AR_TILE_GRASS) {
                ar_draw_diamond(client, wx, wy, cell * 0.5f,
                    ((gx + gy) & 1) ? (Color){44, 74, 44, 255}
                        : (Color){38, 66, 40, 255});
                if (ar_hash3(env->dungeon_seed ^ 0x51f15e, gx, gy) % 13 == 0) {
                    Vector2 rp = ar_iso(client, wx + 0.2f, wy - 0.15f, 0.0f);
                    DrawCircleV(rp, 3.0f, (Color){90, 90, 96, 255});
                }
            } else if (t == AR_TILE_FOREST) {
                ar_draw_diamond(client, wx, wy, cell * 0.5f,
                    ((gx + gy) & 1) ? (Color){30, 58, 36, 255}
                        : (Color){26, 52, 32, 255});
            } else if (t == AR_TILE_SAND) {
                ar_draw_diamond(client, wx, wy, cell * 0.5f,
                    ((gx + gy) & 1) ? (Color){150, 130, 90, 255}
                        : (Color){140, 120, 84, 255});
            } else if (t == AR_TILE_SHALLOW) {
                ar_draw_diamond(client, wx, wy, cell * 0.5f,
                    ((gx + gy) & 1) ? (Color){52, 94, 124, 255}
                        : (Color){46, 86, 116, 255});
                if (ar_hash3(env->dungeon_seed ^ 0xbeef, gx, gy) % 4 == 0) {
                    Vector2 rp = ar_iso(client, wx, wy, 0.0f);
                    DrawLineEx((Vector2){rp.x - 4, rp.y},
                        (Vector2){rp.x - 4, rp.y - 8}, 1.5f,
                        (Color){90, 160, 120, 220});
                    DrawLineEx((Vector2){rp.x + 3, rp.y + 1},
                        (Vector2){rp.x + 3, rp.y - 7}, 1.5f,
                        (Color){90, 160, 120, 220});
                }
            } else if (t == AR_TILE_DEEP) {
                int rim = (gx == 0 || gy == 0 || gx == AR_DUN_W - 1
                    || gy == AR_DUN_H - 1);
                if (!rim) {
                    uint8_t a = env->dungeon[gy * AR_DUN_W + gx - 1];
                    uint8_t b = env->dungeon[gy * AR_DUN_W + gx + 1];
                    uint8_t c = env->dungeon[(gy - 1) * AR_DUN_W + gx];
                    uint8_t d = env->dungeon[(gy + 1) * AR_DUN_W + gx];
                    rim = (a != AR_TILE_ROCK && a != AR_TILE_DEEP)
                        || (b != AR_TILE_ROCK && b != AR_TILE_DEEP)
                        || (c != AR_TILE_ROCK && c != AR_TILE_DEEP)
                        || (d != AR_TILE_ROCK && d != AR_TILE_DEEP);
                }
                if (rim) {
                    ar_draw_diamond(client, wx, wy, cell * 0.5f,
                        (Color){36, 70, 110, 255});
                }
            }
        }
    }
    // Torch glow (under entities): hashed walkable cells next to rock.
    int torches = 0;
    for (int gy = 0; gy < AR_DUN_H && torches < 24; gy++) {
        for (int gx = 0; gx < AR_DUN_W && torches < 24; gx++) {
            uint8_t t = env->dungeon[gy * AR_DUN_W + gx];
            if (t == AR_TILE_ROCK || t == AR_TILE_DEEP) continue;
            if (ar_hash3(env->dungeon_seed, gx, gy) % 41 != 0) continue;
            float wx = -half + (gx + 0.5f) * cell;
            float wy = -half + (gy + 0.5f) * cell;
            float u = wx - wy, v = wx + wy;
            if (u < cu - ru || u > cu + ru || v < cv - rv || v > cv + rv) {
                continue;
            }
            int near_wall = (gx == 0 || gy == 0 || gx == AR_DUN_W - 1
                || gy == AR_DUN_H - 1);
            if (!near_wall) {
                near_wall = env->dungeon[gy * AR_DUN_W + gx - 1] == AR_TILE_ROCK
                    || env->dungeon[gy * AR_DUN_W + gx + 1] == AR_TILE_ROCK
                    || env->dungeon[(gy - 1) * AR_DUN_W + gx] == AR_TILE_ROCK
                    || env->dungeon[(gy + 1) * AR_DUN_W + gx] == AR_TILE_ROCK;
            }
            if (!near_wall) continue;
            Vector2 gp = ar_iso(client, wx, wy, 0.5f);
            DrawCircleV(gp, 26.0f, (Color){255, 160, 60, 28});
            DrawCircleV(gp, 13.0f, (Color){255, 190, 90, 40});
            torches++;
        }
    }

    // Painter's order over rock rims, trees, pillars, shards, buildings,
    // enemies, pets, player.
    ARDrawable draws[AR_DUN_CELLS * 2 + AR_MAX_OBSTACLES + AR_MAX_SHARDS
        + AR_MAX_BUILDINGS + AR_MAX_ENEMIES + AR_MAX_PETS + 1];
    int draw_count = 0;
    for (int gy = 0; gy < AR_DUN_H; gy++) {
        for (int gx = 0; gx < AR_DUN_W; gx++) {
            uint8_t t = env->dungeon[gy * AR_DUN_W + gx];
            float wx = -half + (gx + 0.5f) * cell;
            float wy = -half + (gy + 0.5f) * cell;
            float u = wx - wy, v = wx + wy;
            if (u < cu - ru || u > cu + ru || v < cv - rv || v > cv + rv) {
                continue;
            }
            if (t == AR_TILE_ROCK) {
                int rim = (gx == 0 || gy == 0 || gx == AR_DUN_W - 1
                    || gy == AR_DUN_H - 1);
                if (!rim) {
                    uint8_t a = env->dungeon[gy * AR_DUN_W + gx - 1];
                    uint8_t b = env->dungeon[gy * AR_DUN_W + gx + 1];
                    uint8_t c = env->dungeon[(gy - 1) * AR_DUN_W + gx];
                    uint8_t d = env->dungeon[(gy + 1) * AR_DUN_W + gx];
                    rim = (a != AR_TILE_ROCK && a != AR_TILE_DEEP)
                        || (b != AR_TILE_ROCK && b != AR_TILE_DEEP)
                        || (c != AR_TILE_ROCK && c != AR_TILE_DEEP)
                        || (d != AR_TILE_ROCK && d != AR_TILE_DEEP);
                }
                if (!rim) continue;
                draws[draw_count].depth = wx + wy;
                draws[draw_count].kind = 4;
                draws[draw_count].index = gy * AR_DUN_W + gx;
                draw_count++;
            } else if (t == AR_TILE_FOREST
                    && ar_hash3(env->dungeon_seed ^ 0x7ee5, gx, gy) % 6 == 0) {
                draws[draw_count].depth = wx + wy;
                draws[draw_count].kind = 6;
                draws[draw_count].index = gy * AR_DUN_W + gx;
                draw_count++;
            }
        }
    }
    for (int i = 0; i < env->cfg.obstacle_count; i++) {
        if (!env->obstacle_active[i]) continue;
        draws[draw_count].depth = env->obstacle_x[i] + env->obstacle_y[i];
        draws[draw_count].kind = 0;
        draws[draw_count].index = i;
        draw_count++;
    }
    for (int k = 0; k < env->enemy_count; k++) {
        int i = env->enemies.dense[k];
        draws[draw_count].depth = env->enemies.x[i] + env->enemies.y[i];
        draws[draw_count].kind = 1;
        draws[draw_count].index = i;
        draw_count++;
    }
    for (int i = 0; i < AR_MAX_PETS; i++) {
        if (!env->pets.active[i]) continue;
        draws[draw_count].depth = env->pets.x[i] + env->pets.y[i];
        draws[draw_count].kind = 2;
        draws[draw_count].index = i;
        draw_count++;
    }
    draws[draw_count].depth = env->px + env->py;
    draws[draw_count].kind = 3;
    draws[draw_count].index = 0;
    draw_count++;
    for (int i = 0; i < AR_MAX_SHARDS; i++) {
        if (!env->shard_active[i]) continue;
        draws[draw_count].depth = env->shard_x[i] + env->shard_y[i];
        draws[draw_count].kind = 8;
        draws[draw_count].index = i;
        draw_count++;
    }
    for (int i = 0; i < AR_MAX_BUILDINGS; i++) {
        if (!env->build_active[i]) continue;
        draws[draw_count].depth = env->build_x[i] + env->build_y[i];
        draws[draw_count].kind = 7;
        draws[draw_count].index = i;
        draw_count++;
    }
    for (int i = 1; i < draw_count; i++) {
        ARDrawable key = draws[i];
        int j = i - 1;
        while (j >= 0 && draws[j].depth > key.depth) {
            draws[j + 1] = draws[j];
            j--;
        }
        draws[j + 1] = key;
    }

    for (int i = 0; i < draw_count; i++) {
        ARDrawable* d = &draws[i];
        switch (d->kind) {
            case 0:
                ar_draw_shadow(client, env->obstacle_x[d->index],
                    env->obstacle_y[d->index], env->obstacle_radius[d->index]);
                ar_draw_pillar(client, env->obstacle_x[d->index],
                    env->obstacle_y[d->index], env->obstacle_radius[d->index]);
                break;
            case 4: {
                int cx = d->index % AR_DUN_W;
                int cy = d->index / AR_DUN_W;
                float wx = -half + (cx + 0.5f) * cell;
                float wy = -half + (cy + 0.5f) * cell;
                ar_draw_pillar(client, wx, wy, cell * 0.5f);
                break;
            }
            case 6: {
                int cx = d->index % AR_DUN_W;
                int cy = d->index / AR_DUN_W;
                float wx = -half + (cx + 0.5f) * cell;
                float wy = -half + (cy + 0.5f) * cell;
                float sway = sinf(client->time * 1.5f + (float)(cx + cy)) * 1.5f;
                Vector2 base = ar_iso(client, wx, wy, 0.0f);
                Vector2 top = ar_iso(client, wx, wy, 1.6f);
                DrawLineEx(base, top, 3.0f, (Color){96, 70, 50, 255});
                DrawCircleV((Vector2){top.x + sway, top.y}, 9.0f,
                    (Color){34, 96, 44, 255});
                DrawCircleV((Vector2){top.x + sway - 3.0f, top.y - 3.0f}, 5.0f,
                    (Color){48, 128, 58, 255});
                break;
            }
            case 7: {
                int b = d->index;
                float bx = env->build_x[b], by = env->build_y[b];
                if (env->build_kind[b] == AR_BUILD_TOTEM) {
                    Vector2 base = ar_iso(client, bx, by, 0.0f);
                    Vector2 tip = ar_iso(client, bx, by, 1.8f);
                    float pulse = 1.0f
                        + 0.12f * sinf(client->time * 4.0f + (float)b);
                    DrawLineEx(base, tip, 4.0f, (Color){110, 90, 150, 255});
                    DrawCircleV(tip, 6.0f * pulse, (Color){150, 220, 255, 255});
                    DrawCircleV(tip, 3.0f * pulse, (Color){235, 250, 255, 255});
                    if (env->build_flash[b] > 0.0f) {
                        float t = 1.0f - env->build_flash[b] / 0.4f;
                        float r = env->cfg.totem_radius / cell * AR_ISO_W * t;
                        DrawEllipseLines((int)base.x, (int)base.y, r, r * 0.5f,
                            (Color){150, 220, 255,
                                (unsigned char)(200 * (1.0f - t))});
                    }
                } else {
                    ar_draw_shadow(client, bx, by, env->build_rad[b]);
                    ar_draw_pillar(client, bx, by, env->build_rad[b]);
                    ar_draw_hp_bar(client, bx, by, 2.2f,
                        env->build_hp[b] / env->build_max_hp[b],
                        (Color){170, 170, 190, 255});
                }
                break;
            }
            case 8: {
                int s = d->index;
                float bob = sinf(client->time * 3.0f + (float)s * 2.1f) * 0.12f;
                Vector2 sp = ar_iso(client, env->shard_x[s],
                    env->shard_y[s], 0.5f + bob);
                float big = env->shard_value[s] > 1.5f ? 1.4f : 1.0f;
                DrawCircleV(sp, 4.0f * big, (Color){120, 255, 220, 230});
                DrawCircleV(sp, 2.0f * big, (Color){235, 255, 250, 255});
                break;
            }
            case 1:
                ar_draw_shadow(client, env->enemies.x[d->index],
                    env->enemies.y[d->index], env->enemies.radius[d->index]);
                ar_draw_enemy(client, env, d->index);
                break;
            case 2:
                ar_draw_shadow(client, env->pets.x[d->index],
                    env->pets.y[d->index], env->pets.rad[d->index]);
                ar_draw_pet(client, env, d->index);
                break;
            default:
                ar_draw_shadow(client, env->px, env->py, env->cfg.player_radius);
                ar_draw_player(client, env);
                break;
        }
    }

    // Ability flashes (driven by the sim's visual-only fx timers).
    if (env->fx_nova > 0.0f) {
        float t = 1.0f - env->fx_nova / 0.4f;
        Vector2 base = ar_iso(client, env->px, env->py, 0.0f);
        float r = env->cfg.nova_radius / cell * AR_ISO_W * t;
        DrawEllipseLines((int)base.x, (int)base.y, r, r * 0.5f,
            (Color){255, 200, 110, (unsigned char)(220 * (1.0f - t))});
        DrawEllipseLines((int)base.x, (int)base.y, r * 0.7f, r * 0.35f,
            (Color){255, 240, 200, (unsigned char)(160 * (1.0f - t))});
    }
    if (env->fx_frost > 0.0f) {
        float t = 1.0f - env->fx_frost / 0.5f;
        float ax = env->facing_left ? -1.0f : 1.0f, ay = 0.0f;
        if (env->nearest_enemy >= 0
                && env->enemies.active[env->nearest_enemy]) {
            int ne = env->nearest_enemy;
            float dx = env->enemies.x[ne] - env->px;
            float dy = env->enemies.y[ne] - env->py;
            float d = sqrtf(dx * dx + dy * dy);
            if (d > 0.0001f) {
                ax = dx / d;
                ay = dy / d;
            }
        }
        Vector2 p = ar_iso(client, env->px, env->py, 1.2f);
        float base_a = atan2f(ay, ax);
        for (int s = -2; s <= 2; s++) {
            float a = base_a + s * env->cfg.frost_half_angle / 2.5f;
            float rr = env->cfg.frost_range * AR_ISO_W * (0.4f + 0.6f * t);
            Vector2 tip = (Vector2){p.x + cosf(a) * rr,
                p.y + sinf(a) * rr * 0.5f};
            DrawLineEx(p, tip, 3.0f,
                (Color){170, 220, 255, (unsigned char)(180 * (1.0f - t))});
        }
    }
    if (env->fx_dash > 0.0f) {
        float t = env->fx_dash / 0.35f;
        Vector2 p = ar_iso(client, env->px, env->py, 7.0f);
        float dx = env->facing_left ? 1.0f : -1.0f;
        for (int s = 0; s < 3; s++) {
            float yy = p.y - 6.0f + s * 6.0f;
            DrawLineEx((Vector2){p.x + dx * 26.0f * t, yy},
                (Vector2){p.x + dx * 8.0f * t, yy}, 2.0f,
                (Color){200, 170, 255, (unsigned char)(200 * t)});
        }
    }

    // Torch flames (over entities so fire reads on top of glow).
    {
        int torches = 0;
        for (int gy = 0; gy < AR_DUN_H && torches < 24; gy++) {
            for (int gx = 0; gx < AR_DUN_W && torches < 24; gx++) {
                if (!env->dungeon[gy * AR_DUN_W + gx]) continue;
                if (ar_hash3(env->dungeon_seed, gx, gy) % 41 != 0) continue;
                int near_wall = (gx == 0 || gy == 0 || gx == AR_DUN_W - 1
                    || gy == AR_DUN_H - 1);
                if (!near_wall) {
                    near_wall = !env->dungeon[gy * AR_DUN_W + gx - 1]
                        || !env->dungeon[gy * AR_DUN_W + gx + 1]
                        || !env->dungeon[(gy - 1) * AR_DUN_W + gx]
                        || !env->dungeon[(gy + 1) * AR_DUN_W + gx];
                }
                if (!near_wall) continue;
                float wx = -half + (gx + 0.5f) * cell;
                float wy = -half + (gy + 0.5f) * cell;
                Vector2 fp = ar_iso(client, wx, wy, 1.0f);
                float fl = 1.0f + 0.25f * sinf(client->time * 11.0f
                    + (float)(gx * 5 + gy * 11));
                DrawCircleV(fp, 3.2f * fl, (Color){255, 140, 40, 230});
                DrawCircleV(fp, 1.6f * fl, (Color){255, 230, 150, 255});
                torches++;
            }
        }
    }

    // Particles and damage numbers (world-anchored).
    for (int i = 0; i < AR_PART_MAX; i++) {
        ARParticle* pt = &client->parts[i];
        if (pt->life <= 0.0f) continue;
        float a = pt->life / pt->maxlife;
        Vector2 pp = ar_iso(client, pt->x, pt->y, 0.6f);
        DrawCircleV(pp, pt->size * (0.5f + 0.5f * a),
            (Color){pt->color.r, pt->color.g, pt->color.b,
                (unsigned char)(255 * a)});
    }
    for (int i = 0; i < AR_DMG_MAX; i++) {
        ARDmgNum* d = &client->dmgs[i];
        if (d->life <= 0.0f) continue;
        float a = d->life / 0.7f;
        Vector2 pp = ar_iso(client, d->x, d->y, 2.2f);
        DrawText(TextFormat("%.0f", d->value), (int)pp.x - 8,
            (int)pp.y - (int)((0.7f - d->life) * 40.0f), 14,
            (Color){255, 235, 180, (unsigned char)(255 * a)});
    }

    if (client->show_hitboxes) {
        ar_draw_hitbox(client, env->px, env->py, env->cfg.player_radius, GREEN);
        for (int k = 0; k < env->enemy_count; k++) {
            int i = env->enemies.dense[k];
            ar_draw_hitbox(client, env->enemies.x[i], env->enemies.y[i],
                env->enemies.radius[i], RED);
        }
        for (int i = 0; i < AR_MAX_PETS; i++) {
            if (!env->pets.active[i]) continue;
            ar_draw_hitbox(client, env->pets.x[i], env->pets.y[i],
                env->pets.rad[i], SKYBLUE);
        }
    }

    // HUD.
    DrawRectangle(16, 16, 210, 22, (Color){0, 0, 0, 140});
    float hp_frac = env->hp / env->max_hp;
    if (hp_frac < 0.0f) hp_frac = 0.0f;
    DrawRectangle(18, 18, 206.0f * hp_frac, 18,
        hp_frac > 0.35f ? (Color){96, 210, 120, 255} : (Color){220, 80, 70, 255});
    DrawRectangleLines(16, 16, 210, 22, (Color){255, 255, 255, 60});
    DrawText(TextFormat("HP %.0f/%.0f", env->hp, env->max_hp), 232, 20, 16, RAYWHITE);
    DrawText(TextFormat("wave %d   enemies %d   pets %d/%d",
        env->tick / (int)env->cfg.wave_length_steps, env->enemy_count,
        env->pets_alive, env->cfg.pet_cap), 16, 46, 16, (Color){230, 230, 230, 200});
    DrawText(TextFormat("shards %.0f   builds %d/%d",
        env->shards, env->builds_alive, AR_MAX_BUILDINGS), 16, 124, 16,
        (Color){120, 255, 220, 220});
    // Order + summon class + ability cooldowns.
    const char* order_name = "FOLLOW";
    if (env->order == AR_ORDER_ATTACK) order_name = "ATTACK";
    else if (env->order == AR_ORDER_GUARD) order_name = "GUARD";
    else if (env->order == AR_ORDER_FOCUS) order_name = "FOCUS";
    const char* class_name = "POLICY";
    if (env->pick_class == AR_PET_WISP) class_name = "WISP";
    else if (env->pick_class == AR_PET_FANG) class_name = "FANG";
    else if (env->pick_class == AR_PET_AEGIS) class_name = "AEGIS";
    DrawText(TextFormat("%s  |  %s", order_name, class_name), 16, 68, 16,
        (Color){255, 220, 130, 220});
    float cds[3] = {env->dash_cd, env->nova_cd, env->frost_cd};
    float maxs[3] = {env->cfg.dash_cooldown, env->cfg.nova_cooldown,
        env->cfg.frost_cooldown};
    const char* names[3] = {"Q dash", "E nova", "F frost"};
    for (int i = 0; i < 3; i++) {
        float frac = maxs[i] > 0.0f ? 1.0f - cds[i] / maxs[i] : 1.0f;
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;
        int bx = 16 + i * 74;
        DrawRectangle(bx, 92, 68, 10, (Color){0, 0, 0, 140});
        DrawRectangle(bx + 1, 93, (int)(66.0f * frac), 8,
            frac >= 1.0f ? (Color){140, 200, 255, 255} : (Color){90, 110, 150, 255});
        DrawText(names[i], bx + 2, 106, 12, (Color){230, 230, 230, 180});
    }
    // Minimap.
    {
        int mw = 104, mh = 104;
        int mx = GetScreenWidth() - mw - 12, my = 12;
        DrawRectangle(mx - 2, my - 2, mw + 4, mh + 4, (Color){0, 0, 0, 150});
        float s = (float)mw / (float)AR_DUN_W;
        for (int gy = 0; gy < AR_DUN_H; gy++) {
            for (int gx = 0; gx < AR_DUN_W; gx++) {
                uint8_t t = env->dungeon[gy * AR_DUN_W + gx];
                if (t == AR_TILE_ROCK) continue;
                Color c = (Color){110, 130, 110, 255};
                if (t == AR_TILE_FOREST) c = (Color){60, 110, 70, 255};
                else if (t == AR_TILE_SAND) c = (Color){150, 130, 90, 255};
                else if (t == AR_TILE_SHALLOW) c = (Color){60, 100, 140, 255};
                else if (t == AR_TILE_DEEP) c = (Color){40, 70, 120, 255};
                DrawRectangle(mx + (int)(gx * s), my + (int)(gy * s),
                    (int)(s + 0.5f), (int)(s + 0.5f), c);
            }
        }
        float w2m = (float)mw / env->cfg.arena_size;
        for (int k = 0; k < env->enemy_count; k++) {
            int i = env->enemies.dense[k];
            DrawCircle(mx + (int)((env->enemies.x[i] + half) * w2m),
                my + (int)((env->enemies.y[i] + half) * w2m), 2.0f,
                (Color){230, 80, 60, 255});
        }
        for (int i = 0; i < AR_MAX_PETS; i++) {
            if (!env->pets.active[i]) continue;
            DrawCircle(mx + (int)((env->pets.x[i] + half) * w2m),
                my + (int)((env->pets.y[i] + half) * w2m), 2.0f,
                (Color){120, 236, 255, 255});
        }
        for (int i = 0; i < AR_MAX_SHARDS; i++) {
            if (!env->shard_active[i]) continue;
            DrawCircle(mx + (int)((env->shard_x[i] + half) * w2m),
                my + (int)((env->shard_y[i] + half) * w2m), 1.5f,
                (Color){120, 255, 220, 255});
        }
        for (int i = 0; i < AR_MAX_BUILDINGS; i++) {
            if (!env->build_active[i]) continue;
            DrawCircle(mx + (int)((env->build_x[i] + half) * w2m),
                my + (int)((env->build_y[i] + half) * w2m), 2.0f,
                (Color){150, 220, 255, 255});
        }
        DrawCircle(mx + (int)((env->px + half) * w2m),
            my + (int)((env->py + half) * w2m), 3.0f, WHITE);
    }
    DrawText(TextFormat("%5.0f fps", GetFPS()), 16,
        (float)GetScreenHeight() - 30.0f, 16, (Color){230, 230, 230, 140});

    EndDrawing();
}

static inline void c_close(ARPG* env) {
    if (env->client != NULL) {
        if (IsWindowReady()) CloseWindow();
        free(env->client);
        env->client = NULL;
    }
}
