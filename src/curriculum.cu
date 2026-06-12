// Reward-only best-trajectory curriculum implementation.

#include <cub/device/device_scan.cuh>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define PRIO_WARP_SIZE 32
#define PRIO_FULL_MASK 0xffffffff
#define PRIO_BLOCK_SIZE 256

#ifdef PUFFER_CURRICULUM_TYPES

struct PrioBuffers {
    FloatTensor prio_weights, cdf;
    ByteTensor cdf_temp;
    PrecisionTensor mb_prio;
    IntTensor idx, sample_done;
};

enum CurriculumRole {
    CURRICULUM_ROLE_VANILLA = 0,
    CURRICULUM_ROLE_FRESH = 1,
    CURRICULUM_ROLE_CL = 2,
};

struct StateBuffer {
    PufferState* best_states;
    PufferState* active_states;
    float* best_state_return;
    float* active_state_return;
    int* best_state_step;
    int* active_state_step;
    int* best_valid;
    int* best_len;
    int* best_episode_len;
    float* best_return;
    int* active_role;
    int* active_best_slot;
    int* active_sample_offset;
    int* active_history_count;
    int* active_segment_start;
    float* active_episode_return;
    float* active_prefix_return;
    int* active_episode_len;
    int* active_prefix_step;
    long* active_last_observed_step;
    long long* best_generation;
    long long* active_best_generation;
    int* row_segment_count;
    int* segment_role;
    int* segment_best_slot;
    int* segment_sample_offset;
    int* segment_start;
    int* segment_count;
    float* segment_return;
    float* segment_prefix_return;
    int* segment_episode_len;
    int* segment_prefix_step;
    long long* segment_best_generation;
    int agents_per_env;
    int max_active_envs;
    int max_segments_per_row;
    int num_start_states;
    int trajectory_max_len;
    int checkpoint_interval;
    int num_vanilla_envs;
    int num_fresh_envs;
    int num_cl_envs;
    int seeded;
};

void close_state_buffer(StateBuffer* buf);

void register_state_buffer(StateBuffer* buf,
        int num_envs, int agents_per_env, int max_active_envs,
        int num_start_states, int trajectory_max_len, int checkpoint_interval,
        int rollout_horizon) {
    if (num_start_states < 1) {
        num_start_states = 1;
    }
    if (trajectory_max_len < 1) {
        trajectory_max_len = 1;
    }
    if (rollout_horizon < 1) {
        rollout_horizon = 1;
    }
    // curriculum_clamp_int
    max_active_envs = max_active_envs < 1 ? 1
        : (max_active_envs > num_envs ? num_envs : max_active_envs);

    buf->agents_per_env = agents_per_env;
    buf->max_active_envs = max_active_envs;
    buf->max_segments_per_row = rollout_horizon;
    buf->num_start_states = num_start_states;
    buf->trajectory_max_len = trajectory_max_len;
    buf->checkpoint_interval = checkpoint_interval;
    buf->num_vanilla_envs = num_envs;
    buf->num_fresh_envs = 0;
    buf->num_cl_envs = 0;
    buf->seeded = 0;
}

