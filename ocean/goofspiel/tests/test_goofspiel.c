#include "../goofspiel_core.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static GSConfig make_config(int players, int cards, int turns) {
    GSConfig cfg = {0};
    cfg.num_players = (uint8_t)players;
    cfg.num_cards = (uint8_t)cards;
    cfg.num_turns = (uint8_t)turns;
    cfg.prize_order = GS_PRIZES_ASCENDING;
    cfg.information = GS_INFO_PERFECT;
    cfg.egocentric = 1;
    cfg.return_type = GS_RETURN_WIN_LOSS;
    cfg.tie_rule = GS_TIE_DISCARD;
    cfg.auto_forced_last = 0;
    cfg.full_hand = (uint16_t)((1u << cards) - 1u);
    cfg.total_points = (uint16_t)(cards * (cards + 1) / 2);
    assert(gs_config_valid(&cfg));
    return cfg;
}

static int score_sum(const GSState* state, int players) {
    int sum = 0;
    for (int p = 0; p < players; p++) {
        sum += state->scores[p];
    }
    return sum;
}

static void test_prize_orders(void) {
    GSState state;
    GSHistory history;
    uint32_t rng = gs_mix32(1);
    GSConfig cfg = make_config(2, 5, 5);

    gs_reset(&state, &history, &cfg, &rng);
    for (int i = 0; i < 5; i++) {
        assert(state.prizes[i] == i);
    }

    cfg.prize_order = GS_PRIZES_DESCENDING;
    gs_reset(&state, &history, &cfg, &rng);
    for (int i = 0; i < 5; i++) {
        assert(state.prizes[i] == 4 - i);
    }

    cfg.prize_order = GS_PRIZES_RANDOM;
    uint32_t rng_a = gs_mix32(1234);
    uint32_t rng_b = gs_mix32(1234);
    GSState a;
    GSState b;
    GSHistory ha;
    GSHistory hb;
    gs_reset(&a, &ha, &cfg, &rng_a);
    gs_reset(&b, &hb, &cfg, &rng_b);
    assert(memcmp(a.prizes, b.prizes, cfg.num_cards) == 0);

    uint16_t seen = 0;
    for (int i = 0; i < cfg.num_cards; i++) {
        seen |= (uint16_t)(1u << a.prizes[i]);
    }
    assert(seen == cfg.full_hand);
}

static void test_discard_and_conservation(void) {
    GSConfig cfg = make_config(2, 3, 3);
    GSState state;
    GSHistory history;
    uint32_t rng = gs_mix32(7);
    gs_reset(&state, &history, &cfg, &rng);

    uint8_t first[] = {2, 1};
    assert(!gs_step(&state, &history, &cfg, first));
    assert(state.scores[0] == 1);
    assert(score_sum(&state, 2) + state.pot + state.discarded
        == gs_points_revealed(&state));

    uint8_t tied[] = {0, 0};
    assert(!gs_step(&state, &history, &cfg, tied));
    assert(state.discarded == 2);
    assert(history.winners[1] == GS_NO_WINNER);
    assert(score_sum(&state, 2) + state.pot + state.discarded
        == gs_points_revealed(&state));

    uint8_t final[] = {1, 2};
    assert(gs_step(&state, &history, &cfg, final));
    assert(state.scores[1] == 3);
    assert(state.hands[0] == 0 && state.hands[1] == 0);
    assert(score_sum(&state, 2) + state.discarded == cfg.total_points);
}

static void test_carry(void) {
    GSConfig cfg = make_config(2, 3, 3);
    cfg.tie_rule = GS_TIE_CARRY;
    GSState state;
    GSHistory history;
    uint32_t rng = gs_mix32(9);
    gs_reset(&state, &history, &cfg, &rng);

    uint8_t tied[] = {0, 0};
    assert(!gs_step(&state, &history, &cfg, tied));
    assert(state.pot == 1);

    uint8_t won[] = {2, 1};
    assert(!gs_step(&state, &history, &cfg, won));
    assert(state.scores[0] == 3);
    assert(state.pot == 0);

    uint8_t tied_final[] = {1, 2};
    assert(gs_step(&state, &history, &cfg, tied_final));
    assert(state.scores[1] == 3);
    assert(state.pot == 0);
    assert(state.discarded == 0);

    cfg = make_config(2, 2, 2);
    cfg.tie_rule = GS_TIE_CARRY;
    rng = gs_mix32(10);
    gs_reset(&state, &history, &cfg, &rng);
    uint8_t same_high[] = {1, 1};
    assert(!gs_step(&state, &history, &cfg, same_high));
    uint8_t same_low[] = {0, 0};
    assert(gs_step(&state, &history, &cfg, same_low));
    assert(state.pot == 0);
    assert(state.discarded == cfg.total_points);
}

