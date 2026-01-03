/* Pure C demo/test file for Raid (Great Olm boss fight).
 * Build with:
 *   bash scripts/build_ocean.sh raid local (debug)
 *   bash scripts/build_ocean.sh raid fast
 *
 * Run modes:
 *   ./raid          - Interactive rendering mode (requires display)
 *   ./raid test     - Headless test mode (no rendering)
 *   ./raid bench    - Performance benchmark
 */
#include "raid.h"
#include <time.h>

// Test assertions
#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s (line %d)\n", msg, __LINE__); \
        return 1; \
    } \
} while(0)

// Headless tests for game mechanics
int run_tests(void) {
    printf("Running Raid environment tests...\n");
    int num_players = 2;

    Raid env = {
        .arena_width = DEFAULT_ARENA_WIDTH,
        .arena_height = DEFAULT_ARENA_HEIGHT,
        .num_players = num_players,
        .max_episode_ticks = 10000,
        .player_damage = 30,
        .player_hit_chance = 100,  // 100% hit for deterministic tests
        .olm_base_damage = 20,
        .prayer_reduction = 80,
    };
    init(&env);

    env.observations = calloc(num_players * OBS_SIZE, sizeof(float));
    env.actions = calloc(num_players, sizeof(int));
    env.rewards = calloc(num_players, sizeof(float));
    env.terminals = calloc(num_players, sizeof(unsigned char));

    // Test 1: Reset initializes state correctly
    printf("  Test 1: Reset initialization... ");
    c_reset(&env);
    ASSERT(env.tick == 0, "Tick should be 0 after reset");
    ASSERT(env.olm.left_claw_hp == CLAW_MAX_HP, "Left claw should be at max HP");
    ASSERT(env.olm.right_claw_hp == CLAW_MAX_HP, "Right claw should be at max HP");
    ASSERT(env.olm.head_exposed == 0, "Head should not be exposed initially");
    ASSERT(env.players[0].hp == DEFAULT_MAX_HP, "Player HP should be at max");
    printf("PASS\n");

    // Test 2: Movement actions work
    printf("  Test 2: Movement actions... ");
    c_reset(&env);
    float start_x = env.players[0].x;
    float start_y = env.players[0].y;
    env.actions[0] = 12;  // Stay in place (0,0)
    c_step(&env);
    ASSERT(env.players[0].x == start_x, "Stay action should not change x");
    ASSERT(env.players[0].y == start_y, "Stay action should not change y");

    env.actions[0] = 14;  // Move right (+2, 0) -> action = (0+2)*5 + (2+2) = 14
    c_step(&env);
    // Note: y changes, x changes by +2
    printf("PASS\n");

    // Test 3: Attack target selection
    printf("  Test 3: Attack target selection... ");
    c_reset(&env);
    ASSERT(env.players[0].attack_target == TARGET_NONE, "Default target should be none");
    env.actions[0] = ACTION_ATTACK_MAGE_CLAW;  // 26
    c_step(&env);
    ASSERT(env.players[0].attack_target == TARGET_MAGE_CLAW, "Target should be mage claw after action 26");
    printf("PASS\n");

    // Test 4: Prayer toggle
    printf("  Test 4: Prayer toggle... ");
    c_reset(&env);
    ASSERT(env.players[0].active_prayer == -1, "No prayer active initially");
    env.actions[0] = ACTION_PRAYER_BASE + 1;  // Protect mage
    c_step(&env);
    ASSERT(env.players[0].active_prayer == 1, "Protect mage should be active");
    env.actions[0] = ACTION_PRAYER_BASE + 1;  // Toggle off
    c_step(&env);
    ASSERT(env.players[0].active_prayer == -1, "Prayer should be off after toggle");
    printf("PASS\n");

    // Test 5: Olm visibility and turning (15-tile map, blind zones at cols 0-1 and 13-14)
    printf("  Test 5: Olm visibility... ");
    c_reset(&env);
    env.olm.facing = FACE_CENTER;
    env.players[0].x = 7;  // Center (middle of 15)
    env.players[0].y = 5;
    ASSERT(can_olm_see_player(&env, &env.players[0]) == 1, "Player in center should be visible");
    env.players[0].x = 1;  // Far left (col 1, in blind zone)
    ASSERT(can_olm_see_player(&env, &env.players[0]) == 0, "Player at col 1 should be invisible to center-facing Olm");
    env.olm.facing = FACE_LEFT;
    ASSERT(can_olm_see_player(&env, &env.players[0]) == 1, "Player on left should be visible to left-facing Olm");
    printf("PASS\n");

    // Test 6: Melee range detection (y=0, x=10-14 for left claw)
    printf("  Test 6: Melee range detection... ");
    c_reset(&env);
    env.players[0].x = 12;  // Within left claw range (10-14)
    env.players[0].y = 0;   // Must be at y=0 (front row, cardinal adjacent)
    ASSERT(in_melee_range(&env, &env.players[0]) == 1, "Player at (12,0) should be in melee range");
    env.players[0].x = 3;   // Outside left claw range
    ASSERT(in_melee_range(&env, &env.players[0]) == 0, "Player at (3,0) should NOT be in melee range");
    env.players[0].x = 12;
    env.players[0].y = 1;   // Not at y=0
    ASSERT(in_melee_range(&env, &env.players[0]) == 0, "Player at (12,1) should NOT be in melee range");
    printf("PASS\n");

    // Test 7: Mage range detection (9 tiles Chebyshev from right claw at x=2)
    printf("  Test 7: Mage range detection... ");
    c_reset(&env);
    env.players[0].x = 4;   // 2 tiles from x=2
    env.players[0].y = 2;   // 2 tiles from y=0, Chebyshev = max(2,2) = 2 <= 9
    ASSERT(in_mage_range(&env, &env.players[0]) == 1, "Player at (4,2) should be in mage range");
    env.players[0].x = 14;  // 12 tiles from x=2, > 9
    env.players[0].y = 0;
    ASSERT(in_mage_range(&env, &env.players[0]) == 0, "Player at (14,0) should NOT be in mage range (too far)");
    printf("PASS\n");

    // Test 8: Claw damage
    printf("  Test 8: Claw damage... ");
    c_reset(&env);
    env.players[0].x = 12;  // In left claw melee range (10-14)
    env.players[0].y = 0;   // y=0 for melee
    env.players[0].attack_target = TARGET_MELEE_CLAW;
    env.players[0].attack_cooldown = 0;
    int initial_hp = env.olm.left_claw_hp;
    process_player_attacks(&env);
    ASSERT(env.olm.left_claw_hp < initial_hp, "Left claw should take damage from melee in range");
    ASSERT(env.olm.left_claw_hp == initial_hp - env.player_damage, "Damage should equal player_damage");
    printf("PASS\n");

    // Test 9: Prayer damage reduction (with projectile travel time)
    printf("  Test 9: Prayer damage reduction... ");
    c_reset(&env);
    env.olm.attack_style = STYLE_MAGE;
    env.olm.attack_tick = 0;  // Attack this tick
    env.olm.facing = FACE_CENTER;
    env.players[0].x = 9;  // Visible (center of 19-tile map)
    env.players[0].y = 5;
    env.players[0].active_prayer = STYLE_MAGE;  // Correct prayer
    int hp_before = env.players[0].hp;
    olm_attack_tick(&env);  // Fires projectile with pending damage
    // Advance tick to let projectile land (travel time = 1+ ticks)
    env.tick += 10;  // Ensure projectile has time to land
    apply_projectile_damage(&env);  // Apply pending damage
    int dmg_with_prayer = hp_before - env.players[0].hp;

    c_reset(&env);
    env.olm.attack_style = STYLE_MAGE;
    env.olm.attack_tick = 0;
    env.olm.facing = FACE_CENTER;
    env.players[0].x = 9;
    env.players[0].y = 5;
    env.players[0].active_prayer = -1;  // No prayer
    hp_before = env.players[0].hp;
    olm_attack_tick(&env);  // Fires projectile with pending damage
    env.tick += 10;  // Advance tick
    apply_projectile_damage(&env);  // Apply pending damage
    int dmg_without_prayer = hp_before - env.players[0].hp;

    ASSERT(dmg_with_prayer < dmg_without_prayer, "Prayer should reduce damage");
    printf("PASS\n");

    // Test 10: Phase transition - claw down tracking
    printf("  Test 10: Phase transitions... ");
    c_reset(&env);
    env.olm.left_claw_hp = 0;
    check_phase_transitions(&env);
    ASSERT(env.olm.left_claw_down_tick == env.tick, "Left claw down tick should be set");
    ASSERT(env.olm.head_exposed == 0, "Head should not be exposed with one claw down");

    env.olm.right_claw_hp = 0;
    check_phase_transitions(&env);
    ASSERT(env.olm.head_exposed == 1, "Head should be exposed with both claws down");
    printf("PASS\n");

    // Cleanup
    free(env.observations);
    free(env.actions);
    free(env.rewards);
    free(env.terminals);
    c_close(&env);

    printf("All tests passed!\n");
    return 0;
}