int init_state_buffer(StateBuffer* buf) {
    size_t best_rows = (size_t)buf->num_start_states;
    size_t traj_len = (size_t)buf->trajectory_max_len;
    size_t active_rows = (size_t)buf->max_active_envs;
    size_t segment_entries = active_rows * (size_t)buf->max_segments_per_row;
    size_t best_entries = best_rows * traj_len;
    size_t active_entries = active_rows * traj_len;

    buf->best_states = (PufferState*)calloc(best_entries, sizeof(PufferState));
    buf->active_states = (PufferState*)calloc(active_entries, sizeof(PufferState));
    buf->best_state_return = (float*)calloc(best_entries, sizeof(float));
    buf->active_state_return = (float*)calloc(active_entries, sizeof(float));
    buf->best_state_step = (int*)calloc(best_entries, sizeof(int));
    buf->active_state_step = (int*)calloc(active_entries, sizeof(int));
    buf->best_valid = (int*)calloc(best_rows, sizeof(int));
    buf->best_len = (int*)calloc(best_rows, sizeof(int));
    buf->best_episode_len = (int*)calloc(best_rows, sizeof(int));
    buf->best_return = (float*)calloc(best_rows, sizeof(float));
    buf->active_role = (int*)calloc(active_rows, sizeof(int));
    buf->active_best_slot = (int*)calloc(active_rows, sizeof(int));
    buf->active_sample_offset = (int*)calloc(active_rows, sizeof(int));
    buf->active_history_count = (int*)calloc(active_rows, sizeof(int));
    buf->active_segment_start = (int*)calloc(active_rows, sizeof(int));
    buf->active_episode_return = (float*)calloc(active_rows, sizeof(float));
    buf->active_prefix_return = (float*)calloc(active_rows, sizeof(float));
    buf->active_episode_len = (int*)calloc(active_rows, sizeof(int));
    buf->active_prefix_step = (int*)calloc(active_rows, sizeof(int));
    buf->active_last_observed_step = (long*)calloc(active_rows, sizeof(long));
    buf->best_generation = (long long*)calloc(best_rows, sizeof(long long));
    buf->active_best_generation = (long long*)calloc(active_rows, sizeof(long long));
    buf->row_segment_count = (int*)calloc(active_rows, sizeof(int));
    buf->segment_role = (int*)calloc(segment_entries, sizeof(int));
    buf->segment_best_slot = (int*)calloc(segment_entries, sizeof(int));
    buf->segment_sample_offset = (int*)calloc(segment_entries, sizeof(int));
    buf->segment_start = (int*)calloc(segment_entries, sizeof(int));
    buf->segment_count = (int*)calloc(segment_entries, sizeof(int));
    buf->segment_return = (float*)calloc(segment_entries, sizeof(float));
    buf->segment_prefix_return = (float*)calloc(segment_entries, sizeof(float));
    buf->segment_episode_len = (int*)calloc(segment_entries, sizeof(int));
    buf->segment_prefix_step = (int*)calloc(segment_entries, sizeof(int));
    buf->segment_best_generation = (long long*)calloc(segment_entries, sizeof(long long));

    if (buf->best_states == NULL || buf->active_states == NULL
            || buf->best_state_return == NULL || buf->active_state_return == NULL
            || buf->best_state_step == NULL || buf->active_state_step == NULL
            || buf->best_valid == NULL || buf->best_len == NULL
            || buf->best_episode_len == NULL || buf->best_return == NULL
            || buf->active_role == NULL || buf->active_best_slot == NULL
            || buf->active_sample_offset == NULL
            || buf->active_history_count == NULL
            || buf->active_segment_start == NULL
            || buf->active_episode_return == NULL
            || buf->active_prefix_return == NULL
            || buf->active_episode_len == NULL
            || buf->active_prefix_step == NULL
            || buf->active_last_observed_step == NULL
            || buf->best_generation == NULL
            || buf->active_best_generation == NULL
            || buf->row_segment_count == NULL
            || buf->segment_role == NULL || buf->segment_best_slot == NULL
            || buf->segment_sample_offset == NULL
            || buf->segment_start == NULL || buf->segment_count == NULL
            || buf->segment_return == NULL
            || buf->segment_prefix_return == NULL
            || buf->segment_episode_len == NULL
            || buf->segment_prefix_step == NULL
            || buf->segment_best_generation == NULL) {
        fprintf(stderr,
            "Failed to allocate curriculum trajectory buffer: starts=%d len=%d active=%d state_size=%d\n",
            buf->num_start_states, buf->trajectory_max_len,
            buf->max_active_envs, (int)sizeof(PufferState));
        close_state_buffer(buf);
        return 0;
    }

    for (int i = 0; i < buf->num_start_states; i++) {
        buf->best_return[i] = -3.402823466e+38F;
    }
    for (int row = 0; row < buf->max_active_envs; row++) {
        buf->active_role[row] = CURRICULUM_ROLE_VANILLA;
        buf->active_best_slot[row] = -1;
        buf->active_last_observed_step[row] = -1;
        buf->active_best_generation[row] = -1;
    }

    return 1;
}

