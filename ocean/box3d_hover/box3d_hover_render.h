#pragma once

#include "box3d_hover.h"
#include "raylib.h"

static inline void c_render(Box3DHover* env) {
    if (!IsWindowReady()) {
        InitWindow(960, 960, "Box3D Hover");
        SetTargetFPS(60);
    }

    int a = 0;
    float scale = 44.0f;
    float cx = 480.0f;
    float cy = 500.0f;
    b3Pos p = b3Body_GetPosition(env->balls[a]);
    float lx = p.x - env->center_x[a];
    float lz = p.z - env->center_z[a];

    BeginDrawing();
    ClearBackground((Color){8, 16, 22, 255});
    DrawText("Box3D Hover", 24, 20, 24, RAYWHITE);
    DrawText("WASD/Arrows move, Shift boost, Space brake, R reset", 24, 52, 18, GRAY);

    Rectangle arena = {
        cx - env->arena_half * scale,
        cy - env->arena_half * scale,
        env->arena_half * 2.0f * scale,
        env->arena_half * 2.0f * scale
    };
    DrawRectangleLinesEx(arena, 2.0f, (Color){95, 130, 150, 255});

    for (int o = 0; o < B3H_OBSTACLES; o++) {
        float ox = B3H_OBSTACLE_POS[o][0];
        float oz = B3H_OBSTACLE_POS[o][1];
        DrawCircleV((Vector2){cx + ox * scale, cy + oz * scale}, env->obstacle_radius * scale, (Color){80, 90, 105, 255});
        DrawCircleLines((int)(cx + ox * scale), (int)(cy + oz * scale), env->obstacle_radius * scale, (Color){140, 150, 165, 255});
    }

    DrawCircleV((Vector2){cx + env->target_x[a] * scale, cy + env->target_z[a] * scale}, env->target_radius * scale, (Color){60, 210, 120, 255});
    DrawCircleV((Vector2){cx + lx * scale, cy + lz * scale}, env->ball_radius * scale, (Color){110, 210, 255, 255});
    DrawText(TextFormat("reward %.3f  captures %d  age %d", env->rewards[0], env->captures[0], env->age[0]), 24, 84, 18, RAYWHITE);
    EndDrawing();
}
