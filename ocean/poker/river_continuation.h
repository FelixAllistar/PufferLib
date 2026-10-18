#ifndef POKER_RIVER_CONTINUATION_H
#define POKER_RIVER_CONTINUATION_H
#include <stddef.h>
#include <stdint.h>
#include "river_cfr.h"

/* New recovery prototype, never an implicit replacement of the stateless ABI.
 * Public snapshots are UNSETTLED: terminal pot and contributions are retained,
 * stacks exclude winner payouts. No private cards occur in this transport. */
typedef struct PcPublic {
    int64_t actor, street, button, decisions, pot, high, increment;
    int64_t live, active, pending, raises, last_raiser, board_count;
    int64_t board[5], line[2];
    int64_t stack0, initial0, total0, bet0, reopen0;
    int64_t stack1, initial1, total1, bet1, reopen1;
    int64_t chips_per_bb;
} PcPublic;
enum PcPhase { PC_UNSTARTED, PC_UNANCHORED, PC_FOLLOWING, PC_DISABLED,
    PC_ENDED, PC_ERROR };
enum PcEndReason { PC_TERMINAL = 1, PC_ABANDONED = 2, PC_POLICY_CHANGE = 3 };
typedef struct PcContext PcContext;
typedef struct PcAudit {
    uint64_t generation, events, requests, creates, iterate_calls, iterations;
    uint64_t pending_request, pending_rng_before, pending_rng_after;
    uint64_t anchors, hits, fallbacks;
    int32_t phase, seat, node, checkpoint, pending, pending_action, solve_status;
    unsigned char owner[32], public_hash[32], prefix_hash[32], anchor_hash[32];
    unsigned char strategy_hash[32], whole_hash[32];
} PcAudit;

PcContext* pc_create(const unsigned char owner[32], int seat);
void pc_free(PcContext* context);
int pc_begin(PcContext* context, const unsigned char owner[32], uint64_t generation,
    int policy, int samples);
int pc_envelope(PcContext* context, const unsigned char owner[32], uint64_t generation);
int pc_closed_envelope(PcContext* context, const unsigned char owner[32], uint64_t generation);
int pc_closed_receipt(PcContext* context, const unsigned char owner[32], uint64_t generation,
    const PcPublic* terminal, int card0, int card1);
int pc_start(PcContext* context, const unsigned char owner[32], uint64_t generation,
    const PcPublic* initial);
/* Must be called on every external response, including a terminal response. */
int pc_own(PcContext* context, const unsigned char owner[32], uint64_t generation,
    int card0, int card1);
int pc_observe(PcContext* context, const unsigned char owner[32], uint64_t generation,
    const PcPublic* before, int32_t action, const PcPublic* after);
int pc_decide(PcContext* context, const unsigned char owner[32], uint64_t generation,
    void* poker_game, int lane, uint64_t* rng, const void* profiles,
    uint64_t request_id, int32_t* action);
int pc_end(PcContext* context, const unsigned char owner[32], uint64_t generation,
    int reason);
int pc_protocol_error(PcContext* context, const char* message);
int pc_audit(const PcContext* context, PcAudit* audit);
int pc_certificate(const PcContext* context, RcfrCertificate* certificate);
int pc_node_count(const PcContext* context);
size_t pc_allocated_bytes(const PcContext* context);
int pc_node_info(const PcContext* context, int node, RcfrNodeInfo* info);
int pc_average(const PcContext* context, int node, double* output, size_t capacity);
int pc_public_from_game(const void* poker_game, int lane, PcPublic* output);
/* Pure checked public transition. after supplies newly revealed board cards.
 * Callers must not pass native settled terminal stacks as after. */
int pc_public_transition(const PcPublic* before, int32_t action,
    const PcPublic* after);
int pc_public_next(const PcPublic* before, int32_t action,
    const int64_t visible_board[5], int board_count, PcPublic* after);
const char* pc_last_error(const PcContext* context);
#endif
