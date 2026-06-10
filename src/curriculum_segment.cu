// Compact segment-based curriculum state-set implementation.

#include <cub/device/device_scan.cuh>
#include <math.h>
#include <stdio.h>

#ifndef PUFFER_CURRICULUM_CL_BINS
#define PUFFER_CURRICULUM_CL_BINS 4
#endif

#ifdef PUFFER_CURRICULUM_TYPES

struct CurriculumDiagnostics;

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
    PufferState* states;             // Packed retained segments, length capacity
    PufferState* checkpoint_states;  // num_checkpoints * num_envs rollout snapshots
    PufferState* history_states;     // num_envs * history_capacity ring histories
    PufferState* candidate_states;   // num_envs * history_capacity materialized candidates
    PufferState* pending_states;     // history_capacity pending retained segment
    int* history_source;
    int* history_count;
    int* history_write;
    int* history_from_entry;
    int* candidate_source;
    int* candidate_count;
    int* pending_source;
    int* env_state_inds_host;
    int* cl_start_slot;
    int* cl_head_sample;
    int* state_update_source;
    float* state_priority;           // Per-state raw advantage
    float* state_return;             // Per-state discounted return
    precision_t* priorities_host;    // GPU copy source for diagnostics/compat
    precision_t* env_scores_host;    // Checkpoint advantage scores
    float* candidate_score;
    float* segment_priority;
    float* segment_score;
    float* segment_return;
    int* segment_len;
    long* segment_agent_step;
    long long* segment_sample_count;
    int capacity;
    int size;
    int num_envs;
    int num_checkpoints;
    int checkpoint_interval;
    int candidate_capacity;
    int score_capacity;
    int history_capacity;
    int segment_capacity;
    int max_segments;
    int segment_count;
    int agents_per_env;
    int num_cl_envs;
    int num_fresh_envs;
    int admit_adv;
    int diagnostics_enabled;
    int pending_count;
    int pending_env_idx;
    float pending_score;
    float pending_priority;
    float pending_return;
    float min_priority;
    float saved_score;
    float saved_return;
    float saved_priority;
    int saved_pick_t;
    int saved_first_t;
    int saved_last_t;
    int saved_full;
    PrecisionTensor advantages;      // GPU, shape {state_buffer_size}
    PrecisionTensor env_scores;      // GPU scratch, shape {score_capacity}
    PrecisionTensor importance;      // GPU, shape {total_agents}
    CurriculumDiagnostics* diag;
};

static inline int curriculum_diag_init_state_buffer(StateBuffer* buf, int total_agents);
static inline void curriculum_diag_close_state_buffer(StateBuffer* buf);

void register_state_buffer(StateBuffer* buf, Allocator* alloc,
        int capacity, int total_agents, int num_envs, int agents_per_env,
        int num_cl_envs, int horizon, int checkpoint_interval) {
    buf->capacity = capacity;
    buf->size = 0;
    buf->num_envs = num_envs;
    buf->checkpoint_interval = checkpoint_interval;
    buf->num_checkpoints = (horizon + checkpoint_interval - 1) / checkpoint_interval;
    buf->candidate_capacity = num_envs * buf->num_checkpoints;
    buf->score_capacity = buf->candidate_capacity;
    buf->history_capacity = horizon;
    if (buf->history_capacity > capacity) {
        buf->history_capacity = capacity;
    }
    if (buf->history_capacity < 1) {
        buf->history_capacity = 1;
    }
    buf->segment_capacity = buf->history_capacity;
    buf->max_segments = capacity / buf->segment_capacity;
    if (buf->max_segments < 1 && capacity > 0) {
        buf->max_segments = 1;
    }
    buf->segment_count = 0;
    buf->agents_per_env = agents_per_env;
    buf->num_cl_envs = num_cl_envs;
    buf->num_fresh_envs = num_envs - num_cl_envs;
    buf->admit_adv = 1;
    buf->diagnostics_enabled = 0;
    buf->pending_count = 0;
    buf->pending_env_idx = -1;
    buf->pending_score = 0.0f;
    buf->pending_priority = 0.0f;
    buf->pending_return = 0.0f;
    buf->min_priority = 0.0f;
    buf->saved_score = 0.0f;
    buf->saved_return = 0.0f;
    buf->saved_priority = 0.0f;
    buf->saved_pick_t = -1;
    buf->saved_first_t = -1;
    buf->saved_last_t = -1;
    buf->saved_full = 0;
    buf->diag = NULL;
    buf->advantages = {.shape = {capacity}};
    buf->env_scores = {.shape = {buf->score_capacity}};
    buf->importance = {.shape = {total_agents}};
    alloc_register(alloc, &buf->advantages);
    alloc_register(alloc, &buf->env_scores);
    alloc_register(alloc, &buf->importance);
}

