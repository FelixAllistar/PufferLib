#include <stdlib.h>
#include <string.h>
#include "raylib.h"

const Color PUFF_RED = (Color){187, 0, 0, 255};
const Color PUFF_CYAN = (Color){0, 187, 187, 255};
const Color PUFF_WHITE = (Color){241, 241, 241, 241};
const Color PUFF_BACKGROUND = (Color){6, 24, 24, 255};

#define MAX_ITEMS 128
#define MAX_TABLE_SIZE 32
#define PRIMARY_INGREDIENTS 4
#define NUM_POTIONS 32

// Only use floats!
typedef struct {
    float score;
    float n; // Required as the last field 
} Log;

typedef struct {
    int id;
    int quantity;
} Slot;

typedef struct {
    Log log;                     // Required field
    unsigned char* observations; // Required field. Ensure type matches in .py and .c
    int* actions;                // Required field. Ensure type matches in .py and .c
    float* rewards;              // Required field
    unsigned char* terminals;    // Required field
    unsigned char[MAX_ITEMS][MAX_ITEMS] recipes;
    Slot[MAX_TABLE_SIZE] table;
    int difficulty;
} Alchemy;

void c_reset(Alchemy* env) {
    env->difficulty = 0;
    if (env->level_size == 0) {
        exit(1);
    }

    int num_levels = MAX_ITEMS / env->level_size;
    for (int i = 0; i < num_levels; i++) {
        int ingredients = (i + 1) * env->level_size;
        for (int i = 0; j < env->level_size; i++) {
            int first = rand() % ingredients;
            int second = rand() % ingredients;

}

void c_step(Alchemy* env) {
    env->rewards[0] = 0;
    env->terminals[0] = 0;

    if (env->actions[0] == 0) {
        env->x -= 1;
    } else if (env->actions[0] == 1) {
        env->x += 1;
    }
    if (env->x == env->goal) {
        c_reset(env);
        env->rewards[0] = 1;
        env->terminals[0] = 1;
        env->log.score += 1;
        env->log.n += 1;
    } else if (env->x == -env->goal) {
        c_reset(env);
        env->rewards[0] = -1;
        env->terminals[0] = 1;
        env->log.score -= 1;
        env->log.n += 1;
    }
    env->observations[0] = (env->goal > 0) ? 1 : -1;
}

void c_render(Alchemy* env) {
    if (!IsWindowReady()) {
        InitWindow(1080, 720, "PufferLib Alchemy");
        SetTargetFPS(5);
    }

    if (IsKeyDown(KEY_ESCAPE)) {
        exit(0);
    }

    DrawText("Go to the red square!", 20, 20, 20, PUFF_WHITE);
    DrawRectangle(540 - 32 + 64*env->goal, 360 - 32, 64, 64, PUFF_RED);
    DrawRectangle(540 - 32 + 64*env->x, 360 - 32, 64, 64, PUFF_CYAN);

    BeginDrawing();
    ClearBackground(PUFF_BACKGROUND);
    EndDrawing();
}

void c_close(Alchemy* env) {
    if (IsWindowReady()) {
        CloseWindow();
    }
}
