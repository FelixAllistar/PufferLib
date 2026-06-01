// Curriculum state-set and prioritized replay implementation.
// Included from pufferlib.cu in two phases so the buffer types are
// visible inside PuffeRL while functions that dereference PuffeRL see
// the complete struct definition.

#include <cub/device/device_scan.cuh>

#ifndef PUFFER_CURRICULUM_DIAG_BINS
#define PUFFER_CURRICULUM_DIAG_BINS 16
#endif

#ifndef PUFFER_CURRICULUM_CL_BINS
#define PUFFER_CURRICULUM_CL_BINS 4
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
    int agents_per_env;
    int num_cl_envs;
    int num_fresh_envs;
    int oracle_saved_level;
    int* env_state_inds_host;  // CPU scratch, length num_envs
    PrecisionTensor advantages; // GPU, shape {state_buffer_size}
    PrecisionTensor env_scores; // GPU scratch, shape {candidate_capacity}
    PrecisionTensor importance; // GPU, shape {total_agents}; fresh=1, CL=PER IS weight
    PrioBuffers prio_bufs;      // GPU CDF/weights/idx/importance for curriculum
#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
    int* state_outcomes;        // CPU outcome bucket per persistent slot
    long diag_retained_outcome[PUFFER_CURRICULUM_DIAG_BINS];
    float diag_retained_priority_sum[PUFFER_CURRICULUM_DIAG_BINS];
    int oracle_saved_pick_t;
    int oracle_saved_first_t;
    int oracle_saved_last_t;
    int oracle_saved_full;
    int oracle_saved_mastered;
    long oracle_saved_agent_step;
    int oracle_hist_capacity;
    PufferState* oracle_hist_states;
    int* oracle_hist_count;
    int* oracle_hist_write;
    int* oracle_hist_level;
    int* oracle_hist_from_entry;
    PufferState* oracle_pending_states;
    int oracle_pending_level;
    int oracle_pending_count;
    int oracle_pending_first_t;
    int oracle_pending_last_t;
    int oracle_pending_full;
    int* oracle_cl_start_bucket;
    int* oracle_cl_start_slot;
    int oracle_sample_start;
    long long oracle_window_attempts;
    long long oracle_window_successes;
    long long oracle_start_attempts;
    long long oracle_start_successes;
    long long oracle_cl_attempts[PUFFER_CURRICULUM_CL_BINS];
    long long oracle_cl_successes[PUFFER_CURRICULUM_CL_BINS];
    long long oracle_fresh_attempts;
    long long oracle_fresh_successes;
    long long oracle_random_attempts;
    long long oracle_random_successes;
#endif
};