int init_state_buffer(StateBuffer* buf, int total_agents) {
    size_t capacity = (size_t)buf->capacity;
    size_t envs = (size_t)buf->num_envs;
    size_t hist_cap = (size_t)buf->history_capacity;
    size_t checkpoints = (size_t)buf->candidate_capacity;
    size_t max_segments = (size_t)buf->max_segments;
    size_t state_bytes = capacity * sizeof(PufferState);
    size_t checkpoint_bytes = checkpoints * sizeof(PufferState);
    size_t hist_bytes = envs * hist_cap * sizeof(PufferState);
    size_t source_bytes = envs * hist_cap * sizeof(int);

    buf->states = (PufferState*)malloc(state_bytes);
    buf->checkpoint_states = (PufferState*)malloc(checkpoint_bytes);
    buf->history_states = (PufferState*)malloc(hist_bytes);
    buf->candidate_states = (PufferState*)malloc(hist_bytes);
    buf->pending_states = (PufferState*)malloc(hist_cap * sizeof(PufferState));
    buf->history_source = (int*)malloc(source_bytes);
    buf->history_count = (int*)malloc(envs * sizeof(int));
    buf->history_write = (int*)malloc(envs * sizeof(int));
    buf->history_from_entry = (int*)malloc(envs * sizeof(int));
    buf->candidate_source = (int*)malloc(source_bytes);
    buf->candidate_count = (int*)malloc(envs * sizeof(int));
    buf->pending_source = (int*)malloc(hist_cap * sizeof(int));
    buf->env_state_inds_host = (int*)malloc(envs * sizeof(int));
    buf->cl_start_slot = (int*)malloc(envs * sizeof(int));
    buf->cl_head_sample = (int*)malloc(envs * sizeof(int));
    buf->state_update_source = (int*)malloc(capacity * sizeof(int));
    buf->state_priority = (float*)malloc(capacity * sizeof(float));
    buf->state_return = (float*)malloc(capacity * sizeof(float));
    buf->priorities_host = (precision_t*)malloc(capacity * sizeof(precision_t));
    buf->env_scores_host = (precision_t*)malloc((size_t)buf->score_capacity * sizeof(precision_t));
    buf->candidate_score = (float*)malloc(envs * sizeof(float));
    buf->segment_priority = (float*)malloc(max_segments * sizeof(float));
    buf->segment_score = (float*)malloc(max_segments * sizeof(float));
    buf->segment_return = (float*)malloc(max_segments * sizeof(float));
    buf->segment_len = (int*)malloc(max_segments * sizeof(int));
    buf->segment_agent_step = (long*)malloc(max_segments * sizeof(long));
    buf->segment_sample_count = (long long*)malloc(max_segments * sizeof(long long));

    if (buf->states == NULL || buf->checkpoint_states == NULL
            || buf->history_states == NULL || buf->candidate_states == NULL
            || buf->pending_states == NULL || buf->history_source == NULL
            || buf->history_count == NULL || buf->history_write == NULL
            || buf->history_from_entry == NULL || buf->candidate_source == NULL
            || buf->candidate_count == NULL || buf->pending_source == NULL
            || buf->env_state_inds_host == NULL || buf->cl_start_slot == NULL
            || buf->cl_head_sample == NULL || buf->state_update_source == NULL
            || buf->state_priority == NULL || buf->state_return == NULL
            || buf->priorities_host == NULL || buf->env_scores_host == NULL
            || buf->candidate_score == NULL || buf->segment_priority == NULL
            || buf->segment_score == NULL || buf->segment_return == NULL
            || buf->segment_len == NULL || buf->segment_agent_step == NULL
            || buf->segment_sample_count == NULL) {
        fprintf(stderr,
            "Failed to allocate curriculum segment buffer: capacity=%d state_size=%d bytes=%zu\n",
            buf->capacity, (int)sizeof(PufferState), state_bytes);
        return 0;
    }

    memset(buf->states, 0, state_bytes);
    memset(buf->checkpoint_states, 0, checkpoint_bytes);
    memset(buf->history_states, 0, hist_bytes);
    memset(buf->candidate_states, 0, hist_bytes);
    memset(buf->history_source, -1, source_bytes);
    memset(buf->candidate_source, -1, source_bytes);
    memset(buf->history_count, 0, envs * sizeof(int));
    memset(buf->history_write, 0, envs * sizeof(int));
    memset(buf->history_from_entry, 0, envs * sizeof(int));
    memset(buf->candidate_count, 0, envs * sizeof(int));
    memset(buf->candidate_score, 0, envs * sizeof(float));
    memset(buf->pending_source, -1, hist_cap * sizeof(int));
    memset(buf->env_state_inds_host, -1, envs * sizeof(int));
    memset(buf->cl_start_slot, -1, envs * sizeof(int));
    memset(buf->cl_head_sample, 0, envs * sizeof(int));
    memset(buf->state_update_source, -1, capacity * sizeof(int));
    memset(buf->state_priority, 0, capacity * sizeof(float));
    memset(buf->state_return, 0, capacity * sizeof(float));
    memset(buf->priorities_host, 0, capacity * sizeof(precision_t));
    memset(buf->env_scores_host, 0, (size_t)buf->score_capacity * sizeof(precision_t));
    for (int i = 0; i < buf->max_segments; i++) {
        buf->segment_priority[i] = 0.0f;
        buf->segment_score[i] = 0.0f;
        buf->segment_return[i] = 0.0f;
        buf->segment_len[i] = 0;
        buf->segment_agent_step[i] = -1;
        buf->segment_sample_count[i] = 0;
    }

    return curriculum_diag_init_state_buffer(buf, total_agents);
}

void close_state_buffer(StateBuffer* buf) {
    curriculum_diag_close_state_buffer(buf);
    free(buf->states);
    free(buf->checkpoint_states);
    free(buf->history_states);
    free(buf->candidate_states);
    free(buf->pending_states);
    free(buf->history_source);
    free(buf->history_count);
    free(buf->history_write);
    free(buf->history_from_entry);
    free(buf->candidate_source);
    free(buf->candidate_count);
    free(buf->pending_source);
    free(buf->env_state_inds_host);
    free(buf->cl_start_slot);
    free(buf->cl_head_sample);
    free(buf->state_update_source);
    free(buf->state_priority);
    free(buf->state_return);
    free(buf->priorities_host);
    free(buf->env_scores_host);
    free(buf->candidate_score);
    free(buf->segment_priority);
    free(buf->segment_score);
    free(buf->segment_return);
    free(buf->segment_len);
    free(buf->segment_agent_step);
    free(buf->segment_sample_count);
}

#endif

#ifdef PUFFER_CURRICULUM_IMPL

#define PRIO_WARP_SIZE 32
#define PRIO_FULL_MASK 0xffffffff
#define PRIO_BLOCK_SIZE 256

