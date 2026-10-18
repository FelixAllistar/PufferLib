#pragma once
#include <assert.h>
#include <math.h>
#include <stdint.h>

// Frozen 12-coefficient river-response fit; no private-card/outcome lookup.
// Training model SHA256:
// fd101bf0a24aff070a276ed953836dbaa8482817d4a107ae4460580019a7c93f
// Fixed feature/fit/validation plan SHA256:
// 95afafda91bfb236fbbed505025d6e9d798f65be1e31078409d8e4fff83d00f4
// These predict action categories only, not bet sizes or game value. Runtime
// scope is enforced by river_value.h: HU, exact q, initial200BB, no profiles,
// and only the first predicted opponent response in the current river solve.
static const double poker_response_fold_coefficients[3] = {
    -1.6141440246715977, -2.7425769588998268, -0.04645176862892594
};
static const double poker_response_raise_coefficients[5] = {
    -2.380835799891652, 1.7721446723753196, 1.9510517963886196,
    -0.0303803742043579, 1.2766835030809711
};
static const double poker_response_bet_coefficients[4] = {
    -1.454453005284149, 3.0648024739080766, 4.261231025713257,
    1.122223151467852
};

typedef struct PokerRiverResponseContext {
    double price, stack_ratio;
    int facing, can_raise;
} PokerRiverResponseContext;

static PokerRiverResponseContext poker_river_response_context(uint32_t call,
    uint32_t eligible, uint32_t pot, uint32_t actor_stack, uint32_t other_stack,
    int can_raise) {
    assert(eligible && pot);
    uint32_t effective = actor_stack < other_stack ? actor_stack : other_stack;
    double s = log1p(effective / (double)pot) / log(101.0);
    PokerRiverResponseContext out = {.price=4 * (call / (double)eligible) - 1,
        .stack_ratio=s < 0 ? 0 : s > 1 ? 1 : s,
        .facing=call != 0,.can_raise=can_raise != 0};
    return out;
}

static double poker_river_response_logistic(double value) {
    double p = 1 / (1 + exp(-value));
    return p < 1e-6 ? 1e-6 : p > 1 - 1e-6 ? 1 - 1e-6 : p;
}

// out is [fold, passive, aggressive]. A raise probability is conditional on
// continuing when facing a bet; multiply by continuation mass exactly once.
static void poker_river_response_predict(const PokerRiverResponseContext* ctx,
    double q, double* out) {
    double z = 2*q - 1, z2 = z*z, fold = 0, aggressive = 0;
    if (ctx->facing) {
        const double* c = poker_response_fold_coefficients;
        fold = poker_river_response_logistic(c[0] + c[1]*z + c[2]*ctx->price);
        if (ctx->can_raise) {
            c = poker_response_raise_coefficients;
            aggressive = (1-fold) * poker_river_response_logistic(c[0] + c[1]*z
                + c[2]*z2 + c[3]*ctx->price + c[4]*ctx->stack_ratio);
        }
    } else if (ctx->can_raise) {
        const double* c = poker_response_bet_coefficients;
        aggressive = poker_river_response_logistic(c[0] + c[1]*z
            + c[2]*z2 + c[3]*ctx->stack_ratio);
    }
    out[0] = fold; out[1] = 1-fold-aggressive; out[2] = aggressive;
}
