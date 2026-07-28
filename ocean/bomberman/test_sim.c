// Standalone correctness tests for the shared C/CUDA Bomberman simulator.
// No PufferLib, Raylib, CUDA runtime, or model training is required.

#define BM_HEADLESS 1

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bm_sim.h"

static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        g_fail++; \
    } \
} while (0)

static void clear_arena(BMMatch* m, BMConfig* cfg, int num_agents, uint32_t seed) {
    *cfg = bm_default_config();
    cfg->num_agents = num_agents;
    cfg->soft_density = 0.0f;
    cfg->item_chance = 0.0f;
    cfg->pillar_mode = 0;
    cfg->reward_alive = 0.0f;
    cfg->reward_timeout = 0.0f;
    cfg->reward_bomb_threat = 0.0f;
    cfg->reward_bomb_escape = 0.0f;
    cfg->max_ticks = 1000;
    bm_reset_match(m, cfg, seed);

    int n = m->width * m->height;
    for (int i = 0; i < n; i++) {
        int x = i % m->width;
        int y = i / m->width;
        m->tiles[i] = (x == 0 || y == 0 || x == m->width - 1 || y == m->height - 1)
            ? BM_TILE_HARD : BM_TILE_EMPTY;
        m->items[i] = BM_ITEM_NONE;
        m->flame_ttl[i] = 0;
        m->flame_owner[i] = BM_FLAME_OWNER_NONE;
        m->bomb_here[i] = 0;
        m->danger_time[i] = BM_DANGER_SAFE;
    }
    for (int b = 0; b < BM_MAX_BOMBS; b++) m->bombs[b].active = 0;
    for (int a = 0; a < m->num_agents; a++) {
        m->agents[a].alive = 1;
        m->agents[a].max_bombs = 1;
        m->agents[a].bomb_range = 1;
        m->agents[a].speed_level = 0;
        m->agents[a].move_cd = 0;
        m->agents[a].bombs_out = 0;
        m->agents[a].invuln = 0;
        m->agents[a].ep_return = 0.0f;
        m->agents[a].ep_score = 0.0f;
        m->agents[a].kills = 0;
        m->agents[a].self_kills = 0;
        m->agents[a].soft_breaks = 0;
    }
    m->tick = 0;
    m->done = 0;
    m->winner = -1;
    bm_refresh_danger(m);
}

static void add_bomb(BMMatch* m, int slot, int owner,
        int x, int y, int timer, int range) {
    CHECK(slot >= 0 && slot < BM_MAX_BOMBS, "bomb slot valid");
    BMBomb* b = &m->bombs[slot];
    b->active = 1;
    b->x = (uint8_t)x;
    b->y = (uint8_t)y;
    b->owner = (uint8_t)owner;
    b->timer = (uint16_t)timer;
    b->range = (uint8_t)range;
    b->shaping_flags = 0;
    m->bomb_here[bm_idx(m, x, y)] = (uint8_t)(slot + 1);
    m->agents[owner].bombs_out += 1;
}

static int cell_base(int x, int y) {
    return (y * BM_MAX_W + x) * BM_CELL_CH;
}

static void test_layout(void) {
    CHECK(BM_OBS_SIZE == 1200, "observation size is 1200");
    CHECK(sizeof(BMBomb) == 8, "packed bomb is 8 bytes");
    CHECK(BM_MAX_CELLS == 143, "state board matches 13x11");
    printf("  layout: obs=%d floats, match=%zu bytes, bomb=%zu bytes\n",
        BM_OBS_SIZE, sizeof(BMMatch), sizeof(BMBomb));
}

