/* Separate prospective build entry. Existing build.sh/native.c stay untouched.
 * No model implementation or private header is present in this package.
 */
#ifndef POKER_CLONE_BRIDGE
#define POKER_CLONE_BRIDGE 0
#endif
#if defined(POKER_RIVER_CFR) && POKER_RIVER_CFR
#error "The prospective clone bridge has no combined river-CFR policy"
#endif
#include "../reference_a4b/native.c"
#if POKER_CLONE_BRIDGE && POKER_PLAYERS == 2
#include "bridge_native_impl.h"
#endif
