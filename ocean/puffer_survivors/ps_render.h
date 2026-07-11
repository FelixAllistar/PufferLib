#pragma once

#include "ps_systems.h"
#include "raylib.h"

#ifndef PS_RENDER_QUALITY_DEFAULT
#define PS_RENDER_QUALITY_DEFAULT 1
#endif

typedef struct {
    Texture2D sprites;
    int loaded;
    int player_visual_init;
    float player_visual_x;
    float player_visual_y;
    float player_visual_angle;
    int player_visual_flip;
    int action_debug_init;
    int last_move_action;
    int action_switches;
    float action_switch_rate;
    double action_window_time;
    Rectangle sprite_src[16];
    int render_w;
    int render_h;
    float render_scale;
    int render_quality;
} PSClient;

static inline void ps_remove_chroma_key(Image* image) {
    ImageFormat(image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    Color* pixels = (Color*)image->data;
    int count = image->width * image->height;
    for (int i = 0; i < count; i++) {
        Color* p = &pixels[i];
        int magenta_like = p->r > 150 && p->b > 150 && p->g < 170 && abs((int)p->r - (int)p->b) < 90;
        int hot_pink_edge = p->r > 200 && p->b > 140 && p->g < 150;
        if (magenta_like || hot_pink_edge) {
            p->r = 0;
            p->g = 0;
            p->b = 0;
            p->a = 0;
        }
    }
}

static inline Vector2 ps_screen(PufferSurvivors* env, float x, float y, float scale, int w, int h) {
    return (Vector2){w * 0.5f + (x - env->px) * scale, h * 0.5f + (y - env->py) * scale};
}

static inline PSClient* ps_client(PufferSurvivors* env) {
    return (PSClient*)env->client;
}

static inline Color ps_alpha(Color c, unsigned char a) {
    c.a = a;
    return c;
}

static inline float ps_sprite_facing_degrees(float vx, float vy, int flip_x, float max_degrees) {
    if (fabsf(vx) + fabsf(vy) < 0.01f) return 0.0f;
    float angle = atan2f(vy, fmaxf(fabsf(vx), 0.03f)) * 57.2958f;
    angle = ps_clampf(angle, -max_degrees, max_degrees);
    return flip_x ? -angle : angle;
}

static inline float ps_angle_lerp(float current, float target, float t) {
    float delta = target - current;
    while (delta > 180.0f) delta -= 360.0f;
    while (delta < -180.0f) delta += 360.0f;
    return current + delta * t;
}

static inline void ps_draw_sprite_ex(PufferSurvivors* env, int sprite, float x, float y, float radius, float visual_scale, float rotation, int flip_x, Color fallback) {
    PSClient* client = ps_client(env);
    int w = client != NULL && client->render_w > 0 ? client->render_w : GetScreenWidth();
    int h = client != NULL && client->render_h > 0 ? client->render_h : GetScreenHeight();
    float scale = client != NULL && client->render_scale > 0.0f
        ? client->render_scale
        : fminf((float)w, (float)h) / env->cfg.arena_size;
    Vector2 p = ps_screen(env, x, y, scale, w, h);
    float size = radius * visual_scale * scale;
    if (p.x + size < 0.0f || p.x - size > (float)w || p.y + size < 0.0f || p.y - size > (float)h) return;

    if (client != NULL && client->loaded && sprite >= 0 && sprite < 16) {
        Rectangle src = client->sprite_src[sprite];
        if (flip_x) src.width = -src.width;
        Rectangle dst = {p.x, p.y, size, size};
        DrawTexturePro(client->sprites, src, dst, (Vector2){size * 0.5f, size * 0.5f}, rotation, WHITE);
    } else {
        DrawCircleV(p, fmaxf(2.0f, radius * scale), fallback);
    }
}

static inline void ps_draw_sprite_screen(PufferSurvivors* env, int sprite, float x, float y, float size, int flip_x, Color fallback) {
    PSClient* client = ps_client(env);
    if (client != NULL && client->loaded && sprite >= 0 && sprite < 16) {
        Rectangle src = client->sprite_src[sprite];
        if (flip_x) src.width = -src.width;
        Rectangle dst = {x, y, size, size};
        DrawTexturePro(client->sprites, src, dst, (Vector2){size * 0.5f, size * 0.5f}, 0.0f, WHITE);
    } else {
        DrawCircleV((Vector2){x, y}, size * 0.5f, fallback);
    }
}

static inline void ps_draw_sprite(PufferSurvivors* env, int sprite, float x, float y, float radius, Color fallback) {
    ps_draw_sprite_ex(env, sprite, x, y, radius, 2.6f, 0.0f, 0, fallback);
}

static inline void ps_draw_bar(float x, float y, float w, float h, float pct, Color fill, Color back) {
    pct = ps_clampf(pct, 0.0f, 1.0f);
    DrawRectangle((int)x, (int)y, (int)w, (int)h, back);
    DrawRectangle((int)x, (int)y, (int)(w * pct), (int)h, fill);
    DrawRectangleLines((int)x, (int)y, (int)w, (int)h, ps_alpha(RAYWHITE, 120));
}

static inline Color ps_area_color(int type, unsigned char alpha) {
    switch (type) {
        case PS_WEAPON_WHIRLPOOL: return (Color){61, 199, 255, alpha};
        case PS_WEAPON_INK: return (Color){66, 38, 115, alpha};
        case PS_WEAPON_SONAR: return (Color){143, 247, 255, alpha};
        default: return (Color){92, 175, 255, alpha};
    }
}

static inline int ps_enemy_sprite(uint8_t type) {
    if (type & PS_ENEMY_BOSS_FLAG) return PS_SPRITE_BOSS;
    if (type & PS_ENEMY_ELITE_FLAG) return PS_SPRITE_ELITE;
    switch (type & PS_ENEMY_KIND_MASK) {
        case 1: return PS_SPRITE_JELLY;
        case 2: return PS_SPRITE_URCHIN;
        case 3: return PS_SPRITE_EEL;
        default: return PS_SPRITE_JELLY;
    }
}

static inline void ps_draw_water(PufferSurvivors* env, float scale, int w, int h) {
    ClearBackground((Color){3, 18, 30, 255});
    float ox = fmodf(env->px * scale * 0.23f, 96.0f);
    float oy = fmodf(env->py * scale * 0.18f, 78.0f);
    for (int i = -2; i < 14; i++) {
        int y = (int)(i * 78.0f - oy);
        DrawLineEx((Vector2){0, (float)y}, (Vector2){(float)w, (float)y + 32.0f}, 2.0f, (Color){12, 65, 79, 80});
    }
    for (int i = -2; i < 14; i++) {
        int x = (int)(i * 96.0f - ox);
        DrawLineEx((Vector2){(float)x, 0}, (Vector2){(float)x + 42.0f, (float)h}, 1.5f, (Color){7, 46, 62, 55});
    }
    DrawCircleGradient(w / 2, h / 2, (float)w * 0.65f, (Color){18, 82, 98, 70}, (Color){0, 0, 0, 0});
}

static inline void ps_draw_area(PufferSurvivors* env, int i, float scale, int w, int h) {
    Vector2 p = ps_screen(env, env->areas.x[i], env->areas.y[i], scale, w, h);
    float r = env->areas.radius[i] * scale;
    if (p.x + r < 0.0f || p.x - r > (float)w || p.y + r < 0.0f || p.y - r > (float)h) return;

    int type = env->areas.type[i];
    float pulse = 0.55f + 0.45f * sinf((float)env->tick * 0.18f + (float)i);
    Color c = ps_area_color(type, 70);
    if (type == PS_WEAPON_INK) {
        DrawCircleV(p, r, (Color){45, 26, 82, 92});
        DrawCircleLines((int)p.x, (int)p.y, r * (0.75f + 0.12f * pulse), (Color){132, 94, 202, 170});
        DrawCircleLines((int)p.x, (int)p.y, r, (Color){35, 18, 56, 210});
    } else if (type == PS_WEAPON_WHIRLPOOL) {
        DrawCircleV(p, r, (Color){15, 95, 133, 55});
        DrawRing(p, r * 0.38f, r * 0.46f, 20.0f + env->tick * 7.0f, 300.0f + env->tick * 7.0f, 36, (Color){98, 226, 255, 150});
        DrawRing(p, r * 0.68f, r * 0.75f, 210.0f - env->tick * 5.0f, 520.0f - env->tick * 5.0f, 40, (Color){45, 171, 255, 120});
    } else if (type == PS_WEAPON_SONAR) {
        DrawRing(p, r * (0.55f + 0.18f * pulse), r * (0.58f + 0.18f * pulse), 0, 360, 64, (Color){165, 252, 255, 185});
        DrawRing(p, r * 0.90f, r * 0.93f, 0, 360, 64, (Color){72, 198, 255, 100});
    } else {
        DrawCircleV(p, r, c);
        DrawCircleLines((int)p.x, (int)p.y, r, ps_alpha(c, 150));
    }
}

static inline void ps_draw_projectile(PufferSurvivors* env, int i, float scale, int w, int h) {
    Vector2 p = ps_screen(env, env->projectiles.x[i], env->projectiles.y[i], scale, w, h);
    float r = env->projectiles.radius[i] * scale;
    if (p.x + r < 0.0f || p.x - r > (float)w || p.y + r < 0.0f || p.y - r > (float)h) return;

    PSClient* client = ps_client(env);
    if (client == NULL || client->render_quality >= 2) {
        Vector2 tail = {p.x - env->projectiles.vx[i] * scale * 2.5f, p.y - env->projectiles.vy[i] * scale * 2.5f};
        DrawLineEx(tail, p, fmaxf(2.0f, r * 0.35f), (Color){115, 231, 255, 105});
        DrawCircleV(p, r * 1.65f, (Color){73, 208, 255, 72});
    }
    ps_draw_sprite_ex(env, PS_SPRITE_BUBBLE, env->projectiles.x[i], env->projectiles.y[i], env->projectiles.radius[i], 4.6f, 0.0f, 0, (Color){167, 232, 255, 255});
}

static inline void ps_draw_weapon_orbits(PufferSurvivors* env, float scale, int w, int h) {
    int level = env->weapon_level[PS_WEAPON_ORBIT];
    if (level <= 0) return;
    int count = 1 + level / 2;
    float orbit_r = (2.25f + 0.18f * (float)level) * (1.0f + 0.5f * env->area_bonus);
    float hit_r = (PS_WEAPON_DEFS[PS_WEAPON_ORBIT].base_radius + PS_WEAPON_DEFS[PS_WEAPON_ORBIT].radius_per_level * (float)level) * (1.0f + env->area_bonus);
    for (int i = 0; i < count; i++) {
        float a = env->orbit_phase + 2.0f * PI * ((float)i / (float)count);
        float x = env->px + cosf(a) * orbit_r;
        float y = env->py + sinf(a) * orbit_r;
        Vector2 p = ps_screen(env, x, y, scale, w, h);
        DrawCircleV(p, hit_r * scale * 1.15f, (Color){255, 224, 90, 70});
        ps_draw_sprite_ex(env, PS_SPRITE_ORB, x, y, hit_r, 3.2f, 0.0f, 0, GOLD);
    }
}

static inline void ps_draw_enemy(PufferSurvivors* env, int i, float scale, int w, int h) {
    uint8_t type = env->enemies.type[i];
    int boss = (type & PS_ENEMY_BOSS_FLAG) != 0;
    int elite = (type & PS_ENEMY_ELITE_FLAG) != 0;
    int sprite = ps_enemy_sprite(type);
    float visual_scale = boss ? 3.2f : (elite ? 3.1f : 2.7f);
    int flip_x = env->enemies.vx[i] < -0.001f || (fabsf(env->enemies.vx[i]) < 0.001f && env->enemies.x[i] > env->px);
    ps_draw_sprite_ex(env, sprite, env->enemies.x[i], env->enemies.y[i], env->enemies.radius[i], visual_scale, 0.0f, flip_x, elite ? ORANGE : PINK);

    Vector2 p = ps_screen(env, env->enemies.x[i], env->enemies.y[i], scale, w, h);
    if (elite || boss || env->enemies.hp[i] < env->enemies.max_hp[i]) {
        float bw = env->enemies.radius[i] * scale * (boss ? 3.2f : 2.3f);
        float bh = boss ? 6.0f : 4.0f;
        float pct = env->enemies.max_hp[i] > 0.0f ? env->enemies.hp[i] / env->enemies.max_hp[i] : 0.0f;
        ps_draw_bar(p.x - bw * 0.5f, p.y - env->enemies.radius[i] * scale * 2.0f, bw, bh, pct, boss ? RED : ORANGE, (Color){13, 13, 18, 180});
    }
}

static inline void ps_draw_hud(PufferSurvivors* env) {
    DrawRectangle(12, 12, 372, 170, (Color){0, 0, 0, 168});
    DrawRectangleLines(12, 12, 372, 170, (Color){117, 230, 244, 90});
    DrawText(TextFormat("Puffer Survivors  HP %.0f/%.0f", env->hp, env->max_hp), 24, 24, 18, RAYWHITE);
    ps_draw_bar(24, 49, 200, 12, env->max_hp > 0.0f ? env->hp / env->max_hp : 0.0f, (Color){95, 230, 130, 255}, (Color){58, 18, 28, 210});
    ps_draw_bar(24, 68, 200, 9, ps_xp_threshold(env) > 0.0f ? env->xp / ps_xp_threshold(env) : 0.0f, (Color){70, 210, 255, 255}, (Color){8, 39, 58, 210});
    DrawText(TextFormat("LV %d  Wave %d  Kills %.0f", env->level, ps_wave_index(env) + 1, env->episode_kills), 24, 88, 17, SKYBLUE);
    DrawText(TextFormat("Score %.1f  Damage %.1f", env->episode_score, env->episode_damage_dealt), 24, 111, 17, RAYWHITE);

    const char* labels[PS_WEAPON_COUNT] = {"Bub", "Whirl", "Orb", "Oil", "Sonar"};
    for (int i = 0; i < PS_WEAPON_COUNT; i++) {
        float pct = env->weapon_level[i] > 0 ? 1.0f - ps_clampf(env->weapon_cd[i] / ps_weapon_cooldown_total(env, i), 0.0f, 1.0f) : 0.0f;
        float x = 24.0f + (float)i * 68.0f;
        DrawText(TextFormat("%s %d", labels[i], env->weapon_level[i]), (int)x, 135, 12, env->weapon_level[i] > 0 ? RAYWHITE : GRAY);
        ps_draw_bar(x, 151.0f, 52.0f, 7.0f, pct, (Color){255, 222, 89, 255}, (Color){24, 38, 48, 210});
    }

}

static inline const char* ps_move_action_name(int action) {
    switch (action) {
        case 0: return "idle";
        case 1: return "up";
        case 2: return "down";
        case 3: return "left";
        case 4: return "right";
        case 5: return "up-left";
        case 6: return "up-right";
        case 7: return "down-left";
        case 8: return "down-right";
        default: return "?";
    }
}

static inline void ps_update_action_debug(PufferSurvivors* env) {
    PSClient* client = ps_client(env);
    if (client == NULL) return;

    int action = (int)env->actions[0];
    action = action < 0 ? 0 : (action > 8 ? 8 : action);
    double now = GetTime();
    if (!client->action_debug_init) {
        client->action_debug_init = 1;
        client->last_move_action = action;
        client->action_window_time = now;
        client->action_switches = 0;
        client->action_switch_rate = 0.0f;
        return;
    }

    if (action != client->last_move_action) {
        client->action_switches++;
        client->last_move_action = action;
    }

    double elapsed = now - client->action_window_time;
    if (elapsed >= 1.0) {
        client->action_switch_rate = (float)((double)client->action_switches / elapsed);
        client->action_switches = 0;
        client->action_window_time = now;
    }
}

static inline void ps_draw_action_debug(PufferSurvivors* env, int sw) {
    PSClient* client = ps_client(env);
    if (client == NULL || !client->action_debug_init) return;

    int action = (int)env->actions[0];
    action = action < 0 ? 0 : (action > 8 ? 8 : action);
    int upgrade = (int)env->actions[1];
    int x = sw - 270;
    int y = 14;
    DrawRectangle(x - 10, y - 8, 256, 92, (Color){0, 0, 0, 150});
    DrawRectangleLines(x - 10, y - 8, 256, 92, (Color){117, 230, 244, 80});
    DrawText(TextFormat("move %d %s", action, ps_move_action_name(action)), x, y, 16, RAYWHITE);
    DrawText(TextFormat("upgrade %d", upgrade), x, y + 20, 16, SKYBLUE);
    DrawText(TextFormat("switches/s %.1f", client->action_switch_rate), x, y + 40, 16, GOLD);
    DrawText(TextFormat("vel %.2f %.2f", env->pvx, env->pvy), x, y + 60, 16, (Color){210, 230, 235, 255});
}

static inline Color ps_upgrade_color(int upgrade) {
    switch (upgrade) {
        case PS_UPGRADE_BUBBLE: return (Color){91, 222, 255, 255};
        case PS_UPGRADE_WHIRLPOOL: return (Color){61, 199, 255, 255};
        case PS_UPGRADE_ORBIT: return (Color){255, 222, 89, 255};
        case PS_UPGRADE_INK: return (Color){179, 123, 255, 255};
        case PS_UPGRADE_SONAR: return (Color){170, 255, 255, 255};
        case PS_UPGRADE_HEALTH: return (Color){117, 255, 156, 255};
        case PS_UPGRADE_SPEED: return (Color){255, 238, 125, 255};
        default: return RAYWHITE;
    }
}

static inline int ps_upgrade_sprite(int upgrade) {
    switch (upgrade) {
        case PS_UPGRADE_BUBBLE: return PS_SPRITE_BUBBLE;
        case PS_UPGRADE_WHIRLPOOL: return PS_SPRITE_WHIRL;
        case PS_UPGRADE_ORBIT: return PS_SPRITE_ORB;
        case PS_UPGRADE_INK: return PS_SPRITE_INK;
        case PS_UPGRADE_SONAR: return PS_SPRITE_SONAR;
        case PS_UPGRADE_HEALTH: return PS_SPRITE_HEALTH;
        case PS_UPGRADE_MAGNET: return PS_SPRITE_XP;
        case PS_UPGRADE_AREA: return PS_SPRITE_WHIRL;
        default: return PS_SPRITE_PLAYER;
    }
}

static inline void ps_draw_upgrade_cards(PufferSurvivors* env, int sw, int sh) {
    if (!env->pending_upgrade) return;

    DrawRectangle(0, 0, sw, sh, (Color){0, 8, 14, 150});

    const char* title = "LEVEL UP";
    int title_size = 44;
    DrawText(title, sw / 2 - MeasureText(title, title_size) / 2, sh / 2 - 204, title_size, GOLD);
    const char* hint = "Choose an upgrade";
    DrawText(hint, sw / 2 - MeasureText(hint, 22) / 2, sh / 2 - 154, 22, RAYWHITE);

    float card_w = fminf(250.0f, (float)sw * 0.28f);
    float card_h = 190.0f;
    float gap = 22.0f;
    float start_x = ((float)sw - card_w * 3.0f - gap * 2.0f) * 0.5f;
    float y = (float)sh * 0.5f - card_h * 0.42f;

    for (int i = 0; i < PS_UPGRADE_SLOTS; i++) {
        int upgrade = env->offered[i];
        float x = start_x + (card_w + gap) * (float)i;
        Color accent = ps_upgrade_color(upgrade);

        DrawRectangle((int)(x + 4.0f), (int)(y + 8.0f), (int)card_w, (int)card_h, (Color){0, 0, 0, 112});
        DrawRectangle((int)x, (int)y, (int)card_w, (int)card_h, (Color){8, 31, 46, 238});
        DrawRectangleLinesEx((Rectangle){x, y, card_w, card_h}, 2.0f, accent);
        DrawRectangle((int)x, (int)y, (int)card_w, 42, ps_alpha(accent, 45));

        DrawText(TextFormat("%d", i + 1), (int)(x + 16.0f), (int)(y + 13.0f), 22, accent);
        const char* name = ps_upgrade_name(upgrade);
        DrawText(name, (int)(x + 52.0f), (int)(y + 15.0f), 20, RAYWHITE);

        float icon_x = x + card_w * 0.5f;
        float icon_y = y + 84.0f;
        int sprite = ps_upgrade_sprite(upgrade);
        DrawCircleV((Vector2){icon_x, icon_y}, 29.0f, ps_alpha(accent, 58));
        DrawCircleLines((int)icon_x, (int)icon_y, 29.0f, ps_alpha(accent, 160));
        ps_draw_sprite_screen(env, sprite, icon_x, icon_y, 58.0f, 0, accent);

        const char* desc = ps_upgrade_description(upgrade);
        DrawText(desc, (int)(x + 18.0f), (int)(y + 124.0f), 14, (Color){204, 230, 235, 255});
        DrawText(TextFormat("Press %d", i + 1), (int)(x + 18.0f), (int)(y + card_h - 32.0f), 16, accent);
    }
}

static inline void c_render(PufferSurvivors* env) {
    const int w = 960;
    const int h = 960;
    if (!IsWindowReady()) {
        InitWindow(w, h, "Puffer Survivors");
        SetTargetFPS(60);
    }

    if (env->client == NULL) {
        env->client = calloc(1, sizeof(PSClient));
        const char* path = "resources/puffer_survivors/sprites.png";
        if (FileExists(path)) {
            Image image = LoadImage(path);
            ps_remove_chroma_key(&image);
            PSClient* client = ps_client(env);
            client->sprites = LoadTextureFromImage(image);
            UnloadImage(image);
            client->loaded = client->sprites.id != 0;
            if (client->loaded) {
                float cell_w = (float)client->sprites.width / 4.0f;
                float cell_h = (float)client->sprites.height / 4.0f;
                float inset_x = cell_w * 0.07f;
                float inset_y = cell_h * 0.07f;
                for (int sprite = 0; sprite < 16; sprite++) {
                    client->sprite_src[sprite] = (Rectangle){
                        (float)(sprite % 4) * cell_w + inset_x,
                        (float)(sprite / 4) * cell_h + inset_y,
                        cell_w - 2.0f * inset_x,
                        cell_h - 2.0f * inset_y,
                    };
                }
            }
        }
        ps_client(env)->render_quality = PS_RENDER_QUALITY_DEFAULT;
    }

    if (IsKeyDown(KEY_ESCAPE)) exit(0);
    if (IsKeyPressed(KEY_H)) env->show_hitboxes = !env->show_hitboxes;
    if (IsKeyPressed(KEY_Q) && ps_client(env) != NULL) {
        ps_client(env)->render_quality = (ps_client(env)->render_quality + 1) % 3;
    }

    BeginDrawing();
    ps_update_action_debug(env);

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    float scale = fminf((float)sw, (float)sh) / env->cfg.arena_size;
    PSClient* render_client = ps_client(env);
    if (render_client != NULL) {
        render_client->render_w = sw;
        render_client->render_h = sh;
        render_client->render_scale = scale;
    }
    ps_draw_water(env, scale, sw, sh);

    for (int i = 0; i < env->cfg.obstacle_count; i++) {
        Vector2 p = ps_screen(env, env->obstacles.x[i], env->obstacles.y[i], scale, sw, sh);
        float shadow_r = env->obstacles.radius[i] * scale * 1.35f;
        DrawCircleV((Vector2){p.x + 5.0f, p.y + 7.0f}, shadow_r, (Color){0, 0, 0, 70});
        int sprite = PS_SPRITE_CORAL + (env->obstacles.type[i] % 3);
        ps_draw_sprite_ex(env, sprite, env->obstacles.x[i], env->obstacles.y[i], env->obstacles.radius[i], 2.85f, 0.0f, 0, (Color){73, 91, 99, 255});
        if (env->show_hitboxes) DrawCircleLines((int)p.x, (int)p.y, env->obstacles.radius[i] * scale, (Color){255, 222, 89, 190});
    }

    for (int k = 0; k < env->drop_count; k++) {
        int i = env->drops.dense[k];
        Vector2 p = ps_screen(env, env->drops.x[i], env->drops.y[i], scale, sw, sh);
        float pulse = 0.75f + 0.25f * sinf((float)env->tick * 0.14f + (float)i);
        int high_fx = render_client == NULL || render_client->render_quality >= 2;
        if (env->drops.type[i] == 1) {
            if (high_fx) DrawCircleV(p, 18.0f * pulse, (Color){64, 255, 147, 62});
            ps_draw_sprite_ex(env, PS_SPRITE_HEALTH, env->drops.x[i], env->drops.y[i], 0.34f, 3.0f, 0.0f, 0, RED);
        } else {
            if (high_fx) DrawCircleV(p, 12.0f * pulse, (Color){70, 210, 255, 52});
            ps_draw_sprite_ex(env, PS_SPRITE_XP, env->drops.x[i], env->drops.y[i], 0.24f, 2.8f, 0.0f, 0, SKYBLUE);
        }
        if (env->show_hitboxes) DrawCircleLines((int)p.x, (int)p.y, env->cfg.pickup_radius * scale, (Color){64, 220, 255, 120});
    }

    for (int k = 0; k < env->area_count; k++) {
        ps_draw_area(env, env->areas.dense[k], scale, sw, sh);
    }

    ps_draw_weapon_orbits(env, scale, sw, sh);

    for (int k = 0; k < env->projectile_count; k++) {
        int i = env->projectiles.dense[k];
        ps_draw_projectile(env, i, scale, sw, sh);
        if (env->show_hitboxes) {
            Vector2 p = ps_screen(env, env->projectiles.x[i], env->projectiles.y[i], scale, sw, sh);
            DrawCircleLines((int)p.x, (int)p.y, env->projectiles.radius[i] * scale, (Color){176, 244, 255, 180});
        }
    }

    for (int k = 0; k < env->enemy_count; k++) {
        int i = env->enemies.dense[k];
        ps_draw_enemy(env, i, scale, sw, sh);
        if (env->show_hitboxes) {
            Vector2 p = ps_screen(env, env->enemies.x[i], env->enemies.y[i], scale, sw, sh);
            DrawCircleLines((int)p.x, (int)p.y, env->enemies.radius[i] * scale, (Color){255, 87, 87, 210});
        }
    }

    PSClient* client = ps_client(env);
    if (client != NULL) {
        float dx = env->px - client->player_visual_x;
        float dy = env->py - client->player_visual_y;
        if (!client->player_visual_init || dx * dx + dy * dy > 25.0f) {
            client->player_visual_init = 1;
            client->player_visual_x = env->px;
            client->player_visual_y = env->py;
            client->player_visual_angle = ps_sprite_facing_degrees(env->pvx, env->pvy, env->player_facing_left, 88.0f);
            client->player_visual_flip = env->player_facing_left;
        } else {
            client->player_visual_x += (env->px - client->player_visual_x) * 0.38f;
            client->player_visual_y += (env->py - client->player_visual_y) * 0.38f;
            float target_angle = ps_sprite_facing_degrees(env->pvx, env->pvy, env->player_facing_left, 88.0f);
            client->player_visual_angle = ps_angle_lerp(client->player_visual_angle, target_angle, 0.22f);
            if (fabsf(env->pvx) > 0.015f) {
                client->player_visual_flip = env->player_facing_left;
            }
        }
    }

    float draw_px = client != NULL && client->player_visual_init ? client->player_visual_x : env->px;
    float draw_py = client != NULL && client->player_visual_init ? client->player_visual_y : env->py;
    float draw_angle = client != NULL && client->player_visual_init
        ? client->player_visual_angle
        : ps_sprite_facing_degrees(env->pvx, env->pvy, env->player_facing_left, 88.0f);
    int draw_flip = client != NULL && client->player_visual_init ? client->player_visual_flip : env->player_facing_left;

    Vector2 player = ps_screen(env, draw_px, draw_py, scale, sw, sh);
    if (env->invuln_timer > 0) {
        DrawCircleV(player, 34.0f + 5.0f * sinf((float)env->tick * 0.35f), (Color){255, 86, 86, 70});
    }
    ps_draw_sprite_ex(env, PS_SPRITE_PLAYER, draw_px, draw_py, 0.66f, 3.25f, draw_angle, draw_flip, (Color){86, 216, 255, 255});
    if (env->show_hitboxes) {
        DrawCircleLines((int)player.x, (int)player.y, 0.42f * scale, (Color){105, 255, 168, 230});
        DrawCircleLines((int)player.x, (int)player.y, env->cfg.magnet_radius * (1.0f + env->magnet_bonus) * scale, (Color){64, 220, 255, 80});
    }

    ps_draw_hud(env);
    ps_draw_action_debug(env, sw);
    ps_draw_upgrade_cards(env, sw, sh);
    if (env->show_hitboxes) DrawText("HITBOXES", sw - 132, 20, 20, GOLD);
    if (render_client != NULL) DrawText(TextFormat("Q: FX %d", render_client->render_quality), sw - 132, 44, 16, (Color){180, 210, 220, 180});

    EndDrawing();
}

static inline void c_close(PufferSurvivors* env) {
    if (env->client != NULL) {
        PSClient* client = ps_client(env);
        if (client->loaded) UnloadTexture(client->sprites);
        free(client);
        env->client = NULL;
    }
    if (IsWindowReady()) CloseWindow();
}
