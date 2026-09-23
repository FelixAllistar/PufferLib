#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef ENV_HEADER
#define ENV_HEADER "../trianglepath.h"
#endif
#include ENV_HEADER

int brute_force(const TPState* state, const TPConfig* cfg) {
    int best = -1;
    for (int path = 0; path < (1 << (cfg->height - 1)); path++) {
        int col = 0;
        int total = 0;
        for (int row = 0; row < cfg->height; row++) {
            total += state->cells[row * (row + 1) / 2 + col];
            col += (path >> row) & 1;
        }
        if (total > best) {
            best = total;
        }
    }
    return best;
}

void test_oracle(void) {
    for (int h = 2; h <= 10; h++) {
        for (int seed = 1; seed <= 8; seed++) {
            TPConfig cfg = {h, 1, 9, seed, TP_REWARD_DENSE};
            TPState state = {0};
            uint32_t rng = seed * 2654435761u;
            tp_reset_state(&state, &cfg, &rng);
            int optimal = tp_optimal_total(&state, &cfg);
            assert(optimal == brute_force(&state, &cfg));
            while (!state.done) {
                int action = tp_optimal_action(&state, &cfg, state.row, state.col);
                tp_step(&state, &cfg, action);
            }
            assert(state.total == optimal);
            assert(state.steps == h - 1);
            obs_t obs[OBS_SIZE];
            tp_observe(&state, &cfg, obs);
            assert(obs[TP_MAX_CELLS] == 255);
            assert(obs[TP_MAX_CELLS + 1] == state.col * 255 / (h - 1));
        }
    }
}

void test_adapter(int height, int low, int high, int mode, int seed, int trace) {
    Dict kwargs = {0};
    dict_set(&kwargs, "height", height);
    dict_set(&kwargs, "cell_min", low);
    dict_set(&kwargs, "cell_max", high);
    dict_set(&kwargs, "seed", 42);
    dict_set(&kwargs, "reward_mode", mode);
    Env env = {0};
    env.rng = seed;
    puf_init(&env, &kwargs);
    assert(env.num_agents == 1 && env.agents[0].policy == 0);
    obs_t obs[OBS_SIZE] = {0};
    float action = 0, reward = 0, terminal = 0;
    unsigned char mask[2] = {1, 1};
    env.agents[0].observations = obs;
    env.agents[0].actions = &action;
    env.agents[0].rewards = &reward;
    env.agents[0].terminals = &terminal;
    env.agents[0].action_mask = mask;
    puf_reset(&env);
    assert(reward == 0 && terminal == 0 && env.state.steps == 0);
    uint32_t rng = seed + 1;
    int total = 0;
    int optimal_total = 0;
    for (int episode = 0; episode < 2; episode++) {
        int optimum = tp_optimal_total(&env.state, &env.cfg);
        optimal_total += optimum;
        int score = 0;
        env.tag = episode;
        for (int step = 0; step < height - 1; step++) {
            action = tp_random(&rng) % 2;
            int gain = env.state.cells[tp_cell_index(step, env.state.col)];
            if (step == height - 2) {
                gain += env.state.cells[tp_cell_index(step + 1,
                    env.state.col + (int)action)];
            }
            score += gain;
            puf_step(&env);
            assert(terminal == (step == height - 2));
            float expected = mode == TP_REWARD_DENSE ? gain : 0;
            if (terminal && mode == TP_REWARD_TERMINAL_SCORE && high > 0) {
                expected = (float)score / (height * high);
            } else if (terminal && mode == TP_REWARD_TERMINAL_OPTIMALITY && optimum > 0) {
                expected = (float)score / optimum;
            }
            assert(isfinite(reward) && reward == expected);
            assert(mask[0] == 1 && mask[1] == 1);
            assert(env.state.row == (terminal ? 0 : step + 1));
            if (trace) {
                assert(fwrite(obs, sizeof(obs), 1, stdout) == 1);
                assert(fwrite(&env.state, sizeof(env.state), 1, stdout) == 1);
                assert(fwrite(&env.rng, sizeof(env.rng), 1, stdout) == 1);
                assert(fwrite(&env.log, sizeof(env.log), 1, stdout) == 1);
                assert(fwrite(&reward, sizeof(reward), 1, stdout) == 1);
                assert(fwrite(&terminal, sizeof(terminal), 1, stdout) == 1);
            }
        }
        total += score;
        assert(env.log.n == episode + 1);
        assert(env.log.score == total);
        assert(env.log.optimal == optimal_total);
        assert(env.log.regret == optimal_total - total);
        assert(env.log.episode_length == (episode + 1) * (height - 1));
        assert(env.boundary_reached == episode);
    }
    puf_close(&env);
    dict_clear(&kwargs);
}

int main(int argc, char** argv) {
    int trace = argc == 2 && strcmp(argv[1], "--trace") == 0;
    test_oracle();
    int heights[] = {2, 6, 16, 64};
    int ranges[][2] = {{0, 0}, {1, 9}, {255, 255}};
    for (int h = 0; h < 4; h++) {
        for (int range = 0; range < 3; range++) {
            for (int mode = 0; mode < 3; mode++) {
                for (int seed = 0; seed < 8; seed++) {
                    test_adapter(heights[h], ranges[range][0], ranges[range][1],
                        mode, seed, trace);
                }
            }
        }
    }
    if (!trace) {
        puts("PASS: 72 brute-force oracles, 576 CPU episodes, all reward modes");
    }
    return 0;
}
