#pragma once

// Raylib isometric top-down renderer for human play. Included only on the CPU
// path (the GPU train binary never sees this file). Rendered gameplay geometry
// (radii, arena) comes from ARConfig, exactly like the sim uses it.

#include "ar_constants.h"

// Isometric projection: 2:1 diamonds, one world unit per tile step.
#define AR_ISO_W 30.0f
#define AR_ISO_H (AR_ISO_W * 0.5f)

typedef struct ARClient {
    float cam_x, cam_y;   // world position the camera eases toward
    float off_x, off_y;   // screen offset derived from cam_x/cam_y
    float zoom;
    int show_hitboxes;
    float time;
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
static inline void ar_draw_diamond(ARClient* client, float x, float y,
        float half, Color color) {
    Vector2 top = ar_iso(client, x, y - half, 0.0f);
    Vector2 right = ar_iso(client, x + half, y, 0.0f);
    Vector2 bottom = ar_iso(client, x, y + half, 0.0f);
    Vector2 left = ar_iso(client, x - half, y, 0.0f);
    DrawTriangle(top, right, bottom, color);
    DrawTriangle(top, bottom, left, color);
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
    DrawTriangle(top_l, top_b, base_b, side_a);
    DrawTriangle(top_l, base_b, base_l, side_a);
    DrawTriangle(top_b, top_r, base_r, side_b);
    DrawTriangle(top_b, base_r, base_b, side_b);
    DrawTriangle(top_t, top_r, top_b, (Color){104, 102, 112, 255});
    DrawTriangle(top_t, top_b, top_l, (Color){92, 90, 100, 255});
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
    float z = 14.0f;
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
    float pulse = 1.0f + 0.15f * sinf(client->time * 6.0f + (float)slot);
    float z = 12.0f + 2.0f * sinf(client->time * 3.0f + (float)slot * 1.7f);
    Vector2 p = ar_iso(client, x, y, z);
    DrawCircleV(p, 6.0f * pulse, (Color){120, 236, 255, 230});
    DrawCircleV(p, 3.0f * pulse, (Color){235, 255, 255, 255});
    DrawCircleLines((int)p.x, (int)p.y, 8.0f * pulse, (Color){120, 236, 255, 90});

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
    float z = radius * 22.0f;
    Vector2 p = ar_iso(client, x, y, z);
    Color body = brute ? (Color){156, 52, 44, 255} : (Color){206, 82, 60, 255};
    Color belly = brute ? (Color){196, 96, 82, 255} : (Color){232, 132, 100, 255};
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
    ar_draw_hp_bar(client, x, y, z + 14.0f,
        env->enemies.hp[slot] / env->enemies.max_hp[slot],
        brute ? (Color){220, 70, 60, 255} : (Color){240, 130, 90, 255});
}

// Painter's-order entity record: draw farther entities (smaller x+y) first.
typedef struct ARDrawable {
    float depth;
    int kind;    // 0 pillar, 1 enemy, 2 pet, 3 player
    int index;
} ARDrawable;

static inline void ar_draw_hitbox(ARClient* client, float x, float y,
        float radius, Color color) {
    Vector2 p = ar_iso(client, x, y, 0.0f);
    DrawEllipseLines((int)p.x, (int)p.y, radius * AR_ISO_W,
        radius * AR_ISO_W * 0.5f, color);
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

    // Smooth camera follow, then derive the screen offset from the camera.
    float follow = dt * 6.0f;
    if (follow > 1.0f) follow = 1.0f;
    client->cam_x += (env->px - client->cam_x) * follow;
    client->cam_y += (env->py - client->cam_y) * follow;
    client->off_x = 0.5f * (float)GetScreenWidth()
        - (client->cam_x - client->cam_y) * AR_ISO_W;
    client->off_y = 0.42f * (float)GetScreenHeight()
        - (client->cam_x + client->cam_y) * AR_ISO_H;

    BeginDrawing();
    ClearBackground((Color){16, 14, 22, 255});

    // Ground: checker diamonds + grid lines across the whole arena.
    float half = 0.5f * env->cfg.arena_size;
    Color tile_a = (Color){38, 34, 44, 255};
    Color tile_b = (Color){33, 30, 40, 255};
    for (int gy = 0; gy < (int)env->cfg.arena_size; gy += 2) {
        for (int gx = 0; gx < (int)env->cfg.arena_size; gx += 2) {
            float wx = -half + (float)gx + 1.0f;
            float wy = -half + (float)gy + 1.0f;
            ar_draw_diamond(client, wx, wy, 1.0f,
                ((gx / 2 + gy / 2) & 1) ? tile_a : tile_b);
        }
    }

    // Arena border walls (top edges only read cleanly in 2:1 iso).
    Color wall_top = (Color){64, 50, 40, 255};
    Color wall_side = (Color){44, 34, 28, 255};
    float wall_h = AR_WALL_HEIGHT * AR_ISO_W * 0.9f;
    {
        Vector2 n1 = ar_iso(client, -half, -half, 0.0f);
        Vector2 n2 = ar_iso(client, half, -half, 0.0f);
        Vector2 n1t = ar_iso(client, -half, -half, 2.0f);
        Vector2 n2t = ar_iso(client, half, -half, 2.0f);
        DrawTriangle(n1t, n2t, n2, wall_top);
        DrawTriangle(n1t, n2, n1, wall_top);
        Vector2 w1 = ar_iso(client, -half, half, 0.0f);
        Vector2 w1t = ar_iso(client, -half, half, 2.0f);
        DrawTriangle(n1t, n1, w1, wall_side);
        DrawTriangle(n1t, w1, w1t, wall_side);
        Vector2 e2 = ar_iso(client, half, half, 0.0f);
        Vector2 e2t = ar_iso(client, half, half, 2.0f);
        DrawTriangle(n2t, e2t, e2, wall_top);
        DrawTriangle(n2t, e2, n2, wall_top);
        (void)wall_h;
    }

    // Painter's order over pillars, enemies, pets, player.
    ARDrawable draws[AR_MAX_OBSTACLES + AR_MAX_ENEMIES + AR_MAX_PETS + 1];
    int draw_count = 0;
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
            case 1:
                ar_draw_shadow(client, env->enemies.x[d->index],
                    env->enemies.y[d->index], env->enemies.radius[d->index]);
                ar_draw_enemy(client, env, d->index);
                break;
            case 2:
                ar_draw_shadow(client, env->pets.x[d->index],
                    env->pets.y[d->index], env->cfg.pet_radius);
                ar_draw_pet(client, env, d->index);
                break;
            default:
                ar_draw_shadow(client, env->px, env->py, env->cfg.player_radius);
                ar_draw_player(client, env);
                break;
        }
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
                env->cfg.pet_radius, SKYBLUE);
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