static inline precision_t* curriculum_diag_debug_device(StateBuffer* buf);
static inline void curriculum_diag_copy_checkpoint_debug(StateBuffer* buf, int rows, cudaStream_t stream);
static inline void curriculum_diag_rollout_begin(PuffeRL* pufferl);
static inline void curriculum_diag_source_snapshot(PuffeRL* pufferl, int source, const PufferState* state);
static inline void curriculum_diag_fresh_terminal(StateBuffer* buf, const Env* env);
static inline void curriculum_diag_cl_start(StateBuffer* buf, int env_idx, int sampled_slot, int remaining, int prefer_head);
static inline void curriculum_diag_cl_terminal(StateBuffer* buf, int env_idx, float reward);
static inline void curriculum_diag_cl_tail_replaced(StateBuffer* buf);
static inline void curriculum_diag_segment_stored(StateBuffer* buf, int seg, long agent_step);
static inline void curriculum_diag_segment_ejected(StateBuffer* buf, int seg);
static inline void curriculum_diag_candidate_selected(StateBuffer* buf, int candidate_count,
    float raw_best_score, float raw_best_priority, float adv_best_score,
    float adv_best_priority, int selected_env, int stored);

static inline float clean_state_priority(float priority) {
    if (priority < 0.0f || isnan(priority) || isinf(priority)) {
        return 0.0f;
    }
    return priority;
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
        const precision_t* __restrict__ values_bt,
        const precision_t* __restrict__ rewards_bt,
        const precision_t* __restrict__ terminals_bt,
        int num_envs, int num_checkpoints, int checkpoint_interval,
        int agents_per_env, float gamma, int horizon) {
    int row = blockIdx.x * blockDim.x + threadIdx.x;
    int checkpoint_rows = num_checkpoints * num_envs;
    if (row >= checkpoint_rows) {
        return;
    }

    int c = row / num_envs;
    int env_idx = row - c * num_envs;
    int start_t = c * checkpoint_interval;
    int agent_start = env_idx * agents_per_env;
    float sum_adv = 0.0f;
    float sum_value = 0.0f;
    float sum_next_value = 0.0f;
    float sum_next_reward = 0.0f;
    float sum_next_terminal = 0.0f;
    float sum_delta = 0.0f;

    for (int a = 0; a < agents_per_env; a++) {
        int offset = (agent_start + a) * horizon;
        float adv = start_t < horizon - 1
            ? to_float(advantages_bt[offset + start_t]) : 0.0f;
        sum_adv += adv;
        if (debug_dst != NULL && values_bt != NULL
                && rewards_bt != NULL && terminals_bt != NULL) {
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
    float mean_adv = sum_adv * inv_agents;
    dst[row] = from_float(mean_adv);
    if (debug_dst != NULL && values_bt != NULL
            && rewards_bt != NULL && terminals_bt != NULL) {
        int base = row * 6;
        debug_dst[base + 0] = from_float(sum_value * inv_agents);
        debug_dst[base + 1] = from_float(sum_next_value * inv_agents);
        debug_dst[base + 2] = from_float(sum_next_reward * inv_agents);
        debug_dst[base + 3] = from_float(sum_next_terminal * inv_agents);
        debug_dst[base + 4] = from_float(sum_delta * inv_agents);
        debug_dst[base + 5] = from_float(mean_adv);
    }
}

__global__ void compute_curriculum_checkpoint_scores(
        precision_t* __restrict__ dst,
        const precision_t* __restrict__ advantages_bt,
        int num_envs, int num_fresh_envs, int num_cl_envs,
        int num_checkpoints, int checkpoint_interval,
        int agents_per_env, int horizon) {
    (void)num_fresh_envs;
    (void)num_cl_envs;
    int row = blockIdx.x * blockDim.x + threadIdx.x;
    int checkpoint_rows = num_checkpoints * num_envs;
    if (row >= checkpoint_rows) {
        return;
    }
    int c = row / num_envs;
    int env_idx = row - c * num_envs;
    int start_t = c * checkpoint_interval;
    int agent_start = env_idx * agents_per_env;
    float sum_adv = 0.0f;
    for (int a = 0; a < agents_per_env; a++) {
        int offset = (agent_start + a) * horizon;
        sum_adv += start_t < horizon - 1
            ? fabsf(to_float(advantages_bt[offset + start_t])) : 0.0f;
    }
    dst[row] = from_float(sum_adv / (float)agents_per_env);
}

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

static inline unsigned int curriculum_mix32(unsigned int x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline int curriculum_cl_bucket(int remaining) {
    if (remaining <= 4) return 0;
    if (remaining <= 16) return 1;
    if (remaining <= 64) return 2;
    return 3;
}

static inline int curriculum_state_slot_source(int slot) {
    return -slot - 2;
}

static inline int curriculum_source_state_slot(int source) {
    return -source - 2;
}

static inline float curriculum_source_raw_advantage(StateBuffer* buf, int source) {
    float adv = 0.0f;
    if (source >= 0 && source < buf->candidate_capacity) {
        adv = to_float(buf->env_scores_host[source]);
    } else if (source <= -2) {
        int slot = curriculum_source_state_slot(source);
        if (slot >= 0 && slot < buf->size) {
            adv = buf->state_priority[slot];
        }
    }
    if (isnan(adv) || isinf(adv)) {
        return 0.0f;
    }
    return adv;
}

static inline float curriculum_source_positive_advantage(StateBuffer* buf, int source) {
    return clean_state_priority(curriculum_source_raw_advantage(buf, source));
}

static inline float curriculum_discounted_return(
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
    return (isnan(ret) || isinf(ret)) ? 0.0f : ret;
}

static inline void curriculum_push_history(
        StateBuffer* buf, int env_idx, const PufferState* state, int source) {
    int cap = buf->history_capacity;
    int base = env_idx * cap;
    int write = buf->history_write[env_idx];
    buf->history_states[base + write] = *state;
    buf->history_source[base + write] = source;
    write = (write + 1) % cap;
    buf->history_write[env_idx] = write;
    if (buf->history_count[env_idx] < cap) {
        buf->history_count[env_idx]++;
    }
}

static inline void curriculum_reset_history(
        StateBuffer* buf, int env_idx, const PufferState* state, int source) {
    buf->history_count[env_idx] = 0;
    buf->history_write[env_idx] = 0;
    buf->history_from_entry[env_idx] = 1;
    curriculum_push_history(buf, env_idx, state, source);
}

static inline const PufferState* curriculum_last_history_state(
        StateBuffer* buf, int env_idx) {
    int count = buf->history_count[env_idx];
    if (count <= 0) {
        return NULL;
    }
    int cap = buf->history_capacity;
    int last = (buf->history_write[env_idx] + cap - 1) % cap;
    return &buf->history_states[env_idx * cap + last];
}

static inline void curriculum_save_candidate_history(
        StateBuffer* buf, int env_idx, float terminal_score) {
    int count = buf->history_count[env_idx];
    int cap = buf->history_capacity;
    if (env_idx < 0 || env_idx >= buf->num_envs || count <= 0 || cap <= 0) {
        return;
    }
    int hist_base = env_idx * cap;
    int start = (buf->history_write[env_idx] + cap - count) % cap;
    int cand_base = env_idx * cap;
    for (int i = 0; i < count; i++) {
        int src = (start + i) % cap;
        buf->candidate_states[cand_base + i] = buf->history_states[hist_base + src];
        buf->candidate_source[cand_base + i] = buf->history_source[hist_base + src];
    }
    for (int i = count; i < cap; i++) {
        buf->candidate_source[cand_base + i] = -1;
    }
    buf->candidate_count[env_idx] = count;
    buf->candidate_score[env_idx] =
        (isnan(terminal_score) || isinf(terminal_score)) ? 0.0f : terminal_score;
}

static inline void curriculum_clear_candidates(StateBuffer* buf) {
    memset(buf->candidate_count, 0, (size_t)buf->num_envs * sizeof(int));
    memset(buf->candidate_score, 0, (size_t)buf->num_envs * sizeof(float));
}

static inline float curriculum_history_score(StateBuffer* buf, int env_idx) {
    const PufferState* last = curriculum_last_history_state(buf, env_idx);
    if (last == NULL || isnan(last->episode_return) || isinf(last->episode_return)) {
        return 0.0f;
    }
    return last->episode_return;
}

static inline float curriculum_segment_priority_from_sources(
        StateBuffer* buf, const int* sources, int count) {
    double positive_sum = 0.0;
    for (int i = 0; i < count; i++) {
        positive_sum += (double)curriculum_source_positive_advantage(buf, sources[i]);
    }
    return clean_state_priority((float)positive_sum);
}

static inline int curriculum_segment_better(
        StateBuffer* buf, float score, float priority, int count,
        float ref_score, float ref_priority, int ref_count) {
    if (count <= 0) {
        return 0;
    }
    if (buf == NULL || buf->admit_adv) {
        if (priority <= 0.0f) {
            return 0;
        }
        if (priority > ref_priority + 1e-6f) return 1;
        if (priority < ref_priority - 1e-6f) return 0;
        if (score > ref_score + 1e-6f) return 1;
        if (score < ref_score - 1e-6f) return 0;
    } else {
        if (score > ref_score + 1e-6f) return 1;
        if (score < ref_score - 1e-6f) return 0;
        if (ref_count <= 0) return 1;
        if (count < ref_count) return 1;
        if (count > ref_count) return 0;
        if (priority > ref_priority + 1e-6f) return 1;
        if (priority < ref_priority - 1e-6f) return 0;
    }
    return 0;
}

static inline void curriculum_update_segment_priority_from_slots(StateBuffer* buf, int seg) {
    int seg_cap = buf->segment_capacity;
    if (seg_cap <= 0 || seg < 0 || seg >= buf->segment_count) {
        return;
    }
    int base = seg * seg_cap;
    int len = buf->segment_len[seg];
    if (len <= 0 || base < 0 || base >= buf->size) {
        buf->segment_priority[seg] = 0.0f;
        return;
    }
    int end = base + len;
    if (end > buf->size) {
        end = buf->size;
    }
    double positive_sum = 0.0;
    for (int slot = base; slot < end; slot++) {
        positive_sum += (double)clean_state_priority(buf->state_priority[slot]);
    }
    buf->segment_priority[seg] = clean_state_priority((float)positive_sum);
}

static inline int curriculum_min_segment(StateBuffer* buf) {
    if (buf->segment_count <= 0) {
        return -1;
    }
    int min_idx = 0;
    for (int i = 1; i < buf->segment_count; i++) {
        if (curriculum_segment_better(buf,
                buf->segment_score[min_idx], buf->segment_priority[min_idx],
                buf->segment_len[min_idx],
                buf->segment_score[i], buf->segment_priority[i],
                buf->segment_len[i])) {
            min_idx = i;
        }
    }
    return min_idx;
}

static inline void curriculum_refresh_saved_segment(StateBuffer* buf) {
    int best = -1;
    for (int seg = 0; seg < buf->segment_count; seg++) {
        if (curriculum_segment_better(buf,
                buf->segment_score[seg], buf->segment_priority[seg],
                buf->segment_len[seg],
                best >= 0 ? buf->segment_score[best] : 0.0f,
                best >= 0 ? buf->segment_priority[best] : 0.0f,
                best >= 0 ? buf->segment_len[best] : 0)) {
            best = seg;
        }
    }
    if (best < 0) {
        buf->saved_score = 0.0f;
        buf->saved_return = 0.0f;
        buf->saved_priority = 0.0f;
        buf->saved_pick_t = -1;
        buf->saved_first_t = -1;
        buf->saved_last_t = -1;
        buf->saved_full = 0;
        return;
    }
    buf->saved_score = buf->segment_score[best];
    buf->saved_return = buf->segment_return[best];
    buf->saved_priority = buf->segment_priority[best];
    buf->saved_first_t = 0;
    buf->saved_last_t = buf->segment_len[best] - 1;
    buf->saved_pick_t = buf->saved_last_t;
    buf->saved_full = buf->segment_len[best] == buf->segment_capacity;
}

static inline void curriculum_refresh_min_priority(StateBuffer* buf) {
    int min_seg = curriculum_min_segment(buf);
    buf->min_priority = min_seg >= 0 ? buf->segment_priority[min_seg] : 0.0f;
}

static inline void curriculum_refresh_sample_priorities(StateBuffer* buf) {
    const float uniform_mix = 0.05f;
    for (int slot = 0; slot < buf->capacity; slot++) {
        buf->priorities_host[slot] = from_float(0.0f);
    }
    for (int seg = 0; seg < buf->segment_count; seg++) {
        int base = seg * buf->segment_capacity;
        int len = buf->segment_len[seg];
        if (len <= 0 || base >= buf->size) {
            continue;
        }
        if (base + len > buf->size) {
            len = buf->size - base;
        }
        float pos_sum = 0.0f;
        for (int i = 0; i < len; i++) {
            pos_sum += clean_state_priority(buf->state_priority[base + i]);
        }
        float inv_len = 1.0f / (float)len;
        for (int i = 0; i < len; i++) {
            float priority = inv_len;
            if (pos_sum > 0.0f) {
                priority = uniform_mix * inv_len
                    + (1.0f - uniform_mix)
                    * clean_state_priority(buf->state_priority[base + i]) / pos_sum;
            }
            buf->priorities_host[base + i] = from_float(priority);
        }
    }
    curriculum_refresh_saved_segment(buf);
    curriculum_refresh_min_priority(buf);
}

static inline void curriculum_set_slot_priority_from_source(
        StateBuffer* buf, int slot, int source, int defer_positive) {
    if (slot < 0 || slot >= buf->capacity) {
        return;
    }
    if (defer_positive && source >= 0 && source < buf->candidate_capacity) {
        buf->state_update_source[slot] = source;
        buf->state_priority[slot] = 0.0f;
        return;
    }
    buf->state_update_source[slot] = -1;
    buf->state_priority[slot] = curriculum_source_raw_advantage(buf, source);
}

static inline void curriculum_apply_deferred_priority_updates(StateBuffer* buf) {
    int updated = 0;
    for (int slot = 0; slot < buf->size; slot++) {
        int source = buf->state_update_source[slot];
        if (source < 0) {
            continue;
        }
        buf->state_priority[slot] = curriculum_source_raw_advantage(buf, source);
        buf->state_update_source[slot] = -1;
        updated = 1;
    }
    if (updated) {
        for (int seg = 0; seg < buf->segment_count; seg++) {
            curriculum_update_segment_priority_from_slots(buf, seg);
        }
    }
}

static inline int curriculum_find_segment_slot(
        StateBuffer* buf, float score, float priority, int count) {
    if (buf->max_segments <= 0 || buf->segment_capacity <= 0) {
        return -1;
    }
    if (buf->segment_count < buf->max_segments) {
        int slot = buf->segment_count;
        buf->segment_count++;
        return slot;
    }
    int min_idx = curriculum_min_segment(buf);
    if (min_idx < 0 || !curriculum_segment_better(buf,
            score, priority, count,
            buf->segment_score[min_idx], buf->segment_priority[min_idx],
            buf->segment_len[min_idx])) {
        return -1;
    }
    curriculum_diag_segment_ejected(buf, min_idx);
    return min_idx;
}

static inline int curriculum_store_pending_segment(
        StateBuffer* buf, long agent_step, float gamma) {
    int count = buf->pending_count;
    int seg_cap = buf->segment_capacity;
    if (count <= 0 || seg_cap <= 0 || count > seg_cap) {
        return 0;
    }
    int segment_slot = curriculum_find_segment_slot(
        buf, buf->pending_score, buf->pending_priority, count);
    if (segment_slot < 0) {
        return 0;
    }

    int base = segment_slot * seg_cap;
    gamma = fminf(1.0f, fmaxf(0.0f, gamma));
    for (int i = 0; i < seg_cap; i++) {
        int slot = base + i;
        if (i < count) {
            buf->states[slot] = buf->pending_states[i];
            buf->state_return[slot] = curriculum_discounted_return(
                buf->pending_states, count, i, buf->pending_return, gamma);
            curriculum_set_slot_priority_from_source(
                buf, slot, buf->pending_source[i], 0);
        } else {
            buf->states[slot] = buf->pending_states[count - 1];
            buf->state_return[slot] = 0.0f;
            buf->state_priority[slot] = 0.0f;
            buf->state_update_source[slot] = -1;
        }
        buf->priorities_host[slot] = from_float(0.0f);
    }

    buf->segment_score[segment_slot] = buf->pending_score;
    buf->segment_return[segment_slot] = buf->pending_return;
    buf->segment_len[segment_slot] = count;
    buf->segment_agent_step[segment_slot] = agent_step;
    buf->segment_sample_count[segment_slot] = 0;
    curriculum_update_segment_priority_from_slots(buf, segment_slot);
    buf->size = buf->segment_count * seg_cap;
    curriculum_refresh_sample_priorities(buf);
    curriculum_diag_segment_stored(buf, segment_slot, agent_step);
    return 1;
}

static inline int curriculum_select_pending_candidate(StateBuffer* buf, long agent_step) {
    (void)agent_step;
    buf->pending_count = 0;
    buf->pending_env_idx = -1;
    buf->pending_score = 0.0f;
    buf->pending_priority = 0.0f;
    buf->pending_return = 0.0f;

    int cap = buf->history_capacity;
    int max_envs = buf->num_fresh_envs;
    if (max_envs > buf->num_envs) {
        max_envs = buf->num_envs;
    }
    for (int env_idx = 0; env_idx < max_envs; env_idx++) {
        if (buf->candidate_count[env_idx] > 0) {
            continue;
        }
        int count = buf->history_count[env_idx];
        if (count <= 0 || count > cap) {
            continue;
        }
        curriculum_save_candidate_history(
            buf, env_idx, curriculum_history_score(buf, env_idx));
    }

    int best_env = -1;
    int best_count = 0;
    float best_score = 0.0f;
    float best_priority = 0.0f;
    float raw_best_score = 0.0f;
    float raw_best_priority = 0.0f;
    float adv_best_score = 0.0f;
    float adv_best_priority = 0.0f;
    int candidate_n = 0;

    for (int env_idx = 0; env_idx < max_envs; env_idx++) {
        int count = buf->candidate_count[env_idx];
        if (count <= 0 || count > cap) {
            continue;
        }
        int base = env_idx * cap;
        float score = buf->candidate_score[env_idx];
        float priority = curriculum_segment_priority_from_sources(
            buf, buf->candidate_source + base, count);
        candidate_n++;
        if (score > raw_best_score + 1e-6f
                || (fabsf(score - raw_best_score) <= 1e-6f
                    && priority > raw_best_priority)) {
            raw_best_score = score;
            raw_best_priority = priority;
        }
        if (priority > adv_best_priority + 1e-6f
                || (fabsf(priority - adv_best_priority) <= 1e-6f
                    && score > adv_best_score)) {
            adv_best_score = score;
            adv_best_priority = priority;
        }
        if (curriculum_segment_better(buf, score, priority, count,
                best_score, best_priority, best_count)) {
            best_env = env_idx;
            best_count = count;
            best_score = score;
            best_priority = priority;
        }
    }

    int stored = 0;
    if (best_env >= 0 && best_count > 0) {
        int base = best_env * cap;
        for (int i = 0; i < best_count; i++) {
            buf->pending_states[i] = buf->candidate_states[base + i];
            buf->pending_source[i] = buf->candidate_source[base + i];
        }
        buf->pending_env_idx = best_env;
        buf->pending_count = best_count;
        buf->pending_score = best_score;
        buf->pending_return = best_score;
        buf->pending_priority = best_priority;
    }
    curriculum_diag_candidate_selected(buf, candidate_n, raw_best_score,
        raw_best_priority, adv_best_score, adv_best_priority, best_env, stored);
    return best_env >= 0;
}

static inline int curriculum_select_segment(StateBuffer* buf) {
    int best = -1;
    for (int seg = 0; seg < buf->segment_count; seg++) {
        if (buf->segment_len[seg] <= 0) {
            continue;
        }
        if (curriculum_segment_better(buf,
                buf->segment_score[seg], buf->segment_priority[seg],
                buf->segment_len[seg],
                best >= 0 ? buf->segment_score[best] : 0.0f,
                best >= 0 ? buf->segment_priority[best] : 0.0f,
                best >= 0 ? buf->segment_len[best] : 0)) {
            best = seg;
        }
    }
    if (best < 0 && buf->segment_count > 0) {
        best = 0;
    }
    return best;
}

static inline int curriculum_sample_slot(StateBuffer* buf, unsigned int salt) {
    int seg = curriculum_select_segment(buf);
    if (seg < 0) {
        return -1;
    }
    int len = buf->segment_len[seg];
    if (len <= 0) {
        return -1;
    }
    unsigned int mixed = curriculum_mix32(
        salt ^ (unsigned int)(seg * 0x9e3779b9U)
             ^ (unsigned int)(len * 0x85ebca6bU));
    return seg * buf->segment_capacity + (int)(mixed % (unsigned int)len);
}

static inline int curriculum_start_cl_env_from_slot(
        StateBuffer* buf, Env* env, int env_idx, int sampled_slot, int prefer_head) {
    int seg_cap = buf->segment_capacity;
    if (seg_cap <= 0 || env == NULL || sampled_slot < 0 || sampled_slot >= buf->size) {
        return 0;
    }
    int seg = sampled_slot / seg_cap;
    if (seg < 0 || seg >= buf->segment_count || buf->segment_len[seg] <= 0) {
        return 0;
    }
    if (prefer_head) {
        sampled_slot = seg * seg_cap;
    }
    int offset = sampled_slot - seg * seg_cap;
    if (offset < 0 || offset >= buf->segment_len[seg]) {
        return 0;
    }

    buf->segment_sample_count[seg]++;
    env->state = buf->states[sampled_slot];
    puffer_state_refresh(env);
    buf->env_state_inds_host[env_idx] = sampled_slot;
    buf->cl_start_slot[env_idx] = sampled_slot;
    buf->cl_head_sample[env_idx] = prefer_head ? 1 : 0;
    int remaining = buf->segment_len[seg] - offset - 1;
    curriculum_diag_cl_start(buf, env_idx, sampled_slot, remaining, prefer_head);
    curriculum_reset_history(buf, env_idx, &buf->states[sampled_slot],
        curriculum_state_slot_source(sampled_slot));
    return 1;
}

static inline int curriculum_resample_cl_env(
        StateBuffer* buf, Env* env, int env_idx, int source) {
    int cl_idx = env_idx - buf->num_fresh_envs;
    unsigned int salt = (unsigned int)(source + 0x9e3779b9U)
        ^ (unsigned int)(env_idx * 9176U);
    int slot = curriculum_sample_slot(buf, salt);
    return curriculum_start_cl_env_from_slot(
        buf, env, env_idx, slot, (cl_idx & 1) == 0);
}

static inline float curriculum_history_discounted_return_from(
        StateBuffer* buf, int env_idx, int count, int start_offset,
        float terminal_score, float gamma) {
    int cap = buf->history_capacity;
    if (env_idx < 0 || env_idx >= buf->num_envs || count <= 0 || count > cap
            || start_offset < 0 || start_offset >= count) {
        return 0.0f;
    }
    int base = env_idx * cap;
    int start = (buf->history_write[env_idx] + cap - count) % cap;
    PufferState* states = buf->history_states + base;
    gamma = fminf(1.0f, fmaxf(0.0f, gamma));
    float ret = 0.0f;
    for (int i = count - 1; i >= 0; i--) {
        int src = (start + i) % cap;
        PufferState* state = &states[src];
        if (i == count - 1) {
            ret = terminal_score - state->episode_return;
        } else {
            int next_src = (start + i + 1) % cap;
            float reward = states[next_src].episode_return - state->episode_return;
            if (isnan(reward) || isinf(reward)) {
                reward = 0.0f;
            }
            ret = reward + gamma * ret;
        }
        if (isnan(ret) || isinf(ret)) {
            ret = 0.0f;
        }
        if (i == start_offset) {
            return ret;
        }
    }
    return ret;
}

static inline float curriculum_history_discounted_return(
        StateBuffer* buf, int env_idx, int count, float terminal_score, float gamma) {
    return curriculum_history_discounted_return_from(
        buf, env_idx, count, 0, terminal_score, gamma);
}

static inline void curriculum_queue_head_priority_update(
        StateBuffer* buf, int env_idx, int seg, int count) {
    int seg_cap = buf->segment_capacity;
    int hist_cap = buf->history_capacity;
    if (seg_cap <= 0 || hist_cap <= 0 || count <= 0
            || env_idx < 0 || env_idx >= buf->num_envs
            || seg < 0 || seg >= buf->segment_count) {
        return;
    }
    int usable = count;
    if (usable > buf->segment_len[seg]) {
        usable = buf->segment_len[seg];
    }
    int hist_base = env_idx * hist_cap;
    int hist_start = (buf->history_write[env_idx] + hist_cap - count) % hist_cap;
    int base = seg * seg_cap;
    for (int i = 0; i < usable; i++) {
        int src = (hist_start + i) % hist_cap;
        int source = buf->history_source[hist_base + src];
        int slot = base + i;
        if (source >= 0 && source < buf->candidate_capacity) {
            buf->state_update_source[slot] = source;
        } else {
            curriculum_set_slot_priority_from_source(buf, slot, source, 0);
        }
    }
}

static inline int curriculum_replace_cl_tail(
        StateBuffer* buf, int env_idx, float terminal_score, float gamma) {
    int sampled_slot = buf->cl_start_slot[env_idx];
    int seg_cap = buf->segment_capacity;
    int count = buf->history_count[env_idx];
    if (sampled_slot < 0 || sampled_slot >= buf->size
            || seg_cap <= 0 || count <= 0) {
        return 0;
    }
    int seg = sampled_slot / seg_cap;
    int offset = sampled_slot - seg * seg_cap;
    if (seg < 0 || seg >= buf->segment_count
            || offset < 0 || offset >= buf->segment_len[seg]
            || offset + count > seg_cap) {
        return 0;
    }
    float old_return = buf->state_return[sampled_slot];
    if (isnan(old_return) || isinf(old_return)) {
        old_return = 0.0f;
    }
    float candidate_return = curriculum_history_discounted_return(
        buf, env_idx, count, terminal_score, gamma);
    int improves_segment_return =
        terminal_score > buf->segment_return[seg] + 1e-6f;
    int improves_tail_return =
        !isnan(candidate_return) && !isinf(candidate_return)
        && candidate_return > old_return + 1e-6f;
    if (!improves_segment_return && !improves_tail_return) {
        return 0;
    }

    int hist_cap = buf->history_capacity;
    int hist_base = env_idx * hist_cap;
    int hist_start = (buf->history_write[env_idx] + hist_cap - count) % hist_cap;
    PufferState* hist_states = buf->history_states + hist_base;
    int base = seg * seg_cap;
    for (int i = 0; i < count; i++) {
        int src = (hist_start + i) % hist_cap;
        int slot = base + offset + i;
        int source = buf->history_source[hist_base + src];
        buf->states[slot] = hist_states[src];
        buf->state_return[slot] = curriculum_history_discounted_return_from(
            buf, env_idx, count, i, terminal_score, gamma);
        curriculum_set_slot_priority_from_source(buf, slot, source, 1);
        buf->priorities_host[slot] = from_float(0.0f);
    }
    int old_len = buf->segment_len[seg];
    int new_len = offset + count;
    for (int slot = base + new_len; slot < base + old_len; slot++) {
        buf->state_return[slot] = 0.0f;
        buf->state_priority[slot] = 0.0f;
        buf->state_update_source[slot] = -1;
        buf->priorities_host[slot] = from_float(0.0f);
    }
    buf->segment_len[seg] = new_len;
    if (terminal_score > buf->segment_score[seg]) {
        buf->segment_score[seg] = terminal_score;
    }
    if (terminal_score > buf->segment_return[seg]) {
        buf->segment_return[seg] = terminal_score;
    }
    curriculum_update_segment_priority_from_slots(buf, seg);
    curriculum_refresh_sample_priorities(buf);
    return 1;
}

static inline int curriculum_capture_state(
        StateBuffer* buf, int env_idx, Env* env,
        float reward, float terminal, int source, float gamma) {
    if (buf->history_capacity <= 0 || env == NULL) {
        return 0;
    }
    const PufferState* state = &env->state;
    const PufferState* last = curriculum_last_history_state(buf, env_idx);
    if (last == NULL) {
        curriculum_reset_history(buf, env_idx, state, source);
        return 0;
    }

    if (terminal > 0.5f) {
        float terminal_return = fmaxf(state->episode_return,
            last->episode_return + reward);
        if (env_idx >= buf->num_fresh_envs) {
            int resampled = 0;
            curriculum_diag_cl_terminal(buf, env_idx, reward);
#ifdef _OPENMP
#pragma omp critical(puffer_curriculum_tail)
#endif
            {
                if (buf->cl_head_sample[env_idx]) {
                    int sampled_slot = buf->cl_start_slot[env_idx];
                    int seg_cap = buf->segment_capacity;
                    int count = buf->history_count[env_idx];
                    if (sampled_slot >= 0 && sampled_slot < buf->size
                            && seg_cap > 0 && (sampled_slot % seg_cap) == 0) {
                        int seg = sampled_slot / seg_cap;
                        if (seg >= 0 && seg < buf->segment_count) {
                            float old_return = buf->state_return[sampled_slot];
                            float candidate_return =
                                curriculum_history_discounted_return(
                                    buf, env_idx, count, terminal_return, gamma);
                            int equal_or_better_segment_return =
                                terminal_return + 1e-6f
                                >= buf->segment_return[seg];
                            if (!isnan(candidate_return) && !isinf(candidate_return)
                                    && (equal_or_better_segment_return
                                        || candidate_return + 1e-6f >= old_return)) {
                                curriculum_queue_head_priority_update(
                                    buf, env_idx, seg, count);
                            }
                        }
                    }
                }
                int replaced = curriculum_replace_cl_tail(
                    buf, env_idx, terminal_return, gamma);
                if (replaced) {
                    curriculum_diag_cl_tail_replaced(buf);
                }
                buf->cl_start_slot[env_idx] = -1;
                buf->cl_head_sample[env_idx] = 0;
                resampled = curriculum_resample_cl_env(buf, env, env_idx, source);
            }
            if (resampled) {
                return 1;
            }
        } else {
            curriculum_diag_fresh_terminal(buf, env);
            curriculum_save_candidate_history(buf, env_idx, terminal_return);
        }
        curriculum_reset_history(buf, env_idx, state, source);
        return 0;
    }

    curriculum_push_history(buf, env_idx, state, source);
    return 0;
}

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
    if (checkpoint_idx < 0 || checkpoint_idx >= buf->num_checkpoints) {
        return;
    }

    StaticVec* vec = pufferl->vec;
    int env_start = vec->buffer_env_starts[buffer_idx];
    int env_end = env_start + vec->buffer_env_counts[buffer_idx];
    if (env_end > buf->num_envs) {
        env_end = buf->num_envs;
    }
    PufferState* dst = buf->checkpoint_states + checkpoint_idx * buf->num_envs;
    for (int env_idx = env_start; env_idx < env_end; env_idx++) {
        int source = checkpoint_idx * buf->num_envs + env_idx;
        Env* env = &vec->envs[env_idx];
        dst[env_idx] = env->state;
        curriculum_diag_source_snapshot(pufferl, source, &env->state);
        int resampled = curriculum_capture_state(
            buf, env_idx, env, env->rewards[0], env->terminals[0],
            source, pufferl->hypers.gamma);
        if (resampled) {
            curriculum_copy_env_obs_to_gpu(vec, buf, env_idx);
        }
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
    int total_epochs = h->total_timesteps / (h->total_agents * h->horizon);
    float progress = total_epochs > 0 ? (float)pufferl->epoch / (float)total_epochs : 1.0f;
    progress = fminf(1.0f, fmaxf(0.0f, progress));
    float current_cl_frac = h->cl_frac;
    if (h->anneal_cl) {
        current_cl_frac *= 1.0f - progress;
    }

    int warmed = buf->segment_count > 0 && buf->size >= h->warmup_states;
    int num_cl_envs = warmed
        ? clamp_int((int)(current_cl_frac * (float)total_envs), 0, total_envs)
        : 0;
    int num_fresh_envs = total_envs - num_cl_envs;
    buf->num_cl_envs = num_cl_envs;
    buf->num_fresh_envs = num_fresh_envs;
    vec->log_env_limit = (num_cl_envs > 0) ? num_fresh_envs : 0;
    fill_precision_kernel<<<grid_size(total_agents), BLOCK_SIZE, 0, stream>>>(
        buf->importance.data, from_float(1.0f), total_agents);
    curriculum_diag_rollout_begin(pufferl);

    if (num_cl_envs <= 0) {
        return;
    }

    Env* envs = vec->envs;
    int started = 0;
    for (int i = 0; i < num_cl_envs; i++) {
        int env_idx = num_fresh_envs + i;
        unsigned int salt = ((unsigned int)pufferl->epoch * 1000003U)
            ^ ((unsigned int)env_idx * 9176U)
            ^ (unsigned int)i;
        int sampled_slot = curriculum_sample_slot(buf, salt);
        int prefer_head = (i & 1) == 0;
        if (curriculum_start_cl_env_from_slot(
                buf, &envs[env_idx], env_idx, sampled_slot, prefer_head)) {
            started++;
        }
    }
    if (started <= 0) {
        buf->num_cl_envs = 0;
        buf->num_fresh_envs = total_envs;
        vec->log_env_limit = 0;
        return;
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
        cudaMemcpy(vec->gpu_observations.data, vec->observations.data,
            (size_t)vec->total_agents * get_obs_size() * get_obs_elem_size(),
            cudaMemcpyHostToDevice);
        if (vec->action_mask_size > 0) {
            cudaMemcpy(vec->gpu_action_mask, vec->action_mask,
                (size_t)vec->total_agents * vec->action_mask_size * sizeof(unsigned char),
                cudaMemcpyHostToDevice);
        }
        cudaStreamSynchronize(stream);
    }
}

void curriculum_update_advantages(PuffeRL* pufferl, PrecisionTensor* advantages,
        PrecisionTensor* entropy, cudaStream_t stream) {
    (void)entropy;
    StateBuffer* buf = &pufferl->state_buf;
    int horizon = advantages->shape[1];
    int checkpoint_rows = buf->num_checkpoints * buf->num_envs;
    precision_t* debug_dst = curriculum_diag_debug_device(buf);
    compute_curriculum_checkpoint_scores<<<grid_size(checkpoint_rows), BLOCK_SIZE, 0, stream>>>(
        buf->env_scores.data, debug_dst, advantages->data,
        pufferl->train_rollouts.values.data,
        pufferl->train_rollouts.rewards.data,
        pufferl->train_rollouts.terminals.data,
        buf->num_envs, buf->num_checkpoints,
        buf->checkpoint_interval, buf->agents_per_env,
        pufferl->hypers.gamma, horizon);
    cudaMemcpyAsync(buf->env_scores_host, buf->env_scores.data,
        (size_t)checkpoint_rows * sizeof(precision_t), cudaMemcpyDeviceToHost, stream);
    curriculum_diag_copy_checkpoint_debug(buf, checkpoint_rows, stream);
    cudaStreamSynchronize(stream);

    curriculum_apply_deferred_priority_updates(buf);
    long agent_step = pufferl->global_step * (long)pufferl->hypers.world_size;
    int has_candidate = curriculum_select_pending_candidate(buf, agent_step);
    int stored = 0;
    if (has_candidate) {
        stored = curriculum_store_pending_segment(buf, agent_step, pufferl->hypers.gamma);
    }
    curriculum_diag_candidate_selected(buf, 0, 0.0f, 0.0f, 0.0f, 0.0f,
        buf->pending_env_idx, stored);
    curriculum_clear_candidates(buf);
    curriculum_refresh_sample_priorities(buf);
    if (buf->size > 0) {
        cudaMemcpyAsync(buf->advantages.data, buf->priorities_host,
            (size_t)buf->size * sizeof(precision_t), cudaMemcpyHostToDevice, stream);
    }
}

#endif
