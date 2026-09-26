#include "../kaggriculture.h"

int main(int argc, char** argv) {
    Ini ini = {0};
    puf_ini_load_env(&ini, "kaggriculture", 0, NULL);
    Dict* cfg = puf_ini_section(&ini, "env", 0);
    dict_set(cfg, "num_agents", 2);
    dict_set(cfg, "reward_money", 0.4);
    dict_set(cfg, "reward_growth_land", 0);
    dict_set(cfg, "reward_growth_crop", 0);
    dict_set(cfg, "reward_growth_animal", 0);
    dict_set(cfg, "reward_alive_daily", 0);
    dict_set(cfg, "reward_quality_scale", 0);
    dict_set(cfg, "reset_state_prob", 0);
    if (argc > 1) {
        dict_set(cfg, "potential_beta", 1);
        dict_set_str(cfg, "potential_path", argv[1]);
        puf_ini_put(&ini, "train.gamma", "0.99");
        if (argc > 2) {
            puf_ini_put(&ini, "train.reward_clip", argv[2]);
        }
        kag_configure_potential(&ini, "train");
        puf_ini_free(&ini);
        return 0;
    }
    kag_frozen_potential = (KagPotential){.version = 1, .gamma = 0.99, .intercept = 2};
    for (int i = 0; i < KAG_POTENTIAL_FEATURES; i++) {
        kag_frozen_potential.inverse_scale[i] = 1;
    }
    kag_frozen_potential.weights[3] = 0.03f;
    kag_frozen_potential.weights[12] = 0.2f;
    kag_frozen_potential.weights[5] = -0.1f;
    kag_frozen_potential.weights[95] = 0.15f;
    for (int reset = 0; reset <= 1; reset++) {
        dict_set(cfg, "potential_beta", 0.7);
        Env shaped = {0};
        puf_init(&shaped, cfg);
        float reward[2], terminal[2];
        for (int p = 0; p < 2; p++) {
            shaped.agents[p].rewards = reward + p;
            shaped.agents[p].terminals = terminal + p;
        }
        KGState bank = shaped.game;
        if (reset) {
            for (int t = 0; t < 333; t++) {
                KGAction actions[2] = {0};
                for (int p = 0; p < 2; p++) {
                    kg_rule_action(&bank, p, actions + p);
                }
                kg_step(&bank, actions);
            }
            bank.players[0].money += 7000;
            shaped.reset_states = &bank;
            shaped.reset_count = 1;
            shaped.reset_probability = 1;
        }
        kag_reset_episode(&shaped);
        Env plain = shaped;
        plain.potential_beta = 0;
        float ordinary[2], unused[2];
        for (int p = 0; p < 2; p++) {
            plain.agents[p].rewards = ordinary + p;
            plain.agents[p].terminals = unused + p;
        }
        double expected[2], total[2] = {0}, discount = 1;
        for (int p = 0; p < 2; p++) {
            expected[p] = -shaped.potential_beta * shaped.reward_money *
                shaped.reward[p].last_potential;
        }
        while (!shaped.game.done) {
            KGAction actions[2] = {0};
            for (int p = 0; p < 2; p++) {
                kg_rule_action(&shaped.game, p, actions + p);
            }
            kag_apply_actions(&plain, actions);
            kag_apply_actions(&shaped, actions);
            assert(!memcmp(&plain.game, &shaped.game, sizeof(KGState)));
            for (int p = 0; p < 2; p++) {
                assert(isfinite(reward[p]));
                total[p] += discount * (reward[p] - ordinary[p]);
            }
            discount *= (float)shaped.potential.gamma;
        }
        for (int p = 0; p < 2; p++) {
            assert(shaped.reward[p].last_potential == 0);
            assert(fabs(total[p] - expected[p]) < 2e-5);
        }
        assert(shaped.log.potential_reward == shaped.reward[0].potential +
            shaped.reward[1].potential);
    }
    puts("PBRS: fresh/reset, both seats, unchanged simulator, discounted cancellation PASS");
    puf_ini_free(&ini);
}
