// Standalone unit tests for Bomberman match sim + CPU ABI.
// Does NOT use build.sh, Raylib, or CUDA.
//
//   make -C ocean/bomberman test
//
// Safe to run while another env owns ./puffer.

#define BM_HEADLESS 1

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Minimal Dict stub so pufferenv.h / bomberman puf_init can load without full ini parser.
// We test bm_sim directly and a thin Env wrapper without going through dict_get.

#include "bm_sim.h"

static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        g_fail++; \
    } \
} while (0)

static void test_obs_size(void) {
    CHECK(BM_OBS_SIZE == BM_OBS_CELLS * BM_CELL_CH + BM_GLOBAL_FEAT + BM_MAX_AGENTS * BM_AGENT_FEAT,
        "obs size formula");
    // Compact 13x11 x 5 + agents — full info without 1.6k d redundancy.
    CHECK(BM_OBS_SIZE > 200 && BM_OBS_SIZE < 900, "compact full-board range");
    printf("  obs_size=%d (compact full board)\n", BM_OBS_SIZE);
}

static void test_map_has_border_and_spawns(void) {
    BMConfig cfg = bm_default_config();
    cfg.num_agents = 4;
    cfg.width = 13;
    cfg.height = 11;
    BMMatch m;
    bm_reset_match(&m, &cfg, 12345u);

    CHECK(m.width == 13 && m.height == 11, "map size");
    CHECK(m.num_agents == 4, "num agents");

    // Border hard
    for (int x = 0; x < m.width; x++) {
        CHECK(m.tiles[bm_idx(&m, x, 0)] == BM_TILE_HARD, "top border");
        CHECK(m.tiles[bm_idx(&m, x, m.height - 1)] == BM_TILE_HARD, "bot border");
    }
    for (int y = 0; y < m.height; y++) {
        CHECK(m.tiles[bm_idx(&m, 0, y)] == BM_TILE_HARD, "left border");
        CHECK(m.tiles[bm_idx(&m, m.width - 1, y)] == BM_TILE_HARD, "right border");
    }

    // Spawns clear and agents alive on empty cells
    for (int a = 0; a < m.num_agents; a++) {
        CHECK(m.agents[a].alive, "agent alive");
        int i = bm_idx(&m, m.agents[a].x, m.agents[a].y);
        CHECK(m.tiles[i] == BM_TILE_EMPTY, "spawn empty");
        CHECK(!m.bomb_here[i], "spawn no bomb");
    }

    // Distinct corner-ish spawns
    for (int a = 0; a < m.num_agents; a++) {
        for (int b = a + 1; b < m.num_agents; b++) {
            CHECK(!(m.agents[a].x == m.agents[b].x && m.agents[a].y == m.agents[b].y),
                "unique spawns");
        }
    }
    printf("  map ok (%dx%d, soft_density probe)\n", m.width, m.height);
}

static void test_bomb_breaks_soft(void) {
    BMConfig cfg = bm_default_config();
    cfg.num_agents = 2;
    cfg.bomb_timer = 3;
    cfg.flame_duration = 2;
    cfg.soft_density = 0.0f; // empty arena + we place soft manually
    cfg.pillar_mode = 0;
    cfg.frames_per_cell = 0; // move every step
    cfg.reward_soft = 0.5f;
    cfg.reward_alive = 0.0f;
    cfg.reward_death = 0.0f;
    cfg.reward_win = 0.0f;
    cfg.reward_kill = 0.0f;

    BMMatch m;
    bm_reset_match(&m, &cfg, 1u);

    // Clear to empties (keep border)
    for (int y = 1; y < m.height - 1; y++) {
        for (int x = 1; x < m.width - 1; x++) {
            m.tiles[bm_idx(&m, x, y)] = BM_TILE_EMPTY;
            m.items[bm_idx(&m, x, y)] = BM_ITEM_NONE;
        }
    }

    // Agent 0 at (3,2), soft at (4,2); walk left after plant
    m.agents[0].x = 3;
    m.agents[0].y = 2;
    m.agents[0].bomb_range = 2;
    m.agents[0].move_cd = 0;
    m.agents[0].invuln = 0;
    m.agents[1].x = m.width - 3;
    m.agents[1].y = m.height - 3;
    m.agents[1].invuln = 99;
    m.tiles[bm_idx(&m, 4, 2)] = BM_TILE_SOFT;

    int actions[2] = {BM_ACT_BOMB, BM_ACT_STAY};
    float rewards[2] = {0, 0};
    float terminals[2] = {0, 0};
    bm_step_match(&m, &cfg, actions, rewards, terminals); // place bomb (timer=3 -> 2)
    CHECK(m.bomb_here[bm_idx(&m, 3, 2)], "bomb placed");

    actions[0] = BM_ACT_LEFT;
    bm_step_match(&m, &cfg, actions, rewards, terminals); // move to (2,2), timer 1
    CHECK(m.agents[0].x == 2, "walked away");
    bm_step_match(&m, &cfg, actions, rewards, terminals); // timer 0 explode
    CHECK(m.tiles[bm_idx(&m, 4, 2)] == BM_TILE_EMPTY, "soft destroyed");
    CHECK(rewards[0] >= cfg.reward_soft - 1e-5f, "soft reward");
    printf("  bomb breaks soft ok (reward=%.3f)\n", rewards[0]);
}

