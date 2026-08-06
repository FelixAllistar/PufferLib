#pragma once

#include "ps_systems.h"
#include "raylib.h"
#include <string.h>

#ifndef PS_RENDER_QUALITY_DEFAULT
#define PS_RENDER_QUALITY_DEFAULT 2
#endif

typedef struct {
    Texture2D sprites;
    int loaded;
    Texture2D moving_anchor;
    Texture2D moving_submarine;
    int moving_anchor_loaded;
    int moving_submarine_loaded;
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
#ifdef PS_FAST_RENDER
    Texture2D fast_ink;
    int fast_ink_loaded;
    Texture2D fast_water_caustics;
    Texture2D fast_water_silhouettes;
    Texture2D fast_water_props;
    Texture2D fast_obstacle_debris;
    int fast_water_caustics_loaded;
    int fast_water_silhouettes_loaded;
    int fast_water_props_loaded;
    int fast_obstacle_debris_loaded;
    float fast_frame_ms;
    float fast_update_ms;
    float fast_render_ms;
    int fast_steps;
    float fast_previous_px;
    float fast_previous_py;
    float fast_render_alpha;
    int fast_interp_init;
    int fast_upgrade_selection;
    float fast_hit_time;
    float fast_hit_x;
    float fast_hit_y;
    int fast_last_invuln_timer;
#endif
} PSClient;

static inline Texture2D ps_load_project_texture(const char* primary,
        const char* fallback) {
    if (FileExists(primary)) return LoadTexture(primary);
    if (FileExists(fallback)) return LoadTexture(fallback);
    return (Texture2D){0};
}

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
    float camera_x = env->px;
    float camera_y = env->py;
#ifdef PS_FAST_RENDER
    PSClient* client = (PSClient*)env->client;
    if (client != NULL && client->fast_interp_init) {
        float alpha = ps_clampf(client->fast_render_alpha, 0.0f, 1.0f);
        camera_x = client->fast_previous_px + (env->px - client->fast_previous_px) * alpha;
        camera_y = client->fast_previous_py + (env->py - client->fast_previous_py) * alpha;
    }
#endif
    return (Vector2){w * 0.5f + (x - camera_x) * scale, h * 0.5f + (y - camera_y) * scale};
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

static inline void ps_draw_sprite_ex_tinted(PufferSurvivors* env, int sprite, float x, float y, float radius, float visual_scale, float rotation, int flip_x, Color fallback, Color tint) {
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
        DrawTexturePro(client->sprites, src, dst, (Vector2){size * 0.5f, size * 0.5f}, rotation, tint);
    } else {
        DrawCircleV(p, fmaxf(2.0f, radius * scale), fallback);
    }
}

