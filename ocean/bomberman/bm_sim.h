#pragma once

// Pure Bomberman match simulation shared by C and CUDA. One BMMatch is one
// independent game; the hot path uses fixed-size stack/state storage only.

#include "bm_constants.h"

#include <stdint.h>

#if defined(__CUDACC__)
#define BM_HD __host__ __device__ __forceinline__
#define BM_H __host__ __forceinline__
#else
#define BM_HD static inline
#define BM_H static inline
#endif

typedef struct {
    int width;
    int height;
    int num_agents;
    int max_ticks;
    int bomb_timer;
    int flame_duration;
    int frames_per_cell;       // movement period; 1 means one cell every step
    float soft_density;
    float item_chance;
    float reward_soft;
    float reward_pickup;       // paid only when a pickup increases a stat
    float reward_kill;
    float reward_death;
    float reward_self_kill;    // additional penalty when an agent dies to its own flame
    float reward_win;
    float reward_alive;
    float reward_timeout;      // each survivor on a timeout draw
    float reward_bomb_threat;  // bonus only when a bomb earns a credited kill
    float reward_bomb_escape;  // bonus when that credited kill also leaves its owner alive
    float reward_curriculum_aim;      // one-shot: plant a bomb that reaches the foe
    float reward_curriculum_escape;   // one-shot: leave that bomb's blast safely
    float reward_curriculum_progress; // potential shaping toward the finishing cell
    int reverse_curriculum;
    int curriculum_steps;      // optional safety fallback to force normal resets
    int curriculum_window;     // tactical episodes per mastery decision
    float curriculum_success_rate;
    int pillar_mode;
} BMConfig;

typedef struct {
    int alive;
    int x;
    int y;
    int max_bombs;
    int bomb_range;
    int speed_level;
    int move_cd;               // blocked steps before the next movement
    int bombs_out;
    int invuln;
    float ep_return;
    float ep_score;
    int kills;
    int self_kills;
    int soft_breaks;
    int bomb_pickups;
    int range_pickups;
    int speed_pickups;
} BMAgent;

// Eight bytes instead of six 32-bit ints. This substantially improves match
// cache residency on CPU and global-memory traffic for the optional GPU env.
typedef struct {
    uint16_t timer;
    uint8_t x;
    uint8_t y;
    uint8_t owner;
    uint8_t range;
    uint8_t active;
    uint8_t shaping_flags;
} BMBomb;

typedef struct {
    uint8_t tiles[BM_MAX_CELLS];
    uint8_t items[BM_MAX_CELLS];
    uint8_t flame_ttl[BM_MAX_CELLS];
    int8_t flame_owner[BM_MAX_CELLS];
    // 0 = empty; otherwise bomb slot + 1. O(1) fuse/chain lookup.
    uint8_t bomb_here[BM_MAX_CELLS];
    // Earliest predicted explosion arrival. 0=current flame, 255=safe.
    uint8_t danger_time[BM_MAX_CELLS];

    BMAgent agents[BM_MAX_AGENTS];
    BMBomb bombs[BM_MAX_BOMBS];

    int width;
    int height;
    int num_agents;
    int tick;
    int done;
    int winner;
    int curriculum_stage;      // -1 normal reset; otherwise reverse-curriculum stage
    int curriculum_aimed;
    int curriculum_escaped;
    uint32_t rng;
} BMMatch;

// ---- small helpers -------------------------------------------------------
BM_HD int bm_min_i(int a, int b) { return a < b ? a : b; }
BM_HD int bm_max_i(int a, int b) { return a > b ? a : b; }
BM_HD int bm_clamp_i(int x, int lo, int hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

// danger_time uses 255 as the sentinel for "no predicted danger", so live
// fuse values must stay in 1..254. flame_ttl is uint8_t and receives +1 before
// the same-step decrement, so its configured duration is also capped at 254.
BM_HD int bm_bomb_timer(const BMConfig* cfg) {
    return bm_clamp_i(cfg->bomb_timer, 1, BM_DANGER_SAFE - 1);
}

BM_HD int bm_flame_duration(const BMConfig* cfg) {
    return bm_clamp_i(cfg->flame_duration, 1, 254);
}

BM_HD uint32_t bm_xorshift(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    if (x == 0) x = 0xA341316Cu;
    *state = x;
    return x;
}

BM_HD float bm_randf(uint32_t* state) {
    return (bm_xorshift(state) & 0x00FFFFFFu) / (float)0x01000000u;
}

BM_HD int bm_randi(uint32_t* state, int n) {
    return n <= 1 ? 0 : (int)(bm_xorshift(state) % (uint32_t)n);
}

BM_H BMConfig bm_default_config(void) {
    BMConfig c;
    c.width = 13;
    c.height = 11;
    c.num_agents = 2;
    c.max_ticks = 30000;
    c.bomb_timer = 18;
    c.flame_duration = 4;
    c.frames_per_cell = 2;
    c.soft_density = 0.55f;
    c.item_chance = 0.35f;
    c.reward_soft = 0.02f;
    c.reward_pickup = 0.0f;
    c.reward_kill = 1.0f;
    c.reward_death = -1.0f;
    c.reward_self_kill = -5.0f;
    c.reward_win = 0.5f;
    c.reward_alive = 0.0f;
    c.reward_timeout = -0.10f;
    c.reward_bomb_threat = 8.0f;
    c.reward_bomb_escape = 5.0f;
    c.reward_curriculum_aim = 8.0f;
    c.reward_curriculum_escape = 12.0f;
    c.reward_curriculum_progress = 1.0f;
    c.reverse_curriculum = 0;
    c.curriculum_steps = 12000;
    c.curriculum_window = 32;
    c.curriculum_success_rate = 0.60f;
    c.pillar_mode = 1;
    return c;
}

BM_HD int bm_idx(const BMMatch* m, int x, int y) {
    return y * m->width + x;
}

BM_HD int bm_in_bounds(const BMMatch* m, int x, int y) {
    return x >= 0 && y >= 0 && x < m->width && y < m->height;
}

BM_HD int bm_move_period(const BMConfig* cfg, const BMAgent* a) {
    int period = cfg->frames_per_cell - a->speed_level;
    return period < 1 ? 1 : period;
}

BM_HD int bm_move_cooldown_after_move(const BMConfig* cfg, const BMAgent* a) {
    return bm_move_period(cfg, a) - 1;
}

BM_HD int bm_flip_x(int agent_i) { return (agent_i & 1) != 0; }
BM_HD int bm_flip_y(int agent_i) { return (agent_i & 2) != 0; }

BM_HD void bm_world_to_canonical(const BMMatch* m, int viewer,
        int wx, int wy, int* cx, int* cy) {
    *cx = bm_flip_x(viewer) ? (m->width - 1 - wx) : wx;
    *cy = bm_flip_y(viewer) ? (m->height - 1 - wy) : wy;
}

BM_HD void bm_canonical_to_world(const BMMatch* m, int viewer,
        int cx, int cy, int* wx, int* wy) {
    *wx = bm_flip_x(viewer) ? (m->width - 1 - cx) : cx;
    *wy = bm_flip_y(viewer) ? (m->height - 1 - cy) : cy;
}

BM_HD int bm_action_to_world(int viewer, int action) {
    if (action < 0 || action >= BM_NUM_ACTIONS) return BM_ACT_STAY;
    if (bm_flip_x(viewer)) {
        if (action == BM_ACT_LEFT) action = BM_ACT_RIGHT;
        else if (action == BM_ACT_RIGHT) action = BM_ACT_LEFT;
    }
    if (bm_flip_y(viewer)) {
        if (action == BM_ACT_UP) action = BM_ACT_DOWN;
        else if (action == BM_ACT_DOWN) action = BM_ACT_UP;
    }
    return action;
}

BM_HD void bm_action_delta(int action, int* dx, int* dy) {
    *dx = 0;
    *dy = 0;
    if (action == BM_ACT_UP) *dy = -1;
    else if (action == BM_ACT_DOWN) *dy = 1;
    else if (action == BM_ACT_LEFT) *dx = -1;
    else if (action == BM_ACT_RIGHT) *dx = 1;
}

// ---- map/reset -----------------------------------------------------------
BM_HD void bm_pick_spawns(const BMMatch* m, int num_agents, int* out_x, int* out_y) {
    int corners[4][2] = {
        {1, 1},
        {m->width - 2, 1},
        {1, m->height - 2},
        {m->width - 2, m->height - 2},
    };
    for (int a = 0; a < num_agents && a < BM_MAX_AGENTS; a++) {
        out_x[a] = corners[a][0];
        out_y[a] = corners[a][1];
    }
}

BM_HD void bm_clear_spawn_pocket(BMMatch* m, int sx, int sy) {
    // Center plus the two inward corridors are covered by this cross.
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx != 0 && dy != 0) continue;
            int x = sx + dx;
            int y = sy + dy;
            if (!bm_in_bounds(m, x, y)) continue;
            int i = bm_idx(m, x, y);
            if (m->tiles[i] != BM_TILE_HARD) {
                m->tiles[i] = BM_TILE_EMPTY;
                m->items[i] = BM_ITEM_NONE;
            }
        }
    }
}

