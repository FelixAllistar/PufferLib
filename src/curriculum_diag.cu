// Diagnostics hooks for the compact segment curriculum implementation.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#endif

#ifndef PUFFER_CURRICULUM_DEBUG_FIELDS
#define PUFFER_CURRICULUM_DEBUG_FIELDS 6
#endif

#ifndef PUFFER_CURRICULUM_TOP_DIAG
#define PUFFER_CURRICULUM_TOP_DIAG 4
#endif

#ifndef PUFFER_CURRICULUM_TRACE_PATH
#define PUFFER_CURRICULUM_TRACE_PATH "logs/boxoban_cl_segments.jsonl"
#endif

struct CurriculumDiagnostics {
    PrecisionTensor checkpoint_debug;
    precision_t* checkpoint_debug_host;
    int checkpoint_debug_rows;
    int* cl_start_bucket;
    int* cl_start_head;
    long long window_attempts;
    long long window_successes;
    long long start_attempts;
    long long start_successes;
    long long cl_attempts[PUFFER_CURRICULUM_CL_BINS];
    long long cl_successes[PUFFER_CURRICULUM_CL_BINS];
    long long cl_terminals;
    long long cl_solve_terminals;
    long long cl_tail_replacements;
    long long fresh_attempts;
    long long fresh_successes;
    long long fresh_solve_candidates;
    long long fresh_segments_stored;
    long long segment_ejections;
    int candidate_count;
    int candidate_selected_env;
    int candidate_stored;
    float candidate_raw_score;
    float candidate_raw_priority;
    float candidate_adv_score;
    float candidate_adv_priority;
    float candidate_selected_score;
    float candidate_selected_priority;
    int trace_dump_count;
#ifdef BOXOBAN_LEVEL_LOGS
    long long fresh_level_solved[BOXOBAN_LEVEL_LOGS];
#endif
};

static inline void curriculum_diag_make_trace_dir(void) {
#ifndef _WIN32
    mkdir("logs", 0755);
#endif
}

static inline int curriculum_diag_init_state_buffer(StateBuffer* buf, int total_agents) {
    (void)total_agents;
    if (buf == NULL || !buf->diagnostics_enabled) {
        if (buf != NULL) {
            buf->diag = NULL;
        }
        return 1;
    }
    CurriculumDiagnostics* diag =
        (CurriculumDiagnostics*)calloc(1, sizeof(CurriculumDiagnostics));
    if (diag == NULL) {
        return 0;
    }

    int debug_rows = buf->score_capacity * PUFFER_CURRICULUM_DEBUG_FIELDS;
    diag->checkpoint_debug_rows = debug_rows;
    diag->checkpoint_debug = {.shape = {debug_rows}};
    if (debug_rows > 0) {
        if (cudaMalloc(&diag->checkpoint_debug.data,
                (size_t)debug_rows * sizeof(precision_t)) != cudaSuccess) {
            free(diag);
            return 0;
        }
        diag->checkpoint_debug_host =
            (precision_t*)malloc((size_t)debug_rows * sizeof(precision_t));
        if (diag->checkpoint_debug_host == NULL) {
            cudaFree(diag->checkpoint_debug.data);
            free(diag);
            return 0;
        }
        cudaMemset(diag->checkpoint_debug.data, 0,
            (size_t)debug_rows * sizeof(precision_t));
        memset(diag->checkpoint_debug_host, 0,
            (size_t)debug_rows * sizeof(precision_t));
    }

    diag->cl_start_bucket = (int*)malloc((size_t)buf->num_envs * sizeof(int));
    diag->cl_start_head = (int*)malloc((size_t)buf->num_envs * sizeof(int));
    if (diag->cl_start_bucket == NULL || diag->cl_start_head == NULL) {
        cudaFree(diag->checkpoint_debug.data);
        free(diag->checkpoint_debug_host);
        free(diag->cl_start_bucket);
        free(diag->cl_start_head);
        free(diag);
        return 0;
    }
    for (int i = 0; i < buf->num_envs; i++) {
        diag->cl_start_bucket[i] = -1;
        diag->cl_start_head[i] = 0;
    }

    curriculum_diag_make_trace_dir();
    FILE* trace = fopen(PUFFER_CURRICULUM_TRACE_PATH, "w");
    if (trace != NULL) {
        fclose(trace);
    }

    buf->diag = diag;
    return 1;
}