static void test_map_and_canonical_spawns(void) {
    BMConfig cfg = bm_default_config();
    cfg.num_agents = 4;
    cfg.soft_density = 0.0f;
    BMMatch m;
    bm_reset_match(&m, &cfg, 12345u);

    for (int x = 0; x < m.width; x++) {
        CHECK(m.tiles[bm_idx(&m, x, 0)] == BM_TILE_HARD, "top border hard");
        CHECK(m.tiles[bm_idx(&m, x, m.height - 1)] == BM_TILE_HARD, "bottom border hard");
    }

    for (int viewer = 0; viewer < 4; viewer++) {
        float obs[BM_OBS_SIZE];
        uint8_t mask[BM_NUM_ACTIONS];
        bm_write_obs_mask(&m, &cfg, viewer, obs, mask);
        int b = cell_base(1, 1);
        CHECK(obs[b + 6] == 1.0f, "every viewer spawn canonicalizes to (1,1)");
        CHECK(mask[BM_ACT_UP] == 0 && mask[BM_ACT_LEFT] == 0,
            "canonical border directions masked");
        CHECK(mask[BM_ACT_DOWN] == 1 && mask[BM_ACT_RIGHT] == 1,
            "canonical inward directions legal");
    }
    printf("  map + canonical spawn/action frame ok\n");
}

static void test_bomb_timer_exact(void) {
    BMConfig cfg;
    BMMatch m;
    clear_arena(&m, &cfg, 2, 1u);
    cfg.bomb_timer = 3;
    cfg.flame_duration = 2;
    m.agents[0].x = 3; m.agents[0].y = 3; m.agents[0].invuln = 99;
    m.agents[1].x = 9; m.agents[1].y = 7; m.agents[1].invuln = 99;

    int actions[2] = {BM_ACT_BOMB, BM_ACT_STAY};
    float rewards[2], terminals[2];
    bm_step_match(&m, &cfg, actions, rewards, terminals);
    int slot = bm_bomb_slot_at(&m, 3, 3);
    CHECK(slot >= 0 && m.bombs[slot].timer == 3,
        "new bomb exposes full configured fuse after placement step");

    actions[0] = BM_ACT_STAY;
    bm_step_match(&m, &cfg, actions, rewards, terminals);
    CHECK(m.bombs[slot].active && m.bombs[slot].timer == 2, "fuse 3->2");
    bm_step_match(&m, &cfg, actions, rewards, terminals);
    CHECK(m.bombs[slot].active && m.bombs[slot].timer == 1, "fuse 2->1");
    bm_step_match(&m, &cfg, actions, rewards, terminals);
    CHECK(!m.bombs[slot].active, "fuse explodes exactly on final step");
    printf("  exact fuse timing ok\n");
}

static void test_timer_bounds(void) {
    BMConfig cfg;
    BMMatch m;
    clear_arena(&m, &cfg, 2, 11u);
    cfg.bomb_timer = 100000;
    cfg.flame_duration = 100000;
    m.agents[0].x = 3; m.agents[0].y = 3; m.agents[0].invuln = 99;
    m.agents[1].x = 9; m.agents[1].y = 7; m.agents[1].invuln = 99;

    int actions[2] = {BM_ACT_BOMB, BM_ACT_STAY};
    float rewards[2], terminals[2];
    bm_step_match(&m, &cfg, actions, rewards, terminals);
    int slot = bm_bomb_slot_at(&m, 3, 3);
    CHECK(slot >= 0 && m.bombs[slot].timer == BM_DANGER_SAFE - 1,
        "oversized fuse clamps below no-danger sentinel");
    CHECK(m.danger_time[bm_idx(&m, 3, 3)] == BM_DANGER_SAFE - 1,
        "danger map preserves maximum representable fuse");
    CHECK(bm_flame_duration(&cfg) == 254,
        "oversized flame duration clamps to uint8-safe value");
    printf("  timer/duration bounds ok\n");
}

static void test_chain_does_not_fast_forward_unrelated_bombs(void) {
    BMConfig cfg;
    BMMatch m;
    clear_arena(&m, &cfg, 2, 2u);
    m.agents[0].invuln = 99;
    m.agents[1].invuln = 99;
    add_bomb(&m, 0, 0, 3, 3, 1, 1);
    add_bomb(&m, 1, 1, 9, 7, 5, 1);

    float rewards[BM_MAX_AGENTS] = {0};
    bm_tick_bombs(&m, &cfg, rewards);
    CHECK(!m.bombs[0].active, "due bomb exploded");
    CHECK(m.bombs[1].active && m.bombs[1].timer == 4,
        "unrelated fuse decremented once, not once per chain pass");
    printf("  unrelated fuse fast-forward bug fixed\n");
}