BM_HD void bm_generate_map(BMMatch* m, const BMConfig* cfg) {
    int n = m->width * m->height;
    for (int i = 0; i < n; i++) {
        m->items[i] = BM_ITEM_NONE;
        m->flame_ttl[i] = 0;
        m->flame_owner[i] = BM_FLAME_OWNER_NONE;
        m->bomb_here[i] = 0;
        m->danger_time[i] = BM_DANGER_SAFE;
    }

    for (int y = 0; y < m->height; y++) {
        for (int x = 0; x < m->width; x++) {
            int i = bm_idx(m, x, y);
            if (x == 0 || y == 0 || x == m->width - 1 || y == m->height - 1) {
                m->tiles[i] = BM_TILE_HARD;
            } else if (cfg->pillar_mode && (x % 2 == 0) && (y % 2 == 0)) {
                m->tiles[i] = BM_TILE_HARD;
            } else {
                m->tiles[i] = BM_TILE_EMPTY;
            }
        }
    }

    for (int y = 1; y < m->height - 1; y++) {
        for (int x = 1; x < m->width - 1; x++) {
            int i = bm_idx(m, x, y);
            if (m->tiles[i] != BM_TILE_EMPTY) continue;
            if (bm_randf(&m->rng) < cfg->soft_density) {
                m->tiles[i] = BM_TILE_SOFT;
                if (bm_randf(&m->rng) < cfg->item_chance) {
                    m->items[i] = (uint8_t)(BM_ITEM_BOMB + bm_randi(&m->rng, 3));
                }
            }
        }
    }

    int sx[BM_MAX_AGENTS], sy[BM_MAX_AGENTS];
    bm_pick_spawns(m, m->num_agents, sx, sy);
    for (int a = 0; a < m->num_agents; a++) {
        bm_clear_spawn_pocket(m, sx[a], sy[a]);
    }
}

BM_HD void bm_reset_match(BMMatch* m, const BMConfig* cfg, uint32_t seed) {
    unsigned char* bytes = (unsigned char*)m;
    for (int i = 0; i < (int)sizeof(BMMatch); i++) bytes[i] = 0;

    m->width = bm_clamp_i(cfg->width, 5, BM_MAX_W);
    m->height = bm_clamp_i(cfg->height, 5, BM_MAX_H);
    if ((m->width & 1) == 0) m->width -= 1;
    if ((m->height & 1) == 0) m->height -= 1;
    m->num_agents = bm_clamp_i(cfg->num_agents, 2, BM_MAX_AGENTS);
    m->rng = seed ? seed : 1u;
    m->winner = -1;
    m->curriculum_stage = -1;

    bm_generate_map(m, cfg);

    int sx[BM_MAX_AGENTS], sy[BM_MAX_AGENTS];
    bm_pick_spawns(m, m->num_agents, sx, sy);
    for (int a = 0; a < m->num_agents; a++) {
        BMAgent* ag = &m->agents[a];
        ag->alive = 1;
        ag->x = sx[a];
        ag->y = sy[a];
        ag->max_bombs = 1;
        ag->bomb_range = 1;
        ag->speed_level = 0;
        ag->move_cd = 0;
        ag->bombs_out = 0;
        ag->invuln = BM_SPAWN_INVULN;
    }
}

BM_HD void bm_refresh_danger(BMMatch* m);
BM_HD int bm_blast_reaches(const BMMatch* m,
    int sx, int sy, int range, int tx, int ty);
BM_HD int bm_bomb_escape(const BMMatch* m, const BMConfig* cfg,
    int agent_i, int* out_margin_num, int* out_margin_den);

