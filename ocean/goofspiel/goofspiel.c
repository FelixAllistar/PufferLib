// Standalone seeded-random Goofspiel viewer.
//
//   bash build.sh goofspiel --fast
//   ./goofspiel
//   ./goofspiel env.num_cards=8 env.prize_order=1

#include "goofspiel.h"
#include "ini.h"

int main(int argc, char** argv) {
    Ini ini = {0};
    puf_ini_load_env(&ini, "goofspiel", argc - 1, argv + 1);

    Env env = {0};
    puf_init(&env, puf_ini_section(&ini, "env", 0));

    obs_t observations[GS_MAX_PLAYERS * OBS_SIZE] = {0};
    float actions[GS_MAX_PLAYERS] = {0};
    float rewards[GS_MAX_PLAYERS] = {0};
    float terminals[GS_MAX_PLAYERS] = {0};
    unsigned char masks[GS_MAX_PLAYERS * GS_MAX_CARDS] = {0};
    for (int p = 0; p < env.num_agents; p++) {
        env.agents[p].observations = observations + p * OBS_SIZE;
        env.agents[p].actions = actions + p;
        env.agents[p].rewards = rewards + p;
        env.agents[p].terminals = terminals + p;
        env.agents[p].action_mask = masks + p * GS_MAX_CARDS;
    }
    puf_reset(&env);

    puf_render(&env);
    while (!WindowShouldClose()) {
        for (int p = 0; p < env.num_agents; p++) {
            uint16_t hand = env.state.hands[p];
            int choices = __builtin_popcount((unsigned int)hand);
            int choice = (int)gs_random_bounded(&env.rng, (uint32_t)choices);
            int card = 0;
            while (choice || ((hand >> card) & 1u) == 0) {
                if ((hand >> card) & 1u) {
                    choice--;
                }
                card++;
            }
            actions[p] = (float)card;
        }
        puf_step(&env);
        puf_render(&env);
    }

    puf_close(&env);
    puf_ini_free(&ini);
    return 0;
}
