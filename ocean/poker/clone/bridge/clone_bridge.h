#ifndef PROSPECTIVE_CLONE_BRIDGE_H
#define PROSPECTIVE_CLONE_BRIDGE_H
#include "clone_input_abi.h"

#define BB_SOURCE_COMMIT "a4bfd971ae4bdc2c46b8bffef2617f368b9be017"
#define BB_CONTROL_SHA256 "b74b44f37a33736c685a2da68d3815f25a65c2dee0fc9cebdfcd7b6cbd11af4a"
#define BB_FEATURE_SHA256 "0c998b63b609e1525628b27face7cbbf97bb76396f6060874429ee1dd57cdb36"
#define BB_SIZE_ENCODING "legal_interior_fraction_f64_v2"
#define BB_MODEL_DOMAIN "behavior-clone-full-model-v2"

/* Public physical-seat state, including an UNSETTLED terminal receipt. A
 * terminal has actor255/pending0; stacks still exclude the final pot payout.
 * Only public cards are representable. Own cards travel separately to input
 * construction and never enter this public state or its rolling hash.
 */
typedef struct BbPublic {
    int64_t actor, street, button, decisions, pot, high, increment;
    int64_t live, active, pending, raises, last_raiser, board_count;
    int64_t board[5], line[2];
    int64_t stack0, initial0, total0, bet0, reopen0;
    int64_t stack1, initial1, total1, bet1, reopen1, chips_per_bb;
} BbPublic;
_Static_assert(sizeof(BbPublic) == 31 * sizeof(int64_t), "public transport ABI");

/* Root-qualified expected identities, supplied before create/begin. Empty
 * identities are errors. No v1 model or currently rejected model is supplied
 * by this design. The loading process separately verifies binary/source SHA.
 */
typedef struct BbModelBinding {
    char model_domain[64], model_npz_sha256[65], model_numeric_sha256[65];
    char module_source_closure_sha256[65], module_binary_sha256[65];
    uint64_t numeric_bytes, nodes;
} BbModelBinding;

enum BbPhase { BB_NEW, BB_RESERVED, BB_ACTIVE, BB_TERMINAL, BB_ABANDONED, BB_ERROR };
enum BbBranch { BB_ORIGINAL = 0, BB_CLONE = 1 };
typedef struct BbContext BbContext;
typedef struct BbAudit {
    uint64_t generation, events, decisions_requested, hits, fallbacks;
    uint64_t pending_request, pending_rng_before, pending_rng_after;
    int32_t phase, pending, pending_seat, pending_action, pending_branch;
    unsigned char owner[32], public_prefix_sha256[32];
} BbAudit;

int bb_public_valid(const BbPublic*);
int bb_public_next(const BbPublic*, int32_t, const int64_t board[5], int, BbPublic*);
int bb_public_transition(const BbPublic*, int32_t, const BbPublic*);
int bb_public_from_game(const void*, int lane, BbPublic*);
int bb_game_status(const void*, int lane, int64_t actor_and_street[2]);

/* The context is public and per lane. receipt_mask is a physical seat mask:
 * a transport-managed hero requires its own pending action before ack;
 * Puffer uses0 because learner/league actions originate outside this policy.
 * Either way any outstanding receipt must be acknowledged exactly first.
 */
BbContext* bb_create(const unsigned char owner[32], const BbModelBinding*, unsigned receipt_mask);
void bb_free(BbContext*);
int bb_begin(BbContext*, const unsigned char owner[32], uint64_t generation);
int bb_bind_native(BbContext*, const unsigned char owner[32], uint64_t generation, const void* game, int lane);
int bb_envelope(BbContext*, const unsigned char owner[32], uint64_t generation, int allow_terminal);
int bb_start(BbContext*, const unsigned char owner[32], uint64_t generation, const BbPublic*);
int bb_observe(BbContext*, const unsigned char owner[32], uint64_t generation,
    const BbPublic* before, int32_t action, const BbPublic* after);
int bb_abandon(BbContext*, const unsigned char owner[32], uint64_t generation);
int bb_terminal_receipt(BbContext*, const unsigned char owner[32], uint64_t generation, const BbPublic*);
int bb_protocol_error(BbContext*, const char*);
int bb_audit(const BbContext*, BbAudit*);
const char* bb_last_error(const BbContext*);

/* A new request or exact duplicate returns1 with its action. Protocol errors
 * return0 and never enter the original policy. A NULL context is an explicit
 * disabled bridge and delegates to the original a4b policy, unchanged.
 */
int bb_decide(BbContext*, const unsigned char owner[32], uint64_t generation,
    void* game, int lane, uint64_t* rng, int policy, int samples,
    const void* profiles, uint64_t request_id, int32_t* action);

/* Synchronous native/Puffer route: validate/record/close the public context
 * before the original step settles real cards. Every action source uses this
 * call, including learner and league actions that did not use bb_decide.
 * A pending own receipt requires the unchanged post-decision RNG value.
 * The pointer is read only during this call, never retained by the context.
 */
int bb_step(BbContext*, const unsigned char owner[32], uint64_t generation,
    void* game, int lane, const uint64_t* rng, int32_t action);

/* Pure builder: only own2 plus the immutable public ring are passed onward.
 * 1 eligible input;0 public scope exclusion; -1 malformed public/private data.
 */
int bb_make_input(const BbPublic*, const BcEvent physical_ring[4], unsigned count,
    int own0, int own1, int policy, int samples, int has_profiles, BcInput*);
#endif