// Reverse curriculum for the two-player training setup. Stages 0-1 finish a
// reachable pillar trap. Stages 2-3 teach the ordinary L-pocket breakout first
// from the bomb cell, then from the real corner spawn. Stage 4 is generic bomb
// escape. Stages 5-6 are cleared late-game boards, stage 7 is midgame, stage 8
// is the untouched opening, and stage 9 is the ordinary full game.
BM_HD int bm_apply_reverse_curriculum(BMMatch* m, const BMConfig* cfg,
        float progress) {
    if (!cfg->reverse_curriculum || m->num_agents != 2 || progress >= 1.0f) {
        m->curriculum_stage = -1;
        return 0;
    }
    if (progress < 0.0f) progress = 0.0f;
    float full_game_prob = progress * progress;
    if (bm_randf(&m->rng) < full_game_prob) {
        m->curriculum_stage = -1;
        return 0;
    }

    int hardest_stage = bm_clamp_i(
        (int)((float)BM_CURRICULUM_STAGES * progress),
        0, BM_CURRICULUM_STAGES - 1);
    int stage = hardest_stage;
    // Rehearse solved suffixes so moving the start backward does not erase the
    // finishing behavior. Half of tactical resets use the current frontier;
    // the rest uniformly revisit an earlier stage.
    if (hardest_stage > 0 && bm_randf(&m->rng) < 0.5f) {
        stage = bm_randi(&m->rng, hardest_stage);
    }
    m->curriculum_stage = stage;

    // Generic escape and the final two stages retain the untouched opening.
    if (stage == 4 || stage >= 8) {
        m->tick = 0;
        m->done = 0;
        m->winner = -1;
        bm_refresh_danger(m);
        return 1;
    }

    if (stage == 7) {
        // A reachable mid-game snapshot between the cleared late board and the
        // untouched opening. Randomly remove about two thirds of remaining
        // soft blocks (as if bombed) and collect exposed items. No bombs,
        // inventory upgrades, cooldowns, or invulnerability are fabricated.
        for (int y = 1; y < m->height - 1; y++) {
            for (int x = 1; x < m->width - 1; x++) {
                int i = bm_idx(m, x, y);
                if (m->tiles[i] == BM_TILE_SOFT && bm_randf(&m->rng) < 0.65f) {
                    m->tiles[i] = BM_TILE_EMPTY;
                    m->items[i] = BM_ITEM_NONE;
                }
            }
        }
        for (int a = 0; a < m->num_agents; a++) m->agents[a].invuln = 0;
        m->tick = 0;
        m->done = 0;
        m->winner = -1;
        bm_refresh_danger(m);
        return 1;
    }

    if (stage == 5 || stage == 6) {
        // A reachable late-game snapshot: all destructible blocks have been
        // bombed away or their items collected, while the generated border and
        // pillar topology is untouched. Inventories remain the ordinary
        // turn-zero values and there are no bombs, flames, cooldowns, or spawn
        // invulnerability. Stage 5's opponent stays by curriculum policy;
        // stage 6's opponent moves normally.
        for (int y = 1; y < m->height - 1; y++) {
            for (int x = 1; x < m->width - 1; x++) {
                int i = bm_idx(m, x, y);
                if (m->tiles[i] == BM_TILE_SOFT) m->tiles[i] = BM_TILE_EMPTY;
                m->items[i] = BM_ITEM_NONE;
            }
        }
        int right = bm_randi(&m->rng, 2);
        int bottom = bm_randi(&m->rng, 2);
        BMAgent* learner = &m->agents[0];
        BMAgent* foe = &m->agents[1];
        learner->x = right ? m->width - 2 : 1;
        learner->y = bottom ? m->height - 2 : 1;
        foe->x = learner->x + (right ? -4 : 4);
        foe->y = learner->y;
        learner->move_cd = foe->move_cd = 0;
        learner->bombs_out = foe->bombs_out = 0;
        learner->invuln = foe->invuln = 0;
        m->tick = 0;
        m->done = 0;
        m->winner = -1;
        bm_refresh_danger(m);
        return 1;
    }

    if (stage == 2 || stage == 3) {
        // A normal top-left spawn pocket whose two arms are capped by soft
        // blocks. Bombing the corner is fatal. The valid breakout is to enter
        // one arm, bomb there, retreat through the corner, and turn into the
        // other arm. Stage 2 starts at the bomb cell; stage 3 starts at spawn.
        // No bomb, inventory, cooldown, or invulnerability is fabricated.
        const int use_horizontal_arm = bm_randi(&m->rng, 2);
        const int spawn_x = 1;
        const int spawn_y = 1;
        const int arm_x = use_horizontal_arm ? 2 : 1;
        const int arm_y = use_horizontal_arm ? 1 : 2;
        const int cells[][2] = {
            {1, 1}, {2, 1}, {1, 2},
        };
        for (int i = 0; i < 3; i++) {
            int idx = bm_idx(m, cells[i][0], cells[i][1]);
            m->tiles[idx] = BM_TILE_EMPTY;
            m->items[idx] = BM_ITEM_NONE;
        }
        m->tiles[bm_idx(m, 3, 1)] = BM_TILE_SOFT;
        m->tiles[bm_idx(m, 1, 3)] = BM_TILE_SOFT;
        m->items[bm_idx(m, 3, 1)] = BM_ITEM_NONE;
        m->items[bm_idx(m, 1, 3)] = BM_ITEM_NONE;

        BMAgent* learner = &m->agents[0];
        learner->x = stage == 2 ? arm_x : spawn_x;
        learner->y = stage == 2 ? arm_y : spawn_y;
        learner->bomb_range = 1;
        learner->move_cd = 0;
        learner->bombs_out = 0;
        learner->invuln = 0;
        m->agents[1].move_cd = 0;
        m->agents[1].bombs_out = 0;
        m->agents[1].invuln = 0;
        m->tick = 0;
        m->done = 0;
        m->winner = -1;
        bm_refresh_danger(m);
        return 1;
    }

    // This is a normal pillar-board pattern: the foe stands between two hard
    // pillars, with an ordinary soft block behind it and the learner occupying
    // the only exit. Planting a bomb turns that occupied exit into a bomb-blocked
    // exit. Stage 1 randomizes orientation/location and retains the generated
    // board everywhere else, so the lesson cannot key on one screen position.
    int vertical = stage == 1 ? bm_randi(&m->rng, 2) : 0;
    int dir = stage == 1 && bm_randi(&m->rng, 2) ? -1 : 1;
    int foe_x, foe_y, learner_x, learner_y, soft_x, soft_y;
    if (!vertical) {
        int choices = (m->width - 7) / 2;
        foe_x = stage == 0 ? 4 : 4 + 2 * bm_randi(&m->rng, choices > 0 ? choices : 1);
        foe_y = stage == 0 ? 3 : 1 + 2 * bm_randi(&m->rng, (m->height - 1) / 2);
        learner_x = foe_x + dir;
        learner_y = foe_y;
        soft_x = foe_x - dir;
        soft_y = foe_y;
        m->tiles[bm_idx(m, foe_x, foe_y - 1)] = BM_TILE_HARD;
        m->tiles[bm_idx(m, foe_x, foe_y + 1)] = BM_TILE_HARD;
    } else {
        foe_x = 1 + 2 * bm_randi(&m->rng, (m->width - 1) / 2);
        int choices = (m->height - 7) / 2;
        foe_y = 4 + 2 * bm_randi(&m->rng, choices > 0 ? choices : 1);
        learner_x = foe_x;
        learner_y = foe_y + dir;
        soft_x = foe_x;
        soft_y = foe_y - dir;
        m->tiles[bm_idx(m, foe_x - 1, foe_y)] = BM_TILE_HARD;
        m->tiles[bm_idx(m, foe_x + 1, foe_y)] = BM_TILE_HARD;
    }
    int local_xy[][2] = {
        {foe_x, foe_y}, {learner_x, learner_y},
        {learner_x + (learner_x - foe_x), learner_y + (learner_y - foe_y)},
    };
    for (int i = 0; i < 3; i++) {
        int idx = bm_idx(m, local_xy[i][0], local_xy[i][1]);
        m->tiles[idx] = BM_TILE_EMPTY;
        m->items[idx] = BM_ITEM_NONE;
    }
    m->tiles[bm_idx(m, soft_x, soft_y)] = BM_TILE_SOFT;
    m->items[bm_idx(m, soft_x, soft_y)] = BM_ITEM_NONE;

    BMAgent* learner = &m->agents[0];
    BMAgent* foe = &m->agents[1];
    learner->x = learner_x;
    learner->y = learner_y;
    learner->bomb_range = 1;
    learner->move_cd = 0;
    learner->bombs_out = 0;
    learner->invuln = 0;
    foe->x = foe_x;
    foe->y = foe_y;
    foe->move_cd = 0;
    foe->bombs_out = 0;
    foe->invuln = 0;
    m->tick = 0;
    m->done = 0;
    m->winner = -1;
    bm_refresh_danger(m);
    return 1;
}

