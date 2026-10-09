#pragma once
// Original, approximate platformer mechanics. No ROM, assets, or game source.
// Integer state is shared by the CPU reference and the CUDA training backend.
#include <stdint.h>
#include <string.h>

#ifdef __CUDACC__
#define ML_HD static __host__ __device__ inline
#else
#define ML_HD static inline
#endif

#define ML_VERSION 5
#define ML_FP 256
#define ML_TILE (16 * ML_FP)
#define ML_WIDTH 192
#define ML_MAX_WIDTH 512
#define ML_HEIGHT 15
#define ML_ROOMS 2
#define ML_ENEMIES 8
#define ML_ENEMY_POOL 64
#define ML_PLAYER_W (12 * ML_FP)
#define ML_PLAYER_H (16 * ML_FP)
#define ML_GRID_W 16
#define ML_GRID_H 13
#define ML_GRID_C 5
#define ML_EGO 24
#define ML_ENT_FEATURES 8
#define ML_OBS_SIZE (ML_EGO + ML_ENEMIES * ML_ENT_FEATURES + ML_GRID_W * ML_GRID_H * ML_GRID_C)
#define ML_A 1
#define ML_B 2
#define ML_DOWN 8
#define ML_LEFT 16
#define ML_RIGHT 32

enum { ML_AIR, ML_SOLID, ML_BRICK, ML_PIPE };
enum { ML_MIXED, ML_FLAT, ML_GAPS, ML_PIPES, ML_STAIRS, ML_WALKERS, ML_UNDERGROUND, ML_COMPOSITE };
enum { ML_RUNNING, ML_CLEAR, ML_DEATH, ML_TIMEOUT };
enum { ML_TASK_FULL, ML_TASK_GAP, ML_TASK_ENTRY, ML_TASK_EXIT, ML_TASK_ROUTE };

typedef struct MLConfig {
    int course, difficulty, length, max_frames, require_pipe;
    uint32_t seed, split;
    int accel, friction, walk_speed, run_speed, jump_speed;
    int gravity_hold, gravity_release, max_fall, hold_frames;
    float completion_reward, death_penalty, speed_bonus, progress_reward;
    float practice_prob;
    int practice_gap, practice_entry, practice_exit, practice_route, practice_frames, practice_short_goals;
    int mix_flat, mix_gaps, mix_pipes, mix_stairs, mix_walkers, mix_underground, mix_composite;
    int physics_mode, walk_accel, brake_accel, player_width, player_height;
    int jump_fast_speed, jump_fast_threshold, gravity_fast_hold, gravity_fast_release;
    int pipe_min_height, pipe_max_height, stair_height;
    // Opt-in full-course generator. Zero preserves the original seed stream.
    int generator_mode, length_min, section_min, section_max, spacing_min, spacing_max;
} MLConfig;

typedef struct MLEnemy {
    int x, y, vx, vy, room, alive, activated;
} MLEnemy;

typedef struct MLState {
    uint32_t rng, level_seed;
    unsigned char tiles[ML_ROOMS][ML_HEIGHT][ML_WIDTH];
    int widths[ML_ROOMS];
    int course, difficulty, theme, room;
    // x is the left of the collision box; y is its feet. Units: 1/256 pixel.
    int x, y, vx, vy, grounded, facing, previous_action, jump_frames;
    int tick, status, transition_frames, pipe_visits, pipe_returns, kills;
    int entry_x, entry_y, return_x, return_y, exit_x, goal_x;
    int frontier[ML_ROOMS], progress;
    float episode_return;
    MLEnemy enemies[ML_ENEMIES];
    int task, episode_limit, goal_room, goal_task;
    int jump_released, jump_gravity_hold, jump_gravity_release;
    // Append rather than reshaping the v4 state prefix. Old snapshots still
    // require their original binary; legacy generated trajectories stay exact.
    int generator_mode;
    unsigned char extra_tiles[ML_ROOMS][ML_HEIGHT][ML_MAX_WIDTH - ML_WIDTH];
    MLEnemy extra_enemies[ML_ENEMY_POOL - ML_ENEMIES];
    int visible_enemies[ML_ENEMIES];
    int sections, motifs[8];
} MLState;

