// Curriculum state-set and prioritized replay implementation.
// Included from pufferlib.cu in two phases so the buffer types are
// visible inside PuffeRL while functions that dereference PuffeRL see
// the complete struct definition.

#include <cub/device/device_scan.cuh>
#include <stdio.h>

#ifndef PUFFER_CURRICULUM_DIAG_BINS
#define PUFFER_CURRICULUM_DIAG_BINS 16
#endif

#ifndef PUFFER_CURRICULUM_CL_BINS
#define PUFFER_CURRICULUM_CL_BINS 4
#endif

#ifndef PUFFER_CURRICULUM_SOLVE_RATE_BINS
#define PUFFER_CURRICULUM_SOLVE_RATE_BINS 5
#endif

#ifndef PUFFER_CURRICULUM_TOP_DIAG
#define PUFFER_CURRICULUM_TOP_DIAG 4
#endif

#ifndef PUFFER_CURRICULUM_PRIORITY_TOPK
#define PUFFER_CURRICULUM_PRIORITY_TOPK 16
#endif

#ifndef PUFFER_CURRICULUM_DEBUG_FIELDS
#define PUFFER_CURRICULUM_DEBUG_FIELDS 6
#endif

#ifndef PUFFER_CURRICULUM_TRACE_MAX
#define PUFFER_CURRICULUM_TRACE_MAX 256
#endif

#ifndef PUFFER_CURRICULUM_TRACE_PER_SCORE
#define PUFFER_CURRICULUM_TRACE_PER_SCORE 16
#endif

#ifndef PUFFER_CURRICULUM_TRACE_MIN_STEPS
#define PUFFER_CURRICULUM_TRACE_MIN_STEPS 4000000LL
#endif

#ifndef PUFFER_CURRICULUM_TRACE_PATH
#define PUFFER_CURRICULUM_TRACE_PATH "logs/boxoban_cl_segments.jsonl"
#endif

#ifdef PUFFER_CURRICULUM_TYPES

// Prioritized replay over single-epoch data. These kernels are
// the least cleaned because we will likely have a better method in 5.0
struct PrioBuffers {
    FloatTensor prio_weights, cdf;
    ByteTensor cdf_temp;
    PrecisionTensor mb_prio;
    IntTensor idx, sample_done;
};

void register_prio_buffers(PrioBuffers& bufs, Allocator* alloc, int B, int minibatch_segments) {
    size_t cdf_temp_bytes = 0;
    cudaError_t err = cub::DeviceScan::InclusiveSum(
        NULL, cdf_temp_bytes, (float*)NULL, (float*)NULL, B);
    assert(err == cudaSuccess);

    bufs = (PrioBuffers){
        .prio_weights = {.shape = {B}},
        .cdf = {.shape = {B}},
        .cdf_temp = {.shape = {(int64_t)cdf_temp_bytes}},
        .mb_prio = {.shape = {minibatch_segments}},
        .idx = {.shape = {minibatch_segments}},
        .sample_done = {.shape = {1}},
    };
    alloc_register(alloc, &bufs.prio_weights);
    alloc_register(alloc, &bufs.cdf);
    alloc_register(alloc, &bufs.cdf_temp);
    alloc_register(alloc, &bufs.idx);
    alloc_register(alloc, &bufs.sample_done);
    alloc_register(alloc, &bufs.mb_prio);
}

struct StateBuffer {
    PufferState* states;       // CPU state_buffer_size entries
    PufferState* candidate_states; // CPU scratch, length candidate_capacity
    float* priorities;         // CPU priority per persistent slot
    precision_t* priorities_host; // CPU scratch for copying priorities to GPU
    precision_t* env_scores_host; // CPU scratch, length candidate_capacity
    int* heap;                 // CPU min-heap of persistent slot ids
    int* heap_pos;             // CPU inverse heap position per slot
    int capacity;
    int size;
    int num_envs;
    int num_checkpoints;
    int checkpoint_interval;
    int candidate_capacity;
    int score_capacity;
    float min_priority;
    int agents_per_env;
    int num_cl_envs;
    int num_fresh_envs;
    int admit_adv;
    int oracle_saved_level;
    float oracle_saved_score;
    float oracle_saved_priority;
    int* env_state_inds_host;  // CPU scratch, length num_envs
    PrecisionTensor advantages; // GPU, shape {state_buffer_size}
    PrecisionTensor env_scores; // GPU scratch, shape {candidate_capacity}
    PrecisionTensor importance; // GPU, shape {total_agents}; fresh=1, CL=PER IS weight
    PrioBuffers prio_bufs;      // GPU CDF/weights/idx/importance for curriculum
#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
    PrecisionTensor env_debug;  // GPU scratch, shape {candidate_capacity * debug_fields}
    precision_t* env_debug_host; // CPU scratch, checkpoint source debug fields
    int* state_outcomes;        // CPU outcome bucket per persistent slot
    long diag_retained_outcome[PUFFER_CURRICULUM_DIAG_BINS];
    float diag_retained_priority_sum[PUFFER_CURRICULUM_DIAG_BINS];
    int oracle_saved_pick_t;
    int oracle_saved_first_t;
    int oracle_saved_last_t;
    int oracle_saved_full;
    int oracle_saved_mastered;
    long oracle_saved_agent_step;
    float oracle_saved_terminal_return;
    int oracle_hist_capacity;
    int oracle_segment_capacity;
    int oracle_max_segments;
    int oracle_segment_count;
    float* oracle_state_priority;
    float* oracle_state_return;
    int* oracle_state_update_source;
    float* oracle_segment_priority;
    float* oracle_segment_score;
    float* oracle_segment_terminal_return;
    int* oracle_segment_level;
    int* oracle_segment_len;
    long* oracle_segment_agent_step;
    long long* oracle_segment_sample_count;
#ifdef BOXOBAN_LEVEL_LOGS
    float* oracle_segment_solve_rate;
#endif
    float* oracle_env_discounted_return;
    PufferState* oracle_hist_states;
    int* oracle_hist_source;
    int* oracle_hist_count;
    int* oracle_hist_write;
    int* oracle_hist_level;
    int* oracle_hist_from_entry;
    PufferState* oracle_pending_states;
    int* oracle_pending_source;
    PufferState* oracle_candidate_states;
    int* oracle_candidate_source;
    int* oracle_candidate_count;
    int* oracle_candidate_level;
    int* oracle_candidate_full;
    int* oracle_candidate_solve_level;
    float* oracle_candidate_solve_rate;
    float* oracle_candidate_terminal_return;
#ifdef BOXOBAN_LEVEL_LOGS
    int* oracle_source_solve_level;
    float* oracle_source_solve_rate;
    float* oracle_source_solve_episodes;
    long long* oracle_source_agent_step;
    long long oracle_diag_fresh_episodes;
    long long oracle_diag_fresh_level_solved[BOXOBAN_LEVEL_LOGS];
    long long oracle_diag_vferr_count[PUFFER_CURRICULUM_SOLVE_RATE_BINS];
    float oracle_diag_vferr_sum[PUFFER_CURRICULUM_SOLVE_RATE_BINS];
    float oracle_diag_vferr_max[PUFFER_CURRICULUM_SOLVE_RATE_BINS];
    long long oracle_diag_eject_count[PUFFER_CURRICULUM_SOLVE_RATE_BINS];
    long long oracle_diag_eject_samples_sum[PUFFER_CURRICULUM_SOLVE_RATE_BINS];
    long long oracle_diag_eject_samples_max[PUFFER_CURRICULUM_SOLVE_RATE_BINS];
#endif
    float oracle_diag_candidate_top_raw[PUFFER_CURRICULUM_TOP_DIAG];
    float oracle_diag_candidate_top_priority[PUFFER_CURRICULUM_TOP_DIAG];
    float oracle_diag_candidate_top_solve_rate[PUFFER_CURRICULUM_TOP_DIAG];
    int oracle_diag_candidate_top_solve_level[PUFFER_CURRICULUM_TOP_DIAG];
    int oracle_pending_level;
    float oracle_pending_score;
    float oracle_pending_terminal_return;
    float oracle_pending_priority;
    float oracle_pending_solve_rate;
    int oracle_pending_count;
    int oracle_pending_first_t;
    int oracle_pending_last_t;
    int oracle_pending_full;
    int oracle_pending_env_idx;
    int oracle_pending_needs_priority;
    int oracle_diag_candidate_count;
    int oracle_diag_selected_stored;
    float oracle_diag_candidate_raw_max;
    float oracle_diag_candidate_raw_max_priority;
    float oracle_diag_candidate_raw_max_solve_rate;
    int oracle_diag_candidate_raw_max_solve_level;
    float oracle_diag_candidate_priority_max;
    float oracle_diag_candidate_priority_max_raw;
    float oracle_diag_candidate_priority_max_solve_rate;
    int oracle_diag_candidate_priority_max_solve_level;
    float oracle_diag_selected_raw;
    float oracle_diag_selected_priority;
    float oracle_diag_selected_solve_rate;
    int oracle_diag_selected_solve_level;
    int oracle_diag_raw_selected;
    int oracle_diag_raw_retained;
    int oracle_diag_frontier_offenders;
    float oracle_diag_frontier_offender_priority_sum;
    float oracle_diag_frontier_offender_priority_max;
    float oracle_diag_frontier_offender_raw;
    int oracle_diag_frontier_offender_solve_level;
    float oracle_diag_frontier_offender_solve_rate;
    float oracle_diag_frontier_offender_gap_sum;
    float oracle_diag_frontier_offender_gap_max;
    int oracle_diag_frontier_offender_len;
    float oracle_diag_frontier_offender_abs_mean;
    float oracle_diag_frontier_offender_pos_mean;
    float oracle_diag_frontier_offender_neg_mean;
    float oracle_diag_frontier_offender_pos_frac;
    float oracle_diag_frontier_offender_neg_frac;
    float oracle_diag_frontier_offender_max_pos;
    float oracle_diag_frontier_offender_min_neg;
    float oracle_diag_frontier_offender_max_abs;
    float oracle_diag_frontier_offender_max_raw;
    int oracle_diag_frontier_offender_solve_offset;
    int oracle_diag_frontier_offender_max_offset;
    int oracle_diag_frontier_offender_reward_dist;
    int oracle_diag_frontier_offender_maps_solved;
    int oracle_diag_frontier_offender_sequence_pos;
    int oracle_diag_frontier_offender_puzzle_tick;
    float oracle_diag_frontier_offender_value;
    float oracle_diag_frontier_offender_next_value;
    float oracle_diag_frontier_offender_reward;
    float oracle_diag_frontier_offender_terminal;
    float oracle_diag_frontier_offender_delta;
    float oracle_diag_frontier_offender_gae;
#ifdef BOXOBAN_LEVEL_LOGS
    long long oracle_diag_frontier_offender_bucket_count[PUFFER_CURRICULUM_SOLVE_RATE_BINS];
    float oracle_diag_frontier_offender_bucket_max[PUFFER_CURRICULUM_SOLVE_RATE_BINS];
#endif
    float oracle_diag_min_priority;
    float oracle_diag_min_raw;
    int oracle_diag_raw_solve_offset;
    int oracle_diag_raw_max_offset;
    int oracle_diag_adv_solve_offset;
    int oracle_diag_adv_max_offset;
    float oracle_diag_raw_max_adv;
    float oracle_diag_adv_max_adv;
    float oracle_diag_raw_max_return;
    float oracle_diag_adv_max_return;
#ifdef BOXOBAN_LEVEL_LOGS
    int oracle_diag_raw_max_solve_level;
    float oracle_diag_raw_max_solve_rate;
    float oracle_diag_raw_max_solve_episodes;
    long long oracle_diag_raw_max_agent_step;
    int oracle_diag_adv_max_solve_level;
    float oracle_diag_adv_max_solve_rate;
    float oracle_diag_adv_max_solve_episodes;
    long long oracle_diag_adv_max_agent_step;
#endif
    int oracle_diag_raw_prev_reward_dist;
    int oracle_diag_raw_next_reward_dist;
    int oracle_diag_adv_prev_reward_dist;
    int oracle_diag_adv_next_reward_dist;
    int oracle_diag_raw_max_maps_solved;
    int oracle_diag_raw_max_sequence_pos;
    int oracle_diag_raw_max_puzzle_tick;
    int oracle_diag_adv_max_maps_solved;
    int oracle_diag_adv_max_sequence_pos;
    int oracle_diag_adv_max_puzzle_tick;
    float oracle_diag_raw_pre_adv[5];
    float oracle_diag_adv_pre_adv[5];
    int* oracle_cl_start_bucket;
    int* oracle_cl_start_slot;
    int* oracle_cl_head_sample;
    int* oracle_head_update_seg;
    int* oracle_head_update_count;
    int oracle_sample_start;
    long long oracle_window_attempts;
    long long oracle_window_successes;
    long long oracle_start_attempts;
    long long oracle_start_successes;
    long long oracle_cl_attempts[PUFFER_CURRICULUM_CL_BINS];
    long long oracle_cl_successes[PUFFER_CURRICULUM_CL_BINS];
    long long oracle_fresh_attempts;
    long long oracle_fresh_successes;
    long long oracle_fresh_solve_candidates;
    long long oracle_fresh_segments_stored;
    long long oracle_cl_terminals;
    long long oracle_cl_solve_terminals;
    long long oracle_cl_tail_replacements;
    int oracle_trace_dump_count;
    long long oracle_trace_last_agent_step;
    long long oracle_trace_score_last_agent_step[PUFFER_CURRICULUM_DIAG_BINS];
    int oracle_trace_score_count[PUFFER_CURRICULUM_DIAG_BINS];
#endif
};

void register_state_buffer(StateBuffer* buf, Allocator* alloc,
        int capacity, int total_agents, int num_envs, int agents_per_env,
        int num_cl_envs, int horizon, int checkpoint_interval) {
    buf->capacity = capacity;
    buf->size = 0;
    buf->num_envs = num_envs;
    buf->oracle_saved_level = -1;
    buf->oracle_saved_score = 0.0f;
    buf->oracle_saved_priority = 0.0f;
    buf->checkpoint_interval = checkpoint_interval;
    buf->num_checkpoints = (horizon + checkpoint_interval - 1) / checkpoint_interval;
    buf->candidate_capacity = num_envs * buf->num_checkpoints;
    buf->score_capacity = buf->candidate_capacity + num_envs;
    buf->min_priority = 0.0f;
    buf->agents_per_env = agents_per_env;
    buf->admit_adv = 1;
    buf->advantages = {.shape = {capacity}};
    buf->env_scores = {.shape = {buf->score_capacity}};
    buf->importance = {.shape = {total_agents}};
    alloc_register(alloc, &buf->advantages);
    alloc_register(alloc, &buf->env_scores);
    alloc_register(alloc, &buf->importance);
#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
    buf->env_debug = {
        .shape = {buf->candidate_capacity * PUFFER_CURRICULUM_DEBUG_FIELDS}};
    alloc_register(alloc, &buf->env_debug);
#endif
    if (num_cl_envs > 0) {
        register_prio_buffers(buf->prio_bufs, alloc, capacity, num_cl_envs);
    }
}