static inline void curriculum_diag_close_state_buffer(StateBuffer* buf) {
    if (buf == NULL || buf->diag == NULL) {
        return;
    }
    CurriculumDiagnostics* diag = buf->diag;
    cudaFree(diag->checkpoint_debug.data);
    free(diag->checkpoint_debug_host);
    free(diag->cl_start_bucket);
    free(diag->cl_start_head);
    free(diag);
    buf->diag = NULL;
}

static inline precision_t* curriculum_diag_debug_device(StateBuffer* buf) {
    CurriculumDiagnostics* diag = buf != NULL ? buf->diag : NULL;
    return diag != NULL ? diag->checkpoint_debug.data : NULL;
}

static inline void curriculum_diag_copy_checkpoint_debug(
        StateBuffer* buf, int rows, cudaStream_t stream) {
    CurriculumDiagnostics* diag = buf != NULL ? buf->diag : NULL;
    if (diag == NULL || diag->checkpoint_debug.data == NULL
            || diag->checkpoint_debug_host == NULL || rows <= 0) {
        return;
    }
    int values = rows * PUFFER_CURRICULUM_DEBUG_FIELDS;
    if (values > diag->checkpoint_debug_rows) {
        values = diag->checkpoint_debug_rows;
    }
    cudaMemcpyAsync(diag->checkpoint_debug_host, diag->checkpoint_debug.data,
        (size_t)values * sizeof(precision_t), cudaMemcpyDeviceToHost, stream);
}

static inline void curriculum_diag_rollout_begin(PuffeRL* pufferl) {
    StateBuffer* buf = &pufferl->state_buf;
    CurriculumDiagnostics* diag = buf->diag;
    if (diag == NULL) {
        return;
    }
    for (int i = 0; i < buf->num_envs; i++) {
        diag->cl_start_bucket[i] = -1;
        diag->cl_start_head[i] = 0;
    }
}

static inline void curriculum_diag_source_snapshot(
        PuffeRL* pufferl, int source, const PufferState* state) {
    (void)pufferl;
    (void)source;
    (void)state;
}

static inline void curriculum_diag_fresh_terminal(StateBuffer* buf, const Env* env) {
    CurriculumDiagnostics* diag = buf != NULL ? buf->diag : NULL;
    if (diag == NULL) {
        return;
    }
    diag->fresh_attempts++;
#ifdef BOXOBAN_LEVEL_LOGS
    int solved = env != NULL ? env->state.episode_maps_solved : 0;
    if (solved > 0) {
        diag->fresh_successes++;
        diag->fresh_solve_candidates++;
    }
    for (int level = 0; level < BOXOBAN_LEVEL_LOGS; level++) {
        if (solved >= level + 1) {
            diag->fresh_level_solved[level]++;
        }
    }
#else
    (void)env;
#endif
}

static inline void curriculum_diag_cl_start(
        StateBuffer* buf, int env_idx, int sampled_slot, int remaining,
        int prefer_head) {
    (void)sampled_slot;
    CurriculumDiagnostics* diag = buf != NULL ? buf->diag : NULL;
    if (diag == NULL || env_idx < 0 || env_idx >= buf->num_envs) {
        return;
    }
    int bucket = curriculum_cl_bucket(remaining);
    diag->cl_start_bucket[env_idx] = bucket;
    diag->cl_start_head[env_idx] = prefer_head ? 1 : 0;
    diag->window_attempts++;
    diag->cl_attempts[bucket]++;
    if (prefer_head) {
        diag->start_attempts++;
    }
}

