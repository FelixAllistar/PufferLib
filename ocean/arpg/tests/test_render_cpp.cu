// Native CPU training includes the full environment header under nvcc, unlike
// the headless gameplay test. Catch C-only allocations/types in the renderer.
#include "../arpg.h"
static_assert(AR_OBS_SIZE==443,"Reach presentation must not change policy shape");
static_assert(NUM_ATNS==13,"Reach presentation must not change action shape");
