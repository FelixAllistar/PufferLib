#pragma once

// PufferLib 5c Bomberman: multiagent FFA selfplay env.
// CPU path: full match state + Raylib render.
// GPU path: see bomberman.cu (PUFFER_GPU_ENV).

#include "pufferenv.h"
#include "bm_sim.h"

#include <stdint.h>
#include <string.h>

#define OBS_SIZE BM_OBS_SIZE
#define NUM_ATNS BM_NUM_ATNS
#define ACT_SIZES {BM_NUM_ACTIONS}

// Use PufferLib's standard float observation transport. All observation
// values are written directly in [0, 1]; no shared-core changes are required.
typedef float obs_t;

struct Log {
    float perf;            // slot-0 win rate (same accounting as robocode match)
    float score;           // sum of ep_score
    float episode_return;
    float episode_length;
    float kills;
    float self_kills;
    float soft_breaks;
    float pickups;
    float bomb_pickups;
    float range_pickups;
    float speed_pickups;
    float wins;
    float draws;
    float deaths;
    // match(): after vec_log divides by n, these are win rates for A/B.
    // Accumulate s0 * num_agents once per finished match (see bm_end_episode).
    float slot_0_score;
    float slot_1_score;
    float draw_rate;
    float slot_0_kills;
    float slot_0_self_kills;
    float slot_0_opponent_suicides;
    float curriculum_stage;
    float curriculum_full_game;
    float n;
};

#ifndef PUFFER_GPU_ENV

typedef struct Client Client;
struct Client {
    int cell;
};

struct Env {
    Log log;
    Agent agents[BM_MAX_AGENTS];
    int tag;
    int boundary_reached;
    int num_agents;
    unsigned int rng;
    Client* client;

    BMConfig cfg;
    BMMatch match;
    uint64_t curriculum_elapsed;
    int curriculum_level;
    int curriculum_attempts;
    int curriculum_successes;
    // Per-agent running stats for the current episode (also on match.agents)
    Log agent_logs[BM_MAX_AGENTS];
    // When 1, puf_step ends the match but does NOT auto-reset (play freeze).
    int hold_on_done;
};

static inline float bm_kw(Dict* kwargs, const char* key) {
    return (float)dict_get(kwargs, key);
}

static inline void bm_load_config(BMConfig* cfg, Dict* kwargs) {
    *cfg = bm_default_config();
    cfg->width = (int)bm_kw(kwargs, "width");
    cfg->height = (int)bm_kw(kwargs, "height");
    cfg->num_agents = (int)bm_kw(kwargs, "num_agents");
    cfg->max_ticks = (int)bm_kw(kwargs, "max_ticks");
    cfg->bomb_timer = (int)bm_kw(kwargs, "bomb_timer");
    cfg->flame_duration = (int)bm_kw(kwargs, "flame_duration");
    cfg->frames_per_cell = (int)bm_kw(kwargs, "frames_per_cell");
    cfg->soft_density = bm_kw(kwargs, "soft_density");
    cfg->item_chance = bm_kw(kwargs, "item_chance");
    cfg->reward_soft = bm_kw(kwargs, "reward_soft");
    cfg->reward_pickup = bm_kw(kwargs, "reward_pickup");
    cfg->reward_closer = bm_kw(kwargs, "reward_closer");
    cfg->reward_kill = bm_kw(kwargs, "reward_kill");
    cfg->reward_death = bm_kw(kwargs, "reward_death");
    cfg->reward_self_kill = bm_kw(kwargs, "reward_self_kill");
    cfg->reward_win = bm_kw(kwargs, "reward_win");
    cfg->reward_alive = bm_kw(kwargs, "reward_alive");
    cfg->reward_timeout = bm_kw(kwargs, "reward_timeout");
    cfg->reward_bomb_threat = bm_kw(kwargs, "reward_bomb_threat");
    cfg->reward_bomb_escape = bm_kw(kwargs, "reward_bomb_escape");
    cfg->reward_curriculum_aim = bm_kw(kwargs, "reward_curriculum_aim");
    cfg->reward_curriculum_escape = bm_kw(kwargs, "reward_curriculum_escape");
    cfg->reward_curriculum_progress = bm_kw(kwargs, "reward_curriculum_progress");
    cfg->reverse_curriculum = (int)bm_kw(kwargs, "reverse_curriculum");
    cfg->curriculum_steps = (int)bm_kw(kwargs, "curriculum_steps");
    cfg->curriculum_window = (int)bm_kw(kwargs, "curriculum_window");
    cfg->curriculum_success_rate = bm_kw(kwargs, "curriculum_success_rate");
    cfg->pillar_mode = (int)bm_kw(kwargs, "pillar_mode");
    if (cfg->num_agents < 2) cfg->num_agents = 2;
    if (cfg->num_agents > BM_MAX_AGENTS) cfg->num_agents = BM_MAX_AGENTS;
}