static inline void curriculum_diag_cl_terminal(
        StateBuffer* buf, int env_idx, float reward) {
    CurriculumDiagnostics* diag = buf != NULL ? buf->diag : NULL;
    if (diag == NULL || env_idx < 0 || env_idx >= buf->num_envs) {
        return;
    }
    int solved = reward > 0.5f;
    int bucket = diag->cl_start_bucket[env_idx];
    diag->cl_terminals++;
    if (solved) {
        diag->cl_solve_terminals++;
        diag->window_successes++;
    }
    if (bucket >= 0 && bucket < PUFFER_CURRICULUM_CL_BINS && solved) {
        diag->cl_successes[bucket]++;
    }
    if (diag->cl_start_head[env_idx] && solved) {
        diag->start_successes++;
    }
    diag->cl_start_bucket[env_idx] = -1;
    diag->cl_start_head[env_idx] = 0;
}

static inline void curriculum_diag_cl_tail_replaced(StateBuffer* buf) {
    CurriculumDiagnostics* diag = buf != NULL ? buf->diag : NULL;
    if (diag != NULL) {
        diag->cl_tail_replacements++;
    }
}

static inline void curriculum_diag_trace_segment(
        StateBuffer* buf, int seg, long agent_step) {
#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
    CurriculumDiagnostics* diag = buf != NULL ? buf->diag : NULL;
    if (diag == NULL || seg < 0 || seg >= buf->segment_count) {
        return;
    }
    FILE* trace = fopen(PUFFER_CURRICULUM_TRACE_PATH, "a");
    if (trace == NULL) {
        return;
    }
    int len = buf->segment_len[seg];
    int base = seg * buf->segment_capacity;
    if (len < 0) {
        len = 0;
    }
    if (base + len > buf->size) {
        len = buf->size - base;
    }
    if (len < 0) {
        len = 0;
    }
    fprintf(trace,
        "{\"event\":%d,\"agent_steps_m\":%.6f,\"score_bucket\":%d,"
        "\"frontier\":{\"score\":%.9g,\"solve_rate\":0,\"steps\":[",
        diag->trace_dump_count, (double)agent_step / 1000000.0,
        (int)floorf(buf->segment_score[seg]), buf->segment_score[seg]);
    for (int i = 0; i < len; i++) {
        PufferState* state = &buf->states[base + i];
        float reward = i == 0 ? 0.0f
            : state->episode_return - buf->states[base + i - 1].episode_return;
        if (isnan(reward) || isinf(reward)) {
            reward = 0.0f;
        }
        fprintf(trace,
            "%s{\"i\":%d,\"adv\":%.9g,\"return\":%.9g,"
            "\"reward\":%.9g,\"episode_return\":%.9g",
            i == 0 ? "" : ",", i, buf->state_priority[base + i],
            buf->state_return[base + i], reward, state->episode_return);
#ifdef BOXOBAN_LEVEL_LOGS
        fprintf(trace, ",\"maps_solved\":%d,\"sequence_pos\":%d,"
            "\"puzzle_tick\":%d", state->episode_maps_solved,
            state->sequence_pos, state->puzzle_tick);
#endif
        fprintf(trace, "}");
    }
    fprintf(trace,
        "]},\"offender\":{\"score\":0,\"solve_rate\":0,\"steps\":[]}}\n");
    fclose(trace);
    diag->trace_dump_count++;
#else
    (void)buf;
    (void)seg;
    (void)agent_step;
#endif
}

static inline void curriculum_diag_segment_stored(
        StateBuffer* buf, int seg, long agent_step) {
    CurriculumDiagnostics* diag = buf != NULL ? buf->diag : NULL;
    if (diag == NULL) {
        return;
    }
    diag->fresh_segments_stored++;
    curriculum_diag_trace_segment(buf, seg, agent_step);
}

static inline void curriculum_diag_segment_ejected(StateBuffer* buf, int seg) {
    (void)seg;
    CurriculumDiagnostics* diag = buf != NULL ? buf->diag : NULL;
    if (diag != NULL) {
        diag->segment_ejections++;
    }
}

