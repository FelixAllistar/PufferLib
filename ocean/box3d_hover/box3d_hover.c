#include "box3d_hover_render.h"

int main(void) {
    float observations[B3H_OBS_SIZE] = {0};
    float actions[B3H_NUM_ATNS] = {0};
    float rewards[1] = {0};
    float terminals[1] = {0};

    Box3DHover env = {
        .observations = observations,
        .actions = actions,
        .rewards = rewards,
        .terminals = terminals,
        .rng = 1,
        .arena_half = 8.0f,
        .arena_stride = 22.0f,
        .max_steps = 1000000000,
        .substeps = 1,
    };
    box3d_hover_init(&env, 1);
    c_reset(&env);

    while (!IsWindowReady() || !WindowShouldClose()) {
        if (IsKeyPressed(KEY_R)) c_reset(&env);
        int up = IsKeyDown(KEY_W) || IsKeyDown(KEY_UP);
        int down = IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN);
        int left = IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT);
        int right = IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT);
        int dir = 0;
        if (right && !left && !up && !down) dir = 1;
        else if (left && !right && !up && !down) dir = 2;
        else if (up && !down && !left && !right) dir = 3;
        else if (down && !up && !left && !right) dir = 4;
        else if (right && up) dir = 5;
        else if (right && down) dir = 6;
        else if (left && up) dir = 7;
        else if (left && down) dir = 8;
        actions[0] = (float)dir;
        actions[1] = IsKeyDown(KEY_SPACE) ? 0.0f : (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ? 2.0f : 1.0f);
        c_step(&env);
        c_render(&env);
    }

    c_close(&env);
    return 0;
}
