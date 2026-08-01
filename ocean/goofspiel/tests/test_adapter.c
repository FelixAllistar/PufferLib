#include "../goofspiel.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void bind_env(Env* env, obs_t* observations, float* actions,
        float* rewards, float* terminals, unsigned char* masks) {
    for (int p = 0; p < env->num_agents; p++) {
        env->agents[p].observations = observations + p * OBS_SIZE;
        env->agents[p].actions = actions + p;
        env->agents[p].rewards = rewards + p;
        env->agents[p].terminals = terminals + p;
        env->agents[p].action_mask = masks + p * GS_MAX_CARDS;
    }
}

static Env make_env(void) {
    Env env = {0};
    env.cfg.num_players = 2;
    env.cfg.num_cards = 3;
    env.cfg.num_turns = 3;
    env.cfg.prize_order = GS_PRIZES_ASCENDING;
    env.cfg.information = GS_INFO_PERFECT;
    env.cfg.egocentric = 1;
    env.cfg.return_type = GS_RETURN_WIN_LOSS;
    env.cfg.tie_rule = GS_TIE_DISCARD;
    env.cfg.auto_forced_last = 1;
    env.cfg.full_hand = 7;
    env.cfg.total_points = 6;
    env.num_agents = 2;
    env.rng = gs_mix32(123);
    return env;
}

static void test_observation_views(void) {
    Env env = make_env();
    obs_t observations[2 * OBS_SIZE] = {0};
    float actions[2] = {0};
    float rewards[2] = {0};
    float terminals[2] = {0};
    unsigned char masks[2 * GS_MAX_CARDS] = {0};
    bind_env(&env, observations, actions, rewards, terminals, masks);
    puf_reset(&env);

    env.state.hands[0] = 1u << 0;
    env.state.hands[1] = 1u << 2;
    env.state.scores[0] = 3;
    env.state.scores[1] = 5;
    gs_observe(&env);

    obs_t* p0 = observations;
    obs_t* p1 = observations + OBS_SIZE;
    assert(p0[GS_O_HANDS + 0] == 1);
    assert(p0[GS_O_HANDS + GS_MAX_CARDS + 2] == 1);
    assert(p1[GS_O_HANDS + 2] == 1);
    assert(p1[GS_O_HANDS + GS_MAX_CARDS + 0] == 1);
    assert(p0[GS_O_SCORES] == 3 && p0[GS_O_SCORES + 2] == 5);
    assert(p1[GS_O_SCORES] == 5 && p1[GS_O_SCORES + 2] == 3);

    env.cfg.information = GS_INFO_HIDDEN_BIDS;
    gs_observe(&env);
    for (int card = 0; card < GS_MAX_CARDS; card++) {
        assert(p0[GS_O_HANDS + GS_MAX_CARDS + card] == 0);
        assert(p1[GS_O_HANDS + GS_MAX_CARDS + card] == 0);
    }

    env.cfg.egocentric = 0;
    gs_observe(&env);
    assert(p0[GS_O_SELF] == 1 && p0[GS_O_SELF + 1] == 0);
    assert(p1[GS_O_SELF] == 0 && p1[GS_O_SELF + 1] == 1);
    assert(p1[GS_O_HANDS + 0] == 0);
    assert(p1[GS_O_HANDS + GS_MAX_CARDS + 2] == 1);
}

static void test_masks_and_terminal_reset(void) {
    Env env = make_env();
    obs_t observations[2 * OBS_SIZE] = {0};
    float actions[2] = {0};
    float rewards[2] = {0};
    float terminals[2] = {0};
    unsigned char masks[2 * GS_MAX_CARDS] = {0};
    bind_env(&env, observations, actions, rewards, terminals, masks);
    puf_reset(&env);

    for (int p = 0; p < 2; p++) {
        assert(masks[p * GS_MAX_CARDS + 0] == 1);
        assert(masks[p * GS_MAX_CARDS + 1] == 1);
        assert(masks[p * GS_MAX_CARDS + 2] == 1);
        assert(masks[p * GS_MAX_CARDS + 3] == 0);
    }

    actions[0] = 2;
    actions[1] = 1;
    puf_step(&env);
    assert(masks[2] == 0 && masks[GS_MAX_CARDS + 1] == 0);
    assert(terminals[0] == 0 && terminals[1] == 0);

    actions[0] = 1;
    actions[1] = 2;
    puf_step(&env);
    assert(terminals[0] == 1 && terminals[1] == 1);
    assert(rewards[0] != 0 || rewards[1] != 0);
    assert(env.state.round == 0);
    assert(env.state.hands[0] == env.cfg.full_hand);
    assert(env.state.hands[1] == env.cfg.full_hand);
    for (int p = 0; p < 2; p++) {
        assert(masks[p * GS_MAX_CARDS + 0] == 1);
        assert(masks[p * GS_MAX_CARDS + 1] == 1);
        assert(masks[p * GS_MAX_CARDS + 2] == 1);
    }
    assert(env.log.n == 1.0f);
    assert(env.log.episode_length == 2.0f);
}

static void test_open_spiel_observation(void) {
    Env env = make_env();
    obs_t observations[2 * OBS_SIZE] = {0};
    float actions[2] = {0};
    float rewards[2] = {0};
    float terminals[2] = {0};
    unsigned char masks[2 * GS_MAX_CARDS] = {0};
    bind_env(&env, observations, actions, rewards, terminals, masks);
    puf_reset(&env);
    env.cfg.open_spiel_obs = 1;
    env.cfg.egocentric = 0;
    env.state.round = 1;
    env.state.prizes[0] = 0;
    env.state.prizes[1] = 1;
    env.state.scores[0] = 3;
    env.state.scores[1] = 5;
    env.state.hands[0] = 1u << 0;
    env.state.hands[1] = 1u << 2;
    gs_observe(&env);

    // n=3: 2*7 score bins, 3*3 prize history, 2*3 hands, 2 seats.
    assert(gs_open_spiel_obs_size(&env.cfg) == 31);
    assert(observations[3] == 1);
    assert(observations[7 + 5] == 1);
    assert(observations[14 + 0] == 1);
    assert(observations[14 + 3 + 1] == 1);
    assert(observations[23 + 0] == 1);
    assert(observations[23 + 3 + 2] == 1);
    assert(observations[29] == 1);
    assert(observations[OBS_SIZE + 5] == 1);
    assert(observations[OBS_SIZE + 7 + 3] == 1);
    assert(observations[OBS_SIZE + 23 + 2] == 1);
    assert(observations[OBS_SIZE + 23 + 3 + 0] == 1);
    assert(observations[OBS_SIZE + 30] == 1);
}

int main(void) {
    assert(GS_COMPACT_OBS_SIZE == 27);
    assert(OBS_SIZE == 48);
    test_observation_views();
    test_open_spiel_observation();
    test_masks_and_terminal_reset();
    printf("goofspiel adapter tests passed\n");
    return 0;
}