// ---- bomb/danger system --------------------------------------------------
BM_HD int bm_bomb_slot_at_idx(const BMMatch* m, int i) {
    int encoded = (int)m->bomb_here[i];
    if (encoded == 0) return -1;
    int slot = encoded - 1;
    if (slot < 0 || slot >= BM_MAX_BOMBS || !m->bombs[slot].active) return -1;
    return slot;
}

BM_HD int bm_bomb_slot_at(const BMMatch* m, int x, int y) {
    return bm_in_bounds(m, x, y) ? bm_bomb_slot_at_idx(m, bm_idx(m, x, y)) : -1;
}

BM_HD void bm_set_danger(BMMatch* m, int x, int y, int time) {
    if (!bm_in_bounds(m, x, y)) return;
    int i = bm_idx(m, x, y);
    int t = bm_clamp_i(time, 0, BM_DANGER_SAFE - 1);
    if (t < (int)m->danger_time[i]) m->danger_time[i] = (uint8_t)t;
}

BM_HD void bm_refresh_danger(BMMatch* m) {
    int n = m->width * m->height;
    for (int i = 0; i < n; i++) {
        m->danger_time[i] = m->flame_ttl[i] ? 0 : BM_DANGER_SAFE;
    }

    uint16_t effective[BM_MAX_BOMBS];
    for (int b = 0; b < BM_MAX_BOMBS; b++) {
        effective[b] = m->bombs[b].active ? m->bombs[b].timer : UINT16_MAX;
    }

    // Relax fuse times through chain-reaction edges. Typical matches have 0-3
    // bombs, so this converges in one or two tiny passes.
    const int dxs[4] = {0, 0, -1, 1};
    const int dys[4] = {-1, 1, 0, 0};
    for (int pass = 0; pass < BM_MAX_BOMBS; pass++) {
        int changed = 0;
        for (int b = 0; b < BM_MAX_BOMBS; b++) {
            const BMBomb* bomb = &m->bombs[b];
            if (!bomb->active || effective[b] == UINT16_MAX) continue;
            for (int d = 0; d < 4; d++) {
                for (int r = 1; r <= (int)bomb->range; r++) {
                    int x = (int)bomb->x + dxs[d] * r;
                    int y = (int)bomb->y + dys[d] * r;
                    if (!bm_in_bounds(m, x, y)) break;
                    int i = bm_idx(m, x, y);
                    if (m->tiles[i] == BM_TILE_HARD) break;
                    int target = bm_bomb_slot_at_idx(m, i);
                    if (target >= 0 && effective[target] > effective[b]) {
                        effective[target] = effective[b];
                        changed = 1;
                    }
                    if (m->tiles[i] == BM_TILE_SOFT) break;
                }
            }
        }
        if (!changed) break;
    }

    for (int b = 0; b < BM_MAX_BOMBS; b++) {
        const BMBomb* bomb = &m->bombs[b];
        if (!bomb->active || effective[b] == UINT16_MAX) continue;
        int t = (int)effective[b];
        bm_set_danger(m, bomb->x, bomb->y, t);
        for (int d = 0; d < 4; d++) {
            for (int r = 1; r <= (int)bomb->range; r++) {
                int x = (int)bomb->x + dxs[d] * r;
                int y = (int)bomb->y + dys[d] * r;
                if (!bm_in_bounds(m, x, y)) break;
                int i = bm_idx(m, x, y);
                if (m->tiles[i] == BM_TILE_HARD) break;
                bm_set_danger(m, x, y, t);
                if (m->tiles[i] == BM_TILE_SOFT) break;
            }
        }
    }
}

BM_HD void bm_place_flame(BMMatch* m, int x, int y, int owner, int ttl) {
    if (!bm_in_bounds(m, x, y)) return;
    int i = bm_idx(m, x, y);
    if (m->tiles[i] == BM_TILE_HARD) return;
    int old_ttl = (int)m->flame_ttl[i];
    if (ttl > old_ttl) {
        m->flame_ttl[i] = (uint8_t)bm_clamp_i(ttl, 1, 255);
        m->flame_owner[i] = (int8_t)owner;
    } else if (ttl == old_ttl && old_ttl > 0 && m->flame_owner[i] != owner) {
        // Equal simultaneous blasts from different players are intentionally
        // uncredited instead of depending on bomb-slot/agent iteration order.
        m->flame_owner[i] = BM_FLAME_OWNER_MIXED;
    }
}

BM_HD void bm_destroy_soft(BMMatch* m, const BMConfig* cfg,
        int x, int y, int owner, float* rewards) {
    int i = bm_idx(m, x, y);
    if (m->tiles[i] != BM_TILE_SOFT) return;
    m->tiles[i] = BM_TILE_EMPTY;
    if (owner >= 0 && owner < m->num_agents) {
        rewards[owner] += cfg->reward_soft;
        m->agents[owner].ep_return += cfg->reward_soft;
        m->agents[owner].ep_score += cfg->reward_soft;
        m->agents[owner].soft_breaks += 1;
    }
}

BM_HD void bm_explode_bomb(BMMatch* m, const BMConfig* cfg,
        int bomb_i, float* rewards) {
    BMBomb* bomb = &m->bombs[bomb_i];
    if (!bomb->active) return;

    int ox = bomb->x;
    int oy = bomb->y;
    int owner = bomb->owner;
    int range = bomb->range;
    int ttl = bm_flame_duration(cfg) + 1;
    int bi = bm_idx(m, ox, oy);

    bomb->active = 0;
    if ((int)m->bomb_here[bi] == bomb_i + 1) m->bomb_here[bi] = 0;
    if (owner >= 0 && owner < m->num_agents && m->agents[owner].bombs_out > 0) {
        m->agents[owner].bombs_out -= 1;
    }

    bm_place_flame(m, ox, oy, owner, ttl);
    const int dxs[4] = {0, 0, -1, 1};
    const int dys[4] = {-1, 1, 0, 0};
    for (int d = 0; d < 4; d++) {
        for (int r = 1; r <= range; r++) {
            int x = ox + dxs[d] * r;
            int y = oy + dys[d] * r;
            if (!bm_in_bounds(m, x, y)) break;
            int i = bm_idx(m, x, y);
            if (m->tiles[i] == BM_TILE_HARD) break;

            bm_place_flame(m, x, y, owner, ttl);
            int chained = bm_bomb_slot_at_idx(m, i);
            if (chained >= 0) m->bombs[chained].timer = 0;

            if (m->tiles[i] == BM_TILE_SOFT) {
                bm_destroy_soft(m, cfg, x, y, owner, rewards);
                break;
            }
        }
    }
}

BM_HD void bm_tick_bombs(BMMatch* m, const BMConfig* cfg, float* rewards) {
    // Every active fuse advances exactly once. The old multi-pass loop also
    // decremented unrelated bombs once per chain pass, causing premature RNG-like
    // explosions and making planning from observations impossible.
    for (int b = 0; b < BM_MAX_BOMBS; b++) {
        if (m->bombs[b].active && m->bombs[b].timer > 0) {
            m->bombs[b].timer -= 1;
        }
    }

    for (int pass = 0; pass < BM_MAX_BOMBS; pass++) {
        int exploded = 0;
        for (int b = 0; b < BM_MAX_BOMBS; b++) {
            if (m->bombs[b].active && m->bombs[b].timer == 0) {
                bm_explode_bomb(m, cfg, b, rewards);
                exploded = 1;
            }
        }
        if (!exploded) break;
    }
}