static inline void curriculum_diag_candidate_selected(StateBuffer* buf,
        int candidate_count, float raw_best_score, float raw_best_priority,
        float adv_best_score, float adv_best_priority, int selected_env,
        int stored) {
    CurriculumDiagnostics* diag = buf != NULL ? buf->diag : NULL;
    if (diag == NULL) {
        return;
    }
    if (candidate_count > 0 || selected_env < 0) {
        diag->candidate_count = candidate_count;
        diag->candidate_raw_score = raw_best_score;
        diag->candidate_raw_priority = raw_best_priority;
        diag->candidate_adv_score = adv_best_score;
        diag->candidate_adv_priority = adv_best_priority;
        diag->candidate_selected_env = selected_env;
        diag->candidate_selected_score =
            selected_env >= 0 ? buf->pending_score : 0.0f;
        diag->candidate_selected_priority =
            selected_env >= 0 ? buf->pending_priority : 0.0f;
        diag->candidate_stored = 0;
    }
    if (stored) {
        diag->candidate_stored = 1;
    }
}

static inline const char* curriculum_diag_top_key(
        const char* prefix, int idx, int field) {
    static char keys[2][PUFFER_CURRICULUM_TOP_DIAG][5][16];
    int p = prefix[0] == 'r' ? 0 : 1;
    const char suffixes[5] = {'r', 'a', 'n', 'm', 'g'};
    if (idx < 0 || idx >= PUFFER_CURRICULUM_TOP_DIAG
            || field < 0 || field >= 5) {
        return "";
    }
    if (keys[p][idx][field][0] == '\0') {
        snprintf(keys[p][idx][field], sizeof(keys[p][idx][field]),
            "%s%d%c", prefix, idx, suffixes[field]);
    }
    return keys[p][idx][field];
}

static inline int curriculum_diag_next_top_segment(
        StateBuffer* buf, int* used) {
    int best = -1;
    for (int seg = 0; seg < buf->segment_count; seg++) {
        if (used[seg] || buf->segment_len[seg] <= 0) {
            continue;
        }
        if (best < 0 || curriculum_segment_better(buf,
                buf->segment_score[seg], buf->segment_priority[seg],
                buf->segment_len[seg],
                buf->segment_score[best], buf->segment_priority[best],
                buf->segment_len[best])) {
            best = seg;
        }
    }
    return best;
}

