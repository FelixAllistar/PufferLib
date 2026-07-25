/* Human playable Puffer Survivors demo.
 *
 * Build/run from this directory:
 *   make play
 *
 * Controls:
 *   WASD / arrows  move (diagonals supported)
 *   1 / 2 / 3      pick upgrade when offered
 *   R              restart run
 *   H              toggle hitboxes
 *   Q              cycle FX quality
 *   Esc            quit
 */

#include "puffer_survivors.h"

static float read_move_action(void) {
    int up = IsKeyDown(KEY_W) || IsKeyDown(KEY_UP);
    int down = IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN);
    int left = IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT);
    int right = IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT);

    if (up && left) return 5.0f;
    if (up && right) return 6.0f;
    if (down && left) return 7.0f;
    if (down && right) return 8.0f;
    if (up) return 1.0f;
    if (down) return 2.0f;
    if (left) return 3.0f;
    if (right) return 4.0f;
    return 0.0f;
}

static int read_upgrade_pick(void) {
    if (IsKeyPressed(KEY_ONE)) return 0;
    if (IsKeyPressed(KEY_TWO)) return 1;
    if (IsKeyPressed(KEY_THREE)) return 2;
    return -1;
}

int main(void) {
    float observations[PS_OBS_SIZE] = {0};
    float actions[NUM_ATNS] = {0};
    float rewards[1] = {0};
    float terminals[1] = {0};

    Env env = {0};
    env.num_agents = 1;
    env.rng = 1u;
    env.agents[0].observations = observations;
    env.agents[0].actions = actions;
    env.agents[0].rewards = rewards;
    env.agents[0].terminals = terminals;
    env.agents[0].action_mask = NULL;
    env.agents[0].policy = 0;

    // Align manual play with config/puffer_survivors.ini defaults.
    env.cfg = ps_default_config();
    env.cfg.max_steps = 24000;
    env.cfg.player_health = 8.0f;
    env.cfg.reward_xp = 0.0f;
    env.cfg.reward_kill = 0.0271491f;
    env.cfg.reward_damage = 0.0f;
    env.cfg.reward_survival = 0.0051241f;
    env.cfg.reward_hurt = -1.533688f;
    env.cfg.reward_death = -1.0f;
    env.cfg.reward_success = 1.0f;
    env.cfg.reward_pickup = 0.0200598f;
    env.cfg.reward_levelup = 0.0396332f;
    env.cfg.obstacle_penalty = -0.01f;
    env.cfg.observation_version = 9;

    ps_init(&env);
    c_reset(&env);
    c_render(&env);

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_R)) {
            c_reset(&env);
        }

        actions[0] = read_move_action();

        if (env.pending_upgrade) {
            int picked = read_upgrade_pick();
            if (picked < 0) {
                // Freeze the sim until the player chooses a card.
                c_render(&env);
                continue;
            }
            actions[1] = (float)picked;
        } else {
            actions[1] = 0.0f;
        }

        c_step(&env);
        c_render(&env);
    }

    c_close(&env);
    return 0;
}