static void test_chain_reaction_and_danger_prediction(void) {
    BMConfig cfg;
    BMMatch m;
    clear_arena(&m, &cfg, 2, 3u);
    m.agents[0].invuln = 99;
    m.agents[1].invuln = 99;
    add_bomb(&m, 0, 0, 3, 3, 2, 3);
    add_bomb(&m, 1, 1, 5, 3, 9, 2);
    bm_refresh_danger(&m);
    CHECK(m.danger_time[bm_idx(&m, 5, 3)] == 2,
        "chained bomb inherits earlier fuse in danger map");
    CHECK(m.danger_time[bm_idx(&m, 7, 3)] == 2,
        "danger propagates through the chained bomb blast");

    float rewards[BM_MAX_AGENTS] = {0};
    bm_tick_bombs(&m, &cfg, rewards); // 2->1, 9->8
    bm_tick_bombs(&m, &cfg, rewards); // first explodes and chains second
    CHECK(!m.bombs[0].active && !m.bombs[1].active,
        "chain reaction resolves in the same step");
    printf("  chain reaction + danger prediction ok\n");
}

static void test_simultaneous_movement(void) {
    BMConfig cfg;
    BMMatch m;
    clear_arena(&m, &cfg, 2, 4u);
    cfg.frames_per_cell = 1;
    m.agents[0].x = 3; m.agents[0].y = 3;
    m.agents[1].x = 5; m.agents[1].y = 3;

    int actions[2] = {BM_ACT_RIGHT, BM_ACT_RIGHT};
    float rewards[2], terminals[2];
    // Agent 1's canonical RIGHT mirrors to world LEFT. Both contest (4,3).
    bm_step_match(&m, &cfg, actions, rewards, terminals);
    CHECK(m.agents[0].x == 3 && m.agents[1].x == 5,
        "contested destination blocks both without index bias");

    m.agents[0].x = 1; m.agents[0].y = 1;
    actions[0] = BM_ACT_STAY;
    actions[1] = BM_ACT_RIGHT;
    bm_step_match(&m, &cfg, actions, rewards, terminals);
    CHECK(m.agents[1].x == 4, "agent 1 canonical RIGHT maps to world LEFT");
    printf("  simultaneous movement + mirrored actions ok\n");
}

static void test_move_period(void) {
    BMConfig cfg;
    BMMatch m;
    clear_arena(&m, &cfg, 2, 5u);
    cfg.frames_per_cell = 2;
    m.agents[0].x = 3; m.agents[0].y = 3;
    m.agents[1].x = 9; m.agents[1].y = 7;
    int actions[2] = {BM_ACT_RIGHT, BM_ACT_STAY};
    float rewards[2], terminals[2];

    bm_step_match(&m, &cfg, actions, rewards, terminals);
    CHECK(m.agents[0].x == 4 && m.agents[0].move_cd == 1,
        "first move succeeds and leaves one blocked step");
    bm_step_match(&m, &cfg, actions, rewards, terminals);
    CHECK(m.agents[0].x == 4 && m.agents[0].move_cd == 0,
        "second decision consumes cooldown");
    bm_step_match(&m, &cfg, actions, rewards, terminals);
    CHECK(m.agents[0].x == 5, "frames_per_cell=2 moves every two steps");
    printf("  movement period semantics ok\n");
}

static void test_simultaneous_death_credit(void) {
    BMConfig cfg;
    BMMatch m;
    clear_arena(&m, &cfg, 2, 6u);
    cfg.reward_kill = 1.0f;
    cfg.reward_death = -1.0f;
    cfg.reward_win = 0.5f;
    m.agents[0].x = 2; m.agents[0].y = 3;
    m.agents[1].x = 6; m.agents[1].y = 3;
    add_bomb(&m, 0, 0, 5, 3, 1, 1); // kills agent 1
    add_bomb(&m, 1, 1, 3, 3, 1, 1); // kills agent 0

    int actions[2] = {BM_ACT_STAY, BM_ACT_STAY};
    float rewards[2], terminals[2];
    bm_step_match(&m, &cfg, actions, rewards, terminals);
    CHECK(!m.agents[0].alive && !m.agents[1].alive, "both agents die simultaneously");
    CHECK(m.agents[0].kills == 1 && m.agents[1].kills == 1,
        "both dead bomb owners still receive their valid kill credit");
    CHECK(fabsf(rewards[0]) < 1e-5f && fabsf(rewards[1]) < 1e-5f,
        "kill and death rewards balance for mutual kill");
    CHECK(m.done && m.winner == -1 && terminals[0] == 1.0f && terminals[1] == 1.0f,
        "mutual kill is a terminal draw");
    printf("  simultaneous death/kill accounting ok\n");
}