void puf_init(Env* env, Dict* kwargs) {
    bm_load_config(&env->cfg, kwargs);
    env->num_agents = env->cfg.num_agents;
    env->client = NULL;
    env->tag = 0;
    env->boundary_reached = 0;
    env->curriculum_elapsed = 0;
    env->curriculum_level = 0;
    env->curriculum_attempts = 0;
    env->curriculum_successes = 0;
    for (int i = 0; i < env->num_agents; i++) {
        // Slot 0 learns; others default to bank 1 for selfplay opponents.
        env->agents[i].policy = (i == 0) ? 0 : 1;
        // PufferLib binds this after puf_init; standalone play leaves it NULL.
        env->agents[i].action_mask = NULL;
    }
    memset(env->agent_logs, 0, sizeof(env->agent_logs));
    memset(&env->log, 0, sizeof(env->log));
    env->hold_on_done = 0;
}

void puf_log(Log* log, Dict* out) {
    dict_set(out, "perf", log->perf);
    dict_set(out, "score", log->score);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "kills", log->kills);
    dict_set(out, "self_kills", log->self_kills);
    dict_set(out, "soft_breaks", log->soft_breaks);
    dict_set(out, "pickups", log->pickups);
    dict_set(out, "bomb_pickups", log->bomb_pickups);
    dict_set(out, "range_pickups", log->range_pickups);
    dict_set(out, "speed_pickups", log->speed_pickups);
    dict_set(out, "wins", log->wins);
    dict_set(out, "draws", log->draws);
    dict_set(out, "deaths", log->deaths);
    dict_set(out, "slot_0_score", log->slot_0_score);
    dict_set(out, "slot_1_score", log->slot_1_score);
    dict_set(out, "draw_rate", log->draw_rate);
    dict_set(out, "slot_0_kills", log->slot_0_kills);
    dict_set(out, "slot_0_self_kills", log->slot_0_self_kills);
    dict_set(out, "slot_0_opponent_suicides", log->slot_0_opponent_suicides);
    dict_set(out, "curriculum_stage", log->curriculum_stage);
    dict_set(out, "curriculum_full_game", log->curriculum_full_game);
    dict_set(out, "n", log->n);
}

static inline void bm_compute_observations(Env* env) {
    for (int a = 0; a < env->num_agents; a++) {
        obs_t* obs = (obs_t*)env->agents[a].observations;
        bm_write_obs_mask(&env->match, &env->cfg, a, obs,
            env->agents[a].action_mask);
    }
}

