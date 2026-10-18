#pragma once

// The generic solver accepts ranks as input. This separate ABI shim reuses the
// native evaluator for synthetic/reference comparisons, with no policy state.
uint32_t rcfr_rank7(const uint8_t* cards) {
    return poker_rank(cards, 7);
}