static void test_self_kill_penalty(void) {
    BMConfig cfg;
    BMMatch m;
    clear_arena(&m, &cfg, 2, 61u);
    cfg.reward_death = -1.0f;
    cfg.reward_self_kill = -5.0f;
    cfg.reward_win = 1.0f;
    m.agents[0].x = 3; m.agents[0].y = 3;
    m.agents[1].x = 9; m.agents[1].y = 7;
    add_bomb(&m, 0, 0, 3, 3, 1, 1);

    int actions[2] = {BM_ACT_STAY, BM_ACT_STAY};
    float rewards[2], terminals[2];
    bm_step_match(&m, &cfg, actions, rewards, terminals);
    CHECK(m.agents[0].self_kills == 1, "own flame records a self kill");
    CHECK(m.agents[0].kills == 0, "self kill is not credited as a kill");
    CHECK(fabsf(rewards[0] - (cfg.reward_death + cfg.reward_self_kill)) < 1e-5f,
        "self-kill penalty stacks with ordinary death penalty");
    CHECK(m.winner == 1 && m.done, "survivor wins after opponent suicide");
    printf("  explicit self-kill penalty/accounting ok\n");
}

static void test_kill_coupled_bomb_shaping(void) {
    BMConfig cfg;
    BMMatch m;
    clear_arena(&m, &cfg, 2, 16u);
    cfg.frames_per_cell = 1;
    cfg.bomb_timer = 3;
    cfg.reward_bomb_threat = 8.0f;
    cfg.reward_bomb_escape = 5.0f;
    m.agents[0].x = 3; m.agents[0].y = 3;
    m.agents[0].bomb_range = 2;
    m.agents[1].x = 5; m.agents[1].y = 3;

    int actions[2] = {BM_ACT_BOMB, BM_ACT_STAY};
    float rewards[2], terminals[2];
    bm_step_match(&m, &cfg, actions, rewards, terminals);
    CHECK(fabsf(rewards[0]) < 1e-6f,
        "aimed bomb receives no reward before a credited kill");

    actions[0] = BM_ACT_UP;
    bm_step_match(&m, &cfg, actions, rewards, terminals);
    CHECK(fabsf(rewards[0]) < 1e-6f,
        "escaping a bomb cannot farm reward in an ordinary game");
    actions[0] = BM_ACT_RIGHT;
    bm_step_match(&m, &cfg, actions, rewards, terminals);
    CHECK(fabsf(rewards[0]) < 1e-6f,
        "safe position still receives no reward before detonation");
    actions[0] = BM_ACT_STAY;
    bm_step_match(&m, &cfg, actions, rewards, terminals);
    CHECK(m.agents[0].kills == 1 && m.agents[0].alive,
        "escaped bomb earns a real credited kill");
    CHECK(fabsf(rewards[0] - (cfg.reward_kill + cfg.reward_bomb_threat
            + cfg.reward_bomb_escape + cfg.reward_win)) < 1e-6f,
        "kill bonuses are paid only for a safe credited kill");
    printf("  kill-coupled bomb shaping ok\n");
}

