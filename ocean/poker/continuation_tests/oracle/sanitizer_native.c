/* Compact new proof: malformed int64 public inputs plus retained all-in and
 * off-menu teardown. Independent from the separately tested Puffer bridge.
 * Test-only branch override and LSAN-disable stub are never production code. */
#include "candidate_probe.c"

static const unsigned char sanitizer_owner[32] = {202, 6, 10, 18};

static void sanitized_root(Poker *g, PcPublic *s) {
    const uint32_t stacks[2] = {20000, 20000};
    const uint8_t deck[9] = {4, 8, 40, 41, 50, 46, 42, 38, 34};
    const int32_t actions[6] = {19000, 0, 0, 0, 0, 0};
    poker_reset(g, 0, 1, stacks, deck);
    for (int i = 0; i < 6; i++) poker_step(g, 0, actions[i]);
    assert(pc_public_from_game(g, 0, s));
    assert(s->actor == 0 && s->street == 3 && s->decisions == 6);
}

static void sanitized_event(PcContext *c, Poker *g, uint64_t generation, int32_t action) {
    PcPublic before, after;
    int64_t board[5];
    assert(pc_public_from_game(g, 0, &before));
    for (int i = 0; i < 5; i++) board[i] = g->board[i][0];
    assert(pc_public_next(&before, action, board, 5, &after));
    poker_step(g, 0, action);
    if (g->actor[0] != POKER_DONE) {
        PcPublic actual;
        assert(pc_public_from_game(g, 0, &actual));
        assert(!memcmp(&after, &actual, sizeof(after)));
    }
    assert(pc_own(c, sanitizer_owner, generation, 4, 8));
    assert(pc_observe(c, sanitizer_owner, generation, &before, action, &after));
}

int main(void) {
    Poker *g = calloc(1, sizeof(*g));
    assert(g);
    PcPublic root, after;
    sanitized_root(g, &root);
    int64_t board[5];
    for (int i = 0; i < 5; i++) board[i] = root.board[i];
    int malformed = 0;
    for (int field = 0; field < 31; field++) {
        for (int sign = 0; sign < 2; sign++) {
            int64_t words[31];
            PcPublic bad;
            memcpy(words, &root, sizeof(words));
            words[field] = sign ? INT64_MIN : INT64_MAX;
            memcpy(&bad, words, sizeof(bad));
            assert(!pc_public_next(&bad, 0, board, 5, &after));
            malformed++;
        }
    }
    for (int test = 0; test < 2; test++) {
        sanitized_root(g, &root);
        probe_reset();
        PcContext *c = pc_create(sanitizer_owner, 0);
        assert(c);
        uint64_t generation = 100 + (uint64_t)test, rng = 711;
        int32_t action = -99;
        assert(pc_begin(c, sanitizer_owner, generation, 7, 0));
        assert(pc_own(c, sanitizer_owner, generation, 4, 8));
        assert(pc_start(c, sanitizer_owner, generation, &root));
        probe_force_local(1, 0);
        assert(probe_decide(c, sanitizer_owner, generation, g, 0, &rng, NULL, 1, &action));
        probe_force_local(0, 0);
        assert(action == 0);
        PcAudit anchored;
        assert(pc_audit(c, &anchored) && anchored.phase == PC_FOLLOWING && anchored.anchors == 1);
        sanitized_event(c, g, generation, 0);
        sanitized_event(c, g, generation, test ? 100 : 1000);
        ProbeCounts before, after_counts;
        probe_read_counts(&before);
        PcAudit state;
        assert(pc_audit(c, &state));
        if (!test) {
            RcfrNodeInfo node;
            assert(g->active[0] == 1 && state.phase == PC_FOLLOWING);
            assert(pc_node_info(c, state.node, &node));
            assert(node.action_count == 2 && node.action[0] == -1 && node.action[1] == 0);
            probe_force_local(1, UINT32_MAX);
        } else {
            assert(state.phase == PC_DISABLED);
        }
        assert(probe_decide(c, sanitizer_owner, generation, g, 0, &rng, NULL, 2, &action));
        probe_force_local(0, 0);
        assert(action == 0);
        probe_read_counts(&after_counts);
        assert(before.creates == after_counts.creates && before.iterate_calls == after_counts.iterate_calls);
        assert(before.iterations == after_counts.iterations && before.certificate_calls == after_counts.certificate_calls);
        assert(after_counts.row_draws == before.row_draws + (test ? 0u : 1u));
        assert(pc_audit(c, &state));
        assert(!memcmp(anchored.strategy_hash, state.strategy_hash, 32));
        sanitized_event(c, g, generation, 0);
        assert(g->actor[0] == POKER_DONE);
        assert(pc_end(c, sanitizer_owner, generation, PC_TERMINAL));
        assert(!pc_node_count(c));
        pc_free(c);
    }
    free(g);
    printf("native sanitizer: %d extreme int64 rejections; retained active1 hit; off-menu fallback; terminal teardown passed\n", malformed);
    return 0;
}
