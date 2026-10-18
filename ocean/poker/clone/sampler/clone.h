#ifndef BEHAVIOR_CLONE_NATIVE_V2_H
#define BEHAVIOR_CLONE_NATIVE_V2_H
#include <stddef.h>
#include <stdint.h>

#define BC_ABI_VERSION 1
#define BC_RUNTIME_ABI_VERSION 2
#define BC_FEATURE_COUNT 64
#define BC_TREE_COUNT 32
#define BC_CELL_COUNT 480

/* All public seats and history seats are relative to the current actor.
 * Only own cards and the currently visible board prefix are representable.
 * No Poker pointer, opponent private holding, future board or outcome appears.
 * Fields are wide signed integers so validation precedes narrowing/arithmetic.
 */
typedef struct BcEvent {
    int64_t seat, street, kind, bet;
} BcEvent;

typedef struct BcInput {
    int64_t abi_version, players, policy, samples, has_profiles;
    int64_t actor, street, actor_button, pot;
    int64_t stack[2], initial[2], total[2], bet[2];
    int64_t high, increment, reopen[2], live[2], active[2], pending[2];
    int64_t raises, last_raiser, decisions, line[2][3], history_count;
    BcEvent history[4]; /* chronological last min(decisions,4) public events */
    int64_t own[2], board_count, board[4]; /* unused board entries must be -1 */
} BcInput;

/* Validate immutable compiled model identity, graph, numeric limits and SHA256.
 * Safe to call repeatedly/concurrently. Validation consumes no policy RNG. */
int bc_model_validate(void);
unsigned bc_runtime_abi_version(void);
size_t bc_input_bytes(void);
unsigned bc_model_version(void);
unsigned bc_model_domain_version(void);
const char *bc_model_domain(void);
const char *bc_size_encoding(void);
const char *bc_feature_contract_sha256(void);
const char *bc_model_npz_sha256(void);
const char *bc_model_numeric_sha256(void);
size_t bc_model_numeric_bytes(void);
int bc_model_nodes(void);

/* Feature/public-card validation does not depend on support or model outputs.
 * bc_features enforces the typed HU public/card contract but ignores policy,
 * samples and profiles. Consistent initial stacks <=20000 are representable.
 * Learned routing additionally requires policy7/samples0/no profiles/20000each.
 * No function mutates input. Optional cell output is row-major (3,2,2,2,5,4).
 * The model diagnostic ignores the support bit and averages all32 trees.
 */
int bc_features(const BcInput *input, double features[64], int *cell);
int bc_supported(const BcInput *input); /* -1 invalid/out-of-scope; 0/1 support */
int bc_diagnostic(const BcInput *input, double probabilities[5]);

/* 1 = learned action, exactly three uint32 policy draws even preflop.
 * 0 = inactive/invalid/unsupported: RNG and action are untouched. Integration
 * must then call the exact original b74 policy on its original state/RNG.
 * There is no late fallback after sampling starts. No river CFR is involved.
 */
int bc_try_action(const BcInput *input, uint64_t *rng, int32_t *action);

#ifdef BC_TESTING
/* Bypass support ONLY for fixed parity/cost fixtures; identical feature,
 * legal-mask, tree routing, three-draw sampler and decoder as production.
 * Still rejects invalid input and policy/player/street/card/model exclusions.
 */
int bc_test_action(const BcInput *input, uint64_t *rng, int32_t *action);
#endif
#endif