static void test_reverse_curriculum_finish(void) {
    BMConfig cfg = bm_default_config();
    cfg.num_agents = 2;
    cfg.reverse_curriculum = 1;
    cfg.curriculum_steps = 1000;
    cfg.reward_alive = 0.0f;
    BMMatch m;
    bm_reset_match(&m, &cfg, 62u);
    CHECK(bm_apply_reverse_curriculum(&m, &cfg, 0.0f) == 1,
        "zero-progress reset uses reverse curriculum");
    CHECK(m.curriculum_stage == 0,
        "curriculum begins one move before a stationary finish");
    int initial_distance = bm_nearest_foe_distance(&m, 0);
    CHECK(initial_distance == 2 && m.agents[0].y == m.agents[1].y,
        "first stationary target is exactly two cells away");
    for (int y = 1; y < m.height - 1; y++) {
        for (int x = 1; x < m.width - 1; x++) {
            CHECK(m.tiles[bm_idx(&m, x, y)] != BM_TILE_SOFT,
                "terminal lesson uses a cleared reachable board");
        }
    }
    CHECK(!bm_action_legal_world(&m, 1, BM_ACT_BOMB),
        "curriculum opponent cannot end lessons by suicide");

    int toward = m.agents[1].x > m.agents[0].x ? BM_ACT_RIGHT : BM_ACT_LEFT;
    int away = toward == BM_ACT_RIGHT ? BM_ACT_LEFT : BM_ACT_RIGHT;
    int actions[2] = {toward, BM_ACT_RIGHT};
    float rewards[2], terminals[2];
    int foe_x = m.agents[1].x;
    int foe_y = m.agents[1].y;
    bm_step_match(&m, &cfg, actions, rewards, terminals);
    CHECK(m.agents[1].x == foe_x && m.agents[1].y == foe_y,
        "stage 0 target remains stationary despite policy action");
    actions[0] = BM_ACT_BOMB;
    bm_step_match(&m, &cfg, actions, rewards, terminals);
    actions[0] = away;
    bm_step_match(&m, &cfg, actions, rewards, terminals);
    // Movement period is two: one repeated action consumes cooldown, the next
    // reaches the second retreat cell outside the range-1 blast.
    bm_step_match(&m, &cfg, actions, rewards, terminals);
    bm_step_match(&m, &cfg, actions, rewards, terminals);
    actions[0] = BM_ACT_STAY;
    for (int step = 0; step < bm_bomb_timer(&cfg) + 3 && !m.done; step++) {
        bm_step_match(&m, &cfg, actions, rewards, terminals);
    }
    CHECK(m.done && m.winner == 0,
        "move, bomb, and retreat solves the stationary finish");
    CHECK(m.agents[0].kills == 1 && m.agents[0].self_kills == 0,
        "curriculum finish is a credited enemy kill, not suicide");
    CHECK(m.curriculum_aimed && m.curriculum_escaped,
        "general curriculum shaping records aim and survival");

    bm_reset_match(&m, &cfg, 63u);
    CHECK(bm_apply_reverse_curriculum(&m, &cfg, 1.0f) == 0,
        "completed curriculum uses ordinary turn-zero reset");
    CHECK(m.curriculum_stage == -1, "normal reset is marked separately");
    CHECK(bm_action_legal_world(&m, 1, BM_ACT_BOMB),
        "ordinary self-play restores opponent bomb actions");

    int found_stage1 = 0;
    for (uint32_t seed = 100; seed < 2000 && !found_stage1; seed++) {
        bm_reset_match(&m, &cfg, seed);
        bm_apply_reverse_curriculum(&m, &cfg,
            1.0f / (float)BM_CURRICULUM_STAGES);
        found_stage1 = m.curriculum_stage == 1;
    }
    CHECK(found_stage1 && bm_nearest_foe_distance(&m, 0) == 4,
        "stage 1 moves backward to a farther stationary target");

    int found_stage2 = 0;
    for (uint32_t seed = 2000; seed < 5000 && !found_stage2; seed++) {
        bm_reset_match(&m, &cfg, seed);
        bm_apply_reverse_curriculum(&m, &cfg,
            2.0f / (float)BM_CURRICULUM_STAGES);
        found_stage2 = m.curriculum_stage == 2;
    }
    CHECK(found_stage2 && bm_nearest_foe_distance(&m, 0) == 4,
        "stage 2 activates the same farther target setup");
    int old_x = m.agents[1].x;
    int old_y = m.agents[1].y;
    int move_action = BM_ACT_STAY;
    for (int a = BM_ACT_UP; a <= BM_ACT_RIGHT; a++) {
        if (bm_action_legal(&m, 1, a)) {
            move_action = a;
            break;
        }
    }
    actions[0] = BM_ACT_STAY;
    actions[1] = move_action;
    bm_step_match(&m, &cfg, actions, rewards, terminals);
    CHECK(m.agents[1].x != old_x || m.agents[1].y != old_y,
        "stage 2 target can move but still cannot bomb");

    int found_stage3 = 0;
    for (uint32_t seed = 5000; seed < 9000 && !found_stage3; seed++) {
        bm_reset_match(&m, &cfg, seed);
        bm_apply_reverse_curriculum(&m, &cfg,
            3.0f / (float)BM_CURRICULUM_STAGES);
        found_stage3 = m.curriculum_stage == 3;
    }
    CHECK(found_stage3, "stage 3 plausible midgame is reachable");

    int found_stage4 = 0;
    for (uint32_t seed = 9000; seed < 15000 && !found_stage4; seed++) {
        bm_reset_match(&m, &cfg, seed);
        bm_apply_reverse_curriculum(&m, &cfg,
            4.0f / (float)BM_CURRICULUM_STAGES);
        found_stage4 = m.curriculum_stage == 4;
    }
    CHECK(found_stage4 && m.agents[0].x == 1 && m.agents[0].y == 1,
        "stage 4 restores the untouched ordinary opening");
    CHECK(!bm_action_legal_world(&m, 1, BM_ACT_BOMB),
        "last curriculum opponent remains movement-only");

    int found_mixed_full_game = 0;
    for (uint32_t seed = 15000; seed < 25000 && !found_mixed_full_game; seed++) {
        bm_reset_match(&m, &cfg, seed);
        bm_apply_reverse_curriculum(&m, &cfg,
            4.0f / (float)BM_CURRICULUM_STAGES);
        found_mixed_full_game = m.curriculum_stage == -1;
    }
    CHECK(found_mixed_full_game,
        "ordinary full games remain mixed before curriculum completion");
    printf("  reverse curriculum terminal-to-normal progression ok\n");
}

