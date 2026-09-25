#ifndef ENV_HEADER
#define ENV_HEADER "../kaggriculture.h"
#endif
#include ENV_HEADER

int main(void) {
    Ini ini = {0};
    puf_ini_load_env(&ini, "kaggriculture", 0, NULL);
    Dict* cfg = puf_ini_section(&ini, "env", 0);
    dict_set(cfg, "reset_state_prob", 0);
    uint64_t hash = 14695981039346656037ULL;
    unsigned rng = 123;
    int sizes[] = ACT_SIZES;
    int terminals_seen = 0;
    for (int agents = 1; agents <= 2; agents++) {
        for (int shaped = 0; shaped <= 1; shaped++) {
            dict_set(cfg, "num_agents", agents);
            dict_set(cfg, "reward_growth_land", shaped);
            dict_set(cfg, "reward_growth_crop", shaped);
            dict_set(cfg, "reward_growth_animal", shaped);
            dict_set(cfg, "reward_alive_daily", shaped);
            dict_set(cfg, "reward_quality_scale", shaped);
            Env env = {0};
            puf_init(&env, cfg);
            env.game.config.episode_steps = 33;
            float obs[2][OBS_SIZE], actions[2][NUM_ATNS];
            float rewards[2], terminals[2];
            for (int a = 0; a < agents; a++) {
                env.agents[a].observations = obs[a];
                env.agents[a].actions = actions[a];
                env.agents[a].rewards = rewards + a;
                env.agents[a].terminals = terminals + a;
            }
            puf_reset(&env);
            for (int t = 0; t < 512; t++) {
                for (int a = 0; a < agents; a++) {
                    for (int h = 0; h < NUM_ATNS; h++) {
                        rng = rng * 1664525u + 1013904223u;
                        actions[a][h] = (rng >> 8) % sizes[h];
                    }
                }
#ifdef REPLAY_API
                KGAction commands[2] = {0};
                for (int a = 0; a < agents; a++) {
                    int player = agents == 2 ? a : env.learner_seat;
                    kag_decode_multi_action(&commands[player], actions[a],
                        &env.game, player, &env.policy);
                }
                if (agents == 1 && env.bot_policy == 1) {
                    kg_rule_action(&env.game, 1 - env.learner_seat,
                        &commands[1 - env.learner_seat]);
                }
                kag_apply_actions(&env, commands);
                assert(env.game.done == (terminals[0] != 0));
                if (env.game.done) kag_reset_episode(&env);
                kag_observe(&env, false);
#else
                puf_step(&env);
#endif
                terminals_seen += terminals[0] != 0;
                const void* parts[] = {&env.game, &env.policy, &env.log,
                    rewards, terminals, obs};
                size_t lengths[] = {sizeof(env.game), sizeof(env.policy), sizeof(env.log),
                    agents * sizeof(float), agents * sizeof(float),
                    agents * OBS_SIZE * sizeof(float)};
                for (int p = 0; p < 6; p++) {
                    const unsigned char* bytes = (const unsigned char*)parts[p];
                    for (size_t i = 0; i < lengths[p]; i++) {
                        hash = (hash ^ bytes[i]) * 1099511628211ULL;
                    }
                }
            }
        }
    }
    assert(terminals_seen == 64);
    printf("reward transition trace=%016llx steps=2048 terminals=%d\n",
        (unsigned long long)hash, terminals_seen);
    puf_ini_free(&ini);
}
