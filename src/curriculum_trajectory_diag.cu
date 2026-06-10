// Diagnostics for the reward-only trajectory curriculum.

struct CurriculumDiagnostics {
    int enabled;
};

static inline int curriculum_diag_init_state_buffer(StateBuffer* buf, int total_agents) {
    (void)total_agents;
    if (buf == NULL || !buf->diagnostics_enabled) {
        if (buf != NULL) {
            buf->diag = NULL;
        }
        return 1;
    }
    buf->diag = (CurriculumDiagnostics*)calloc(1, sizeof(CurriculumDiagnostics));
    if (buf->diag == NULL) {
        fprintf(stderr, "Failed to allocate curriculum diagnostics\n");
        return 0;
    }
    buf->diag->enabled = 1;
    return 1;
}

static inline void curriculum_diag_close_state_buffer(StateBuffer* buf) {
    if (buf == NULL) {
        return;
    }
    free(buf->diag);
    buf->diag = NULL;
}

static inline void curriculum_best_summary(StateBuffer* buf,
        double* best_return, double* best_length, long* best_step) {
    double max_ret = -3.402823466e+38;
    int max_len = 0;
    long max_step = 0;
    int found = 0;
    for (int i = 0; i < buf->num_start_states; i++) {
        if (!buf->best_valid[i]) {
            continue;
        }
        double ret = (double)buf->best_return[i];
        int len = buf->best_len[i];
        if (!found || ret > max_ret
                || (ret == max_ret && len < max_len)) {
            max_ret = ret;
            max_len = len;
            max_step = buf->best_agent_step[i];
            found = 1;
        }
    }
    *best_return = found ? max_ret : 0.0;
    *best_length = found ? (double)max_len : 0.0;
    *best_step = found ? max_step : 0;
}

void curriculum_log_diagnostics(PuffeRL* pufferl, Dict* out) {
    StateBuffer* buf = &pufferl->state_buf;
    double best_return = 0.0;
    double best_length = 0.0;
    long best_step = 0;
    curriculum_best_summary(buf, &best_return, &best_length, &best_step);

    long current_step = pufferl->global_step;
    if (pufferl->hypers.world_size > 1) {
        current_step *= (long)pufferl->hypers.world_size;
        best_step *= (long)pufferl->hypers.world_size;
    }
    double stale_m = current_step >= best_step
        ? (double)(current_step - best_step) / 1000000.0 : 0.0;

    dict_set(out, "best_return", best_return);
    dict_set(out, "best_length", best_length);
    dict_set(out, "best_stale_m", stale_m);
}

#ifdef BOXOBAN_LEVEL_LOGS
static long boxoban_t80_first_step[BOXOBAN_LEVEL_LOGS];
static long boxoban_t80_done_step[BOXOBAN_LEVEL_LOGS];
static long boxoban_t80_last_epoch = -1;
static int boxoban_t80_initialized = 0;
static double boxoban_t80_prev_episodes = 0.0;
static double boxoban_t80_prev_successes[BOXOBAN_LEVEL_LOGS];

static inline const char* boxoban_t80_key(int level) {
    static int initialized = 0;
    static char keys[BOXOBAN_LEVEL_LOGS][16];
    if (!initialized) {
        for (int i = 0; i < BOXOBAN_LEVEL_LOGS; i++) {
            snprintf(keys[i], sizeof(keys[i]), "l%d_t80", i + 1);
        }
        initialized = 1;
    }
    return keys[level];
}

static inline void boxoban_t80_reset_arrays(void) {
    for (int i = 0; i < BOXOBAN_LEVEL_LOGS; i++) {
        boxoban_t80_first_step[i] = -1;
        boxoban_t80_done_step[i] = -1;
        boxoban_t80_prev_successes[i] = 0.0;
    }
    boxoban_t80_prev_episodes = 0.0;
}

