#define _POSIX_C_SOURCE 199309L

#include "ps_systems.h"

#include <inttypes.h>
#include <time.h>

static inline uint64_t ns_now(void) {
    struct timespec ts;
#ifdef CLOCK_MONOTONIC_RAW
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t fnv1a_bytes(const void* data, size_t size) {
    const unsigned char* p = (const unsigned char*)data;
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < size; i++) {
        h ^= (uint64_t)p[i];
        h *= 1099511628211ull;
    }
    return h;
}

static inline void bench_actions(float* actions, int t) {
    static const int pattern[] = {0, 1, 6, 4, 8, 2, 7, 3, 5};
    actions[0] = (float)pattern[(t / 37) % 9];
    actions[1] = (float)((t / 251) % 3);
}

int main(int argc, char** argv) {
    int steps = 100000;
    if (argc > 1) steps = atoi(argv[1]);
    if (steps <= 0) steps = 100000;
    int enemy_obstacle_stride = 1;
    if (argc > 2) enemy_obstacle_stride = atoi(argv[2]);
    if (enemy_obstacle_stride < 1) enemy_obstacle_stride = 1;

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
        .player_health = 1000000.0f,
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
        .enemy_obstacle_stride = enemy_obstacle_stride,
    };

    ps_init(&env);
    c_reset(&env);

    for (int t = 0; t < 1000; t++) {
        bench_actions(actions, t);
        c_step(&env);
    }

    uint64_t start = ns_now();

    for (int t = 0; t < steps; t++) {
        bench_actions(actions, t);
        c_step(&env);
    }

    uint64_t end = ns_now();
    double seconds = (double)(end - start) / 1000000000.0;
    double sps = (double)steps / seconds;

    printf("steps %d\n", steps);
    printf("enemy_obstacle_stride %d\n", env.enemy_obstacle_stride);
    printf("seconds %.6f\n", seconds);
    printf("steps_per_sec %.2f\n", sps);
    printf("tick %d\n", env.tick);
    printf("hp %.2f\n", env.hp);
    printf("level %d\n", env.level);
    printf("xp %.2f\n", env.xp);
    printf("enemies %d\n", ps_count_enemies(&env));
    printf("projectiles %d\n", ps_count_projectiles(&env));
    printf("drops %d\n", ps_count_drops(&env));
    printf("areas %d\n", ps_count_areas(&env));
    printf("obs_hash %" PRIu64 "\n", fnv1a_bytes(observations, sizeof(observations)));

    return 0;
}
