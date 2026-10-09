#pragma once
typedef float obs_t;
#include "pufferenv.h"
#include "ml_sim.h"

#define OBS_SIZE ML_OBS_SIZE
#define NUM_ATNS 1
#define ACT_SIZES {64}

struct Log {
    float perf, score, episode_return, episode_length;
    float deaths, timeouts, progress_pixels, pipe_visits, pipe_returns, kills;
    float clear_frames, clears, n;
    float root_episodes, root_clears, gap_episodes, gap_clears;
    float entry_episodes, entry_clears, exit_episodes, exit_clears, route_episodes, route_clears;
};
struct Env {
    Log log;
    Agent agents[1];
    int tag, boundary_reached, num_agents;
    unsigned int rng;
    MLConfig cfg;
    MLState* state;
};

#include "ml_config.h"

ML_HD void ml_log_episode(Log* l, const MLState* s) {
    float clear = (float)(s->status == ML_CLEAR);
    l->perf += clear; l->score += clear; l->clears += clear;
    l->episode_return += s->episode_return; l->episode_length += (float)s->tick;
    l->deaths += (float)(s->status == ML_DEATH); l->timeouts += (float)(s->status == ML_TIMEOUT);
    l->progress_pixels += (float)s->progress / ML_FP;
    l->pipe_visits += (float)s->pipe_visits; l->pipe_returns += (float)s->pipe_returns;
    l->kills += (float)s->kills; l->clear_frames += clear * s->tick; l->n++;
    if (s->task == ML_TASK_FULL) { l->root_episodes++; l->root_clears += clear; }
    if (s->task == ML_TASK_GAP) { l->gap_episodes++; l->gap_clears += clear; }
    if (s->task == ML_TASK_ENTRY) { l->entry_episodes++; l->entry_clears += clear; }
    if (s->task == ML_TASK_EXIT) { l->exit_episodes++; l->exit_clears += clear; }
    if (s->task == ML_TASK_ROUTE) { l->route_episodes++; l->route_clears += clear; }
}
void puf_log(Log* l, Dict* out) {
#define ML_LOG(name) dict_set(out, #name, l->name)
    ML_LOG(perf); ML_LOG(score); ML_LOG(episode_return); ML_LOG(episode_length);
    ML_LOG(deaths); ML_LOG(timeouts); ML_LOG(progress_pixels);
    ML_LOG(pipe_visits); ML_LOG(pipe_returns); ML_LOG(kills); ML_LOG(clears);
    dict_set(out, "clear_frames", l->clears > 0 ? l->clear_frames / l->clears : 0);
    dict_set(out, "practice_fraction", l->n > 0 ? 1 - l->root_episodes / l->n : 0);
#define ML_RATE(name) dict_set(out, #name "_clear_rate", l->name##_episodes > 0 ? l->name##_clears / l->name##_episodes : 0)
    ML_RATE(root); ML_RATE(gap); ML_RATE(entry); ML_RATE(exit); ML_RATE(route);
#undef ML_RATE
#undef ML_LOG
}

#ifndef ML_HEADLESS
static void ml_render_state(const MLState* s, const MLConfig* c) {
    if (!IsWindowReady()) { InitWindow(1024, 600, "Mario Lab synthetic courses"); SetTargetFPS(60); }
    int camera = ml_clamp(s->x / ML_FP - 96, 0, s->widths[s->room] * 16 - 512);
    Color bg = s->room || s->theme == 1 ? (Color){18, 22, 34, 255} : (Color){103, 174, 214, 255};
    BeginDrawing(); ClearBackground(bg);
    for (int y = 0; y < ML_HEIGHT; y++) for (int x = 0; x < s->widths[s->room]; x++) {
        int t = ml_tile(s, s->room, x, y); if (!t) continue;
        int sx = 2 * (x * 16 - camera); if (sx < -32 || sx > 1024) continue;
        Color col = t == ML_PIPE ? (Color){47, 165, 82, 255}
            : t == ML_BRICK ? (Color){211, 138, 85, 255} : (Color){145, 111, 84, 255};
        DrawRectangle(sx, y * 32 + 60, 32, 32, col);
        DrawRectangleLines(sx, y * 32 + 60, 32, 32, (Color){49, 46, 45, 255});
    }
    if (!s->room) {
        int gx = 2 * (s->goal_x / ML_FP - camera);
        DrawLine(gx, 60 + 6 * 32, gx, 60 + 13 * 32, WHITE);
        DrawTriangle((Vector2){(float)gx, 252}, (Vector2){(float)gx, 280}, (Vector2){(float)gx + 30, 252}, YELLOW);
    }
    for (int i = 0; i < ml_enemy_count(s); i++) {
        const MLEnemy* e = ml_enemy_const(s, i);
        if (e->alive && e->room == s->room)
            DrawRectangle(2 * (e->x / ML_FP - camera), 60 + 2 * (e->y / ML_FP - 12), 24, 24, ORANGE);
    }
    DrawRectangle(2 * (s->x / ML_FP - camera), 60 + 2 * (s->y / ML_FP - c->player_height),
        2 * c->player_width, 2 * c->player_height, RED);
    DrawText(TextFormat("Controller v%d | seed %u | course %d | task %d | room %d | frame %d",
        ML_VERSION, s->level_seed, s->course, s->task, s->room, s->tick), 12, 12, 18, WHITE);
    DrawText("Arrows move / Down enters pipe / X jump / Z run / R reset / T theme", 12, 560, 17, WHITE);
    if (s->status) DrawText(s->status == ML_CLEAR ? "CLEAR" : s->status == ML_DEATH ? "DEATH" : "TIMEOUT", 420, 60, 32, YELLOW);
    EndDrawing(); puf_web_vsync();
}
#endif

#ifndef PUFFER_GPU_ENV
void puf_init(Env* e, Dict* kwargs) {
    e->num_agents = 1; e->cfg = ml_config(kwargs);
    e->rng = ml_initial_seed(&e->cfg, (int)e->rng);
    e->state = (MLState*)calloc(1, sizeof(MLState));
    if (!e->state) abort();
}
void puf_reset(Env* e) {
    ml_reset(e->state, &e->cfg, ml_rand(&e->rng));
    if (e->agents[0].observations) ml_observe(e->state, &e->cfg, e->agents[0].observations);
}
void puf_step(Env* e) {
    float reward = ml_step(e->state, &e->cfg, (int)e->agents[0].actions[0]);
    int done = e->state->status != ML_RUNNING;
    if (done) { ml_log_episode(&e->log, e->state); e->boundary_reached = 1; puf_reset(e); }
    else ml_observe(e->state, &e->cfg, e->agents[0].observations);
    e->agents[0].rewards[0] = reward; e->agents[0].terminals[0] = (float)done;
}
void puf_render(Env* e) {
#ifndef ML_HEADLESS
    ml_render_state(e->state, &e->cfg);
#else
    (void)e;
#endif
}
void puf_close(Env* e) { free(e->state); e->state = NULL; }
#endif