static inline void ps_draw_sprite_ex(PufferSurvivors* env, int sprite, float x, float y, float radius, float visual_scale, float rotation, int flip_x, Color fallback) {
    ps_draw_sprite_ex_tinted(env, sprite, x, y, radius, visual_scale, rotation, flip_x, fallback, WHITE);
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

#ifdef PS_FAST_RENDER
static inline float ps_fast_smoothstep(float edge0, float edge1, float x) {
    float t = ps_clampf((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Build the oil texture once on the CPU. This avoids the atlas ink artwork's
// multi-lobed silhouette while retaining a dark rim, purple shading, and a
// small highlight. It is drawn as a normal textured quad at runtime.
static inline Image ps_make_fast_ink_image(void) {
    const int size = 128;
    const float cx = 63.5f;
    const float cy = 65.0f;
    const float rx = 56.0f;
    const float ry = 49.0f;

    Image image = GenImageColor(size, size, BLANK);
    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    Color* pixels = (Color*)image.data;

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float nx = ((float)x + 0.5f - cx) / rx;
            float ny = ((float)y + 0.5f - cy) / ry;
            float distance = sqrtf(nx * nx + ny * ny);
            float angle = atan2f(ny, nx);
            float wobble = 1.0f
                + 0.035f * sinf(angle * 3.0f + 0.4f)
                + 0.025f * sinf(angle * 5.0f - 1.1f);
            float depth = wobble - distance;
            float alpha = ps_fast_smoothstep(0.0f, 0.055f, depth);
            Color* pixel = &pixels[y * size + x];

            if (alpha <= 0.0f) {
                *pixel = BLANK;
                continue;
            }

            float top_light = ps_clampf(0.55f - 0.32f * ny - 0.22f * nx, 0.0f, 1.0f);
            float lower_shadow = ps_clampf((ny + 0.15f) * 0.5f, 0.0f, 1.0f);
            // Keep the oil closer to the scene's muted, translucent palette
            // instead of the saturated opaque purple from the atlas art.
            float body_r = 63.0f + 28.0f * top_light - 8.0f * lower_shadow;
            float body_g = 37.0f + 18.0f * top_light - 4.0f * lower_shadow;
            float body_b = 102.0f + 52.0f * top_light - 10.0f * lower_shadow;
            float body_mix = ps_fast_smoothstep(0.015f, 0.14f, depth);
            float r = 30.0f + (body_r - 30.0f) * body_mix;
            float g = 22.0f + (body_g - 22.0f) * body_mix;
            float b = 50.0f + (body_b - 50.0f) * body_mix;

            float hx = (nx + 0.27f) / 0.26f;
            float hy = (ny + 0.29f) / 0.17f;
            float highlight = 0.42f * expf(-0.5f * (hx * hx + hy * hy)) * body_mix;
            r += (150.0f - r) * highlight;
            g += (112.0f - g) * highlight;
            b += (190.0f - b) * highlight;
            float opacity = 0.60f;

            *pixel = (Color){
                (unsigned char)ps_clampf(r, 0.0f, 255.0f),
                (unsigned char)ps_clampf(g, 0.0f, 255.0f),
                (unsigned char)ps_clampf(b, 0.0f, 255.0f),
                (unsigned char)ps_clampf(alpha * opacity * 255.0f, 0.0f, 255.0f),
            };
        }
    }
    return image;
}

static inline void ps_draw_fast_ink(PufferSurvivors* env, Vector2 p, float r, float pulse) {
    PSClient* client = ps_client(env);
    if (client != NULL && client->fast_ink_loaded) {
        float size = r * (1.96f + 0.10f * pulse);
        Rectangle src = {0.0f, 0.0f, (float)client->fast_ink.width, (float)client->fast_ink.height};
        Rectangle dst = {p.x, p.y, size, size};
        DrawTexturePro(client->fast_ink, src, dst,
            (Vector2){size * 0.5f, size * 0.5f}, 0.0f, WHITE);
    } else {
        // Keep a clean fallback if texture creation fails.
        DrawCircleV(p, r, (Color){45, 26, 82, 190});
        DrawCircleLines((int)p.x, (int)p.y, r, (Color){25, 12, 45, 230});
    }
}

static inline void ps_fast_update_hit_effect(PufferSurvivors* env) {
    PSClient* client = ps_client(env);
    if (client == NULL) return;

    float frame_dt = GetFrameTime();
    if (frame_dt <= 0.0f || frame_dt > 0.10f) frame_dt = 1.0f / 60.0f;
    if (env->invuln_timer > client->fast_last_invuln_timer) {
        client->fast_hit_time = 0.22f;
        client->fast_hit_x = env->px;
        client->fast_hit_y = env->py;
    }
    client->fast_last_invuln_timer = env->invuln_timer;
    if (client->fast_hit_time > 0.0f) {
        client->fast_hit_time = fmaxf(0.0f, client->fast_hit_time - frame_dt);
    }
}

static inline void ps_draw_fast_hit_effect(PufferSurvivors* env, float scale, int w, int h) {
    PSClient* client = ps_client(env);
    if (client == NULL || client->fast_hit_time <= 0.0f) return;

    const float lifetime = 0.22f;
    float remaining = ps_clampf(client->fast_hit_time / lifetime, 0.0f, 1.0f);
    float progress = 1.0f - remaining;
    Vector2 origin = ps_screen(env, client->fast_hit_x, client->fast_hit_y, scale, w, h);
    float burst = 7.0f + 28.0f * progress;
    for (int i = 0; i < 7; i++) {
        float angle = (float)i * (2.0f * PI / 7.0f) + 0.18f * sinf((float)i * 3.1f);
        float distance = burst * (0.78f + 0.18f * sinf((float)i * 2.4f + 0.6f));
        Vector2 p = {
            origin.x + cosf(angle) * distance,
            origin.y + sinf(angle) * distance,
        };
        float radius = 2.0f + 1.4f * (0.5f + 0.5f * sinf((float)i * 1.7f));
        unsigned char alpha = (unsigned char)(remaining * 190.0f);
        Color bubble = (i % 3 == 0)
            ? (Color){255, 142, 145, alpha}
            : (Color){170, 238, 248, alpha};
        DrawCircleV(p, radius * 0.55f, ps_alpha(bubble, (unsigned char)(alpha * 0.35f)));
        DrawCircleLines((int)p.x, (int)p.y, radius, bubble);
    }
}

static inline Texture2D ps_load_fast_background_texture(const char* primary, const char* fallback) {
    if (FileExists(primary)) return LoadTexture(primary);
    if (FileExists(fallback)) return LoadTexture(fallback);
    return (Texture2D){0};
}

static inline void ps_draw_fast_obstacle_debris(PufferSurvivors* env, int variant,
    float x, float y, float radius, float visual_scale, float rotation, Color fallback) {
    PSClient* client = ps_client(env);
    int w = client != NULL && client->render_w > 0 ? client->render_w : GetScreenWidth();
    int h = client != NULL && client->render_h > 0 ? client->render_h : GetScreenHeight();
    float scale = client != NULL && client->render_scale > 0.0f
        ? client->render_scale
        : fminf((float)w, (float)h) / env->cfg.arena_size;
    Vector2 p = ps_screen(env, x, y, scale, w, h);
    float size = radius * visual_scale * scale;
    if (p.x + size < 0.0f || p.x - size > (float)w || p.y + size < 0.0f || p.y - size > (float)h) return;

    if (client != NULL && client->fast_obstacle_debris_loaded) {
        int cell = variant & 3;
        float cell_w = (float)client->fast_obstacle_debris.width * 0.5f;
        float cell_h = (float)client->fast_obstacle_debris.height * 0.5f;
        Rectangle src = {
            (float)(cell % 2) * cell_w,
            (float)(cell / 2) * cell_h,
            cell_w,
            cell_h,
        };
        Rectangle dst = {p.x, p.y, size, size};
        DrawTexturePro(client->fast_obstacle_debris, src, dst,
            (Vector2){size * 0.5f, size * 0.5f}, rotation, WHITE);
    } else {
        ps_draw_sprite_ex(env, PS_SPRITE_CORAL + (variant % 3),
            x, y, radius, visual_scale, rotation, 0, fallback);
    }
}

static inline void ps_draw_fast_background(PufferSurvivors* env, float scale, int w, int h) {
    PSClient* client = ps_client(env);
    ClearBackground((Color){8, 58, 77, 255});

    float now = (float)GetTime();
    float viewport = fmaxf((float)w, (float)h);
    float camera_phase_x = env->px * scale * 0.0024f;
    float camera_phase_y = env->py * scale * 0.0018f;

    // The large overscan keeps the viewport covered while the layers drift,
    // so this background never exposes a hard edge or a repeated seam.
    if (client != NULL && client->fast_water_caustics_loaded) {
        float size = viewport * 1.48f;
        float x = ((float)w - size) * 0.5f
            + 24.0f * sinf(now * 0.065f + camera_phase_x)
            + 9.0f * sinf(now * 0.031f + camera_phase_y);
        float y = ((float)h - size) * 0.5f
            + 18.0f * sinf(now * 0.052f + camera_phase_y + 1.7f)
            + 8.0f * sinf(now * 0.027f + camera_phase_x);
        Rectangle src = {0.0f, 0.0f,
            (float)client->fast_water_caustics.width,
            (float)client->fast_water_caustics.height};
        Rectangle dst = {x, y, size, size};
        DrawTexturePro(client->fast_water_caustics, src, dst,
            (Vector2){0.0f, 0.0f}, 0.0f, (Color){255, 255, 255, 150});

        // Lift the caustic highlights without flattening the texture into a
        // solid wash. This is a single cached-texture blend pass, not a
        // per-pixel CPU effect.
        BeginBlendMode(BLEND_ADDITIVE);
        DrawTexturePro(client->fast_water_caustics, src, dst,
            (Vector2){0.0f, 0.0f}, 0.0f, (Color){112, 220, 224, 24});
        EndBlendMode();

        // A slower, broader pass creates depth without introducing another
        // line pattern or per-pixel work.
        size = viewport * 1.92f;
        x = ((float)w - size) * 0.5f
            - 32.0f * sinf(now * 0.022f + camera_phase_x * 0.7f + 0.8f);
        y = ((float)h - size) * 0.5f
            + 26.0f * sinf(now * 0.019f + camera_phase_y * 0.6f + 2.1f);
        src = (Rectangle){0.0f, 0.0f,
            (float)client->fast_water_caustics.width,
            (float)client->fast_water_caustics.height};
        dst = (Rectangle){x, y, size, size};
        DrawTexturePro(client->fast_water_caustics, src, dst,
            (Vector2){0.0f, 0.0f}, 0.0f, (Color){115, 208, 216, 36});
    }

    if (client != NULL && client->fast_water_props_loaded) {
        float size = viewport * 1.52f;
        float x = ((float)w - size) * 0.5f
            + 68.0f * sinf(now * 0.016f + camera_phase_x * 0.35f + 2.4f);
        float y = ((float)h - size) * 0.5f
            + 54.0f * sinf(now * 0.013f + camera_phase_y * 0.30f + 0.6f);
        Rectangle src = {0.0f, 0.0f,
            (float)client->fast_water_props.width,
            (float)client->fast_water_props.height};
        Rectangle dst = {x, y, size, size};
        DrawTexturePro(client->fast_water_props, src, dst,
            (Vector2){0.0f, 0.0f}, 0.0f, (Color){220, 225, 204, 132});
    }

    if (client != NULL && client->fast_water_silhouettes_loaded) {
        float size = viewport * 1.70f;
        float x = ((float)w - size) * 0.5f
            + 52.0f * sinf(now * 0.024f + camera_phase_x * 0.55f + 0.4f);
        float y = ((float)h - size) * 0.5f
            + 38.0f * sinf(now * 0.018f + camera_phase_y * 0.45f + 1.2f);
        Rectangle src = {0.0f, 0.0f,
            (float)client->fast_water_silhouettes.width,
            (float)client->fast_water_silhouettes.height};
        Rectangle dst = {x, y, size, size};
        DrawTexturePro(client->fast_water_silhouettes, src, dst,
            (Vector2){0.0f, 0.0f}, 0.0f, (Color){32, 118, 122, 145});
    }

    // Foreground particulate is intentionally sparse; the generated texture
    // carries the visual weight now instead of dozens of large primitives.
    float drift_x = env->px * scale * 0.025f + now * 3.0f;
    float drift_y = env->py * scale * 0.018f - now * 1.5f;
    for (int i = 0; i < 24; i++) {
        float x = fmodf((float)(i * 173 % 997) + drift_x, (float)w + 48.0f) - 24.0f;
        float y = fmodf((float)(i * 277 % 991) + drift_y, (float)h + 48.0f) - 24.0f;
        if (x < -24.0f) x += (float)w + 48.0f;
        if (y < -24.0f) y += (float)h + 48.0f;
        float r = 1.0f + (float)(i % 3) * 0.6f;
        DrawCircleV((Vector2){x, y}, r,
            (Color){138, 226, 224, (unsigned char)(18 + 6 * (i % 3))});
    }
}
#endif

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
#ifdef PS_FAST_RENDER
    ps_draw_fast_background(env, scale, w, h);
#else
    ClearBackground((Color){2, 15, 27, 255});

    // A handful of broad depth bands is cheaper than a full-screen CPU shader
    // and still gives the water a much less flat, grid-like appearance.
    const int bands = 10;
    for (int i = 0; i < bands; i++) {
        float t = (float)i / (float)(bands - 1);
        Color c = {
            (unsigned char)(5.0f + 2.0f * t),
            (unsigned char)(31.0f - 16.0f * t),
            (unsigned char)(48.0f - 17.0f * t),
            255
        };
        int y0 = i * h / bands;
        int y1 = (i + 1) * h / bands + 1;
        DrawRectangle(0, y0, w, y1 - y0, c);
    }

    float drift_x = env->px * scale * 0.11f + (float)env->tick * 0.08f;
    float drift_y = env->py * scale * 0.08f - (float)env->tick * 0.035f;
    for (int i = 0; i < 36; i++) {
        float x = fmodf((float)(i * 173 % 997) + drift_x, (float)w + 80.0f) - 40.0f;
        float y = fmodf((float)(i * 277 % 991) + drift_y, (float)h + 80.0f) - 40.0f;
        if (x < -40.0f) x += (float)w + 80.0f;
        if (y < -40.0f) y += (float)h + 80.0f;
        float r = 1.2f + (float)(i % 4) * 0.7f;
        DrawCircleV((Vector2){x, y}, r, (Color){120, 220, 225, (unsigned char)(24 + 7 * (i % 4))});
    }

    // Sparse animated caustics: a few GPU-rasterized lines, no render texture
    // readback and no per-pixel CPU work.
    float phase = (float)env->tick * 0.035f;
    for (int i = -2; i < 10; i++) {
        float y = (float)i * 112.0f + fmodf(phase * 7.0f - env->py * scale * 0.12f, 112.0f);
        float bend = 22.0f * sinf(phase + (float)i * 0.83f);
        DrawLineEx((Vector2){-40.0f, y + bend}, (Vector2){(float)w + 40.0f, y - bend},
            2.0f, (Color){76, 179, 190, 32});
    }

    DrawCircleGradient(w / 2, h / 2, (float)w * 0.72f,
        (Color){24, 104, 116, 62}, (Color){0, 5, 12, 8});
#endif
}

static inline void ps_draw_area(PufferSurvivors* env, int i, float scale, int w, int h) {
    Vector2 p = ps_screen(env, env->areas.x[i], env->areas.y[i], scale, w, h);
    float r = env->areas.radius[i] * scale;
    if (p.x + r < 0.0f || p.x - r > (float)w || p.y + r < 0.0f || p.y - r > (float)h) return;

    int type = env->areas.type[i];
    float pulse = 0.55f + 0.45f * sinf((float)env->tick * 0.18f + (float)i);
    Color c = ps_area_color(type, 70);
    if (type == PS_WEAPON_INK) {
#ifdef PS_FAST_RENDER
        ps_draw_fast_ink(env, p, r, pulse);
#else
        DrawCircleV(p, r, (Color){45, 26, 82, 92});
        DrawCircleLines((int)p.x, (int)p.y, r * (0.75f + 0.12f * pulse), (Color){132, 94, 202, 170});
        DrawCircleLines((int)p.x, (int)p.y, r, (Color){35, 18, 56, 210});
#endif
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
        DrawCircleLines((int)p.x, (int)p.y, r * 1.25f, (Color){115, 231, 255, 80});
    }
    float travel_angle = atan2f(env->projectiles.vy[i], env->projectiles.vx[i]) * 57.2958f;
    // The atlas bubble points left (large bubble at the leading edge), so its
    // zero-rotation forward vector is 180 degrees.
    ps_draw_sprite_ex(env, PS_SPRITE_BUBBLE, env->projectiles.x[i], env->projectiles.y[i],
        env->projectiles.radius[i], 4.6f, travel_angle - 180.0f, 0,
        (Color){167, 232, 255, 255});
}

static inline void ps_draw_weapon_orbits(PufferSurvivors* env, float scale, int w, int h) {
    int level = env->weapon_level[PS_WEAPON_ORBIT];
    if (level <= 0) return;
    int count = 1 + level / 2;
    float orbit_r = (env->cfg.weapon_orbit_distance
        + env->cfg.weapon_orbit_distance_per_level * (float)level)
        * (1.0f + 0.5f * env->area_bonus);
    float hit_r = ps_geometry_weapon_radius(&env->cfg, PS_WEAPON_ORBIT, level)
        * (1.0f + env->area_bonus);
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
    int ari_k = (type & PS_ENEMY_ARI_K_FLAG) != 0;
    int elite = (type & PS_ENEMY_ELITE_FLAG) != 0;
    int kind = type & PS_ENEMY_KIND_MASK;
    int sprite = ps_enemy_sprite(type);
    float visual_scale = boss ? 3.2f : (elite ? 3.1f
        : (kind == 1 ? 3.85f : (kind == 2 ? 3.5f : 3.35f)));
    int flip_x = env->enemies.vx[i] < -0.001f || (fabsf(env->enemies.vx[i]) < 0.001f && env->enemies.x[i] > env->px);
    Vector2 p = ps_screen(env, env->enemies.x[i], env->enemies.y[i], scale, w, h);
    if (ari_k) {
        float width = env->enemies.half_width[i] * scale * 2.0f;
        float height = env->enemies.half_height[i] * scale * 2.0f;
        DrawRectangle((int)(p.x - width * 0.5f), (int)(p.y - height * 0.5f),
            (int)width, (int)height, (Color){0, 0, 0, 255});
        DrawRectangleLines((int)(p.x - width * 0.5f), (int)(p.y - height * 0.5f),
            (int)width, (int)height, (Color){95, 95, 110, 255});
        int font_size = (int)ps_clampf(fminf(width, height) * 0.22f, 18.0f, 36.0f);
        int text_width = MeasureText("Ari K.", font_size);
        DrawText("Ari K.", (int)p.x - text_width / 2,
            (int)p.y - font_size / 2, font_size, RAYWHITE);
    } else {
#ifdef PS_FAST_RENDER
        if (kind == 1) {
            // Draw the same sprite slightly enlarged underneath so the edge
            // follows the jellyfish silhouette instead of becoming a circle.
            ps_draw_sprite_ex_tinted(env, sprite, env->enemies.x[i], env->enemies.y[i],
                env->enemies.radius[i], visual_scale * 1.10f, 0.0f, flip_x,
                (Color){4, 42, 57, 255}, (Color){4, 42, 57, 255});
            ps_draw_sprite_ex_tinted(env, sprite, env->enemies.x[i], env->enemies.y[i],
                env->enemies.radius[i], visual_scale, 0.0f, flip_x,
                PINK, (Color){205, 242, 240, 255});
        } else {
            ps_draw_sprite_ex(env, sprite, env->enemies.x[i], env->enemies.y[i],
                env->enemies.radius[i], visual_scale, 0.0f, flip_x, elite ? ORANGE : PINK);
        }
#else
        ps_draw_sprite_ex(env, sprite, env->enemies.x[i], env->enemies.y[i], env->enemies.radius[i], visual_scale, 0.0f, flip_x, elite ? ORANGE : PINK);
#endif
    }

#ifndef PS_FAST_RENDER
    if (elite || boss || env->enemies.hp[i] < env->enemies.max_hp[i]) {
        float bw = ari_k ? env->enemies.half_width[i] * scale * 2.0f
            : env->enemies.radius[i] * scale * (boss ? 3.2f : 2.3f);
        float bh = boss ? 6.0f : 4.0f;
        float pct = env->enemies.max_hp[i] > 0.0f ? env->enemies.hp[i] / env->enemies.max_hp[i] : 0.0f;
        float bar_y = p.y - scale * (ari_k ? env->enemies.half_height[i] + 0.35f
            : env->enemies.radius[i] * 2.0f);
        ps_draw_bar(p.x - bw * 0.5f, bar_y, bw, bh, pct, boss ? RED : ORANGE, (Color){13, 13, 18, 180});
    }
#endif
}

static inline void ps_draw_moving_obstacle(PufferSurvivors* env, int i,
        float scale, int w, int h) {
    Vector2 p = ps_screen(env, env->moving_obstacles.x[i],
        env->moving_obstacles.y[i], scale, w, h);
    float width = env->moving_obstacles.half_width[i] * scale * 2.0f;
    float height = env->moving_obstacles.half_height[i] * scale * 2.0f;
    float extent = fmaxf(width, height);
    if (p.x + extent < 0.0f || p.x - extent > (float)w
            || p.y + extent < 0.0f || p.y - extent > (float)h) return;

    DrawEllipse((int)(p.x + 4.0f), (int)(p.y + 6.0f),
        width * 0.48f, height * 0.30f, (Color){0, 42, 52, 90});
    PSClient* client = ps_client(env);
    Texture2D texture = env->moving_obstacles.type[i] == PS_MOVING_OBSTACLE_ANCHOR
        ? (client != NULL ? client->moving_anchor : (Texture2D){0})
        : (client != NULL ? client->moving_submarine : (Texture2D){0});
    int loaded = env->moving_obstacles.type[i] == PS_MOVING_OBSTACLE_ANCHOR
        ? (client != NULL && client->moving_anchor_loaded)
        : (client != NULL && client->moving_submarine_loaded);
    float rotation = env->moving_obstacles.type[i] == PS_MOVING_OBSTACLE_ANCHOR
        ? 7.0f * sinf((float)env->tick * 0.035f + (float)i)
        : (env->moving_obstacles.vx[i] < 0.0f ? 180.0f : 0.0f);
    if (loaded) {
        Rectangle src = {0.0f, 0.0f, (float)texture.width, (float)texture.height};
        Rectangle dst = {p.x, p.y, width, height};
        DrawTexturePro(texture, src, dst,
            (Vector2){width * 0.5f, height * 0.5f}, rotation, WHITE);
    } else {
        Color fill = env->moving_obstacles.type[i] == PS_MOVING_OBSTACLE_ANCHOR
            ? (Color){115, 128, 140, 255} : (Color){190, 105, 48, 255};
        DrawRectangle((int)(p.x - width * 0.5f), (int)(p.y - height * 0.5f),
            (int)width, (int)height, fill);
        DrawRectangleLines((int)(p.x - width * 0.5f), (int)(p.y - height * 0.5f),
            (int)width, (int)height, (Color){20, 32, 42, 255});
    }
    if (env->show_hitboxes) {
        DrawRectangleLines((int)(p.x - width * 0.5f), (int)(p.y - height * 0.5f),
            (int)width, (int)height, (Color){255, 224, 90, 220});
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

    int action = (int)env->agents[0].actions[0];
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

    int action = (int)env->agents[0].actions[0];
    action = action < 0 ? 0 : (action > 8 ? 8 : action);
    int upgrade = (int)env->agents[0].actions[1];
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

static inline void ps_draw_centered_multiline(const char* text, float center_x,
    int y, int font_size, Color color) {
    const char* newline = strchr(text, '\n');
    if (newline == NULL) {
        int width = MeasureText(text, font_size);
        DrawText(text, (int)(center_x - width * 0.5f), y, font_size, color);
        return;
    }

    int first_length = (int)(newline - text);
    const char* first = TextSubtext(text, 0, first_length);
    int first_width = MeasureText(first, font_size);
    DrawText(first, (int)(center_x - first_width * 0.5f), y, font_size, color);

    const char* second = TextSubtext(text, first_length + 1, (int)strlen(newline + 1));
    int second_width = MeasureText(second, font_size);
    DrawText(second, (int)(center_x - second_width * 0.5f), y + font_size + 4, font_size, color);
}

static inline void ps_draw_upgrade_cards(PufferSurvivors* env, int sw, int sh) {
    if (!env->pending_upgrade) return;

    DrawRectangle(0, 0, sw, sh, (Color){0, 8, 14, 150});

    const char* title = "LEVEL UP";
    int title_size = 44;
    DrawText(title, sw / 2 - MeasureText(title, title_size) / 2, sh / 2 - 204, title_size, GOLD);
#ifdef PS_FAST_RENDER
    const char* hint = "A/D or arrows: select    Space: confirm    1/2/3: choose";
#else
    const char* hint = "Choose an upgrade";
#endif
    DrawText(hint, sw / 2 - MeasureText(hint, 22) / 2, sh / 2 - 154, 22, RAYWHITE);

    float card_w = fminf(250.0f, (float)sw * 0.28f);
    float card_h = 190.0f;
    float gap = 22.0f;
    float start_x = ((float)sw - card_w * 3.0f - gap * 2.0f) * 0.5f;
    float y = (float)sh * 0.5f - card_h * 0.42f;
#ifdef PS_FAST_RENDER
    PSClient* client = ps_client(env);
    int selected_slot = client != NULL ? client->fast_upgrade_selection : 0;
    selected_slot = selected_slot < 0 ? 0 : (selected_slot >= PS_UPGRADE_SLOTS ? PS_UPGRADE_SLOTS - 1 : selected_slot);
#endif

    for (int i = 0; i < PS_UPGRADE_SLOTS; i++) {
        int upgrade = env->offered[i];
        float x = start_x + (card_w + gap) * (float)i;
        Color accent = ps_upgrade_color(upgrade);
#ifdef PS_FAST_RENDER
        int selected = i == selected_slot;
        Color border = selected ? GOLD : (Color){117, 230, 244, 180};
        Color card_fill = selected ? (Color){25, 57, 72, 248} : (Color){8, 31, 46, 238};
#else
        Color border = accent;
        Color card_fill = (Color){8, 31, 46, 238};
#endif

        DrawRectangle((int)(x + 4.0f), (int)(y + 8.0f), (int)card_w, (int)card_h, (Color){0, 0, 0, 112});
        DrawRectangle((int)x, (int)y, (int)card_w, (int)card_h, card_fill);
#ifdef PS_FAST_RENDER
        DrawRectangleLinesEx((Rectangle){x, y, card_w, card_h}, selected ? 5.0f : 2.0f, border);
        DrawRectangle((int)x, (int)y, (int)card_w, 42, ps_alpha(selected ? GOLD : accent, selected ? 70 : 45));
#else
        DrawRectangleLinesEx((Rectangle){x, y, card_w, card_h}, 2.0f, border);
        DrawRectangle((int)x, (int)y, (int)card_w, 42, ps_alpha(accent, 45));
#endif

        DrawText(TextFormat("%d", i + 1), (int)(x + 16.0f), (int)(y + 13.0f), 22, accent);
        const char* name = ps_upgrade_name(upgrade);
        int name_width = MeasureText(name, 20);
        DrawText(name, (int)(x + (card_w - (float)name_width) * 0.5f), (int)(y + 15.0f), 20, RAYWHITE);

        float icon_x = x + card_w * 0.5f;
        float icon_y = y + 84.0f;
        int sprite = ps_upgrade_sprite(upgrade);
        DrawCircleV((Vector2){icon_x, icon_y}, 29.0f, ps_alpha(accent, 58));
        DrawCircleLines((int)icon_x, (int)icon_y, 29.0f, ps_alpha(accent, 160));
        ps_draw_sprite_screen(env, sprite, icon_x, icon_y, 58.0f, 0, accent);

        const char* desc = ps_upgrade_description(upgrade);
        ps_draw_centered_multiline(desc, x + card_w * 0.5f, (int)(y + 124.0f),
            14, (Color){204, 230, 235, 255});
#ifdef PS_FAST_RENDER
        const char* footer = selected ? "SPACE to confirm" : "A/D to select";
        const int footer_size = 13;
        const int footer_width = MeasureText(footer, footer_size);
        DrawText(footer,
            (int)(x + (card_w - (float)footer_width) * 0.5f),
            (int)(y + card_h - 32.0f), footer_size, selected ? GOLD : accent);
#else
        DrawText(TextFormat("Press %d", i + 1), (int)(x + 18.0f), (int)(y + card_h - 32.0f), 16, accent);
#endif
    }
}

#ifdef PS_FAST_RENDER
static inline void ps_draw_fast_metrics(PufferSurvivors* env, int sw, int sh) {
    PSClient* client = ps_client(env);
    if (client == NULL) return;

    int oil_count = 0;
    for (int k = 0; k < env->area_count; k++) {
        int i = env->areas.dense[k];
        if (env->areas.type[i] == PS_WEAPON_INK) oil_count++;
    }

    int panel_w = sw - 24;
    if (panel_w > 620) panel_w = 620;
    if (panel_w < 100) panel_w = 100;
    int y = sh - 34;
    DrawRectangle(12, y, panel_w, 24, (Color){0, 0, 0, 168});
    DrawRectangleLines(12, y, panel_w, 24, (Color){117, 230, 244, 80});
    DrawText(TextFormat("FPS %d  frame %.1fms  sim %.2fms x%d  draw %.2fms  oil %d",
        GetFPS(), client->fast_frame_ms, client->fast_update_ms,
        client->fast_steps, client->fast_render_ms, oil_count),
        20, y + 4, 14, (Color){190, 225, 230, 235});
}
#endif

static inline void c_render(PufferSurvivors* env) {
    const int w = 960;
    const int h = 960;
    if (!IsWindowReady()) {
        InitWindow(w, h, "Puffer Survivors");
        SetTargetFPS(60);
    }

    if (env->client == NULL) {
        env->client = calloc(1, sizeof(PSClient));
        PSClient* client = ps_client(env);
        client->moving_anchor = ps_load_project_texture(
            "resources/puffer_survivors/moving_anchor.png",
            "../../resources/puffer_survivors/moving_anchor.png");
        client->moving_submarine = ps_load_project_texture(
            "resources/puffer_survivors/moving_submarine.png",
            "../../resources/puffer_survivors/moving_submarine.png");
        client->moving_anchor_loaded = client->moving_anchor.id != 0;
        client->moving_submarine_loaded = client->moving_submarine.id != 0;
        if (client->moving_anchor_loaded)
            SetTextureFilter(client->moving_anchor, TEXTURE_FILTER_POINT);
        if (client->moving_submarine_loaded)
            SetTextureFilter(client->moving_submarine, TEXTURE_FILTER_POINT);
#ifdef PS_FAST_RENDER
        Image fast_ink_image = ps_make_fast_ink_image();
        client->fast_ink = LoadTextureFromImage(fast_ink_image);
        client->fast_ink_loaded = client->fast_ink.id != 0;
        UnloadImage(fast_ink_image);
        if (client->fast_ink_loaded) SetTextureFilter(client->fast_ink, TEXTURE_FILTER_BILINEAR);
        client->fast_water_caustics = ps_load_fast_background_texture(
            "resources/puffer_survivors/fast_water_caustics_bright.png",
            "../../resources/puffer_survivors/fast_water_caustics_bright.png");
        client->fast_water_silhouettes = ps_load_fast_background_texture(
            "resources/puffer_survivors/fast_water_silhouettes_bright.png",
            "../../resources/puffer_survivors/fast_water_silhouettes_bright.png");
        client->fast_water_props = ps_load_fast_background_texture(
            "resources/puffer_survivors/fast_water_props.png",
            "../../resources/puffer_survivors/fast_water_props.png");
        client->fast_obstacle_debris = ps_load_fast_background_texture(
            "resources/puffer_survivors/fast_obstacle_debris.png",
            "../../resources/puffer_survivors/fast_obstacle_debris.png");
        client->fast_water_caustics_loaded = client->fast_water_caustics.id != 0;
        client->fast_water_silhouettes_loaded = client->fast_water_silhouettes.id != 0;
        client->fast_water_props_loaded = client->fast_water_props.id != 0;
        client->fast_obstacle_debris_loaded = client->fast_obstacle_debris.id != 0;
        if (client->fast_water_caustics_loaded) {
            SetTextureFilter(client->fast_water_caustics, TEXTURE_FILTER_BILINEAR);
            SetTextureWrap(client->fast_water_caustics, TEXTURE_WRAP_CLAMP);
        }
        if (client->fast_water_silhouettes_loaded) {
            SetTextureFilter(client->fast_water_silhouettes, TEXTURE_FILTER_POINT);
            SetTextureWrap(client->fast_water_silhouettes, TEXTURE_WRAP_CLAMP);
        }
        if (client->fast_water_props_loaded) {
            SetTextureFilter(client->fast_water_props, TEXTURE_FILTER_POINT);
            SetTextureWrap(client->fast_water_props, TEXTURE_WRAP_CLAMP);
        }
        if (client->fast_obstacle_debris_loaded) {
            SetTextureFilter(client->fast_obstacle_debris, TEXTURE_FILTER_POINT);
            SetTextureWrap(client->fast_obstacle_debris, TEXTURE_WRAP_CLAMP);
        }
#endif
        const char* path = "resources/puffer_survivors/sprites.png";
        if (!FileExists(path)) {
            path = "../../resources/puffer_survivors/sprites.png";
        }
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
#ifdef PS_FAST_RENDER
    double fast_draw_start = GetTime();
    ps_fast_update_hit_effect(env);
#endif
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
#ifdef PS_FAST_RENDER
        float shadow_w = env->obstacles.radius[i] * scale * 1.20f;
        float shadow_h = env->obstacles.radius[i] * scale * 0.68f;
        DrawEllipse((int)(p.x + 4.0f), (int)(p.y + 6.0f),
            shadow_w, shadow_h, (Color){0, 52, 64, 72});
#else
        float shadow_r = env->obstacles.radius[i] * scale * 1.35f;
        DrawCircleV((Vector2){p.x + 5.0f, p.y + 7.0f}, shadow_r, (Color){0, 0, 0, 70});
#endif
        int sprite = PS_SPRITE_CORAL + (env->obstacles.type[i] % 3);
#ifdef PS_FAST_RENDER
        const float obstacle_visual_scale = 3.18f;
#else
        const float obstacle_visual_scale = 2.85f;
#endif
#ifdef PS_FAST_RENDER
        if (render_client != NULL && render_client->fast_obstacle_debris_loaded) {
            float debris_rotation = (float)((i * 37) % 26 - 13);
            ps_draw_fast_obstacle_debris(env, i % 4,
                env->obstacles.x[i], env->obstacles.y[i],
                env->obstacles.radius[i], obstacle_visual_scale,
                debris_rotation, (Color){73, 91, 99, 255});
        } else {
            ps_draw_sprite_ex(env, sprite, env->obstacles.x[i], env->obstacles.y[i],
                env->obstacles.radius[i], obstacle_visual_scale, 0.0f, 0,
                (Color){73, 91, 99, 255});
        }
#else
        ps_draw_sprite_ex(env, sprite, env->obstacles.x[i], env->obstacles.y[i],
            env->obstacles.radius[i], obstacle_visual_scale, 0.0f, 0,
            (Color){73, 91, 99, 255});
#endif
        if (env->show_hitboxes) DrawCircleLines((int)p.x, (int)p.y, env->obstacles.radius[i] * scale, (Color){255, 222, 89, 190});
    }

    for (int k = 0; k < env->moving_obstacle_count; k++) {
        ps_draw_moving_obstacle(env, env->moving_obstacles.dense[k], scale, sw, sh);
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
            // XP is small and cyan, which disappears into the bright water.
            // A dark, restrained badge gives it luminance contrast without
            // adding another bright particle layer.
            float xp_badge = fmaxf(5.0f, 0.24f * 2.8f * scale * 0.62f);
            DrawCircleV((Vector2){p.x + 1.0f, p.y + 1.0f}, xp_badge + 1.0f,
                (Color){3, 18, 25, 180});
            DrawCircleV(p, xp_badge, (Color){8, 35, 45, 235});
            DrawCircleLines((int)p.x, (int)p.y, xp_badge,
                (Color){75, 125, 132, 230});
            ps_draw_sprite_ex(env, PS_SPRITE_XP, env->drops.x[i], env->drops.y[i],
                0.24f, 2.8f, 0.0f, 0, (Color){180, 220, 220, 255});
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
            if (env->enemies.shape[i] == PS_SHAPE_AABB) {
                float width = env->enemies.half_width[i] * scale * 2.0f;
                float height = env->enemies.half_height[i] * scale * 2.0f;
                DrawRectangleLines((int)(p.x - width * 0.5f),
                    (int)(p.y - height * 0.5f), (int)width, (int)height,
                    (Color){255, 87, 87, 210});
            } else {
                DrawCircleLines((int)p.x, (int)p.y,
                    env->enemies.radius[i] * scale, (Color){255, 87, 87, 210});
            }
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
    // Keep the player's art-to-hitbox proportion stable when player_radius is
    // tuned in the shared gameplay config. 0.66 is the current art footprint
    // for the default 0.42 world-space collision radius.
    float player_visual_radius = env->cfg.player_radius * (0.66f / 0.42f);

    Vector2 player = ps_screen(env, draw_px, draw_py, scale, sw, sh);
#ifndef PS_FAST_RENDER
    if (env->invuln_timer > 0) {
        DrawCircleV(player, 34.0f + 5.0f * sinf((float)env->tick * 0.35f), (Color){255, 86, 86, 70});
    }
#endif
#ifdef PS_FAST_RENDER
    ps_draw_fast_hit_effect(env, scale, sw, sh);
    Color player_tint = WHITE;
    if (client != NULL && client->fast_hit_time > 0.0f) {
        float flash = ps_clampf(client->fast_hit_time / 0.13f, 0.0f, 1.0f);
        unsigned char channel = (unsigned char)(150.0f + 105.0f * (1.0f - flash));
        player_tint = (Color){255, channel, channel, 255};
    }
    ps_draw_sprite_ex_tinted(env, PS_SPRITE_PLAYER, draw_px, draw_py, player_visual_radius, 3.25f, draw_angle, draw_flip, (Color){86, 216, 255, 255}, player_tint);
#else
    ps_draw_sprite_ex(env, PS_SPRITE_PLAYER, draw_px, draw_py, player_visual_radius, 3.25f, draw_angle, draw_flip, (Color){86, 216, 255, 255});
#endif
#ifdef PS_FAST_RENDER
    float player_bar_w = ps_clampf(2.4f * scale, 36.0f, 68.0f);
    float player_bar_h = ps_clampf(0.22f * scale, 4.0f, 6.0f);
    float player_sprite_half = player_visual_radius * 3.25f * scale * 0.5f;
    ps_draw_bar(player.x - player_bar_w * 0.5f,
        player.y - player_sprite_half - 11.0f,
        player_bar_w, player_bar_h,
        env->max_hp > 0.0f ? env->hp / env->max_hp : 0.0f,
        (Color){95, 230, 130, 255}, (Color){58, 18, 28, 220});
#endif
    if (env->show_hitboxes) {
        DrawCircleLines((int)player.x, (int)player.y, env->cfg.player_radius * scale, (Color){105, 255, 168, 230});
        DrawCircleLines((int)player.x, (int)player.y, env->cfg.magnet_radius * (1.0f + env->magnet_bonus) * scale, (Color){64, 220, 255, 80});
    }

    ps_draw_hud(env);
    ps_draw_action_debug(env, sw);
    ps_draw_upgrade_cards(env, sw, sh);
    if (env->show_hitboxes) DrawText("HITBOXES", sw - 132, 20, 20, GOLD);
    if (render_client != NULL) DrawText(TextFormat("Q: FX %d", render_client->render_quality), sw - 132, 44, 16, (Color){180, 210, 220, 180});
#ifdef PS_FAST_RENDER
    if (render_client != NULL) {
        render_client->fast_render_ms = (float)((GetTime() - fast_draw_start) * 1000.0);
    }
    ps_draw_fast_metrics(env, sw, sh);
#endif

    EndDrawing();
}

static inline void c_close(PufferSurvivors* env) {
    if (env->client != NULL) {
        PSClient* client = ps_client(env);
#ifdef PS_FAST_RENDER
        if (client->fast_ink_loaded) UnloadTexture(client->fast_ink);
        if (client->fast_water_caustics_loaded) UnloadTexture(client->fast_water_caustics);
        if (client->fast_water_silhouettes_loaded) UnloadTexture(client->fast_water_silhouettes);
        if (client->fast_water_props_loaded) UnloadTexture(client->fast_water_props);
        if (client->fast_obstacle_debris_loaded) UnloadTexture(client->fast_obstacle_debris);
#endif
        if (client->moving_anchor_loaded) UnloadTexture(client->moving_anchor);
        if (client->moving_submarine_loaded) UnloadTexture(client->moving_submarine);
        if (client->loaded) UnloadTexture(client->sprites);
        free(client);
        env->client = NULL;
    }
    if (IsWindowReady()) CloseWindow();
}
