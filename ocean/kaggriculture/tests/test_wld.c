#include "../kaggriculture.h"

int main(void) {
    assert(kag_wld_reward(1, 0) == 1 && kag_wld_reward(0, 1) == -1);
    assert(kag_wld_reward(100000, 100000) == 0);
    for (int i = -100; i <= 100; i++) {
        float own = i / 7.0f, other = (i % 13) / 3.0f;
        float v = kag_paired_value(own, other);
        assert(isfinite(v) && fabsf(v) <= 1);
        assert(v == -kag_paired_value(other, own));
        float epsilon = .001f;
        float derivative = (kag_paired_value(own + epsilon, other)
            - kag_paired_value(own - epsilon, other)) / (2 * epsilon);
        assert(fabsf(derivative - kag_paired_value_derivative(v)) < .0003f);
    }
    Ini ini = {0};
    puf_ini_load_env(&ini, "kaggriculture", 0, NULL);
#ifndef KAG_WITH_PAIRED_CRITIC
    dict_set(puf_ini_section(&ini, "policy", 0), "critic_mode", 0);
#endif
    kag_configure_potential(&ini, "eval");
    if (getenv("KAG_CRITIC_MODE")) {
        assert(kag_critic_mode == atoi(getenv("KAG_CRITIC_MODE")));
        assert(puf_ini_get(&ini, "env", "reward_win_loss_draw") == 1);
    }
    Dict* cfg = puf_ini_section(&ini, "env", 0);
    dict_set(cfg, "reset_state_prob", 0);
    dict_set(cfg, "reward_win_loss_draw", 1);
    dict_set(cfg, "reward_money", 1);
    dict_set(cfg, "reward_growth_land", 0);
    dict_set(cfg, "reward_growth_crop", 0);
    dict_set(cfg, "reward_growth_animal", 0);
    dict_set(cfg, "reward_alive_daily", 0);
    dict_set(cfg, "reward_quality_scale", 0);
    for (int agents = 1; agents <= 2; agents++) {
        for (int seat = 0; seat < 2; seat++) {
            dict_set(cfg, "num_agents", agents);
            dict_set(cfg, "learner_seat", seat);
#ifdef KAG_WITH_PAIRED_CRITIC
            kag_critic_mode = 2;
#endif
            for (int result = -1; result <= 1; result++) {
                Env env = {0};
                puf_init(&env, cfg);
                float obs[2][OBS_SIZE], actions[2][NUM_ATNS] = {{0}};
                float rewards[2] = {0}, terminals[2] = {0};
                for (int a = 0; a < agents; a++) {
                    env.agents[a].observations = obs[a];
                    env.agents[a].actions = actions[a];
                    env.agents[a].rewards = rewards + a;
                    env.agents[a].terminals = terminals + a;
                }
                puf_reset(&env);
                KGAction commands[2] = {0};
                kag_apply_actions(&env, commands);
                assert(!env.game.done && rewards[0] == 0 && terminals[0] == 0);
                env.game.players[0].money = 10000 + 100 * result;
                env.game.players[1].money = 10000;
                // Terminal WLD uses final absolute cash, not reset-bank gain.
                env.policy.history[0].start_cash = 30000;
                env.policy.history[1].start_cash = 10;
                env.game.step = env.game.config.episode_steps - 2;
                kag_apply_actions(&env, commands);
                assert(env.game.done);
                for (int a = 0; a < agents; a++) {
                    int player = agents == 2 ? a : seat;
                    assert(terminals[a] == 1);
                    assert(rewards[a] == (player == 0 ? result : -result));
                }
                if (agents == 2) assert(rewards[0] == -rewards[1]);
                kag_observe(&env, false);
#ifdef KAG_WITH_PAIRED_CRITIC
                if (agents == 2) {
                    assert(!memcmp(obs[0] + KAG_ENTITY_OBS_SIZE,
                        obs[1] + KAG_ENTITY_OBS_SIZE + KAG_CRITIC_FEATURES,
                        KAG_CRITIC_FEATURES * sizeof(float)));
                    assert(!memcmp(obs[1] + KAG_ENTITY_OBS_SIZE,
                        obs[0] + KAG_ENTITY_OBS_SIZE + KAG_CRITIC_FEATURES,
                        KAG_CRITIC_FEATURES * sizeof(float)));
                }
#endif
            }
        }
    }
    // Growth remains an opt-in addition to WLD, including with the paired critic.
    dict_set(cfg, "num_agents", 2);
    dict_set(cfg, "reward_growth_land", .1f);
    dict_set(cfg, "reward_growth_crop", .01f);
    dict_set(cfg, "reward_growth_animal", .02f);
    Env bonus_env = {0};
    puf_init(&bonus_env, cfg);
    float bonus_obs[2][OBS_SIZE], bonus_actions[2][NUM_ATNS] = {{0}};
    float bonus_rewards[2] = {0}, bonus_terminals[2] = {0};
    for (int a = 0; a < 2; a++) {
        bonus_env.agents[a].observations = bonus_obs[a];
        bonus_env.agents[a].actions = bonus_actions[a];
        bonus_env.agents[a].rewards = bonus_rewards + a;
        bonus_env.agents[a].terminals = bonus_terminals + a;
    }
    puf_reset(&bonus_env);
    bonus_env.policy.history[0].peak_plots = 2;
    bonus_env.policy.history[0].peak_crops = 3;
    bonus_env.policy.history[0].peak_animals = 4;
    bonus_env.game.players[0].money = 20000;
    bonus_env.game.players[1].money = 10000;
    bonus_env.game.step = bonus_env.game.config.episode_steps - 2;
    KGAction bonus_commands[2] = {0};
    kag_apply_actions(&bonus_env, bonus_commands);
    assert(bonus_env.game.done && bonus_terminals[0] == 1);
    assert(fabsf(bonus_env.reward[0].growth[0] - .1f) < .00001f);
    assert(fabsf(bonus_env.reward[0].growth[1] - .03f) < .00001f);
    assert(fabsf(bonus_env.reward[0].growth[2] - .08f) < .00001f);
    assert(fabsf(bonus_rewards[0] - 1.21f) < .00001f);
    assert(bonus_rewards[1] == -1);
    puf_ini_free(&ini);
    puts("WLD terminal rewards, draws, paired features and derivatives PASS");
}
