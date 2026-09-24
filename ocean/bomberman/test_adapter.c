#define BM_HEADLESS
#include "bomberman.h"
#include <assert.h>

int main(int argc, char** argv) {
    assert(argc == 2);
    Ini ini = {0};
    puf_ini_load_file(&ini, argv[1]);
    Env configured = {0};
    puf_init(&configured, puf_ini_section(&ini, "env", 0));
    assert(configured.num_agents == 2 && configured.cfg.width == 13);
    assert(configured.cfg.reward_kill == 1 && configured.cfg.max_ticks == 1600);
    assert(configured.agents[0].policy == 0 && configured.agents[1].policy == 1);
    puf_ini_free(&ini);
    Env env = {0};
    env.cfg = bm_default_config();
    env.cfg.reverse_curriculum = 0;
    env.cfg.max_ticks = 1;
    env.num_agents = env.cfg.num_agents = 2;
    float obs[2 * OBS_SIZE], actions[2] = {0}, rewards[2], terminals[2];
    unsigned char masks[2 * BM_NUM_ACTIONS];
    for (int a = 0; a < 2; a++) {
        env.agents[a].observations = obs + a * OBS_SIZE;
        env.agents[a].actions = actions + a;
        env.agents[a].rewards = rewards + a;
        env.agents[a].terminals = terminals + a;
        env.agents[a].action_mask = masks + a * BM_NUM_ACTIONS;
    }
    puf_reset(&env);
    for (int a = 0; a < 2; a++) {
        float expected[OBS_SIZE];
        unsigned char legal[BM_NUM_ACTIONS];
        bm_write_obs_mask(&env.match, &env.cfg, a, expected, legal);
        assert(!memcmp(obs + a * OBS_SIZE, expected, sizeof(expected)));
        assert(!memcmp(masks + a * BM_NUM_ACTIONS, legal, sizeof(legal)));
    }
    puf_step(&env);
    assert(env.match.tick == 0 && !env.match.done);
    assert(terminals[0] == 1 && terminals[1] == 1);
    assert(rewards[0] == env.cfg.reward_timeout + env.cfg.reward_alive);
    assert(rewards[1] == env.cfg.reward_timeout + env.cfg.reward_alive);
    assert(env.log.n == 2);
    assert(env.log.curriculum_stage == 2 * BM_CURRICULUM_STAGES);
    assert(env.log.curriculum_full_game == 2);
    Log full_game = {0};
    env.match.curriculum_stage = BM_CURRICULUM_STAGES - 1;
    bm_log_match(&full_game, &env.match, 0);
    assert(full_game.curriculum_full_game == 2);
    env.hold_on_done = 1;
    puf_step(&env);
    assert(env.match.done && env.match.tick == 1);
    puf_step(&env);
    assert(env.log.n == 4 && rewards[0] == 0 && terminals[0] == 1);
    Dict metrics = {0};
    puf_log(&env.log, &metrics);
    assert(dict_get(&metrics, "policy_0_score") == dict_get(&metrics, "slot_0_score"));
    assert(dict_get(&metrics, "policy_1_score") == dict_get(&metrics, "slot_1_score"));
    dict_clear(&metrics);
    puts("Bomberman CPU adapter: PASS");
}
