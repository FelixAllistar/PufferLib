#define BM_HEADLESS
#include "bomberman.h"
#include <assert.h>

int main(void) {
    Env env = {0};
    env.cfg = bm_default_config();
    env.cfg.reverse_curriculum = 0;
    env.cfg.max_ticks = 1;
    env.num_agents = env.cfg.num_agents = 2;
    float obs[2 * OBS_SIZE], actions[2] = {0}, rewards[2], terminals[2];
    for (int a = 0; a < 2; a++) {
        env.agents[a].observations = obs + a * OBS_SIZE;
        env.agents[a].actions = actions + a;
        env.agents[a].rewards = rewards + a;
        env.agents[a].terminals = terminals + a;
    }
    puf_reset(&env);
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
    puts("Bomberman CPU adapter: PASS");
}
