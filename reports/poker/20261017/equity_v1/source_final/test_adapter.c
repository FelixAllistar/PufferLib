#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include "poker.h"

int main(int argc, char** argv) {
    Dict vk = {0}, ek = {0};
    dict_set(&vk, "total_agents", 128);
    dict_set(&vk, "num_buffers", 2);
    dict_set(&vk, "num_policies", 1);
    dict_set(&ek, "num_players", POKER_PLAYERS);
    dict_set(&ek, "starting_stack", 20000);
    dict_set(&ek, "bot_policy", argc > 3 ? atoi(argv[3]) : 1);
    dict_set(&ek, "native_baseline_mask", argc > 4 ? atoi(argv[4]) : 0);
    dict_set(&ek, "native_history", argc > 5 ? atoi(argv[5]) : 0);
    dict_set(&ek, "seed", 73);
    dict_set(&ek, "rules_fraction", 0.25);
    dict_set_str(&ek, "native_league", argc > 1 ? argv[1] : "None");
    int size, starts[2], counts[2];
    Env* envs = my_vec_init(&size, starts, counts, &vk, &ek);
    float observations[128][OBS_SIZE], actions[128], rewards[128], terminals[128];
    uint8_t masks[128][POKER_ACTIONS];
    uint64_t rng = 91;
    uint8_t baseline_styles[128][POKER_PLAYERS];
    assert(size == 128 && counts[0] == 64 && starts[1] == 64);
    for (int i = 0; i < size; i++) {
        envs[i].agents[0] = (Agent){observations[i], actions + i,
            rewards + i, terminals + i, masks[i], 0};
        envs[i].tag = 0; // Trainer owns this field and writes policy-group tags.
        memcpy(baseline_styles[i], envs[i].baseline_styles, POKER_PLAYERS);
        puf_reset(envs + i);
    }
    int finished = 0;
    int steps = argc > 2 ? atoi(argv[2]) : 1000;
    struct timespec start, stop;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int step = 0; step < steps; step++) {
        for (int i = 0; i < size; i++) {
            assert(envs[i].game->actor[envs[i].lane] == 0);
            int a;
            do {
                a = poker_random(&rng) % POKER_ACTIONS;
            } while (!masks[i][a]);
            actions[i] = a;
            uint64_t hands = envs[i].profiles ? envs[i].profiles[0].hands : 0;
            puf_step(envs + i);
            if (envs[i].profiles) {
                assert(envs[i].profiles[0].hands >= hands);
                assert(!terminals[i] || envs[i].profiles[0].hands > hands);
                assert(envs[i].profiles[0].vpip <= envs[i].profiles[0].hands);
                assert(envs[i].profiles[0].pfr <= envs[i].profiles[0].vpip);
            }
            assert(!memcmp(baseline_styles[i], envs[i].baseline_styles, POKER_PLAYERS));
            assert(isfinite(rewards[i]));
            assert(terminals[i] || rewards[i] == 0);
            finished += terminals[i] != 0;
            assert(observations[i][6] >= 0 && observations[i][6] < 1);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &stop);
    double elapsed = stop.tv_sec - start.tv_sec + 1e-9 * (stop.tv_nsec - start.tv_nsec);
    for (int i = 0; i < size; i++) {
        puf_close(envs + i);
    }
    free(envs);
    dict_clear(&vk);
    dict_clear(&ek);
    printf("PASS Puffer adapter players=%d learner_decisions=%d terminal_hands=%d "
        "seconds=%.6f learner_decisions_s=%.0f\n", POKER_PLAYERS, 128 * steps,
        finished, elapsed, 128 * steps / elapsed);
}