static void test_flame_kills_and_win(void) {
    BMConfig cfg = bm_default_config();
    cfg.num_agents = 2;
    cfg.bomb_timer = 4;
    cfg.flame_duration = 3;
    cfg.soft_density = 0.0f;
    cfg.pillar_mode = 0;
    cfg.frames_per_cell = 0; // move every step
    cfg.reward_alive = 0.0f;
    cfg.reward_soft = 0.0f;
    cfg.reward_death = -1.0f;
    cfg.reward_kill = 1.0f;
    cfg.reward_win = 1.0f;
    cfg.max_ticks = 100;

    BMMatch m;
    bm_reset_match(&m, &cfg, 2u);
    for (int y = 1; y < m.height - 1; y++) {
        for (int x = 1; x < m.width - 1; x++) {
            m.tiles[bm_idx(&m, x, y)] = BM_TILE_EMPTY;
            m.items[bm_idx(&m, x, y)] = BM_ITEM_NONE;
        }
    }

    // A0 at (3,2), A1 at (4,2). Bomb range 1 hits (4,2). A0 walks to (1,2) (out of range).
    m.agents[0].x = 3; m.agents[0].y = 2;
    m.agents[0].bomb_range = 1;
    m.agents[0].move_cd = 0;
    m.agents[0].invuln = 0;
    m.agents[1].x = 4; m.agents[1].y = 2;
    m.agents[1].invuln = 0;

    int actions[2] = {BM_ACT_BOMB, BM_ACT_STAY};
    float rewards[2];
    float terminals[2];

    bm_step_match(&m, &cfg, actions, rewards, terminals); // place, timer=3
    actions[0] = BM_ACT_LEFT;
    bm_step_match(&m, &cfg, actions, rewards, terminals); // A0 -> (2,2), timer=2
    bm_step_match(&m, &cfg, actions, rewards, terminals); // A0 -> (1,2), timer=1
    CHECK(m.agents[0].x == 1, "bomber out of blast range");
    bm_step_match(&m, &cfg, actions, rewards, terminals); // explode

    CHECK(!m.agents[1].alive, "victim dead");
    CHECK(m.agents[0].alive, "bomber alive");
    CHECK(m.done, "match done");
    CHECK(m.winner == 0, "bomber wins");
    CHECK(terminals[0] == 1.0f && terminals[1] == 1.0f, "both terminal");
    CHECK(rewards[0] >= cfg.reward_kill + cfg.reward_win - 0.01f, "kill+win reward");
    CHECK(rewards[1] <= cfg.reward_death + 0.01f, "death reward");
    printf("  kill/win ok (r0=%.2f r1=%.2f)\n", rewards[0], rewards[1]);
}