static inline void boxoban_t80_ensure_initialized(void) {
    if (!boxoban_t80_initialized) {
        boxoban_t80_reset_arrays();
        boxoban_t80_initialized = 1;
    }
}

static inline void boxoban_t80_reset(void) {
    boxoban_t80_reset_arrays();
    boxoban_t80_initialized = 1;
}

static inline long boxoban_agent_steps(PuffeRL& pufferl) {
    return pufferl.global_step * (long)pufferl.hypers.world_size;
}

static inline int boxoban_frontier_level(void) {
    boxoban_t80_ensure_initialized();
    for (int level = 0; level < BOXOBAN_LEVEL_LOGS; level++) {
        if (boxoban_t80_done_step[level] < 0) {
            return level;
        }
    }
    return BOXOBAN_LEVEL_LOGS;
}

static inline void boxoban_update_t80(PuffeRL& pufferl) {
    boxoban_t80_ensure_initialized();
    if (boxoban_t80_last_epoch < 0
            || (long)pufferl.epoch < boxoban_t80_last_epoch) {
        boxoban_t80_reset();
    }
    boxoban_t80_last_epoch = (long)pufferl.epoch;
    long step = boxoban_agent_steps(pufferl);

    StaticVec* vec = pufferl.vec;
    int end_env = pufferl.curriculum_enabled
        ? pufferl.state_buf.num_vanilla_envs
        : vec->size;
    if (end_env > vec->size) {
        end_env = vec->size;
    }
    if (end_env <= 0) {
        return;
    }

    double episodes = 0.0;
    double successes[BOXOBAN_LEVEL_LOGS] = {};
    for (int i = 0; i < end_env; i++) {
        Env* env = &vec->envs[i];
        if (env->log.n == 0.0f) {
            continue;
        }
        episodes += (double)env->log.n;
        for (int level = 0; level < BOXOBAN_LEVEL_LOGS; level++) {
            successes[level] += (double)env->log.level_solved[level];
        }
    }
    if (episodes == 0.0) {
        return;
    }
    if (episodes < boxoban_t80_prev_episodes) {
        boxoban_t80_prev_episodes = 0.0;
        for (int level = 0; level < BOXOBAN_LEVEL_LOGS; level++) {
            boxoban_t80_prev_successes[level] = 0.0;
        }
    }

    double delta_episodes = episodes - boxoban_t80_prev_episodes;
    if (delta_episodes <= 0.0) {
        return;
    }
    for (int level = 0; level < BOXOBAN_LEVEL_LOGS; level++) {
        double delta_successes =
            successes[level] - boxoban_t80_prev_successes[level];
        if (delta_successes < 0.0) {
            delta_successes = successes[level];
        }
        if (delta_successes > 0.0 && boxoban_t80_first_step[level] < 0) {
            boxoban_t80_first_step[level] = step;
        }
        if (boxoban_t80_first_step[level] >= 0
                && boxoban_t80_done_step[level] < 0) {
            double rate = delta_successes / delta_episodes;
            if (rate >= 0.80) {
                boxoban_t80_done_step[level] = step;
            }
        }
        boxoban_t80_prev_successes[level] = successes[level];
    }
    boxoban_t80_prev_episodes = episodes;
}

static inline void boxoban_log_t80(Dict* out, PuffeRL& pufferl) {
    long step = boxoban_agent_steps(pufferl);
    for (int level = 0; level < BOXOBAN_LEVEL_LOGS; level++) {
        double value = -1.0;
        long first = boxoban_t80_first_step[level];
        if (first >= 0) {
            long end = boxoban_t80_done_step[level] >= 0
                ? boxoban_t80_done_step[level]
                : step;
            if (end < first) {
                end = first;
            }
            value = (double)(end - first) / 1000000.0;
        }
        dict_set(out, boxoban_t80_key(level), value);
    }
    dict_set(out, "fr", (double)(boxoban_frontier_level() + 1));
}
#endif
