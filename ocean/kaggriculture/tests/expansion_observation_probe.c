/* Read-only source diagnostics, not a policy evaluation or a passing
 * regression test for egocentric farm ownership. The collision is deliberately
 * reported so a future versioned observation fix can reproduce the problem.
 * Define KAG_AUDIT_HEADER to compile against a downloaded deployed header. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef KAG_AUDIT_HEADER
#define KAG_AUDIT_HEADER "../kaggriculture.h"
#endif
#include KAG_AUDIT_HEADER

static Env* fresh(void) {
    Env* env = calloc(1, sizeof(*env));
    assert(env);
    KGConfig config;
    kg_config_default(&config);
    config.weed_spawn_chance = 0;
    kg_init(&env->game_storage, &config);
    env->macro_mode = 2;
    env->macro_decision_interval = 1;
    env->macro_score_scale = 10000;
    env->reward_expansion_scale = 10;
    env->reward_expansion_deadline = 672;
    env->reward_expansion_land_target = 4;
    env->reward_expansion_plant_target = 60;
    env->reward_expansion_animal_target = 12;
    return env;
}

static void expansion_checks(void) {
    Env* env = fresh();
    KGState* game = &env->game_storage;
    /* Inject only the capital prerequisite to isolate EXPAND execution. This
     * does NOT demonstrate that a learned policy can finance its investments. */
    game->players[0].money = 10000;
    kag_reset_expansion_peaks(env, 0);
    float reward = 0;
    for (int purchase = 0; purchase < 3; purchase++) {
        KGAction actions[2] = {0};
        assert(kag_macro_candidate_legal(env, 0, KAG_MACRO_EXPAND));
        kag_macro_action_ex(game, 0, KAG_MACRO_EXPAND, 1, 0, 1, &actions[0]);
        kg_step(game, actions);
        reward += kag_expansion_reward(env, 0);
        assert(kag_popcount(game->players[0].unlocked_mask) == purchase + 2);
    }
    assert(game->players[0].money == 3000);
    assert(fabsf(reward - 10) < 1e-5f);
    assert(!kag_macro_candidate_legal(env, 0, KAG_MACRO_EXPAND));
    assert(fabsf(kag_expansion_progress(env, 4, 60, 12) - 5) < 1e-5f);
    assert(kag_expansion_reward(env, 0) == 0);
    printf("EXPAND: three real orders unlocked four plots; cost=7000; land reward=%.3f\n", reward);
    printf("TARGET: 4 plots / 60 peak plants / 12 peak animals => total un-discounted reward=50\n");

    /* Counts are deliberately independent episode peaks. Losing an animal
     * and replacing it below the old peak must not pay again. */
    kg_new_animal(&game->players[0], 0, KG_COW, game->day);
    assert(kag_expansion_reward(env, 0) > 0);
    kg_set_player_tile(&game->players[0], 0, KG_TILE_EMPTY);
    assert(kag_expansion_reward(env, 0) == 0);
    kg_new_animal(&game->players[0], 0, KG_COW, game->day);
    assert(kag_expansion_reward(env, 0) == 0);
    game->step = 673;
    kg_new_animal(&game->players[0], 1, KG_COW, game->day);
    assert(kag_expansion_reward(env, 0) == 0);
    puts("REWARD: no repeated-count farming; no reward after deadline; survival itself is not rewarded");
    free(env);
}

static void ownership_probe(void) {
    Env* env = fresh();
    KGState* game = &env->game_storage;
    obs_t observations[2][OBS_SIZE] = {{0}};
    /* Equal public cash/positions/private stock; distinct crops at the same
     * location away from the farmer. Route classes/geometry remain equal. */
    game->players[1] = game->players[0];
    kg_new_plant(&game->players[0], 0, KG_WHEAT, 0, game->config.turns_per_day);
    kg_new_plant(&game->players[1], 0, KG_STRAWBERRY, 0, game->config.turns_per_day);
    for (int player = 0; player < 2; player++) {
        env->agents[player].observations = observations[player];
        kag_write_observation(env, player);
    }
    int differences = 0;
    for (int i = 0; i < OBS_SIZE; i++) {
        if (observations[0][i] != observations[1][i]) {
            if (differences < 8) printf("OBS difference byte=%d seat0=%d seat1=%d\n",
                i, observations[0][i], observations[1][i]);
            differences++;
        }
    }
    printf("OWNERSHIP: seat0 owns wheat; seat1 owns strawberry; differing observation bytes=%d/%d\n",
        differences, OBS_SIZE);
    if (differences == 0) {
        puts("CONFIRMED: different own-farm identities have identical current observations across seats");
    }
    free(env);
}

int main(void) {
    expansion_checks();
    ownership_probe();
    return 0;
}