BM_HD void bm_tick_flames(BMMatch* m) {
    int n = m->width * m->height;
    for (int i = 0; i < n; i++) {
        if (m->flame_ttl[i] == 0) continue;
        m->flame_ttl[i] -= 1;
        if (m->flame_ttl[i] == 0) m->flame_owner[i] = BM_FLAME_OWNER_NONE;
    }
}

// ---- agents/actions ------------------------------------------------------
BM_HD int bm_has_free_bomb_slot(const BMMatch* m) {
    for (int b = 0; b < BM_MAX_BOMBS; b++) {
        if (!m->bombs[b].active) return 1;
    }
    return 0;
}

BM_HD int bm_try_place_bomb(BMMatch* m, const BMConfig* cfg, int agent_i) {
    BMAgent* a = &m->agents[agent_i];
    if (!a->alive || a->bombs_out >= a->max_bombs) return 0;
    int i = bm_idx(m, a->x, a->y);
    if (m->bomb_here[i]) return 0;

    int slot = -1;
    for (int b = 0; b < BM_MAX_BOMBS; b++) {
        if (!m->bombs[b].active) {
            slot = b;
            break;
        }
    }
    if (slot < 0) return 0;

    BMBomb* bomb = &m->bombs[slot];
    bomb->active = 1;
    bomb->x = (uint8_t)a->x;
    bomb->y = (uint8_t)a->y;
    bomb->owner = (uint8_t)agent_i;
    bomb->range = (uint8_t)bm_clamp_i(a->bomb_range, 1, BM_MAX_FLAME_RANGE);
    bomb->shaping_flags = 0;
    // +1 compensates for the fuse tick later in this same environment step, so
    // bomb_timer is the actual number of future decision steps before explosion.
    bomb->timer = (uint16_t)(bm_bomb_timer(cfg) + 1);
    m->bomb_here[i] = (uint8_t)(slot + 1);
    a->bombs_out += 1;
    return 1;
}

BM_HD int bm_cell_blocked_static(const BMMatch* m, int x, int y) {
    if (!bm_in_bounds(m, x, y)) return 1;
    int i = bm_idx(m, x, y);
    return m->tiles[i] == BM_TILE_HARD || m->tiles[i] == BM_TILE_SOFT || m->bomb_here[i];
}

BM_HD int bm_agent_at(const BMMatch* m, int x, int y, int except) {
    for (int a = 0; a < m->num_agents; a++) {
        if (a == except || !m->agents[a].alive) continue;
        if (m->agents[a].x == x && m->agents[a].y == y) return a;
    }
    return -1;
}

BM_HD int bm_action_legal_world(const BMMatch* m, int agent_i, int action) {
    const BMAgent* a = &m->agents[agent_i];
    if (!a->alive) return action == BM_ACT_STAY;
    if (action == BM_ACT_STAY) return 1;
    if (action == BM_ACT_BOMB) {
        if (m->curriculum_stage >= 0
                && m->curriculum_stage < BM_CURRICULUM_STAGES - 1
                && agent_i > 0) return 0;
        int i = bm_idx(m, a->x, a->y);
        return a->bombs_out < a->max_bombs && !m->bomb_here[i] && bm_has_free_bomb_slot(m);
    }
    if (a->move_cd > 0) return 0;
    int dx, dy;
    bm_action_delta(action, &dx, &dy);
    if (dx == 0 && dy == 0) return 0;
    int nx = a->x + dx;
    int ny = a->y + dy;
    return !bm_cell_blocked_static(m, nx, ny) && bm_agent_at(m, nx, ny, agent_i) < 0;
}

BM_HD int bm_action_legal(const BMMatch* m, int agent_i, int canonical_action) {
    return bm_action_legal_world(m, agent_i,
        bm_action_to_world(agent_i, canonical_action));
}

BM_HD void bm_pickup(BMMatch* m, const BMConfig* cfg, int agent_i,
        float* rewards) {
    BMAgent* a = &m->agents[agent_i];
    int i = bm_idx(m, a->x, a->y);
    uint8_t item = m->items[i];
    if (item == BM_ITEM_NONE || m->tiles[i] != BM_TILE_EMPTY) return;
    int upgraded = 0;
    if (item == BM_ITEM_BOMB && a->max_bombs < BM_MAX_BOMBS_PER_AGENT) {
        a->max_bombs += 1;
        a->bomb_pickups += 1;
        upgraded = 1;
    } else if (item == BM_ITEM_FLAME && a->bomb_range < BM_MAX_FLAME_RANGE) {
        a->bomb_range += 1;
        a->range_pickups += 1;
        upgraded = 1;
    } else if (item == BM_ITEM_SPEED && a->speed_level < BM_MAX_SPEED_LEVEL) {
        a->speed_level += 1;
        a->speed_pickups += 1;
        upgraded = 1;
    }
    if (upgraded) {
        rewards[agent_i] += cfg->reward_pickup;
        a->ep_return += cfg->reward_pickup;
        a->ep_score += cfg->reward_pickup;
    }
    m->items[i] = BM_ITEM_NONE;
}

BM_HD void bm_resolve_actions(BMMatch* m, const BMConfig* cfg,
        const int* canonical_actions, float* rewards) {
    int na = bm_clamp_i(m->num_agents, 0, BM_MAX_AGENTS);
    int actions[BM_MAX_AGENTS];
    int tx[BM_MAX_AGENTS];
    int ty[BM_MAX_AGENTS];
    uint8_t wants_move[BM_MAX_AGENTS];
    uint8_t move_ok[BM_MAX_AGENTS];

    for (int a = 0; a < BM_MAX_AGENTS; a++) {
        if (a >= na) break;
        actions[a] = bm_action_to_world(a, canonical_actions[a]);
        if (m->curriculum_stage == 5 && a > 0) {
            actions[a] = BM_ACT_STAY;
        } else if (m->curriculum_stage >= 0
                && m->curriculum_stage < BM_CURRICULUM_STAGES - 1
                && a > 0 && actions[a] == BM_ACT_BOMB) {
            actions[a] = BM_ACT_STAY;
        }
        tx[a] = m->agents[a].x;
        ty[a] = m->agents[a].y;
        wants_move[a] = 0;
        move_ok[a] = 0;
    }

    // Bomb placement is simultaneous and never consumes a movement in this
    // single-action action space. It does advance an existing cooldown.
    for (int a = 0; a < BM_MAX_AGENTS; a++) {
        if (a >= na) break;
        BMAgent* ag = &m->agents[a];
        if (!ag->alive || actions[a] != BM_ACT_BOMB) continue;
        bm_try_place_bomb(m, cfg, a);
        if (ag->move_cd > 0) ag->move_cd -= 1;
    }

    // Build movement intents from the same pre-move board. This removes the old
    // lower-agent-index advantage on contested cells and swaps.
    for (int a = 0; a < BM_MAX_AGENTS; a++) {
        if (a >= na) break;
        BMAgent* ag = &m->agents[a];
        if (!ag->alive || actions[a] == BM_ACT_BOMB) continue;
        if (ag->move_cd > 0) {
            ag->move_cd -= 1;
            continue;
        }
        int dx, dy;
        bm_action_delta(actions[a], &dx, &dy);
        if (dx == 0 && dy == 0) continue;
        int nx = ag->x + dx;
        int ny = ag->y + dy;
        if (bm_cell_blocked_static(m, nx, ny)) continue;
        // Conservatively disallow entering any cell occupied at decision time,
        // including swaps. This is deterministic and index-neutral.
        if (bm_agent_at(m, nx, ny, a) >= 0) continue;
        tx[a] = nx;
        ty[a] = ny;
        wants_move[a] = 1;
        move_ok[a] = 1;
    }

    for (int a = 0; a < BM_MAX_AGENTS; a++) {
        if (a >= na) break;
        if (!wants_move[a]) continue;
        for (int b = a + 1; b < BM_MAX_AGENTS; b++) {
            if (b >= na) break;
            if (wants_move[b] && tx[a] == tx[b] && ty[a] == ty[b]) {
                move_ok[a] = 0;
                move_ok[b] = 0;
            }
        }
    }

    for (int a = 0; a < BM_MAX_AGENTS; a++) {
        if (a >= na) break;
        if (!move_ok[a]) continue;
        BMAgent* ag = &m->agents[a];
        ag->x = tx[a];
        ag->y = ty[a];
        ag->move_cd = bm_move_cooldown_after_move(cfg, ag);
    }
    for (int a = 0; a < BM_MAX_AGENTS; a++) {
        if (a >= na) break;
        if (move_ok[a]) bm_pickup(m, cfg, a, rewards);
    }
}