ML_HD int ml_min(int a, int b) { return a < b ? a : b; }
ML_HD int ml_max(int a, int b) { return a > b ? a : b; }
ML_HD int ml_abs(int x) { return x < 0 ? -x : x; }
ML_HD int ml_clamp(int x, int lo, int hi) { return ml_min(hi, ml_max(lo, x)); }
ML_HD float ml_clip(float x) { return x < -1 ? -1 : x > 1 ? 1 : x; }
ML_HD int ml_floor(int x, int scale) { return x >= 0 ? x / scale : -1 - (-1 - x) / scale; }
ML_HD uint32_t ml_hash(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15;
    x *= 0x846ca68bu; return x ^ (x >> 16);
}
ML_HD uint32_t ml_rand(uint32_t* x) {
    uint32_t v = *x ? *x : 1u;
    v ^= v << 13; v ^= v >> 17; v ^= v << 5; return *x = v;
}
ML_HD int ml_range(uint32_t* rng, int lo, int hi) {
    return lo + (int)(ml_rand(rng) % (uint32_t)(hi - lo + 1));
}
ML_HD MLConfig ml_default_config(void) {
    MLConfig c = {0};
    c.course = ML_MIXED; c.difficulty = 1; c.length = 96; c.max_frames = 2400;
    c.seed = 73; c.accel = 24; c.friction = 32;
    c.walk_speed = 384; c.run_speed = 768; c.jump_speed = 1280;
    c.gravity_hold = 48; c.gravity_release = 112; c.max_fall = 1152; c.hold_frames = 20;
    c.completion_reward = 10; c.death_penalty = 1; c.speed_bonus = 2;
    c.progress_reward = 0.001f;
    c.practice_gap = c.practice_entry = c.practice_exit = c.practice_route = 1;
    c.practice_frames = 600;
    c.practice_short_goals = 1;
    c.mix_flat = c.mix_gaps = c.mix_pipes = c.mix_stairs = c.mix_walkers = c.mix_underground = c.mix_composite = 1;
    c.walk_accel = 10; c.brake_accel = 32; c.player_width = 12; c.player_height = 16;
    c.jump_fast_speed = 1280; c.jump_fast_threshold = 512;
    c.gravity_fast_hold = 40; c.gravity_fast_release = 144;
    c.section_min = 40; c.section_max = 96;
    c.spacing_min = 8; c.spacing_max = 24;
    return c;
}
ML_HD uint32_t ml_initial_seed(const MLConfig* c, int env_index) {
    return ml_hash(c->seed ^ ml_hash((uint32_t)env_index + 1u)
        ^ ml_hash(c->split + 0x517cc1b7u));
}
ML_HD int ml_tile(const MLState* s, int room, int tx, int ty) {
    if (tx < 0 || tx >= s->widths[room]) return ML_SOLID;
    if (ty < 0 || ty >= ML_HEIGHT) return ML_AIR;
    return tx < ML_WIDTH ? s->tiles[room][ty][tx] : s->extra_tiles[room][ty][tx - ML_WIDTH];
}
ML_HD void ml_rect(MLState* s, int room, int x, int y, int w, int h, int tile) {
    for (int yy = ml_max(0, y); yy < ml_min(ML_HEIGHT, y + h); yy++)
        for (int xx = ml_max(0, x); xx < ml_min(s->widths[room], x + w); xx++)
            if (xx < ML_WIDTH) s->tiles[room][yy][xx] = (unsigned char)tile;
            else s->extra_tiles[room][yy][xx - ML_WIDTH] = (unsigned char)tile;
}
ML_HD int ml_enemy_count(const MLState* s) { return s->generator_mode ? ML_ENEMY_POOL : ML_ENEMIES; }
ML_HD MLEnemy* ml_enemy(MLState* s, int i) {
    return i < ML_ENEMIES ? &s->enemies[i] : &s->extra_enemies[i - ML_ENEMIES];
}
ML_HD const MLEnemy* ml_enemy_const(const MLState* s, int i) {
    return i < ML_ENEMIES ? &s->enemies[i] : &s->extra_enemies[i - ML_ENEMIES];
}
ML_HD void ml_visible_enemies(MLState* s) {
    if (!s->generator_mode) return;
    for (int slot = 0; slot < ML_ENEMIES; slot++) s->visible_enemies[slot] = -1;
    for (int i = 0; i < ML_ENEMY_POOL; i++) {
        const MLEnemy* e = ml_enemy_const(s, i);
        int distance = ml_abs(e->x - s->x);
        if (!e->alive || e->room != s->room || distance > 12 * ML_TILE) continue;
        for (int slot = 0; slot < ML_ENEMIES; slot++) {
            int old = s->visible_enemies[slot];
            if (old >= 0 && ml_abs(ml_enemy_const(s, old)->x - s->x) <= distance) continue;
            for (int j = ML_ENEMIES - 1; j > slot; j--) s->visible_enemies[j] = s->visible_enemies[j-1];
            s->visible_enemies[slot] = i; break;
        }
    }
}
ML_HD void ml_add_enemy(MLState* s, int x, int room) {
    for (int i = 0; i < ml_enemy_count(s); i++) if (!ml_enemy(s, i)->alive) {
        MLEnemy* e = ml_enemy(s, i);
        e->alive = 1; e->x = x * ML_TILE; e->y = 13 * ML_TILE;
        e->vx = -128; e->room = room; return;
    }
}
ML_HD void ml_generate_legacy(MLState* s, const MLConfig* c, uint32_t seed) {
    memset(s, 0, sizeof(*s));
    s->rng = seed ? seed : 1u; s->level_seed = s->rng;
    s->widths[0] = c->length; s->widths[1] = 40;
    s->course = c->course;
    if (!s->course) {
        int weights[7] = {c->mix_flat, c->mix_gaps, c->mix_pipes, c->mix_stairs,
            c->mix_walkers, c->mix_underground, c->mix_composite};
        int total = 0; for (int i = 0; i < 7; i++) total += weights[i];
        int pick = ml_range(&s->rng, 0, total - 1);
        for (int i = 0; i < 7; i++) {
            if (pick < weights[i]) { s->course = i + 1; break; }
            pick -= weights[i];
        }
    }
    s->difficulty = c->difficulty; s->theme = ml_range(&s->rng, 0, 2);
    for (int room = 0; room < ML_ROOMS; room++)
        ml_rect(s, room, 0, 13, s->widths[room], 2, ML_SOLID);
    // A reachable entrance/return pair is reserved before placing obstacles.
    s->entry_x = ml_range(&s->rng, 16, 20);
    s->entry_y = 11; s->return_x = c->length - 22; s->return_y = 11;
    s->exit_x = 35; s->goal_x = (c->length - 5) * ML_TILE;
    int has_pipe = s->course == ML_UNDERGROUND || s->course == ML_COMPOSITE || c->require_pipe;
    if (has_pipe && c->pipe_max_height)
        s->entry_y = 13 - ml_range(&s->rng, c->pipe_min_height ? c->pipe_min_height : 1, c->pipe_max_height);
    if (!has_pipe) s->entry_x = -100;
    int spacing = 14;
    int end_margin = c->stair_height ? ml_max(12, 2 * c->stair_height + 4) : 12;
    for (int x = 12; x < c->length - end_margin; x += ml_range(&s->rng, spacing, ml_max(20, spacing))) {
        spacing = 14;
        if (has_pipe && (ml_abs(x - s->entry_x) < 8 || ml_abs(x - s->return_x) < 8)) continue;
        int kind = s->course;
        if (kind == ML_COMPOSITE) kind = ml_range(&s->rng, ML_GAPS, ML_WALKERS);
        if (kind == ML_GAPS) {
            int w = ml_range(&s->rng, 2, 2 + c->difficulty);
            ml_rect(s, 0, x, 13, w, 2, ML_AIR);
        } else if (kind == ML_PIPES) {
            int h = ml_range(&s->rng, c->pipe_min_height ? c->pipe_min_height : 1,
                c->pipe_max_height ? c->pipe_max_height : 2 + (c->difficulty > 1));
            ml_rect(s, 0, x, 13 - h, 2, h, ML_PIPE);
        } else if (kind == ML_STAIRS) {
            int h = c->stair_height ? ml_range(&s->rng, 2, c->stair_height) : 2 + (c->difficulty > 1);
            for (int j = 0; j < h; j++) ml_rect(s, 0, x + j, 12 - j, 1, j + 1, ML_SOLID);
            for (int j = h; j < 2*h - 1; j++) ml_rect(s, 0, x + j, 13 - (2*h - j - 1), 1, 2*h - j - 1, ML_SOLID);
            if (c->stair_height) spacing = ml_max(14, 2 * h + 3);
        } else if (kind == ML_WALKERS) {
            ml_add_enemy(s, x, 0);
            if (c->difficulty > 1) ml_add_enemy(s, x + 3, 0);
        }
        // Overhead blocks only in the mixed course, away from pit takeoffs.
        if (s->course == ML_COMPOSITE && kind == ML_WALKERS)
            ml_rect(s, 0, x + 5, 8, 3, 1, ML_BRICK);
    }
    if (has_pipe) {
        ml_rect(s, 0, s->entry_x - 3, 13, 8, 2, ML_SOLID);
        ml_rect(s, 0, s->entry_x, s->entry_y, 2, 13 - s->entry_y, ML_PIPE);
        ml_rect(s, 0, s->return_x - 3, 13, 8, 2, ML_SOLID);
        ml_rect(s, 0, s->return_x, s->return_y, 2, 13 - s->return_y, ML_PIPE);
        ml_rect(s, 1, 0, 0, s->widths[1], 2, ML_SOLID);
        for (int x = 8; x < 28; x += ml_range(&s->rng, 6, 9))
            ml_rect(s, 1, x, 8, ml_range(&s->rng, 2, 4), 1, ML_BRICK);
        ml_rect(s, 1, s->exit_x, 11, 2, 2, ML_PIPE);
    }
    s->x = 2 * ML_TILE; s->y = 13 * ML_TILE; s->grounded = 1; s->facing = 1;
    s->frontier[0] = s->x; s->frontier[1] = 2 * ML_TILE;
    s->episode_limit = c->max_frames;
}

