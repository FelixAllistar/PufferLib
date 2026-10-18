#pragma once
#include <assert.h>
#include <math.h>

// Frozen one-parameter marginal range fit from the designated old TRAIN split.
// p_alpha(h|s) = W(h,s)^alpha / sum_g W(g,s)^alpha.
// The existing value tree normalizes the initial weights when reporting values.
// Model SHA256:
// c195e2766bec51174e581c8ace18d77d8e564d972f7851e2bfa9b70f6c00e5bd
// Fixed fit/selection/validation plan SHA256:
// a781a93180793a84161b160612a1ab3f7d0a25479c07d1c4a42a4b0eac8da630
// Runtime scope and the single root application are enforced by river_value.h.
// Reference equities and descendant response likelihoods are not transformed.
static const double poker_river_range_alpha = 0.42089278763026228;

static double poker_river_range_weight(double weight) {
    assert(weight > 0);
    return pow(weight, poker_river_range_alpha);
}
