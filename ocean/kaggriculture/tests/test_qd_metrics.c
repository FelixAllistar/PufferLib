#include "../kaggriculture.h"

int main(void) {
    Ini ini = {0};
    puf_ini_load_env(&ini, "kaggriculture", 0, NULL);
    Dict* cfg = puf_ini_section(&ini, "env", 0);
    dict_set(cfg, "num_agents", 2);
    dict_set(cfg, "potential_beta", 0);
    dict_set(cfg, "reset_state_prob", 0);
    dict_set(cfg, "reward_money", 1);
    for (int reset = 0; reset <= 1; reset++) {
        Env env = {0};
        puf_init(&env, cfg);
        env.game.config.episode_steps = 9;
        env.game.config.turns_per_day = 24;
        float rewards[2], terminals[2];
        for (int p = 0; p < 2; p++) {
            env.agents[p].rewards = rewards + p;
            env.agents[p].terminals = terminals + p;
        }
        KGState bank = env.game;
        bank.step = 2;
        bank.players[0].unlocked_mask = 3;
        kg_new_animal(&bank.players[0], kg_tile_index(1, 1), KG_COW, bank.day);
        bank.placed_animals[0] = 1;
        bank.production_product_units[0][KG_ITEM_MILK] = 10;
        if (reset) {
            env.reset_states = &bank;
            env.reset_count = 1;
            env.reset_probability = 1;
        }
        kag_reset_episode(&env);
        while (!env.game.done) {
            if (env.game.step == 3) {
                kg_new_animal(&env.game.players[0], kg_tile_index(2, 1), KG_COW, env.game.day);
                env.game.placed_animals[0]++;
                env.game.players[0].unlocked_mask = 7;
                env.game.production_product_units[0][KG_ITEM_MILK] += 2;
                env.game.production_product_units[0][KG_ITEM_WHEAT] += 4;
            }
            KGState reference = env.game;
            KGAction commands[2] = {0};
            kg_step(&reference, commands);
            kag_apply_actions(&env, commands);
            assert(!memcmp(&reference, &env.game, sizeof(reference)));
        }
        // Seat 1 never acquires an animal or a second plot: contributes delay 1.
        float first = reset ? 0 : 0.5f;
        assert(fabsf(env.log.animal_delay - (1 + first)) < 1e-6);
        assert(env.log.animal_seen == 1);
        assert(fabsf(env.log.plot2_delay - (1 + first)) < 1e-6);
        float third = reset ? 2.0f/6 : 0.5f;
        assert(fabsf(env.log.plot3_delay - (1 + third)) < 1e-6);
        assert(env.log.animal_ref_value == 2 * KG_MARKET_DEFS[KG_ITEM_MILK].base);
        assert(env.log.crop_ref_value == 4 * KG_MARKET_DEFS[KG_ITEM_WHEAT].base);
        assert(env.log.n == 2);
        Dict metrics = {0};
        puf_log(&env.log, &metrics);
        assert(dict_get(&metrics, "animal_seen") == 1);
        dict_clear(&metrics);
        // Resetting removes all event times and production-value offsets.
        env.reset_probability = 0;
        kag_reset_episode(&env);
        assert(env.behavior[0].animal == 8 && env.behavior[0].plot2 == 8);
        assert(env.start[0].animal_value == 0 && env.start[0].crop_value == 0);
    }
    puf_ini_free(&ini);
    puts("QD telemetry: fresh/reset offsets, never sentinel, both seats and simulator parity PASS");
}