void close_state_buffer(StateBuffer* buf) {
    free(buf->best_states);
    free(buf->active_states);
    free(buf->best_state_return);
    free(buf->active_state_return);
    free(buf->best_state_step);
    free(buf->active_state_step);
    free(buf->best_valid);
    free(buf->best_len);
    free(buf->best_episode_len);
    free(buf->best_return);
    free(buf->active_role);
    free(buf->active_best_slot);
    free(buf->active_sample_offset);
    free(buf->active_history_count);
    free(buf->active_segment_start);
    free(buf->active_episode_return);
    free(buf->active_prefix_return);
    free(buf->active_episode_len);
    free(buf->active_prefix_step);
    free(buf->active_last_observed_step);
    free(buf->best_generation);
    free(buf->active_best_generation);
    free(buf->row_segment_count);
    free(buf->segment_role);
    free(buf->segment_best_slot);
    free(buf->segment_sample_offset);
    free(buf->segment_start);
    free(buf->segment_count);
    free(buf->segment_return);
    free(buf->segment_prefix_return);
    free(buf->segment_episode_len);
    free(buf->segment_prefix_step);
    free(buf->segment_best_generation);
}

#endif

#ifdef PUFFER_CURRICULUM_TYPES

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

#endif

#ifdef PUFFER_CURRICULUM_IMPL

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