static void test_observation_and_mask(void) {
    BMConfig cfg;
    BMMatch m;
    clear_arena(&m, &cfg, 2, 7u);
    m.agents[0].x = 3; m.agents[0].y = 3;
    m.agents[0].bomb_range = 3;
    m.agents[0].max_bombs = 2;
    m.agents[1].x = 5; m.agents[1].y = 3;
    add_bomb(&m, 0, 0, 3, 5, 8, 2);
    add_bomb(&m, 1, 1, 8, 7, 4, 2);
    m.items[bm_idx(&m, 4, 3)] = BM_ITEM_FLAME;
    bm_refresh_danger(&m);

    float obs[BM_OBS_SIZE];
    uint8_t mask[BM_NUM_ACTIONS];
    for (int i = 0; i < BM_OBS_SIZE; i++) obs[i] = -1.0f;
    bm_write_obs_mask(&m, &cfg, 0, obs, mask);
    for (int i = 0; i < BM_OBS_SIZE; i++) {
        CHECK(isfinite(obs[i]), "observation value is finite");
        CHECK(obs[i] >= 0.0f && obs[i] <= 1.0f,
            "observation value stays in [0,1]");
    }

    int self = cell_base(3, 3);
    int foe = cell_base(5, 3);
    int own_bomb = cell_base(3, 5);
    int foe_bomb = cell_base(8, 7);
    CHECK(obs[self + 6] == 1.0f && obs[foe + 7] == 1.0f,
        "self and foe spatial channels populated");
    CHECK(obs[own_bomb + 3] > 0 && obs[own_bomb + 4] == 0,
        "own bomb has own-fuse channel");
    CHECK(obs[foe_bomb + 4] > 0 && obs[foe_bomb + 3] == 0,
        "foe bomb has foe-fuse channel");
    CHECK(obs[own_bomb + 5] > 0 && obs[foe_bomb + 5] > 0,
        "predicted danger channel populated");
    CHECK(fabsf(obs[cell_base(4, 3) + 2] - (2.0f / 3.0f)) < 1e-6f,
        "item type is normalized categorically");

    int g = BM_MAX_CELLS * BM_CELL_CH;
    CHECK(obs[g + 4] == 1.0f, "prospective bomb says current foe is in blast line");
    CHECK(obs[g + 6] == 1.0f, "prospective bomb has an escape route");
    for (int a = 0; a < BM_NUM_ACTIONS; a++) {
        CHECK(obs[g + 8 + a] == (mask[a] ? 1.0f : 0.0f),
            "observation legal bits match action mask");
    }
    CHECK(mask[BM_ACT_BOMB] == 1, "bomb is legal when capacity and cell allow it");

    // Place a bomb under the agent: bomb action becomes illegal immediately.
    add_bomb(&m, 2, 0, 3, 3, 10, 1);
    bm_refresh_danger(&m);
    bm_write_obs_mask(&m, &cfg, 0, obs, mask);
    CHECK(mask[BM_ACT_BOMB] == 0, "bomb mask rejects occupied current cell");
    printf("  float tactical observation + action mask ok\n");
}