int init_state_buffer(StateBuffer* buf, int total_agents) {
    size_t capacity = (size_t)buf->capacity;
    size_t state_size = sizeof(PufferState);
    size_t state_bytes = capacity * state_size;
    size_t candidate_bytes = (size_t)buf->candidate_capacity * state_size;
    buf->states = (PufferState*)malloc(state_bytes);
    buf->candidate_states = (PufferState*)malloc(candidate_bytes);
    buf->priorities = (float*)malloc(capacity * sizeof(float));
    buf->priorities_host = (precision_t*)malloc(capacity * sizeof(precision_t));
    buf->env_scores_host = (precision_t*)malloc(
        (size_t)buf->score_capacity * sizeof(precision_t));
    buf->heap = (int*)malloc(capacity * sizeof(int));
    buf->heap_pos = (int*)malloc(capacity * sizeof(int));
    buf->env_state_inds_host = (int*)malloc((size_t)buf->num_envs * sizeof(int));
#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
    buf->env_debug_host = (precision_t*)malloc(
        (size_t)buf->candidate_capacity
        * PUFFER_CURRICULUM_DEBUG_FIELDS * sizeof(precision_t));
    buf->oracle_saved_score = 0.0f;
    buf->oracle_saved_priority = 0.0f;
    buf->oracle_saved_pick_t = -1;
    buf->oracle_saved_first_t = -1;
    buf->oracle_saved_last_t = -1;
    buf->oracle_saved_full = 0;
    buf->oracle_saved_mastered = 0;
    buf->oracle_saved_agent_step = -1;
    buf->oracle_saved_terminal_return = 0.0f;
    buf->oracle_hist_capacity = buf->capacity < 256 ? buf->capacity : 256;
    buf->oracle_segment_capacity = buf->oracle_hist_capacity;
    buf->oracle_max_segments = buf->oracle_segment_capacity > 0
        ? buf->capacity / buf->oracle_segment_capacity : 0;
    if (buf->oracle_max_segments < 1 && buf->capacity > 0) {
        buf->oracle_max_segments = 1;
    }
    buf->oracle_segment_count = 0;
    buf->oracle_pending_level = -1;
    buf->oracle_pending_score = 0.0f;
    buf->oracle_pending_terminal_return = 0.0f;
    buf->oracle_pending_priority = 0.0f;
    buf->oracle_pending_solve_rate = -1.0f;
    buf->oracle_pending_count = 0;
    buf->oracle_pending_first_t = -1;
    buf->oracle_pending_last_t = -1;
    buf->oracle_pending_full = 0;
    buf->oracle_pending_env_idx = -1;
    buf->oracle_pending_needs_priority = 0;
    buf->oracle_diag_candidate_count = 0;
    buf->oracle_diag_selected_stored = 0;
    buf->oracle_diag_candidate_raw_max = 0.0f;
    buf->oracle_diag_candidate_raw_max_priority = 0.0f;
    buf->oracle_diag_candidate_raw_max_solve_rate = -1.0f;
    buf->oracle_diag_candidate_raw_max_solve_level = -1;
    buf->oracle_diag_candidate_priority_max = 0.0f;
    buf->oracle_diag_candidate_priority_max_raw = 0.0f;
    buf->oracle_diag_candidate_priority_max_solve_rate = -1.0f;
    buf->oracle_diag_candidate_priority_max_solve_level = -1;
    buf->oracle_diag_selected_raw = 0.0f;
    buf->oracle_diag_selected_priority = 0.0f;
    buf->oracle_diag_selected_solve_rate = -1.0f;
    buf->oracle_diag_selected_solve_level = -1;
    buf->oracle_diag_raw_selected = 0;
    buf->oracle_diag_raw_retained = 0;
    buf->oracle_diag_frontier_offenders = 0;
    buf->oracle_diag_frontier_offender_priority_sum = 0.0f;
    buf->oracle_diag_frontier_offender_priority_max = 0.0f;
    buf->oracle_diag_frontier_offender_raw = 0.0f;
    buf->oracle_diag_frontier_offender_solve_level = -1;
    buf->oracle_diag_frontier_offender_solve_rate = -1.0f;
    buf->oracle_diag_frontier_offender_gap_sum = 0.0f;
    buf->oracle_diag_frontier_offender_gap_max = 0.0f;
    buf->oracle_diag_frontier_offender_len = 0;
    buf->oracle_diag_frontier_offender_abs_mean = 0.0f;
    buf->oracle_diag_frontier_offender_pos_mean = 0.0f;
    buf->oracle_diag_frontier_offender_neg_mean = 0.0f;
    buf->oracle_diag_frontier_offender_pos_frac = 0.0f;
    buf->oracle_diag_frontier_offender_neg_frac = 0.0f;
    buf->oracle_diag_frontier_offender_max_pos = 0.0f;
    buf->oracle_diag_frontier_offender_min_neg = 0.0f;
    buf->oracle_diag_frontier_offender_max_abs = 0.0f;
    buf->oracle_diag_frontier_offender_max_raw = 0.0f;
    buf->oracle_diag_frontier_offender_solve_offset = -1;
    buf->oracle_diag_frontier_offender_max_offset = -1;
    buf->oracle_diag_frontier_offender_reward_dist = -1;
    buf->oracle_diag_frontier_offender_maps_solved = -1;
    buf->oracle_diag_frontier_offender_sequence_pos = -1;
    buf->oracle_diag_frontier_offender_puzzle_tick = -1;
    buf->oracle_diag_frontier_offender_value = 0.0f;
    buf->oracle_diag_frontier_offender_next_value = 0.0f;
    buf->oracle_diag_frontier_offender_reward = 0.0f;
    buf->oracle_diag_frontier_offender_terminal = 0.0f;
    buf->oracle_diag_frontier_offender_delta = 0.0f;
    buf->oracle_diag_frontier_offender_gae = 0.0f;
    buf->oracle_diag_min_priority = 0.0f;
    buf->oracle_diag_min_raw = 0.0f;
    buf->oracle_diag_raw_solve_offset = -1;
    buf->oracle_diag_raw_max_offset = -1;
    buf->oracle_diag_adv_solve_offset = -1;
    buf->oracle_diag_adv_max_offset = -1;
    buf->oracle_diag_raw_max_adv = 0.0f;
    buf->oracle_diag_adv_max_adv = 0.0f;
    buf->oracle_diag_raw_max_return = 0.0f;
    buf->oracle_diag_adv_max_return = 0.0f;
#ifdef BOXOBAN_LEVEL_LOGS
    buf->oracle_diag_raw_max_solve_level = -1;
    buf->oracle_diag_raw_max_solve_rate = -1.0f;
    buf->oracle_diag_raw_max_solve_episodes = 0.0f;
    buf->oracle_diag_raw_max_agent_step = -1;
    buf->oracle_diag_adv_max_solve_level = -1;
    buf->oracle_diag_adv_max_solve_rate = -1.0f;
    buf->oracle_diag_adv_max_solve_episodes = 0.0f;
    buf->oracle_diag_adv_max_agent_step = -1;
    buf->oracle_diag_fresh_episodes = 0;
    for (int i = 0; i < BOXOBAN_LEVEL_LOGS; i++) {
        buf->oracle_diag_fresh_level_solved[i] = 0;
    }
    for (int i = 0; i < PUFFER_CURRICULUM_SOLVE_RATE_BINS; i++) {
        buf->oracle_diag_vferr_count[i] = 0;
        buf->oracle_diag_vferr_sum[i] = 0.0f;
        buf->oracle_diag_vferr_max[i] = 0.0f;
        buf->oracle_diag_eject_count[i] = 0;
        buf->oracle_diag_eject_samples_sum[i] = 0;
        buf->oracle_diag_eject_samples_max[i] = 0;
        buf->oracle_diag_frontier_offender_bucket_count[i] = 0;
        buf->oracle_diag_frontier_offender_bucket_max[i] = 0.0f;
    }
#endif
    for (int i = 0; i < PUFFER_CURRICULUM_TOP_DIAG; i++) {
        buf->oracle_diag_candidate_top_raw[i] = 0.0f;
        buf->oracle_diag_candidate_top_priority[i] = 0.0f;
        buf->oracle_diag_candidate_top_solve_rate[i] = -1.0f;
        buf->oracle_diag_candidate_top_solve_level[i] = -1;
    }
    buf->oracle_diag_raw_prev_reward_dist = -1;
    buf->oracle_diag_raw_next_reward_dist = -1;
    buf->oracle_diag_adv_prev_reward_dist = -1;
    buf->oracle_diag_adv_next_reward_dist = -1;
    buf->oracle_diag_raw_max_maps_solved = -1;
    buf->oracle_diag_raw_max_sequence_pos = -1;
    buf->oracle_diag_raw_max_puzzle_tick = -1;
    buf->oracle_diag_adv_max_maps_solved = -1;
    buf->oracle_diag_adv_max_sequence_pos = -1;
    buf->oracle_diag_adv_max_puzzle_tick = -1;
    for (int i = 0; i < 5; i++) {
        buf->oracle_diag_raw_pre_adv[i] = 0.0f;
        buf->oracle_diag_adv_pre_adv[i] = 0.0f;
    }
    buf->oracle_sample_start = 0;
    buf->oracle_window_attempts = 0;
    buf->oracle_window_successes = 0;
    buf->oracle_start_attempts = 0;
    buf->oracle_start_successes = 0;
    buf->oracle_fresh_attempts = 0;
    buf->oracle_fresh_successes = 0;
    buf->oracle_fresh_solve_candidates = 0;
    buf->oracle_fresh_segments_stored = 0;
    buf->oracle_cl_terminals = 0;
    buf->oracle_cl_solve_terminals = 0;
    buf->oracle_cl_tail_replacements = 0;
    buf->oracle_trace_dump_count = 0;
    buf->oracle_trace_last_agent_step = -PUFFER_CURRICULUM_TRACE_MIN_STEPS;
    for (int i = 0; i < PUFFER_CURRICULUM_DIAG_BINS; i++) {
        buf->oracle_trace_score_count[i] = 0;
        buf->oracle_trace_score_last_agent_step[i] =
            -PUFFER_CURRICULUM_TRACE_MIN_STEPS;
    }
    FILE* trace_file = fopen(PUFFER_CURRICULUM_TRACE_PATH, "w");
    if (trace_file != NULL) {
        fclose(trace_file);
    }
    memset(buf->oracle_cl_attempts, 0, sizeof(buf->oracle_cl_attempts));
    memset(buf->oracle_cl_successes, 0, sizeof(buf->oracle_cl_successes));
    buf->state_outcomes = (int*)malloc(capacity * sizeof(int));
    buf->oracle_state_priority = (float*)malloc(capacity * sizeof(float));
    buf->oracle_state_return = (float*)malloc(capacity * sizeof(float));
    buf->oracle_state_update_source = (int*)malloc(capacity * sizeof(int));
    buf->oracle_segment_priority = (float*)malloc(
        (size_t)buf->oracle_max_segments * sizeof(float));
    buf->oracle_segment_score = (float*)malloc(
        (size_t)buf->oracle_max_segments * sizeof(float));
    buf->oracle_segment_terminal_return = (float*)malloc(
        (size_t)buf->oracle_max_segments * sizeof(float));
    buf->oracle_segment_level = (int*)malloc(
        (size_t)buf->oracle_max_segments * sizeof(int));
    buf->oracle_segment_len = (int*)malloc(
        (size_t)buf->oracle_max_segments * sizeof(int));
    buf->oracle_segment_agent_step = (long*)malloc(
        (size_t)buf->oracle_max_segments * sizeof(long));
    buf->oracle_segment_sample_count = (long long*)malloc(
        (size_t)buf->oracle_max_segments * sizeof(long long));
#ifdef BOXOBAN_LEVEL_LOGS
    buf->oracle_segment_solve_rate = (float*)malloc(
        (size_t)buf->oracle_max_segments * sizeof(float));
#endif
    buf->oracle_env_discounted_return = (float*)malloc(
        (size_t)buf->num_envs * sizeof(float));
    buf->oracle_hist_states = (PufferState*)malloc(
        (size_t)buf->num_envs * buf->oracle_hist_capacity * sizeof(PufferState));
    buf->oracle_hist_source = (int*)malloc(
        (size_t)buf->num_envs * buf->oracle_hist_capacity * sizeof(int));
    buf->oracle_hist_count = (int*)malloc((size_t)buf->num_envs * sizeof(int));
    buf->oracle_hist_write = (int*)malloc((size_t)buf->num_envs * sizeof(int));
    buf->oracle_hist_level = (int*)malloc((size_t)buf->num_envs * sizeof(int));
    buf->oracle_hist_from_entry = (int*)malloc((size_t)buf->num_envs * sizeof(int));
    buf->oracle_cl_start_bucket = (int*)malloc((size_t)buf->num_envs * sizeof(int));
    buf->oracle_cl_start_slot = (int*)malloc((size_t)buf->num_envs * sizeof(int));
    buf->oracle_cl_head_sample = (int*)malloc((size_t)buf->num_envs * sizeof(int));
    buf->oracle_head_update_seg = (int*)malloc((size_t)buf->num_envs * sizeof(int));
    buf->oracle_head_update_count = (int*)malloc((size_t)buf->num_envs * sizeof(int));
    buf->oracle_pending_states = (PufferState*)malloc(
        (size_t)buf->oracle_hist_capacity * sizeof(PufferState));
    buf->oracle_pending_source = (int*)malloc(
        (size_t)buf->oracle_hist_capacity * sizeof(int));
    buf->oracle_candidate_states = (PufferState*)malloc(
        (size_t)buf->num_envs * buf->oracle_hist_capacity * sizeof(PufferState));
    buf->oracle_candidate_source = (int*)malloc(
        (size_t)buf->num_envs * buf->oracle_hist_capacity * sizeof(int));
    buf->oracle_candidate_count = (int*)malloc(
        (size_t)buf->num_envs * sizeof(int));
    buf->oracle_candidate_level = (int*)malloc(
        (size_t)buf->num_envs * sizeof(int));
    buf->oracle_candidate_full = (int*)malloc(
        (size_t)buf->num_envs * sizeof(int));
    buf->oracle_candidate_solve_level = (int*)malloc(
        (size_t)buf->num_envs * sizeof(int));
    buf->oracle_candidate_solve_rate = (float*)malloc(
        (size_t)buf->num_envs * sizeof(float));
    buf->oracle_candidate_terminal_return = (float*)malloc(
        (size_t)buf->num_envs * sizeof(float));
#ifdef BOXOBAN_LEVEL_LOGS
    buf->oracle_source_solve_level = (int*)malloc(
        (size_t)buf->candidate_capacity * sizeof(int));
    buf->oracle_source_solve_rate = (float*)malloc(
        (size_t)buf->candidate_capacity * sizeof(float));
    buf->oracle_source_solve_episodes = (float*)malloc(
        (size_t)buf->candidate_capacity * sizeof(float));
    buf->oracle_source_agent_step = (long long*)malloc(
        (size_t)buf->candidate_capacity * sizeof(long long));
#endif
#endif
    if (buf->states == NULL || buf->candidate_states == NULL
            || buf->priorities == NULL || buf->priorities_host == NULL
            || buf->env_scores_host == NULL || buf->heap == NULL || buf->heap_pos == NULL
            || buf->env_state_inds_host == NULL
#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
            || buf->env_debug_host == NULL
            || buf->state_outcomes == NULL
            || buf->oracle_state_priority == NULL
            || buf->oracle_state_return == NULL
            || buf->oracle_state_update_source == NULL
            || buf->oracle_segment_priority == NULL
            || buf->oracle_segment_score == NULL
            || buf->oracle_segment_terminal_return == NULL
            || buf->oracle_segment_level == NULL
            || buf->oracle_segment_len == NULL
            || buf->oracle_segment_agent_step == NULL
            || buf->oracle_segment_sample_count == NULL
#ifdef BOXOBAN_LEVEL_LOGS
            || buf->oracle_segment_solve_rate == NULL
#endif
            || buf->oracle_env_discounted_return == NULL
            || buf->oracle_hist_states == NULL
            || buf->oracle_hist_source == NULL
            || buf->oracle_hist_count == NULL
            || buf->oracle_hist_write == NULL
            || buf->oracle_hist_level == NULL
            || buf->oracle_hist_from_entry == NULL
            || buf->oracle_cl_start_bucket == NULL
            || buf->oracle_cl_start_slot == NULL
            || buf->oracle_cl_head_sample == NULL
            || buf->oracle_head_update_seg == NULL
            || buf->oracle_head_update_count == NULL
            || buf->oracle_pending_states == NULL
            || buf->oracle_pending_source == NULL
            || buf->oracle_candidate_states == NULL
            || buf->oracle_candidate_source == NULL
            || buf->oracle_candidate_count == NULL
            || buf->oracle_candidate_level == NULL
            || buf->oracle_candidate_full == NULL
            || buf->oracle_candidate_solve_level == NULL
            || buf->oracle_candidate_solve_rate == NULL
            || buf->oracle_candidate_terminal_return == NULL
#ifdef BOXOBAN_LEVEL_LOGS
            || buf->oracle_source_solve_level == NULL
            || buf->oracle_source_solve_rate == NULL
            || buf->oracle_source_solve_episodes == NULL
            || buf->oracle_source_agent_step == NULL
#endif
#endif
            ) {
        fprintf(stderr,
            "Failed to allocate curriculum state buffer: capacity=%d state_size=%d bytes=%zu\n",
            buf->capacity, (int)state_size, state_bytes);
        return 0;
    }
#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
    memset(buf->env_debug_host, 0,
        (size_t)buf->candidate_capacity
        * PUFFER_CURRICULUM_DEBUG_FIELDS * sizeof(precision_t));
    memset(buf->state_outcomes, 0, capacity * sizeof(int));
    memset(buf->oracle_state_priority, 0, capacity * sizeof(float));
    memset(buf->oracle_state_return, 0, capacity * sizeof(float));
    memset(buf->oracle_state_update_source, -1, capacity * sizeof(int));
    memset(buf->oracle_env_discounted_return, 0,
        (size_t)buf->num_envs * sizeof(float));
    memset(buf->diag_retained_outcome, 0, sizeof(buf->diag_retained_outcome));
    memset(buf->diag_retained_priority_sum, 0, sizeof(buf->diag_retained_priority_sum));
    for (int i = 0; i < buf->oracle_max_segments; i++) {
        buf->oracle_segment_priority[i] = 0.0f;
        buf->oracle_segment_score[i] = 0.0f;
        buf->oracle_segment_terminal_return[i] = 0.0f;
        buf->oracle_segment_level[i] = -1;
        buf->oracle_segment_len[i] = 0;
        buf->oracle_segment_agent_step[i] = -1;
        buf->oracle_segment_sample_count[i] = 0;
#ifdef BOXOBAN_LEVEL_LOGS
        buf->oracle_segment_solve_rate[i] = -1.0f;
#endif
    }
    memset(buf->oracle_hist_count, 0, (size_t)buf->num_envs * sizeof(int));
    memset(buf->oracle_hist_write, 0, (size_t)buf->num_envs * sizeof(int));
    memset(buf->oracle_hist_source, -1,
        (size_t)buf->num_envs * buf->oracle_hist_capacity * sizeof(int));
    memset(buf->oracle_pending_source, -1,
        (size_t)buf->oracle_hist_capacity * sizeof(int));
    memset(buf->oracle_candidate_source, -1,
        (size_t)buf->num_envs * buf->oracle_hist_capacity * sizeof(int));
    memset(buf->oracle_candidate_count, 0, (size_t)buf->num_envs * sizeof(int));
    memset(buf->oracle_candidate_terminal_return, 0,
        (size_t)buf->num_envs * sizeof(float));
#ifdef BOXOBAN_LEVEL_LOGS
    for (int i = 0; i < buf->candidate_capacity; i++) {
        buf->oracle_source_solve_level[i] = -1;
        buf->oracle_source_solve_rate[i] = -1.0f;
        buf->oracle_source_solve_episodes[i] = 0.0f;
        buf->oracle_source_agent_step[i] = -1;
    }
#endif
    for (int i = 0; i < buf->num_envs; i++) {
        buf->oracle_hist_level[i] = -1;
        buf->oracle_hist_from_entry[i] = 0;
        buf->oracle_candidate_level[i] = -1;
        buf->oracle_candidate_full[i] = 0;
        buf->oracle_candidate_solve_level[i] = -1;
        buf->oracle_candidate_solve_rate[i] = -1.0f;
        buf->oracle_cl_start_bucket[i] = -1;
        buf->oracle_cl_start_slot[i] = -1;
        buf->oracle_cl_head_sample[i] = 0;
        buf->oracle_head_update_seg[i] = -1;
        buf->oracle_head_update_count[i] = 0;
    }
#endif
    return 1;
}

void close_state_buffer(StateBuffer* buf) {
    free(buf->states);
    free(buf->candidate_states);
    free(buf->priorities);
    free(buf->priorities_host);
    free(buf->env_scores_host);
    free(buf->heap);
    free(buf->heap_pos);
    free(buf->env_state_inds_host);
#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
    free(buf->env_debug_host);
    free(buf->state_outcomes);
    free(buf->oracle_state_priority);
    free(buf->oracle_state_return);
    free(buf->oracle_state_update_source);
    free(buf->oracle_segment_priority);
    free(buf->oracle_segment_score);
    free(buf->oracle_segment_terminal_return);
    free(buf->oracle_segment_level);
    free(buf->oracle_segment_len);
    free(buf->oracle_segment_agent_step);
    free(buf->oracle_segment_sample_count);
#ifdef BOXOBAN_LEVEL_LOGS
    free(buf->oracle_segment_solve_rate);
#endif
    free(buf->oracle_env_discounted_return);
    free(buf->oracle_hist_states);
    free(buf->oracle_hist_source);
    free(buf->oracle_hist_count);
    free(buf->oracle_hist_write);
    free(buf->oracle_hist_level);
    free(buf->oracle_hist_from_entry);
    free(buf->oracle_cl_start_bucket);
    free(buf->oracle_cl_start_slot);
    free(buf->oracle_cl_head_sample);
    free(buf->oracle_head_update_seg);
    free(buf->oracle_head_update_count);
    free(buf->oracle_pending_states);
    free(buf->oracle_pending_source);
    free(buf->oracle_candidate_states);
    free(buf->oracle_candidate_source);
    free(buf->oracle_candidate_count);
    free(buf->oracle_candidate_level);
    free(buf->oracle_candidate_full);
    free(buf->oracle_candidate_solve_level);
    free(buf->oracle_candidate_solve_rate);
    free(buf->oracle_candidate_terminal_return);
#ifdef BOXOBAN_LEVEL_LOGS
    free(buf->oracle_source_solve_level);
    free(buf->oracle_source_solve_rate);
    free(buf->oracle_source_solve_episodes);
    free(buf->oracle_source_agent_step);
#endif
#endif
}

#endif

#ifdef PUFFER_CURRICULUM_IMPL

#define PRIO_WARP_SIZE 32
#define PRIO_FULL_MASK 0xffffffff
#define PRIO_BLOCK_SIZE 256
#define PRIO_NUM_WARPS (PRIO_BLOCK_SIZE / PRIO_WARP_SIZE)

static inline float clean_state_priority(float priority) {
    if (priority < 0.0f || isnan(priority) || isinf(priority)) {
        return 0.0f;
    }
    return priority;
}

#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
static inline int curriculum_diag_bucket(int value) {
    if (value < 0) {
        return 0;
    }
    if (value >= PUFFER_CURRICULUM_DIAG_BINS) {
        return PUFFER_CURRICULUM_DIAG_BINS - 1;
    }
    return value;
}

static inline int curriculum_oracle_cl_bucket(int remaining) {
    if (remaining <= 4) {
        return 0;
    }
    if (remaining <= 16) {
        return 1;
    }
    if (remaining <= 64) {
        return 2;
    }
    return 3;
}

static inline void curriculum_oracle_reset_cl_counters(StateBuffer* buf) {
    memset(buf->oracle_cl_attempts, 0, sizeof(buf->oracle_cl_attempts));
    memset(buf->oracle_cl_successes, 0, sizeof(buf->oracle_cl_successes));
    buf->oracle_window_attempts = 0;
    buf->oracle_window_successes = 0;
    buf->oracle_start_attempts = 0;
    buf->oracle_start_successes = 0;
    buf->oracle_fresh_attempts = 0;
    buf->oracle_fresh_successes = 0;
    buf->oracle_fresh_solve_candidates = 0;
    buf->oracle_fresh_segments_stored = 0;
    buf->oracle_cl_terminals = 0;
    buf->oracle_cl_solve_terminals = 0;
    buf->oracle_cl_tail_replacements = 0;
#ifdef BOXOBAN_LEVEL_LOGS
    memset(buf->oracle_diag_eject_count, 0,
        sizeof(buf->oracle_diag_eject_count));
    memset(buf->oracle_diag_eject_samples_sum, 0,
        sizeof(buf->oracle_diag_eject_samples_sum));
    memset(buf->oracle_diag_eject_samples_max, 0,
        sizeof(buf->oracle_diag_eject_samples_max));
#endif
    for (int i = 0; i < buf->num_envs; i++) {
        buf->oracle_cl_start_bucket[i] = -1;
        buf->oracle_cl_start_slot[i] = -1;
        buf->oracle_cl_head_sample[i] = 0;
        buf->oracle_head_update_seg[i] = -1;
        buf->oracle_head_update_count[i] = 0;
    }
}

static inline int curriculum_oracle_monitor_envs(StateBuffer* buf) {
    int monitor_envs = buf->num_fresh_envs / 2;
    if (monitor_envs < 1 && buf->num_fresh_envs > 0) {
        monitor_envs = 1;
    }
    return monitor_envs;
}

static inline void curriculum_oracle_refresh_sample_priorities(StateBuffer* buf) {
    int seg_cap = buf->oracle_segment_capacity;
    const float uniform_mix = 0.05f;
    if (seg_cap <= 0 || buf->oracle_segment_count <= 0) {
        for (int i = 0; i < buf->size; i++) {
            buf->priorities[i] = 0.0f;
            buf->priorities_host[i] = from_float(0.0f);
        }
        return;
    }

    for (int seg = 0; seg < buf->oracle_segment_count; seg++) {
        int len = buf->oracle_segment_len[seg];
        int base = seg * seg_cap;
        if (len <= 0 || len > seg_cap || base < 0 || base >= buf->size) {
            int end = base + seg_cap;
            if (end > buf->size) {
                end = buf->size;
            }
            for (int slot = base; slot < end; slot++) {
                buf->priorities[slot] = 0.0f;
                buf->priorities_host[slot] = from_float(0.0f);
            }
            continue;
        }

        int end = base + len;
        if (end > buf->size) {
            end = buf->size;
            len = end - base;
        }
        if (len <= 0) {
            continue;
        }

        float pos_sum = 0.0f;
        for (int slot = base; slot < end; slot++) {
            float adv = buf->oracle_state_priority[slot];
            if (!isnan(adv) && !isinf(adv) && adv > 0.0f) {
                pos_sum += adv;
            }
        }

        float inv_len = 1.0f / (float)len;
        for (int slot = base; slot < end; slot++) {
            float priority = inv_len;
            if (pos_sum > 0.0f) {
                float adv = buf->oracle_state_priority[slot];
                float pos_adv = (!isnan(adv) && !isinf(adv) && adv > 0.0f)
                    ? adv : 0.0f;
                priority = uniform_mix * inv_len
                    + (1.0f - uniform_mix) * (pos_adv / pos_sum);
            }
            buf->priorities[slot] = priority;
            buf->priorities_host[slot] = from_float(priority);
        }

        int clear_end = base + seg_cap;
        if (clear_end > buf->size) {
            clear_end = buf->size;
        }
        for (int slot = end; slot < clear_end; slot++) {
            buf->priorities[slot] = 0.0f;
            buf->priorities_host[slot] = from_float(0.0f);
        }
    }

    int used = buf->oracle_segment_count * seg_cap;
    if (used < 0) {
        used = 0;
    }
    if (used < buf->size) {
        for (int slot = used; slot < buf->size; slot++) {
            buf->priorities[slot] = 0.0f;
            buf->priorities_host[slot] = from_float(0.0f);
        }
    }
}

static inline int curriculum_oracle_max_advantage_slot(StateBuffer* buf) {
    int seg_cap = buf->oracle_segment_capacity;
    if (seg_cap <= 0 || buf->oracle_segment_count <= 0 || buf->size <= 0) {
        return -1;
    }

    int best_slot = -1;
    float best_adv = 0.0f;
    for (int seg = 0; seg < buf->oracle_segment_count; seg++) {
        int len = buf->oracle_segment_len[seg];
        int base = seg * seg_cap;
        if (len <= 0 || len > seg_cap || base < 0 || base >= buf->size) {
            continue;
        }

        int end = base + len;
        if (end > buf->size) {
            end = buf->size;
        }
        for (int slot = base; slot < end; slot++) {
            float adv = buf->oracle_state_priority[slot];
            if (isnan(adv) || isinf(adv) || adv <= 0.0f) {
                continue;
            }
            if (best_slot < 0 || adv > best_adv + 1e-6f) {
                best_adv = adv;
                best_slot = slot;
            }
        }
    }
    return best_slot;
}

