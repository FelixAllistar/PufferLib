#include "puffer_survivors.h"
#include "ps_render.h"

int main(void) {
    float observations[PS_OBS_SIZE] = {0};
    float actions[2] = {0};
    float rewards[1] = {0};
    float terminals[1] = {0};

    PufferSurvivors env = {
        .observations = observations,
        .actions = actions,
        .rewards = rewards,
        .terminals = terminals,
        .num_agents = 1,
        .rng = 1,
        .arena_size = 48.0f,
        .max_steps = 1000000000,
        .wave_length_steps = 600,
        .enemy_cap = 256,
        .projectile_cap = 512,
        .drop_cap = 192,
        .obstacle_count = 14,
        .enemy_spawn_rate = 0.085f,
        .elite_spawn_rate = 0.006f,
        .player_speed = 0.18f,
        .player_health = 7.0f,
        .enemy_speed = 0.0875f,
        .enemy_hp_scale = 0.65f,
        .enemy_damage_scale = 1.0f,
        .spawn_ramp = 3.2f,
        .projectile_speed = 0.42f,
        .projectile_damage = 1.0f,
        .fire_cooldown = 22.0f,
        .pickup_radius = 0.65f,
        .magnet_radius = 3.4f,
        .health_drop_rate = 0.045f,
        .health_heal = 3.0f,
        .reward_xp = 0.12f,
        .reward_kill = 0.30f,
        .reward_damage = 0.002f,
        .reward_survival = 0.0003f,
        .reward_hurt = -0.25f,
        .reward_death = -3.0f,
        .obstacle_penalty = -0.003f,
        .contact_damage = 1.0f,
        .invuln_steps = 36,
    };

    ps_init(&env);
    c_reset(&env);

    while (!IsWindowReady() || !WindowShouldClose()) {
        if (IsKeyPressed(KEY_R)) {
            c_reset(&env);
        }

        actions[0] = 0;
        if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) actions[0] = 1;
        if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) actions[0] = 2;
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) actions[0] = 3;
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) actions[0] = 4;
        if ((IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) && (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))) actions[0] = 5;
        if ((IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) && (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT))) actions[0] = 6;
        if ((IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) && (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))) actions[0] = 7;
        if ((IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) && (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT))) actions[0] = 8;

        if (env.pending_upgrade) {
            int picked = -1;
            if (IsKeyPressed(KEY_ONE)) picked = 0;
            if (IsKeyPressed(KEY_TWO)) picked = 1;
            if (IsKeyPressed(KEY_THREE)) picked = 2;
            if (picked < 0) {
                c_render(&env);
                continue;
            }
            actions[1] = (float)picked;
        } else {
            actions[1] = 0;
        }

        c_step(&env);
        c_render(&env);
    }

    c_close(&env);
    return 0;
}
