#ifndef RIVER_CFR_REFERENCE_H
#define RIVER_CFR_REFERENCE_H
#include <stddef.h>
#include <stdint.h>

/* Standalone public-range game. No actual/private hero hand is a solve input. */
typedef struct RcfrHand { uint8_t c0, c1; uint32_t rank; } RcfrHand;
typedef struct RcfrPublic {
    int64_t pot, total[2], bet[2], stack[2], high, increment, reopen[2];
    int pending, actor;
    double chips_per_bb;
} RcfrPublic;

/* kind: 0 decision, 1 showdown, 2 fold; payoff indexed by rank0<=>rank1. */
typedef struct RcfrNodeInfo {
    RcfrPublic state;
    int kind, folder, action_count, child[3];
    int64_t action[3];
    double payoff0[3];
} RcfrNodeInfo;
typedef struct RcfrCertificate {
    double value_chips, upper_chips, lower_chips, nashconv_chips, gap_over_two_chips;
    double value_bb, upper_bb, lower_bb, nashconv_bb, gap_over_two_bb;
    double chance_normalizer;
    uint64_t iterations;
} RcfrCertificate;
typedef struct RcfrGame RcfrGame;

RcfrGame* rcfr_create(const RcfrHand* hands, int n, const double* prior0,
    const double* prior1, const RcfrPublic* root, char* error, size_t error_size);
void rcfr_free(RcfrGame* game);
int rcfr_iterate(RcfrGame* game, uint64_t iterations);
int rcfr_certificate(RcfrGame* game, RcfrCertificate* out);
int rcfr_hand_count(const RcfrGame* game);
int rcfr_node_count(const RcfrGame* game);
int rcfr_max_depth(const RcfrGame* game);
size_t rcfr_allocated_bytes(const RcfrGame* game);
int rcfr_node_info(const RcfrGame* game, int node, RcfrNodeInfo* out);
/* Flat action-major array of action_count*N entries; row h is [a*N+h]. */
int rcfr_average(const RcfrGame* game, int node, double* out);
int rcfr_regrets(const RcfrGame* game, int node, double* out);
/* Test-only dense traversal switch, default zero; never selects a policy. */
int rcfr_test_dense_backend(RcfrGame* game, int enabled);
/* Read-only terminal oracle surface, values for every player holding. */
int rcfr_terminal_values(RcfrGame* game, int terminal_node, int player,
    const double* opponent_reach, double* out, int dense);
uint32_t rcfr_rank7(const uint8_t* cards);
#endif
