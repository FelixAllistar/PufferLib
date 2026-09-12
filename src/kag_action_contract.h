#pragma once

/* Task mode owns its decoder. A leftover structured-executor flag must not
 * prevent task training or label task checkpoints as executor-1 models.
 * Normalize each side independently: a mode-2 opponent still needs v1. */
static inline void kag_resolve_executor_versions(int mode, int frozen_mode,
        int* executor, int* frozen_executor) {
    if (mode == 3 && *executor == 1) *executor = 0;
    int opponent_mode = frozen_mode < 0 ? mode : frozen_mode;
    if (opponent_mode == 3 && (*frozen_executor == 1
            || (*frozen_executor == -1 && *executor == 1))) *frozen_executor = 0;
}