BM_HD void bm_resolve_deaths(BMMatch* m, const BMConfig* cfg, float* rewards) {
    uint8_t dies[BM_MAX_AGENTS];
    int killer[BM_MAX_AGENTS];
    for (int a = 0; a < BM_MAX_AGENTS; a++) {
        dies[a] = 0;
        killer[a] = BM_FLAME_OWNER_NONE;
    }

    // Detect everyone from one board snapshot first; kill credit no longer
    // depends on whether the killer happened to be processed before dying.
    for (int a = 0; a < m->num_agents; a++) {
        BMAgent* ag = &m->agents[a];
        if (!ag->alive) continue;
        if (ag->invuln > 0) {
            ag->invuln -= 1;
            continue;
        }
        int i = bm_idx(m, ag->x, ag->y);
        if (!m->flame_ttl[i]) continue;
        dies[a] = 1;
        killer[a] = (int)m->flame_owner[i];
    }

    for (int a = 0; a < m->num_agents; a++) {
        if (!dies[a]) continue;
        m->agents[a].alive = 0;
        rewards[a] += cfg->reward_death;
        m->agents[a].ep_return += cfg->reward_death;
        if (killer[a] == a) {
            rewards[a] += cfg->reward_self_kill;
            m->agents[a].ep_return += cfg->reward_self_kill;
            m->agents[a].self_kills += 1;
        }
    }

    for (int victim = 0; victim < m->num_agents; victim++) {
        int k = killer[victim];
        if (!dies[victim] || k < 0 || k >= m->num_agents || k == victim) continue;
        // These bonuses are deliberately coupled to a real credited kill.
        // Paying them when a bomb merely lines up with or is escaped from by
        // an enemy is exploitable: policies can repeatedly plant harmless
        // bombs and collect shaping without ever completing a kill.
        float kill_reward = cfg->reward_kill + cfg->reward_bomb_threat;
        if (!dies[k]) kill_reward += cfg->reward_bomb_escape;
        rewards[k] += kill_reward;
        m->agents[k].ep_return += kill_reward;
        m->agents[k].ep_score += cfg->reward_kill;
        m->agents[k].kills += 1;
    }
}

BM_HD void bm_check_done(BMMatch* m, const BMConfig* cfg,
        float* rewards, float* terminals) {
    int alive_count = 0;
    int last = -1;
    for (int a = 0; a < m->num_agents; a++) {
        if (m->agents[a].alive) {
            alive_count += 1;
            last = a;
        }
    }

    // Stages 2-4 are short, valid-state escape lessons. End once slot 0 has
    // genuinely left its own bomb's blast; this is an episode boundary, not a
    // fabricated board state or a credited game win.
    if (m->curriculum_stage >= 2 && m->curriculum_stage <= 4
            && m->curriculum_escaped
            && m->agents[0].alive) {
        m->done = 1;
        m->winner = -1;
        for (int a = 0; a < m->num_agents; a++) terminals[a] = 1.0f;
        return;
    }

    int timeout = cfg->max_ticks > 0 && m->tick >= cfg->max_ticks;
    if (alive_count > 1 && !timeout) return;

    m->done = 1;
    if (alive_count == 1) {
        m->winner = last;
        rewards[last] += cfg->reward_win;
        m->agents[last].ep_return += cfg->reward_win;
        m->agents[last].ep_score += cfg->reward_win;
    } else {
        m->winner = -1;
        if (timeout && alive_count > 1) {
            for (int a = 0; a < m->num_agents; a++) {
                if (!m->agents[a].alive) continue;
                rewards[a] += cfg->reward_timeout;
                m->agents[a].ep_return += cfg->reward_timeout;
            }
        }
    }
    for (int a = 0; a < m->num_agents; a++) terminals[a] = 1.0f;
}

