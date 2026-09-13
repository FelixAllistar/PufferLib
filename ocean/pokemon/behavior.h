#pragma once
#include <math.h>
#define PK_BEHAVIOR_DIM 2
static const char* const pk_behavior_names[PK_BEHAVIOR_DIM] = {"sleep", "paralysis"};
// Genuine event objectives: no terminal cancellation and no discount-potential
// term. Pay capped progress when an event happens, including a terminal step.
static inline float pk_behavior_feature(float raw, int k) {
    return fminf(fmaxf(raw, 0), k == 0 ? 1.0f : 3.0f) / (k == 0 ? 1.0f : 3.0f);
}
static inline float pk_behavior_progress(float before, float after, int k) {
    return fmaxf(0, pk_behavior_feature(after,k)-pk_behavior_feature(before,k));
}
static inline float pk_behavior_extra(const float delta[2][PK_BEHAVIOR_DIM], const double weights[PK_BEHAVIOR_DIM]) {
    double extra=0;
    for (int k=0;k<PK_BEHAVIOR_DIM;k++) extra+=weights[k]*(delta[0][k]-delta[1][k]);
    return (float)extra;
}