static inline unsigned int curriculum_oracle_mix32(unsigned int x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline int curriculum_oracle_uniform_slot_in_best_segment(
        StateBuffer* buf, unsigned int salt) {
    int best_slot = curriculum_oracle_max_advantage_slot(buf);
    int seg_cap = buf->oracle_segment_capacity;
    if (best_slot < 0 || seg_cap <= 0) {
        return -1;
    }

    int seg = best_slot / seg_cap;
    if (seg < 0 || seg >= buf->oracle_segment_count) {
        return -1;
    }

    int len = buf->oracle_segment_len[seg];
    int base = seg * seg_cap;
    if (len <= 0 || len > seg_cap || base < 0 || base >= buf->size) {
        return -1;
    }
    if (base + len > buf->size) {
        len = buf->size - base;
    }
    if (len <= 0) {
        return -1;
    }

    unsigned int mixed = curriculum_oracle_mix32(
        salt ^ (unsigned int)(seg * 0x9e3779b9U)
             ^ (unsigned int)(len * 0x85ebca6bU));
    return base + (int)(mixed % (unsigned int)len);
}

static inline int curriculum_oracle_state_slot_source(int slot);
static inline void curriculum_oracle_reset_history(
    StateBuffer* buf, int env_idx, const PufferState* state, int source);

static inline int curriculum_oracle_start_cl_env_from_slot(
        StateBuffer* buf, Env* env, int env_idx, int sampled_slot,
        int prefer_head) {
    int seg_cap = buf->oracle_segment_capacity;
    if (env == NULL || seg_cap <= 0 || sampled_slot < 0
            || sampled_slot >= buf->size) {
        return 0;
    }

    int seg = sampled_slot / seg_cap;
    if (seg < 0 || seg >= buf->oracle_segment_count
            || buf->oracle_segment_len[seg] <= 0) {
        return 0;
    }

    buf->oracle_cl_head_sample[env_idx] = 0;
    if (prefer_head) {
        sampled_slot = seg * seg_cap;
        buf->oracle_cl_head_sample[env_idx] = 1;
    }

    int sampled_offset = sampled_slot - seg * seg_cap;
    if (sampled_offset < 0
            || sampled_offset >= buf->oracle_segment_len[seg]) {
        return 0;
    }

    buf->oracle_segment_sample_count[seg]++;
    const PufferState* sampled_state = &buf->states[sampled_slot];
    env->state = *sampled_state;
    puffer_state_refresh(env);

    buf->oracle_cl_start_bucket[env_idx] = -1;
    buf->oracle_cl_start_slot[env_idx] = -1;
    buf->env_state_inds_host[env_idx] = sampled_slot;
    int seg_end = seg * seg_cap + buf->oracle_segment_len[seg];
    if (seg_end > buf->size) {
        seg_end = buf->size;
    }
    int remaining = seg_end - sampled_slot - 1;
    int bucket = curriculum_oracle_cl_bucket(remaining);
    buf->oracle_cl_start_bucket[env_idx] = bucket;
    buf->oracle_cl_start_slot[env_idx] = sampled_slot;
    buf->oracle_cl_attempts[bucket]++;
    if ((sampled_slot % seg_cap) == 0) {
        buf->oracle_start_attempts++;
    }
    buf->oracle_window_attempts++;
    buf->oracle_env_discounted_return[env_idx] = 0.0f;
    curriculum_oracle_reset_history(buf, env_idx, sampled_state,
        curriculum_oracle_state_slot_source(sampled_slot));
    return 1;
}

static inline int curriculum_oracle_resample_cl_env(
        StateBuffer* buf, Env* env, int env_idx, int source) {
    int sampled_slot = curriculum_oracle_uniform_slot_in_best_segment(
        buf, (unsigned int)(source + 0x9e3779b9U));
    if (sampled_slot < 0) {
        return 0;
    }

    int cl_idx = env_idx - buf->num_fresh_envs;
    int prefer_head = (cl_idx & 1) == 0;
    return curriculum_oracle_start_cl_env_from_slot(
        buf, env, env_idx, sampled_slot, prefer_head);
}

static inline void curriculum_oracle_maybe_expand_window(StateBuffer* buf) {
    (void)buf;
}

static inline int curriculum_diag_candidate_outcome(
        StateBuffer* buf, int checkpoint_idx, int env_idx) {
    (void)buf;
    (void)env_idx;
    return curriculum_diag_bucket(checkpoint_idx);
}

static inline int curriculum_same_puzzle_segment(
        const PufferState* state, const PufferState* later) {
    return later->tick >= state->tick;
}

static inline void curriculum_backfill_checkpoint_scores(StateBuffer* buf) {
    for (int env_idx = 0; env_idx < buf->num_envs; env_idx++) {
        float best_priority = 0.0f;
        for (int c = buf->num_checkpoints - 1; c >= 0; c--) {
            int idx = c * buf->num_envs + env_idx;
            PufferState* state = &buf->candidate_states[idx];
            float priority = clean_state_priority(to_float(buf->env_scores_host[idx]));

            int same_segment = 0;
            if (c + 1 < buf->num_checkpoints) {
                PufferState* later = &buf->candidate_states[(c + 1) * buf->num_envs + env_idx];
                same_segment = curriculum_same_puzzle_segment(state, later);
            }
            if (!same_segment) {
                best_priority = priority;
            } else if (priority > best_priority) {
                best_priority = priority;
            }
            buf->env_scores_host[idx] = from_float(best_priority);
        }
    }
}

static inline void curriculum_diag_recount_retained(StateBuffer* buf) {
    memset(buf->diag_retained_outcome, 0, sizeof(buf->diag_retained_outcome));
    memset(buf->diag_retained_priority_sum, 0,
        sizeof(buf->diag_retained_priority_sum));
    for (int i = 0; i < buf->size; i++) {
        int bucket = curriculum_diag_bucket(buf->state_outcomes[i]);
        buf->diag_retained_outcome[bucket]++;
        buf->diag_retained_priority_sum[bucket] += buf->priorities[i];
    }
}
#endif

__device__ __forceinline__ float priority_power(float value, float alpha) {
    if (alpha == 0.0f) {
        return 1.0f;
    }
    if (value <= 0.0f) {
        return 0.0f;
    }
    value = __powf(value, alpha);
    if (isnan(value) || isinf(value)) {
        return 0.0f;
    }
    return value;
}

__global__ void compute_prio_abs(
        const precision_t* __restrict__ advantages,
        float* prio_weights, float prio_alpha, float eps, int rows, int stride) {
    if (stride == 1) {
        int idx = blockIdx.x * blockDim.x + threadIdx.x;
        if (idx < rows) {
            prio_weights[idx] = priority_power(fabsf(to_float(advantages[idx])), prio_alpha) + eps;
        }
        return;
    }

    int row = blockIdx.x;
    int tx = threadIdx.x;
    int offset = row * stride;

    float local_sum = 0.0f;
    for (int t = tx; t < stride; t += blockDim.x) {
        local_sum += fabsf(to_float(advantages[offset + t]));
    }

    for (int s = PRIO_WARP_SIZE / 2; s >= 1; s /= 2) {
        local_sum += __shfl_down_sync(PRIO_FULL_MASK, local_sum, s);
    }
    if (tx == 0) {
        prio_weights[row] = priority_power(local_sum, prio_alpha) + eps;
    }
}

__global__ void compute_curriculum_checkpoint_scores(
        precision_t* __restrict__ dst,
        precision_t* __restrict__ debug_dst,
        const precision_t* __restrict__ advantages_bt,
        const precision_t* __restrict__ entropy_bt,
        const precision_t* __restrict__ values_bt,
        const precision_t* __restrict__ rewards_bt,
        const precision_t* __restrict__ terminals_bt,
        int num_envs, int num_fresh_envs, int num_cl_envs,
        int num_checkpoints, int checkpoint_interval,
        int agents_per_env, float gamma, int horizon) {
    (void)entropy_bt;
    int row = blockIdx.x * blockDim.x + threadIdx.x;
    int checkpoint_rows = num_checkpoints * num_envs;
    int total_rows = checkpoint_rows + num_cl_envs;
    if (row >= total_rows) {
        return;
    }

    int env_idx;
    int start_t = 0;
    if (row < checkpoint_rows) {
        int c = row / num_envs;
        env_idx = row - c * num_envs;
        start_t = c * checkpoint_interval;
    } else {
        int i = row - checkpoint_rows;
        env_idx = num_fresh_envs + i;
    }

    int agent_start = env_idx * agents_per_env;
    float sum_agent_adv = 0.0f;
    float sum_value = 0.0f;
    float sum_next_value = 0.0f;
    float sum_next_reward = 0.0f;
    float sum_next_terminal = 0.0f;
    float sum_delta = 0.0f;
    for (int a = 0; a < agents_per_env; a++) {
        int offset = (agent_start + a) * horizon;
        // GAE already carries forward-looking credit, so score the checkpoint
        // state/action directly instead of aggregating the remaining rollout.
        float agent_adv = start_t < horizon - 1
            ? to_float(advantages_bt[offset + start_t])
            : 0.0f;
        sum_agent_adv += agent_adv;
        if (debug_dst != NULL && row < checkpoint_rows
                && values_bt != NULL && rewards_bt != NULL
                && terminals_bt != NULL) {
            float value = start_t < horizon
                ? to_float(values_bt[offset + start_t]) : 0.0f;
            float next_value = start_t < horizon - 1
                ? to_float(values_bt[offset + start_t + 1]) : 0.0f;
            float next_reward = start_t < horizon - 1
                ? to_float(rewards_bt[offset + start_t + 1]) : 0.0f;
            float next_terminal = start_t < horizon - 1
                ? to_float(terminals_bt[offset + start_t + 1]) : 1.0f;
            float delta = start_t < horizon - 1
                ? next_reward + gamma * next_value * (1.0f - next_terminal) - value
                : 0.0f;
            sum_value += value;
            sum_next_value += next_value;
            sum_next_reward += next_reward;
            sum_next_terminal += next_terminal;
            sum_delta += delta;
        }
    }
    float inv_agents = 1.0f / (float)agents_per_env;
    float mean_adv = sum_agent_adv * inv_agents;
    dst[row] = from_float(mean_adv);
    if (debug_dst != NULL && row < checkpoint_rows
            && values_bt != NULL && rewards_bt != NULL
            && terminals_bt != NULL) {
        int base = row * PUFFER_CURRICULUM_DEBUG_FIELDS;
        debug_dst[base + 0] = from_float(sum_value * inv_agents);
        debug_dst[base + 1] = from_float(sum_next_value * inv_agents);
        debug_dst[base + 2] = from_float(sum_next_reward * inv_agents);
        debug_dst[base + 3] = from_float(sum_next_terminal * inv_agents);
        debug_dst[base + 4] = from_float(sum_delta * inv_agents);
        debug_dst[base + 5] = from_float(mean_adv);
    }
}

// Multinomial with replacement (uses cuRAND)
__global__ void multinomial_sample_advance(int* __restrict__ out_idx,
        precision_t* __restrict__ out_importance,
        precision_t* __restrict__ row_importance,
        const float* __restrict__ prio_weights, const float* __restrict__ cdf,
        int B, int num_samples, uint64_t seed, float beta,
        int row_importance_offset, int agents_per_sample,
        int64_t* __restrict__ offset_ptr, int* __restrict__ done_counter,
        int launch_blocks) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    int64_t base_off = *offset_ptr;
    float total_weight = cdf[B - 1];
    if (tid < num_samples) {
        curandStatePhilox4_32_10_t rng_state;
        curand_init(seed, (uint64_t)base_off + tid, 0, &rng_state);
        float u = curand_uniform(&rng_state);

        int lo;
        int use_uniform = total_weight <= 0.0f || isnan(total_weight) || isinf(total_weight);
        if (use_uniform) {
            lo = (int)(u * (float)B);
            if (lo >= B) {
                lo = B - 1;
            }
        } else {
            float target = u * total_weight;
            lo = 0;
            int hi = B - 1;
            while (lo < hi) {
                int mid = (lo + hi) / 2;
                if (cdf[mid] < target) {
                    lo = mid + 1;
                } else {
                    hi = mid;
                }
            }
        }
        if (out_importance != NULL || row_importance != NULL) {
            float weight = 1.0f;
            if (!use_uniform) {
                float value = prio_weights[lo] * (float)B / total_weight;
                weight = __powf(value, -beta);
                if (isnan(weight) || isinf(weight)) {
                    weight = 1.0f;
                }
            }
            precision_t value = from_float(weight);
            if (out_importance != NULL) {
                out_importance[tid] = value;
            }
            if (row_importance != NULL) {
                int base = row_importance_offset + tid * agents_per_sample;
                for (int a = 0; a < agents_per_sample; a++) {
                    row_importance[base + a] = value;
                }
            }
        }
        out_idx[tid] = lo;
    }
    __syncthreads();

    if (threadIdx.x == 0) {
        __threadfence();
        int ticket = atomicAdd(done_counter, 1);
        if (ticket == launch_blocks - 1) {
            *offset_ptr = base_off + num_samples;
            *done_counter = 0;
        }
    }
}

static inline void sample_prio_indices(PrioBuffers* bufs, int population,
        int samples, ulong seed, long* offset_ptr, precision_t* out_importance,
        precision_t* row_importance, int row_importance_offset,
        int agents_per_sample, float beta, cudaStream_t stream) {
    size_t cdf_temp_bytes = (size_t)bufs->cdf_temp.shape[0];
    cub::DeviceScan::InclusiveSum(bufs->cdf_temp.data, cdf_temp_bytes,
        bufs->prio_weights.data, bufs->cdf.data, population, stream);
    int blocks = (samples + PRIO_BLOCK_SIZE - 1) / PRIO_BLOCK_SIZE;
    multinomial_sample_advance<<<blocks, PRIO_BLOCK_SIZE, 0, stream>>>(
        bufs->idx.data, out_importance, row_importance,
        bufs->prio_weights.data, bufs->cdf.data, population, samples, seed, beta,
        row_importance_offset, agents_per_sample,
        offset_ptr, bufs->sample_done.data, blocks);
}

