#pragma once

#include "box3d_pusher.h"
#include "raylib.h"

static inline void c_render(Box3DPusher* env) {
    if (!IsWindowReady()) {
        InitWindow(960, 960, "Box3D Pinball Pusher");
        SetTargetFPS(60);
    }

    int a = 0;
    float scale = 44.0f;
    float cx = 480.0f;
    float cy = 500.0f;
    b3Pos pp = b3Body_GetPosition(env->players[a]);
    float plx = pp.x - env->center_x[a];
    float plz = pp.z - env->center_z[a];

    BeginDrawing();
    ClearBackground((Color){7, 12, 18, 255});
    DrawText("Box3D Pinball Pusher", 24, 20, 24, RAYWHITE);
    DrawText("Push pucks into the green goal. WASD/Arrows move, Shift boost, Space brake, R reset", 24, 52, 18, GRAY);

    Rectangle arena = {
        cx - env->arena_half * scale,
        cy - env->arena_half * scale,
        env->arena_half * 2.0f * scale,
        env->arena_half * 2.0f * scale
    };
    DrawRectangleLinesEx(arena, 2.0f, (Color){90, 125, 150, 255});

    for (int b = 0; b < B3P_BUMPERS; b++) {
        float bx = cx + B3P_BUMPER_POS[b][0] * scale;
        float bz = cy + B3P_BUMPER_POS[b][1] * scale;
        DrawCircleV((Vector2){bx, bz}, env->bumper_radius * scale, (Color){145, 75, 185, 255});
        DrawCircleLines((int)bx, (int)bz, env->bumper_radius * scale, (Color){235, 180, 255, 255});
    }

    DrawCircleV((Vector2){cx + env->goal_x[a] * scale, cy + env->goal_z[a] * scale}, env->goal_radius * scale, (Color){45, 150, 82, 110});
    DrawCircleLines((int)(cx + env->goal_x[a] * scale), (int)(cy + env->goal_z[a] * scale), env->goal_radius * scale, (Color){85, 245, 135, 255});

    for (int p = 0; p < B3P_PUCKS; p++) {
        int idx = b3p_puck_index(a, p);
        b3Pos kp = b3Body_GetPosition(env->pucks[idx]);
        float klx = kp.x - env->center_x[a];
        float klz = kp.z - env->center_z[a];
        DrawCircleV((Vector2){cx + klx * scale, cy + klz * scale}, env->puck_radius * scale, (Color){245, 180, 70, 255});
        DrawCircleLines((int)(cx + klx * scale), (int)(cy + klz * scale), env->puck_radius * scale, (Color){255, 238, 180, 255});
    }

    DrawCircleV((Vector2){cx + plx * scale, cy + plz * scale}, env->player_radius * scale, (Color){95, 205, 255, 255});
    DrawCircleLines((int)(cx + plx * scale), (int)(cy + plz * scale), env->player_radius * scale, RAYWHITE);
    DrawText(TextFormat("reward %.3f  goals %d/%d  age %d", env->rewards[0], env->goals[0], env->target_goals, env->age[0]), 24, 84, 18, RAYWHITE);
    EndDrawing();
}