ML_HD int ml_pick_family(uint32_t* rng, const MLConfig* c) {
    int weights[7] = {c->mix_flat, c->mix_gaps, c->mix_pipes, c->mix_stairs,
        c->mix_walkers, c->mix_underground, c->mix_composite};
    int total = 0; for (int i = 0; i < 7; i++) total += weights[i];
    int pick = ml_range(rng, 0, total - 1);
    for (int i = 0; i < 7; i++) {
        if (pick < weights[i]) return i + 1;
        pick -= weights[i];
    }
    return ML_FLAT;
}
ML_HD void ml_generate_full(MLState* s, const MLConfig* c, uint32_t seed) {
    memset(s, 0, sizeof(*s)); s->generator_mode = 1;
    s->rng = seed ? seed : 1u; s->level_seed = s->rng;
    int length = c->length_min ? ml_range(&s->rng, c->length_min, c->length) : c->length;
    s->widths[0] = length; s->widths[1] = ml_range(&s->rng, 48, 128);
    s->course = c->course ? c->course : ML_COMPOSITE;
    s->difficulty = c->difficulty; s->theme = ml_range(&s->rng, 0, 2);
    s->entry_x = -100; s->entry_y = 11; s->return_y = 11;
    s->exit_x = s->widths[1] - 5; s->goal_x = (length - 5) * ML_TILE;
    for (int room = 0; room < ML_ROOMS; room++) ml_rect(s, room, 0, 13, s->widths[room], 2, ML_SOLID);
    int has_pipe = c->require_pipe || c->course == ML_UNDERGROUND
        || ((!c->course || c->course == ML_COMPOSITE) && ml_range(&s->rng, 0, 2) == 0);
    if (has_pipe) {
        s->entry_x = ml_range(&s->rng, 16, ml_max(16, length - 64));
        s->return_x = ml_min(length - 18, s->entry_x + ml_range(&s->rng, 24, 96));
        s->entry_y = 13 - ml_range(&s->rng, c->pipe_min_height ? c->pipe_min_height : 2,
            c->pipe_max_height ? c->pipe_max_height : 3);
        s->return_y = 13 - ml_range(&s->rng, 2, 3);
    }
    // Each complete episode has changing section styles, densities and sizes.
    // Motifs share one world and clock; no section resets or intermediate goals.
    for (int begin = 12; begin < length - 20;) {
        int end = ml_min(length - 20, begin + ml_range(&s->rng, c->section_min, c->section_max));
        int family = c->course ? c->course : ml_pick_family(&s->rng, c);
        int difficulty = ml_range(&s->rng, 0, c->difficulty);
        s->sections++;
        for (int x = begin; x < end;) {
            int gap = ml_range(&s->rng, c->spacing_min, c->spacing_max);
            int kind = family;
            if (kind == ML_COMPOSITE) kind = ml_range(&s->rng, ML_GAPS, ML_WALKERS);
            if (!c->course && ml_range(&s->rng, 0, 3) == 0) {
                kind = ml_pick_family(&s->rng, c);
                if (kind == ML_COMPOSITE) kind = ml_range(&s->rng, ML_GAPS, ML_WALKERS);
            }
            if (has_pipe && (ml_abs(x - s->entry_x) < 12 || ml_abs(x - s->return_x) < 12)) { x += gap; continue; }
            int extent = 0;
            if (kind == ML_GAPS) {
                extent = ml_range(&s->rng, 2, 2 + difficulty);
                ml_rect(s, 0, x, 13, extent, 2, ML_AIR);
            } else if (kind == ML_PIPES) {
                int h = ml_range(&s->rng, c->pipe_min_height ? c->pipe_min_height : 1,
                    c->pipe_max_height ? c->pipe_max_height : 2 + (difficulty > 1));
                extent = 2; ml_rect(s, 0, x, 13 - h, extent, h, ML_PIPE);
            } else if (kind == ML_STAIRS) {
                int h = ml_range(&s->rng, 2, c->stair_height ? c->stair_height : 3);
                // Keep both sides and a landing inside the section/end margin.
                if (x + 2 * h + 4 > end) { x += gap; continue; }
                extent = 2 * h - 1;
                for (int j = 0; j < extent; j++) {
                    int height = j < h ? j + 1 : extent - j;
                    ml_rect(s, 0, x + j, 13 - height, 1, height, ML_SOLID);
                }
            } else if (kind == ML_WALKERS) {
                extent = ml_range(&s->rng, 1, 1 + difficulty);
                for (int j = 0; j < extent; j++) {
                    int ex = x + 2 * j;
                    if (ex >= length - 12 || ml_tile(s, 0, ex, 13) == ML_AIR) continue;
                    ml_add_enemy(s, ex, 0);
                }
                extent = 2 * extent;
                if (ml_range(&s->rng, 0, 1)) {
                    int row = ml_range(&s->rng, 7, 9), width = ml_range(&s->rng, 2, 5);
                    ml_rect(s, 0, x + extent + 2, row, width, 1, ML_BRICK);
                }
            } else if (kind == ML_UNDERGROUND) {
                // Underground-flavored surface sections provide block ceilings;
                // the separately randomized entrance/return pair stays optional.
                extent = ml_range(&s->rng, 3, 7);
                ml_rect(s, 0, x, ml_range(&s->rng, 6, 9), extent, 1, ML_BRICK);
            }
            s->motifs[kind]++;
            x += extent + gap;
        }
        begin = end;
    }
    if (has_pipe) {
        ml_rect(s, 0, s->entry_x - 4, 13, 10, 2, ML_SOLID);
        ml_rect(s, 0, s->return_x - 4, 13, 10, 2, ML_SOLID);
        // Reserved pipe approaches must not retain overhanging stair walls.
        ml_rect(s, 0, s->entry_x - 3, 3, 8, 10, ML_AIR);
        ml_rect(s, 0, s->return_x - 3, 3, 8, 10, ML_AIR);
        ml_rect(s, 0, s->entry_x, s->entry_y, 2, 13 - s->entry_y, ML_PIPE);
        ml_rect(s, 0, s->return_x, s->return_y, 2, 13 - s->return_y, ML_PIPE);
        ml_rect(s, 1, 0, 0, s->widths[1], 2, ML_SOLID);
        for (int x = 8; x < s->exit_x - 8; x += ml_range(&s->rng, 5, 12)) {
            int row = ml_range(&s->rng, 6, 9), width = ml_range(&s->rng, 2, 6);
            ml_rect(s, 1, x, row, width, 1, ML_BRICK);
        }
        ml_rect(s, 1, s->exit_x, 11, 2, 2, ML_PIPE);
    }
    s->x = 2 * ML_TILE; s->y = 13 * ML_TILE; s->grounded = 1; s->facing = 1;
    s->frontier[0] = s->x; s->frontier[1] = 2 * ML_TILE; s->episode_limit = c->max_frames;
    ml_visible_enemies(s);
}
ML_HD void ml_generate(MLState* s, const MLConfig* c, uint32_t seed) {
    if (c->generator_mode) ml_generate_full(s, c, seed);
    else ml_generate_legacy(s, c, seed);
}