// Performance benchmark
int run_benchmark(void) {
    printf("Running Raid performance benchmark...\n");
    int num_players = 4;

    Raid env = {
        .arena_width = DEFAULT_ARENA_WIDTH,
        .arena_height = DEFAULT_ARENA_HEIGHT,
        .num_players = num_players,
        .max_episode_ticks = 100000,
        .player_damage = 30,
        .player_hit_chance = 70,
        .olm_base_damage = 20,
        .prayer_reduction = 80,
    };
    init(&env);

    env.observations = calloc(num_players * OBS_SIZE, sizeof(float));
    env.actions = calloc(num_players, sizeof(int));
    env.rewards = calloc(num_players, sizeof(float));
    env.terminals = calloc(num_players, sizeof(unsigned char));

    c_reset(&env);

    int total_steps = 0;
    clock_t start = clock();
    double target_seconds = 5.0;

    while ((double)(clock() - start) / CLOCKS_PER_SEC < target_seconds) {
        for (int i = 0; i < num_players; i++) {
            env.actions[i] = rand() % NUM_ACTIONS;
        }
        c_step(&env);
        total_steps++;
    }

    double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
    double sps = (total_steps * num_players) / elapsed;

    printf("  Duration: %.2f seconds\n", elapsed);
    printf("  Total steps: %d\n", total_steps);
    printf("  Steps per second (SPS): %.0f\n", sps);
    printf("  Target: 1,000,000 SPS\n");
    printf("  Status: %s\n", sps >= 1000000 ? "PASS" : "NEEDS OPTIMIZATION");

    // Cleanup
    free(env.observations);
    free(env.actions);
    free(env.rewards);
    free(env.terminals);
    c_close(&env);

    return 0;
}