static void test_short_game_and_forced_last(void) {
    GSState state;
    GSHistory history;
    uint32_t rng = gs_mix32(11);
    GSConfig cfg = make_config(2, 5, 3);
    cfg.auto_forced_last = 1;
    gs_reset(&state, &history, &cfg, &rng);

    uint8_t bids0[] = {4, 3};
    uint8_t bids1[] = {3, 2};
    uint8_t bids2[] = {2, 1};
    assert(!gs_step(&state, &history, &cfg, bids0));
    assert(!gs_step(&state, &history, &cfg, bids1));
    assert(gs_step(&state, &history, &cfg, bids2));
    assert(__builtin_popcount((unsigned int)state.hands[0]) == 2);
    assert(__builtin_popcount((unsigned int)state.hands[1]) == 2);

    cfg = make_config(2, 3, 3);
    cfg.auto_forced_last = 1;
    rng = gs_mix32(12);
    gs_reset(&state, &history, &cfg, &rng);
    uint8_t a[] = {2, 1};
    uint8_t b[] = {1, 2};
    assert(!gs_step(&state, &history, &cfg, a));
    assert(gs_step(&state, &history, &cfg, b));
    assert(state.round == 3);
    assert(state.hands[0] == 0 && state.hands[1] == 0);
    assert(history.bids[2][0] == 1 && history.bids[2][1] == 1);
}

static void test_returns(void) {
    GSConfig cfg = make_config(3, 5, 5);
    GSState state = {0};
    state.scores[0] = 10;
    state.scores[1] = 5;
    state.scores[2] = 5;
    float returns[GS_MAX_PLAYERS] = {0};

    gs_returns(&state, &cfg, returns);
    assert(returns[0] == 1.0f);
    assert(returns[1] == -0.5f && returns[2] == -0.5f);

    cfg.return_type = GS_RETURN_POINT_DIFFERENCE;
    gs_returns(&state, &cfg, returns);
    assert(fabsf(returns[0] - 10.0f / 3.0f) < 1e-5f);
    assert(fabsf(returns[1] + 5.0f / 3.0f) < 1e-5f);
    assert(fabsf(returns[2] + 5.0f / 3.0f) < 1e-5f);

    cfg.return_type = GS_RETURN_TOTAL_POINTS;
    gs_returns(&state, &cfg, returns);
    assert(returns[0] == 10.0f && returns[1] == 5.0f
        && returns[2] == 5.0f);

    cfg.return_type = GS_RETURN_WIN_LOSS;
    state.scores[0] = state.scores[1] = state.scores[2] = 4;
    gs_returns(&state, &cfg, returns);
    assert(returns[0] == 0.0f && returns[1] == 0.0f
        && returns[2] == 0.0f);
}

static void test_state_copy_replay(void) {
    GSConfig cfg = make_config(2, 5, 5);
    cfg.prize_order = GS_PRIZES_RANDOM;
    GSState a;
    GSHistory ha;
    uint32_t rng_a = gs_mix32(99);
    gs_reset(&a, &ha, &cfg, &rng_a);

    uint8_t first[] = {4, 3};
    assert(!gs_step(&a, &ha, &cfg, first));
    GSState b = a;
    GSHistory hb = ha;
    uint32_t rng_b = rng_a;

    uint8_t bids[][2] = {{3, 4}, {2, 1}, {1, 2}, {0, 0}};
    for (int i = 0; i < 4; i++) {
        int done_a = gs_step(&a, &ha, &cfg, bids[i]);
        int done_b = gs_step(&b, &hb, &cfg, bids[i]);
        assert(done_a == done_b);
        assert(memcmp(&a, &b, sizeof(a)) == 0);
        assert(memcmp(&ha, &hb, sizeof(ha)) == 0);
        assert(rng_a == rng_b);
        if (done_a) break;
    }
}

static uint8_t random_card(uint16_t hand, uint32_t* rng) {
    int choice = (int)gs_random_bounded(rng,
        (uint32_t)__builtin_popcount((unsigned int)hand));
    for (int card = 0; card < GS_MAX_CARDS; card++) {
        if ((hand >> card) & 1u) {
            if (choice-- == 0) return (uint8_t)card;
        }
    }
    return 0;
}

static void test_random_properties(void) {
    uint32_t rng = gs_mix32(20260730);
    for (int game = 0; game < 2000; game++) {
        int players = 2 + (int)gs_random_bounded(&rng, 9);
        int cards = 2 + (int)gs_random_bounded(&rng, 15);
        int turns = 1 + (int)gs_random_bounded(&rng, (uint32_t)cards);
        GSConfig cfg = make_config(players, cards, turns);
        cfg.prize_order = GS_PRIZES_RANDOM;
        cfg.tie_rule = (uint8_t)gs_random_bounded(&rng, 2);
        cfg.auto_forced_last = 1;

        GSState state;
        GSHistory history;
        gs_reset(&state, &history, &cfg, &rng);
        int done = 0;
        while (!done) {
            uint8_t bids[GS_MAX_PLAYERS];
            for (int p = 0; p < players; p++) {
                bids[p] = random_card(state.hands[p], &rng);
            }
            done = gs_step(&state, &history, &cfg, bids);
            assert(score_sum(&state, players) + state.pot + state.discarded
                == gs_points_revealed(&state));
            for (int p = 0; p < players; p++) {
                assert(__builtin_popcount((unsigned int)state.hands[p])
                    == cards - state.round);
            }
        }
        assert(state.round == turns);
        assert(state.pot == 0);
    }
}

int main(void) {
    test_prize_orders();
    test_discard_and_conservation();
    test_carry();
    test_short_game_and_forced_last();
    test_returns();
    test_state_copy_replay();
    test_random_properties();
    printf("goofspiel core tests passed\n");
    return 0;
}