// Authored synthetic practice starts. Geometry uses the ordinary generator;
// a separate draw selects practice without changing root-level seed streams.
ML_HD void ml_reset(MLState* s, const MLConfig* c, uint32_t seed) {
    int task = ML_TASK_FULL;
    uint32_t choice = ml_hash(seed ^ 0x9b741d83u);
    int weight = c->practice_gap + c->practice_entry + c->practice_exit + c->practice_route;
    if (c->practice_prob > 0 && weight > 0
            && (float)(ml_rand(&choice) >> 8) / 16777216.0f < c->practice_prob) {
        int pick = ml_range(&choice, 0, weight - 1);
        if (pick < c->practice_gap) task = ML_TASK_GAP;
        else if ((pick -= c->practice_gap) < c->practice_entry) task = ML_TASK_ENTRY;
        else if ((pick -= c->practice_entry) < c->practice_exit) task = ML_TASK_EXIT;
        else task = ML_TASK_ROUTE;
    }
    MLConfig geometry = *c;
    if (task) { geometry.course = task == ML_TASK_GAP ? ML_GAPS : ML_UNDERGROUND; geometry.require_pipe = 0; }
    ml_generate(s, &geometry, seed);
    if (!task) return;
    s->task = task; s->episode_limit = c->practice_frames;
    s->goal_task = c->practice_short_goals ? task : ML_TASK_FULL;
    if (task == ML_TASK_GAP) {
        int count = 0, start = 0, end = 0;
        for (int x = 1; x < s->widths[0]; x++)
            if (!ml_tile(s, 0, x, 13) && ml_tile(s, 0, x-1, 13)) count++;
        int pick = ml_range(&s->rng, 0, count - 1);
        for (int x = 1; x < s->widths[0]; x++) {
            if (ml_tile(s, 0, x, 13) || !ml_tile(s, 0, x-1, 13)) continue;
            if (pick-- == 0) { start = end = x; break; }
        }
        while (!ml_tile(s, 0, end, 13)) end++;
        s->x = (start - ml_range(&s->rng, 2, 6)) * ML_TILE + ml_range(&s->rng, 0, ML_TILE - 1);
        if (c->practice_short_goals) s->goal_x = (end + 3) * ML_TILE;
    } else if (task == ML_TASK_EXIT) {
        s->room = 1; s->pipe_visits = 1;
        s->x = (s->exit_x - ml_range(&s->rng, 2, 5)) * ML_TILE + ml_range(&s->rng, 0, ML_TILE - 1);
        if (c->practice_short_goals) s->goal_x = s->return_x * ML_TILE + 4 * ML_FP;
    } else {
        if (ml_range(&s->rng, 0, 1)) {
            s->x = s->entry_x * ML_TILE + ml_range(&s->rng, 3 * ML_FP, 17 * ML_FP);
            s->y = s->entry_y * ML_TILE;
        } else {
            s->x = (s->entry_x - ml_range(&s->rng, 2, 5)) * ML_TILE + ml_range(&s->rng, 0, ML_TILE - 1);
        }
        if (c->practice_short_goals) {
            s->goal_room = task == ML_TASK_ENTRY;
            s->goal_x = task == ML_TASK_ENTRY ? 2 * ML_TILE : (s->return_x + 4) * ML_TILE;
        }
    }
    s->vx = ml_range(&s->rng, 0, c->run_speed);
    s->previous_action = ml_range(&s->rng, 0, 1) ? ML_A : 0;
    s->frontier[s->room] = s->x;
    ml_visible_enemies(s);
}