static void test_pickup_reward(void) {
    BMConfig cfg;
    BMMatch m;
    clear_arena(&m, &cfg, 2, 71u);
    cfg.reward_pickup = 0.5f;
    BMAgent* agent = &m.agents[0];
    agent->x = 4;
    agent->y = 4;
    int i = bm_idx(&m, agent->x, agent->y);
    float rewards[BM_MAX_AGENTS] = {0};

    m.items[i] = BM_ITEM_BOMB;
    bm_pickup(&m, &cfg, 0, rewards);
    m.items[i] = BM_ITEM_FLAME;
    bm_pickup(&m, &cfg, 0, rewards);
    m.items[i] = BM_ITEM_SPEED;
    bm_pickup(&m, &cfg, 0, rewards);

    CHECK(agent->max_bombs == 2 && agent->bomb_range == 2
        && agent->speed_level == 1, "all pickup types improve their statistic");
    CHECK(agent->bomb_pickups == 1 && agent->range_pickups == 1
        && agent->speed_pickups == 1, "successful pickup types are counted");
    CHECK(fabsf(rewards[0] - 1.5f) < 1e-6f
        && fabsf(agent->ep_return - 1.5f) < 1e-6f,
        "successful pickups receive configured reward");

    agent->max_bombs = BM_MAX_BOMBS_PER_AGENT;
    m.items[i] = BM_ITEM_BOMB;
    bm_pickup(&m, &cfg, 0, rewards);
    CHECK(fabsf(rewards[0] - 1.5f) < 1e-6f && agent->bomb_pickups == 1,
        "capped pickup is consumed without reward or count");
    printf("  useful pickup reward + typed counters ok\n");
}

static void test_closer_reward_is_signed(void) {
    BMConfig cfg;
    BMMatch m;
    clear_arena(&m, &cfg, 2, 72u);
    cfg.frames_per_cell = 1;
    cfg.reward_closer = 0.1f;
    m.agents[0].x = 3;
    m.agents[0].y = 3;
    m.agents[1].x = 6;
    m.agents[1].y = 3;
    int actions[2] = {BM_ACT_RIGHT, BM_ACT_STAY};
    float rewards[BM_MAX_AGENTS] = {0};

    bm_resolve_actions(&m, &cfg, actions, rewards);
    CHECK(fabsf(rewards[0] - 0.1f) < 1e-6f,
        "reducing nearest-foe distance earns configured shaping");
    float approach_reward = rewards[0];

    rewards[0] = rewards[1] = 0.0f;
    actions[0] = BM_ACT_LEFT;
    bm_resolve_actions(&m, &cfg, actions, rewards);
    CHECK(fabsf(rewards[0] + 0.1f) < 1e-6f,
        "increasing nearest-foe distance pays the symmetric penalty");
    CHECK(fabsf(approach_reward + rewards[0]) < 1e-6f,
        "approach-retreat oscillation has zero undiscounted shaping return");
    printf("  optional signed nearest-foe shaping ok\n");
}

static void test_timeout_penalty(void) {
    BMConfig cfg;
    BMMatch m;
    clear_arena(&m, &cfg, 2, 8u);
    cfg.max_ticks = 1;
    cfg.reward_timeout = -0.25f;
    int actions[2] = {BM_ACT_STAY, BM_ACT_STAY};
    float rewards[2], terminals[2];
    bm_step_match(&m, &cfg, actions, rewards, terminals);
    CHECK(m.done && m.winner == -1, "timeout with survivors is a draw");
    CHECK(fabsf(rewards[0] + 0.25f) < 1e-5f && fabsf(rewards[1] + 0.25f) < 1e-5f,
        "timeout penalty reaches all survivors");
    printf("  timeout anti-camping reward ok\n");
}