BM_HD void bm_step_match(BMMatch* m, const BMConfig* cfg,
        const int* actions, float* rewards, float* terminals) {
    if (m->done) {
        for (int a = 0; a < m->num_agents; a++) {
            rewards[a] = 0.0f;
            terminals[a] = 1.0f;
        }
        return;
    }

    m->tick += 1;
    for (int a = 0; a < m->num_agents; a++) {
        rewards[a] = 0.0f;
        terminals[a] = 0.0f;
        if (m->agents[a].alive && cfg->reward_alive != 0.0f) {
            rewards[a] = cfg->reward_alive;
            m->agents[a].ep_return += cfg->reward_alive;
        }
    }

    int old_finish_dist = 0;
    int aimed_now = 0;
    int safe_escape_bomb_now = 0;
    if (m->curriculum_stage >= 0 && m->curriculum_stage < 2) {
        BMAgent* learner = &m->agents[0];
        old_finish_dist = (learner->x > 5 ? learner->x - 5 : 5 - learner->x)
            + (learner->y > 3 ? learner->y - 3 : 3 - learner->y);
        int world_action = bm_action_to_world(0, actions[0]);
        aimed_now = !m->curriculum_aimed && world_action == BM_ACT_BOMB
            && bm_action_legal_world(m, 0, BM_ACT_BOMB)
            && bm_blast_reaches(m, learner->x, learner->y,
                learner->bomb_range, m->agents[1].x, m->agents[1].y);
    }
    if (m->curriculum_stage >= 2 && m->curriculum_stage <= 4
            && !m->curriculum_aimed
            && bm_action_to_world(0, actions[0]) == BM_ACT_BOMB
            && bm_action_legal_world(m, 0, BM_ACT_BOMB)) {
        int margin_num = 0, margin_den = 1;
        safe_escape_bomb_now = bm_bomb_escape(
            m, cfg, 0, &margin_num, &margin_den);
    }

    bm_resolve_actions(m, cfg, actions, rewards);

    if (safe_escape_bomb_now) {
        m->curriculum_aimed = 1;
        rewards[0] += cfg->reward_curriculum_aim;
        m->agents[0].ep_return += cfg->reward_curriculum_aim;
    }

    // Stages 2-4 are explicit bomb-escape lessons. Ordinary games do not
    // pay for merely placing or escaping a bomb; their bonuses are coupled to
    // credited kills in bm_resolve_deaths above.
    if (m->curriculum_stage >= 2 && m->curriculum_stage <= 4
            && !m->curriculum_escaped) {
        BMAgent* learner = &m->agents[0];
        for (int b = 0; b < BM_MAX_BOMBS; b++) {
            BMBomb* bomb = &m->bombs[b];
            if (!bomb->active || bomb->owner != 0 || !learner->alive) continue;
            if (bm_blast_reaches(m, bomb->x, bomb->y, bomb->range,
                    learner->x, learner->y)) continue;
            m->curriculum_escaped = 1;
            rewards[0] += cfg->reward_bomb_escape;
            learner->ep_return += cfg->reward_bomb_escape;
            break;
        }
    }

    if (m->curriculum_stage >= 0 && m->curriculum_stage < 2) {
        BMAgent* learner = &m->agents[0];
        if (!m->curriculum_aimed && cfg->reward_curriculum_progress != 0.0f) {
            int new_finish_dist = (learner->x > 5 ? learner->x - 5 : 5 - learner->x)
                + (learner->y > 3 ? learner->y - 3 : 3 - learner->y);
            float shaped = cfg->reward_curriculum_progress
                * (float)(old_finish_dist - new_finish_dist);
            rewards[0] += shaped;
            learner->ep_return += shaped;
        }
        if (aimed_now) {
            m->curriculum_aimed = 1;
            rewards[0] += cfg->reward_curriculum_aim;
            learner->ep_return += cfg->reward_curriculum_aim;
        }
        if (m->curriculum_aimed && !m->curriculum_escaped) {
            int aimed_bomb = 0;
            int learner_threatened = 0;
            for (int b = 0; b < BM_MAX_BOMBS; b++) {
                BMBomb* bomb = &m->bombs[b];
                if (!bomb->active || bomb->owner != 0) continue;
                if (bm_blast_reaches(m, bomb->x, bomb->y, bomb->range,
                        m->agents[1].x, m->agents[1].y)) {
                    aimed_bomb = 1;
                    if (bm_blast_reaches(m, bomb->x, bomb->y, bomb->range,
                            learner->x, learner->y)) {
                        learner_threatened = 1;
                    }
                }
            }
            if (aimed_bomb && !learner_threatened) {
                m->curriculum_escaped = 1;
                rewards[0] += cfg->reward_curriculum_escape;
                learner->ep_return += cfg->reward_curriculum_escape;
            }
        }
    }
    bm_tick_bombs(m, cfg, rewards);
    bm_resolve_deaths(m, cfg, rewards);
    bm_tick_flames(m);
    bm_check_done(m, cfg, rewards, terminals);
    bm_refresh_danger(m);
}

// ---- tactical observation helpers --------------------------------------
BM_HD int bm_blast_reaches(const BMMatch* m, int sx, int sy, int range, int tx, int ty) {
    if (sx == tx && sy == ty) return 1;
    int dx = 0, dy = 0, dist = 0;
    if (sx == tx) {
        dy = ty > sy ? 1 : -1;
        dist = ty > sy ? ty - sy : sy - ty;
    } else if (sy == ty) {
        dx = tx > sx ? 1 : -1;
        dist = tx > sx ? tx - sx : sx - tx;
    } else {
        return 0;
    }
    if (dist > range) return 0;
    for (int r = 1; r <= dist; r++) {
        int x = sx + dx * r;
        int y = sy + dy * r;
        int i = bm_idx(m, x, y);
        if (m->tiles[i] == BM_TILE_HARD) return 0;
        if (m->tiles[i] == BM_TILE_SOFT) return r == dist;
    }
    return 1;
}

BM_HD void bm_prospective_bomb_counts(const BMMatch* m, int agent_i,
        int* foe_hits, int* soft_hits) {
    const BMAgent* me = &m->agents[agent_i];
    *foe_hits = 0;
    *soft_hits = 0;
    for (int a = 0; a < m->num_agents; a++) {
        if (a == agent_i || !m->agents[a].alive) continue;
        if (bm_blast_reaches(m, me->x, me->y, me->bomb_range,
                m->agents[a].x, m->agents[a].y)) {
            *foe_hits += 1;
        }
    }

    const int dxs[4] = {0, 0, -1, 1};
    const int dys[4] = {-1, 1, 0, 0};
    for (int d = 0; d < 4; d++) {
        for (int r = 1; r <= me->bomb_range; r++) {
            int x = me->x + dxs[d] * r;
            int y = me->y + dys[d] * r;
            if (!bm_in_bounds(m, x, y)) break;
            int tile = m->tiles[bm_idx(m, x, y)];
            if (tile == BM_TILE_HARD) break;
            if (tile == BM_TILE_SOFT) {
                *soft_hits += 1;
                break;
            }
        }
    }
}

BM_HD int bm_bomb_escape(const BMMatch* m, const BMConfig* cfg, int agent_i,
        int* out_margin_num, int* out_margin_den) {
    const BMAgent* me = &m->agents[agent_i];
    int start = bm_idx(m, me->x, me->y);
    int16_t dist[BM_MAX_CELLS];
    uint16_t queue[BM_MAX_CELLS];
    int n = m->width * m->height;
    for (int i = 0; i < n; i++) dist[i] = -1;
    int qh = 0, qt = 0;
    dist[start] = 0;
    queue[qt++] = (uint16_t)start;

    int deadline = bm_bomb_timer(cfg);
    int first_move = bm_max_i(1, me->move_cd);
    int period = bm_move_period(cfg, me);
    const int dxs[4] = {0, 0, -1, 1};
    const int dys[4] = {-1, 1, 0, 0};

    while (qh < qt) {
        int cur = queue[qh++];
        int x = cur % m->width;
        int y = cur / m->width;
        int d0 = dist[cur];
        for (int k = 0; k < 4; k++) {
            int nx = x + dxs[k];
            int ny = y + dys[k];
            if (!bm_in_bounds(m, nx, ny)) continue;
            int ni = bm_idx(m, nx, ny);
            if (dist[ni] >= 0 || bm_cell_blocked_static(m, nx, ny)) continue;
            if (bm_agent_at(m, nx, ny, agent_i) >= 0) continue;

            int moves = d0 + 1;
            int arrival = first_move + (moves - 1) * period;
            if (arrival > deadline) continue;
            int danger = m->danger_time[ni];
            if (danger != BM_DANGER_SAFE && arrival >= danger) continue;

            dist[ni] = (int16_t)moves;
            queue[qt++] = (uint16_t)ni;
            if (!bm_blast_reaches(m, me->x, me->y, me->bomb_range, nx, ny)) {
                *out_margin_num = deadline - arrival;
                *out_margin_den = deadline;
                return 1;
            }
        }
    }

    *out_margin_num = 0;
    *out_margin_den = deadline;
    return 0;
}

// ---- observations ------------------------------------------------------
BM_HD float bm_obs_ratio(int num, int den) {
    if (num <= 0 || den <= 0) return 0.0f;
    if (num >= den) return 1.0f;
    return (float)num / (float)den;
}