static inline unsigned int curriculum_mix32(unsigned int x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline int curriculum_best_base(StateBuffer* buf, int slot) {
    return slot * buf->trajectory_max_len;
}

static inline int curriculum_segment_base(StateBuffer* buf, int row) {
    return row * buf->max_segments_per_row;
}

static inline int curriculum_count_valid(StateBuffer* buf) {
    int count = 0;
    for (int i = 0; i < buf->num_start_states; i++) {
        count += buf->best_valid[i] ? 1 : 0;
    }
    return count;
}

static inline void curriculum_seed_best(PuffeRL* pufferl) {
    StateBuffer* buf = &pufferl->state_buf;
    StaticVec* vec = pufferl->vec;
    if (buf->seeded || vec == NULL || vec->size <= 0) {
        return;
    }

    for (int slot = 0; slot < buf->num_start_states; slot++) {
        Env* env = &vec->envs[slot % vec->size];
        PufferState* dst = buf->best_states + curriculum_best_base(buf, slot);
        dst[0] = env->state;
        buf->best_valid[slot] = 1;
        buf->best_len[slot] = 1;
        buf->best_episode_len[slot] = 0;
        buf->best_return[slot] = 0.0f;
        buf->best_state_return[curriculum_best_base(buf, slot)] = 0.0f;
        buf->best_state_step[curriculum_best_base(buf, slot)] = 0;
        buf->best_generation[slot] = 1;
    }
    buf->seeded = 1;
}

static inline unsigned int curriculum_epoch_start_salt(PuffeRL* pufferl,
        int env_idx, int role) {
    if (role == CURRICULUM_ROLE_FRESH) {
        return (unsigned int)(pufferl->seed
            + pufferl->epoch * 4099 + env_idx * 31) ^ 0xb5297a4dU;
    }
    return (unsigned int)(pufferl->seed
        + pufferl->epoch * 65537 + env_idx * 131) ^ 0x68e31da4U;
}

static inline int curriculum_active_row(StateBuffer* buf, int env_idx) {
    int row = env_idx - buf->num_vanilla_envs;
    if (row < 0 || row >= buf->max_active_envs) {
        return -1;
    }
    return row;
}

static inline int curriculum_env_role(StateBuffer* buf, int env_idx) {
    int row = curriculum_active_row(buf, env_idx);
    if (row < 0) {
        return CURRICULUM_ROLE_VANILLA;
    }
    return buf->active_role[row];
}

static inline void curriculum_record_state(StateBuffer* buf, int env_idx,
        const PufferState* state) {
    int row = curriculum_active_row(buf, env_idx);
    if (row < 0 || row >= buf->max_active_envs) {
        return;
    }
    int count = buf->active_history_count[row];
    if (count < 0 || count >= buf->trajectory_max_len) {
        return;
    }
    int base = row * buf->trajectory_max_len + count;
    buf->active_states[base] = *state;
    buf->active_state_return[base] = buf->active_episode_return[row];
    buf->active_state_step[base] = buf->active_episode_len[row];
    buf->active_history_count[row] = count + 1;
}

static inline int curriculum_should_record_checkpoint(StateBuffer* buf, int env_idx) {
    int row = curriculum_active_row(buf, env_idx);
    if (row < 0) {
        return 0;
    }
    int interval = buf->checkpoint_interval;
    if (interval <= 1) {
        return 1;
    }
    int len = buf->active_episode_len[row];
    return len > 0 && (len % interval) == 0;
}

static inline void curriculum_observe_step(StateBuffer* buf, int env_idx,
        Env* env, long step_serial) {
    int row = curriculum_active_row(buf, env_idx);
    if (row < 0) {
        return;
    }
    int role = buf->active_role[row];
    if (role != CURRICULUM_ROLE_FRESH && role != CURRICULUM_ROLE_CL) {
        return;
    }
    if (buf->active_last_observed_step[row] == step_serial) {
        return;
    }
    buf->active_last_observed_step[row] = step_serial;
    float reward = 0.0f;
    for (int a = 0; a < env->num_agents; a++) {
        float value = env->rewards[a];
        if (!isnan(value) && !isinf(value)) {
            reward += value;
        }
    }
    buf->active_episode_return[row] += reward;
    buf->active_episode_len[row] += 1;
}

static inline int curriculum_start_env(PuffeRL* pufferl, int env_idx,
        int role, unsigned int salt, int clear_outputs, int reset_history,
        int copy_to_gpu) {
    StateBuffer* buf = &pufferl->state_buf;
    StaticVec* vec = pufferl->vec;
    int active_row = curriculum_active_row(buf, env_idx);
    if (active_row < 0) {
        return 0;
    }

    int slot = -1;
    int offset = 0;
    float prefix_return = 0.0f;
    int prefix_step = 0;
    long long generation = -1;
    PufferState start_state;

    // curriculum_sample_best_slot
    int valid = curriculum_count_valid(buf);
    if (valid <= 0) {
        return 0;
    }
    int pick = (int)(curriculum_mix32(salt) % (unsigned int)valid);
    for (int i = 0; i < buf->num_start_states; i++) {
        if (!buf->best_valid[i]) {
            continue;
        }
        if (pick == 0) {
            slot = i;
            break;
        }
        pick--;
    }
    if (slot < 0 || !buf->best_valid[slot]) {
        return 0;
    }
    if (role == CURRICULUM_ROLE_CL) {
        // curriculum_sample_offset
        int len = slot >= 0 && slot < buf->num_start_states
            ? buf->best_len[slot] : 0;
        offset = len <= 1
            ? 0
            : (int)(curriculum_mix32(salt ^ 0xa511e9b3U)
                % (unsigned int)len);
    }
    int best_base = curriculum_best_base(buf, slot);
    start_state = buf->best_states[best_base + offset];
    if (role == CURRICULUM_ROLE_CL) {
        prefix_return = buf->best_state_return[best_base + offset];
        prefix_step = buf->best_state_step[best_base + offset];
    }
    generation = buf->best_generation[slot];

    Env* env = &vec->envs[env_idx];
    // curriculum_set_env_state
    env->state = start_state;
    puffer_state_refresh(env);
    if (clear_outputs) {
        for (int a = 0; a < env->num_agents; a++) {
            env->rewards[a] = 0.0f;
            env->terminals[a] = 0.0f;
        }
    }
    if (reset_history) {
        buf->active_history_count[active_row] = 0;
        buf->active_segment_start[active_row] = 0;
        buf->row_segment_count[active_row] = 0;
    } else {
        buf->active_segment_start[active_row] =
            buf->active_history_count[active_row];
    }
    buf->active_role[active_row] = role;
    buf->active_best_slot[active_row] = slot;
    buf->active_sample_offset[active_row] = offset;
    buf->active_episode_return[active_row] = 0.0f;
    buf->active_prefix_return[active_row] = prefix_return;
    buf->active_episode_len[active_row] = 0;
    buf->active_prefix_step[active_row] = prefix_step;
    buf->active_best_generation[active_row] = generation;
    if (clear_outputs) {
        buf->active_last_observed_step[active_row] = -1;
    }
    curriculum_record_state(buf, env_idx, &env->state);
    // curriculum_copy_env_obs_to_gpu
    if (copy_to_gpu && vec != NULL && buf != NULL && vec->gpu) {
        int agent_start = env_idx * buf->agents_per_env;
        size_t agent_bytes = (size_t)buf->agents_per_env * sizeof(float);
        cudaMemcpy(vec->gpu_rewards + agent_start,
            vec->rewards + agent_start, agent_bytes, cudaMemcpyHostToDevice);
        cudaMemcpy(vec->gpu_terminals + agent_start,
            vec->terminals + agent_start, agent_bytes,
            cudaMemcpyHostToDevice);
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
    return 1;
}

static inline void curriculum_process_terminal(PuffeRL* pufferl, int env_idx,
        int buffer_idx, int t, int restart, int copy_to_gpu) {
    StateBuffer* buf = &pufferl->state_buf;
    int row = curriculum_active_row(buf, env_idx);
    if (row < 0) {
        return;
    }
    int role = buf->active_role[row];
    if (role != CURRICULUM_ROLE_FRESH && role != CURRICULUM_ROLE_CL) {
        return;
    }
    // curriculum_append_completed_segment
    int slot = buf->active_best_slot[row];
    int start = buf->active_segment_start[row];
    int count = buf->active_history_count[row] - start;
    int seg_count = buf->row_segment_count[row];
    if ((role == CURRICULUM_ROLE_FRESH || role == CURRICULUM_ROLE_CL)
            && slot >= 0 && start >= 0 && count > 0
            && seg_count < buf->max_segments_per_row) {
        int seg_idx = curriculum_segment_base(buf, row) + seg_count;
        buf->row_segment_count[row] = seg_count + 1;
        buf->segment_role[seg_idx] = role;
        buf->segment_best_slot[seg_idx] = slot;
        buf->segment_sample_offset[seg_idx] =
            buf->active_sample_offset[row];
        buf->segment_start[seg_idx] = start;
        buf->segment_count[seg_idx] = count;
        buf->segment_return[seg_idx] = buf->active_episode_return[row];
        if (role == CURRICULUM_ROLE_CL) {
            buf->segment_return[seg_idx] += buf->active_prefix_return[row];
        }
        if (isnan(buf->segment_return[seg_idx])
                || isinf(buf->segment_return[seg_idx])) {
            buf->segment_return[seg_idx] = -3.402823466e+38F;
        }
        buf->segment_prefix_return[seg_idx] = buf->active_prefix_return[row];
        buf->segment_episode_len[seg_idx] = buf->active_episode_len[row];
        if (role == CURRICULUM_ROLE_CL) {
            buf->segment_episode_len[seg_idx] +=
                buf->active_prefix_step[row];
        }
        buf->segment_prefix_step[seg_idx] = buf->active_prefix_step[row];
        buf->segment_best_generation[seg_idx] =
            buf->active_best_generation[row];
    }
    if (!restart) {
        buf->active_best_slot[row] = -1;
        return;
    }
    // curriculum_terminal_salt
    unsigned int salt = (unsigned int)(pufferl->seed
        + (uint64_t)pufferl->epoch * 1000003ULL
        + (uint64_t)buffer_idx * 9176ULL
        + (uint64_t)t * 101ULL
        + (uint64_t)env_idx * 17ULL);
    salt = role == CURRICULUM_ROLE_FRESH
        ? salt ^ 0x2f1bbcdcU
        : salt ^ 0x9e3779b9U;

    if (role == CURRICULUM_ROLE_FRESH) {
        curriculum_start_env(pufferl, env_idx, CURRICULUM_ROLE_FRESH,
            salt, 0, 0, copy_to_gpu);
    } else {
        curriculum_start_env(pufferl, env_idx, CURRICULUM_ROLE_CL,
            salt, 0, 0, copy_to_gpu);
    }
}

static inline void curriculum_post_step(PuffeRL* pufferl, int buffer_idx,
        int t, int env_idx) {
    StateBuffer* buf = &pufferl->state_buf;
    StaticVec* vec = pufferl->vec;
    int role = curriculum_env_role(buf, env_idx);
    if (role != CURRICULUM_ROLE_FRESH && role != CURRICULUM_ROLE_CL) {
        return;
    }
    Env* env = &vec->envs[env_idx];
    if (env->rewards == NULL || env->terminals == NULL) {
        return;
    }
    long step_serial = pufferl->global_step
        + (long)t * (long)vec->total_agents;
    curriculum_observe_step(buf, env_idx, env, step_serial);
    if (env->terminals[0] > 0.5f) {
        curriculum_process_terminal(pufferl, env_idx, buffer_idx,
            t + 1, 1, 0);
        return;
    }
    if (curriculum_should_record_checkpoint(buf, env_idx)) {
        curriculum_record_state(buf, env_idx, &env->state);
    }
}

void curriculum_rollout_begin(PuffeRL* pufferl) {
    StateBuffer* buf = &pufferl->state_buf;
    StaticVec* vec = pufferl->vec;
    HypersT* h = &pufferl->hypers;
    int total_envs = vec->size;

    curriculum_seed_best(pufferl);
    if (curriculum_count_valid(buf) <= 0) {
        vec->log_env_limit = 0;
        return;
    }

    int num_cl_envs = h->cl_frac * total_envs;
    int num_fresh_envs = h->fresh_frac * total_envs;
    int active_envs = num_cl_envs + num_fresh_envs;

    buf->num_cl_envs = num_cl_envs;
    buf->num_fresh_envs = num_fresh_envs;
    buf->num_vanilla_envs = total_envs - active_envs;
    vec->log_env_limit = active_envs > 0 ? buf->num_vanilla_envs : 0;

    int started = 0;
    int fresh_start = buf->num_vanilla_envs;
    int cl_start = fresh_start + num_fresh_envs;
    for (int i = 0; i < num_fresh_envs; i++) {
        int env_idx = fresh_start + i;
        int active_row = curriculum_active_row(buf, env_idx);
        if (active_row >= 0
                && buf->active_role[active_row] == CURRICULUM_ROLE_FRESH
                && buf->active_best_slot[active_row] >= 0) {
            started++;
            continue;
        }
        unsigned int salt = curriculum_epoch_start_salt(
            pufferl, env_idx, CURRICULUM_ROLE_FRESH);
        started += curriculum_start_env(pufferl, env_idx,
            CURRICULUM_ROLE_FRESH, salt, 1, 1, 1);
    }
    for (int i = 0; i < num_cl_envs; i++) {
        int env_idx = cl_start + i;
        int active_row = curriculum_active_row(buf, env_idx);
        if (active_row >= 0
                && buf->active_role[active_row] == CURRICULUM_ROLE_CL
                && buf->active_best_slot[active_row] >= 0) {
            started++;
            continue;
        }
        unsigned int salt = curriculum_epoch_start_salt(
            pufferl, env_idx, CURRICULUM_ROLE_CL);
        started += curriculum_start_env(pufferl, env_idx,
            CURRICULUM_ROLE_CL, salt, 1, 1, 1);
    }
    if (started <= 0) {
        buf->num_cl_envs = 0;
        buf->num_fresh_envs = 0;
        buf->num_vanilla_envs = total_envs;
        vec->log_env_limit = 0;
        for (int row = 0; row < buf->max_active_envs; row++) {
            buf->active_role[row] = CURRICULUM_ROLE_VANILLA;
            buf->active_best_slot[row] = -1;
        }
    }

}

void curriculum_rollout_end(PuffeRL* pufferl) {
    StateBuffer* buf = &pufferl->state_buf;
    // curriculum_apply_completed_segments
    for (int slot = 0; slot < buf->num_start_states; slot++) {
        int best_row = -1;
        int best_seg = -1;
        float candidate_return = buf->best_return[slot];
        int candidate_len = buf->best_episode_len[slot];
        long long generation = buf->best_generation[slot];
        for (int row = 0; row < buf->max_active_envs; row++) {
            int seg_base = curriculum_segment_base(buf, row);
            int seg_count = buf->row_segment_count[row];
            for (int i = 0; i < seg_count; i++) {
                int seg_idx = seg_base + i;
                if (buf->segment_best_slot[seg_idx] != slot
                        || buf->segment_best_generation[seg_idx] != generation) {
                    continue;
                }
                // curriculum_segment_beats
                float terminal_return = buf->segment_return[seg_idx];
                int segment_beats =
                    terminal_return > candidate_return + 1e-6f
                    || (fabsf(terminal_return - candidate_return) <= 1e-6f
                        && buf->segment_episode_len[seg_idx] < candidate_len);
                if (segment_beats) {
                    best_row = row;
                    best_seg = seg_idx;
                    candidate_return = buf->segment_return[seg_idx];
                    candidate_len = buf->segment_episode_len[seg_idx];
                }
            }
        }
        if (best_seg < 0) {
            continue;
        }
        if (buf->segment_role[best_seg] == CURRICULUM_ROLE_FRESH) {
            // curriculum_apply_full_segment
            int apply_slot = buf->segment_best_slot[best_seg];
            int apply_count = buf->segment_count[best_seg];
            if (apply_count > buf->trajectory_max_len) {
                apply_count = buf->trajectory_max_len;
            }
            if (apply_slot >= 0 && apply_count > 0) {
                int active_base = best_row * buf->trajectory_max_len
                    + buf->segment_start[best_seg];
                int best_base = curriculum_best_base(buf, apply_slot);
                memcpy(buf->best_states + best_base,
                    buf->active_states + active_base,
                    (size_t)apply_count * sizeof(PufferState));
                memcpy(buf->best_state_return + best_base,
                    buf->active_state_return + active_base,
                    (size_t)apply_count * sizeof(float));
                memcpy(buf->best_state_step + best_base,
                    buf->active_state_step + active_base,
                    (size_t)apply_count * sizeof(int));
                buf->best_valid[apply_slot] = 1;
                buf->best_len[apply_slot] = apply_count;
                buf->best_episode_len[apply_slot] =
                    buf->segment_episode_len[best_seg];
                buf->best_return[apply_slot] =
                    buf->segment_return[best_seg];
                buf->best_generation[apply_slot]++;
                if (buf->best_generation[apply_slot] <= 0) {
                    buf->best_generation[apply_slot] = 1;
                }
            }
        } else {
            // curriculum_apply_tail_segment
            int apply_slot = buf->segment_best_slot[best_seg];
            int offset = buf->segment_sample_offset[best_seg];
            int apply_count = buf->segment_count[best_seg];
            if (apply_slot >= 0 && buf->best_valid[apply_slot]
                    && apply_count > 0) {
                if (offset < 0) {
                    offset = 0;
                }
                if (offset >= buf->trajectory_max_len) {
                    offset = buf->trajectory_max_len - 1;
                }
                if (offset + apply_count > buf->trajectory_max_len) {
                    apply_count = buf->trajectory_max_len - offset;
                }
                if (apply_count > 0) {
                    int active_base = best_row * buf->trajectory_max_len
                        + buf->segment_start[best_seg];
                    int best_base = curriculum_best_base(buf, apply_slot);
                    memcpy(buf->best_states + best_base + offset,
                        buf->active_states + active_base,
                        (size_t)apply_count * sizeof(PufferState));
                    for (int i = 0; i < apply_count; i++) {
                        buf->best_state_return[best_base + offset + i] =
                            buf->segment_prefix_return[best_seg]
                            + buf->active_state_return[active_base + i];
                        buf->best_state_step[best_base + offset + i] =
                            buf->segment_prefix_step[best_seg]
                            + buf->active_state_step[active_base + i];
                    }
                    buf->best_len[apply_slot] = offset + apply_count;
                    buf->best_episode_len[apply_slot] =
                        buf->segment_episode_len[best_seg];
                    buf->best_return[apply_slot] =
                        buf->segment_return[best_seg];
                    buf->best_generation[apply_slot]++;
                    if (buf->best_generation[apply_slot] <= 0) {
                        buf->best_generation[apply_slot] = 1;
                    }
                }
            }
        }
    }

    // curriculum_compact_active_rows
    for (int row = 0; row < buf->max_active_envs; row++) {
        int keep = buf->active_role[row] != CURRICULUM_ROLE_VANILLA
            && buf->active_best_slot[row] >= 0;
        int start = keep ? buf->active_segment_start[row] : 0;
        int count = keep ? buf->active_history_count[row] - start : 0;
        if (count < 0) {
            count = 0;
        }
        if (count > buf->trajectory_max_len) {
            count = buf->trajectory_max_len;
        }
        int base = row * buf->trajectory_max_len;
        if (keep && start > 0 && count > 0) {
            memmove(buf->active_states + base,
                buf->active_states + base + start,
                (size_t)count * sizeof(PufferState));
            memmove(buf->active_state_return + base,
                buf->active_state_return + base + start,
                (size_t)count * sizeof(float));
            memmove(buf->active_state_step + base,
                buf->active_state_step + base + start,
                (size_t)count * sizeof(int));
        }
        buf->active_history_count[row] = count;
        buf->active_segment_start[row] = 0;
        buf->row_segment_count[row] = 0;
        if (!keep) {
            buf->active_role[row] = CURRICULUM_ROLE_VANILLA;
            buf->active_best_slot[row] = -1;
            buf->active_last_observed_step[row] = -1;
            buf->active_best_generation[row] = -1;
        }
    }
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

__global__ void multinomial_sample_advance(int* __restrict__ out_idx,
        precision_t* __restrict__ out_importance,
        const float* __restrict__ prio_weights, const float* __restrict__ cdf,
        int B, int num_samples, uint64_t seed, float beta,
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
        if (out_importance != NULL) {
            float weight = 1.0f;
            if (!use_uniform) {
                float value = prio_weights[lo] * (float)B / total_weight;
                weight = __powf(value, -beta);
                if (isnan(weight) || isinf(weight)) {
                    weight = 1.0f;
                }
            }
            precision_t value = from_float(weight);
            out_importance[tid] = value;
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
        float beta, cudaStream_t stream) {
    size_t cdf_temp_bytes = (size_t)bufs->cdf_temp.shape[0];
    cub::DeviceScan::InclusiveSum(bufs->cdf_temp.data, cdf_temp_bytes,
        bufs->prio_weights.data, bufs->cdf.data, population, stream);
    int blocks = (samples + PRIO_BLOCK_SIZE - 1) / PRIO_BLOCK_SIZE;
    multinomial_sample_advance<<<blocks, PRIO_BLOCK_SIZE, 0, stream>>>(
        bufs->idx.data, out_importance,
        bufs->prio_weights.data, bufs->cdf.data, population, samples, seed, beta,
        offset_ptr, bufs->sample_done.data, blocks);
}

#endif