static void test_default_reward_envelope(void) {
    BMConfig cfg = bm_default_config();
    float safe_kill = cfg.reward_alive + cfg.reward_kill
        + cfg.reward_bomb_threat + cfg.reward_bomb_escape + cfg.reward_win;
    float timeout = cfg.reward_alive + cfg.reward_timeout;
    float killed = cfg.reward_alive + cfg.reward_death;
    float mutual = cfg.reward_alive + cfg.reward_death + cfg.reward_self_kill
        + cfg.reward_kill + cfg.reward_bomb_threat;
    float self_kill = cfg.reward_alive + cfg.reward_death
        + cfg.reward_self_kill;

    CHECK(safe_kill < 1.0f && safe_kill > 0.0f,
        "safe kill remains positive without learner clipping");
    CHECK(timeout > mutual && mutual < killed && killed > self_kill,
        "timeout, mutual kill, death, and self-kill have intended ordering");
    CHECK(timeout >= -1.0f && killed >= -1.0f
        && mutual >= -1.0f && self_kill >= -1.0f,
        "primary negative outcomes remain inside learner reward envelope");
    printf("  reward envelope safe=%.3f timeout=%.3f killed=%.3f"
        " mutual=%.3f self=%.3f\n",
        safe_kill, timeout, killed, mutual, self_kill);
}

static void test_random_rollout_and_determinism(void) {
    BMConfig cfg = bm_default_config();
    cfg.num_agents = 4;
    cfg.max_ticks = 200;
    BMMatch m;
    bm_reset_match(&m, &cfg, 777u);

    uint32_t rng = 42u;
    int episodes = 0;
    for (int step = 0; step < 10000; step++) {
        int actions[BM_MAX_AGENTS];
        float rewards[BM_MAX_AGENTS];
        float terminals[BM_MAX_AGENTS];
        for (int a = 0; a < m.num_agents; a++) actions[a] = bm_randi(&rng, BM_NUM_ACTIONS);
        bm_step_match(&m, &cfg, actions, rewards, terminals);
        if (m.done) {
            episodes++;
            bm_reset_match(&m, &cfg, bm_xorshift(&rng));
        }
    }
    CHECK(episodes > 0, "random rollout completes episodes");

    BMMatch a, b;
    bm_reset_match(&a, &cfg, 424242u);
    bm_reset_match(&b, &cfg, 424242u);
    int actions[BM_MAX_AGENTS] = {BM_ACT_RIGHT, BM_ACT_RIGHT, BM_ACT_DOWN, BM_ACT_DOWN};
    float ra[BM_MAX_AGENTS], rb[BM_MAX_AGENTS];
    float ta[BM_MAX_AGENTS], tb[BM_MAX_AGENTS];
    for (int t = 0; t < 100; t++) {
        bm_step_match(&a, &cfg, actions, ra, ta);
        bm_step_match(&b, &cfg, actions, rb, tb);
        CHECK(memcmp(&a, &b, sizeof(BMMatch)) == 0, "full simulator state deterministic");
        if (a.done) break;
    }
    printf("  random rollout + deterministic state transitions ok (episodes=%d)\n", episodes);
}

int main(void) {
    printf("bomberman reworked simulator tests\n");
    test_layout();
    test_map_and_canonical_spawns();
    test_bomb_timer_exact();
    test_timer_bounds();
    test_chain_does_not_fast_forward_unrelated_bombs();
    test_chain_reaction_and_danger_prediction();
    test_simultaneous_movement();
    test_move_period();
    test_simultaneous_death_credit();
    test_self_kill_penalty();
    test_kill_coupled_bomb_shaping();
    test_reverse_curriculum_finish();
    test_observation_and_mask();
    test_pickup_reward();
    test_closer_reward_is_signed();
    test_timeout_penalty();
    test_default_reward_envelope();
    test_random_rollout_and_determinism();

    if (g_fail) {
        fprintf(stderr, "\n%d check(s) failed\n", g_fail);
        return 1;
    }
    printf("\nAll Bomberman simulator tests passed.\n");
    return 0;
}