void curriculum_log_diagnostics(PuffeRL* pufferl, Dict* out) {
    if (pufferl == NULL || out == NULL || !pufferl->curriculum_enabled) {
        return;
    }
    StateBuffer* buf = &pufferl->state_buf;
    CurriculumDiagnostics* diag = buf->diag;
    if (diag == NULL) {
        return;
    }

    dict_set(out, "state_buffer_count", (double)buf->size);
    dict_set(out, "seg", (double)buf->saved_score);
    dict_set(out, "seg_raw", (double)buf->saved_return);
    dict_set(out, "trace_n", (double)diag->trace_dump_count);
    dict_set(out, "seg_n", (double)buf->size);
    dict_set(out, "seg_pick", (double)buf->saved_pick_t);
    dict_set(out, "seg_t0", (double)buf->saved_first_t);
    dict_set(out, "seg_t1", (double)buf->saved_last_t);
    dict_set(out, "seg_full", (double)buf->saved_full);
    dict_set(out, "buf_min", (double)buf->min_priority);
    dict_set(out, "pend", (double)buf->pending_score);
    dict_set(out, "pend_raw", (double)buf->pending_return);
    dict_set(out, "pend_n", (double)buf->pending_count);
    dict_set(out, "ca_n", (double)diag->candidate_count);
    dict_set(out, "ca_raw", (double)diag->candidate_raw_score);
    dict_set(out, "ca_rawa", (double)diag->candidate_raw_priority);
    dict_set(out, "ca_adv", (double)diag->candidate_adv_score);
    dict_set(out, "ca_advr", (double)diag->candidate_adv_priority);
    dict_set(out, "ca_sel", (double)diag->candidate_selected_score);
    dict_set(out, "ca_sela", (double)diag->candidate_selected_priority);
    dict_set(out, "ca_in", (double)diag->candidate_stored);
    dict_set(out, "ca_min", (double)buf->min_priority);
    dict_set(out, "ca_minr", (double)buf->min_priority);
    dict_set(out, "ca_rawsel", diag->candidate_selected_env >= 0 ? 1.0 : 0.0);
    dict_set(out, "ca_rawin", (double)diag->candidate_stored);
    dict_set(out, "fs_n", (double)diag->fresh_solve_candidates);
    dict_set(out, "fs_in", (double)diag->fresh_segments_stored);
    dict_set(out, "fs_ep", (double)diag->fresh_attempts);
    dict_set(out, "nf_n", (double)diag->fresh_attempts);
    dict_set(out, "nf_sr", diag->fresh_attempts > 0
        ? (double)diag->fresh_successes / (double)diag->fresh_attempts : 0.0);
    dict_set(out, "cl_t", (double)diag->cl_terminals);
    dict_set(out, "cl_s", (double)diag->cl_solve_terminals);
    dict_set(out, "cl_rep", (double)diag->cl_tail_replacements);
    dict_set(out, "cl_rep_sr", diag->cl_terminals > 0
        ? (double)diag->cl_tail_replacements / (double)diag->cl_terminals : 0.0);

    long long attempts = 0;
    long long successes = 0;
    for (int i = 0; i < PUFFER_CURRICULUM_CL_BINS; i++) {
        attempts += diag->cl_attempts[i];
        successes += diag->cl_successes[i];
    }
    dict_set(out, "cl_n", (double)attempts);
    dict_set(out, "cl_sr", attempts > 0
        ? (double)successes / (double)attempts : 0.0);
    dict_set(out, "cl0_n", (double)diag->start_attempts);
    dict_set(out, "cl0_sr", diag->start_attempts > 0
        ? (double)diag->start_successes / (double)diag->start_attempts : 0.0);
    dict_set(out, "cl4_n", (double)diag->window_attempts);
    dict_set(out, "cl4_sr", diag->window_attempts > 0
        ? (double)diag->window_successes / (double)diag->window_attempts : 0.0);

    double priority_sum = 0.0;
    double priority_max = 0.0;
    for (int slot = 0; slot < buf->size; slot++) {
        double p = (double)clean_state_priority(buf->state_priority[slot]);
        priority_sum += p;
        if (p > priority_max) {
            priority_max = p;
        }
    }
    dict_set(out, "buf_n", (double)buf->size);
    dict_set(out, "buf_avg", buf->size > 0
        ? priority_sum / (double)buf->size : 0.0);
    dict_set(out, "buf_max", priority_max);
    dict_set(out, "buf_hi", priority_max);

    if (buf->segment_count > 0) {
        int* used = (int*)calloc((size_t)buf->segment_count, sizeof(int));
        if (used != NULL) {
            for (int i = 0; i < PUFFER_CURRICULUM_TOP_DIAG; i++) {
                int seg = curriculum_diag_next_top_segment(buf, used);
                if (seg < 0) {
                    break;
                }
                used[seg] = 1;
                dict_set(out, curriculum_diag_top_key("rt", i, 0),
                    (double)buf->segment_score[seg]);
                dict_set(out, curriculum_diag_top_key("rt", i, 1),
                    (double)buf->segment_priority[seg]);
                dict_set(out, curriculum_diag_top_key("rt", i, 2),
                    (double)buf->segment_len[seg]);
                dict_set(out, curriculum_diag_top_key("rt", i, 3),
                    (double)buf->segment_sample_count[seg]);
                dict_set(out, curriculum_diag_top_key("rt", i, 4),
                    buf->segment_agent_step[seg] >= 0
                        ? (double)buf->segment_agent_step[seg] / 1000000.0 : -1.0);
            }
            free(used);
        }
    }
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
        ? pufferl.state_buf.num_fresh_envs
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