ML_HD void ml_move_x(const MLState* s, int room, int* x, int y, int* vx, int w, int h) {
    int nx = *x + *vx;
    int col = ml_floor(*vx > 0 ? nx + w - 1 : nx, ML_TILE);
    for (int row = ml_floor(y - h, ML_TILE); row <= ml_floor(y - 1, ML_TILE); row++) {
        if (!ml_tile(s, room, col, row)) continue;
        if (*vx > 0) nx = col * ML_TILE - w;
        else if (*vx < 0) nx = (col + 1) * ML_TILE;
        *vx = 0; break;
    }
    *x = nx;
}
ML_HD int ml_move_y(const MLState* s, int room, int x, int* y, int* vy, int w, int h) {
    int ny = *y + *vy, grounded = 0;
    int row = ml_floor(*vy > 0 ? ny - 1 : ny - h, ML_TILE);
    for (int col = ml_floor(x, ML_TILE); col <= ml_floor(x + w - 1, ML_TILE); col++) {
        if (!ml_tile(s, room, col, row)) continue;
        if (*vy > 0) { ny = row * ML_TILE; grounded = 1; }
        else if (*vy < 0) ny = (row + 1) * ML_TILE + h;
        *vy = 0; break;
    }
    *y = ny; return grounded;
}
ML_HD int ml_overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return ax < bx + bw && ax + aw > bx && ay > by - bh && ay - ah < by;
}
ML_HD int ml_near_entry(const MLState* s) {
    int center = s->x + ML_PLAYER_W / 2;
    return s->room == 0 && s->entry_x > 0 && s->grounded
        && s->y == s->entry_y * ML_TILE
        && center >= s->entry_x * ML_TILE + 3 * ML_FP
        && center <= (s->entry_x + 2) * ML_TILE - 3 * ML_FP;
}
ML_HD void ml_enter_room(MLState* s, int room) {
    s->room = room; s->vx = s->vy = 0; s->jump_frames = 0;
    s->x = room ? 2 * ML_TILE : s->return_x * ML_TILE + 4 * ML_FP;
    s->y = room ? 13 * ML_TILE : s->return_y * ML_TILE;
    s->grounded = 1; s->transition_frames = 24;
    if (room) s->pipe_visits++; else s->pipe_returns++;
    // Teleportation never earns frontier reward; revisits never lower it.
    s->frontier[room] = ml_max(s->frontier[room], s->x);
}
ML_HD int ml_requires_pipe(const MLState* s, const MLConfig* c) {
    if (s->goal_task) return s->goal_task >= ML_TASK_ENTRY;
    if (s->task == ML_TASK_GAP) return 0;
    return c->require_pipe || s->course == ML_UNDERGROUND;
}
ML_HD int ml_goal_reached(const MLState* s, const MLConfig* c) {
    if (s->room != s->goal_room || s->x < s->goal_x || s->transition_frames) return 0;
    if (s->goal_task == ML_TASK_GAP) return s->grounded && s->y == 13 * ML_TILE;
    if (s->goal_task == ML_TASK_ENTRY) return s->pipe_visits > 0;
    if (s->goal_task == ML_TASK_EXIT || s->goal_task == ML_TASK_ROUTE) return s->pipe_returns > 0;
    return !ml_requires_pipe(s, c) || s->pipe_returns > 0;
}
ML_HD float ml_step(MLState* s, const MLConfig* c, int action) {
    if (s->status != ML_RUNNING) return 0;
    action &= 63; s->tick++;
    // Mode 2 preserves a valid controller convention for the main glitchless
    // experiment: opposing directions cancel before reaching either backend.
    if (c->physics_mode >= 2 && (action & (ML_LEFT | ML_RIGHT)) == (ML_LEFT | ML_RIGHT))
        action &= ~(ML_LEFT | ML_RIGHT);
    int before_room = s->room;
    if (s->transition_frames > 0) {
        s->transition_frames--;
    } else if ((action & ML_DOWN) && ml_near_entry(s)) {
        ml_enter_room(s, 1);
    } else if (s->room == 1 && (action & ML_RIGHT) && s->grounded
            && s->y == 13 * ML_TILE && s->x < s->exit_x * ML_TILE
            && s->x + ML_PLAYER_W >= s->exit_x * ML_TILE - 2 * ML_FP) {
        ml_enter_room(s, 0);
    } else {
        int direction = !!(action & ML_RIGHT) - !!(action & ML_LEFT);
        int down_brake = c->physics_mode >= 2 && s->grounded && (action & ML_DOWN);
        if (down_brake) direction = 0;
        int top_speed = action & ML_B ? c->run_speed : c->walk_speed;
        if (direction) {
            s->facing = direction;
            int accel = c->accel;
            if (c->physics_mode) {
                accel = action & ML_B ? c->accel : c->walk_accel;
                if (direction * s->vx < 0) accel = c->brake_accel;
                // Releasing run does not instantly erase running momentum.
                if (direction * s->vx > top_speed) {
                    s->vx -= direction * c->friction; accel = 0;
                    top_speed = ml_max(top_speed, ml_abs(s->vx));
                }
            }
            s->vx = ml_clamp(s->vx + direction * accel, -top_speed, top_speed);
        } else if (s->grounded) {
            int friction = down_brake ? c->walk_accel : c->friction;
            s->vx = s->vx > 0 ? ml_max(0, s->vx - friction) : ml_min(0, s->vx + friction);
        }
        if ((action & ML_A) && !(s->previous_action & ML_A) && s->grounded) {
            s->vy = -c->jump_speed; s->grounded = 0; s->jump_frames = 0;
            if (c->physics_mode) {
                int fast = ml_abs(s->vx) >= c->jump_fast_threshold;
                s->vy = -(fast ? c->jump_fast_speed : c->jump_speed);
                s->jump_gravity_hold = fast ? c->gravity_fast_hold : c->gravity_hold;
                s->jump_gravity_release = fast ? c->gravity_fast_release : c->gravity_release;
                s->jump_released = 0;
            }
        }
        int hold = (action & ML_A) && s->vy < 0 && s->jump_frames < c->hold_frames;
        if (c->physics_mode) {
            if (!(action & ML_A)) s->jump_released = 1;
            hold &= !s->jump_released;
        } else s->vy = ml_min(c->max_fall, s->vy + (hold ? c->gravity_hold : c->gravity_release));
        if (s->vy < 0) s->jump_frames++;
        int old_y = s->y;
        int width = c->player_width * ML_FP, height = c->player_height * ML_FP;
        // Quantize speed symmetrically. Flooring a signed fractional speed
        // introduces a leftward drift even under balanced random inputs.
        int move_vx = c->physics_mode ? (s->vx / 16) * 16 : s->vx;
        ml_move_x(s, s->room, &s->x, s->y, &move_vx, width, height);
        if (!c->physics_mode) s->vx = move_vx;
        else if (!move_vx && s->vx && s->vx / 16 != 0) s->vx = 0;
        // Gravity follows position in the measured profile. Probe support so
        // standing still does not alternate between grounded and free fall.
        if (c->physics_mode && s->grounded && !s->vy) s->vy = 1;
        s->grounded = ml_move_y(s, s->room, s->x, &s->y, &s->vy, width, height);
        if (c->physics_mode && !s->grounded) {
            int gravity = hold ? s->jump_gravity_hold : s->jump_gravity_release;
            if (!gravity) gravity = c->gravity_fast_hold; // first fall from support
            s->vy = ml_min(c->max_fall, s->vy + gravity);
        }
        for (int i = 0; i < ml_enemy_count(s); i++) {
            MLEnemy* e = ml_enemy(s, i);
            if (!e->alive || e->room != s->room) continue;
            if (ml_abs(e->x - s->x) < 12 * ML_TILE) e->activated = 1;
            if (!e->activated) continue;
            int evx = e->vx;
            ml_move_x(s, e->room, &e->x, e->y, &e->vx, ML_PLAYER_W, 12 * ML_FP);
            if (!e->vx) e->vx = -evx;
            e->vy = ml_min(c->max_fall, e->vy + c->gravity_release);
            ml_move_y(s, e->room, e->x, &e->y, &e->vy, ML_PLAYER_W, 12 * ML_FP);
            if (e->y > (ML_HEIGHT + 2) * ML_TILE) { e->alive = 0; continue; }
            if (!ml_overlap(s->x, s->y, width, height, e->x, e->y, ML_PLAYER_W, 12 * ML_FP)) continue;
            if (s->vy > 0 && old_y <= e->y - 10 * ML_FP) {
                e->alive = 0; s->kills++; s->vy = -3 * ML_FP; s->grounded = 0;
            } else s->status = ML_DEATH;
        }
        if (s->y > (ML_HEIGHT + 2) * ML_TILE) s->status = ML_DEATH;
    }
    if (s->status == ML_RUNNING && ml_goal_reached(s, c)) s->status = ML_CLEAR;
    s->previous_action = action;
    int delta = 0;
    if (s->room == before_room && !s->transition_frames) {
        delta = ml_max(0, s->x - s->frontier[s->room]);
        s->frontier[s->room] = ml_max(s->frontier[s->room], s->x);
        s->progress += delta;
    }
    if (s->status == ML_RUNNING && s->tick >= s->episode_limit) s->status = ML_TIMEOUT;
    float reward = c->progress_reward * ((float)delta / ML_FP);
    if (s->status == ML_CLEAR)
        reward += c->completion_reward + c->speed_bonus * (1.0f - (float)s->tick / s->episode_limit);
    if (s->status == ML_DEATH || s->status == ML_TIMEOUT) reward -= c->death_penalty;
    s->episode_return += reward;
    ml_visible_enemies(s);
    return reward;
}