BM_HD float bm_obs_urgency(int timer, int max_timer) {
    if (timer < 0) return 0.0f;
    if (timer == 0) return 1.0f;
    max_timer = bm_max_i(max_timer, 1);
    timer = bm_clamp_i(timer, 1, max_timer);
    return (float)(max_timer - timer + 1) / (float)max_timer;
}

BM_HD void bm_write_obs_mask(const BMMatch* m, const BMConfig* cfg,
        int agent_i, float* obs, uint8_t* action_mask) {
    const BMAgent* me = &m->agents[agent_i];

    // Cell-major canonical board. Occupancy channels are filled afterward,
    // avoiding a 4-agent scan for every cell.
    for (int cy = 0; cy < BM_MAX_H; cy++) {
        for (int cx = 0; cx < BM_MAX_W; cx++) {
            int base = (cy * BM_MAX_W + cx) * BM_CELL_CH;
            for (int c = 0; c < BM_CELL_CH; c++) obs[base + c] = 0.0f;
            if (cx >= m->width || cy >= m->height) {
                obs[base + 0] = 1.0f;
                continue;
            }

            int x, y;
            bm_canonical_to_world(m, agent_i, cx, cy, &x, &y);
            int i = bm_idx(m, x, y);
            obs[base + 0] = m->tiles[i] == BM_TILE_HARD ? 1.0f : 0.0f;
            obs[base + 1] = m->tiles[i] == BM_TILE_SOFT ? 1.0f : 0.0f;
            obs[base + 2] = m->tiles[i] == BM_TILE_EMPTY
                ? (float)m->items[i] * (1.0f / 3.0f) : 0.0f;

            int slot = bm_bomb_slot_at_idx(m, i);
            if (slot >= 0) {
                const BMBomb* bomb = &m->bombs[slot];
                float fuse = bm_obs_urgency((int)bomb->timer, bm_bomb_timer(cfg));
                obs[base + (bomb->owner == agent_i ? 3 : 4)] = fuse;
            }
            int danger = m->danger_time[i];
            obs[base + 5] = danger == BM_DANGER_SAFE
                ? 0.0f : bm_obs_urgency(danger, bm_bomb_timer(cfg));
        }
    }

    for (int a = 0; a < m->num_agents; a++) {
        if (!m->agents[a].alive) continue;
        int cx, cy;
        bm_world_to_canonical(m, agent_i, m->agents[a].x, m->agents[a].y, &cx, &cy);
        int base = (cy * BM_MAX_W + cx) * BM_CELL_CH;
        obs[base + (a == agent_i ? 6 : 7)] = 1.0f;
    }

    uint8_t legal[BM_NUM_ACTIONS];
    int legal_moves = 0;
    for (int a = 0; a < BM_NUM_ACTIONS; a++) {
        legal[a] = (uint8_t)(bm_action_legal(m, agent_i, a) ? 1 : 0);
        if (action_mask) action_mask[a] = legal[a];
        if (a >= BM_ACT_UP && a <= BM_ACT_RIGHT) legal_moves += legal[a];
    }

    int nearest = m->width + m->height;
    int alive_foes = 0;
    for (int a = 0; a < m->num_agents; a++) {
        if (a == agent_i || !m->agents[a].alive) continue;
        int dx = m->agents[a].x - me->x;
        int dy = m->agents[a].y - me->y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        nearest = bm_min_i(nearest, dx + dy);
        alive_foes += 1;
    }

    int foe_hits = 0, soft_hits = 0;
    int escape = 0, margin_num = 0, margin_den = 1;
    if (legal[BM_ACT_BOMB]) {
        bm_prospective_bomb_counts(m, agent_i, &foe_hits, &soft_hits);
        escape = bm_bomb_escape(m, cfg, agent_i, &margin_num, &margin_den);
    }

    int o = BM_MAX_CELLS * BM_CELL_CH;
    obs[o++] = bm_obs_ratio(m->tick, bm_max_i(cfg->max_ticks, 1));
    int here_danger = m->danger_time[bm_idx(m, me->x, me->y)];
    obs[o++] = here_danger == BM_DANGER_SAFE ? 0.0f
        : bm_obs_urgency(here_danger, bm_bomb_timer(cfg));
    int max_dist = bm_max_i(1, m->width + m->height - 2);
    obs[o++] = alive_foes ? 1.0f - bm_obs_ratio(nearest, max_dist) : 0.0f;
    obs[o++] = bm_obs_ratio(bm_max_i(0, me->max_bombs - me->bombs_out),
        BM_MAX_BOMBS_PER_AGENT);
    obs[o++] = foe_hits > 0 ? 1.0f : 0.0f;
    obs[o++] = bm_obs_ratio(soft_hits, 4);
    obs[o++] = escape ? 1.0f : 0.0f;
    obs[o++] = escape ? bm_obs_ratio(margin_num, margin_den) : 0.0f;
    for (int a = 0; a < BM_NUM_ACTIONS; a++) obs[o++] = legal[a] ? 1.0f : 0.0f;
    obs[o++] = bm_obs_ratio(alive_foes, bm_max_i(1, m->num_agents - 1));
    obs[o++] = bm_obs_ratio(legal_moves, 4);

    int order[BM_MAX_AGENTS];
    int count = 0;
    order[count++] = agent_i;
    for (int a = 0; a < m->num_agents && count < BM_MAX_AGENTS; a++) {
        if (a != agent_i) order[count++] = a;
    }
    while (count < BM_MAX_AGENTS) order[count++] = -1;

    for (int slot = 0; slot < BM_MAX_AGENTS; slot++) {
        int a = order[slot];
        if (a < 0) {
            for (int k = 0; k < BM_AGENT_FEAT; k++) obs[o++] = 0.0f;
            continue;
        }
        const BMAgent* ag = &m->agents[a];
        int cx, cy;
        bm_world_to_canonical(m, agent_i, ag->x, ag->y, &cx, &cy);
        obs[o++] = ag->alive ? 1.0f : 0.0f;
        obs[o++] = bm_obs_ratio(cx, BM_MAX_W - 1);
        obs[o++] = bm_obs_ratio(cy, BM_MAX_H - 1);
        obs[o++] = bm_obs_ratio(bm_max_i(0, ag->max_bombs - ag->bombs_out),
            BM_MAX_BOMBS_PER_AGENT);
        obs[o++] = bm_obs_ratio(ag->max_bombs, BM_MAX_BOMBS_PER_AGENT);
        obs[o++] = bm_obs_ratio(ag->bomb_range, BM_MAX_FLAME_RANGE);
        obs[o++] = bm_obs_ratio(ag->speed_level, BM_MAX_SPEED_LEVEL);
        obs[o++] = bm_obs_ratio(ag->bombs_out, BM_MAX_BOMBS_PER_AGENT);
        obs[o++] = bm_obs_ratio(ag->move_cd, bm_max_i(1, cfg->frames_per_cell));
        obs[o++] = bm_obs_ratio(ag->invuln, BM_SPAWN_INVULN);
    }
}

BM_HD void bm_write_obs(const BMMatch* m, const BMConfig* cfg,
        int agent_i, float* obs) {
    bm_write_obs_mask(m, cfg, agent_i, obs, (uint8_t*)0);
}

#if !defined(__CUDACC__)
_Static_assert(BM_OBS_SIZE == 1200, "unexpected Bomberman observation size");
_Static_assert(sizeof(BMBomb) == 8, "BMBomb packing regression");
#endif
