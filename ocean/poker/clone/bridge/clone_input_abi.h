#ifndef PROSPECTIVE_CLONE_INPUT_ABI_H
#define PROSPECTIVE_CLONE_INPUT_ABI_H
#include <stddef.h>
#include <stdint.h>

/* Input ABI1, exactly the frozen v1 feature contract. This header contains
 * no model, decoder, Poker pointer, opponent holding, future board or payoff.
 * The future module must expose its separate model/decoder version at runtime.
 */
#define BC_ABI_VERSION 1
#define BC_FEATURE_COUNT 64
typedef struct BcEvent { int64_t seat, street, kind, bet; } BcEvent;
typedef struct BcInput {
    int64_t abi_version, players, policy, samples, has_profiles;
    int64_t actor, street, actor_button, pot;
    int64_t stack[2], initial[2], total[2], bet[2];
    int64_t high, increment, reopen[2], live[2], active[2], pending[2];
    int64_t raises, last_raiser, decisions, line[2][3], history_count;
    BcEvent history[4];
    int64_t own[2], board_count, board[4];
} BcInput;
_Static_assert(sizeof(BcInput) == 480, "frozen clone input ABI1");
#endif