// outcome: +1 slot-0 won, -1 slot-0 lost, 0 draw. Robocode match accounting.
static inline void bm_end_episode(Env* env, int outcome) {
    float s0 = (outcome > 0) ? 1.0f : (outcome < 0) ? 0.0f : 0.5f;
    float na = (float)env->num_agents;
    env->log.slot_0_score += s0 * na;
    env->log.slot_1_score += (1.0f - s0) * na;
    if (outcome == 0) env->log.draw_rate += na;
    // perf tracks slot-0 win rate the same way
    env->log.perf += s0 * na;
    env->log.slot_0_kills += (float)env->match.agents[0].kills * na;
    env->log.slot_0_self_kills += (float)env->match.agents[0].self_kills * na;
    int opponent_suicides = 0;
    for (int a = 1; a < env->num_agents; a++) {
        opponent_suicides += env->match.agents[a].self_kills;
    }
    env->log.slot_0_opponent_suicides += (float)opponent_suicides * na;
    int curriculum_stage = env->match.curriculum_stage;
    env->log.curriculum_stage += (float)(curriculum_stage < 0
        ? BM_CURRICULUM_STAGES : curriculum_stage) * na;
    env->log.curriculum_full_game += (curriculum_stage < 0 ? 1.0f : 0.0f) * na;

    int draw = (outcome == 0) ? 1 : 0;
    for (int a = 0; a < env->num_agents; a++) {
        BMAgent* ag = &env->match.agents[a];
        int win = (env->match.winner == a) ? 1 : 0;
        env->log.score += ag->ep_score;
        env->log.episode_return += ag->ep_return;
        env->log.episode_length += (float)env->match.tick;
        env->log.kills += (float)ag->kills;
        env->log.self_kills += (float)ag->self_kills;
        env->log.soft_breaks += (float)ag->soft_breaks;
        env->log.bomb_pickups += (float)ag->bomb_pickups;
        env->log.range_pickups += (float)ag->range_pickups;
        env->log.speed_pickups += (float)ag->speed_pickups;
        env->log.pickups += (float)(ag->bomb_pickups
            + ag->range_pickups + ag->speed_pickups);
        env->log.wins += (float)win;
        env->log.draws += (float)draw;
        env->log.deaths += ag->alive ? 0.0f : 1.0f;
        env->log.n += 1.0f;
    }
    int mastery_stage = env->curriculum_level < BM_CURRICULUM_STAGES
        ? env->curriculum_level : BM_CURRICULUM_STAGES - 1;
    // Rehearsal episodes retain old skills but cannot promote the frontier.
    if (env->cfg.reverse_curriculum && curriculum_stage == mastery_stage) {
        env->curriculum_attempts += 1;
        int success = env->match.winner == 0
            && env->match.agents[0].kills > 0
            && env->match.agents[0].self_kills == 0;
        if (success) {
            env->curriculum_successes += 1;
        }
        int window = env->cfg.curriculum_window > 0
            ? env->cfg.curriculum_window : 32;
        if (env->curriculum_attempts >= window) {
            float rate = (float)env->curriculum_successes
                / (float)env->curriculum_attempts;
            float required_rate = env->cfg.curriculum_success_rate;
            // Moving-target lessons are intrinsically much harder than the
            // forced finish, but every success is still a real, safe credited
            // kill because the sparring opponent cannot bomb. Use attainable
            // gates so training can actually move backward to the opening.
            if (mastery_stage == 2 && required_rate > 0.05f) {
                required_rate = 0.05f;
            } else if (mastery_stage >= 3 && required_rate > 0.02f) {
                required_rate = 0.02f;
            }
            if (rate >= required_rate
                    && env->curriculum_level < BM_CURRICULUM_STAGES) {
                env->curriculum_level += 1;
            }
            env->curriculum_attempts = 0;
            env->curriculum_successes = 0;
        }
    }
    if (env->tag > 0) {
        env->boundary_reached = 1;
    }
}

static inline int bm_match_outcome(const Env* env) {
    // +1 slot0 win, -1 slot0 loss, 0 draw
    if (env->match.winner == 0) return 1;
    if (env->match.winner > 0) return -1;
    return 0;
}

void puf_reset(Env* env) {
    uint32_t seed = env->rng ? env->rng : 1u;
    // Mix tick-ish entropy without depending on time()
    seed ^= 0x9e3779b9u * (uint32_t)(env->num_agents + 1);
    bm_reset_match(&env->match, &env->cfg, seed);
    float progress = 1.0f;
    if (env->cfg.reverse_curriculum) {
        if (env->cfg.curriculum_steps > 0
                && env->curriculum_elapsed >= (uint64_t)env->cfg.curriculum_steps) {
            env->curriculum_level = BM_CURRICULUM_STAGES;
        }
        progress = (float)env->curriculum_level
            / (float)BM_CURRICULUM_STAGES;
    }
    bm_apply_reverse_curriculum(&env->match, &env->cfg, progress);
    // Advance rng so consecutive resets differ.
    env->rng = bm_xorshift(&seed);
    for (int a = 0; a < env->num_agents; a++) {
        if (env->agents[a].rewards) env->agents[a].rewards[0] = 0.0f;
        if (env->agents[a].terminals) env->agents[a].terminals[0] = 0.0f;
    }
    bm_compute_observations(env);
}