void register_state_buffer(StateBuffer* buf, Allocator* alloc,
        int capacity, int total_agents, int num_envs, int agents_per_env,
        int num_cl_envs, int horizon, int checkpoint_interval) {
    buf->capacity = capacity;
    buf->size = 0;
    buf->num_envs = num_envs;
    buf->oracle_saved_level = -1;
    buf->checkpoint_interval = checkpoint_interval;
    buf->num_checkpoints = (horizon + checkpoint_interval - 1) / checkpoint_interval;
    buf->candidate_capacity = num_envs * buf->num_checkpoints;
    buf->score_capacity = buf->candidate_capacity + num_envs;
    buf->agents_per_env = agents_per_env;
    buf->advantages = {.shape = {capacity}};
    buf->env_scores = {.shape = {buf->score_capacity}};
    buf->importance = {.shape = {total_agents}};
    alloc_register(alloc, &buf->advantages);
    alloc_register(alloc, &buf->env_scores);
    alloc_register(alloc, &buf->importance);
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
    buf->oracle_saved_pick_t = -1;
    buf->oracle_saved_first_t = -1;
    buf->oracle_saved_last_t = -1;
    buf->oracle_saved_full = 0;
    buf->oracle_saved_mastered = 0;
    buf->oracle_saved_agent_step = -1;
    buf->oracle_hist_capacity = buf->capacity < 256 ? buf->capacity : 256;
    buf->oracle_pending_level = -1;
    buf->oracle_pending_count = 0;
    buf->oracle_pending_first_t = -1;
    buf->oracle_pending_last_t = -1;
    buf->oracle_pending_full = 0;
    buf->oracle_sample_start = 0;
    buf->oracle_window_attempts = 0;
    buf->oracle_window_successes = 0;
    buf->oracle_start_attempts = 0;
    buf->oracle_start_successes = 0;
    buf->oracle_fresh_attempts = 0;
    buf->oracle_fresh_successes = 0;
    buf->oracle_random_attempts = 0;
    buf->oracle_random_successes = 0;
    memset(buf->oracle_cl_attempts, 0, sizeof(buf->oracle_cl_attempts));
    memset(buf->oracle_cl_successes, 0, sizeof(buf->oracle_cl_successes));
    buf->state_outcomes = (int*)malloc(capacity * sizeof(int));
    buf->oracle_hist_states = (PufferState*)malloc(
        (size_t)buf->num_envs * buf->oracle_hist_capacity * sizeof(PufferState));
    buf->oracle_hist_count = (int*)malloc((size_t)buf->num_envs * sizeof(int));
    buf->oracle_hist_write = (int*)malloc((size_t)buf->num_envs * sizeof(int));
    buf->oracle_hist_level = (int*)malloc((size_t)buf->num_envs * sizeof(int));
    buf->oracle_hist_from_entry = (int*)malloc((size_t)buf->num_envs * sizeof(int));
    buf->oracle_cl_start_bucket = (int*)malloc((size_t)buf->num_envs * sizeof(int));
    buf->oracle_cl_start_slot = (int*)malloc((size_t)buf->num_envs * sizeof(int));
    buf->oracle_pending_states = (PufferState*)malloc(
        (size_t)buf->oracle_hist_capacity * sizeof(PufferState));
#endif
    if (buf->states == NULL || buf->candidate_states == NULL
            || buf->priorities == NULL || buf->priorities_host == NULL
            || buf->env_scores_host == NULL || buf->heap == NULL || buf->heap_pos == NULL
            || buf->env_state_inds_host == NULL
#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
            || buf->state_outcomes == NULL
            || buf->oracle_hist_states == NULL
            || buf->oracle_hist_count == NULL
            || buf->oracle_hist_write == NULL
            || buf->oracle_hist_level == NULL
            || buf->oracle_hist_from_entry == NULL
            || buf->oracle_cl_start_bucket == NULL
            || buf->oracle_cl_start_slot == NULL
            || buf->oracle_pending_states == NULL
#endif
            ) {
        fprintf(stderr,
            "Failed to allocate curriculum state buffer: capacity=%d state_size=%d bytes=%zu\n",
            buf->capacity, (int)state_size, state_bytes);
        return 0;
    }
#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
    memset(buf->state_outcomes, 0, capacity * sizeof(int));
    memset(buf->diag_retained_outcome, 0, sizeof(buf->diag_retained_outcome));
    memset(buf->diag_retained_priority_sum, 0, sizeof(buf->diag_retained_priority_sum));
    memset(buf->oracle_hist_count, 0, (size_t)buf->num_envs * sizeof(int));
    memset(buf->oracle_hist_write, 0, (size_t)buf->num_envs * sizeof(int));
    for (int i = 0; i < buf->num_envs; i++) {
        buf->oracle_hist_level[i] = -1;
        buf->oracle_hist_from_entry[i] = 0;
        buf->oracle_cl_start_bucket[i] = -1;
        buf->oracle_cl_start_slot[i] = -1;
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
    free(buf->state_outcomes);
    free(buf->oracle_hist_states);
    free(buf->oracle_hist_count);
    free(buf->oracle_hist_write);
    free(buf->oracle_hist_level);
    free(buf->oracle_hist_from_entry);
    free(buf->oracle_cl_start_bucket);
    free(buf->oracle_cl_start_slot);
    free(buf->oracle_pending_states);
#endif
}

#endif

#ifdef PUFFER_CURRICULUM_IMPL

#define PRIO_WARP_SIZE 32
#define PRIO_FULL_MASK 0xffffffff
#define PRIO_BLOCK_SIZE 256
#define PRIO_NUM_WARPS (PRIO_BLOCK_SIZE / PRIO_WARP_SIZE)

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
    buf->oracle_random_attempts = 0;
    buf->oracle_random_successes = 0;
    for (int i = 0; i < buf->num_envs; i++) {
        buf->oracle_cl_start_bucket[i] = -1;
        buf->oracle_cl_start_slot[i] = -1;
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
    for (int i = 0; i < buf->size; i++) {
        float priority = 1.0f;
        buf->priorities[i] = priority;
        buf->priorities_host[i] = from_float(priority);
    }
}

static inline void curriculum_oracle_maybe_expand_window(StateBuffer* buf) {
    (void)buf;
}

static inline int curriculum_diag_candidate_outcome(
        StateBuffer* buf, int checkpoint_idx, int env_idx) {
    int candidate_idx = checkpoint_idx * buf->num_envs + env_idx;
    const PufferState* start = &buf->candidate_states[candidate_idx];
    int outcome = start->sequence_pos;
    int prev_tick = start->tick;
    int prev_sequence_pos = start->sequence_pos;
    int prev_maps_solved = start->episode_maps_solved;

    for (int c = checkpoint_idx + 1; c < buf->num_checkpoints; c++) {
        const PufferState* later = &buf->candidate_states[c * buf->num_envs + env_idx];
        if (later->tick < prev_tick
                || later->sequence_pos < prev_sequence_pos
                || later->episode_maps_solved < prev_maps_solved) {
            break;
        }
        if (later->sequence_pos > outcome) {
            outcome = later->sequence_pos;
        }
        prev_tick = later->tick;
        prev_sequence_pos = later->sequence_pos;
        prev_maps_solved = later->episode_maps_solved;
    }

    return curriculum_diag_bucket(outcome);
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
        const precision_t* __restrict__ advantages_bt,
        int num_envs, int num_fresh_envs, int num_cl_envs,
        int num_checkpoints, int checkpoint_interval,
        int agents_per_env, int horizon) {
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
    for (int a = 0; a < agents_per_env; a++) {
        int offset = (agent_start + a) * horizon;
        // GAE already carries forward-looking credit, so score the checkpoint
        // state/action directly instead of aggregating the remaining rollout.
        float agent_adv = start_t < horizon - 1
            ? to_float(advantages_bt[offset + start_t])
            : 0.0f;
        sum_agent_adv += agent_adv;
    }
    dst[row] = from_float(sum_agent_adv / (float)agents_per_env);
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

static inline float clean_state_priority(float priority) {
    if (priority < 0.0f || isnan(priority) || isinf(priority)) {
        return 0.0f;
    }
    return priority;
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

static inline void state_heap_update_slot(StateBuffer* buf, int slot, float priority,
        float decay) {
    priority = clean_state_priority(priority);
    float old_priority = buf->priorities[slot];
    if (priority < old_priority) {
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
        return 1;
    }

    int min_slot = buf->heap[0];
    if (priority <= buf->priorities[min_slot]) {
        return 0;
    }
    buf->states[min_slot] = *state;
    buf->priorities[min_slot] = priority;
    buf->priorities_host[min_slot] = from_float(priority);
#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
    buf->state_outcomes[min_slot] = curriculum_diag_bucket(outcome);
#endif
    state_heap_sift_down(buf, 0);
    return 1;
}

#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
static inline int curriculum_oracle_segment_better(
        int level, int count, int full, int last_t,
        int ref_level, int ref_count, int ref_full, int ref_last_t) {
    if (level < 0 || level >= BOXOBAN_LEVEL_LOGS || count <= 0) {
        return 0;
    }
    if (level > ref_level) {
        return 1;
    }
    if (level < ref_level) {
        return 0;
    }
    if (full && !ref_full) {
        return 1;
    }
    if (full == ref_full && full && last_t >= 0 && ref_last_t >= 0
            && last_t < ref_last_t) {
        return 1;
    }
    if (full == ref_full && !full && count > ref_count) {
        return 1;
    }
    return 0;
}

static inline int curriculum_oracle_current_promotable(StateBuffer* buf) {
    return buf->oracle_saved_level >= 0 && buf->size > 0
        && buf->oracle_saved_mastered;
}

static inline int curriculum_oracle_history_improves(
        StateBuffer* buf, int level, int count, int full, int last_t) {
    return curriculum_oracle_segment_better(
        level, count, full, last_t,
        buf->oracle_saved_level, buf->size,
        buf->oracle_saved_full, buf->oracle_saved_last_t);
}

static inline int curriculum_oracle_pending_can_replace_saved(StateBuffer* buf) {
    if (buf->oracle_pending_level <= buf->oracle_saved_level) {
        return 1;
    }
    if (buf->oracle_saved_level < 0 || buf->size <= 0) {
        return 1;
    }
    return buf->oracle_saved_mastered;
}

static inline void curriculum_oracle_push_history(
        StateBuffer* buf, int env_idx, const PufferState* state) {
    int cap = buf->oracle_hist_capacity;
    int base = env_idx * cap;
    int write = buf->oracle_hist_write[env_idx];
    buf->oracle_hist_states[base + write] = *state;
    write = (write + 1) % cap;
    buf->oracle_hist_write[env_idx] = write;
    if (buf->oracle_hist_count[env_idx] < cap) {
        buf->oracle_hist_count[env_idx]++;
    }
}

static inline void curriculum_oracle_reset_history(
        StateBuffer* buf, int env_idx, const PufferState* state) {
    buf->oracle_hist_count[env_idx] = 0;
    buf->oracle_hist_write[env_idx] = 0;
    buf->oracle_hist_level[env_idx] = state->sequence_pos;
    buf->oracle_hist_from_entry[env_idx] = state->puzzle_tick <= 1;
    curriculum_oracle_push_history(buf, env_idx, state);
    if (state->sequence_pos == buf->oracle_saved_level
            && state->puzzle_tick <= 1
            && env_idx < buf->num_fresh_envs) {
        int monitor_envs = curriculum_oracle_monitor_envs(buf);
        if (env_idx < monitor_envs) {
            __sync_fetch_and_add(&buf->oracle_fresh_attempts, 1);
        } else {
            __sync_fetch_and_add(&buf->oracle_random_attempts, 1);
        }
    }
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

static inline void curriculum_oracle_publish_history(StateBuffer* buf, int env_idx) {
    int level = buf->oracle_hist_level[env_idx];
    int count = buf->oracle_hist_count[env_idx];
    int full = buf->oracle_hist_from_entry[env_idx];
    int last_t = -1;
    if (count > 0) {
        const PufferState* last = curriculum_oracle_last_history_state(buf, env_idx);
        last_t = last != NULL ? last->puzzle_tick : -1;
    }
    if (count <= 0 || !buf->oracle_hist_from_entry[env_idx]
            || !curriculum_oracle_segment_better(
                level, count, full, last_t,
                buf->oracle_saved_level, buf->size,
                buf->oracle_saved_full, buf->oracle_saved_last_t)) {
        return;
    }

    int cap = buf->oracle_hist_capacity;
#ifdef _OPENMP
#pragma omp critical(puffer_oracle_segment)
#endif
    {
        int improves_pending = curriculum_oracle_segment_better(
            level, count, full, last_t,
            buf->oracle_pending_level, buf->oracle_pending_count,
            buf->oracle_pending_full, buf->oracle_pending_last_t);
        int improves_saved = curriculum_oracle_segment_better(
            level, count, full, last_t,
            buf->oracle_saved_level, buf->size,
            buf->oracle_saved_full, buf->oracle_saved_last_t);
        if (improves_pending && improves_saved) {
            int base = env_idx * cap;
            int start = (buf->oracle_hist_write[env_idx] + cap - count) % cap;
            for (int i = 0; i < count; i++) {
                int src = (start + i) % cap;
                buf->oracle_pending_states[i] = buf->oracle_hist_states[base + src];
            }
            buf->oracle_pending_level = level;
            buf->oracle_pending_count = count;
            buf->oracle_pending_first_t = buf->oracle_pending_states[0].puzzle_tick;
            buf->oracle_pending_last_t = buf->oracle_pending_states[count - 1].puzzle_tick;
            buf->oracle_pending_full = buf->oracle_hist_from_entry[env_idx];
        }
    }
}

static inline void curriculum_oracle_capture_state(
        StateBuffer* buf, int env_idx, const PufferState* state,
        float reward, float terminal) {
    if (buf->oracle_hist_capacity <= 0) {
        return;
    }

    const PufferState* last = curriculum_oracle_last_history_state(buf, env_idx);
    if (last == NULL || buf->oracle_hist_level[env_idx] < 0) {
        curriculum_oracle_reset_history(buf, env_idx, state);
        return;
    }

    int hist_level = buf->oracle_hist_level[env_idx];
    int solved_transition = state->sequence_pos > hist_level
        && state->episode_maps_solved > last->episode_maps_solved;
    int terminal_solve = terminal > 0.5f && reward > 0.5f;
        if (solved_transition || terminal_solve) {
        if (hist_level == buf->oracle_saved_level) {
            if (env_idx < buf->num_fresh_envs) {
                int monitor_envs = curriculum_oracle_monitor_envs(buf);
                if (env_idx < monitor_envs) {
                    __sync_fetch_and_add(&buf->oracle_fresh_successes, 1);
                } else {
                    __sync_fetch_and_add(&buf->oracle_random_successes, 1);
                }
            } else if (env_idx < buf->num_fresh_envs + buf->num_cl_envs) {
                int bucket = buf->oracle_cl_start_bucket[env_idx];
                int slot = buf->oracle_cl_start_slot[env_idx];
                if (bucket >= 0 && bucket < PUFFER_CURRICULUM_CL_BINS) {
                    __sync_fetch_and_add(&buf->oracle_cl_successes[bucket], 1);
                }
                if (slot == 0) {
                    __sync_fetch_and_add(&buf->oracle_start_successes, 1);
                }
                if (slot >= 0 && slot < buf->size) {
                    __sync_fetch_and_add(&buf->oracle_window_successes, 1);
                }
                buf->oracle_cl_start_bucket[env_idx] = -1;
                buf->oracle_cl_start_slot[env_idx] = -1;
            }
        }
        curriculum_oracle_publish_history(buf, env_idx);
        curriculum_oracle_reset_history(buf, env_idx, state);
        return;
    }

    int discontinuity = state->tick < last->tick
        || state->puzzle_tick < last->puzzle_tick
        || state->sequence_pos != hist_level
        || state->episode_maps_solved < last->episode_maps_solved;
    if (discontinuity) {
        if (env_idx >= buf->num_fresh_envs
                && env_idx < buf->num_fresh_envs + buf->num_cl_envs) {
            buf->oracle_cl_start_bucket[env_idx] = -1;
            buf->oracle_cl_start_slot[env_idx] = -1;
        }
        curriculum_oracle_reset_history(buf, env_idx, state);
        return;
    }

    curriculum_oracle_push_history(buf, env_idx, state);
}

static inline void state_buffer_overwrite_pending_segment(StateBuffer* buf, long agent_step) {
    int count = buf->oracle_pending_count;
    if (count <= 0 || count > buf->capacity) {
        return;
    }

    int level = buf->oracle_pending_level;
    buf->size = count;
    buf->oracle_saved_level = level;
    buf->oracle_saved_pick_t = buf->oracle_pending_last_t;
    buf->oracle_saved_first_t = buf->oracle_pending_first_t;
    buf->oracle_saved_last_t = buf->oracle_pending_last_t;
    buf->oracle_saved_full = buf->oracle_pending_full;
    buf->oracle_saved_mastered = 0;
    buf->oracle_saved_agent_step = agent_step;
    buf->oracle_sample_start = 0;
    curriculum_oracle_reset_cl_counters(buf);
    for (int i = 0; i < count; i++) {
        buf->states[i] = buf->oracle_pending_states[i];
        buf->heap[i] = i;
        buf->heap_pos[i] = i;
        buf->state_outcomes[i] = curriculum_diag_bucket(level);
    }
    curriculum_oracle_refresh_sample_priorities(buf);
    curriculum_diag_recount_retained(buf);
}

static inline int curriculum_try_oracle_solve_segment(PuffeRL* pufferl) {
    StateBuffer* buf = &pufferl->state_buf;
    if (!curriculum_oracle_history_improves(
            buf, buf->oracle_pending_level, buf->oracle_pending_count,
            buf->oracle_pending_full, buf->oracle_pending_last_t)) {
        return 0;
    }
    if (!curriculum_oracle_pending_can_replace_saved(buf)) {
        return 0;
    }
    long agent_step = pufferl->global_step * (long)pufferl->hypers.world_size;
    state_buffer_overwrite_pending_segment(buf, agent_step);
    return 1;
}
#endif

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
    for (int env_idx = env_start; env_idx < env_end; env_idx++) {
        dst[env_idx] = envs[env_idx].state;
#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
        if (pufferl->hypers.frontier_explore) {
            Env* env = &envs[env_idx];
            curriculum_oracle_capture_state(
                buf, env_idx, &env->state, env->rewards[0], env->terminals[0]);
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
    if (h->frontier_explore && pufferl->frontier_random_row_mask_host != NULL) {
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
    fill_precision_kernel<<<grid_size(fresh_agents), BLOCK_SIZE, 0, stream>>>(
        buf->importance.data, from_float(1.0f), fresh_agents);

    if (num_cl_envs > 0) {
        compute_prio_abs<<<grid_size(buf->size), BLOCK_SIZE, 0, stream>>>(
            buf->advantages.data, buf->prio_bufs.prio_weights.data,
            h->explore_alpha, 0.0f, buf->size, 1);
        long* rng_offset = pufferl->rng_offset_puf.data + h->num_buffers + 1;
        sample_prio_indices(&buf->prio_bufs, buf->size, num_cl_envs,
            pufferl->seed, rng_offset, NULL, buf->importance.data,
            fresh_agents, agents_per_env, h->explore_beta, stream);
        cudaMemcpyAsync(buf->env_state_inds_host + num_fresh_envs, buf->prio_bufs.idx.data,
            num_cl_envs * sizeof(int), cudaMemcpyDeviceToHost, stream);
        cudaStreamSynchronize(stream);

        Env* envs = vec->envs;
        int* state_inds = buf->env_state_inds_host + num_fresh_envs;
        for (int i = 0; i < num_cl_envs; i++) {
            int sampled_slot = state_inds[i];
            const PufferState* sampled_state = &buf->states[sampled_slot];
            int env_idx = num_fresh_envs + i;
            Env* env = &envs[env_idx];
            env->state = *sampled_state;
            puffer_state_refresh(env);
#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
            if (h->frontier_explore) {
                buf->oracle_cl_start_bucket[env_idx] = -1;
                buf->oracle_cl_start_slot[env_idx] = -1;
                if (sampled_state->sequence_pos == buf->oracle_saved_level
                        && buf->oracle_saved_last_t >= 0) {
                    int remaining = buf->oracle_saved_last_t - sampled_state->puzzle_tick;
                    if (remaining < 0) {
                        remaining = 0;
                    }
                    int bucket = curriculum_oracle_cl_bucket(remaining);
                    buf->oracle_cl_start_bucket[env_idx] = bucket;
                    buf->oracle_cl_start_slot[env_idx] = sampled_slot;
                    buf->oracle_cl_attempts[bucket]++;
                    if (sampled_slot == 0) {
                        buf->oracle_start_attempts++;
                    }
                    if (sampled_slot >= 0 && sampled_slot < buf->size) {
                        buf->oracle_window_attempts++;
                    }
                }
                curriculum_oracle_reset_history(buf, env_idx, sampled_state);
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
        cudaStream_t stream) {
    StateBuffer* buf = &pufferl->state_buf;
    if (pufferl->hypers.frontier_explore) {
#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
        curriculum_try_oracle_solve_segment(pufferl);
        curriculum_oracle_maybe_expand_window(buf);
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
        buf->env_scores.data, advantages->data, num_envs,
        num_fresh_envs, num_cl_envs, buf->num_checkpoints,
        buf->checkpoint_interval, agents_per_env, horizon);
    int cl_score_offset = checkpoint_rows;
    cudaMemcpyAsync(buf->env_scores_host, buf->env_scores.data,
        (size_t)score_rows * sizeof(precision_t), cudaMemcpyDeviceToHost, stream);
    cudaStreamSynchronize(stream);

    for (int i = 0; i < num_cl_envs; i++) {
        int env_idx = num_fresh_envs + i;
        int slot = buf->env_state_inds_host[env_idx];
        float priority = to_float(buf->env_scores_host[cl_score_offset + i]);
#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
        int outcome = curriculum_diag_candidate_outcome(buf, 0, env_idx);
        if (outcome > buf->state_outcomes[slot]) {
            buf->state_outcomes[slot] = outcome;
        }
#endif
        state_heap_update_slot(buf, slot, priority, pufferl->hypers.explore_decay);
    }

    for (int c = 0; c < buf->num_checkpoints; c++) {
        int candidate_offset = c * buf->num_envs;
        for (int i = 0; i < num_envs; i++) {
            int candidate_idx = candidate_offset + i;
            PufferState* candidate_state = &buf->candidate_states[candidate_idx];
            float priority = clean_state_priority(to_float(buf->env_scores_host[candidate_idx]));
#ifdef PUFFER_CURRICULUM_DIAG_SEQUENCE_OUTCOME
            int outcome = curriculum_diag_candidate_outcome(buf, c, i);
            state_heap_insert(buf, candidate_state, priority, outcome);
#else
            state_heap_insert(buf, candidate_state, priority);
#endif
        }
    }

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
        dict_set(out, "seg", (double)(buf->oracle_saved_level + 1));
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
        dict_set(out, "pend", (double)(buf->oracle_pending_level + 1));
        dict_set(out, "pend_n", (double)buf->oracle_pending_count);
        dict_set(out, "pend_t1", (double)buf->oracle_pending_last_t);
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
        dict_set(out, "nf_n", (double)buf->oracle_fresh_attempts);
        dict_set(out, "nf_sr", buf->oracle_fresh_attempts > 0
            ? (double)buf->oracle_fresh_successes /
                (double)buf->oracle_fresh_attempts : 0.0);
        dict_set(out, "rf_n", (double)buf->oracle_random_attempts);
        dict_set(out, "rf_sr", buf->oracle_random_attempts > 0
            ? (double)buf->oracle_random_successes /
                (double)buf->oracle_random_attempts : 0.0);
    }
}
#endif

#endif
