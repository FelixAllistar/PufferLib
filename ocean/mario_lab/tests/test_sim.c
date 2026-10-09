#include "../ml_sim.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static MLConfig config(void) {
    MLConfig c = ml_default_config(); c.course = ML_FLAT; c.length = 48;
    c.progress_reward = 0; return c;
}
static void test_controller(void) {
    MLConfig c = config(); MLState s;
    ml_reset(&s, &c, 17);
    for (int i = 0; i < 60; i++) ml_step(&s, &c, ML_RIGHT);
    assert(s.vx == c.walk_speed && s.grounded && s.y == 13 * ML_TILE);
    for (int i = 0; i < 60; i++) ml_step(&s, &c, ML_RIGHT | ML_B);
    assert(s.vx == c.run_speed);
    for (int i = 0; i < 60; i++) ml_step(&s, &c, 0);
    assert(s.vx == 0 && s.grounded);
    int apex[2];
    for (int hold = 0; hold < 2; hold++) {
        ml_reset(&s, &c, 17); apex[hold] = s.y;
        for (int i = 0; i < 120; i++) {
            ml_step(&s, &c, (hold || i == 0) ? ML_A : 0);
            apex[hold] = ml_min(apex[hold], s.y);
        }
        assert(s.grounded && s.y == 13 * ML_TILE);
        // Holding A does not automatically jump again after landing.
        if (hold) { ml_step(&s, &c, ML_A); assert(s.grounded); }
    }
    assert(apex[1] + 16 * ML_FP < apex[0]);
    assert(13 * ML_TILE - apex[1] > 3 * ML_TILE);
    puts("controller: acceleration, friction, variable jump and held-button edges PASS");
}
static MLConfig measured_config(void) {
    MLConfig c = config(); c.physics_mode = 1;
    c.accel = 14; c.friction = 13; c.run_speed = 640;
    c.jump_speed = 1024; c.gravity_hold = 32; c.max_fall = 1024; c.hold_frames = 60;
    c.player_width = 10; c.player_height = 12; return c;
}
static void test_measured_controller(void) {
    MLConfig c = measured_config(); MLState s;
    ml_reset(&s, &c, 17);
    for (int i = 0; i < 100; i++) {
        ml_step(&s, &c, 0); assert(s.grounded && s.y == 208 * ML_FP && !s.vy);
    }
    s.vx = 448;
    for (int i = 0; i < 35; i++) {
        int old = s.vx; ml_step(&s, &c, 0);
        assert(s.grounded && s.vx == ml_max(0, old - 13));
    }
    int heights[2];
    for (int running = 0; running < 2; running++) {
        ml_reset(&s, &c, 17); s.vx = running ? 640 : 0;
        int minimum = s.y;
        for (int t = 0; t < 65; t++) {
            ml_step(&s, &c, ML_A | (running ? ML_RIGHT | ML_B : 0));
            minimum = ml_min(minimum, s.y);
        }
        heights[running] = 208 * ML_FP - minimum;
        assert(s.grounded);
    }
    // Reference probes measure about 66px standing and 82.5px running.
    assert(heights[0] >= 65 * ML_FP && heights[0] <= 68 * ML_FP);
    assert(heights[1] >= 81 * ML_FP && heights[1] <= 84 * ML_FP);
    MLState released, reheld; ml_reset(&released, &c, 17); reheld = released;
    for (int t = 0; t < 22; t++) {
        ml_step(&released, &c, t < 3 ? ML_A : 0);
        ml_step(&reheld, &c, t < 3 || t >= 5 ? ML_A : 0);
        assert(released.y == reheld.y && released.vy == reheld.vy);
    }
    ml_reset(&s, &c, 17); ml_rect(&s, 0, 4, 13, 4, 2, ML_AIR);
    s.x = 5 * ML_TILE; ml_step(&s, &c, 0); assert(!s.grounded && s.vy > 0);
    c.length = 192;
    MLState left, right; ml_reset(&left, &c, 17); right = left;
    left.x = right.x = 80 * ML_TILE;
    uint32_t rng = 927;
    for (int t = 0; t < 512; t++) {
        int action = ml_rand(&rng) % 64;
        int mirrored = (action & ~(ML_LEFT | ML_RIGHT))
            | ((action & ML_LEFT) ? ML_RIGHT : 0) | ((action & ML_RIGHT) ? ML_LEFT : 0);
        ml_step(&right, &c, action); ml_step(&left, &c, mirrored);
        assert(left.x + right.x == 160 * ML_TILE && left.vx == -right.vx);
        assert(left.y == right.y && left.vy == right.vy);
    }
    c.physics_mode = 2; ml_reset(&s, &c, 17); s.vx = 448;
    for (int i = 0; i < 60; i++) ml_step(&s, &c, ML_DOWN | ML_RIGHT | ML_B);
    assert(!s.vx && s.grounded);
    ml_reset(&left, &c, 17); right = left;
    for (int i = 0; i < 100; i++) {
        ml_step(&left, &c, ML_LEFT | ML_RIGHT | ML_B);
        ml_step(&right, &c, ML_B);
        assert(!memcmp(&left, &right, sizeof(left)));
    }
    puts("measured controller: stable support, braking, reference jump heights and release latch PASS");
}
static void test_collisions_and_outcomes(void) {
    MLConfig c = config(); MLState s;
    ml_reset(&s, &c, 18);
    ml_rect(&s, 0, 10, 9, 2, 4, ML_SOLID);
    s.x = 10 * ML_TILE - ML_PLAYER_W - 1; s.vx = c.run_speed;
    ml_step(&s, &c, ML_RIGHT | ML_B);
    assert(s.x == 10 * ML_TILE - ML_PLAYER_W && s.vx == 0);
    ml_reset(&s, &c, 18); ml_rect(&s, 0, 2, 10, 2, 1, ML_BRICK);
    for (int i = 0; i < 30; i++) { ml_step(&s, &c, ML_A); assert(s.y - ML_PLAYER_H >= 11 * ML_TILE); }
    ml_reset(&s, &c, 18); ml_rect(&s, 0, 8, 13, 8, 2, ML_AIR); s.x = 10 * ML_TILE;
    for (int i = 0; i < 100 && !s.status; i++) ml_step(&s, &c, 0);
    assert(s.status == ML_DEATH && s.episode_return == -c.death_penalty);
    ml_reset(&s, &c, 18);
    while (!s.status) ml_step(&s, &c, ML_RIGHT | ML_B);
    assert(s.status == ML_CLEAR && s.tick < 300 && s.episode_return >= c.completion_reward);
    c.max_frames = 1; ml_reset(&s, &c, 18);
    assert(ml_step(&s, &c, 0) == -c.death_penalty && s.status == ML_TIMEOUT);
    puts("collisions: walls, ceilings, pits, clear and timeout PASS");
}
static void test_pipe(void) {
    MLConfig c = config(); c.course = ML_UNDERGROUND; c.progress_reward = 1;
    MLState s; ml_reset(&s, &c, 19);
    s.x = s.entry_x * ML_TILE + 4 * ML_FP; s.y = s.entry_y * ML_TILE;
    s.frontier[0] = s.x; s.grounded = 1;
    assert(ml_near_entry(&s));
    assert(ml_step(&s, &c, ML_DOWN) == 0);
    assert(s.room == 1 && s.pipe_visits == 1 && s.transition_frames == 24);
    int x = s.x;
    for (int i = 0; i < 24; i++) ml_step(&s, &c, ML_RIGHT | ML_B);
    assert(s.x == x && !s.transition_frames);
    s.x = s.exit_x * ML_TILE - ML_PLAYER_W; s.frontier[1] = s.x;
    assert(ml_step(&s, &c, ML_RIGHT) == 0);
    assert(s.room == 0 && s.pipe_returns == 1);
    assert(s.x == s.return_x * ML_TILE + 4 * ML_FP && s.y == s.return_y * ML_TILE);
    ml_enter_room(&s, 1); s.transition_frames = 0;
    s.x = s.exit_x * ML_TILE; s.y = 11 * ML_TILE; s.grounded = 1;
    ml_step(&s, &c, ML_RIGHT);
    assert(s.room == 1); // Walking on top of an exit pipe must not enter its side.
    ml_reset(&s, &c, 19); s.x = s.goal_x; ml_step(&s, &c, 0);
    assert(s.status == ML_RUNNING); // Required underground task cannot be bypassed.
    s.pipe_returns = 1; ml_step(&s, &c, 0); assert(s.status == ML_CLEAR);
    puts("pipes: down entry, timed transition, underground return, task gate and no teleport reward PASS");
}
static void test_enemies(void) {
    MLConfig c = config(); MLState s;
    ml_reset(&s, &c, 20); ml_add_enemy(&s, 10, 0);
    MLEnemy* e = &s.enemies[0]; e->vx = 0;
    s.x = e->x; s.y = e->y - 12 * ML_FP; s.vy = 2 * ML_FP; s.grounded = 0;
    ml_step(&s, &c, 0);
    assert(!e->alive && s.kills == 1 && s.vy < 0 && !s.status);
    ml_reset(&s, &c, 20); ml_add_enemy(&s, 10, 0); e = &s.enemies[0]; e->vx = 0;
    s.x = e->x; s.y = e->y; ml_step(&s, &c, 0); assert(s.status == ML_DEATH);
    puts("enemies: stomp bounce and side collision PASS");
}
static void test_observation_and_restore(void) {
    MLConfig c = config(); MLState a, b; float oa[ML_OBS_SIZE], ob[ML_OBS_SIZE];
    ml_reset(&a, &c, 21); a.x = 10 * ML_TILE + 73;
    ml_observe(&a, &c, oa); b = a; b.theme = (a.theme + 1) % 3; ml_observe(&b, &c, ob);
    assert(!memcmp(oa, ob, sizeof(oa)));
    b = a;
    for (int room = 0; room < ML_ROOMS; room++) for (int y = 0; y < ML_HEIGHT; y++)
        for (int x = ML_WIDTH - 1; x >= 4; x--) b.tiles[room][y][x] = a.tiles[room][y][x - 4];
    b.x += 4 * ML_TILE; b.goal_x += 4 * ML_TILE; b.entry_x += 4; b.return_x += 4;
    ml_observe(&b, &c, ob); assert(!memcmp(oa, ob, sizeof(oa)));
    a.theme = b.theme = 0; b = a;
    uint32_t rng = 1;
    for (int t = 0; t < 300; t++) {
        int action = (int)(ml_rand(&rng) % 64);
        assert(ml_step(&a, &c, action) == ml_step(&b, &c, action));
        assert(!memcmp(&a, &b, sizeof(a)));
        ml_observe(&a, &c, oa);
        for (int i = 0; i < ML_OBS_SIZE; i++) assert(isfinite(oa[i]));
    }
    puts("observations: palette invariance, egocentric translation and exact snapshot replay PASS");
}
static void test_generation(void) {
    MLConfig c = ml_default_config(); MLState a, b;
    for (int mode = 0; mode <= ML_COMPOSITE; mode++) for (int seed = 1; seed <= 100; seed++) {
        c.course = mode; c.length = seed == 1 ? 48 : seed == 100 ? ML_WIDTH : 48 + (seed * 37) % 145;
        c.difficulty = seed % 3;
        ml_reset(&a, &c, (uint32_t)seed); ml_reset(&b, &c, (uint32_t)seed);
        assert(!memcmp(&a, &b, sizeof(a)));
        assert(a.grounded && ml_tile(&a, 0, 2, 13));
        assert(ml_tile(&a, 0, a.goal_x / ML_TILE, 13));
        for (int r = 0; r < ML_ROOMS; r++) for (int y = 0; y < ML_HEIGHT; y++)
            for (int x = 0; x < a.widths[r]; x++) assert(a.tiles[r][y][x] <= ML_PIPE);
        for (int i = 0; i < 200; i++) ml_step(&a, &c, ml_script_action(&a));
    }
    c.split = 0; uint32_t train = ml_initial_seed(&c, 0);
    c.split = 1; assert(train != ml_initial_seed(&c, 0));
    puts("generator: 800 seeded courses, supported bounds and distinct split streams PASS");
}
static void test_full_levels(void) {
    MLConfig c = measured_config(); c.physics_mode = 2; c.generator_mode = 1;
    c.course = ML_MIXED; c.length_min = 256; c.length = ML_MAX_WIDTH; c.max_frames = 8000;
    c.difficulty = 2; c.pipe_min_height = 2; c.pipe_max_height = 4; c.stair_height = 8;
    MLState a, b; int lengths[ML_MAX_WIDTH + 1] = {0}, seen[8] = {0}, rooms[129] = {0};
    int optional_pipes = 0, max_enemies = 0;
    for (int seed = 1; seed <= 400; seed++) {
        ml_reset(&a, &c, seed); ml_reset(&b, &c, seed);
        assert(!memcmp(&a, &b, sizeof(a)));
        assert(a.generator_mode && a.task == ML_TASK_FULL && a.x == 2 * ML_TILE);
        assert(a.widths[0] >= 256 && a.widths[0] <= ML_MAX_WIDTH && a.sections >= 3);
        lengths[a.widths[0]]++; rooms[a.widths[1]]++;
        assert(a.goal_x == (a.widths[0] - 5) * ML_TILE && a.episode_limit == 8000);
        assert(ml_tile(&a, 0, 2, 13) && ml_tile(&a, 0, a.goal_x / ML_TILE, 13));
        for (int k = 1; k < 8; k++) seen[k] += a.motifs[k];
        optional_pipes += a.entry_x > 0;
        int enemies = 0;
        for (int k = 0; k < ML_ENEMY_POOL; k++) enemies += ml_enemy_const(&a, k)->alive;
        max_enemies = ml_max(max_enemies, enemies);
        for (int room = 0; room < ML_ROOMS; room++) for (int y = 0; y < ML_HEIGHT; y++)
            for (int x = 0; x < a.widths[room]; x++) assert(ml_tile(&a, room, x, y) <= ML_PIPE);
        // Both sides of the old storage boundary must participate in collision.
        ml_rect(&a, 0, ML_WIDTH - 1, 13, 3, 2, ML_AIR);
        assert(!ml_tile(&a, 0, ML_WIDTH - 1, 13) && !ml_tile(&a, 0, ML_WIDTH, 13));
        ml_rect(&a, 0, ML_WIDTH, 9, 2, 4, ML_SOLID);
        int x = ML_WIDTH * ML_TILE - c.player_width * ML_FP - 1, vx = 640;
        ml_move_x(&a, 0, &x, 13 * ML_TILE, &vx, c.player_width * ML_FP, c.player_height * ML_FP);
        assert(x == ML_WIDTH * ML_TILE - c.player_width * ML_FP && !vx);
        a = b;
        uint32_t rng = seed;
        for (int step = 0; step < 200; step++) {
            int action = ml_rand(&rng) % 64;
            assert(ml_step(&a, &c, action) == ml_step(&b, &c, action));
            assert(!memcmp(&a, &b, sizeof(a)));
        }
    }
    int unique_lengths = 0, unique_rooms = 0;
    for (int k = 256; k <= ML_MAX_WIDTH; k++) unique_lengths += lengths[k] > 0;
    for (int k = 48; k <= 128; k++) unique_rooms += rooms[k] > 0;
    assert(unique_lengths > 100 && unique_rooms > 50 && optional_pipes > 50 && optional_pipes < 250);
    for (int k = ML_FLAT; k <= ML_UNDERGROUND; k++) assert(seen[k] > 0);
    assert(max_enemies > ML_ENEMIES);
    // Enemy features must describe nearby entities from the entire pool, not
    // disappear after the first eight authored enemies have been passed.
    c.course = ML_FLAT; c.length_min = 0; ml_reset(&a, &c, 9);
    for (int k = 0; k < ML_ENEMIES; k++) ml_add_enemy(&a, 20 + k, 0);
    ml_add_enemy(&a, 400, 0); a.x = 399 * ML_TILE; ml_visible_enemies(&a);
    float obs[ML_OBS_SIZE]; ml_observe(&a, &c, obs);
    assert(a.visible_enemies[0] == 8 && obs[ML_EGO] == 1 && obs[ML_EGO + 1] > 0);
    // An easy full-size course has one start and one endpoint, with no reset at
    // a section boundary; successful trajectories exceed the former frame cap.
    ml_reset(&a, &c, 9);
    while (!a.status) ml_step(&a, &c, ML_RIGHT | ML_B);
    assert(a.status == ML_CLEAR && a.tick > 3000 && !a.task);
    printf("full levels: 400 seeded worlds; %d lengths, %d room widths, up to %d enemies; boundary collisions, local entities and long completion PASS\n",
        unique_lengths, unique_rooms, max_enemies);
}
static MLConfig practice_config(int task) {
    MLConfig c = ml_default_config(); c.practice_prob = 1; c.practice_frames = 600;
    c.practice_gap = task == ML_TASK_GAP; c.practice_entry = task == ML_TASK_ENTRY;
    c.practice_exit = task == ML_TASK_EXIT; c.practice_route = task == ML_TASK_ROUTE;
    c.difficulty = 2; return c;
}
static void test_practice(void) {
    for (int task = ML_TASK_GAP; task <= ML_TASK_ROUTE; task++) for (int seed = 1; seed <= 100; seed++) {
        MLConfig c = practice_config(task); MLState s, copy;
        ml_reset(&s, &c, seed); ml_reset(&copy, &c, seed);
        assert(!memcmp(&s, &copy, sizeof(s)) && s.task == task && s.episode_limit == c.practice_frames);
        assert(s.tick == 0 && s.progress == 0 && s.episode_return == 0 && s.grounded);
        assert(s.frontier[s.room] == s.x && !s.transition_frames);
        assert(ml_tile(&s, s.room, ml_floor(s.x, ML_TILE), ml_floor(s.y, ML_TILE)));
        if (task == ML_TASK_GAP) {
            int end = s.goal_x / ML_TILE - 3, start = end - 1;
            while (!s.tiles[0][13][start - 1]) start--;
            assert(s.x < start * ML_TILE && s.y == 13 * ML_TILE && !ml_requires_pipe(&s, &c));
            MLState airborne = s; airborne.x = s.goal_x; airborne.grounded = 0;
            assert(!ml_goal_reached(&airborne, &c)); // Crossing while airborne is not a landing.
            while (!s.status) {
                int a = ML_RIGHT | ML_B;
                if (!s.grounded || (s.x >= start * ML_TILE - 12 * ML_FP && s.x < end * ML_TILE)) a |= ML_A;
                ml_step(&s, &c, a);
            }
            assert(s.status == ML_CLEAR && s.grounded && s.tick < 240);
        } else {
            assert(ml_requires_pipe(&s, &c));
            if (task != ML_TASK_EXIT) {
                // Isolate entry semantics from the approach policy in this check.
                s.x = s.entry_x * ML_TILE + 4 * ML_FP; s.y = s.entry_y * ML_TILE;
                s.frontier[0] = s.x;
                ml_step(&s, &c, ML_DOWN); assert(!s.status && s.room == 1);
                for (int t = 0; t < 23; t++) { ml_step(&s, &c, 0); assert(!s.status); }
                ml_step(&s, &c, 0);
                if (task == ML_TASK_ENTRY) { assert(s.status == ML_CLEAR); continue; }
                assert(!s.status); // Entry alone never clears a required route.
            }
            while (!s.status) ml_step(&s, &c, ML_RIGHT | ML_B);
            assert(s.status == ML_CLEAR && s.room == 0 && s.pipe_returns && !s.transition_frames);
        }
    }
    MLConfig c = ml_default_config(); c.practice_prob = 0.8f;
    int counts[5] = {0};
    for (int seed = 1; seed <= 1000; seed++) {
        MLState s; ml_reset(&s, &c, seed); counts[s.task]++;
        if (!s.task) assert(s.episode_limit == c.max_frames && s.x == 2 * ML_TILE);
    }
    for (int i = 0; i < 5; i++) assert(counts[i] > 0);
    c.mix_flat = c.mix_gaps = c.mix_pipes = c.mix_stairs = c.mix_underground = c.mix_composite = 0;
    c.mix_walkers = 1;
    for (int seed = 1; seed <= 1000; seed++) {
        MLState s; ml_reset(&s, &c, seed);
        assert(s.task != ML_TASK_FULL || s.course == ML_WALKERS);
        assert(s.task != ML_TASK_GAP || s.course == ML_GAPS);
    }
    c = practice_config(ML_TASK_GAP); c.practice_short_goals = 0;
    MLState full, local; ml_reset(&full, &c, 91);
    c.practice_short_goals = 1; ml_reset(&local, &c, 91);
    assert(full.task == ML_TASK_GAP && full.goal_task == ML_TASK_FULL);
    assert(full.x == local.x && full.vx == local.vx && !memcmp(full.tiles, local.tiles, sizeof(full.tiles)));
    assert(full.goal_x == (c.length - 5) * ML_TILE && local.goal_x < full.goal_x);
    full.x = local.x = local.goal_x; full.vx = local.vx = 0;
    ml_step(&full, &c, 0); ml_step(&local, &c, 0);
    assert(full.status == ML_RUNNING && local.status == ML_CLEAR);
    full.x = full.goal_x; ml_step(&full, &c, 0); assert(full.status == ML_CLEAR);
    puts("practice: 400 seeded gap/pipe setups, grounded landings, timed transfer goals and root/practice mixture PASS");
}
int main(void) {
    test_controller(); test_measured_controller(); test_collisions_and_outcomes(); test_pipe(); test_enemies();
    test_observation_and_restore(); test_generation(); test_practice(); test_full_levels();
    printf("Mario Lab CPU core PASS: %d observations; state %zu bytes\n", ML_OBS_SIZE, sizeof(MLState));
}