void puf_step(Env* env) {
    int actions[BM_MAX_AGENTS];
    float rewards[BM_MAX_AGENTS];
    float terminals[BM_MAX_AGENTS];

    // Play mode: freeze on death — stay on terminal frame until puf_reset.
    if (env->hold_on_done && env->match.done) {
        for (int a = 0; a < env->num_agents; a++) {
            env->agents[a].rewards[0] = 0.0f;
            env->agents[a].terminals[0] = 1.0f;
        }
        return;
    }

    for (int a = 0; a < env->num_agents; a++) {
        actions[a] = (int)env->agents[a].actions[0];
        rewards[a] = 0.0f;
        terminals[a] = 0.0f;
        env->agents[a].rewards[0] = 0.0f;
        env->agents[a].terminals[0] = 0.0f;
    }

    bm_step_match(&env->match, &env->cfg, actions, rewards, terminals);
    env->curriculum_elapsed += 1;

    for (int a = 0; a < env->num_agents; a++) {
        env->agents[a].rewards[0] = rewards[a];
        env->agents[a].terminals[0] = terminals[a];
    }

    if (env->match.done) {
        // Log once when the match first completes.
        bm_end_episode(env, bm_match_outcome(env));
        if (env->hold_on_done) {
            // Leave corpse/flame on screen for play; caller will puf_reset later.
            bm_compute_observations(env);
            return;
        }
        // Train path: auto-reset; preserve terminal/reward for the learner.
        float term_r[BM_MAX_AGENTS];
        float term_t[BM_MAX_AGENTS];
        for (int a = 0; a < env->num_agents; a++) {
            term_r[a] = env->agents[a].rewards[0];
            term_t[a] = env->agents[a].terminals[0];
        }
        puf_reset(env);
        for (int a = 0; a < env->num_agents; a++) {
            env->agents[a].rewards[0] = term_r[a];
            env->agents[a].terminals[0] = term_t[a];
        }
        return;
    }

    bm_compute_observations(env);
}

void puf_close(Env* env) {
    (void)env;
}

// ---- Render (Raylib) ----
#ifndef BM_HEADLESS
#include "raylib.h"

static inline Client* bm_make_client(Env* env) {
    Client* c = (Client*)calloc(1, sizeof(Client));
    c->cell = 40;
    int w = env->match.width * c->cell;
    int h = env->match.height * c->cell + 92;
    InitWindow(w, h, "PufferLib Bomberman");
    // Render FPS only — game step rate is controlled in bomberman.c play loop.
    SetTargetFPS(30);
    return c;
}