static void test_obs_finite(void) {
    BMConfig cfg = bm_default_config();
    BMMatch m;
    bm_reset_match(&m, &cfg, 99u);
    float obs[BM_OBS_SIZE];
    bm_write_obs(&m, &cfg, 0, obs);
    for (int i = 0; i < BM_OBS_SIZE; i++) {
        CHECK(isfinite(obs[i]), "obs finite");
        // Relative agent dx/dy can be slightly outside [-1,1] at edges — allow a bit
        CHECK(obs[i] >= -1.5f && obs[i] <= 1.5f, "obs roughly normalized");
    }
    // Place a bomb and ensure danger channel lights up near plant
    BMConfig cfg2 = bm_default_config();
    cfg2.num_agents = 2;
    cfg2.soft_density = 0.0f;
    cfg2.pillar_mode = 0;
    BMMatch m2;
    bm_reset_match(&m2, &cfg2, 7u);
    for (int y = 1; y < m2.height - 1; y++)
        for (int x = 1; x < m2.width - 1; x++) {
            m2.tiles[bm_idx(&m2, x, y)] = BM_TILE_EMPTY;
            m2.items[bm_idx(&m2, x, y)] = BM_ITEM_NONE;
        }
    m2.agents[0].x = 3; m2.agents[0].y = 3;
    m2.agents[0].alive = 1;
    m2.agents[0].bomb_range = 2;
    m2.agents[1].x = 8; m2.agents[1].y = 8;
    int acts[2] = {BM_ACT_BOMB, BM_ACT_STAY};
    float r[2], t[2];
    bm_step_match(&m2, &cfg2, acts, r, t);
    float obs2[BM_OBS_SIZE];
    bm_write_obs(&m2, &cfg2, 0, obs2);
    // danger channel is index 4 in each cell
    float d_at = obs2[(3 * BM_OBS_W + 3) * BM_CELL_CH + 4];
    float d_right = obs2[(3 * BM_OBS_W + 4) * BM_CELL_CH + 4];
    CHECK(d_at > 0.0f, "danger under bomb");
    CHECK(d_right > 0.0f, "danger along blast");
    printf("  obs finite + danger channel ok (d=%.2f)\n", d_at);
}

static void test_random_rollout_no_crash(void) {
    BMConfig cfg = bm_default_config();
    cfg.num_agents = 4;
    cfg.max_ticks = 200;
    BMMatch m;
    bm_reset_match(&m, &cfg, 777u);

    uint32_t rng = 42;
    int episodes = 0;
    for (int step = 0; step < 5000; step++) {
        int actions[BM_MAX_AGENTS];
        float rewards[BM_MAX_AGENTS];
        float terminals[BM_MAX_AGENTS];
        for (int a = 0; a < m.num_agents; a++) {
            actions[a] = bm_randi(&rng, BM_NUM_ACTIONS);
        }
        bm_step_match(&m, &cfg, actions, rewards, terminals);
        if (m.done) {
            episodes++;
            bm_reset_match(&m, &cfg, bm_xorshift(&rng));
        }
    }
    CHECK(episodes > 0, "completed some episodes");
    printf("  random rollout ok (episodes=%d)\n", episodes);
}

static void test_determinism(void) {
    BMConfig cfg = bm_default_config();
    cfg.num_agents = 2;
    uint32_t seed = 424242u;

    BMMatch a, b;
    bm_reset_match(&a, &cfg, seed);
    bm_reset_match(&b, &cfg, seed);
    CHECK(memcmp(a.tiles, b.tiles, sizeof(a.tiles)) == 0, "same map");

    int actions[2] = {BM_ACT_RIGHT, BM_ACT_LEFT};
    float ra[2], rb[2], ta[2], tb[2];
    for (int t = 0; t < 50; t++) {
        bm_step_match(&a, &cfg, actions, ra, ta);
        bm_step_match(&b, &cfg, actions, rb, tb);
    }
    CHECK(a.agents[0].x == b.agents[0].x && a.agents[0].y == b.agents[0].y, "det pos0");
    CHECK(a.agents[1].x == b.agents[1].x && a.agents[1].y == b.agents[1].y, "det pos1");
    CHECK(a.tick == b.tick, "det tick");
    printf("  determinism ok\n");
}

int main(void) {
    printf("bomberman sim tests\n");
    test_obs_size();
    test_map_has_border_and_spawns();
    test_bomb_breaks_soft();
    test_flame_kills_and_win();
    test_obs_finite();
    test_random_rollout_no_crash();
    test_determinism();

    if (g_fail) {
        fprintf(stderr, "\n%d check(s) failed\n", g_fail);
        return 1;
    }
    printf("\nAll bomberman sim tests passed.\n");
    return 0;
}