static inline int clamp_int(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static int fixed_agents_per_env(StaticVec* vec) {
    assert(vec->size > 0 && "state curriculum requires at least one env");
    int agents_per_env = vec->envs[0].num_agents;
    assert(agents_per_env > 0 && "env num_agents must be positive");
    for (int i = 0; i < vec->size; i++) {
        assert(vec->envs[i].num_agents == agents_per_env
            && "state curriculum currently requires fixed num_agents per env");
    }
    assert(vec->size * agents_per_env == vec->total_agents
        && "env agent counts must sum to total_agents");
    assert(vec->agents_per_buffer % agents_per_env == 0
        && "state curriculum requires agents_per_buffer to be divisible by num_agents");
    return agents_per_env;
}

static inline void state_heap_swap(StateBuffer* buf, int a, int b) {
    int slot_a = buf->heap[a];
    int slot_b = buf->heap[b];
    buf->heap[a] = slot_b;
    buf->heap[b] = slot_a;
    buf->heap_pos[slot_a] = b;
    buf->heap_pos[slot_b] = a;
}

static inline void state_heap_sift_up(StateBuffer* buf, int pos) {
    while (pos > 0) {
        int parent = (pos - 1) / 2;
        if (buf->priorities[buf->heap[parent]] <= buf->priorities[buf->heap[pos]]) {
            break;
        }
        state_heap_swap(buf, parent, pos);
        pos = parent;
    }
}

static inline void state_heap_sift_down(StateBuffer* buf, int pos) {
    while (true) {
        int left = 2 * pos + 1;
        int right = left + 1;
        int best = pos;
        if (left < buf->size
                && buf->priorities[buf->heap[left]] < buf->priorities[buf->heap[best]]) {
            best = left;
        }
        if (right < buf->size
                && buf->priorities[buf->heap[right]] < buf->priorities[buf->heap[best]]) {
            best = right;
        }
        if (best == pos) {
            break;
        }
        state_heap_swap(buf, pos, best);
        pos = best;
    }
}

static inline void state_heap_refresh_min(StateBuffer* buf) {
    buf->min_priority = buf->size > 0
        ? buf->priorities[buf->heap[0]]
        : 0.0f;
}

static inline void state_heap_update_slot(StateBuffer* buf, int slot, float priority,
        float decay) {
    priority = clean_state_priority(priority);
    float old_priority = buf->priorities[slot];
    if (decay > 0.0f && priority < old_priority) {
        priority = decay * old_priority;
    }
    buf->priorities[slot] = priority;
    buf->priorities_host[slot] = from_float(priority);
    int pos = buf->heap_pos[slot];
    if (priority < old_priority) {
        state_heap_sift_up(buf, pos);
    } else {
        state_heap_sift_down(buf, pos);
    }
    state_heap_refresh_min(buf);
}

static inline int state_heap_insert(StateBuffer* buf, const PufferState* state, float priority
#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
        , int outcome
#endif
        ) {
    priority = clean_state_priority(priority);
    if (buf->size < buf->capacity) {
        int slot = buf->size;
        buf->states[slot] = *state;
        buf->priorities[slot] = priority;
        buf->priorities_host[slot] = from_float(priority);
#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
        buf->state_outcomes[slot] = curriculum_diag_bucket(outcome);
#endif
        buf->heap[slot] = slot;
        buf->heap_pos[slot] = slot;
        buf->size++;
        state_heap_sift_up(buf, slot);
        state_heap_refresh_min(buf);
        return 1;
    }

    if (priority <= buf->min_priority) {
        return 0;
    }
    int min_slot = buf->heap[0];
    buf->states[min_slot] = *state;
    buf->priorities[min_slot] = priority;
    buf->priorities_host[min_slot] = from_float(priority);
#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
    buf->state_outcomes[min_slot] = curriculum_diag_bucket(outcome);
#endif
    state_heap_sift_down(buf, 0);
    state_heap_refresh_min(buf);
    return 1;
}

#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
#endif

#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
static inline int curriculum_oracle_segment_better(
        StateBuffer* buf, float score, float priority, int count, int full, int last_t,
        float ref_score, float ref_priority, int ref_count, int ref_full,
        int ref_last_t) {
    if (count <= 0) {
        return 0;
    }

    if (buf == NULL || buf->admit_adv) {
        if (priority <= 0.0f) {
            return 0;
        }
        if (priority > ref_priority + 1e-6f) {
            return 1;
        }
        if (priority < ref_priority - 1e-6f) {
            return 0;
        }
        if (score > ref_score + 1e-6f) {
            return 1;
        }
        if (score < ref_score - 1e-6f) {
            return 0;
        }
    } else {
        if (score > ref_score + 1e-6f) {
            return 1;
        }
        if (score < ref_score - 1e-6f) {
            return 0;
        }

        if (ref_count <= 0) {
            return 1;
        }
        if (count < ref_count) {
            return 1;
        }
        if (count > ref_count) {
            return 0;
        }

        if (priority > ref_priority + 1e-6f) {
            return 1;
        }
        if (priority < ref_priority - 1e-6f) {
            return 0;
        }
    }

    (void)full;
    (void)last_t;
    (void)ref_full;
    (void)ref_last_t;
    return 0;
}

static inline int curriculum_oracle_current_promotable(StateBuffer* buf) {
    return buf->size > 0;
}

static inline void curriculum_oracle_clear_pending(StateBuffer* buf) {
    buf->oracle_pending_count = 0;
    buf->oracle_pending_level = -1;
    buf->oracle_pending_score = 0.0f;
    buf->oracle_pending_terminal_return = 0.0f;
    buf->oracle_pending_priority = 0.0f;
    buf->oracle_pending_solve_rate = -1.0f;
    buf->oracle_pending_first_t = -1;
    buf->oracle_pending_last_t = -1;
    buf->oracle_pending_full = 0;
    buf->oracle_pending_env_idx = -1;
    buf->oracle_pending_needs_priority = 0;
    if (buf->oracle_pending_source != NULL) {
        memset(buf->oracle_pending_source, -1,
            (size_t)buf->oracle_hist_capacity * sizeof(int));
    }
}

static inline int curriculum_oracle_state_slot_source(int slot) {
    return -slot - 2;
}

static inline int curriculum_oracle_source_state_slot(int source) {
    return -source - 2;
}

static inline float curriculum_oracle_discounted_return(
        const PufferState* states, int count, int start,
        float terminal_score, float gamma) {
    if (states == NULL || count <= 0 || start < 0 || start >= count) {
        return 0.0f;
    }
    gamma = fminf(1.0f, fmaxf(0.0f, gamma));
    float ret = terminal_score - states[count - 1].episode_return;
    if (isnan(ret) || isinf(ret)) {
        ret = 0.0f;
    }
    for (int i = count - 2; i >= start; i--) {
        float reward = states[i + 1].episode_return - states[i].episode_return;
        if (isnan(reward) || isinf(reward)) {
            reward = 0.0f;
        }
        ret = reward + gamma * ret;
    }
    if (isnan(ret) || isinf(ret)) {
        return 0.0f;
    }
    return ret;
}

static inline void curriculum_oracle_push_history(
        StateBuffer* buf, int env_idx, const PufferState* state, int source) {
    int cap = buf->oracle_hist_capacity;
    int base = env_idx * cap;
    int write = buf->oracle_hist_write[env_idx];
    buf->oracle_hist_states[base + write] = *state;
    buf->oracle_hist_source[base + write] = source;
    write = (write + 1) % cap;
    buf->oracle_hist_write[env_idx] = write;
    if (buf->oracle_hist_count[env_idx] < cap) {
        buf->oracle_hist_count[env_idx]++;
    }
}

static inline void curriculum_oracle_reset_history(
        StateBuffer* buf, int env_idx, const PufferState* state, int source) {
    buf->oracle_hist_count[env_idx] = 0;
    buf->oracle_hist_write[env_idx] = 0;
    buf->oracle_hist_level[env_idx] = -1;
    buf->oracle_hist_from_entry[env_idx] = 1;
    curriculum_oracle_push_history(buf, env_idx, state, source);
}

static inline const PufferState* curriculum_oracle_last_history_state(
        StateBuffer* buf, int env_idx) {
    int count = buf->oracle_hist_count[env_idx];
    if (count <= 0) {
        return NULL;
    }
    int cap = buf->oracle_hist_capacity;
    int write = buf->oracle_hist_write[env_idx];
    int last = (write + cap - 1) % cap;
    return &buf->oracle_hist_states[env_idx * cap + last];
}

static inline void curriculum_oracle_publish_history(
        StateBuffer* buf, int env_idx, float admission_score,
        float terminal_return, float priority, int needs_priority) {
    int level = buf->oracle_hist_level[env_idx];
    int count = buf->oracle_hist_count[env_idx];
    int full = buf->oracle_hist_from_entry[env_idx];
    int last_t = count > 0 ? count - 1 : -1;
    float admission_priority = needs_priority
        ? (float)(buf->num_envs - env_idx)
        : clean_state_priority(priority);
    if (count <= 0
            || (!needs_priority && admission_priority <= 0.0f)) {
        return;
    }

    int cap = buf->oracle_hist_capacity;
#ifdef _OPENMP
#pragma omp critical(puffer_oracle_segment)
#endif
    {
        int improves_pending = curriculum_oracle_segment_better(buf,
            admission_score, admission_priority, count, full, last_t,
            buf->oracle_pending_score, buf->oracle_pending_priority,
            buf->oracle_pending_count,
            buf->oracle_pending_full, buf->oracle_pending_last_t);
        if (improves_pending) {
            int base = env_idx * cap;
            int start = (buf->oracle_hist_write[env_idx] + cap - count) % cap;
            for (int i = 0; i < count; i++) {
                int src = (start + i) % cap;
                buf->oracle_pending_states[i] = buf->oracle_hist_states[base + src];
                buf->oracle_pending_source[i] = buf->oracle_hist_source[base + src];
            }
            buf->oracle_pending_level = level;
            buf->oracle_pending_score = admission_score;
            buf->oracle_pending_terminal_return = terminal_return;
            buf->oracle_pending_priority = admission_priority;
            buf->oracle_pending_solve_rate = -1.0f;
            buf->oracle_pending_count = count;
            buf->oracle_pending_first_t = 0;
            buf->oracle_pending_last_t = count - 1;
            buf->oracle_pending_full = buf->oracle_hist_from_entry[env_idx];
            buf->oracle_pending_env_idx = env_idx;
            buf->oracle_pending_needs_priority = needs_priority;
        }
    }
}

static inline void curriculum_oracle_save_candidate_history(
        StateBuffer* buf, int env_idx, float terminal_return,
        int solve_level, float solve_rate) {
    int count = buf->oracle_hist_count[env_idx];
    int cap = buf->oracle_hist_capacity;
    if (env_idx < 0 || env_idx >= buf->num_envs || count <= 0 || cap <= 0) {
        return;
    }

    int hist_base = env_idx * cap;
    int cand_base = env_idx * cap;
    int start = (buf->oracle_hist_write[env_idx] + cap - count) % cap;
    for (int i = 0; i < count; i++) {
        int src = (start + i) % cap;
        buf->oracle_candidate_states[cand_base + i] =
            buf->oracle_hist_states[hist_base + src];
        buf->oracle_candidate_source[cand_base + i] =
            buf->oracle_hist_source[hist_base + src];
    }
    for (int i = count; i < cap; i++) {
        buf->oracle_candidate_source[cand_base + i] = -1;
    }
    buf->oracle_candidate_count[env_idx] = count;
    buf->oracle_candidate_level[env_idx] = buf->oracle_hist_level[env_idx];
    buf->oracle_candidate_full[env_idx] = buf->oracle_hist_from_entry[env_idx];
    buf->oracle_candidate_solve_level[env_idx] = solve_level;
    buf->oracle_candidate_solve_rate[env_idx] = solve_rate;
    buf->oracle_candidate_terminal_return[env_idx] = terminal_return;
}

static inline void curriculum_oracle_clear_candidates(StateBuffer* buf) {
    if (buf->oracle_candidate_count == NULL) {
        return;
    }
    memset(buf->oracle_candidate_count, 0,
        (size_t)buf->num_envs * sizeof(int));
    memset(buf->oracle_candidate_terminal_return, 0,
        (size_t)buf->num_envs * sizeof(float));
    for (int i = 0; i < buf->num_envs; i++) {
        buf->oracle_candidate_level[i] = -1;
        buf->oracle_candidate_full[i] = 0;
        buf->oracle_candidate_solve_level[i] = -1;
        buf->oracle_candidate_solve_rate[i] = -1.0f;
    }
}

static inline float curriculum_oracle_source_advantage(
        StateBuffer* buf, int source);

static inline float curriculum_oracle_compute_history_priority(
        StateBuffer* buf, const int* sources, int count, int env_idx) {
    if (env_idx < 0 || env_idx >= buf->num_envs || count <= 0) {
        return 0.0f;
    }

    double positive_sum = 0.0;
    for (int i = 0; i < count; i++) {
        positive_sum += (double)curriculum_oracle_source_advantage(
            buf, sources[i]);
    }
    return clean_state_priority((float)positive_sum);
}

static inline float curriculum_oracle_compute_env_history_priority(
        StateBuffer* buf, int env_idx, int count) {
    if (env_idx < 0 || env_idx >= buf->num_envs || count <= 0
            || count > buf->oracle_hist_capacity) {
        return 0.0f;
    }

    int cap = buf->oracle_hist_capacity;
    int base = env_idx * cap;
    int start = (buf->oracle_hist_write[env_idx] + cap - count) % cap;
    double positive_sum = 0.0;
    for (int i = 0; i < count; i++) {
        int src = (start + i) % cap;
        positive_sum += (double)curriculum_oracle_source_advantage(
            buf, buf->oracle_hist_source[base + src]);
    }
    return clean_state_priority((float)positive_sum);
}

static inline float curriculum_oracle_env_history_score(
        StateBuffer* buf, int env_idx) {
    if (env_idx < 0 || env_idx >= buf->num_envs
            || buf->oracle_hist_count[env_idx] <= 0
            || buf->oracle_hist_capacity <= 0) {
        return 0.0f;
    }

    int cap = buf->oracle_hist_capacity;
    int last = (buf->oracle_hist_write[env_idx] + cap - 1) % cap;
    float score = buf->oracle_hist_states[env_idx * cap + last].episode_return;
    if (isnan(score) || isinf(score)) {
        return 0.0f;
    }
    return score;
}

static inline float curriculum_oracle_source_advantage(
        StateBuffer* buf, int source) {
    float adv = 0.0f;
    if (source >= 0 && source < buf->candidate_capacity) {
        adv = to_float(buf->env_scores_host[source]);
    } else if (source <= -2) {
        int slot = curriculum_oracle_source_state_slot(source);
        if (slot >= 0 && slot < buf->size) {
            adv = buf->oracle_state_priority[slot];
        }
    }
    if (isnan(adv) || isinf(adv)) {
        return 0.0f;
    }
    return clean_state_priority(adv);
}

static inline float curriculum_oracle_source_raw_advantage(
        StateBuffer* buf, int source) {
    float adv = 0.0f;
    if (source >= 0 && source < buf->candidate_capacity) {
        adv = to_float(buf->env_scores_host[source]);
    } else if (source <= -2) {
        int slot = curriculum_oracle_source_state_slot(source);
        if (slot >= 0 && slot < buf->size) {
            adv = buf->oracle_state_priority[slot];
        }
    }
    if (isnan(adv) || isinf(adv)) {
        return 0.0f;
    }
    return adv;
}

static inline void curriculum_oracle_set_slot_priority_from_source(
        StateBuffer* buf, int slot, int source, int defer_positive) {
    if (slot < 0 || slot >= buf->capacity) {
        return;
    }

    if (defer_positive && source >= 0 && source < buf->candidate_capacity) {
        buf->oracle_state_update_source[slot] = source;
        buf->oracle_state_priority[slot] = 0.0f;
        return;
    }

    buf->oracle_state_update_source[slot] = -1;
    float adv = curriculum_oracle_source_raw_advantage(buf, source);
    if (isnan(adv) || isinf(adv)) {
        adv = 0.0f;
    }
    buf->oracle_state_priority[slot] = adv;
}

static inline float curriculum_oracle_source_debug(
        StateBuffer* buf, int source, int field) {
    if (buf->env_debug_host == NULL
            || source < 0 || source >= buf->candidate_capacity
            || field < 0 || field >= PUFFER_CURRICULUM_DEBUG_FIELDS) {
        return 0.0f;
    }
    float value = to_float(
        buf->env_debug_host[source * PUFFER_CURRICULUM_DEBUG_FIELDS + field]);
    if (isnan(value) || isinf(value)) {
        return 0.0f;
    }
    return value;
}

static inline int curriculum_oracle_source_has_debug(StateBuffer* buf, int source) {
    return buf->env_debug_host != NULL
        && source >= 0
        && source < buf->candidate_capacity;
}

static inline void curriculum_oracle_trace_number(
        FILE* file, float value, int valid) {
    if (!valid || isnan(value) || isinf(value)) {
        fprintf(file, "null");
        return;
    }
    fprintf(file, "%.9g", value);
}

static inline void curriculum_oracle_trace_segment(
        FILE* file, StateBuffer* buf, const char* name, int env_idx,
        const PufferState* states, const int* sources, int count,
        float score, float priority, int solve_level, float solve_rate) {
    fprintf(file,
        "\"%s\":{\"env\":%d,\"score\":%.9g,\"priority\":%.9g,"
        "\"solve_level\":%d,\"solve_rate\":",
        name, env_idx, score, priority, solve_level);
    curriculum_oracle_trace_number(file, solve_rate, solve_rate >= 0.0f);
    fprintf(file, ",\"count\":%d,\"steps\":[", count);

    for (int i = 0; i < count; i++) {
        const PufferState* state = &states[i];
        int source = sources != NULL ? sources[i] : -1;
        int has_debug = curriculum_oracle_source_has_debug(buf, source);
        float adv = curriculum_oracle_source_raw_advantage(buf, source);
        float value = curriculum_oracle_source_debug(buf, source, 0);
        float next_value = curriculum_oracle_source_debug(buf, source, 1);
        float reward = curriculum_oracle_source_debug(buf, source, 2);
        float terminal = curriculum_oracle_source_debug(buf, source, 3);
        float delta = curriculum_oracle_source_debug(buf, source, 4);
        float gae = curriculum_oracle_source_debug(buf, source, 5);
        int target_valid = has_debug && !isnan(value) && !isinf(value)
            && !isnan(adv) && !isinf(adv);
        int td_target_valid = has_debug && !isnan(value) && !isinf(value)
            && !isnan(delta) && !isinf(delta);

        if (i > 0) {
            fprintf(file, ",");
        }
        fprintf(file,
            "{\"i\":%d,\"source\":%d,\"tick\":%d,"
            "\"episode_return\":%.9g",
            i, source, state->tick, state->episode_return);
#ifdef BOXOBAN_LEVEL_LOGS
        fprintf(file,
            ",\"maps_solved\":%d,\"sequence_pos\":%d,"
            "\"puzzle_tick\":%d",
            state->episode_maps_solved, state->sequence_pos,
            state->puzzle_tick);
        if (source >= 0 && source < buf->candidate_capacity) {
            fprintf(file,
                ",\"source_solve_level\":%d,\"source_solve_rate\":",
                buf->oracle_source_solve_level[source]);
            curriculum_oracle_trace_number(
                file, buf->oracle_source_solve_rate[source],
                buf->oracle_source_solve_rate[source] >= 0.0f);
            fprintf(file, ",\"source_step_m\":");
            if (buf->oracle_source_agent_step[source] >= 0) {
                fprintf(file, "%.9g",
                    (double)buf->oracle_source_agent_step[source] / 1000000.0);
            } else {
                fprintf(file, "null");
            }
        }
#endif
        fprintf(file, ",\"adv\":");
        curriculum_oracle_trace_number(file, adv, 1);
        fprintf(file, ",\"abs_adv\":");
        curriculum_oracle_trace_number(file, fabsf(adv), 1);
        fprintf(file, ",\"value\":");
        curriculum_oracle_trace_number(file, value, has_debug);
        fprintf(file, ",\"next_value\":");
        curriculum_oracle_trace_number(file, next_value, has_debug);
        fprintf(file, ",\"reward\":");
        curriculum_oracle_trace_number(file, reward, has_debug);
        fprintf(file, ",\"terminal\":");
        curriculum_oracle_trace_number(file, terminal, has_debug);
        fprintf(file, ",\"delta\":");
        curriculum_oracle_trace_number(file, delta, has_debug);
        fprintf(file, ",\"gae\":");
        curriculum_oracle_trace_number(file, gae, has_debug);
        fprintf(file, ",\"target\":");
        curriculum_oracle_trace_number(file, value + adv, target_valid);
        fprintf(file, ",\"td_target\":");
        curriculum_oracle_trace_number(file, value + delta, td_target_valid);
        fprintf(file, "}");
    }

    fprintf(file, "]}");
}

static inline void curriculum_oracle_maybe_trace_candidate_compare(
        StateBuffer* buf, long long agent_step,
        int raw_env, int raw_count, float raw_score, float raw_priority,
        int offender_env, int offender_count, float offender_score,
        float offender_priority) {
    int score_bucket = curriculum_diag_bucket((int)floorf(raw_score));
    if (buf->oracle_trace_dump_count >= PUFFER_CURRICULUM_TRACE_MAX
            || raw_env < 0 || offender_env < 0
            || raw_count <= 0 || offender_count <= 0
            || raw_score <= offender_score + 1e-6f
            || offender_priority <= raw_priority + 1e-6f
            || buf->oracle_trace_score_count[score_bucket]
                >= PUFFER_CURRICULUM_TRACE_PER_SCORE
            || agent_step - buf->oracle_trace_score_last_agent_step[score_bucket]
                < PUFFER_CURRICULUM_TRACE_MIN_STEPS) {
        return;
    }

#ifdef BOXOBAN_LEVEL_LOGS
    float offender_solve_rate = buf->oracle_candidate_solve_rate[offender_env];
    if (offender_solve_rate >= 0.0f && offender_solve_rate < 0.90f) {
        return;
    }
#endif

    FILE* file = fopen(PUFFER_CURRICULUM_TRACE_PATH, "a");
    if (file == NULL) {
        return;
    }

    int cap = buf->oracle_hist_capacity;
    int raw_base = raw_env * cap;
    int offender_base = offender_env * cap;
    fprintf(file,
        "{\"event\":%d,\"agent_steps\":%lld,"
        "\"agent_steps_m\":%.9g,\"checkpoint_interval\":%d,"
        "\"priority_metric\":\"pos_l1\",\"sample_metric\":\"max_pos_adv\","
        "\"score_bucket\":%d,"
        "\"score_bucket_event\":%d,\"min_priority\":%.9g,"
        "\"min_score\":%.9g,",
        buf->oracle_trace_dump_count, agent_step,
        (double)agent_step / 1000000.0, buf->checkpoint_interval,
        score_bucket,
        buf->oracle_trace_score_count[score_bucket],
        buf->oracle_diag_min_priority,
        buf->oracle_diag_min_raw);
    curriculum_oracle_trace_segment(
        file, buf, "frontier", raw_env,
        buf->oracle_candidate_states + raw_base,
        buf->oracle_candidate_source + raw_base, raw_count,
        raw_score, raw_priority,
        buf->oracle_candidate_solve_level[raw_env],
        buf->oracle_candidate_solve_rate[raw_env]);
    fprintf(file, ",");
    curriculum_oracle_trace_segment(
        file, buf, "offender", offender_env,
        buf->oracle_candidate_states + offender_base,
        buf->oracle_candidate_source + offender_base, offender_count,
        offender_score, offender_priority,
        buf->oracle_candidate_solve_level[offender_env],
        buf->oracle_candidate_solve_rate[offender_env]);
    fprintf(file, "}\n");
    fclose(file);

    buf->oracle_trace_dump_count++;
    buf->oracle_trace_last_agent_step = agent_step;
    buf->oracle_trace_score_count[score_bucket]++;
    buf->oracle_trace_score_last_agent_step[score_bucket] = agent_step;
}

#ifdef BOXOBAN_LEVEL_LOGS
static inline int curriculum_oracle_solve_rate_bucket(float solve_rate) {
    if (solve_rate < 0.0f || isnan(solve_rate) || isinf(solve_rate)) {
        return -1;
    }
    if (solve_rate < 0.01f) {
        return 0;
    }
    if (solve_rate < 0.10f) {
        return 1;
    }
    if (solve_rate < 0.50f) {
        return 2;
    }
    if (solve_rate < 0.90f) {
        return 3;
    }
    return 4;
}

static inline float curriculum_oracle_max_source_solve_rate(
        StateBuffer* buf, const int* sources, int count) {
    if (sources == NULL || count <= 0) {
        return -1.0f;
    }

    float max_adv = 0.0f;
    int max_source = -1;
    for (int i = 0; i < count; i++) {
        int source = sources[i];
        float adv = curriculum_oracle_source_advantage(buf, source);
        if (adv > max_adv + 1e-6f) {
            max_adv = adv;
            max_source = source;
        }
    }

    if (max_source < 0 || max_source >= buf->candidate_capacity) {
        return -1.0f;
    }
    return buf->oracle_source_solve_rate[max_source];
}

static inline float curriculum_oracle_env_history_max_source_solve_rate(
        StateBuffer* buf, int env_idx, int count) {
    if (env_idx < 0 || env_idx >= buf->num_envs || count <= 0
            || count > buf->oracle_hist_capacity) {
        return -1.0f;
    }

    int cap = buf->oracle_hist_capacity;
    int base = env_idx * cap;
    int start = (buf->oracle_hist_write[env_idx] + cap - count) % cap;
    float max_adv = 0.0f;
    int max_source = -1;
    for (int i = 0; i < count; i++) {
        int src = (start + i) % cap;
        int source = buf->oracle_hist_source[base + src];
        float adv = curriculum_oracle_source_advantage(buf, source);
        if (adv > max_adv + 1e-6f) {
            max_adv = adv;
            max_source = source;
        }
    }

    if (max_source < 0 || max_source >= buf->candidate_capacity) {
        return -1.0f;
    }
    return buf->oracle_source_solve_rate[max_source];
}

static inline void curriculum_oracle_record_vferr_bucket(
        StateBuffer* buf, float solve_rate, float value_error) {
    int bucket = curriculum_oracle_solve_rate_bucket(solve_rate);
    if (bucket < 0 || bucket >= PUFFER_CURRICULUM_SOLVE_RATE_BINS) {
        return;
    }
    value_error = clean_state_priority(value_error);
    buf->oracle_diag_vferr_count[bucket]++;
    buf->oracle_diag_vferr_sum[bucket] += value_error;
    if (value_error > buf->oracle_diag_vferr_max[bucket]) {
        buf->oracle_diag_vferr_max[bucket] = value_error;
    }
}

static inline void curriculum_oracle_record_segment_ejection(
        StateBuffer* buf, int seg) {
    if (seg < 0 || seg >= buf->oracle_segment_count) {
        return;
    }
    int bucket = curriculum_oracle_solve_rate_bucket(
        buf->oracle_segment_solve_rate[seg]);
    if (bucket < 0 || bucket >= PUFFER_CURRICULUM_SOLVE_RATE_BINS) {
        return;
    }
    long long samples = buf->oracle_segment_sample_count[seg];
    if (samples < 0) {
        samples = 0;
    }
    buf->oracle_diag_eject_count[bucket]++;
    buf->oracle_diag_eject_samples_sum[bucket] += samples;
    if (samples > buf->oracle_diag_eject_samples_max[bucket]) {
        buf->oracle_diag_eject_samples_max[bucket] = samples;
    }
}
#endif

static inline void curriculum_oracle_diag_insert_top_candidate(
        StateBuffer* buf, float raw, float priority, int solve_level,
        float solve_rate) {
    priority = clean_state_priority(priority);
    int dst = -1;
    for (int i = 0; i < PUFFER_CURRICULUM_TOP_DIAG; i++) {
        if (priority > buf->oracle_diag_candidate_top_priority[i] + 1e-6f
                || (fabsf(priority - buf->oracle_diag_candidate_top_priority[i]) <= 1e-6f
                    && raw > buf->oracle_diag_candidate_top_raw[i])) {
            dst = i;
            break;
        }
    }
    if (dst < 0) {
        return;
    }

    for (int i = PUFFER_CURRICULUM_TOP_DIAG - 1; i > dst; i--) {
        buf->oracle_diag_candidate_top_raw[i] =
            buf->oracle_diag_candidate_top_raw[i - 1];
        buf->oracle_diag_candidate_top_priority[i] =
            buf->oracle_diag_candidate_top_priority[i - 1];
        buf->oracle_diag_candidate_top_solve_level[i] =
            buf->oracle_diag_candidate_top_solve_level[i - 1];
        buf->oracle_diag_candidate_top_solve_rate[i] =
            buf->oracle_diag_candidate_top_solve_rate[i - 1];
    }
    buf->oracle_diag_candidate_top_raw[dst] = raw;
    buf->oracle_diag_candidate_top_priority[dst] = priority;
    buf->oracle_diag_candidate_top_solve_level[dst] = solve_level;
    buf->oracle_diag_candidate_top_solve_rate[dst] = solve_rate;
}

static inline const char* curriculum_diag_top_key(
        const char* prefix, int index, int field) {
    static char rt_keys[PUFFER_CURRICULUM_TOP_DIAG][5][16];
    static char ct_keys[PUFFER_CURRICULUM_TOP_DIAG][4][16];
    static int initialized = 0;
    if (!initialized) {
        for (int i = 0; i < PUFFER_CURRICULUM_TOP_DIAG; i++) {
            snprintf(rt_keys[i][0], sizeof(rt_keys[i][0]), "rt%dr", i);
            snprintf(rt_keys[i][1], sizeof(rt_keys[i][1]), "rt%da", i);
            snprintf(rt_keys[i][2], sizeof(rt_keys[i][2]), "rt%dn", i);
            snprintf(rt_keys[i][3], sizeof(rt_keys[i][3]), "rt%dm", i);
            snprintf(rt_keys[i][4], sizeof(rt_keys[i][4]), "rt%dg", i);
            snprintf(ct_keys[i][0], sizeof(ct_keys[i][0]), "ct%dr", i);
            snprintf(ct_keys[i][1], sizeof(ct_keys[i][1]), "ct%da", i);
            snprintf(ct_keys[i][2], sizeof(ct_keys[i][2]), "ct%dl", i);
            snprintf(ct_keys[i][3], sizeof(ct_keys[i][3]), "ct%ds", i);
        }
        initialized = 1;
    }
    if (index < 0 || index >= PUFFER_CURRICULUM_TOP_DIAG) {
        return "";
    }
    if (prefix != NULL && prefix[0] == 'c') {
        if (field < 0 || field >= 4) {
            return "";
        }
        return ct_keys[index][field];
    }
    if (field < 0 || field >= 5) {
        return "";
    }
    return rt_keys[index][field];
}

#ifdef BOXOBAN_LEVEL_LOGS
static inline const char* curriculum_diag_vferr_key(int index, int field) {
    static char keys[PUFFER_CURRICULUM_SOLVE_RATE_BINS][3][16];
    static int initialized = 0;
    if (!initialized) {
        for (int i = 0; i < PUFFER_CURRICULUM_SOLVE_RATE_BINS; i++) {
            snprintf(keys[i][0], sizeof(keys[i][0]), "ve%d_n", i);
            snprintf(keys[i][1], sizeof(keys[i][1]), "ve%d_m", i);
            snprintf(keys[i][2], sizeof(keys[i][2]), "ve%d_x", i);
        }
        initialized = 1;
    }
    if (index < 0 || index >= PUFFER_CURRICULUM_SOLVE_RATE_BINS
            || field < 0 || field >= 3) {
        return "";
    }
    return keys[index][field];
}

static inline const char* curriculum_diag_frontier_offender_key(
        int index, int field) {
    static char keys[PUFFER_CURRICULUM_SOLVE_RATE_BINS][2][16];
    static int initialized = 0;
    if (!initialized) {
        for (int i = 0; i < PUFFER_CURRICULUM_SOLVE_RATE_BINS; i++) {
            snprintf(keys[i][0], sizeof(keys[i][0]), "fo%d_n", i);
            snprintf(keys[i][1], sizeof(keys[i][1]), "fo%d_x", i);
        }
        initialized = 1;
    }
    if (index < 0 || index >= PUFFER_CURRICULUM_SOLVE_RATE_BINS
            || field < 0 || field >= 2) {
        return "";
    }
    return keys[index][field];
}

static inline const char* curriculum_diag_segment_use_key(
        int index, int field) {
    static char keys[PUFFER_CURRICULUM_SOLVE_RATE_BINS][5][16];
    static int initialized = 0;
    if (!initialized) {
        for (int i = 0; i < PUFFER_CURRICULUM_SOLVE_RATE_BINS; i++) {
            snprintf(keys[i][0], sizeof(keys[i][0]), "ej%d_n", i);
            snprintf(keys[i][1], sizeof(keys[i][1]), "ej%d_m", i);
            snprintf(keys[i][2], sizeof(keys[i][2]), "ej%d_x", i);
            snprintf(keys[i][3], sizeof(keys[i][3]), "lv%d_n", i);
            snprintf(keys[i][4], sizeof(keys[i][4]), "lv%d_m", i);
        }
        initialized = 1;
    }
    if (index < 0 || index >= PUFFER_CURRICULUM_SOLVE_RATE_BINS
            || field < 0 || field >= 5) {
        return "";
    }
    return keys[index][field];
}
#endif

static inline int curriculum_oracle_pre_solve_offset_index(int i) {
    switch (i) {
        case 0: return 1;
        case 1: return 4;
        case 2: return 8;
        case 3: return 16;
        case 4: return 32;
        default: return 1;
    }
}

static inline void curriculum_oracle_profile_solve_window(
        StateBuffer* buf, const PufferState* states, const int* sources,
        int count, float terminal_return, int* solve_offset_out, int* max_offset_out,
        float* max_return_out, int* prev_reward_dist_out,
        int* next_reward_dist_out, int* max_maps_solved_out,
        int* max_sequence_pos_out, int* max_puzzle_tick_out,
        float* pre_adv_out) {
    *solve_offset_out = -1;
    *max_offset_out = -1;
    *max_return_out = 0.0f;
    *prev_reward_dist_out = -1;
    *next_reward_dist_out = -1;
    *max_maps_solved_out = -1;
    *max_sequence_pos_out = -1;
    *max_puzzle_tick_out = -1;
    for (int i = 0; i < 5; i++) {
        pre_adv_out[i] = 0.0f;
    }
    if (states == NULL || sources == NULL || count <= 0) {
        return;
    }

    float max_adv = 0.0f;
    for (int i = 0; i < count; i++) {
        float adv = curriculum_oracle_source_advantage(buf, sources[i]);
        if (adv > max_adv + 1e-6f) {
            max_adv = adv;
            *max_offset_out = i;
        }
    }
    if (*max_offset_out >= 0 && *max_offset_out < count) {
        const PufferState* max_state = &states[*max_offset_out];
        *max_return_out = max_state->episode_return;
#ifdef BOXOBAN_LEVEL_LOGS
        *max_maps_solved_out = max_state->episode_maps_solved;
        *max_sequence_pos_out = max_state->sequence_pos;
        *max_puzzle_tick_out = max_state->puzzle_tick;
#else
        *max_puzzle_tick_out = max_state->tick;
#endif
    }

    int solve_offset = -1;
    int prev_reward_offset = -1;
    int next_reward_offset = -1;
    for (int i = 1; i < count; i++) {
        float reward = states[i].episode_return - states[i - 1].episode_return;
        if (!isnan(reward) && !isinf(reward) && reward > 1e-6f) {
            solve_offset = i;
            if (*max_offset_out >= 0) {
                if (i <= *max_offset_out) {
                    prev_reward_offset = i;
                } else if (next_reward_offset < 0) {
                    next_reward_offset = i;
                }
            }
        }
    }
    if (count > 0) {
        float terminal_reward = terminal_return - states[count - 1].episode_return;
        if (!isnan(terminal_reward) && !isinf(terminal_reward)
                && terminal_reward > 1e-6f) {
            solve_offset = count;
            if (*max_offset_out >= 0) {
                if (count <= *max_offset_out) {
                    prev_reward_offset = count;
                } else if (next_reward_offset < 0) {
                    next_reward_offset = count;
                }
            }
        }
    }
    if (*max_offset_out >= 0) {
        if (prev_reward_offset >= 0) {
            *prev_reward_dist_out = *max_offset_out - prev_reward_offset;
        }
        if (next_reward_offset >= 0) {
            *next_reward_dist_out = next_reward_offset - *max_offset_out;
        }
    }
    *solve_offset_out = solve_offset;
    if (solve_offset < 0) {
        return;
    }

    for (int i = 0; i < 5; i++) {
        int offset = solve_offset - curriculum_oracle_pre_solve_offset_index(i);
        if (offset >= 0 && offset < count) {
            pre_adv_out[i] =
            curriculum_oracle_source_advantage(buf, sources[offset]);
        }
    }
}

static inline void curriculum_oracle_profile_max_source_diag(
        StateBuffer* buf, const int* sources, int count, int max_offset,
        int adv_candidate) {
    float max_adv = 0.0f;
    int solve_level = -1;
    float solve_rate = -1.0f;
    float solve_episodes = 0.0f;
    long long agent_step = -1;

    if (sources != NULL && max_offset >= 0 && max_offset < count) {
        int source = sources[max_offset];
        max_adv = curriculum_oracle_source_advantage(buf, source);
#ifdef BOXOBAN_LEVEL_LOGS
        if (source >= 0 && source < buf->candidate_capacity) {
            solve_level = buf->oracle_source_solve_level[source];
            solve_rate = buf->oracle_source_solve_rate[source];
            solve_episodes = buf->oracle_source_solve_episodes[source];
            agent_step = buf->oracle_source_agent_step[source];
        }
#endif
    }

    if (adv_candidate) {
        buf->oracle_diag_adv_max_adv = max_adv;
#ifdef BOXOBAN_LEVEL_LOGS
        buf->oracle_diag_adv_max_solve_level = solve_level;
        buf->oracle_diag_adv_max_solve_rate = solve_rate;
        buf->oracle_diag_adv_max_solve_episodes = solve_episodes;
        buf->oracle_diag_adv_max_agent_step = agent_step;
#endif
    } else {
        buf->oracle_diag_raw_max_adv = max_adv;
#ifdef BOXOBAN_LEVEL_LOGS
        buf->oracle_diag_raw_max_solve_level = solve_level;
        buf->oracle_diag_raw_max_solve_rate = solve_rate;
        buf->oracle_diag_raw_max_solve_episodes = solve_episodes;
        buf->oracle_diag_raw_max_agent_step = agent_step;
#endif
    }
}

static inline void curriculum_oracle_profile_frontier_offender(
        StateBuffer* buf, const PufferState* states, const int* sources,
        int count, float terminal_return) {
    if (states == NULL || sources == NULL || count <= 0) {
        return;
    }

    double abs_sum = 0.0;
    double pos_sum = 0.0;
    double neg_sum = 0.0;
    int pos_count = 0;
    int neg_count = 0;
    float max_pos = 0.0f;
    float min_neg = 0.0f;
    float max_abs = 0.0f;
    float max_raw = 0.0f;
    int max_offset = -1;
    for (int i = 0; i < count; i++) {
        float adv = curriculum_oracle_source_raw_advantage(buf, sources[i]);
        float abs_adv = fabsf(adv);
        abs_sum += (double)abs_adv;
        if (adv > 0.0f) {
            pos_sum += (double)adv;
            pos_count++;
            if (adv > max_pos) {
                max_pos = adv;
            }
        } else if (adv < 0.0f) {
            neg_sum += (double)adv;
            neg_count++;
            if (adv < min_neg) {
                min_neg = adv;
            }
        }
        if (abs_adv > max_abs + 1e-6f) {
            max_abs = abs_adv;
            max_raw = adv;
            max_offset = i;
        }
    }

    int solve_offset = -1;
    int prev_reward_offset = -1;
    int next_reward_offset = -1;
    for (int i = 1; i < count; i++) {
        float reward = states[i].episode_return - states[i - 1].episode_return;
        if (!isnan(reward) && !isinf(reward) && reward > 1e-6f) {
            solve_offset = i;
            if (max_offset >= 0) {
                if (i <= max_offset) {
                    prev_reward_offset = i;
                } else if (next_reward_offset < 0) {
                    next_reward_offset = i;
                }
            }
        }
    }
    float terminal_reward = terminal_return - states[count - 1].episode_return;
    if (!isnan(terminal_reward) && !isinf(terminal_reward)
            && terminal_reward > 1e-6f) {
        solve_offset = count;
        if (max_offset >= 0) {
            if (count <= max_offset) {
                prev_reward_offset = count;
            } else if (next_reward_offset < 0) {
                next_reward_offset = count;
            }
        }
    }

    int reward_dist = -1;
    if (max_offset >= 0) {
        if (prev_reward_offset >= 0) {
            reward_dist = max_offset - prev_reward_offset;
        } else if (next_reward_offset >= 0) {
            reward_dist = next_reward_offset - max_offset;
        }
    }

    buf->oracle_diag_frontier_offender_len = count;
    buf->oracle_diag_frontier_offender_abs_mean =
        (float)(abs_sum / (double)count);
    buf->oracle_diag_frontier_offender_pos_mean =
        pos_count > 0 ? (float)(pos_sum / (double)pos_count) : 0.0f;
    buf->oracle_diag_frontier_offender_neg_mean =
        neg_count > 0 ? (float)(neg_sum / (double)neg_count) : 0.0f;
    buf->oracle_diag_frontier_offender_pos_frac =
        (float)pos_count / (float)count;
    buf->oracle_diag_frontier_offender_neg_frac =
        (float)neg_count / (float)count;
    buf->oracle_diag_frontier_offender_max_pos = max_pos;
    buf->oracle_diag_frontier_offender_min_neg = min_neg;
    buf->oracle_diag_frontier_offender_max_abs = max_abs;
    buf->oracle_diag_frontier_offender_max_raw = max_raw;
    buf->oracle_diag_frontier_offender_solve_offset = solve_offset;
    buf->oracle_diag_frontier_offender_max_offset = max_offset;
    buf->oracle_diag_frontier_offender_reward_dist = reward_dist;
    if (max_offset >= 0 && max_offset < count) {
        int source = sources[max_offset];
        buf->oracle_diag_frontier_offender_value =
            curriculum_oracle_source_debug(buf, source, 0);
        buf->oracle_diag_frontier_offender_next_value =
            curriculum_oracle_source_debug(buf, source, 1);
        buf->oracle_diag_frontier_offender_reward =
            curriculum_oracle_source_debug(buf, source, 2);
        buf->oracle_diag_frontier_offender_terminal =
            curriculum_oracle_source_debug(buf, source, 3);
        buf->oracle_diag_frontier_offender_delta =
            curriculum_oracle_source_debug(buf, source, 4);
        buf->oracle_diag_frontier_offender_gae =
            curriculum_oracle_source_debug(buf, source, 5);
        const PufferState* state = &states[max_offset];
#ifdef BOXOBAN_LEVEL_LOGS
        buf->oracle_diag_frontier_offender_maps_solved =
            state->episode_maps_solved;
        buf->oracle_diag_frontier_offender_sequence_pos =
            state->sequence_pos;
        buf->oracle_diag_frontier_offender_puzzle_tick =
            state->puzzle_tick;
#else
        buf->oracle_diag_frontier_offender_puzzle_tick = state->tick;
#endif
    }
}

static inline void curriculum_oracle_reset_candidate_diagnostics(StateBuffer* buf) {
    buf->oracle_diag_candidate_count = 0;
    buf->oracle_diag_selected_stored = 0;
    buf->oracle_diag_candidate_raw_max = 0.0f;
    buf->oracle_diag_candidate_raw_max_priority = 0.0f;
    buf->oracle_diag_candidate_raw_max_solve_rate = -1.0f;
    buf->oracle_diag_candidate_raw_max_solve_level = -1;
    buf->oracle_diag_candidate_priority_max = 0.0f;
    buf->oracle_diag_candidate_priority_max_raw = 0.0f;
    buf->oracle_diag_candidate_priority_max_solve_rate = -1.0f;
    buf->oracle_diag_candidate_priority_max_solve_level = -1;
    buf->oracle_diag_selected_raw = 0.0f;
    buf->oracle_diag_selected_priority = 0.0f;
    buf->oracle_diag_selected_solve_rate = -1.0f;
    buf->oracle_diag_selected_solve_level = -1;
    buf->oracle_diag_raw_selected = 0;
    buf->oracle_diag_raw_retained = 0;
    buf->oracle_diag_frontier_offenders = 0;
    buf->oracle_diag_frontier_offender_priority_sum = 0.0f;
    buf->oracle_diag_frontier_offender_priority_max = 0.0f;
    buf->oracle_diag_frontier_offender_raw = 0.0f;
    buf->oracle_diag_frontier_offender_solve_level = -1;
    buf->oracle_diag_frontier_offender_solve_rate = -1.0f;
    buf->oracle_diag_frontier_offender_gap_sum = 0.0f;
    buf->oracle_diag_frontier_offender_gap_max = 0.0f;
    buf->oracle_diag_frontier_offender_len = 0;
    buf->oracle_diag_frontier_offender_abs_mean = 0.0f;
    buf->oracle_diag_frontier_offender_pos_mean = 0.0f;
    buf->oracle_diag_frontier_offender_neg_mean = 0.0f;
    buf->oracle_diag_frontier_offender_pos_frac = 0.0f;
    buf->oracle_diag_frontier_offender_neg_frac = 0.0f;
    buf->oracle_diag_frontier_offender_max_pos = 0.0f;
    buf->oracle_diag_frontier_offender_min_neg = 0.0f;
    buf->oracle_diag_frontier_offender_max_abs = 0.0f;
    buf->oracle_diag_frontier_offender_max_raw = 0.0f;
    buf->oracle_diag_frontier_offender_solve_offset = -1;
    buf->oracle_diag_frontier_offender_max_offset = -1;
    buf->oracle_diag_frontier_offender_reward_dist = -1;
    buf->oracle_diag_frontier_offender_maps_solved = -1;
    buf->oracle_diag_frontier_offender_sequence_pos = -1;
    buf->oracle_diag_frontier_offender_puzzle_tick = -1;
    buf->oracle_diag_frontier_offender_value = 0.0f;
    buf->oracle_diag_frontier_offender_next_value = 0.0f;
    buf->oracle_diag_frontier_offender_reward = 0.0f;
    buf->oracle_diag_frontier_offender_terminal = 0.0f;
    buf->oracle_diag_frontier_offender_delta = 0.0f;
    buf->oracle_diag_frontier_offender_gae = 0.0f;
    buf->oracle_diag_min_priority = 0.0f;
    buf->oracle_diag_min_raw = 0.0f;
    buf->oracle_diag_raw_solve_offset = -1;
    buf->oracle_diag_raw_max_offset = -1;
    buf->oracle_diag_adv_solve_offset = -1;
    buf->oracle_diag_adv_max_offset = -1;
    buf->oracle_diag_raw_max_adv = 0.0f;
    buf->oracle_diag_adv_max_adv = 0.0f;
    buf->oracle_diag_raw_max_return = 0.0f;
    buf->oracle_diag_adv_max_return = 0.0f;
#ifdef BOXOBAN_LEVEL_LOGS
    buf->oracle_diag_raw_max_solve_level = -1;
    buf->oracle_diag_raw_max_solve_rate = -1.0f;
    buf->oracle_diag_raw_max_solve_episodes = 0.0f;
    buf->oracle_diag_raw_max_agent_step = -1;
    buf->oracle_diag_adv_max_solve_level = -1;
    buf->oracle_diag_adv_max_solve_rate = -1.0f;
    buf->oracle_diag_adv_max_solve_episodes = 0.0f;
    buf->oracle_diag_adv_max_agent_step = -1;
    for (int i = 0; i < PUFFER_CURRICULUM_SOLVE_RATE_BINS; i++) {
        buf->oracle_diag_vferr_count[i] = 0;
        buf->oracle_diag_vferr_sum[i] = 0.0f;
        buf->oracle_diag_vferr_max[i] = 0.0f;
        buf->oracle_diag_frontier_offender_bucket_count[i] = 0;
        buf->oracle_diag_frontier_offender_bucket_max[i] = 0.0f;
    }
#endif
    for (int i = 0; i < PUFFER_CURRICULUM_TOP_DIAG; i++) {
        buf->oracle_diag_candidate_top_raw[i] = 0.0f;
        buf->oracle_diag_candidate_top_priority[i] = 0.0f;
        buf->oracle_diag_candidate_top_solve_rate[i] = -1.0f;
        buf->oracle_diag_candidate_top_solve_level[i] = -1;
    }
    buf->oracle_diag_raw_prev_reward_dist = -1;
    buf->oracle_diag_raw_next_reward_dist = -1;
    buf->oracle_diag_adv_prev_reward_dist = -1;
    buf->oracle_diag_adv_next_reward_dist = -1;
    buf->oracle_diag_raw_max_maps_solved = -1;
    buf->oracle_diag_raw_max_sequence_pos = -1;
    buf->oracle_diag_raw_max_puzzle_tick = -1;
    buf->oracle_diag_adv_max_maps_solved = -1;
    buf->oracle_diag_adv_max_sequence_pos = -1;
    buf->oracle_diag_adv_max_puzzle_tick = -1;
    for (int i = 0; i < 5; i++) {
        buf->oracle_diag_raw_pre_adv[i] = 0.0f;
        buf->oracle_diag_adv_pre_adv[i] = 0.0f;
    }
}

static inline void curriculum_oracle_update_retention_threshold_diag(
        StateBuffer* buf) {
    if (buf->oracle_segment_count <= 0
            || buf->oracle_segment_count < buf->oracle_max_segments) {
        buf->oracle_diag_min_priority = 0.0f;
        buf->oracle_diag_min_raw = 0.0f;
        return;
    }

    int min_idx = 0;
    for (int i = 1; i < buf->oracle_segment_count; i++) {
        if (curriculum_oracle_segment_better(buf,
                buf->oracle_segment_score[min_idx],
                buf->oracle_segment_priority[min_idx],
                buf->oracle_segment_len[min_idx], 1, -1,
                buf->oracle_segment_score[i],
                buf->oracle_segment_priority[i],
                buf->oracle_segment_len[i], 1, -1)) {
            min_idx = i;
        }
    }
    buf->oracle_diag_min_priority = buf->oracle_segment_priority[min_idx];
    buf->oracle_diag_min_raw = buf->oracle_segment_terminal_return[min_idx];
}

static inline int curriculum_oracle_min_segment(StateBuffer* buf);

static inline void curriculum_oracle_materialize_candidate_histories(
        StateBuffer* buf) {
    curriculum_oracle_clear_candidates(buf);

    int cap = buf->oracle_hist_capacity;
    if (cap <= 0 || buf->num_fresh_envs <= 0) {
        return;
    }

    int max_envs = buf->num_fresh_envs;
    if (max_envs > buf->num_envs) {
        max_envs = buf->num_envs;
    }

    int min_idx = curriculum_oracle_min_segment(buf);
    for (int env_idx = 0; env_idx < max_envs; env_idx++) {
        int count = buf->oracle_hist_count[env_idx];
        if (count <= 0 || count > cap) {
            continue;
        }

        float score = curriculum_oracle_env_history_score(buf, env_idx);
        float priority = curriculum_oracle_compute_env_history_priority(
            buf, env_idx, count);
        if (score <= 0.0f && priority <= 0.0f) {
            continue;
        }

        int full = buf->oracle_hist_from_entry[env_idx];
        int last_t = count - 1;
        int can_enter = buf->oracle_segment_count < buf->oracle_max_segments
            || min_idx < 0
            || curriculum_oracle_segment_better(buf,
                score, priority, count, full, last_t,
                buf->oracle_segment_score[min_idx],
                buf->oracle_segment_priority[min_idx],
                buf->oracle_segment_len[min_idx], 1, -1);
        if (!can_enter) {
            continue;
        }

        int solve_level = (int)floorf(score + 1e-5f);
        float solve_rate = -1.0f;
#ifdef BOXOBAN_LEVEL_LOGS
        solve_rate = curriculum_oracle_env_history_max_source_solve_rate(
            buf, env_idx, count);
#endif
        __sync_fetch_and_add(&buf->oracle_fresh_solve_candidates, 1);
        curriculum_oracle_save_candidate_history(
            buf, env_idx, score, solve_level, solve_rate);
    }
}

static inline void curriculum_oracle_select_pending_candidate(
        StateBuffer* buf, long long agent_step) {
    curriculum_oracle_clear_pending(buf);
    curriculum_oracle_reset_candidate_diagnostics(buf);
    curriculum_oracle_update_retention_threshold_diag(buf);
    curriculum_oracle_materialize_candidate_histories(buf);

    int best_env = -1;
    float best_score = 0.0f;
    float best_priority = 0.0f;
    int best_count = 0;
    int best_full = 0;
    int best_last_t = -1;
    int raw_best_env = -1;
    int raw_best_count = 0;
    int adv_best_env = -1;
    int adv_best_count = 0;
    int offender_best_env = -1;
    int offender_best_count = 0;
    float offender_best_score = 0.0f;
    float offender_best_priority = 0.0f;
    int cap = buf->oracle_hist_capacity;
    int max_envs = buf->num_fresh_envs;
    if (max_envs > buf->num_envs) {
        max_envs = buf->num_envs;
    }

    for (int env_idx = 0; env_idx < max_envs; env_idx++) {
        int count = buf->oracle_candidate_count[env_idx];
        if (count <= 0 || count > cap) {
            continue;
        }
        int base = env_idx * cap;
        float priority = curriculum_oracle_compute_history_priority(
            buf, buf->oracle_candidate_source + base, count, env_idx);
#ifdef BOXOBAN_LEVEL_LOGS
        curriculum_oracle_record_vferr_bucket(
            buf,
            curriculum_oracle_max_source_solve_rate(
                buf, buf->oracle_candidate_source + base, count),
            priority);
#endif
        float score = buf->oracle_candidate_terminal_return[env_idx];
        curriculum_oracle_diag_insert_top_candidate(
            buf, score, priority, buf->oracle_candidate_solve_level[env_idx],
            buf->oracle_candidate_solve_rate[env_idx]);
        int full = buf->oracle_candidate_full[env_idx];
        int last_t = count - 1;
        buf->oracle_diag_candidate_count++;
        if (score > buf->oracle_diag_candidate_raw_max + 1e-6f
                || (fabsf(score - buf->oracle_diag_candidate_raw_max) <= 1e-6f
                    && priority > buf->oracle_diag_candidate_raw_max_priority)) {
            buf->oracle_diag_candidate_raw_max = score;
            buf->oracle_diag_candidate_raw_max_priority = priority;
            buf->oracle_diag_candidate_raw_max_solve_rate =
                buf->oracle_candidate_solve_rate[env_idx];
            buf->oracle_diag_candidate_raw_max_solve_level =
                buf->oracle_candidate_solve_level[env_idx];
            raw_best_env = env_idx;
            raw_best_count = count;
        }
        if (priority > buf->oracle_diag_candidate_priority_max + 1e-6f
                || (fabsf(priority - buf->oracle_diag_candidate_priority_max) <= 1e-6f
                    && score > buf->oracle_diag_candidate_priority_max_raw)) {
            buf->oracle_diag_candidate_priority_max = priority;
            buf->oracle_diag_candidate_priority_max_raw = score;
            buf->oracle_diag_candidate_priority_max_solve_rate =
                buf->oracle_candidate_solve_rate[env_idx];
            buf->oracle_diag_candidate_priority_max_solve_level =
                buf->oracle_candidate_solve_level[env_idx];
            adv_best_env = env_idx;
            adv_best_count = count;
        }
        if (curriculum_oracle_segment_better(buf,
                score, priority, count, full, last_t,
                best_score, best_priority, best_count, best_full, best_last_t)) {
            best_env = env_idx;
            best_score = score;
            best_priority = priority;
            best_count = count;
            best_full = full;
            best_last_t = last_t;
        }
    }

    if (raw_best_env >= 0 && raw_best_count > 0) {
        int raw_base = raw_best_env * cap;
        float raw_score = buf->oracle_candidate_terminal_return[raw_best_env];
        float raw_priority = curriculum_oracle_compute_history_priority(
            buf, buf->oracle_candidate_source + raw_base,
            raw_best_count, raw_best_env);
        int raw_full = buf->oracle_candidate_full[raw_best_env];
        int raw_last_t = raw_best_count - 1;
        buf->oracle_diag_raw_selected = raw_best_env == best_env;
        int min_idx = curriculum_oracle_min_segment(buf);
        buf->oracle_diag_raw_retained =
            buf->oracle_segment_count < buf->oracle_max_segments
            || min_idx < 0
            || curriculum_oracle_segment_better(buf,
                raw_score, raw_priority, raw_best_count, raw_full, raw_last_t,
                buf->oracle_segment_score[min_idx],
                buf->oracle_segment_priority[min_idx],
                buf->oracle_segment_len[min_idx], 1, -1);

        for (int env_idx = 0; env_idx < max_envs; env_idx++) {
            int count = buf->oracle_candidate_count[env_idx];
            if (count <= 0 || count > cap) {
                continue;
            }
            int base = env_idx * cap;
            float score = buf->oracle_candidate_terminal_return[env_idx];
            float priority = curriculum_oracle_compute_history_priority(
                buf, buf->oracle_candidate_source + base, count, env_idx);
            if (score >= raw_score - 1e-6f
                    || priority <= raw_priority + 1e-6f) {
                continue;
            }

            float gap = raw_score - score;
            buf->oracle_diag_frontier_offenders++;
            buf->oracle_diag_frontier_offender_priority_sum += priority;
            buf->oracle_diag_frontier_offender_gap_sum += gap;
            if (gap > buf->oracle_diag_frontier_offender_gap_max) {
                buf->oracle_diag_frontier_offender_gap_max = gap;
            }
            if (priority > buf->oracle_diag_frontier_offender_priority_max) {
                buf->oracle_diag_frontier_offender_priority_max = priority;
                buf->oracle_diag_frontier_offender_raw = score;
                buf->oracle_diag_frontier_offender_solve_level =
                    buf->oracle_candidate_solve_level[env_idx];
                buf->oracle_diag_frontier_offender_solve_rate =
                    buf->oracle_candidate_solve_rate[env_idx];
                offender_best_env = env_idx;
                offender_best_count = count;
                offender_best_score = score;
                offender_best_priority = priority;
                curriculum_oracle_profile_frontier_offender(
                    buf, buf->oracle_candidate_states + base,
                    buf->oracle_candidate_source + base, count, score);
            }
#ifdef BOXOBAN_LEVEL_LOGS
            int bucket = curriculum_oracle_solve_rate_bucket(
                buf->oracle_candidate_solve_rate[env_idx]);
            if (bucket >= 0 && bucket < PUFFER_CURRICULUM_SOLVE_RATE_BINS) {
                buf->oracle_diag_frontier_offender_bucket_count[bucket]++;
                if (priority
                        > buf->oracle_diag_frontier_offender_bucket_max[bucket]) {
                    buf->oracle_diag_frontier_offender_bucket_max[bucket] =
                        priority;
                }
            }
#endif
        }
        curriculum_oracle_maybe_trace_candidate_compare(
            buf, agent_step,
            raw_best_env, raw_best_count, raw_score, raw_priority,
            offender_best_env, offender_best_count, offender_best_score,
            offender_best_priority);
    }

    if (best_env < 0 || best_count <= 0) {
        if (raw_best_env >= 0 && raw_best_count > 0) {
            int raw_base = raw_best_env * cap;
            curriculum_oracle_profile_solve_window(
                buf, buf->oracle_candidate_states + raw_base,
                buf->oracle_candidate_source + raw_base, raw_best_count,
                buf->oracle_candidate_terminal_return[raw_best_env],
                &buf->oracle_diag_raw_solve_offset,
                &buf->oracle_diag_raw_max_offset,
                &buf->oracle_diag_raw_max_return,
                &buf->oracle_diag_raw_prev_reward_dist,
                &buf->oracle_diag_raw_next_reward_dist,
                &buf->oracle_diag_raw_max_maps_solved,
                &buf->oracle_diag_raw_max_sequence_pos,
                &buf->oracle_diag_raw_max_puzzle_tick,
                buf->oracle_diag_raw_pre_adv);
            curriculum_oracle_profile_max_source_diag(
                buf, buf->oracle_candidate_source + raw_base,
                raw_best_count, buf->oracle_diag_raw_max_offset, 0);
        }
        if (adv_best_env >= 0 && adv_best_count > 0) {
            int adv_base = adv_best_env * cap;
            curriculum_oracle_profile_solve_window(
                buf, buf->oracle_candidate_states + adv_base,
                buf->oracle_candidate_source + adv_base, adv_best_count,
                buf->oracle_candidate_terminal_return[adv_best_env],
                &buf->oracle_diag_adv_solve_offset,
                &buf->oracle_diag_adv_max_offset,
                &buf->oracle_diag_adv_max_return,
                &buf->oracle_diag_adv_prev_reward_dist,
                &buf->oracle_diag_adv_next_reward_dist,
                &buf->oracle_diag_adv_max_maps_solved,
                &buf->oracle_diag_adv_max_sequence_pos,
                &buf->oracle_diag_adv_max_puzzle_tick,
                buf->oracle_diag_adv_pre_adv);
            curriculum_oracle_profile_max_source_diag(
                buf, buf->oracle_candidate_source + adv_base,
                adv_best_count, buf->oracle_diag_adv_max_offset, 1);
        }
        return;
    }

    if (raw_best_env >= 0 && raw_best_count > 0) {
        int raw_base = raw_best_env * cap;
        curriculum_oracle_profile_solve_window(
            buf, buf->oracle_candidate_states + raw_base,
            buf->oracle_candidate_source + raw_base, raw_best_count,
            buf->oracle_candidate_terminal_return[raw_best_env],
            &buf->oracle_diag_raw_solve_offset,
            &buf->oracle_diag_raw_max_offset,
            &buf->oracle_diag_raw_max_return,
            &buf->oracle_diag_raw_prev_reward_dist,
            &buf->oracle_diag_raw_next_reward_dist,
            &buf->oracle_diag_raw_max_maps_solved,
            &buf->oracle_diag_raw_max_sequence_pos,
            &buf->oracle_diag_raw_max_puzzle_tick,
            buf->oracle_diag_raw_pre_adv);
        curriculum_oracle_profile_max_source_diag(
            buf, buf->oracle_candidate_source + raw_base,
            raw_best_count, buf->oracle_diag_raw_max_offset, 0);
    }
    if (adv_best_env >= 0 && adv_best_count > 0) {
        int adv_base = adv_best_env * cap;
        curriculum_oracle_profile_solve_window(
            buf, buf->oracle_candidate_states + adv_base,
            buf->oracle_candidate_source + adv_base, adv_best_count,
            buf->oracle_candidate_terminal_return[adv_best_env],
            &buf->oracle_diag_adv_solve_offset,
            &buf->oracle_diag_adv_max_offset,
            &buf->oracle_diag_adv_max_return,
            &buf->oracle_diag_adv_prev_reward_dist,
            &buf->oracle_diag_adv_next_reward_dist,
            &buf->oracle_diag_adv_max_maps_solved,
            &buf->oracle_diag_adv_max_sequence_pos,
            &buf->oracle_diag_adv_max_puzzle_tick,
            buf->oracle_diag_adv_pre_adv);
        curriculum_oracle_profile_max_source_diag(
            buf, buf->oracle_candidate_source + adv_base,
            adv_best_count, buf->oracle_diag_adv_max_offset, 1);
    }

    int base = best_env * cap;
    for (int i = 0; i < best_count; i++) {
        buf->oracle_pending_states[i] = buf->oracle_candidate_states[base + i];
        buf->oracle_pending_source[i] = buf->oracle_candidate_source[base + i];
    }
    buf->oracle_pending_level = buf->oracle_candidate_level[best_env];
    buf->oracle_pending_score = best_score;
    buf->oracle_pending_terminal_return = best_score;
    buf->oracle_pending_priority = best_priority;
    buf->oracle_pending_solve_rate =
        buf->oracle_candidate_solve_rate[best_env];
    buf->oracle_pending_count = best_count;
    buf->oracle_pending_first_t = 0;
    buf->oracle_pending_last_t = best_last_t;
    buf->oracle_pending_full = best_full;
    buf->oracle_pending_env_idx = best_env;
    buf->oracle_pending_needs_priority = 0;
    buf->oracle_diag_selected_raw = best_score;
    buf->oracle_diag_selected_priority = best_priority;
    buf->oracle_diag_selected_solve_rate =
        buf->oracle_candidate_solve_rate[best_env];
    buf->oracle_diag_selected_solve_level =
        buf->oracle_candidate_solve_level[best_env];
}

static inline void curriculum_oracle_decay_segment_priorities(
        StateBuffer* buf, float decay) {
    if (decay < 0.0f || isnan(decay) || isinf(decay)) {
        decay = 0.0f;
    }
    if (decay > 1.0f) {
        decay = 1.0f;
    }
    for (int seg = 0; seg < buf->oracle_segment_count; seg++) {
        float priority = buf->oracle_segment_priority[seg] * decay;
        buf->oracle_segment_priority[seg] = clean_state_priority(priority);
    }
}

static inline void curriculum_oracle_update_pending_priority(StateBuffer* buf) {
    if (!buf->oracle_pending_needs_priority) {
        return;
    }
    buf->oracle_pending_priority = curriculum_oracle_compute_history_priority(
        buf, buf->oracle_pending_source, buf->oracle_pending_count,
        buf->oracle_pending_env_idx);
    buf->oracle_pending_needs_priority = 0;
}

static inline void curriculum_oracle_update_segment_priority_from_slots(
        StateBuffer* buf, int seg) {
    int seg_cap = buf->oracle_segment_capacity;
    if (seg_cap <= 0 || seg < 0 || seg >= buf->oracle_segment_count) {
        return;
    }

    int base = seg * seg_cap;
    int seg_len = buf->oracle_segment_len[seg];
    if (seg_len <= 0 || seg_len > seg_cap) {
        return;
    }
    int end = base + seg_len;
    if (end > buf->size) {
        end = buf->size;
    }

    double positive_sum = 0.0;
    int priority_count = end - base;
    for (int slot = base; slot < end; slot++) {
        float slot_priority = buf->oracle_state_priority[slot];
        positive_sum += (double)clean_state_priority(slot_priority);
    }
    buf->oracle_segment_priority[seg] =
        priority_count > 0 ? clean_state_priority((float)positive_sum) : 0.0f;
}

static inline void curriculum_oracle_refresh_saved_diagnostics(
        StateBuffer* buf, long agent_step) {
    int best_seg = -1;
    for (int seg = 0; seg < buf->oracle_segment_count; seg++) {
        if (curriculum_oracle_segment_better(buf,
                buf->oracle_segment_score[seg],
                buf->oracle_segment_priority[seg],
                buf->oracle_segment_len[seg], 1, -1,
                best_seg >= 0 ? buf->oracle_segment_score[best_seg] : 0.0f,
                best_seg >= 0 ? buf->oracle_segment_priority[best_seg] : 0.0f,
                best_seg >= 0 ? buf->oracle_segment_len[best_seg] : 0,
                1, -1)) {
            best_seg = seg;
        }
    }
    if (best_seg < 0) {
        return;
    }

    int seg_cap = buf->oracle_segment_capacity;
    int base = best_seg * seg_cap;
    int seg_len = buf->oracle_segment_len[best_seg];
    int end = base + seg_len;
    if (seg_cap <= 0 || seg_len <= 0 || base < 0 || base >= buf->size) {
        return;
    }
    if (end > buf->size) {
        end = buf->size;
    }
    int last = end - 1;

    float old_score = buf->oracle_saved_score;
    float score = buf->oracle_segment_score[best_seg];
    int changed = fabsf(score - old_score) > 1e-6f;

    buf->oracle_saved_level = -1;
    buf->oracle_saved_score = score;
    buf->oracle_saved_priority = buf->oracle_segment_priority[best_seg];
    buf->oracle_saved_terminal_return =
        buf->oracle_segment_terminal_return[best_seg];
    buf->oracle_saved_pick_t = last - base;
    buf->oracle_saved_first_t = 0;
    buf->oracle_saved_last_t = last - base;
    buf->oracle_saved_full = (end - base) == buf->oracle_segment_capacity;
    if (changed) {
        buf->oracle_saved_mastered = 0;
        if (agent_step >= 0) {
            buf->oracle_saved_agent_step = agent_step;
        }
    }
}

static inline void curriculum_oracle_recompute_segment_priorities(StateBuffer* buf) {
    int seg_cap = buf->oracle_segment_capacity;
    int seg_count = buf->oracle_segment_count;
    if (seg_cap <= 0 || seg_count <= 0 || buf->num_cl_envs <= 0) {
        return;
    }

    int num_envs = buf->num_envs;
    for (int i = 0; i < buf->num_cl_envs; i++) {
        int env_idx = buf->num_fresh_envs + i;
        if (env_idx < 0 || env_idx >= num_envs) {
            continue;
        }
        int sampled_slot = buf->env_state_inds_host[env_idx];
        if (sampled_slot < 0 || sampled_slot >= buf->size) {
            continue;
        }
        int seg = sampled_slot / seg_cap;
        if (seg < 0 || seg >= seg_count) {
            continue;
        }
        int end = (seg + 1) * seg_cap;
        if (end > buf->size) {
            end = buf->size;
        }
        for (int slot = sampled_slot; slot < end; slot++) {
            buf->oracle_state_priority[slot] = 0.0f;
        }
    }

    for (int i = 0; i < buf->num_cl_envs; i++) {
        int env_idx = buf->num_fresh_envs + i;
        if (env_idx < 0 || env_idx >= num_envs) {
            continue;
        }
        int sampled_slot = buf->env_state_inds_host[env_idx];
        if (sampled_slot < 0 || sampled_slot >= buf->size) {
            continue;
        }
        int seg = sampled_slot / seg_cap;
        if (seg < 0 || seg >= seg_count) {
            continue;
        }

        for (int c = 0; c < buf->num_checkpoints; c++) {
            int idx = c * num_envs + env_idx;
            float checkpoint_priority = to_float(buf->env_scores_host[idx]);
            if (isnan(checkpoint_priority) || isinf(checkpoint_priority)) {
                continue;
            }
            int slot = sampled_slot + c * buf->checkpoint_interval;
            if (slot < sampled_slot || slot >= (seg + 1) * seg_cap
                    || slot >= buf->size) {
                continue;
            }
            float checkpoint_abs = fabsf(checkpoint_priority);
            float slot_abs = fabsf(buf->oracle_state_priority[slot]);
            if (checkpoint_abs > slot_abs) {
                buf->oracle_state_priority[slot] = checkpoint_priority;
            }
        }
    }

    for (int seg = 0; seg < seg_count; seg++) {
        curriculum_oracle_update_segment_priority_from_slots(buf, seg);
    }

    curriculum_oracle_refresh_saved_diagnostics(buf, -1);
}

static inline float curriculum_oracle_history_discounted_return(
        StateBuffer* buf, int env_idx, int count, float terminal_score,
        float gamma) {
    int hist_cap = buf->oracle_hist_capacity;
    if (env_idx < 0 || env_idx >= buf->num_envs
            || hist_cap <= 0 || count <= 0 || count > hist_cap) {
        return 0.0f;
    }

    int hist_base = env_idx * hist_cap;
    int hist_start = (buf->oracle_hist_write[env_idx] + hist_cap - count) % hist_cap;
    PufferState* hist_states = buf->oracle_hist_states + hist_base;
    gamma = fminf(1.0f, fmaxf(0.0f, gamma));

    float ret = 0.0f;
    for (int i = count - 1; i >= 0; i--) {
        int src = (hist_start + i) % hist_cap;
        PufferState* state = &hist_states[src];
        if (i == count - 1) {
            ret = terminal_score - state->episode_return;
            if (isnan(ret) || isinf(ret)) {
                ret = 0.0f;
            }
        } else {
            int next_src = (hist_start + i + 1) % hist_cap;
            PufferState* next_state = &hist_states[next_src];
            float reward = next_state->episode_return - state->episode_return;
            if (isnan(reward) || isinf(reward)) {
                reward = 0.0f;
            }
            ret = reward + gamma * ret;
        }
    }
    if (isnan(ret) || isinf(ret)) {
        return 0.0f;
    }
    return ret;
}

static inline int curriculum_oracle_replace_cl_tail(
        StateBuffer* buf, int env_idx, float terminal_score, float gamma) {
    int sampled_slot = buf->oracle_cl_start_slot[env_idx];
    int seg_cap = buf->oracle_segment_capacity;
    int count = buf->oracle_hist_count[env_idx];
    if (sampled_slot < 0 || sampled_slot >= buf->size
            || seg_cap <= 0 || count <= 0) {
        return 0;
    }

    int seg = sampled_slot / seg_cap;
    int offset = sampled_slot - seg * seg_cap;
    if (seg < 0 || seg >= buf->oracle_segment_count
            || offset < 0 || offset >= seg_cap
            || offset >= buf->oracle_segment_len[seg]
            || offset + count > seg_cap) {
        return 0;
    }

    int hist_cap = buf->oracle_hist_capacity;
    int hist_base = env_idx * hist_cap;
    int hist_start = (buf->oracle_hist_write[env_idx] + hist_cap - count) % hist_cap;
    PufferState* hist_states = buf->oracle_hist_states + hist_base;
    gamma = fminf(1.0f, fmaxf(0.0f, gamma));

    float old_return = buf->oracle_state_return[sampled_slot];

    float candidate_return = curriculum_oracle_history_discounted_return(
        buf, env_idx, count, terminal_score, gamma);
    if (isnan(candidate_return) || isinf(candidate_return)
            || candidate_return <= old_return + 1e-6f) {
        return 0;
    }

    int base = seg * seg_cap;
    float tail_return = 0.0f;
    for (int i = count - 1; i >= 0; i--) {
        int src = (hist_start + i) % hist_cap;
        PufferState* state = &hist_states[src];
        if (i == count - 1) {
            tail_return = terminal_score - state->episode_return;
            if (isnan(tail_return) || isinf(tail_return)) {
                tail_return = 0.0f;
            }
        } else {
            int next_src = (hist_start + i + 1) % hist_cap;
            PufferState* next_state = &hist_states[next_src];
            float reward = next_state->episode_return - state->episode_return;
            if (isnan(reward) || isinf(reward)) {
                reward = 0.0f;
            }
            tail_return = reward + gamma * tail_return;
        }
        int slot = base + offset + i;
        int source = buf->oracle_hist_source[hist_base + src];
        buf->states[slot] = *state;
        buf->oracle_state_return[slot] = tail_return;
        curriculum_oracle_set_slot_priority_from_source(buf, slot, source, 1);
        buf->priorities[slot] = 0.0f;
        buf->priorities_host[slot] = from_float(0.0f);
        buf->state_outcomes[slot] = curriculum_diag_bucket(
            (int)floorf(terminal_score));
    }

    int old_len = buf->oracle_segment_len[seg];
    int new_len = offset + count;
    if (new_len < old_len) {
        for (int slot = base + new_len; slot < base + old_len; slot++) {
            buf->oracle_state_return[slot] = 0.0f;
            buf->oracle_state_priority[slot] = 0.0f;
            buf->oracle_state_update_source[slot] = -1;
            buf->priorities[slot] = 0.0f;
            buf->priorities_host[slot] = from_float(0.0f);
        }
    }
    buf->oracle_segment_len[seg] = new_len;
    if (terminal_score > buf->oracle_segment_terminal_return[seg]) {
        buf->oracle_segment_terminal_return[seg] = terminal_score;
    }
    curriculum_oracle_update_segment_priority_from_slots(buf, seg);
    curriculum_oracle_refresh_saved_diagnostics(buf, -1);
    return 1;
}

static inline void curriculum_oracle_queue_head_priority_update(
        StateBuffer* buf, int env_idx, int seg, int count) {
    int seg_cap = buf->oracle_segment_capacity;
    int hist_cap = buf->oracle_hist_capacity;
    if (seg_cap <= 0 || hist_cap <= 0 || count <= 0
            || env_idx < 0 || env_idx >= buf->num_envs
            || seg < 0 || seg >= buf->oracle_segment_count) {
        return;
    }

    int seg_len = buf->oracle_segment_len[seg];
    if (seg_len <= 0 || seg_len > seg_cap) {
        return;
    }

    int usable = count;
    if (usable > seg_len) {
        usable = seg_len;
    }
    if (usable <= 0) {
        return;
    }

    int base = seg * seg_cap;
    int hist_base = env_idx * hist_cap;
    int hist_start = (buf->oracle_hist_write[env_idx] + hist_cap - count) % hist_cap;
    for (int c = 0; c < usable; c++) {
        int src = (hist_start + c) % hist_cap;
        int source = buf->oracle_hist_source[hist_base + src];
        int slot = base + c;
        if (source >= 0 && source < buf->candidate_capacity) {
            buf->oracle_state_update_source[slot] = source;
        } else {
            curriculum_oracle_set_slot_priority_from_source(
                buf, slot, source, 0);
        }
    }
}

static inline void curriculum_oracle_apply_deferred_priority_updates(StateBuffer* buf) {
    int updated = 0;
    for (int slot = 0; slot < buf->size; slot++) {
        int source = buf->oracle_state_update_source[slot];
        if (source < 0) {
            continue;
        }

        float adv = curriculum_oracle_source_raw_advantage(buf, source);
        if (isnan(adv) || isinf(adv)) {
            adv = 0.0f;
        }
        buf->oracle_state_priority[slot] = adv;
        buf->oracle_state_update_source[slot] = -1;
        updated = 1;
    }

    if (updated) {
        for (int seg = 0; seg < buf->oracle_segment_count; seg++) {
            curriculum_oracle_update_segment_priority_from_slots(buf, seg);
        }
        curriculum_oracle_refresh_saved_diagnostics(buf, -1);
    }
}

static inline int curriculum_oracle_capture_state(
        StateBuffer* buf, int env_idx, Env* env,
        float reward, float terminal, int source, float gamma,
        const float* fresh_solve_rates) {
    (void)fresh_solve_rates;
    if (buf->oracle_hist_capacity <= 0 || env == NULL) {
        return 0;
    }

    const PufferState* state = &env->state;
    const PufferState* last = curriculum_oracle_last_history_state(buf, env_idx);
    if (last == NULL) {
        buf->oracle_env_discounted_return[env_idx] = 0.0f;
        curriculum_oracle_reset_history(buf, env_idx, state, source);
        return 0;
    }

    gamma = fminf(1.0f, fmaxf(0.0f, gamma));
    if (terminal <= 0.5f && state->tick < last->tick) {
        buf->oracle_env_discounted_return[env_idx] = 0.0f;
    }
    float step_discount = powf(gamma, fmaxf(0.0f, (float)last->tick));
    float discounted_return = buf->oracle_env_discounted_return[env_idx]
        + step_discount * reward;
    if (isnan(discounted_return) || isinf(discounted_return)) {
        discounted_return = buf->oracle_env_discounted_return[env_idx];
    }
    buf->oracle_env_discounted_return[env_idx] = discounted_return;

    if (terminal > 0.5f) {
        float terminal_return = fmaxf(state->episode_return,
            last->episode_return + reward);
        if (env_idx >= buf->num_fresh_envs) {
            int resampled = 0;
            __sync_fetch_and_add(&buf->oracle_cl_terminals, 1);
            if (reward > 0.5f) {
                __sync_fetch_and_add(&buf->oracle_cl_solve_terminals, 1);
                int bucket = buf->oracle_cl_start_bucket[env_idx];
                if (bucket >= 0 && bucket < PUFFER_CURRICULUM_CL_BINS) {
                    __sync_fetch_and_add(&buf->oracle_cl_successes[bucket], 1);
                }
                __sync_fetch_and_add(&buf->oracle_window_successes, 1);
                if (buf->oracle_cl_start_slot[env_idx] >= 0
                        && buf->oracle_segment_capacity > 0
                        && (buf->oracle_cl_start_slot[env_idx]
                            % buf->oracle_segment_capacity) == 0) {
                    __sync_fetch_and_add(&buf->oracle_start_successes, 1);
                }
            }
#ifdef _OPENMP
#pragma omp critical(puffer_oracle_tail)
#endif
            {
                if (buf->oracle_cl_head_sample[env_idx]) {
                    int sampled_slot = buf->oracle_cl_start_slot[env_idx];
                    int seg_cap = buf->oracle_segment_capacity;
                    int count = buf->oracle_hist_count[env_idx];
                    if (sampled_slot >= 0 && sampled_slot < buf->size
                            && seg_cap > 0 && count > 0
                            && (sampled_slot % seg_cap) == 0) {
                        int seg = sampled_slot / seg_cap;
                        if (seg >= 0 && seg < buf->oracle_segment_count
                                && buf->oracle_segment_len[seg] > 0) {
                            float old_return = buf->oracle_state_return[sampled_slot];
                            if (isnan(old_return) || isinf(old_return)) {
                                old_return = 0.0f;
                            }
                            float candidate_return =
                                curriculum_oracle_history_discounted_return(
                                    buf, env_idx, count, terminal_return, gamma);
                            if (!isnan(candidate_return) && !isinf(candidate_return)
                                    && candidate_return + 1e-6f >= old_return) {
                                curriculum_oracle_queue_head_priority_update(
                                    buf, env_idx, seg, count);
                            }
                        }
                    }
                }
                int replaced = curriculum_oracle_replace_cl_tail(
                    buf, env_idx, terminal_return, gamma);
                if (replaced) {
                    __sync_fetch_and_add(&buf->oracle_cl_tail_replacements, 1);
                }
                buf->oracle_cl_start_slot[env_idx] = -1;
                buf->oracle_cl_start_bucket[env_idx] = -1;
                buf->oracle_cl_head_sample[env_idx] = 0;
                resampled = curriculum_oracle_resample_cl_env(
                    buf, env, env_idx, source);
            }
            if (resampled) {
                return 1;
            }
        }
        int reset_episode = state->tick <= last->tick;
        curriculum_oracle_reset_history(buf, env_idx, state, source);
        if (reset_episode) {
            buf->oracle_env_discounted_return[env_idx] = 0.0f;
        }
        return 0;
    }

    curriculum_oracle_push_history(buf, env_idx, state, source);
    return 0;
}

static inline void curriculum_oracle_clear_segments(StateBuffer* buf) {
#ifdef BOXOBAN_LEVEL_LOGS
    for (int i = 0; i < buf->oracle_segment_count; i++) {
        curriculum_oracle_record_segment_ejection(buf, i);
    }
#endif
    buf->size = 0;
    buf->oracle_segment_count = 0;
    buf->min_priority = 0.0f;
    memset(buf->oracle_state_priority, 0,
        (size_t)buf->capacity * sizeof(float));
    memset(buf->oracle_state_return, 0,
        (size_t)buf->capacity * sizeof(float));
    memset(buf->oracle_state_update_source, -1,
        (size_t)buf->capacity * sizeof(int));
    for (int i = 0; i < buf->oracle_max_segments; i++) {
        buf->oracle_segment_priority[i] = 0.0f;
        buf->oracle_segment_score[i] = 0.0f;
        buf->oracle_segment_terminal_return[i] = 0.0f;
        buf->oracle_segment_level[i] = -1;
        buf->oracle_segment_len[i] = 0;
        buf->oracle_segment_agent_step[i] = -1;
        buf->oracle_segment_sample_count[i] = 0;
#ifdef BOXOBAN_LEVEL_LOGS
        buf->oracle_segment_solve_rate[i] = -1.0f;
#endif
    }
}

static inline int curriculum_oracle_min_segment(StateBuffer* buf) {
    if (buf->oracle_segment_count <= 0) {
        return -1;
    }
    int min_idx = 0;
    for (int i = 1; i < buf->oracle_segment_count; i++) {
        if (curriculum_oracle_segment_better(buf,
                buf->oracle_segment_score[min_idx],
                buf->oracle_segment_priority[min_idx],
                buf->oracle_segment_len[min_idx], 1, -1,
                buf->oracle_segment_score[i],
                buf->oracle_segment_priority[i],
                buf->oracle_segment_len[i], 1, -1)) {
            min_idx = i;
        }
    }
    return min_idx;
}

static inline int curriculum_oracle_find_segment_slot(
        StateBuffer* buf, float score, float priority,
        int count, int full, int last_t) {
    if (buf->oracle_max_segments <= 0 || buf->oracle_segment_capacity <= 0) {
        return -1;
    }
    if (buf->oracle_segment_count < buf->oracle_max_segments) {
        int slot = buf->oracle_segment_count;
        buf->oracle_segment_count++;
        return slot;
    }

    int min_idx = curriculum_oracle_min_segment(buf);
    if (min_idx < 0 || !curriculum_oracle_segment_better(buf,
            score, priority, count, full, last_t,
            buf->oracle_segment_score[min_idx],
            buf->oracle_segment_priority[min_idx],
            buf->oracle_segment_len[min_idx], 1, -1)) {
        return -1;
    }
#ifdef BOXOBAN_LEVEL_LOGS
    curriculum_oracle_record_segment_ejection(buf, min_idx);
#endif
    return min_idx;
}

static inline int curriculum_oracle_store_pending_segment(
        StateBuffer* buf, long agent_step, float gamma) {
    int count = buf->oracle_pending_count;
    int seg_cap = buf->oracle_segment_capacity;
    if (count <= 0 || seg_cap <= 0 || count > seg_cap) {
        return 0;
    }

    int segment_slot = curriculum_oracle_find_segment_slot(
        buf, buf->oracle_pending_score, buf->oracle_pending_priority,
        count, buf->oracle_pending_full, buf->oracle_pending_last_t);
    if (segment_slot < 0) {
        return 0;
    }

    int base = segment_slot * seg_cap;
    gamma = fminf(1.0f, fmaxf(0.0f, gamma));
    for (int i = 0; i < seg_cap; i++) {
        int slot = base + i;
        if (i < count) {
            buf->states[slot] = buf->oracle_pending_states[i];
            buf->oracle_state_return[slot] = curriculum_oracle_discounted_return(
                buf->oracle_pending_states, count, i,
                buf->oracle_pending_terminal_return, gamma);
            int source = buf->oracle_pending_source[i];
            curriculum_oracle_set_slot_priority_from_source(buf, slot, source, 0);
            buf->priorities[slot] = 0.0f;
            buf->priorities_host[slot] = from_float(0.0f);
        } else {
            buf->states[slot] = buf->oracle_pending_states[count - 1];
            buf->oracle_state_return[slot] = 0.0f;
            buf->oracle_state_priority[slot] = 0.0f;
            buf->oracle_state_update_source[slot] = -1;
            buf->priorities[slot] = 0.0f;
            buf->priorities_host[slot] = from_float(0.0f);
        }
        buf->heap[slot] = slot;
        buf->heap_pos[slot] = slot;
        buf->state_outcomes[slot] = curriculum_diag_bucket(
            (int)floorf(buf->oracle_pending_terminal_return));
    }
    buf->oracle_segment_priority[segment_slot] = buf->oracle_pending_priority;
    buf->oracle_segment_score[segment_slot] = buf->oracle_pending_score;
    buf->oracle_segment_terminal_return[segment_slot] =
        buf->oracle_pending_terminal_return;
    buf->oracle_segment_level[segment_slot] = buf->oracle_pending_level;
    buf->oracle_segment_len[segment_slot] = count;
    buf->oracle_segment_agent_step[segment_slot] = agent_step;
    buf->oracle_segment_sample_count[segment_slot] = 0;
#ifdef BOXOBAN_LEVEL_LOGS
    buf->oracle_segment_solve_rate[segment_slot] =
        buf->oracle_pending_solve_rate;
#endif
    buf->size = buf->oracle_segment_count * seg_cap;
    __sync_fetch_and_add(&buf->oracle_fresh_segments_stored, 1);
    curriculum_oracle_refresh_saved_diagnostics(buf, agent_step);
    curriculum_oracle_refresh_sample_priorities(buf);
    state_heap_refresh_min(buf);
    curriculum_diag_recount_retained(buf);
    return 1;
}

static inline int curriculum_try_oracle_solve_segment(PuffeRL* pufferl) {
    StateBuffer* buf = &pufferl->state_buf;
    if (buf->oracle_pending_count <= 0) {
        return 0;
    }

    long agent_step = pufferl->global_step * (long)pufferl->hypers.world_size;
    int stored = curriculum_oracle_store_pending_segment(
        buf, agent_step, pufferl->hypers.gamma);
    buf->oracle_diag_selected_stored = stored;
    curriculum_oracle_clear_pending(buf);
    return stored;
}

#ifdef BOXOBAN_LEVEL_LOGS
static inline void curriculum_oracle_fresh_solve_rates(
        PuffeRL* pufferl, float* solve_rates, float* episodes_out) {
    for (int level = 0; level < BOXOBAN_LEVEL_LOGS; level++) {
        solve_rates[level] = -1.0f;
    }
    if (episodes_out != NULL) {
        *episodes_out = 0.0f;
    }

    StateBuffer* buf = &pufferl->state_buf;
    StaticVec* vec = pufferl->vec;
    int end_env = buf->num_fresh_envs;
    if (end_env > vec->size) {
        end_env = vec->size;
    }
    if (end_env <= 0) {
        return;
    }

    double episodes = 0.0;
    double successes[BOXOBAN_LEVEL_LOGS] = {};
    for (int env_idx = 0; env_idx < end_env; env_idx++) {
        Env* env = &vec->envs[env_idx];
        if (env->log.n <= 0.0f) {
            continue;
        }
        episodes += (double)env->log.n;
        for (int level = 0; level < BOXOBAN_LEVEL_LOGS; level++) {
            successes[level] += (double)env->log.level_solved[level];
        }
    }

    if (episodes <= 0.0) {
        return;
    }
    if (episodes_out != NULL) {
        *episodes_out = (float)episodes;
    }
    for (int level = 0; level < BOXOBAN_LEVEL_LOGS; level++) {
        solve_rates[level] = (float)(successes[level] / episodes);
    }
}

static inline void curriculum_oracle_record_fresh_terminal_diag(
        StateBuffer* buf, const Env* env) {
    if (env == NULL || env->terminals[0] <= 0.5f) {
        return;
    }

    __sync_fetch_and_add(&buf->oracle_diag_fresh_episodes, 1LL);
    int solved = env->state.episode_maps_solved;
    for (int level = 0; level < BOXOBAN_LEVEL_LOGS; level++) {
        if (solved >= level + 1) {
            __sync_fetch_and_add(
                &buf->oracle_diag_fresh_level_solved[level], 1LL);
        }
    }
}

static inline void curriculum_oracle_snapshot_source_diag(
        StateBuffer* buf, int source, const PufferState* state,
        long long agent_step, const float* fresh_solve_rates,
        float fresh_solve_rate_episodes) {
    if (source < 0 || source >= buf->candidate_capacity || state == NULL) {
        return;
    }

    int solve_level = state->episode_maps_solved + 1;
    buf->oracle_source_solve_level[source] = solve_level;
    buf->oracle_source_agent_step[source] = agent_step;
    buf->oracle_source_solve_episodes[source] = fresh_solve_rate_episodes;
    if (solve_level < 1 || solve_level > BOXOBAN_LEVEL_LOGS
            || fresh_solve_rates == NULL
            || fresh_solve_rate_episodes <= 0.0f) {
        buf->oracle_source_solve_rate[source] = -1.0f;
        return;
    }

    buf->oracle_source_solve_rate[source] =
        fresh_solve_rates[solve_level - 1];
}
#endif
#endif

static inline void curriculum_copy_env_obs_to_gpu(StaticVec* vec,
        StateBuffer* buf, int env_idx) {
    if (vec == NULL || buf == NULL || !vec->gpu) {
        return;
    }

    int agent_start = env_idx * buf->agents_per_env;
    size_t obs_size = (size_t)get_obs_size();
    size_t obs_bytes = (size_t)buf->agents_per_env * obs_size
        * get_obs_elem_size();
    cudaMemcpy(vec->gpu_observations.data + (long)agent_start * obs_size,
        vec->observations.data + (long)agent_start * obs_size,
        obs_bytes, cudaMemcpyHostToDevice);

    if (vec->action_mask_size > 0) {
        size_t mask_size = (size_t)vec->action_mask_size;
        size_t mask_bytes = (size_t)buf->agents_per_env * mask_size
            * sizeof(unsigned char);
        cudaMemcpy(vec->gpu_action_mask + (long)agent_start * mask_size,
            vec->action_mask + (long)agent_start * mask_size,
            mask_bytes, cudaMemcpyHostToDevice);
    }
}

static inline void capture_curriculum_checkpoint(PuffeRL* pufferl, int buffer_idx, int t) {
    StateBuffer* buf = &pufferl->state_buf;
    int interval = buf->checkpoint_interval;
    if ((t % interval) != 0) {
        return;
    }
    int checkpoint_idx = t / interval;

    StaticVec* vec = pufferl->vec;
    int env_start = vec->buffer_env_starts[buffer_idx];
    int env_end = env_start + vec->buffer_env_counts[buffer_idx];

    Env* envs = vec->envs;
    PufferState* dst = buf->candidate_states + checkpoint_idx * buf->num_envs;
#ifdef BOXOBAN_LEVEL_LOGS
    float fresh_solve_rates[BOXOBAN_LEVEL_LOGS];
    float fresh_solve_rate_episodes = 0.0f;
    int have_fresh_solve_rates = 0;
#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
    long long agent_step = pufferl->global_step * (long long)pufferl->hypers.world_size;
    if (pufferl->hypers.frontier_explore) {
        curriculum_oracle_fresh_solve_rates(
            pufferl, fresh_solve_rates, &fresh_solve_rate_episodes);
        have_fresh_solve_rates = fresh_solve_rate_episodes > 0.0f;
        for (int env_idx = env_start; env_idx < env_end; env_idx++) {
            if (env_idx < buf->num_fresh_envs) {
                curriculum_oracle_record_fresh_terminal_diag(
                    buf, &envs[env_idx]);
            }
        }
    }
#endif
#endif
    for (int env_idx = env_start; env_idx < env_end; env_idx++) {
        int source = checkpoint_idx * buf->num_envs + env_idx;
        dst[env_idx] = envs[env_idx].state;
#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
        if (pufferl->hypers.frontier_explore) {
            Env* env = &envs[env_idx];
#ifdef BOXOBAN_LEVEL_LOGS
            curriculum_oracle_snapshot_source_diag(
                buf, source, &env->state, agent_step,
                have_fresh_solve_rates ? fresh_solve_rates : NULL,
                fresh_solve_rate_episodes);
#endif
            int resampled = curriculum_oracle_capture_state(
                buf, env_idx, env, env->rewards[0], env->terminals[0],
                source, pufferl->hypers.gamma,
#ifdef BOXOBAN_LEVEL_LOGS
                have_fresh_solve_rates ? fresh_solve_rates : NULL
#else
                NULL
#endif
                );
            if (resampled) {
                curriculum_copy_env_obs_to_gpu(vec, buf, env_idx);
            }
        }
#endif
    }
}

void curriculum_rollout_begin(PuffeRL* pufferl) {
    HypersT* h = &pufferl->hypers;
    StateBuffer* buf = &pufferl->state_buf;
    StaticVec* vec = pufferl->vec;
    cudaStream_t stream = pufferl->default_stream;
    int total_envs = vec->size;
    int agents_per_env = buf->agents_per_env;
    int total_agents = total_envs * agents_per_env;
#ifdef BOXOBAN_LEVEL_LOGS
    if (h->frontier_random && pufferl->frontier_random_row_mask_host != NULL) {
        memset(pufferl->frontier_random_row_mask_host, 0,
            (size_t)total_agents * sizeof(int));
        cudaMemsetAsync(pufferl->frontier_random_row_mask.data, 0,
            (size_t)total_agents * sizeof(int), stream);
    }
#endif
    int total_epochs = h->total_timesteps / (h->total_agents * h->horizon);
    float progress = total_epochs > 0 ? (float)pufferl->epoch / (float)total_epochs : 1.0f;
    progress = fminf(1.0f, fmaxf(0.0f, progress));
    float current_cl_frac = h->cl_frac;
    if (h->anneal_cl) {
        current_cl_frac *= 1.0f - progress;
    }
    int num_cl_envs;
    if (h->frontier_explore) {
        num_cl_envs = (buf->size == 0) ? 0 :
            clamp_int((int)(current_cl_frac * (float)total_envs), 0, total_envs);
    } else {
        num_cl_envs = (buf->size == 0 || buf->size < h->warmup_states) ? 0 :
            clamp_int((int)(current_cl_frac * (float)total_envs), 0, total_envs);
    }
    int num_fresh_envs = total_envs - num_cl_envs;

    buf->num_cl_envs = num_cl_envs;
    buf->num_fresh_envs = num_fresh_envs;
    vec->log_env_limit = (num_cl_envs > 0) ? num_fresh_envs : 0;
    if (h->frontier_explore && num_fresh_envs > 0) {
        int monitor_envs = num_fresh_envs / 2;
        if (monitor_envs < 1) {
            monitor_envs = 1;
        }
        vec->log_env_limit = monitor_envs;
    }

    int fresh_agents = num_fresh_envs * agents_per_env;
    fill_precision_kernel<<<grid_size(total_agents), BLOCK_SIZE, 0, stream>>>(
        buf->importance.data, from_float(1.0f), total_agents);

    if (num_cl_envs > 0) {
        int* state_inds = buf->env_state_inds_host + num_fresh_envs;
#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
        if (h->frontier_explore) {
            int first_slot = curriculum_oracle_uniform_slot_in_best_segment(
                buf, (unsigned int)pufferl->epoch ^ 0x517cc1b7U);
            if (first_slot < 0) {
                num_cl_envs = 0;
                buf->num_cl_envs = 0;
                buf->num_fresh_envs = total_envs;
                vec->log_env_limit = 0;
            } else {
                for (int i = 0; i < num_cl_envs; i++) {
                    int env_idx = num_fresh_envs + i;
                    unsigned int salt = ((unsigned int)pufferl->epoch * 1000003U)
                        ^ ((unsigned int)env_idx * 9176U)
                        ^ (unsigned int)i;
                    int sampled_slot =
                        curriculum_oracle_uniform_slot_in_best_segment(
                            buf, salt);
                    state_inds[i] = sampled_slot >= 0 ? sampled_slot : first_slot;
                }
            }
        } else
#endif
        {
            compute_prio_abs<<<grid_size(buf->size), BLOCK_SIZE, 0, stream>>>(
                buf->advantages.data, buf->prio_bufs.prio_weights.data,
                1.0f, 0.0f, buf->size, 1);
            long* rng_offset = pufferl->rng_offset_puf.data + h->num_buffers + 1;
            sample_prio_indices(&buf->prio_bufs, buf->size, num_cl_envs,
                pufferl->seed, rng_offset, NULL, buf->importance.data,
                fresh_agents, agents_per_env, h->explore_beta, stream);
            cudaMemcpyAsync(state_inds, buf->prio_bufs.idx.data,
                num_cl_envs * sizeof(int), cudaMemcpyDeviceToHost, stream);
            cudaStreamSynchronize(stream);
        }

        Env* envs = vec->envs;
        for (int i = 0; i < num_cl_envs; i++) {
            int sampled_slot = state_inds[i];
            int env_idx = num_fresh_envs + i;
#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
            if (h->frontier_explore) {
                buf->oracle_cl_head_sample[env_idx] = 0;
                buf->oracle_head_update_seg[env_idx] = -1;
                buf->oracle_head_update_count[env_idx] = 0;
                int seg_cap = buf->oracle_segment_capacity;
                if ((i & 1) == 0 && seg_cap > 0
                        && sampled_slot >= 0 && sampled_slot < buf->size) {
                    int seg = sampled_slot / seg_cap;
                    if (seg >= 0 && seg < buf->oracle_segment_count
                            && buf->oracle_segment_len[seg] > 0) {
                        sampled_slot = seg * seg_cap;
                        state_inds[i] = sampled_slot;
                        buf->oracle_cl_head_sample[env_idx] = 1;
                    }
                }
            }
#endif
#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
            if (h->frontier_explore && buf->oracle_segment_capacity > 0
                    && sampled_slot >= 0 && sampled_slot < buf->size) {
                int sampled_seg = sampled_slot / buf->oracle_segment_capacity;
                int sampled_offset =
                    sampled_slot - sampled_seg * buf->oracle_segment_capacity;
                if (sampled_seg >= 0
                        && sampled_seg < buf->oracle_segment_count
                        && sampled_offset >= 0
                        && sampled_offset < buf->oracle_segment_len[sampled_seg]) {
                    buf->oracle_segment_sample_count[sampled_seg]++;
                }
            }
#endif
            const PufferState* sampled_state = &buf->states[sampled_slot];
            Env* env = &envs[env_idx];
            env->state = *sampled_state;
            puffer_state_refresh(env);
#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
            if (h->frontier_explore) {
                buf->oracle_cl_start_bucket[env_idx] = -1;
                buf->oracle_cl_start_slot[env_idx] = -1;
                int seg_cap = buf->oracle_segment_capacity;
                if (seg_cap > 0 && sampled_slot >= 0 && sampled_slot < buf->size) {
                    int seg = sampled_slot / seg_cap;
                    int seg_end = seg * seg_cap + buf->oracle_segment_len[seg];
                    if (seg_end > buf->size) {
                        seg_end = buf->size;
                    }
                    int remaining = seg_end - sampled_slot - 1;
                    int bucket = curriculum_oracle_cl_bucket(remaining);
                    buf->oracle_cl_start_bucket[env_idx] = bucket;
                    buf->oracle_cl_start_slot[env_idx] = sampled_slot;
                    buf->oracle_cl_attempts[bucket]++;
                    if ((sampled_slot % seg_cap) == 0) {
                        buf->oracle_start_attempts++;
                    }
                    buf->oracle_window_attempts++;
                }
                curriculum_oracle_reset_history(buf, env_idx, sampled_state,
                    curriculum_oracle_state_slot_source(sampled_slot));
            }
#endif
        }
        int cl_agent_start = num_fresh_envs * agents_per_env;
        int cl_agents = num_cl_envs * agents_per_env;
        memset(vec->rewards + cl_agent_start, 0, (size_t)cl_agents * sizeof(float));
        memset(vec->terminals + cl_agent_start, 0, (size_t)cl_agents * sizeof(float));
        if (vec->gpu) {
            cudaMemsetAsync(vec->gpu_rewards + cl_agent_start, 0,
                (size_t)cl_agents * sizeof(float), stream);
            cudaMemsetAsync(vec->gpu_terminals + cl_agent_start, 0,
                (size_t)cl_agents * sizeof(float), stream);
            cudaStreamSynchronize(stream);
        }
        cudaMemcpy(vec->gpu_observations.data, vec->observations.data,
            (size_t)vec->total_agents * get_obs_size() * get_obs_elem_size(),
            cudaMemcpyHostToDevice);
        if (vec->action_mask_size > 0) {
            cudaMemcpy(vec->gpu_action_mask, vec->action_mask,
                (size_t)vec->total_agents * vec->action_mask_size * sizeof(unsigned char),
                cudaMemcpyHostToDevice);
        }
    }
}

void curriculum_update_advantages(PuffeRL* pufferl, PrecisionTensor* advantages,
        PrecisionTensor* entropy, cudaStream_t stream) {
    StateBuffer* buf = &pufferl->state_buf;
    if (pufferl->hypers.frontier_explore) {
#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
        int horizon = advantages->shape[1];
        int num_envs = buf->num_envs;
        int checkpoint_rows = buf->num_checkpoints * num_envs;
        compute_curriculum_checkpoint_scores<<<grid_size(checkpoint_rows), BLOCK_SIZE, 0, stream>>>(
            buf->env_scores.data, buf->env_debug.data,
            advantages->data, entropy->data,
            pufferl->train_rollouts.values.data,
            pufferl->train_rollouts.rewards.data,
            pufferl->train_rollouts.terminals.data,
            num_envs,
            buf->num_fresh_envs, buf->num_cl_envs, buf->num_checkpoints,
            buf->checkpoint_interval, buf->agents_per_env,
            pufferl->hypers.gamma, horizon);
        cudaMemcpyAsync(buf->env_scores_host, buf->env_scores.data,
            (size_t)checkpoint_rows * sizeof(precision_t),
            cudaMemcpyDeviceToHost, stream);
        cudaMemcpyAsync(buf->env_debug_host, buf->env_debug.data,
            (size_t)checkpoint_rows * PUFFER_CURRICULUM_DEBUG_FIELDS
                * sizeof(precision_t),
            cudaMemcpyDeviceToHost, stream);
        cudaStreamSynchronize(stream);

        curriculum_oracle_apply_deferred_priority_updates(buf);
        long long agent_step =
            pufferl->global_step * (long long)pufferl->hypers.world_size;
        curriculum_oracle_select_pending_candidate(buf, agent_step);
        curriculum_try_oracle_solve_segment(pufferl);
        curriculum_oracle_clear_candidates(buf);
        curriculum_oracle_refresh_saved_diagnostics(buf, -1);
        curriculum_oracle_maybe_expand_window(buf);
        curriculum_oracle_refresh_sample_priorities(buf);
        if (buf->size > 0) {
            cudaMemcpyAsync(buf->advantages.data, buf->priorities_host,
                (size_t)buf->size * sizeof(precision_t), cudaMemcpyHostToDevice, stream);
        }
#endif
        return;
    }

    int horizon = advantages->shape[1];
    int num_fresh_envs = buf->num_fresh_envs;
    int num_cl_envs = buf->num_cl_envs;
    int num_envs = buf->num_envs;
    int agents_per_env = buf->agents_per_env;

    int checkpoint_rows = buf->num_checkpoints * num_envs;
    int score_rows = checkpoint_rows + num_cl_envs;
    compute_curriculum_checkpoint_scores<<<grid_size(score_rows), BLOCK_SIZE, 0, stream>>>(
        buf->env_scores.data, NULL, advantages->data, entropy->data,
        NULL, NULL, NULL, num_envs,
        num_fresh_envs, num_cl_envs, buf->num_checkpoints,
        buf->checkpoint_interval, agents_per_env, pufferl->hypers.gamma,
        horizon);
    int cl_score_offset = checkpoint_rows;
    cudaMemcpyAsync(buf->env_scores_host, buf->env_scores.data,
        (size_t)score_rows * sizeof(precision_t), cudaMemcpyDeviceToHost, stream);
    cudaStreamSynchronize(stream);

#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
    curriculum_backfill_checkpoint_scores(buf);
#endif

    for (int i = 0; i < num_cl_envs; i++) {
        int env_idx = num_fresh_envs + i;
        int slot = buf->env_state_inds_host[env_idx];
        float priority = to_float(buf->env_scores_host[cl_score_offset + i]);
#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
        float relabeled_priority = to_float(buf->env_scores_host[env_idx]);
        if (relabeled_priority > priority) {
            priority = relabeled_priority;
        }
        int outcome = curriculum_diag_candidate_outcome(buf, 0, env_idx);
        if (outcome > buf->state_outcomes[slot]) {
            buf->state_outcomes[slot] = outcome;
        }
#endif
        state_heap_update_slot(buf, slot, priority, pufferl->hypers.explore_decay);
    }

#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
    for (int c = 0; c < buf->num_checkpoints; c++) {
        int candidate_offset = c * buf->num_envs;
        for (int i = 0; i < num_envs; i++) {
            int candidate_idx = candidate_offset + i;
            float priority = clean_state_priority(to_float(buf->env_scores_host[candidate_idx]));
            if (buf->size >= buf->capacity && priority <= buf->min_priority) {
                continue;
            }
            PufferState* candidate_state = &buf->candidate_states[candidate_idx];
            int outcome = curriculum_diag_candidate_outcome(buf, c, i);
            state_heap_insert(buf, candidate_state, priority, outcome);
        }
    }
#else
    for (int c = 0; c < buf->num_checkpoints; c++) {
        int candidate_offset = c * buf->num_envs;
        for (int i = 0; i < num_envs; i++) {
            int candidate_idx = candidate_offset + i;
            float priority = clean_state_priority(to_float(buf->env_scores_host[candidate_idx]));
            if (buf->size >= buf->capacity && priority <= buf->min_priority) {
                continue;
            }
            PufferState* candidate_state = &buf->candidate_states[candidate_idx];
            state_heap_insert(buf, candidate_state, priority);
        }
    }
#endif

#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
    curriculum_diag_recount_retained(buf);
#endif
    cudaMemcpyAsync(buf->advantages.data, buf->priorities_host,
        (size_t)buf->size * sizeof(precision_t), cudaMemcpyHostToDevice, stream);
}

#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
static inline void curriculum_log_diagnostics(PuffeRL* pufferl, Dict* out) {
    StateBuffer* buf = &pufferl->state_buf;
    if (pufferl->hypers.frontier_explore) {
        double max_raw = 0.0;
        double max_raw_n = 0.0;
        for (int seg = 0; seg < buf->oracle_segment_count; seg++) {
            double raw = (double)buf->oracle_segment_terminal_return[seg];
            if (raw > max_raw + 1e-6) {
                max_raw = raw;
                max_raw_n = 1.0;
            } else if (fabs(raw - max_raw) <= 1e-6 && raw > 0.0) {
                max_raw_n += 1.0;
            }
        }
        dict_set(out, "seg", (double)buf->oracle_saved_score);
        dict_set(out, "seg_raw", (double)buf->oracle_saved_terminal_return);
        dict_set(out, "trace_n", (double)buf->oracle_trace_dump_count);
        dict_set(out, "buf_raw", max_raw);
        dict_set(out, "buf_raw_n", max_raw_n);
        int retained_top[PUFFER_CURRICULUM_TOP_DIAG];
        for (int i = 0; i < PUFFER_CURRICULUM_TOP_DIAG; i++) {
            retained_top[i] = -1;
        }
        for (int seg = 0; seg < buf->oracle_segment_count; seg++) {
            float priority = buf->oracle_segment_priority[seg];
            float raw = buf->oracle_segment_terminal_return[seg];
            int dst = -1;
            for (int i = 0; i < PUFFER_CURRICULUM_TOP_DIAG; i++) {
                int cur = retained_top[i];
                if (cur < 0
                        || priority > buf->oracle_segment_priority[cur] + 1e-6f
                        || (fabsf(priority - buf->oracle_segment_priority[cur]) <= 1e-6f
                            && raw > buf->oracle_segment_terminal_return[cur])) {
                    dst = i;
                    break;
                }
            }
            if (dst < 0) {
                continue;
            }
            for (int i = PUFFER_CURRICULUM_TOP_DIAG - 1; i > dst; i--) {
                retained_top[i] = retained_top[i - 1];
            }
            retained_top[dst] = seg;
        }
        long agent_step = pufferl->global_step * (long)pufferl->hypers.world_size;
        for (int i = 0; i < PUFFER_CURRICULUM_TOP_DIAG; i++) {
            int seg = retained_top[i];
            dict_set(out, curriculum_diag_top_key("rt", i, 0), seg >= 0
                ? (double)buf->oracle_segment_terminal_return[seg] : 0.0);
            dict_set(out, curriculum_diag_top_key("rt", i, 1), seg >= 0
                ? (double)buf->oracle_segment_priority[seg] : 0.0);
            dict_set(out, curriculum_diag_top_key("rt", i, 2), seg >= 0
                ? (double)buf->oracle_segment_len[seg] : 0.0);
            dict_set(out, curriculum_diag_top_key("rt", i, 3),
                seg >= 0 && buf->oracle_segment_agent_step[seg] >= 0
                ? (double)buf->oracle_segment_agent_step[seg] / 1000000.0 : -1.0);
            dict_set(out, curriculum_diag_top_key("rt", i, 4),
                seg >= 0 && buf->oracle_segment_agent_step[seg] >= 0
                ? (double)(agent_step - buf->oracle_segment_agent_step[seg])
                    / 1000000.0 : -1.0);
        }
        dict_set(out, "seg_n", (double)buf->size);
        dict_set(out, "seg_pick", (double)buf->oracle_saved_pick_t);
        dict_set(out, "seg_t0", (double)buf->oracle_saved_first_t);
        dict_set(out, "seg_t1", (double)buf->oracle_saved_last_t);
        dict_set(out, "seg_full", (double)buf->oracle_saved_full);
        dict_set(out, "seg_s", (double)buf->oracle_sample_start);
        dict_set(out, "seg_w", (double)(buf->size - buf->oracle_sample_start));
        dict_set(out, "seg_a", (double)buf->oracle_window_attempts);
        dict_set(out, "seg_sr", buf->oracle_window_attempts > 0
            ? (double)buf->oracle_window_successes /
                (double)buf->oracle_window_attempts : 0.0);
        dict_set(out, "seg_go", (double)curriculum_oracle_current_promotable(buf));
        dict_set(out, "pend", (double)buf->oracle_pending_score);
        dict_set(out, "pend_raw", (double)buf->oracle_pending_terminal_return);
        dict_set(out, "pend_n", (double)buf->oracle_pending_count);
        dict_set(out, "pend_t1", (double)buf->oracle_pending_last_t);
        dict_set(out, "ca_n", (double)buf->oracle_diag_candidate_count);
        dict_set(out, "ca_raw", (double)buf->oracle_diag_candidate_raw_max);
        dict_set(out, "ca_rawa",
            (double)buf->oracle_diag_candidate_raw_max_priority);
        dict_set(out, "ca_rlv",
            (double)buf->oracle_diag_candidate_raw_max_solve_level);
        dict_set(out, "ca_rsr",
            (double)buf->oracle_diag_candidate_raw_max_solve_rate);
        dict_set(out, "ca_adv",
            (double)buf->oracle_diag_candidate_priority_max);
        dict_set(out, "ca_advr",
            (double)buf->oracle_diag_candidate_priority_max_raw);
        dict_set(out, "ca_alv",
            (double)buf->oracle_diag_candidate_priority_max_solve_level);
        dict_set(out, "ca_asr",
            (double)buf->oracle_diag_candidate_priority_max_solve_rate);
        dict_set(out, "ca_sel", (double)buf->oracle_diag_selected_raw);
        dict_set(out, "ca_sela", (double)buf->oracle_diag_selected_priority);
        dict_set(out, "ca_slv", (double)buf->oracle_diag_selected_solve_level);
        dict_set(out, "ca_ssr", (double)buf->oracle_diag_selected_solve_rate);
        dict_set(out, "ca_in", (double)buf->oracle_diag_selected_stored);
        dict_set(out, "ca_min", (double)buf->oracle_diag_min_priority);
        dict_set(out, "ca_minr", (double)buf->oracle_diag_min_raw);
        dict_set(out, "ca_rawsel", (double)buf->oracle_diag_raw_selected);
        dict_set(out, "ca_rawin", (double)buf->oracle_diag_raw_retained);
        dict_set(out, "fo_n",
            (double)buf->oracle_diag_frontier_offenders);
        dict_set(out, "fo_a", buf->oracle_diag_frontier_offenders > 0
            ? (double)buf->oracle_diag_frontier_offender_priority_sum
                / (double)buf->oracle_diag_frontier_offenders : 0.0);
        dict_set(out, "fo_x",
            (double)buf->oracle_diag_frontier_offender_priority_max);
        dict_set(out, "fo_r",
            (double)buf->oracle_diag_frontier_offender_raw);
        dict_set(out, "fo_l",
            (double)buf->oracle_diag_frontier_offender_solve_level);
        dict_set(out, "fo_s",
            (double)buf->oracle_diag_frontier_offender_solve_rate);
        dict_set(out, "fo_g", buf->oracle_diag_frontier_offenders > 0
            ? (double)buf->oracle_diag_frontier_offender_gap_sum
                / (double)buf->oracle_diag_frontier_offenders : 0.0);
        dict_set(out, "fo_gx",
            (double)buf->oracle_diag_frontier_offender_gap_max);
        dict_set(out, "fo_len",
            (double)buf->oracle_diag_frontier_offender_len);
        dict_set(out, "fo_am",
            (double)buf->oracle_diag_frontier_offender_abs_mean);
        dict_set(out, "fo_pm",
            (double)buf->oracle_diag_frontier_offender_pos_mean);
        dict_set(out, "fo_nm",
            (double)buf->oracle_diag_frontier_offender_neg_mean);
        dict_set(out, "fo_pf",
            (double)buf->oracle_diag_frontier_offender_pos_frac);
        dict_set(out, "fo_nf",
            (double)buf->oracle_diag_frontier_offender_neg_frac);
        dict_set(out, "fo_mx",
            (double)buf->oracle_diag_frontier_offender_max_pos);
        dict_set(out, "fo_mn",
            (double)buf->oracle_diag_frontier_offender_min_neg);
        dict_set(out, "fo_ma",
            (double)buf->oracle_diag_frontier_offender_max_abs);
        dict_set(out, "fo_mraw",
            (double)buf->oracle_diag_frontier_offender_max_raw);
        dict_set(out, "fo_so",
            (double)buf->oracle_diag_frontier_offender_solve_offset);
        dict_set(out, "fo_mo",
            (double)buf->oracle_diag_frontier_offender_max_offset);
        dict_set(out, "fo_rd",
            (double)buf->oracle_diag_frontier_offender_reward_dist);
        dict_set(out, "fo_v",
            (double)buf->oracle_diag_frontier_offender_value);
        dict_set(out, "fo_vn",
            (double)buf->oracle_diag_frontier_offender_next_value);
        dict_set(out, "fo_rw",
            (double)buf->oracle_diag_frontier_offender_reward);
        dict_set(out, "fo_dn",
            (double)buf->oracle_diag_frontier_offender_terminal);
        dict_set(out, "fo_de",
            (double)buf->oracle_diag_frontier_offender_delta);
        dict_set(out, "fo_ga",
            (double)buf->oracle_diag_frontier_offender_gae);
        dict_set(out, "fo_lvl",
            (double)buf->oracle_diag_frontier_offender_maps_solved);
        dict_set(out, "fo_seq",
            (double)buf->oracle_diag_frontier_offender_sequence_pos);
        dict_set(out, "fo_pt",
            (double)buf->oracle_diag_frontier_offender_puzzle_tick);
#ifdef BOXOBAN_LEVEL_LOGS
        for (int i = 0; i < PUFFER_CURRICULUM_SOLVE_RATE_BINS; i++) {
            dict_set(out, curriculum_diag_frontier_offender_key(i, 0),
                (double)buf->oracle_diag_frontier_offender_bucket_count[i]);
            dict_set(out, curriculum_diag_frontier_offender_key(i, 1),
                (double)buf->oracle_diag_frontier_offender_bucket_max[i]);
        }
#endif
        for (int i = 0; i < PUFFER_CURRICULUM_TOP_DIAG; i++) {
            dict_set(out, curriculum_diag_top_key("ct", i, 0),
                (double)buf->oracle_diag_candidate_top_raw[i]);
            dict_set(out, curriculum_diag_top_key("ct", i, 1),
                (double)buf->oracle_diag_candidate_top_priority[i]);
            dict_set(out, curriculum_diag_top_key("ct", i, 2),
                (double)buf->oracle_diag_candidate_top_solve_level[i]);
            dict_set(out, curriculum_diag_top_key("ct", i, 3),
                (double)buf->oracle_diag_candidate_top_solve_rate[i]);
        }
        dict_set(out, "ra_so", (double)buf->oracle_diag_raw_solve_offset);
        dict_set(out, "ra_mo", (double)buf->oracle_diag_raw_max_offset);
        dict_set(out, "ra_md",
            (buf->oracle_diag_raw_solve_offset >= 0
                && buf->oracle_diag_raw_max_offset >= 0)
            ? (double)(buf->oracle_diag_raw_solve_offset
                - buf->oracle_diag_raw_max_offset)
            : -1.0);
        dict_set(out, "ra_a1", (double)buf->oracle_diag_raw_pre_adv[0]);
        dict_set(out, "ra_a4", (double)buf->oracle_diag_raw_pre_adv[1]);
        dict_set(out, "ra_a8", (double)buf->oracle_diag_raw_pre_adv[2]);
        dict_set(out, "ra_a16", (double)buf->oracle_diag_raw_pre_adv[3]);
        dict_set(out, "ra_a32", (double)buf->oracle_diag_raw_pre_adv[4]);
        dict_set(out, "ra_ma", (double)buf->oracle_diag_raw_max_adv);
        dict_set(out, "ra_mret", (double)buf->oracle_diag_raw_max_return);
#ifdef BOXOBAN_LEVEL_LOGS
        dict_set(out, "ra_mlv", (double)buf->oracle_diag_raw_max_solve_level);
        dict_set(out, "ra_msr", (double)buf->oracle_diag_raw_max_solve_rate);
        dict_set(out, "ra_mn", (double)buf->oracle_diag_raw_max_solve_episodes);
        dict_set(out, "ra_ms", buf->oracle_diag_raw_max_agent_step >= 0
            ? (double)buf->oracle_diag_raw_max_agent_step / 1000000.0 : -1.0);
#endif
        dict_set(out, "ra_prd", (double)buf->oracle_diag_raw_prev_reward_dist);
        dict_set(out, "ra_nrd", (double)buf->oracle_diag_raw_next_reward_dist);
        dict_set(out, "ra_lvl", (double)buf->oracle_diag_raw_max_maps_solved);
        dict_set(out, "ra_seq", (double)buf->oracle_diag_raw_max_sequence_pos);
        dict_set(out, "ra_pt", (double)buf->oracle_diag_raw_max_puzzle_tick);
        dict_set(out, "aa_so", (double)buf->oracle_diag_adv_solve_offset);
        dict_set(out, "aa_mo", (double)buf->oracle_diag_adv_max_offset);
        dict_set(out, "aa_md",
            (buf->oracle_diag_adv_solve_offset >= 0
                && buf->oracle_diag_adv_max_offset >= 0)
            ? (double)(buf->oracle_diag_adv_solve_offset
                - buf->oracle_diag_adv_max_offset)
            : -1.0);
        dict_set(out, "aa_a1", (double)buf->oracle_diag_adv_pre_adv[0]);
        dict_set(out, "aa_a4", (double)buf->oracle_diag_adv_pre_adv[1]);
        dict_set(out, "aa_a8", (double)buf->oracle_diag_adv_pre_adv[2]);
        dict_set(out, "aa_a16", (double)buf->oracle_diag_adv_pre_adv[3]);
        dict_set(out, "aa_a32", (double)buf->oracle_diag_adv_pre_adv[4]);
        dict_set(out, "aa_ma", (double)buf->oracle_diag_adv_max_adv);
        dict_set(out, "aa_mret", (double)buf->oracle_diag_adv_max_return);
#ifdef BOXOBAN_LEVEL_LOGS
        dict_set(out, "aa_mlv", (double)buf->oracle_diag_adv_max_solve_level);
        dict_set(out, "aa_msr", (double)buf->oracle_diag_adv_max_solve_rate);
        dict_set(out, "aa_mn", (double)buf->oracle_diag_adv_max_solve_episodes);
        dict_set(out, "aa_ms", buf->oracle_diag_adv_max_agent_step >= 0
            ? (double)buf->oracle_diag_adv_max_agent_step / 1000000.0 : -1.0);
#endif
        dict_set(out, "aa_prd", (double)buf->oracle_diag_adv_prev_reward_dist);
        dict_set(out, "aa_nrd", (double)buf->oracle_diag_adv_next_reward_dist);
        dict_set(out, "aa_lvl", (double)buf->oracle_diag_adv_max_maps_solved);
        dict_set(out, "aa_seq", (double)buf->oracle_diag_adv_max_sequence_pos);
        dict_set(out, "aa_pt", (double)buf->oracle_diag_adv_max_puzzle_tick);
#ifdef BOXOBAN_LEVEL_LOGS
        for (int i = 0; i < PUFFER_CURRICULUM_SOLVE_RATE_BINS; i++) {
            dict_set(out, curriculum_diag_vferr_key(i, 0),
                (double)buf->oracle_diag_vferr_count[i]);
            dict_set(out, curriculum_diag_vferr_key(i, 1),
                buf->oracle_diag_vferr_count[i] > 0
                ? (double)buf->oracle_diag_vferr_sum[i]
                    / (double)buf->oracle_diag_vferr_count[i]
                : 0.0);
            dict_set(out, curriculum_diag_vferr_key(i, 2),
                (double)buf->oracle_diag_vferr_max[i]);
        }
        long long live_count[PUFFER_CURRICULUM_SOLVE_RATE_BINS] = {};
        long long live_samples_sum[PUFFER_CURRICULUM_SOLVE_RATE_BINS] = {};
        for (int seg = 0; seg < buf->oracle_segment_count; seg++) {
            int bucket = curriculum_oracle_solve_rate_bucket(
                buf->oracle_segment_solve_rate[seg]);
            if (bucket < 0 || bucket >= PUFFER_CURRICULUM_SOLVE_RATE_BINS) {
                continue;
            }
            live_count[bucket]++;
            live_samples_sum[bucket] += buf->oracle_segment_sample_count[seg];
        }
        for (int i = 0; i < PUFFER_CURRICULUM_SOLVE_RATE_BINS; i++) {
            dict_set(out, curriculum_diag_segment_use_key(i, 0),
                (double)buf->oracle_diag_eject_count[i]);
            dict_set(out, curriculum_diag_segment_use_key(i, 1),
                buf->oracle_diag_eject_count[i] > 0
                ? (double)buf->oracle_diag_eject_samples_sum[i]
                    / (double)buf->oracle_diag_eject_count[i]
                : 0.0);
            dict_set(out, curriculum_diag_segment_use_key(i, 2),
                (double)buf->oracle_diag_eject_samples_max[i]);
            dict_set(out, curriculum_diag_segment_use_key(i, 3),
                (double)live_count[i]);
            dict_set(out, curriculum_diag_segment_use_key(i, 4),
                live_count[i] > 0
                ? (double)live_samples_sum[i] / (double)live_count[i]
                : 0.0);
        }
#endif
        long long attempts = 0;
        long long successes = 0;
        for (int i = 0; i < PUFFER_CURRICULUM_CL_BINS; i++) {
            attempts += buf->oracle_cl_attempts[i];
            successes += buf->oracle_cl_successes[i];
        }
        long long tail_attempts = buf->oracle_cl_attempts[0];
        long long tail_successes = buf->oracle_cl_successes[0];
        dict_set(out, "cl_n", (double)attempts);
        dict_set(out, "cl_sr", attempts > 0
            ? (double)successes / (double)attempts : 0.0);
        dict_set(out, "cl4_n", (double)tail_attempts);
        dict_set(out, "cl4_sr", tail_attempts > 0
            ? (double)tail_successes / (double)tail_attempts : 0.0);
        dict_set(out, "cl0_n", (double)buf->oracle_start_attempts);
        dict_set(out, "cl0_sr", buf->oracle_start_attempts > 0
            ? (double)buf->oracle_start_successes /
                (double)buf->oracle_start_attempts : 0.0);
        dict_set(out, "cl_t", (double)buf->oracle_cl_terminals);
        dict_set(out, "cl_s", (double)buf->oracle_cl_solve_terminals);
        dict_set(out, "cl_rep", (double)buf->oracle_cl_tail_replacements);
        dict_set(out, "cl_rep_sr", buf->oracle_cl_terminals > 0
            ? (double)buf->oracle_cl_tail_replacements /
                (double)buf->oracle_cl_terminals : 0.0);
        dict_set(out, "fs_n", (double)buf->oracle_fresh_solve_candidates);
        dict_set(out, "fs_in", (double)buf->oracle_fresh_segments_stored);
#ifdef BOXOBAN_LEVEL_LOGS
        dict_set(out, "fs_ep", (double)buf->oracle_diag_fresh_episodes);
#endif
        dict_set(out, "nf_n", (double)buf->oracle_fresh_attempts);
        dict_set(out, "nf_sr", buf->oracle_fresh_attempts > 0
            ? (double)buf->oracle_fresh_successes /
                (double)buf->oracle_fresh_attempts : 0.0);
    } else {
        double priority_sum = 0.0;
        double priority_max = 0.0;
        for (int i = 0; i < buf->size; i++) {
            double p = (double)buf->priorities[i];
            priority_sum += p;
            if (p > priority_max) {
                priority_max = p;
            }
        }

        int hi = -1;
        int prev = -1;
        for (int i = 0; i < PUFFER_CURRICULUM_DIAG_BINS; i++) {
            if (buf->diag_retained_outcome[i] <= 0) {
                continue;
            }
            prev = hi;
            hi = i;
        }

        double hi_n = hi >= 0 ? (double)buf->diag_retained_outcome[hi] : 0.0;
        double prev_n = prev >= 0 ? (double)buf->diag_retained_outcome[prev] : 0.0;
        double hi_p = hi_n > 0.0
            ? (double)buf->diag_retained_priority_sum[hi] / hi_n : 0.0;
        double prev_p = prev_n > 0.0
            ? (double)buf->diag_retained_priority_sum[prev] / prev_n : 0.0;
        double hi_pre_n = 0.0;
        double hi_post_n = 0.0;
        double hi_pre_p_sum = 0.0;
        double hi_pre_tick_sum = 0.0;
        int hi_pre_t0 = 1000000000;
        int hi_pre_t1 = -1;

        dict_set(out, "buf_n", (double)buf->size);
        dict_set(out, "buf_min", (double)buf->min_priority);
        dict_set(out, "buf_avg", buf->size > 0 ? priority_sum / (double)buf->size : 0.0);
        dict_set(out, "buf_max", priority_max);
        dict_set(out, "buf_hi", (double)hi);
        dict_set(out, "buf_hi_n", hi_n);
        dict_set(out, "buf_hi_p", hi_p);
        dict_set(out, "buf_prv", (double)prev);
        dict_set(out, "buf_prv_n", prev_n);
        dict_set(out, "buf_prv_p", prev_p);
        dict_set(out, "buf_pre_n", hi_pre_n);
        dict_set(out, "buf_pre_p", hi_pre_n > 0.0 ? hi_pre_p_sum / hi_pre_n : 0.0);
        dict_set(out, "buf_pre_t", hi_pre_n > 0.0 ? hi_pre_tick_sum / hi_pre_n : -1.0);
        dict_set(out, "buf_pre_t0", hi_pre_n > 0.0 ? (double)hi_pre_t0 : -1.0);
        dict_set(out, "buf_pre_t1", hi_pre_n > 0.0 ? (double)hi_pre_t1 : -1.0);
        dict_set(out, "buf_post_n", hi_post_n);
    }
}
#endif

#endif