void puf_render(Env* env) {
    if (env->client == NULL) {
        env->client = bm_make_client(env);
    }
    if (IsKeyDown(KEY_ESCAPE)) {
        exit(0);
    }
    if (IsKeyPressed(KEY_R)) {
        puf_reset(env);
    }

    Client* c = env->client;
    int cell = c->cell;
    BeginDrawing();
    ClearBackground((Color){6, 24, 24, 255});

    const Color hard_c = (Color){40, 70, 70, 255};
    const Color soft_c = (Color){120, 90, 50, 255};
    const Color empty_c = (Color){20, 45, 45, 255};
    const Color flame_c = (Color){255, 120, 40, 200};
    const Color bomb_c = (Color){30, 30, 30, 255};
    const Color bomb_item_c = (Color){80, 220, 120, 255};
    const Color range_item_c = (Color){255, 175, 45, 255};
    const Color speed_item_c = (Color){80, 165, 255, 255};
    const Color agent_cols[BM_MAX_AGENTS] = {
        {0, 200, 200, 255},
        {220, 80, 80, 255},
        {220, 200, 40, 255},
        {160, 100, 255, 255},
    };

    BMMatch* m = &env->match;
    for (int y = 0; y < m->height; y++) {
        for (int x = 0; x < m->width; x++) {
            int i = bm_idx(m, x, y);
            Color col = empty_c;
            if (m->tiles[i] == BM_TILE_HARD) col = hard_c;
            else if (m->tiles[i] == BM_TILE_SOFT) col = soft_c;
            DrawRectangle(x * cell, y * cell, cell - 1, cell - 1, col);
            if (m->tiles[i] == BM_TILE_EMPTY && m->items[i] != BM_ITEM_NONE) {
                int cx = x * cell + cell / 2;
                int cy = y * cell + cell / 2;
                Color item_c = m->items[i] == BM_ITEM_BOMB ? bomb_item_c
                    : m->items[i] == BM_ITEM_FLAME ? range_item_c : speed_item_c;
                DrawCircle(cx, cy, cell / 4.5f, item_c);
                const char* label = m->items[i] == BM_ITEM_BOMB ? "B"
                    : m->items[i] == BM_ITEM_FLAME ? "R" : "S";
                DrawText(label, cx - 5, cy - 8, 16, BLACK);
            }
            if (m->bomb_here[i]) {
                DrawCircle(x * cell + cell / 2, y * cell + cell / 2, cell / 3.5f, bomb_c);
            }
            if (m->flame_ttl[i] > 0) {
                DrawRectangle(x * cell + 2, y * cell + 2, cell - 5, cell - 5, flame_c);
            }
        }
    }

    for (int a = 0; a < m->num_agents; a++) {
        if (!m->agents[a].alive) continue;
        int x = m->agents[a].x;
        int y = m->agents[a].y;
        DrawCircle(x * cell + cell / 2, y * cell + cell / 2, cell / 2.6f, agent_cols[a]);
        DrawText(TextFormat("%d", a), x * cell + cell / 2 - 4, y * cell + cell / 2 - 8, 14, BLACK);
    }

    int alive = 0;
    for (int a = 0; a < m->num_agents; a++) if (m->agents[a].alive) alive++;
    int hud_y = m->height * cell + 4;
    DrawText(TextFormat(
        "t=%d/%d  alive=%d  winner=%d  fuse=%d  [R reset]",
        m->tick, env->cfg.max_ticks, alive, m->winner, env->cfg.bomb_timer),
        8, hud_y, 16, (Color){241, 241, 241, 255});
    for (int a = 0; a < m->num_agents && a < 2; a++) {
        BMAgent* ag = &m->agents[a];
        int available = bm_max_i(0, ag->max_bombs - ag->bombs_out);
        DrawText(TextFormat(
            "P%d  bombs=%d/%d  deployed=%d  range=%d  speed=%d  %s",
            a, available, ag->max_bombs, ag->bombs_out,
            ag->bomb_range, ag->speed_level, ag->alive ? "alive" : "DEAD"),
            8, hud_y + 20 + 18 * a, 16, agent_cols[a]);
    }
    DrawText("Items: B = bomb capacity   R = blast range   S = movement speed",
        8, hud_y + 58, 15, (Color){210, 220, 220, 255});
    EndDrawing();
}
#else
void puf_render(Env* env) { (void)env; }
#endif // BM_HEADLESS

#else
// GPU builds: Env is a log shell only; match state lives in bomberman.cu
struct Env {
    Log log;
};

void puf_log(Log* log, Dict* out) {
    dict_set(out, "perf", log->perf);
    dict_set(out, "score", log->score);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "kills", log->kills);
    dict_set(out, "self_kills", log->self_kills);
    dict_set(out, "soft_breaks", log->soft_breaks);
    dict_set(out, "pickups", log->pickups);
    dict_set(out, "bomb_pickups", log->bomb_pickups);
    dict_set(out, "range_pickups", log->range_pickups);
    dict_set(out, "speed_pickups", log->speed_pickups);
    dict_set(out, "wins", log->wins);
    dict_set(out, "draws", log->draws);
    dict_set(out, "deaths", log->deaths);
    dict_set(out, "slot_0_score", log->slot_0_score);
    dict_set(out, "slot_1_score", log->slot_1_score);
    dict_set(out, "draw_rate", log->draw_rate);
    dict_set(out, "slot_0_kills", log->slot_0_kills);
    dict_set(out, "slot_0_self_kills", log->slot_0_self_kills);
    dict_set(out, "slot_0_opponent_suicides", log->slot_0_opponent_suicides);
    dict_set(out, "curriculum_stage", log->curriculum_stage);
    dict_set(out, "curriculum_full_game", log->curriculum_full_game);
    dict_set(out, "n", log->n);
}

void puf_render(Env* env) { (void)env; }
#endif // !PUFFER_GPU_ENV