// Interactive rendering mode - keyboard controlled
int run_interactive(void) {
    int num_players = 1;

    Raid env = {
        .arena_width = DEFAULT_ARENA_WIDTH,
        .arena_height = DEFAULT_ARENA_HEIGHT,
        .num_players = num_players,
        .max_episode_ticks = 10000,
        .player_damage = 30,
        .player_hit_chance = 70,
        .olm_base_damage = 20,
        .prayer_reduction = 80,
        .reward_damage_dealt = 0.01f,
        .reward_damage_taken = 0.02f,
        .reward_olm_kill = 10.0f,
        .reward_death = 1.0f
    };
    init(&env);

    env.observations = calloc(num_players * OBS_SIZE, sizeof(float));
    env.actions = calloc(num_players, sizeof(int));
    env.rewards = calloc(num_players, sizeof(float));
    env.terminals = calloc(num_players, sizeof(unsigned char));

    c_reset(&env);
    c_render(&env);

    int episode = 0;
    int steps = 0;
    int pending_action = 12;  // Default: stay in place

    // Click-to-move state
    int target_tile_x = -1;  // -1 = no target
    int target_tile_y = -1;
    int scale = 32;
    int hp_bar_height = 40;
    int arena_offset = hp_bar_height + scale;  // Must match c_render

    printf("\n=== CONTROLS ===\n");
    printf("LEFT CLICK: Move to tile\n");
    printf("WASD / Arrows: Move manually\n");
    printf("1: Attack melee claw (right side, drag + melee)\n");
    printf("2: Attack mage claw (left side, drag + magic)\n");
    printf("3: Attack head (center, drag + ranged)\n");
    printf("Q: Pray melee | E: Pray mage | R: Pray range\n");
    printf("ESC: Quit\n");
    printf("================\n\n");

    int attack_action = -1;  // -1 = no attack target, else attack action

    while (!WindowShouldClose()) {
        // Check for pending click captured during previous c_render
        // (clicks are accumulated inside c_render's animation loop)
        if (env.has_pending_click) {
            int click_x = env.pending_click_x / scale;
            int click_screen_y = env.pending_click_y;

            // Check if click is on Olm row (between hp_bar_height and arena_offset)
            if (click_screen_y >= hp_bar_height && click_screen_y < arena_offset) {
                // Click on Olm row - determine attack target
                target_tile_x = -1;  // Clear move target
                if (click_x >= RIGHT_CLAW_START && click_x <= RIGHT_CLAW_END) {
                    attack_action = ACTION_ATTACK_MAGE_CLAW;
                } else if (click_x >= LEFT_CLAW_START && click_x <= LEFT_CLAW_END) {
                    attack_action = ACTION_ATTACK_MELEE_CLAW;
                } else if (click_x >= HEAD_CENTER - 1 && click_x <= HEAD_CENTER + 1) {
                    attack_action = ACTION_ATTACK_HEAD;
                }
            } else if (click_screen_y >= arena_offset) {
                // Click on arena - set move target
                int click_tile_y = (click_screen_y - arena_offset) / scale;
                if (click_x >= 0 && click_x < env.arena_width &&
                    click_tile_y >= 0 && click_tile_y < env.arena_height) {
                    target_tile_x = click_x;
                    target_tile_y = click_tile_y;
                    attack_action = -1;  // Clear attack target
                }
            }
            env.has_pending_click = 0;  // Clear after processing
        }

        // Manual keyboard movement cancels both targets
        if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) {
            target_tile_x = -1;
            attack_action = -1;
            if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) {
                pending_action = 0;  // (-2, -2) up-left
            } else if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) {
                pending_action = 4;  // (+2, -2) up-right
            } else {
                pending_action = 2;  // (0, -2) up
            }
        } else if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) {
            target_tile_x = -1;
            attack_action = -1;
            if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) {
                pending_action = 20;  // (-2, +2) down-left
            } else if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) {
                pending_action = 24;  // (+2, +2) down-right
            } else {
                pending_action = 22;  // (0, +2) down
            }
        } else if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) {
            target_tile_x = -1;
            attack_action = -1;
            pending_action = 10;  // (-2, 0) left
        } else if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) {
            target_tile_x = -1;
            attack_action = -1;
            pending_action = 14;  // (+2, 0) right
        }

        // Attack target keys (clears move target)
        if (IsKeyPressed(KEY_ONE)) {
            attack_action = ACTION_ATTACK_MELEE_CLAW;
            target_tile_x = -1;
        } else if (IsKeyPressed(KEY_TWO)) {
            attack_action = ACTION_ATTACK_MAGE_CLAW;
            target_tile_x = -1;
        } else if (IsKeyPressed(KEY_THREE)) {
            attack_action = ACTION_ATTACK_HEAD;
            target_tile_x = -1;
        }

        // Prayer keys
        if (IsKeyPressed(KEY_Q)) {
            pending_action = ACTION_PRAYER_BASE + STYLE_MELEE;
        } else if (IsKeyPressed(KEY_E)) {
            pending_action = ACTION_PRAYER_BASE + STYLE_MAGE;
        } else if (IsKeyPressed(KEY_R)) {
            pending_action = ACTION_PRAYER_BASE + STYLE_RANGE;
        }

        // Attack action takes priority (env handles drag)
        if (attack_action >= 0) {
            pending_action = attack_action;
        }
        // Otherwise compute move action from click target
        else if (target_tile_x >= 0) {
            int px = (int)env.players[0].x;
            int py = (int)env.players[0].y;
            int dx = target_tile_x - px;
            int dy = target_tile_y - py;

            if (dx == 0 && dy == 0) {
                // Reached target
                target_tile_x = -1;
                target_tile_y = -1;
                pending_action = 12;  // stay
            } else {
                // Pathfinding: reduce long axis first, then diagonal
                // 2 tiles per tick max
                int abs_dx = dx < 0 ? -dx : dx;
                int abs_dy = dy < 0 ? -dy : dy;
                int sign_dx = dx < 0 ? -1 : (dx > 0 ? 1 : 0);
                int sign_dy = dy < 0 ? -1 : (dy > 0 ? 1 : 0);
                int move_dx = 0, move_dy = 0;
                int remaining = 2;

                // First, reduce excess on long axis to equalize
                int excess = (abs_dx > abs_dy) ? (abs_dx - abs_dy) : (abs_dy - abs_dx);
                if (excess > 0) {
                    int reduce = (excess < remaining) ? excess : remaining;
                    if (abs_dx > abs_dy) {
                        move_dx = sign_dx * reduce;
                    } else {
                        move_dy = sign_dy * reduce;
                    }
                    remaining -= reduce;
                }

                // Then move diagonally with remaining movement
                if (remaining > 0 && abs_dx > 0 && abs_dy > 0) {
                    // After excess reduction, remaining distances
                    int new_abs_dx = abs_dx - (move_dx < 0 ? -move_dx : move_dx);
                    int new_abs_dy = abs_dy - (move_dy < 0 ? -move_dy : move_dy);
                    int diag = (new_abs_dx < remaining) ? new_abs_dx : remaining;
                    diag = (new_abs_dy < diag) ? new_abs_dy : diag;
                    move_dx += sign_dx * diag;
                    move_dy += sign_dy * diag;
                } else if (remaining > 0) {
                    // Only one axis has distance, continue on it
                    if (abs_dx > 0) {
                        int step = (abs_dx - (move_dx < 0 ? -move_dx : move_dx));
                        step = (step < remaining) ? step : remaining;
                        move_dx += sign_dx * step;
                    } else if (abs_dy > 0) {
                        int step = (abs_dy - (move_dy < 0 ? -move_dy : move_dy));
                        step = (step < remaining) ? step : remaining;
                        move_dy += sign_dy * step;
                    }
                }

                // Action encoding: action = (dy + 2) * 5 + (dx + 2)
                pending_action = (move_dy + 2) * 5 + (move_dx + 2);
            }
        }

        // Process game tick
        env.actions[0] = pending_action;
        c_step(&env);
        steps++;

        // Render full tick animation (handles all TICK_FRAMES internally)
        // Note: c_render captures any clicks during animation into env.has_pending_click
        c_render(&env);

        // Reset to stay unless we have an active target or movement key held
        if (attack_action < 0 && target_tile_x < 0 &&
            !IsKeyDown(KEY_W) && !IsKeyDown(KEY_S) && !IsKeyDown(KEY_A) && !IsKeyDown(KEY_D) &&
            !IsKeyDown(KEY_UP) && !IsKeyDown(KEY_DOWN) && !IsKeyDown(KEY_LEFT) && !IsKeyDown(KEY_RIGHT)) {
            pending_action = 12;  // stay
        }

        if (env.terminals[0]) {
            episode++;
            printf("Episode %d ended at tick %d. Olm kills: %.0f/%.0f (%.1f%%)\n",
                   episode, steps, env.log.olm_kills, env.log.n,
                   100.0 * env.log.olm_kills / env.log.n);
            // Clear both targets on episode end
            target_tile_x = -1;
            target_tile_y = -1;
            attack_action = -1;
        }
    }

    free(env.observations);
    free(env.actions);
    free(env.rewards);
    free(env.terminals);
    c_close(&env);

    return 0;
}

int main(int argc, char* argv[]) {
    srand(time(NULL));

    if (argc > 1) {
        if (strcmp(argv[1], "test") == 0) {
            return run_tests();
        } else if (strcmp(argv[1], "bench") == 0) {
            return run_benchmark();
        } else {
            printf("Usage: %s [test|bench]\n", argv[0]);
            printf("  (no args) - Interactive rendering mode\n");
            printf("  test      - Run headless unit tests\n");
            printf("  bench     - Run performance benchmark\n");
            return 1;
        }
    }

    return run_interactive();
}
