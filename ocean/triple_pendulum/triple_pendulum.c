#include "triple_pendulum.h"

int main(void) {
    float observations[TP_OBS_SIZE] = {0};
    float actions[1] = {0};
    float rewards[1] = {0};
    float terminals[1] = {0};

    TriplePendulum env = {
        .observations = observations,
        .actions = actions,
        .rewards = rewards,
        .terminals = terminals,
        .num_agents = 1,
        .rng = 1,
        .cart_mass = 1.0f,
        .link_mass = {0.1f, 0.1f, 0.1f},
        .link_length = {0.45f, 0.45f, 0.45f},
        .gravity = 9.8f,
        .force_mag = 12.0f,
        .dt = 0.02f,
    };

    init(&env);
    c_reset(&env);
    c_render(&env);
    while (!WindowShouldClose()) {
        if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) actions[0] = 0;
        else if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) actions[0] = 2;
        else actions[0] = 1;
        c_step(&env);
        c_render(&env);
    }
    c_close(&env);
    return 0;
}
