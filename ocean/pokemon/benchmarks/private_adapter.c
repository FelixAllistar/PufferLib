// Evaluation-only extension. No training ABI or observation changes.
#include "adapter.c"

// The external opponent receives its own existing player observation, never
// bench_state/bench_fields, the learner's team array, or engine memory.
int private_observation(void* handle, uint8_t* out) {
    Benchmark* b = handle;
    if (b->game.phase != PK_PHASE_BATTLE) return -1;
    memcpy(out, b->game.obs[1-b->learner], PK_OBS);
    return b->game.result;
}

int private_step(void* handle, int action) {
    int selected = -1;
    return bench_step(handle, action, &selected);
}

// Team-strength control only: an external policy also drives the learner seat
// (the checkpoint just supplies the fixed team and lead). Same private views.
int private_learner_observation(void* handle, uint8_t* out) {
    Benchmark* b = handle;
    if (b->game.phase != PK_PHASE_BATTLE) return -1;
    memcpy(out, b->game.obs[b->learner], PK_OBS);
    return b->game.result;
}

int private_step_both(void* handle, int learner_action, int opponent_action) {
    Benchmark* b = handle; int p = b->learner;
    if (b->game.result) return b->game.result;
    if (learner_action < 0 || learner_action >= PK_ACTIONS || !b->game.masks[p][learner_action]
            || opponent_action < 0 || opponent_action >= PK_ACTIONS || !b->game.masks[1-p][opponent_action]) return -1;
    return pk_game_step(&b->game, p == 0 ? learner_action : opponent_action, p == 0 ? opponent_action : learner_action);
}

// Team-strength control: draft BOTH sides from given 30-byte teams (species + four moves each,
// in order; the first Pokemon leads). The checkpoint's team and policy are not consulted.
int private_reset_teams(void* handle, uint64_t seed, int learner, const uint8_t* ours, const uint8_t* theirs) {
    Benchmark* b = handle; b->learner = learner;
    PKMon mine[6]; memcpy(mine, ours, sizeof(mine)); memcpy(b->fixed, theirs, sizeof(b->fixed));
    memset(&b->game, 0, sizeof(b->game)); b->game.draft = 1; b->game.max_updates = 512; b->game.rng = seed;
    pk_game_reset(&b->game);
    while (b->game.phase != PK_PHASE_BATTLE && !b->game.result) {
        int act[2];
        for (int side = 0; side < 2; side++) {
            const PKMon* t = side == learner ? mine : b->fixed;
            int a;
            if (b->game.phase == PK_PHASE_SPECIES) a = t[b->game.picks].species - 1;
            else {
                int i = b->game.move_index[side];
                int n = i < 6 ? pk_move_count(&b->game.teams[side][i]) : 4;
                int move = i < 6 && n < 4 ? t[i].moves[n] : 0;
                a = move ? move - 1 : PK_MOVE_DONE;
            }
            // -100 - 10*side - phase: which side's draft pick was illegal, and in which phase.
            if (!b->game.masks[side][a]) return -100 - 10 * (side == learner ? 0 : 1) - b->game.phase;
            act[side] = a;
        }
        if (pk_game_step(&b->game, act[0], act[1]) == 4) return -2;
    }
    return b->game.result;
}

// Mirrored test vs Metamon: our policy pilots the given 30-byte team `ours` (species + four moves each, the
// first Pokemon leads), drafting it under forced masks while its policy still runs, so its recurrent draft
// history is the usual one; the other side drafts `theirs`. Same draft loop as bench_reset.
int private_reset_mirror(void* handle, uint64_t seed, int learner, const uint8_t* ours, const uint8_t* theirs) {
    Benchmark* b = handle; b->learner = learner;
    PKMon mine[6]; memcpy(mine, ours, sizeof(mine)); memcpy(b->fixed, theirs, sizeof(b->fixed));
    memset(&b->game, 0, sizeof(b->game)); b->game.draft = 1; b->game.max_updates = 512; b->game.rng = seed;
    pk_reset_policy(&b->policy, seed);
    pk_game_reset(&b->game);
    while (b->game.phase != PK_PHASE_BATTLE && !b->game.result) {
        int act[2];
        for (int side = 0; side < 2; side++) {
            const PKMon* t = side == learner ? mine : b->fixed;
            int a;
            if (b->game.phase == PK_PHASE_SPECIES) a = t[b->game.picks].species - 1;
            else {
                int i = b->game.move_index[side];
                int n = i < 6 ? pk_move_count(&b->game.teams[side][i]) : 4;
                int move = i < 6 && n < 4 ? t[i].moves[n] : 0;
                a = move ? move - 1 : PK_MOVE_DONE;
            }
            if (!b->game.masks[side][a]) return -100 - 10 * (side == learner ? 0 : 1) - b->game.phase;
            act[side] = a;
        }
        memset(b->game.masks[learner], 0, PK_ACTIONS); b->game.masks[learner][act[learner]] = 1;
        memcpy(b->game.obs[learner] + 480, b->game.masks[learner], PK_ACTIONS);
        int chosen = pk_policy_action(&b->policy, b->game.obs[learner], b->game.masks[learner], b->deterministic);
        if (chosen != act[learner]) return -3;
        if (pk_game_step(&b->game, act[0], act[1]) == 4) return -2;
    }
    return b->game.result;
}