// One observation element can be computed independently by a CUDA lane.
// World position, level seed, palette and course labels are deliberately absent.
ML_HD float ml_observation_at(const MLState* s, const MLConfig* c, int i) {
    if (i < ML_EGO) {
        switch (i) {
        case 0: return ml_clip((float)s->vx / 1024);
        case 1: return ml_clip((float)s->vy / 1536);
        case 2: return (float)(s->x - ml_floor(s->x, ML_TILE) * ML_TILE) / ML_TILE;
        case 3: return (float)(s->y - ml_floor(s->y, ML_TILE) * ML_TILE) / ML_TILE;
        case 4: return (float)(s->x & 255) / 255;
        case 5: return (float)(s->y & 255) / 255;
        case 6: return (float)s->grounded;
        case 7: return (float)(s->vy < 0);
        case 8: return (float)(s->vy > 0);
        case 9: return (float)s->facing;
        case 10: return (float)!!(s->previous_action & ML_A);
        case 11: return (float)!!(s->previous_action & ML_B);
        case 12: return (float)s->jump_frames / 32;
        case 13: return (float)(s->room == 1);
        case 14: return (float)s->transition_frames / 24;
        case 15: return (float)ml_near_entry(s);
        case 16: return (float)ml_requires_pipe(s, c);
        case 17: return (float)(s->pipe_returns > 0);
        case 18: return 1.0f - (float)s->tick / s->episode_limit;
        case 19: return (float)c->run_speed / 1024;
        case 20: return (float)c->jump_speed / 2048;
        case 21: return (float)c->gravity_hold / 256;
        case 22: return (float)c->gravity_release / 256;
        default: return 1.0f; // Small-player form; reserved for later forms.
        }
    }
    i -= ML_EGO;
    if (i < ML_ENEMIES * ML_ENT_FEATURES) {
        int slot = i / ML_ENT_FEATURES;
        int index = s->generator_mode ? s->visible_enemies[slot] : slot;
        if (index < 0) return 0;
        const MLEnemy* e = ml_enemy_const(s, index);
        if (!e->alive || e->room != s->room || ml_abs(e->x - s->x) > 12 * ML_TILE) return 0;
        switch (i % ML_ENT_FEATURES) {
        case 0: return 1;
        case 1: return ml_clip((float)(e->x - s->x) / (12 * ML_TILE));
        case 2: return ml_clip((float)(e->y - s->y) / (8 * ML_TILE));
        case 3: return (float)e->vx / 1024;
        case 4: return (float)e->vy / 1536;
        case 5: return 1; // Walker type.
        case 6: return 12.0f / 16;
        default: return 12.0f / 16;
        }
    }
    i -= ML_ENEMIES * ML_ENT_FEATURES;
    int channel = i % ML_GRID_C, cell = i / ML_GRID_C;
    int col = ml_floor(s->x + ML_PLAYER_W / 2, ML_TILE) - 4 + cell % ML_GRID_W;
    int row = ml_floor(s->y, ML_TILE) - 8 + cell / ML_GRID_W;
    int tile = ml_tile(s, s->room, col, row);
    if (channel == 0) return (float)(tile != ML_AIR);
    if (channel == 1) return (float)(tile == ML_BRICK);
    if (channel == 2) {
        if (s->room == 0) return (float)(col >= s->entry_x && col < s->entry_x + 2 && row == s->entry_y);
        return (float)(col == s->exit_x && row >= 11 && row < 13);
    }
    if (channel == 3) {
        // Geometry defines unsupported space, independently of its rendered color.
        if (col < 0 || col >= s->widths[s->room] || row < 0 || tile) return 0;
        for (int y = row + 1; y < ML_HEIGHT; y++) if (ml_tile(s, s->room, col, y)) return 0;
        return 1;
    }
    if (s->goal_task == ML_TASK_ENTRY && s->room == 0)
        return (float)(col == s->entry_x && row == s->entry_y);
    return (float)(s->room == s->goal_room && col == s->goal_x / ML_TILE && row >= 6 && row < 13);
}
ML_HD void ml_observe(const MLState* s, const MLConfig* c, float* obs) {
    for (int i = 0; i < ML_OBS_SIZE; i++) obs[i] = ml_observation_at(s, c, i);
}

// Diagnostic controller, not an expert or an optimal TAS. Used for smoke tests.
ML_HD int ml_script_action(const MLState* s) {
    int a = ML_RIGHT | ML_B;
    if (s->transition_frames) return 0;
    if (ml_near_entry(s)) return ML_DOWN;
    if (!s->grounded) return a | ML_A;
    int tx = ml_floor(s->x + ML_PLAYER_W, ML_TILE);
    int ty = ml_floor(s->y - 1, ML_TILE);
    int obstacle = 0;
    for (int dx = 0; dx < 3; dx++) {
        obstacle |= ml_tile(s, s->room, tx + dx, ty) != ML_AIR;
        obstacle |= ml_tile(s, s->room, tx + dx, 13) == ML_AIR;
    }
    for (int i = 0; i < ml_enemy_count(s); i++) {
        const MLEnemy* e = ml_enemy_const(s, i);
        obstacle |= e->alive && e->room == s->room && e->x > s->x && e->x - s->x < 3 * ML_TILE;
    }
    if (obstacle && !(s->previous_action & ML_A)) a |= ML_A;
    return a;
}
