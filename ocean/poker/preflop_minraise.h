#pragma once

// Frozen offline call-versus-fold rollouts against range opening to 2 BB.
// Only currently folded hands passed the fixed mean > 3.5 SE
// gate across all 75 classes. This table is an opponent-specific
// candidate, not a solved or human-calibrated preflop strategy.
static const uint64_t poker_hu_minraise_calls[3] = {
    UINT64_C(0x22e20b9001800800), UINT64_C(0x00600f00e00207b0), UINT64_C(0x0000000000000000)
};
