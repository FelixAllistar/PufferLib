// CUDA
#include <cuda_runtime.h>
#include <cuda_bf16.h>
#include <cuda_profiler_api.h>
#include <cublas_v2.h>
#include <curand.h>
#include <curand_kernel.h>
#include <nccl.h>
#include <nvml.h>
#include <nvtx3/nvToolsExt.h>

// C standard
#include <cassert>
#include <cmath>
#include <cstdint>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// POSIX / threading
#include <dirent.h>
#include <fcntl.h>
#include <omp.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// Project
#include "ini.h"


// Shared preamble: tensors, allocator, precision, cast, env include.
#include "pufferl_preamble.h"

// Policy / optim / GEMM / encoders / weight init.
// Needs tensors, Allocator, batch_size/ndim, grid_size, cast, CUBLAS_PRECISION*.
#include "algo.cu"
#include "protein.cu"

typedef struct {
    int horizon;
    int total_agents;
    int num_buffers;
    int hidden_size;
    int num_layers;
    float lr;
    float min_lr_ratio;
    bool anneal_lr;
    float momentum;
    int minibatch_size;
    float replay_ratio;
    long total_timesteps;
    float max_grad_norm;
    float clip_coef;
    float vf_clip_coef;
    float vf_coef;
    float ent_coef;
    float emag_kl_coef;
    float emag_tau;
    float min_ent_coef_ratio;
    bool anneal_ent_coef;
    float gamma;
    float gae_lambda;
    float vtrace_rho_clip;
    float vtrace_c_clip;
    float prio_alpha;
    float prio_beta0;
    bool epoch_sampling;
    bool async;
    bool reset_every_horizon;
    // true when base.cudagraphs >= 0: first real rollout/train use captures.
    bool cudagraphs;
    bool profile;
    int rank;
    int world_size;
    int gpu_id;
    int num_threads;
    int seed;
} HypersT;

// Rank / device context for one process in a multi-GPU train job.
typedef struct {
    int rank;
    int world_size;
    int gpu_id;
    int artifact_owner;
    ncclUniqueId* nccl_id;
} TrainContext;

typedef struct ObsTensor {
    obs_t* data;
    int64_t shape[8];
} ObsTensor;

// Data collected by parallel environment workers. Each worker handles
// a constant subset of agents
struct RolloutBuf {
    PrecisionTensor observations;  // (horizon, agents, input_size)
    // (slots, layers, agents, hidden) when !reset_every_horizon; slots = async ? 2 : 1.
    // Default path is carry + async: per-slot states for pipelined horizons.
    PrecisionTensor initial_states;
    PrecisionTensor actions;       // (horizon, agents, num_atns)
    PrecisionTensor values;        // (horizon, agents)
    PrecisionTensor logprobs;      // ...
    PrecisionTensor rewards;
    PrecisionTensor terminals;
    PrecisionTensor ratio;
    PrecisionTensor importance;
    ByteTensor action_mask;        // (horizon, agents, ceil(mask_size/8)); packed bits
};

// Buffers are initialized as raw structs with only shape information. alloc_register
// stores the shape and data pointer. Memory is only allocated after all buffers are registered.
void register_rollout_buffers(RolloutBuf& bufs, Allocator* alloc, int T, int B, int input_size,
        int num_atns, int mask_size) {
    memset(&bufs, 0, sizeof(bufs));
    bufs.observations = {.shape = {T, B, input_size}};
    bufs.actions = {.shape = {T, B, num_atns}};
    bufs.values = {.shape = {T, B}};
    bufs.logprobs = {.shape = {T, B}};
    bufs.rewards = {.shape = {T, B}};
    bufs.terminals = {.shape = {T, B}};
    bufs.ratio = {.shape = {T, B}};
    bufs.importance = {.shape = {T, B}};
    bufs.action_mask = {.shape = {T, B, (mask_size + 7) / 8}};
    PrecisionTensor* fields[] = {
        &bufs.observations, &bufs.actions, &bufs.values, &bufs.logprobs,
        &bufs.rewards, &bufs.terminals, &bufs.ratio, &bufs.importance,
    };
    for (int i = 0; i < (int)(sizeof(fields) / sizeof(fields[0])); i++) {
        alloc_register(alloc, fields[i]);
    }
    alloc_register(alloc, &bufs.action_mask);
}

// Rank-2 or rank-3 time-major tensor. F==0 means rank-2 (zero-terminated shape);
// stride still multiplies by max(F, 1).
PrecisionTensor puf_time_view(PrecisionTensor p, int start_t, int T) {
    long B = p.shape[1];
    long F = p.shape[2];
    long stride_f = F > 1 ? F : 1;
    return {
        .data = p.data + (long)start_t * B * stride_f,
        .shape = {T, B, F},
    };
}

ByteTensor puf_time_view(ByteTensor p, int start_t, int T) {
    long B = p.shape[1];
    long F = p.shape[2];
    return {
        .data = p.data + (long)start_t * B * F,
        .shape = {T, B, F},
    };
}

RolloutBuf rollout_time_view(RolloutBuf* base, int start_t, int T) {
    RolloutBuf view = *base;
    view.observations = puf_time_view(base->observations, start_t, T);
    view.actions      = puf_time_view(base->actions,      start_t, T);
    view.values       = puf_time_view(base->values,       start_t, T);
    view.logprobs     = puf_time_view(base->logprobs,     start_t, T);
    view.rewards      = puf_time_view(base->rewards,      start_t, T);
    view.terminals    = puf_time_view(base->terminals,    start_t, T);
    view.ratio        = puf_time_view(base->ratio,        start_t, T);
    view.importance   = puf_time_view(base->importance,   start_t, T);
    view.action_mask  = puf_time_view(base->action_mask,  start_t, T);
    return view;
}

enum VecProfileIdx {
    VEC_MODEL = 0,
    VEC_ENV_STEP,
    VEC_COPY,
    NUM_VEC_PROF,
};

// Per-buffer worker handshake (CPU env path). Atomic on worker_state[].
enum BufWorkerState {
    BUF_STARTING = 0,
    BUF_WAITING = 1,
    BUF_RUNNING = 2,
};

// Env batch + host/device buffers. Per-buffer worker threads are started later
// with a PuffeRL* (worker threads); they are not isolated from the trainer.
struct VecEnv {
    Env* envs;           // host (CPU) or device (PUFFER_GPU_ENV)
#ifdef PUFFER_GPU_ENV
    float* gpu_log;      // device reduce scratch (sizeof(Log) floats)
#endif
    int size;
    int total_agents;
    int buffers;
    int agents_per_buffer;
    int* buffer_env_starts;
    int* buffer_env_counts;
    obs_t* observations;
    float* actions;
    float* rewards;
    float* terminals;
    unsigned char* action_mask;
    obs_t* gpu_observations;
    float* gpu_actions;
    float* gpu_rewards;
    float* gpu_terminals;
    unsigned char* gpu_action_mask;
    // Cross-thread: BufWorkerState, only via __atomic_*.
    int* worker_state;
    int shutdown;
    pthread_t* threads;
    void* thread_args;  // VecThreadArg[buffers]; owned once threads exist
    float* accum;
    int num_workers;
    int action_mask_size;
    int num_banks;
    int* bank_layout;  // per-buffer agent offsets; sole owner (not mirrored on PuffeRL)
};

struct EnvBuf {
    ObsTensor obs;         // (total_agents, obs_size)
    FloatTensor actions;   // (total_agents, num_atns)
    FloatTensor rewards;   // (total_agents,)
    FloatTensor terminals; // (total_agents,)
    ByteTensor action_mask; // (total_agents, mask_size); always allocated
};

// Frozen opponent bank (selfplay / match): rollout-only policy + params.
// Same build_policy / weights_create path as primary, but no train acts, grads, or muon.
// Optional different hidden/layers via frozen_bank_hidden_size / frozen_bank_num_layers.
typedef struct {
    Policy policy;
    PolicyWeights weights;
    Allocator params_alloc;
    Allocator acts_alloc;
    PrecisionTensor param_puf;
    FloatTensor master_weights;
    PrecisionTensor* buffer_states;         // [num_buffers]
    PolicyActivations* buffer_activations;  // [num_buffers]
} WeightBank;

enum ProfileIdx {
    PROF_ROLLOUT = 0,
    PROF_EVAL_MODEL,
    PROF_EVAL_ENV,
    PROF_EVAL_COPY,
    PROF_TRAIN_MISC,
    PROF_TRAIN_MODEL,
};

// Index must match ProfileIdx order.
const char* PROF_NAMES[] = {
    "rollout",
    "eval_model",
    "eval_env",
    "eval_copy",
    "train_misc",
    "train_model",
};

typedef struct {
    cudaEvent_t events[5];  // train_impl markers: pre-start, pre-end, misc, model-start, model-end
    cudaEvent_t* rollout_gpu_start;
    cudaEvent_t* rollout_gpu_end;
    cudaEvent_t* rollout_env_end;
    int rollout_horizon;
    float accum[sizeof(PROF_NAMES) / sizeof(PROF_NAMES[0])];
} ProfileT;

typedef struct PuffeRL {
    Policy policy;
    PolicyWeights weights;       // current precision_t weights (structured)
    PolicyWeights actor_weights; // async rollout snapshot; unused when async=0
    PolicyWeights magnet_weights;
    PolicyActivations train_activations;
    PolicyActivations magnet_activations;
    Allocator params_alloc;
    Allocator actor_params_alloc;
    Allocator grads_alloc;
    Allocator activations_alloc;
    Allocator magnet_params_alloc;
    Allocator magnet_grads_alloc;
    Allocator magnet_acts_alloc;
    VecEnv* vec;
    Muon muon;
    ncclComm_t nccl_comm;  // NCCL communicator for multi-GPU
    HypersT hypers;
    bool is_continuous;  // True if all action dimensions are continuous (size==1)
    PrecisionTensor* buffer_states;  // Per-buffer states for contiguous access
    PolicyActivations* buffer_activations;  // Per-buffer inference activations
    RolloutBuf rollouts;
    RolloutBuf train_rollouts;  // Pre-allocated transposed copy for train_impl
    EnvBuf env;
    TrainGraph train_buf;
    PrecisionTensor advantages_puf;  // Pre-allocated for train_impl (B, T)
    cudaGraphExec_t* fused_rollout_cudagraphs;  // [slots][horizon][num_buffers]; null if !cudagraphs
    cudaGraphExec_t train_cudagraph;  // null until first-use capture
    cudaStream_t* streams;  // per-buffer raw CUDA streams
    cudaStream_t default_stream;  // main-thread stream (captured once at init)
    cudaStream_t train_stream;    // dedicated learner stream (always non-default)
    IntTensor act_sizes_puf;    // CUDA int32 tensor of action head sizes
    FloatTensor losses_puf;     // (NUM_LOSSES,) f32 accumulator
    PPOBuffersPuf ppo_bufs_puf; // Pre-allocated buffers for ppo_loss_fwd_bwd
    PrioBuffers prio_bufs;      // Pre-allocated buffers for prio_replay
    FloatTensor master_weights;  // fp32 master weights (flat); same buffer as param_puf in fp32 mode
    FloatTensor magnet_master_weights;
    PrecisionTensor param_puf;
    PrecisionTensor magnet_param_puf;
    PrecisionTensor actor_param_puf;
    PrecisionTensor grad_puf;
    LongTensor rng_offset_puf;   // (num_buffers+1,) int64 CUDA device counters
    ProfileT profile;
    nvmlDevice_t nvml_device;
    long epoch;
    long global_step;
    double start_time;
    double last_log_time;
    long last_log_step;
    int rollout_write_slot;
    int async_ready_slot;
    int async_next_slot;
    bool async_bootstrapped;
    ulong seed;
    curandStatePhilox4_32_10_t** rng_states;  // per-buffer persistent RNG states [num_buffers]
    // Optional frozen weight banks for match / selfplay opponents.
    WeightBank* frozen_banks;  // [num_frozen_banks]
    int num_frozen_banks;
    char env_name[64];  // For frozen-bank policy rebuild at create.
} PuffeRL;

static inline void update_emag(PuffeRL& pufferl, float tau,
        cudaStream_t stream) {
    int n = numel(pufferl.master_weights.shape);
    ema_weights<<<grid_size(n), BLOCK_SIZE, 0, stream>>>(
        pufferl.magnet_master_weights.data,
        pufferl.master_weights.data, tau, n);
    if (USE_BF16) {
        cast<<<grid_size(n), BLOCK_SIZE, 0, stream>>>(
            pufferl.magnet_param_puf.data,
            pufferl.magnet_master_weights.data, n);
    }
}

// --- Infer path: sample + forward, then vec workers ---
static void profile_begin(const char* tag, bool enable) {
    if (enable) {
        nvtxRangePushA(tag);
    }
}

static void profile_end(bool enable) {
    if (enable) {
        nvtxRangePop();
    }
}

__global__ void rng_init(curandStatePhilox4_32_10_t* states, uint64_t seed, int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) {
        curand_init(seed, idx, 0, &states[idx]);
    }
}

// Action logits and value share one row: [logits..., value]. logstd empty ⇒ discrete.
// Discrete: logsumexp + inverse-CDF with always-present mask (all-ones if env has none).
// Continuous: ignores mask. Small heads (A<=16) cache logits; large heads re-read.
__global__ void sample_logits(
        PrecisionTensor dec_out,              // (B, logits_dim + 1)
        PrecisionTensor logstd_puf,           // (1, od) continuous only; .data null if discrete
        IntTensor act_sizes_puf,              // (num_atns,)
        precision_t* actions,                 // (B, num_atns)
        precision_t* logprobs,                // (B,)
        precision_t* value_out,               // (B,)
        curandStatePhilox4_32_10_t* rng_states,
        const unsigned char* action_mask,     // (B, A_total); unpacked live mask
        int mask_stride) {
    int B = dec_out.shape[0];
    int fused_cols = dec_out.shape[1];
    int num_atns = numel(act_sizes_puf.shape);
    int* act_sizes = act_sizes_puf.data;
    precision_t* logits = dec_out.data;
    bool is_continuous = logstd_puf.data != nullptr && numel(logstd_puf.shape) > 0;
    precision_t* logstd = logstd_puf.data;

    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B) {
        return;
    }

    curandStatePhilox4_32_10_t state = rng_states[idx];
    int logits_base = idx * fused_cols;
    float total_log_prob = 0.0f;

    if (is_continuous) {
        for (int h = 0; h < num_atns; h++) {
            float mean = safe_continuous_mean(logits, logits_base + h);
            float log_std = safe_continuous_logstd(logstd, h);
            float std = expf(log_std);
            float action = finite_or_clamp(
                mean + std * curand_normal(&state), -1.0e6f, 1.0e6f);
            // Round-trip so logprob matches stored precision_t action (bf16).
            precision_t stored_p = from_float(action);
            float lp, ent;
            ppo_continuous_head(mean, log_std, to_float(stored_p), &lp, &ent);
            total_log_prob += lp;
            actions[idx * num_atns + h] = stored_p;
        }
    } else {
        constexpr int LOGIT_CACHE = 16;
        int logits_offset = 0;
        int mask_base = idx * mask_stride;
        for (int h = 0; h < num_atns; h++) {
            int A = act_sizes[h];
            if (!puf_action_head_active(actions, idx * num_atns, h)) {
                actions[idx * num_atns + h] = from_float(0.0f);
                logits_offset += A;
                continue;
            }
            float cache[LOGIT_CACHE];
            int use_cache = A <= LOGIT_CACHE;
            float max_val = -INFINITY;
            for (int a = 0; a < A; a++) {
                float l = load_logit_masked_byte(
                    logits, logits_base, logits_offset, a, action_mask, mask_base);
                if (use_cache) {
                    cache[a] = l;
                }
                max_val = fmaxf(max_val, l);
            }
            float sum_exp = 0.0f;
            for (int a = 0; a < A; a++) {
                float l = use_cache ? cache[a] : load_logit_masked_byte(
                    logits, logits_base, logits_offset, a, action_mask, mask_base);
                sum_exp += expf(l - max_val);
            }
            float logsumexp = max_val + logf(sum_exp);

            float rand_val = curand_uniform(&state);
            float cumsum = 0.0f;
            int sampled = A - 1;
            for (int a = 0; a < A; a++) {
                float l = use_cache ? cache[a] : load_logit_masked_byte(
                    logits, logits_base, logits_offset, a, action_mask, mask_base);
                cumsum += expf(l - logsumexp);
                if (rand_val < cumsum) {
                    sampled = a;
                    break;
                }
            }

            float sampled_logit = use_cache ? cache[sampled] : load_logit_masked_byte(
                logits, logits_base, logits_offset, sampled, action_mask, mask_base);
            actions[idx * num_atns + h] = from_float((float)sampled);
            total_log_prob += sampled_logit - logsumexp;
            logits_offset += A;
        }
    }

    logprobs[idx] = from_float(total_log_prob);
    value_out[idx] = logits[logits_base + fused_cols - 1];
    rng_states[idx] = state;
}

// Index into (L, agents, H): element-parallel over L*count*H.
// state_row is agent index within the state tensor's agent dim.
static __device__ long state_elem_idx(
        int layer, int agents_stride, int state_row, int h, int H) {
    return ((long)layer * agents_stride + state_row) * H + h;
}

// Copy buffer RNN state into rollout initial_states at t=0 (carry path).
// Primary bank only; src agents are 0..count, dst at dst_start.
__global__ void snapshot_initial_state(PrecisionTensor dst, PrecisionTensor src,
        int dst_start, int count) {
    int L = src.shape[0];
    int H = src.shape[2];
    int total = L * count * H;
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= total) {
        return;
    }
    int h = idx % H;
    int rel = (idx / H) % count;
    int layer = idx / (count * H);
    long src_i = state_elem_idx(layer, (int)src.shape[1], rel, h, H);
    long dst_i = state_elem_idx(layer, (int)dst.shape[1], dst_start + rel, h, H);
    dst.data[dst_i] = src.data[src_i];
}

// View slot of full (slots, L, A, H) as (L, A, H).
static PrecisionTensor initial_states_slot(PrecisionTensor full, int slot) {
    int L = (int)full.shape[1];
    int A = (int)full.shape[2];
    int H = (int)full.shape[3];
    long stride = (long)L * A * H;
    return {.data = full.data + (long)slot * stride, .shape = {L, A, H}};
}

// Zero RNN state for agents that just terminated. Same grid as snapshot
// (L*count*H); non-terminal threads return after one terminal load.
__global__ void zero_state_on_terminal(PrecisionTensor state, FloatTensor terminals,
        int state_start, int terminal_start, int count) {
    int L = state.shape[0];
    int H = state.shape[2];
    int total = L * count * H;
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= total) {
        return;
    }
    int h = idx % H;
    int rel = (idx / H) % count;
    int layer = idx / (count * H);
    if (terminals.data[terminal_start + rel] == 0.0f) {
        return;
    }
    long i = state_elem_idx(layer, (int)state.shape[1], state_start + rel, h, H);
    state.data[i] = from_float(0.0f);
}

// Select time t, then agents [start, start+count). Rank-2 has F==0 (zero-term shape);
// stride uses max(F, 1). Out shape {count, F} keeps ndim 1 when F==0.
PrecisionTensor puf_slice(PrecisionTensor& p, int t, int start, int count) {
    long B = p.shape[1];
    long F = p.shape[2];
    long stride_f = F > 1 ? F : 1;
    return {
        .data = p.data + (long)(t * B + start) * stride_f,
        .shape = {count, F},
    };
}

ByteTensor puf_slice(ByteTensor& p, int t, int start, int count) {
    long B = p.shape[1];
    long F = p.shape[2];
    return {
        .data = p.data + (long)(t * B + start) * F,
        .shape = {count, F},
    };
}

__global__ void pack_action_mask(unsigned char* dst,
        const unsigned char* src, int B, int mask_size, int packed_stride);

void pufferl_forward(PuffeRL* pufferl, int buf, int t, cudaStream_t stream) {
    HypersT& hypers = pufferl->hypers;
    int graph_slot = hypers.async ? pufferl->rollout_write_slot : 0;
    int graph = (graph_slot * hypers.horizon + t) * hypers.num_buffers + buf;
    profile_begin("fused_rollout", hypers.profile);

    if (hypers.cudagraphs && pufferl->fused_rollout_cudagraphs[graph] != nullptr) {
        assert(cudaGraphLaunch(pufferl->fused_rollout_cudagraphs[graph], stream) == cudaSuccess
                && "cudaGraphLaunch failed");
        profile_end(hypers.profile);
        return;
    }

    if (hypers.cudagraphs) {
        assert(cudaStreamBeginCapture(stream, cudaStreamCaptureModeThreadLocal) == cudaSuccess
                && "cudaStreamBeginCapture failed");
    }

    RolloutBuf rollouts = pufferl->rollouts;
    if (hypers.async) {
        rollouts = rollout_time_view(&pufferl->rollouts,
            pufferl->rollout_write_slot * hypers.horizon, hypers.horizon);
    }
    EnvBuf& env = pufferl->env;
    VecEnv* vec = pufferl->vec;
    int block_size = hypers.total_agents / hypers.num_buffers;
    int start = buf * block_size;
    int* layout = vec->bank_layout;

    // Copy observations, rewards, terminals from GPU env buffers to rollout buffer
    ObsTensor& obs_env = env.obs;
    int n = block_size * obs_env.shape[1];
    PrecisionTensor obs_dst = puf_slice(rollouts.observations, t, start, block_size);
    // Env obs → rollout: D2D if same type, else cast (float/uchar → precision_t).
    if (sizeof(obs_t) == sizeof(precision_t)) {
        cudaMemcpyAsync(obs_dst.data, obs_env.data + (long)start * obs_env.shape[1],
            (size_t)n * sizeof(precision_t), cudaMemcpyDeviceToDevice, stream);
    } else {
        cast<<<grid_size(n), BLOCK_SIZE, 0, stream>>>(
            obs_dst.data, obs_env.data + (long)start * obs_env.shape[1], n);
    }

    PrecisionTensor rew_dst = puf_slice(rollouts.rewards, t, start, block_size);
    PrecisionTensor term_dst = puf_slice(rollouts.terminals, t, start, block_size);
    cast_rew_term<<<grid_size(block_size), BLOCK_SIZE, 0, stream>>>(
        rew_dst.data, env.rewards.data + start,
        term_dst.data, env.terminals.data + start, block_size);

    // Keep the live byte mask for sampling, but archive one bit per action for PPO.
    int mask_stride = vec->action_mask_size;
    int packed_stride = (mask_stride + 7) / 8;
    ByteTensor mask_slice = puf_slice(rollouts.action_mask, t, start, block_size);
    pack_action_mask<<<grid_size(block_size * packed_stride), BLOCK_SIZE, 0, stream>>>(
        mask_slice.data,
        env.action_mask.data + (long)start * mask_stride,
        block_size, mask_stride, packed_stride);

    // Per-bank forward: layout[b]..layout[b+1) within each buffer chunk.
    int num_banks = 1 + pufferl->num_frozen_banks;
    long act_cols = env.actions.shape[1];
    for (int b = 0; b < num_banks; b++) {
        int bank_off = layout[b];
        int bank_end = layout[b + 1];
        int bank_size = bank_end - bank_off;
        if (bank_size == 0) {
            continue;
        }

        Policy* p_bank;
        PolicyWeights* w_bank;
        PolicyActivations* a_bank;
        PrecisionTensor* s_bank;
        if (b == 0) {
            p_bank = &pufferl->policy;
            w_bank = hypers.async ? &pufferl->actor_weights : &pufferl->weights;
            a_bank = &pufferl->buffer_activations[buf];
            s_bank = &pufferl->buffer_states[buf];
        } else {
            WeightBank* fb = &pufferl->frozen_banks[b - 1];
            p_bank = &fb->policy;
            w_bank = &fb->weights;
            a_bank = &fb->buffer_activations[buf];
            s_bank = &fb->buffer_states[buf];
        }

        int sub_start = start + bank_off;
        PrecisionTensor obs_b  = puf_slice(rollouts.observations, t, sub_start, bank_size);
        PrecisionTensor act_b  = puf_slice(rollouts.actions,      t, sub_start, bank_size);
        PrecisionTensor lp_b   = puf_slice(rollouts.logprobs,     t, sub_start, bank_size);
        PrecisionTensor val_b  = puf_slice(rollouts.values,       t, sub_start, bank_size);

        int state_start = (b == 0) ? bank_off : 0;
        int state_n = (int)s_bank->shape[0] * bank_size * (int)s_bank->shape[2];
        zero_state_on_terminal<<<grid_size(state_n), BLOCK_SIZE, 0, stream>>>(
            *s_bank, env.terminals, state_start, sub_start, bank_size);

        // Carry path: snapshot primary buffer state into per-slot initial_states.
        if (b == 0 && t == 0 && rollouts.initial_states.data != nullptr) {
            PrecisionTensor init_slot = initial_states_slot(
                rollouts.initial_states, graph_slot);
            snapshot_initial_state<<<grid_size(state_n), BLOCK_SIZE, 0, stream>>>(
                init_slot, *s_bank, sub_start, bank_size);
        }

        PrecisionTensor dec_puf = policy_forward(p_bank, *w_bank, *a_bank, obs_b, *s_bank, stream);

        PrecisionTensor p_logstd = {};
        if (pufferl->is_continuous) {
            DecoderWeights* dw = (DecoderWeights*)w_bank->decoder;
            p_logstd = dw->logstd;
        }

        // Offset RNG by bank_off so banks don't collide on per-buffer rng slots.
        sample_logits<<<grid_size(bank_size), BLOCK_SIZE, 0, stream>>>(
            dec_puf, p_logstd, pufferl->act_sizes_puf,
            act_b.data, lp_b.data, val_b.data,
            pufferl->rng_states[buf] + bank_off,
            env.action_mask.data + (long)sub_start * mask_stride, mask_stride);

        cast<<<grid_size(numel(act_b.shape)), BLOCK_SIZE, 0, stream>>>(
                env.actions.data + (long)sub_start * act_cols,
                act_b.data, numel(act_b.shape));
    }

    if (hypers.cudagraphs) {
        cudaGraph_t _graph;
        assert(cudaStreamEndCapture(stream, &_graph) == cudaSuccess
                && "cudaStreamEndCapture failed");
        assert(cudaGraphInstantiate(&pufferl->fused_rollout_cudagraphs[graph], _graph, 0)
                == cudaSuccess && "cudaGraphInstantiate failed");
        cudaGraphDestroy(_graph);
        // Capture records without executing; run once so this step has effects.
        assert(cudaGraphLaunch(pufferl->fused_rollout_cudagraphs[graph], stream) == cudaSuccess
                && "cudaGraphLaunch failed");
    }
    profile_end(hypers.profile);
}


// --- VecEnv worker threads + lifecycle (after pufferl_forward; needs PuffeRL) ---
// One arg per buffer thread: trainer + which buffer. vec/horizon come from pufferl.
typedef struct {
    PuffeRL* pufferl;
    int buf;
} VecThreadArg;

#ifndef PUFFER_GPU_ENV
static void* vec_thread_main(void* arg) {
    VecThreadArg* a = (VecThreadArg*)arg;
    PuffeRL* pufferl = a->pufferl;
    VecEnv* vec = pufferl->vec;
    int buf = a->buf;
    int horizon = pufferl->hypers.horizon;
    cublas_init_handle();

    int agents_per_buffer = vec->agents_per_buffer;
    int agent_start = buf * agents_per_buffer;
    int env_start = vec->buffer_env_starts[buf];
    int env_count = vec->buffer_env_counts[buf];

    Env* envs = vec->envs;
    cudaStream_t stream = pufferl->streams[buf];
    cudaEvent_t model_start, model_end, copy_end, h2d_start, h2d_end;
    cudaEventCreate(&model_start);
    cudaEventCreate(&model_end);
    cudaEventCreate(&copy_end);
    cudaEventCreate(&h2d_start);
    cudaEventCreate(&h2d_end);
    __atomic_store_n(&vec->worker_state[buf], BUF_WAITING, __ATOMIC_SEQ_CST);

    float* my_accum = &vec->accum[buf * NUM_VEC_PROF];
    struct timespec t0, t1;
    float ms = 0.0f;

    int alive = 1;
    while (alive) {
        while (__atomic_load_n(&vec->worker_state[buf], __ATOMIC_SEQ_CST) != BUF_RUNNING) {
            if (__atomic_load_n(&vec->shutdown, __ATOMIC_SEQ_CST)) {
                alive = 0;
                break;
            }
        }
        if (!alive) {
            break;
        }

        int h2d_pending = 0;

        for (int t = 0; t < horizon; t++) {
            cudaEventRecord(model_start, stream);
            pufferl_forward(pufferl, buf, t, stream);
            cudaEventRecord(model_end, stream);
            cudaMemcpyAsync(
                &vec->actions[agent_start * NUM_ATNS],
                &vec->gpu_actions[agent_start * NUM_ATNS],
                agents_per_buffer * NUM_ATNS * sizeof(float),
                cudaMemcpyDeviceToHost, stream);
            cudaEventRecord(copy_end, stream);
            cudaStreamSynchronize(stream);

            cudaEventElapsedTime(&ms, model_start, model_end);
            my_accum[VEC_MODEL] += ms;
            cudaEventElapsedTime(&ms, model_end, copy_end);
            my_accum[VEC_COPY] += ms;
            if (h2d_pending) {
                cudaEventElapsedTime(&ms, h2d_start, h2d_end);
                my_accum[VEC_COPY] += ms;
                h2d_pending = 0;
            }

            memset(&vec->rewards[agent_start], 0, agents_per_buffer * sizeof(float));
            memset(&vec->terminals[agent_start], 0, agents_per_buffer * sizeof(float));
            clock_gettime(CLOCK_MONOTONIC, &t0);
            #pragma omp parallel for schedule(static) num_threads(vec->num_workers)
            for (int i = env_start; i < env_start + env_count; i++) {
                puf_step(&envs[i]);
            }
            clock_gettime(CLOCK_MONOTONIC, &t1);
            my_accum[VEC_ENV_STEP] += (t1.tv_sec - t0.tv_sec) * 1000.0f + (t1.tv_nsec - t0.tv_nsec) / 1e6f;

            cudaEventRecord(h2d_start, stream);
            cudaMemcpyAsync(
                vec->gpu_observations + (size_t)agent_start * OBS_SIZE,
                vec->observations + (size_t)agent_start * OBS_SIZE,
                (size_t)agents_per_buffer * OBS_SIZE * sizeof(obs_t),
                cudaMemcpyHostToDevice, stream);
            cudaMemcpyAsync(
                &vec->gpu_rewards[agent_start],
                &vec->rewards[agent_start],
                agents_per_buffer * sizeof(float),
                cudaMemcpyHostToDevice, stream);
            cudaMemcpyAsync(
                &vec->gpu_terminals[agent_start],
                &vec->terminals[agent_start],
                agents_per_buffer * sizeof(float),
                cudaMemcpyHostToDevice, stream);
            cudaMemcpyAsync(
                vec->gpu_action_mask + agent_start * vec->action_mask_size,
                vec->action_mask + agent_start * vec->action_mask_size,
                (size_t)agents_per_buffer * vec->action_mask_size * sizeof(unsigned char),
                cudaMemcpyHostToDevice, stream);
            cudaEventRecord(h2d_end, stream);
            h2d_pending = 1;
        }
        cudaStreamSynchronize(stream);
        if (h2d_pending) {
            cudaEventElapsedTime(&ms, h2d_start, h2d_end);
            my_accum[VEC_COPY] += ms;
        }
        __atomic_store_n(&vec->worker_state[buf], BUF_WAITING, __ATOMIC_SEQ_CST);
    }

    cudaEventDestroy(model_start);
    cudaEventDestroy(model_end);
    cudaEventDestroy(copy_end);
    cudaEventDestroy(h2d_start);
    cudaEventDestroy(h2d_end);
    return NULL;
}
#endif  // !PUFFER_GPU_ENV


// Aggregate env logs → env/<key>. Log + puf_log are env-defined, compile-time.
// PUFFER_GPU_ENV: Env* is device (Log first member). CPU: Env* is host.
#ifdef PUFFER_GPU_ENV
// Sum envs[i].log into out[NF]; optional clear. Requires Env.log addressable as Log.
#define PUF_LOG_REDUCE_THREADS 128
__global__ void puf_log_reduce_kernel(Env* envs, float* out, int num_envs,
        int clear, int tag_filter) {
    constexpr int NF = (int)(sizeof(Log) / sizeof(float));
    extern __shared__ float sh[];
    int tid = threadIdx.x;
    float local[NF];
    for (int j = 0; j < NF; j++) {
        local[j] = 0.0f;
    }
    for (int i = tid; i < num_envs; i += blockDim.x) {
        float* el = (float*)&envs[i].log;
        int selected = tag_filter < 0 || envs[i].tag == tag_filter;
        if (selected && envs[i].log.n != 0.0f) {
            for (int j = 0; j < NF; j++) {
                local[j] += el[j];
            }
        }
        if (clear && selected) {
            for (int j = 0; j < NF; j++) {
                el[j] = 0.0f;
            }
        }
    }
    for (int j = 0; j < NF; j++) {
        sh[j * blockDim.x + tid] = local[j];
    }
    __syncthreads();
    for (int stride = blockDim.x / 2; stride > 0; stride >>= 1) {
        if (tid < stride) {
            for (int j = 0; j < NF; j++) {
                sh[j * blockDim.x + tid] += sh[j * blockDim.x + tid + stride];
            }
        }
        __syncthreads();
    }
    if (tid == 0) {
        for (int j = 0; j < NF; j++) {
            out[j] = sh[j * blockDim.x];
        }
    }
}
#endif

void vec_log(VecEnv* vec, Dict* out, int clear) {
    Log aggregate = {0};
    int num_keys = (int)(sizeof(Log) / sizeof(float));
    float* acc = (float*)&aggregate;

#ifdef PUFFER_GPU_ENV
    constexpr int NF = (int)(sizeof(Log) / sizeof(float));
    puf_log_reduce_kernel<<<1, PUF_LOG_REDUCE_THREADS,
        NF * PUF_LOG_REDUCE_THREADS * sizeof(float)>>>(
        vec->envs, vec->gpu_log, vec->total_agents, clear, -1);
    cudaMemcpy(&aggregate, vec->gpu_log, sizeof(Log), cudaMemcpyDeviceToHost);
#else
    Env* envs = vec->envs;
    for (int i = 0; i < vec->size; i++) {
        if (envs[i].log.n == 0.0f) {
            continue;
        }
        float* el = (float*)&envs[i].log;
        for (int j = 0; j < num_keys; j++) {
            acc[j] += el[j];
        }
    }
    if (clear) {
        for (int i = 0; i < vec->size; i++) {
            envs[i].log = (Log){0};
        }
    }
#endif

    float n = aggregate.n;
    Dict env_out = {0};
    if (n > 0.0f) {
        for (int j = 0; j < num_keys; j++) {
            acc[j] /= n;
        }
        puf_log(&aggregate, &env_out);
    }
    dict_set(&env_out, "n", n);
    for (int i = 0; i < env_out.size; i++) {
        char key[256];
        snprintf(key, sizeof(key), "env/%s", env_out.items[i].key);
        dict_set(out, key, env_out.items[i].value);
    }
    dict_clear(&env_out);
}

void vec_log_tag(VecEnv* vec, int tag, Dict* out) {
    Log aggregate = {0};
    int num_keys = (int)(sizeof(Log) / sizeof(float));
    float* acc = (float*)&aggregate;
#ifdef PUFFER_GPU_ENV
    constexpr int NF = (int)(sizeof(Log) / sizeof(float));
    puf_log_reduce_kernel<<<1, PUF_LOG_REDUCE_THREADS,
        NF * PUF_LOG_REDUCE_THREADS * sizeof(float)>>>(
        vec->envs, vec->gpu_log, vec->total_agents, 0, tag);
    cudaMemcpy(&aggregate, vec->gpu_log, sizeof(Log), cudaMemcpyDeviceToHost);
#else
    for (int i = 0; i < vec->size; i++) {
        Env* env = &vec->envs[i];
        if (env->tag != tag || env->log.n == 0.0f) continue;
        float* log = (float*)&env->log;
        for (int j = 0; j < num_keys; j++) acc[j] += log[j];
    }
#endif

    float n = aggregate.n;
    if (n > 0.0f) {
        for (int j = 0; j < num_keys; j++) acc[j] /= n;
        puf_log(&aggregate, out);
    }
    dict_set(out, "n", n);
}

// Zero advantages on frozen-bank rows so prio_replay never samples them. Frozen
// rollout rows hold actions/logprobs from the frozen policy; training the
// primary's PPO on them produces garbage ratios and poisoned gradients.
__global__ void zero_frozen_advantages_kernel(precision_t* advantages,
        int agents_per_buffer, int primary_per_buffer, int total_rows, int horizon) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = total_rows * horizon;
    if (idx >= total) {
        return;
    }
    int row = idx / horizon;
    int rel = row % agents_per_buffer;
    if (rel >= primary_per_buffer) {
        advantages[idx] = from_float(0.0f);
    }
}

// Cooperative row copy (int4 when 16-byte aligned).
__device__ void copy_bytes(
        const char* src, char* dst,
        int src_row, int dst_row, int row_bytes) {
    const char* s = src + (int64_t)src_row * row_bytes;
    char* d = dst + (int64_t)dst_row * row_bytes;
    if (((uintptr_t)s & 15) == 0 && ((uintptr_t)d & 15) == 0 && row_bytes >= 16) {
        int n16 = row_bytes >> 4;
        const int4* __restrict__ s4 = reinterpret_cast<const int4*>(s);
        int4* __restrict__ d4 = reinterpret_cast<int4*>(d);
        for (int i = threadIdx.x; i < n16; i += blockDim.x) {
            d4[i] = s4[i];
        }
        for (int i = (n16 << 4) + threadIdx.x; i < row_bytes; i += blockDim.x) {
            d[i] = s[i];
        }
    } else {
        for (int i = threadIdx.x; i < row_bytes; i += blockDim.x) {
            d[i] = s[i];
        }
    }
}

#define SELECT_COPY_THREADS 256
// One block per minibatch segment: copy all base train fields for idx[mb].
// Row byte sizes are host-precomputed. Mask is an optional follow-up kernel.
// When initial_states.data is set (carry path), fold state gather here.
__global__ void select_copy(RolloutBuf rollouts, TrainGraph graph,
        int* idx, precision_t* advantages, float* mb_prio,
        int obs_rb, int act_rb, int lp_rb, int term_rb, int horizon) {
    int mb = blockIdx.x;
    int src_row = idx[mb];

    copy_bytes((const char*)rollouts.observations.data,
        (char*)graph.mb_obs.data, src_row, mb, obs_rb);
    copy_bytes((const char*)rollouts.actions.data,
        (char*)graph.mb_actions.data, src_row, mb, act_rb);
    copy_bytes((const char*)rollouts.logprobs.data,
        (char*)graph.mb_logprobs.data, src_row, mb, lp_rb);

    int srh = (int64_t)src_row * horizon;
    int drh = (int64_t)mb * horizon;
    precision_t* s_values = rollouts.values.data + srh;
    precision_t* s_adv = advantages + srh;
    precision_t* d_values = graph.mb_values.data + drh;
    precision_t* d_adv = graph.mb_advantages.data + drh;
    precision_t* d_returns = graph.mb_returns.data + drh;
    for (int i = threadIdx.x; i < horizon; i += blockDim.x) {
        precision_t val = s_values[i];
        precision_t adv = s_adv[i];
        d_values[i] = val;
        d_adv[i] = adv;
#ifndef PRECISION_FLOAT
        d_returns[i] = __hadd(val, adv);
#else
        d_returns[i] = val + adv;
#endif
    }

    if (threadIdx.x == 0) {
        graph.mb_prio.data[mb] = from_float(mb_prio[mb]);
    }
    copy_bytes((const char*)rollouts.terminals.data,
        (char*)graph.mb_terminals.data, src_row, mb, term_rb);

    if (rollouts.initial_states.data != nullptr) {
        int L = (int)rollouts.initial_states.shape[0];
        int H = (int)rollouts.initial_states.shape[2];
        int total = L * H;
        for (int i = threadIdx.x; i < total; i += blockDim.x) {
            int layer = i / H;
            int h = i % H;
            long src_idx = ((long)layer * rollouts.initial_states.shape[1] + src_row) * H + h;
            long dst_idx = ((long)layer * graph.mb_state.shape[1] + mb) * H + h;
            graph.mb_state.data[dst_idx] = rollouts.initial_states.data[src_idx];
        }
    }
}

__global__ void select_copy_mask(const char* src, char* dst, int* idx, int row_bytes) {
    int mb = blockIdx.x;
    copy_bytes(src, dst, idx[mb], mb, row_bytes);
}

__global__ void pack_action_mask(unsigned char* __restrict__ dst,
        const unsigned char* __restrict__ src, int B, int mask_size,
        int packed_stride) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * packed_stride) return;
    int b = idx / packed_stride;
    int byte = idx % packed_stride;
    int base = byte * 8;
    unsigned char bits = 0;
    #pragma unroll
    for (int bit = 0; bit < 8; bit++) {
        int action = base + bit;
        if (action < mask_size && src[(long)b * mask_size + action]) {
            bits |= (unsigned char)(1u << bit);
        }
    }
    dst[idx] = bits;
}

// Transpose dims 0,1: [A, B, C] -> [B, A, C]. For 2D, pass C=1.
__global__ void transpose_102(precision_t* dst,
        precision_t* src, int A, int B, int C) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = A * B * C;
    if (idx >= total) {
        return;
    }
    int a = idx / (B * C);
    int rem = idx % (B * C);
    int b = rem / C;
    int c = rem % C;
    dst[b * A * C + a * C + c] = src[idx];
}

__global__ void transpose_102(unsigned char* dst,
        const unsigned char* src, int A, int B, int C) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = A * B * C;
    if (idx >= total) return;
    int a = idx / (B * C);
    int rem = idx % (B * C);
    int b = rem / C;
    int c = rem % C;
    dst[b * A * C + a * C + c] = src[idx];
}

// Sparse row scatter: dst[idx[i], :] = src[i, :]. One thread per element.
__global__ void scatter_rows(precision_t* dst, int* idx, const precision_t* src,
        int num_idx, int row_elems) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    int total = num_idx * row_elems;
    if (i >= total) {
        return;
    }
    int mb = i / row_elems;
    int e = i % row_elems;
    dst[(int64_t)idx[mb] * row_elems + e] = src[i];
}

float cosine_annealing(float lr_base, float lr_min, long t, long T) {
    if (T == 0) {
        return lr_base;
    }
    // double ratio: t/T can exceed 2^24 (float mantissa) on long runs.
    double ratio = (double)t / (double)T;
    if (ratio < 0.0) {
        ratio = 0.0;
    }
    if (ratio > 1.0) {
        ratio = 1.0;
    }
    return lr_min + 0.5f * (lr_base - lr_min) * (1.0f + (float)cos(M_PI * ratio));
}

__global__ void fill_precision_kernel(precision_t* dst, precision_t val, int n) {
    for (int idx = blockIdx.x * blockDim.x + threadIdx.x; idx < n;
            idx += blockDim.x * gridDim.x) {
        dst[idx] = val;
    }
}

__global__ void clamp_precision_kernel(precision_t* dst, float lo, float hi, int n) {
    for (int idx = blockIdx.x * blockDim.x + threadIdx.x; idx < n;
            idx += blockDim.x * gridDim.x) {
        float v = to_float(dst[idx]);
        dst[idx] = from_float(fminf(fmaxf(v, lo), hi));
    }
}

void train_impl(PuffeRL& pufferl, RolloutBuf* src_arg) {
    HypersT& hypers = pufferl.hypers;
    RolloutBuf src = src_arg ? *src_arg : pufferl.rollouts;
    cudaStream_t train_stream = pufferl.train_stream;

    cudaEventRecord(pufferl.profile.events[0], train_stream);  // pre-loop start

    // Transpose from rollout layout (T, B, ...) to train layout (B, T, ...)
    RolloutBuf& rollouts = pufferl.train_rollouts;
    PrecisionTensor& advantages_puf = pufferl.advantages_puf;

    int T = src.observations.shape[0];
    int B = src.observations.shape[1];
    int obs_size = (ndim(src.observations.shape) >= 3) ? src.observations.shape[2] : 1;
    int num_atns = (ndim(src.actions.shape) >= 3) ? src.actions.shape[2] : 1;

    transpose_102<<<grid_size(T * B * obs_size), BLOCK_SIZE, 0, train_stream>>>(
        rollouts.observations.data, src.observations.data, T, B, obs_size);
    transpose_102<<<grid_size(T * B * num_atns), BLOCK_SIZE, 0, train_stream>>>(
        rollouts.actions.data, src.actions.data, T, B, num_atns);
    transpose_102<<<grid_size(T * B), BLOCK_SIZE, 0, train_stream>>>(
        rollouts.logprobs.data, src.logprobs.data, T, B, 1);
    transpose_102<<<grid_size(T * B), BLOCK_SIZE, 0, train_stream>>>(
        rollouts.rewards.data, src.rewards.data, T, B, 1);
    transpose_102<<<grid_size(T * B), BLOCK_SIZE, 0, train_stream>>>(
        rollouts.terminals.data, src.terminals.data, T, B, 1);
    transpose_102<<<grid_size(T * B), BLOCK_SIZE, 0, train_stream>>>(
        rollouts.ratio.data, src.ratio.data, T, B, 1);
    transpose_102<<<grid_size(T * B), BLOCK_SIZE, 0, train_stream>>>(
        rollouts.values.data, src.values.data, T, B, 1);
    int mask_c = src.action_mask.shape[2];
    transpose_102<<<grid_size(T * B * mask_c), BLOCK_SIZE, 0, train_stream>>>(
        rollouts.action_mask.data, src.action_mask.data, T, B, mask_c);

    // We hard-clamp rewards to -1, 1. Our envs are mostly designed to respect this range
    clamp_precision_kernel<<<grid_size(numel(rollouts.rewards.shape)), BLOCK_SIZE, 0, train_stream>>>(
        rollouts.rewards.data, -1.0f, 1.0f, numel(rollouts.rewards.shape));

    // Treat rollout data as on-policy for advantage. PPO clipping still uses
    // behavior logprobs captured by the actor snapshot.
    fill_precision_kernel<<<grid_size(numel(rollouts.ratio.shape)), BLOCK_SIZE, 0, train_stream>>>(
        rollouts.ratio.data, from_float(1.0f), numel(rollouts.ratio.shape));

    int batch_size = hypers.total_agents * hypers.horizon;
    float prio_beta0 = hypers.prio_beta0;
    float prio_alpha = hypers.prio_alpha;
    bool anneal_lr = hypers.anneal_lr;
    int current_epoch = pufferl.epoch;

    int total_epochs = hypers.total_timesteps / batch_size;
    if (anneal_lr) {
        float lr_min = hypers.min_lr_ratio * hypers.lr;
        float lr = cosine_annealing(hypers.lr, lr_min, current_epoch, total_epochs);
        cudaMemcpy(pufferl.muon.lr_puf.data, &lr,
            sizeof(float), cudaMemcpyHostToDevice);
    }

    // Annealed entropy coefficient — same cosine shape as lr. With PG signal
    // alive, the entropy bonus that kept early-training exploratory becomes
    // load-bearing dead weight late in training; cosine-decay frees the policy
    // to commit harder on what it has already learned.
    float current_ent_coef = hypers.ent_coef;
    if (hypers.anneal_ent_coef) {
        float ent_min = hypers.min_ent_coef_ratio * hypers.ent_coef;
        current_ent_coef = cosine_annealing(hypers.ent_coef, ent_min,
                                            current_epoch, total_epochs);
    }
    // Device ptr for ent_coef so CUDA graphs do not bake host by-value.
    cudaMemcpyAsync(pufferl.ppo_bufs_puf.ent_coef.data, &current_ent_coef,
        sizeof(float), cudaMemcpyHostToDevice, train_stream);

    // Annealed priority exponent
    float anneal_beta = prio_beta0 + (1.0f - prio_beta0) * prio_alpha * (float)current_epoch/(float)total_epochs;
    TrainGraph& graph = pufferl.train_buf;
    cudaEventRecord(pufferl.profile.events[1], train_stream);  // pre-loop end

    // Advantage + prio CDF once per train; minibatches only re-sample indices.
    cudaMemsetAsync(advantages_puf.data, 0,
        numel(advantages_puf.shape) * sizeof(precision_t), train_stream);
    profile_begin("compute_advantage", hypers.profile);
    puff_advantage_cuda(rollouts.values, rollouts.rewards, rollouts.terminals,
        rollouts.ratio, advantages_puf, hypers.gamma, hypers.gae_lambda,
        hypers.vtrace_rho_clip, hypers.vtrace_c_clip, train_stream);
    if (pufferl.num_frozen_banks > 0) {
        int apb = hypers.total_agents / hypers.num_buffers;
        int rows = advantages_puf.shape[0];
        int horizon = advantages_puf.shape[1];
        int total = rows * horizon;
        zero_frozen_advantages_kernel<<<grid_size(total), BLOCK_SIZE, 0, train_stream>>>(
            advantages_puf.data, apb, pufferl.vec->bank_layout[1], rows, horizon);
    }
    profile_end(hypers.profile);

    profile_begin("compute_prio", hypers.profile);
    int agents_per_buffer = hypers.total_agents / hypers.num_buffers;
    int primary_per_buffer = pufferl.vec->bank_layout[1];
    int primary_agents = primary_per_buffer * hypers.num_buffers;
    prio_build_cdf_cuda(advantages_puf, prio_alpha,
        pufferl.prio_bufs, agents_per_buffer, primary_per_buffer, train_stream);
    profile_end(hypers.profile);

    long* train_rng_offset = pufferl.rng_offset_puf.data + hypers.num_buffers;
    int primary_batch_size = primary_agents * hypers.horizon;
    assert(hypers.minibatch_size <= primary_batch_size
        && "minibatch_size exceeds primary-policy rollout size");
    int total_minibatches = hypers.replay_ratio
        * primary_batch_size / hypers.minibatch_size;
    int minibatches_per_epoch = primary_batch_size / hypers.minibatch_size;
    if (hypers.epoch_sampling) {
        assert(hypers.prio_alpha == 0.0f
            && "train.epoch_sampling requires train.prio_alpha=0");
        assert(primary_batch_size % hypers.minibatch_size == 0
            && "epoch sampling requires primary batch divisible by minibatch_size");
    }
    int completed_emag_epochs = 0;
    for (int mb = 0; mb < total_minibatches; ++mb) {
        cudaEventRecord(pufferl.profile.events[2], train_stream);  // start of misc (overwritten each iter)

        profile_begin("compute_prio", hypers.profile);
        if (hypers.epoch_sampling) {
            int within_epoch = mb % minibatches_per_epoch;
            if (within_epoch == 0) {
                uint64_t shuffle_seed = pufferl.seed
                    ^ ((uint64_t)pufferl.epoch << 32)
                    ^ (uint64_t)(mb / minibatches_per_epoch + 1);
                puf_shuffle_rows<<<1, 1, 0, train_stream>>>(
                    pufferl.prio_bufs.permutation.data, primary_agents,
                    shuffle_seed);
            }
            int segments = pufferl.prio_bufs.idx.shape[0];
            puf_epoch_sample<<<grid_size(segments), BLOCK_SIZE, 0, train_stream>>>(
                pufferl.prio_bufs.idx.data,
                pufferl.prio_bufs.mb_prio.data,
                pufferl.prio_bufs.permutation.data,
                within_epoch * segments, segments,
                agents_per_buffer, primary_per_buffer);
        } else {
            prio_sample_cuda(anneal_beta, pufferl.prio_bufs, pufferl.seed,
                train_rng_offset, train_stream);
        }
        profile_end(hypers.profile);

        profile_begin("train_select_and_copy", hypers.profile);
        // reset_every_horizon: train from zeros, no carry buffer. Else: gather from
        // initial_states slot (always allocated when !reset_every_horizon).
        RolloutBuf sel_src = rollouts;
        if (hypers.reset_every_horizon) {
            cudaMemsetAsync(graph.mb_state.data, 0,
                numel(graph.mb_state.shape) * sizeof(precision_t), train_stream);
            sel_src.initial_states = PrecisionTensor();
        } else {
            int slot = hypers.async ? pufferl.async_ready_slot : 0;
            sel_src.initial_states = initial_states_slot(src.initial_states, slot);
        }
        int mb_segs = pufferl.prio_bufs.idx.shape[0];
        int* sel_idx = pufferl.prio_bufs.idx.data;
        int pe = (int)sizeof(precision_t);
        int obs_rb = (numel(rollouts.observations.shape)
            / rollouts.observations.shape[0]) * pe;
        int act_rb = (numel(rollouts.actions.shape)
            / rollouts.actions.shape[0]) * pe;
        int lp_rb = (numel(rollouts.logprobs.shape)
            / rollouts.logprobs.shape[0]) * pe;
        int term_rb = (numel(rollouts.terminals.shape)
            / rollouts.terminals.shape[0]) * pe;
        int sel_horizon = rollouts.values.shape[1];
        select_copy<<<mb_segs, SELECT_COPY_THREADS, 0, train_stream>>>(
            sel_src, graph, sel_idx, advantages_puf.data,
            pufferl.prio_bufs.mb_prio.data,
            obs_rb, act_rb, lp_rb, term_rb, sel_horizon);
        int mask_rb = numel(rollouts.action_mask.shape)
            / rollouts.action_mask.shape[0];
        select_copy_mask<<<mb_segs, SELECT_COPY_THREADS, 0, train_stream>>>(
            (const char*)rollouts.action_mask.data,
            (char*)graph.mb_action_mask.data, sel_idx, mask_rb);
        profile_end(hypers.profile);

        cudaEventRecord(pufferl.profile.events[3], train_stream);  // end misc / start forward
        profile_begin("train_forward_backward", hypers.profile);
        if (hypers.cudagraphs && pufferl.train_cudagraph != nullptr) {
            cudaGraphLaunch(pufferl.train_cudagraph, train_stream);
        } else {
            if (hypers.cudagraphs) {
                assert(cudaStreamBeginCapture(train_stream, cudaStreamCaptureModeThreadLocal) == cudaSuccess
                        && "cudaStreamBeginCapture failed");
            }

            cudaStream_t stream = train_stream;
            PrecisionTensor obs_puf = graph.mb_obs;
            PrecisionTensor state_puf = graph.mb_state;
            PrecisionTensor terminals_puf = graph.mb_terminals;
            PrecisionTensor magnet_out;
            PrecisionTensor magnet_logstd;
            if (hypers.emag_kl_coef > 0.0f) {
                magnet_out = policy_forward_train(&pufferl.policy,
                    pufferl.magnet_weights, pufferl.magnet_activations,
                    obs_puf, state_puf, terminals_puf, stream);
                if (pufferl.is_continuous) {
                    DecoderWeights* mdw = (DecoderWeights*)
                        pufferl.magnet_weights.decoder;
                    magnet_logstd = mdw->logstd;
                }
            }
            PrecisionTensor dec_puf = policy_forward_train(&pufferl.policy, pufferl.weights,
                pufferl.train_activations, obs_puf, state_puf, terminals_puf, stream);
            PrecisionTensor p_logstd;
            if (pufferl.is_continuous) {
                DecoderWeights* dw_train = (DecoderWeights*)
                    pufferl.weights.decoder;
                p_logstd = dw_train->logstd;
            }

            ppo_loss_fwd_bwd(dec_puf, p_logstd, magnet_out, magnet_logstd, graph,
                pufferl.act_sizes_puf, pufferl.losses_puf,
                hypers.clip_coef, hypers.vf_clip_coef, hypers.vf_coef,
                pufferl.ppo_bufs_puf.ent_coef.data,
                hypers.emag_kl_coef,
                pufferl.ppo_bufs_puf, pufferl.is_continuous, stream);

            FloatTensor grad_logits_puf = pufferl.ppo_bufs_puf.grad_logits;
            FloatTensor grad_logstd_puf = pufferl.is_continuous ? pufferl.ppo_bufs_puf.grad_logstd : FloatTensor();
            FloatTensor grad_values_puf = pufferl.ppo_bufs_puf.grad_values;
            policy_backward(&pufferl.policy, pufferl.weights, pufferl.train_activations,
                grad_logits_puf, grad_logstd_puf, grad_values_puf, stream);

            if (pufferl.nccl_comm != nullptr && hypers.world_size > 1) {
                ncclAllReduce(pufferl.grad_puf.data, pufferl.grad_puf.data,
                    numel(pufferl.grad_puf.shape), NCCL_PRECISION, ncclAvg,
                    pufferl.nccl_comm, stream);
            }
            muon_step(&pufferl.muon, pufferl.master_weights,
                pufferl.grad_puf, hypers.max_grad_norm, stream);
            if (USE_BF16) {
                int n = numel(pufferl.param_puf.shape);
                cast<<<grid_size(n), BLOCK_SIZE, 0, stream>>>(
                    pufferl.param_puf.data, pufferl.master_weights.data, n);
            }
            if (hypers.cudagraphs) {
                cudaGraph_t _graph;
                assert(cudaStreamEndCapture(train_stream, &_graph) == cudaSuccess
                        && "cudaStreamEndCapture failed");
                assert(cudaGraphInstantiate(&pufferl.train_cudagraph, _graph, 0)
                        == cudaSuccess && "cudaGraphInstantiate failed");
                cudaGraphDestroy(_graph);
                // Capture records without executing; run once so this step has effects.
                assert(cudaGraphLaunch(pufferl.train_cudagraph, train_stream) == cudaSuccess
                        && "cudaGraphLaunch failed");
            }
        }
        profile_end(hypers.profile);

        // This version is consistent with PufferLib 3.0. One of the major algorithmic
        // questions remaining is how and when to update value and advantage estimates.
        int num_idx = numel(pufferl.prio_bufs.idx.shape);
        int ratio_row = numel(graph.mb_ratio.shape) / graph.mb_ratio.shape[0];
        int value_row = graph.mb_newvalue.shape[1];
        scatter_rows<<<grid_size(num_idx * ratio_row), BLOCK_SIZE, 0, train_stream>>>(
            rollouts.ratio.data, pufferl.prio_bufs.idx.data,
            graph.mb_ratio.data, num_idx, ratio_row);
        scatter_rows<<<grid_size(num_idx * value_row), BLOCK_SIZE, 0, train_stream>>>(
            rollouts.values.data, pufferl.prio_bufs.idx.data,
            graph.mb_newvalue.data, num_idx, value_row);

        int completed_epochs = (mb + 1) * hypers.minibatch_size / batch_size;
        if (hypers.emag_kl_coef > 0.0f
                && completed_epochs > completed_emag_epochs) {
            update_emag(pufferl, hypers.emag_tau, train_stream);
            completed_emag_epochs = completed_epochs;
        }
        cudaEventRecord(pufferl.profile.events[4], train_stream);  // end forward
    }

    if (hypers.emag_kl_coef > 0.0f) {
        float replay_epochs = (float)total_minibatches
            * hypers.minibatch_size / batch_size;
        float partial_epoch = replay_epochs - completed_emag_epochs;
        if (partial_epoch > 0.0f) {
            float partial_tau = 1.0f
                - powf(1.0f - hypers.emag_tau, partial_epoch);
            update_emag(pufferl, partial_tau, train_stream);
        }
    }

    cudaStreamSynchronize(train_stream);

    if (total_minibatches > 0) {
        float ms;
        // Pre-loop setup (transpose, advantage, allocs)
        cudaEventElapsedTime(&ms, pufferl.profile.events[0], pufferl.profile.events[1]);
        pufferl.profile.accum[PROF_TRAIN_MISC] += ms;
        // In-loop misc (last iteration, representative) scaled by count
        cudaEventElapsedTime(&ms, pufferl.profile.events[2], pufferl.profile.events[3]);
        pufferl.profile.accum[PROF_TRAIN_MISC] += ms * total_minibatches;
        // In-loop forward (last iteration, representative) scaled by count
        cudaEventElapsedTime(&ms, pufferl.profile.events[3], pufferl.profile.events[4]);
        pufferl.profile.accum[PROF_TRAIN_MODEL] += ms * total_minibatches;
    }
    pufferl.epoch += 1;
}


// Load frozen bank weights (flat fp32 checkpoint). Safe between rollouts —
// graphs hold the pointer, not a copy of the data.
// --- Checkpoint I/O (load/save weights) ---
void mkdir_p(const char* path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
                fprintf(stderr, "failed to create directory %s: %s\n", tmp, strerror(errno));
                exit(1);
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
        fprintf(stderr, "failed to create directory %s: %s\n", tmp, strerror(errno));
        exit(1);
    }
}

void puf_find_latest_checkpoint(const char* dir,
        char* out, size_t out_size, time_t* best_time) {
    DIR* dp = opendir(dir);
    if (!dp) {
        return;
    }

    struct dirent* ent = NULL;
    while ((ent = readdir(dp))) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }

        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);

        struct stat st;
        if (stat(path, &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            puf_find_latest_checkpoint(path, out, out_size, best_time);
        } else if (S_ISREG(st.st_mode)) {
            size_t plen = strlen(path);
            if (plen < 4 || strcmp(path + plen - 4, ".bin") != 0) continue;
            // A self-play run may create a bootstrap copy at step zero.  It is
            // not a resumable training checkpoint, so `latest` must never
            // select it over a real learned checkpoint.
            if (strcmp(ent->d_name, "0000000000000000.bin") == 0) continue;
            if (st.st_ctime < *best_time) continue;
            *best_time = st.st_ctime;
            snprintf(out, out_size, "%s", path);
        }
    }

    closedir(dp);
}

const char* puf_checkpoint_path_key(Ini* ini, const char* key,
        char* out, size_t out_size) {
    const char* load_path = puf_ini_get_str(ini, "base", key);
    if (!load_path || strcmp(load_path, "None") == 0) {
        return NULL;
    }

    if (strcmp(load_path, "latest") != 0) {
        return load_path;
    }

    char root[2048];
    snprintf(root, sizeof(root), "%s/%s",
        puf_ini_get_str(ini, "base", "checkpoint_dir"),
        puf_ini_get_str(ini, "base", "env_name"));

    out[0] = 0;
    time_t best_time = 0;
    puf_find_latest_checkpoint(root, out, out_size, &best_time);
    if (!out[0]) {
        fprintf(stderr, "no learned .bin checkpoints found in %s\n", root);
        exit(1);
    }
    return out;
}

void puf_save_tensor(FloatTensor weights, const char* path) {
    int64_t nbytes = numel(weights.shape) * sizeof(float);
    char* buf = (char*)malloc(nbytes);
    cudaMemcpy(buf, weights.data, nbytes, cudaMemcpyDeviceToHost);
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s.tmp.%d", path, getpid());
    FILE* fp = fopen(tmp, "wb");
    if (!fp) {
        fprintf(stderr, "failed to open %s for writing\n", tmp);
        free(buf);
        exit(1);
    }
    if (fwrite(buf, 1, nbytes, fp) != (size_t)nbytes) {
        fprintf(stderr, "failed to write weights to %s\n", tmp);
        fclose(fp);
        free(buf);
        exit(1);
    }
    fclose(fp);
    free(buf);
    if (rename(tmp, path) != 0) {
        fprintf(stderr, "failed to publish weights to %s\n", path);
        exit(1);
    }
}

void puf_save_weights(PuffeRL* p, const char* path) {
    puf_save_tensor(p->master_weights, path);
    if (p->hypers.emag_kl_coef > 0.0f) {
        char magnet_path[8192];
        snprintf(magnet_path, sizeof(magnet_path), "%s.emag", path);
        puf_save_tensor(p->magnet_master_weights, magnet_path);
    }
}

void puf_load_weights_into(FloatTensor dst, PrecisionTensor params,
        cudaStream_t stream, const char* path) {
    int64_t nbytes = numel(dst.shape) * sizeof(float);
    struct stat info;
    if (stat(path, &info) != 0) {
        fprintf(stderr, "failed to stat weights %s: %s\n", path, strerror(errno));
        exit(1);
    }
    if (info.st_size != nbytes) {
        fprintf(stderr, "incompatible weights %s: got %lld bytes, expected %lld "
            "for this policy architecture\n", path, (long long)info.st_size,
            (long long)nbytes);
        exit(1);
    }
    FILE* fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "failed to open %s for reading\n", path);
        exit(1);
    }
    char* buf = (char*)malloc(nbytes);
    size_t nread = fread(buf, 1, nbytes, fp);
    fclose(fp);
    if ((int64_t)nread != nbytes) {
        fprintf(stderr, "failed to read weights from %s\n", path);
        free(buf);
        exit(1);
    }
    cudaMemcpy(dst.data, buf, nbytes, cudaMemcpyHostToDevice);
    free(buf);
    if (USE_BF16) {
        int n = numel(params.shape);
        cast<<<grid_size(n), BLOCK_SIZE, 0, stream>>>(params.data, dst.data, n);
    }
}

void pufferl_load_frozen_bank(PuffeRL* pufferl, int bank_idx, const char* path) {
    WeightBank* bank = &pufferl->frozen_banks[bank_idx];
    puf_load_weights_into(bank->master_weights, bank->param_puf,
        pufferl->default_stream, path);
    cudaDeviceSynchronize();
}

// Bootstrap a frozen opponent from the learner without creating a fake
// zero-step checkpoint.  This is used only when training starts from a fresh
// policy; resumed training uses the requested checkpoint path directly.
void pufferl_copy_frozen_bank_from_learner(PuffeRL* pufferl, int bank_idx) {
    WeightBank* bank = &pufferl->frozen_banks[bank_idx];
    int64_t learner_n = numel(pufferl->master_weights.shape);
    int64_t bank_n = numel(bank->master_weights.shape);
    if (learner_n != bank_n) {
        fprintf(stderr, "fresh self-play bootstrap requires learner and frozen "
            "bank architectures to have the same parameter count: learner=%lld "
            "bank=%lld\n", (long long)learner_n, (long long)bank_n);
        exit(1);
    }
    cudaMemcpyAsync(bank->master_weights.data, pufferl->master_weights.data,
        (size_t)learner_n * sizeof(float), cudaMemcpyDeviceToDevice,
        pufferl->default_stream);
    if (USE_BF16) {
        cast<<<grid_size((int)bank_n), BLOCK_SIZE, 0, pufferl->default_stream>>>(
            bank->param_puf.data, bank->master_weights.data, (int)bank_n);
    }
    cudaStreamSynchronize(pufferl->default_stream);
}

// Loading updates the learner/master tensors.  Async rollout inference uses
// a separate actor snapshot, so it must be refreshed before the first
// bootstrap save or rollout; otherwise the actor can run the random init.
void pufferl_sync_loaded_policy(PuffeRL* pufferl) {
    cudaStreamSynchronize(pufferl->default_stream);
    if (pufferl->hypers.async) {
        puf_copy(&pufferl->actor_param_puf, &pufferl->param_puf,
            pufferl->default_stream);
        cudaStreamSynchronize(pufferl->default_stream);
    }
}

// Die on OOM in the train worker. Sweep observes failed workers as bad samples and continues.
// fp32 master weights: alias param buffer in float mode; separate fp32 copy in bf16.
// cast_now: copy param→master now (primary after init). Frozen banks load later.
static void master_weights_setup(FloatTensor* mw, PrecisionTensor* param,
        bool cast_now, cudaStream_t stream) {
    long n = numel(param->shape);
    if (USE_BF16) {
        *mw = (FloatTensor){.shape = {n}};
        mw->data = (float*)xcuda((size_t)n * sizeof(float));
        if (cast_now) {
            cast<<<grid_size(n), BLOCK_SIZE, 0, stream>>>(mw->data, param->data, n);
        }
    } else {
        *mw = (FloatTensor){.data = (float*)param->data, .shape = {n}};
    }
}

void create_allocator_or_die(const char* name, Allocator* alloc) {
    cudaError_t err = alloc_create(alloc);
    if (err != cudaSuccess) {
        fprintf(stderr, "create_pufferl: alloc_create(%s) failed for %ld bytes: %s\n",
            name, alloc->total_bytes, cudaGetErrorString(err));
        exit(1);
    }
}

double wall_clock() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

PuffeRL* create_pufferl(Ini* ini, TrainContext* ctx) {
    HypersT hypers = {
        .horizon = puf_ini_get_int(ini, "train", "horizon"),
        .total_agents = puf_ini_get_int(ini, "vec", "total_agents"),
        .num_buffers = puf_ini_get_int(ini, "vec", "num_buffers"),
        .hidden_size = puf_ini_get_int(ini, "policy", "hidden_size"),
        .num_layers = puf_ini_get_int(ini, "policy", "num_layers"),
        .lr = puf_ini_get_float(ini, "train", "learning_rate"),
        .min_lr_ratio = puf_ini_get_float(ini, "train", "min_lr_ratio"),
        .anneal_lr = puf_ini_get_int(ini, "train", "anneal_lr") != 0,
        .momentum = puf_ini_get_float(ini, "train", "momentum"),
        .minibatch_size = puf_ini_get_int(ini, "train", "minibatch_size"),
        .replay_ratio = puf_ini_get_float(ini, "train", "replay_ratio"),
        .total_timesteps = puf_ini_get_long(ini, "train", "total_timesteps"),
        .max_grad_norm = puf_ini_get_float(ini, "train", "max_grad_norm"),
        .clip_coef = puf_ini_get_float(ini, "train", "clip_coef"),
        .vf_clip_coef = puf_ini_get_float(ini, "train", "vf_clip_coef"),
        .vf_coef = puf_ini_get_float(ini, "train", "vf_coef"),
        .ent_coef = puf_ini_get_float(ini, "train", "ent_coef"),
        .emag_kl_coef = puf_ini_get_float(ini, "train", "emag_kl_coef"),
        .emag_tau = puf_ini_get_float(ini, "train", "emag_tau"),
        .min_ent_coef_ratio = puf_ini_get_float(ini, "train", "min_ent_coef_ratio"),
        .anneal_ent_coef = puf_ini_get_int(ini, "train", "anneal_ent_coef") != 0,
        .gamma = puf_ini_get_float(ini, "train", "gamma"),
        .gae_lambda = puf_ini_get_float(ini, "train", "gae_lambda"),
        .vtrace_rho_clip = puf_ini_get_float(ini, "train", "vtrace_rho_clip"),
        .vtrace_c_clip = puf_ini_get_float(ini, "train", "vtrace_c_clip"),
        .prio_alpha = puf_ini_get_float(ini, "train", "prio_alpha"),
        .prio_beta0 = puf_ini_get_float(ini, "train", "prio_beta0"),
        .epoch_sampling = puf_ini_get_int(
            ini, "train", "epoch_sampling") != 0,
        .async = puf_ini_get_int(ini, "base", "async") != 0,
        .reset_every_horizon = puf_ini_get_int(ini, "base", "reset_every_horizon") != 0,
        .cudagraphs = puf_ini_get(ini, "base", "cudagraphs") >= 0,
        .profile = puf_ini_get_int(ini, "base", "profile") != 0,
        .rank = ctx->rank,
        .world_size = ctx->world_size,
        .gpu_id = ctx->gpu_id,
        .num_threads = puf_ini_get_int(ini, "vec", "num_threads"),
        .seed = puf_ini_get_int(ini, "base", "seed"),
    };

    Dict vec_kwargs = {0};
    dict_copy(&vec_kwargs, puf_ini_section(ini, "vec", 0));
    Dict* env_kwargs = puf_ini_section(ini, "env", 0);
    ncclUniqueId* nccl_id = ctx->nccl_id;

    PuffeRL* pufferl = (PuffeRL*)calloc(1, sizeof(PuffeRL));
    pufferl->hypers = hypers;
    snprintf(pufferl->env_name, sizeof(pufferl->env_name), "%s", PUFFER_ENV_NAME);

    cudaSetDevice(hypers.gpu_id);
    cublas_init_handle();

    if (hypers.world_size > 1) {
        ncclCommInitRank(&pufferl->nccl_comm, hypers.world_size, *nccl_id, hypers.rank);
        printf("Rank %d/%d: NCCL initialized\n", hypers.rank, hypers.world_size);
    }

    pufferl->seed = (ulong)hypers.seed + hypers.rank;

    // GPU tensors allocated into pufferl->env; vec aliases them.

    int total_agents = (int)dict_get(&vec_kwargs, "total_agents");
    int num_buffers = (int)dict_get(&vec_kwargs, "num_buffers");
    VecEnv* vec = (VecEnv*)calloc(1, sizeof(VecEnv));
    vec->total_agents = total_agents;
    vec->buffers = num_buffers;
    vec->agents_per_buffer = total_agents / num_buffers;
    vec->num_workers = (int)dict_get(&vec_kwargs, "num_threads") / num_buffers;
    if (vec->num_workers < 1) {
        vec->num_workers = 1;
    }
    int frozen_banks = (int)dict_get(&vec_kwargs, "num_frozen_banks");
    int action_mask_size = (int)dict_get(&vec_kwargs, "action_mask_size");
    // Discrete action layout (needed before mask alloc). Continuous dims are size 1.
    int num_action_heads = NUM_ATNS;
    int act_sizes[] = ACT_SIZES;
    int act_n = 0;
    int n_cont = 0;
    int n_disc = 0;
    for (int i = 0; i < num_action_heads; i++) {
        if (act_sizes[i] == 1) {
            n_cont++;
        } else {
            n_disc++;
        }
        act_n += act_sizes[i];
    }
    assert(!(n_cont > 0 && n_disc > 0)
        && "mixed continuous/discrete action spaces not supported");
    bool is_continuous = n_cont > 0;
    pufferl->is_continuous = is_continuous;
    // Always allocate a mask of width act_n: env-written or synthetic all-ones.
    // Continuous ignores it in sample/PPO; one host path for every env.
    if (action_mask_size == 0) {
        action_mask_size = act_n;
    }
    if (action_mask_size != act_n) {
        fprintf(stderr, "vec.action_mask_size=%d but sum(ACT_SIZES)=%d\n",
            action_mask_size, act_n);
        abort();
    }
    vec->num_banks = frozen_banks + 1;
    vec->bank_layout = (int*)xcalloc((size_t)(vec->num_banks + 1) * sizeof(int));
    vec->buffer_env_starts = (int*)xcalloc((size_t)num_buffers * sizeof(int));
    vec->buffer_env_counts = (int*)xcalloc((size_t)num_buffers * sizeof(int));

#ifdef PUFFER_GPU_ENV
    vec->size = total_agents;
    vec->buffer_env_starts[0] = 0;
    vec->buffer_env_counts[0] = total_agents;
#ifdef PUF_GPU_ENV_BANK_LAYOUT
    assert(num_buffers == 1
        && "GPU match environments currently require vec.num_buffers=1");
    assert(total_agents % 2 == 0
        && "GPU two-player match environments require an even total_agents");
    int gpu_matches = total_agents / 2;
    float frozen_pct = (float)dict_get(&vec_kwargs, "frozen_bank_pct");
    int frozen_matches = frozen_banks > 0
        ? (int)(frozen_pct * gpu_matches) : 0;
    if (frozen_matches < 0) frozen_matches = 0;
    if (frozen_matches > gpu_matches) frozen_matches = gpu_matches;
    int primary_count = 2 * (gpu_matches - frozen_matches) + frozen_matches;
    vec->bank_layout[0] = 0;
    vec->bank_layout[1] = primary_count;
    int assigned = 0;
    for (int bank = 0; bank < frozen_banks; bank++) {
        int count = frozen_matches / frozen_banks
            + (bank < frozen_matches % frozen_banks ? 1 : 0);
        assigned += count;
        vec->bank_layout[bank + 2] = primary_count + assigned;
    }
    assert(vec->bank_layout[vec->num_banks] == total_agents
        && "GPU bank layout does not cover every agent");
    vec->envs = puf_envs_create(total_agents, env_kwargs,
        &vec_kwargs, vec->bank_layout);
#else
    vec->envs = puf_envs_create(total_agents, env_kwargs);
    vec->bank_layout[0] = 0;
    vec->bank_layout[vec->num_banks] = vec->agents_per_buffer;
#endif
    vec->gpu_log = (float*)xcuda(sizeof(Log));
#else
    int num_envs = 0;
#ifdef MY_VEC_INIT
    vec->envs = my_vec_init(&num_envs, vec->buffer_env_starts, vec->buffer_env_counts,
        &vec_kwargs, env_kwargs);
#else
    int agents_per_buffer = total_agents / num_buffers;
    Env* envs = (Env*)calloc(total_agents, sizeof(Env));
    num_envs = 0;
    int agents_created = 0;
    while (agents_created < total_agents) {
        envs[num_envs].rng = num_envs;
        puf_init(&envs[num_envs], env_kwargs);
        agents_created += envs[num_envs].num_agents;
        num_envs++;
    }
    envs = (Env*)realloc(envs, num_envs * sizeof(Env));

    /* Population evaluation assigns policy zero to alternating seats within
     * every frozen-bank group. Agent.policy is already the environment-facing
     * bank selector, so this stays generic and requires no game-specific
     * action remapping. */
    int seat_balance = (int)dict_get(&vec_kwargs, "seat_balance");
    if (seat_balance) {
        assert(frozen_banks > 0 && "vec.seat_balance requires frozen banks");
        for (int i = 0; i < num_envs; i++) {
            assert(envs[i].num_agents == 2
                && "vec.seat_balance requires two-agent environments");
            if ((i / frozen_banks) & 1) {
                int policy = envs[i].agents[0].policy;
                envs[i].agents[0].policy = envs[i].agents[1].policy;
                envs[i].agents[1].policy = policy;
            }
        }
    }

    int buf = 0;
    int buf_agents = 0;
    vec->buffer_env_starts[0] = 0;
    vec->buffer_env_counts[0] = 0;
    for (int i = 0; i < num_envs; i++) {
        buf_agents += envs[i].num_agents;
        vec->buffer_env_counts[buf]++;
        if (buf_agents >= agents_per_buffer && buf < num_buffers - 1) {
            buf++;
            vec->buffer_env_starts[buf] = i + 1;
            vec->buffer_env_counts[buf] = 0;
            buf_agents = 0;
        }
    }
    vec->envs = envs;
#endif
    vec->size = num_envs;
    size_t obs_bytes = (size_t)total_agents * OBS_SIZE * sizeof(obs_t);
    vec->observations = (obs_t*)xpin(obs_bytes);
    vec->actions = (float*)xpin((size_t)total_agents * NUM_ATNS * sizeof(float));
    vec->rewards = (float*)xpin((size_t)total_agents * sizeof(float));
    vec->terminals = (float*)xpin((size_t)total_agents * sizeof(float));
#endif

    // GPU side lives in EnvBuf; VecEnv keeps raw aliases for step/reset paths.
    pufferl->env.obs = { .shape = {total_agents, OBS_SIZE} };
    pufferl->env.actions = { .shape = {total_agents, NUM_ATNS} };
    pufferl->env.rewards = { .shape = {total_agents} };
    pufferl->env.terminals = { .shape = {total_agents} };
    pufferl->env.obs.data = (obs_t*)xcuda((size_t)total_agents * OBS_SIZE * sizeof(obs_t));
    pufferl->env.actions.data = (float*)xcuda((size_t)total_agents * NUM_ATNS * sizeof(float));
    pufferl->env.rewards.data = (float*)xcuda((size_t)total_agents * sizeof(float));
    pufferl->env.terminals.data = (float*)xcuda((size_t)total_agents * sizeof(float));
    vec->gpu_observations = pufferl->env.obs.data;
    vec->gpu_actions = pufferl->env.actions.data;
    vec->gpu_rewards = pufferl->env.rewards.data;
    vec->gpu_terminals = pufferl->env.terminals.data;

    vec->action_mask_size = action_mask_size;
    size_t mask_bytes = (size_t)total_agents * action_mask_size * sizeof(unsigned char);
    vec->action_mask = (unsigned char*)xpin(mask_bytes);
    // All-ones = every logit legal. Envs that write real masks overwrite per step.
    memset(vec->action_mask, 1, mask_bytes);
    pufferl->env.action_mask = { .shape = {total_agents, action_mask_size} };
    pufferl->env.action_mask.data = (unsigned char*)xcuda(mask_bytes);
    vec->gpu_action_mask = pufferl->env.action_mask.data;
    cudaMemcpy(vec->gpu_action_mask, vec->action_mask, mask_bytes, cudaMemcpyHostToDevice);
#ifdef PUF_GPU_ENV_BIND_BUFFERS
    puf_envs_bind_buffers(vec->gpu_actions, vec->gpu_action_mask);
#endif
#ifndef PUFFER_GPU_ENV
    for (int buf = 0; buf < num_buffers; buf++) {
            int buf_start = buf * vec->agents_per_buffer;
            int env_start = vec->buffer_env_starts[buf];
            int env_count = vec->buffer_env_counts[buf];

            float frozen_pct = (float)dict_get(&vec_kwargs, "frozen_bank_pct");
            int frozen_envs = (int)(frozen_pct * env_count);
            int frozen_start = env_count - frozen_envs;
            if (frozen_banks == 0 || frozen_pct <= 0.0f) {
                frozen_start = env_count;
            }

            int* counts = (int*)calloc(vec->num_banks, sizeof(int));
            for (int e = 0; e < env_count; e++) {
                Env* eptr = &vec->envs[env_start + e];
                for (int s = 0; s < eptr->num_agents; s++) {
                    int policy = e < frozen_start || eptr->agents[s].policy == 0
                        ? 0 : 1 + (e - frozen_start) % frozen_banks;
                    assert(policy >= 0 && policy < vec->num_banks
                        && "agent policy outside bank range");
                    counts[policy]++;
                }
            }

            int offset = 0;
            for (int b = 0; b < vec->num_banks; b++) {
                if (buf == 0) {
                    vec->bank_layout[b] = offset;
                } else {
                    assert(vec->bank_layout[b] == offset
                        && "bank layout must match across buffers");
                }
                offset += counts[b];
            }
            assert(offset == vec->agents_per_buffer
                && "buffer agent count must equal agents_per_buffer");
            if (buf == 0) {
                vec->bank_layout[vec->num_banks] = offset;
            } else {
                assert(vec->bank_layout[vec->num_banks] == offset
                    && "bank layout must match across buffers");
            }

            int* cursors = (int*)calloc(vec->num_banks, sizeof(int));
            for (int b = 0; b < vec->num_banks; b++) {
                cursors[b] = buf_start + vec->bank_layout[b];
            }
            for (int e = 0; e < env_count; e++) {
                Env* eptr = &vec->envs[env_start + e];
                int tag = 0;
                for (int s = 0; s < eptr->num_agents; s++) {
                    int policy = e < frozen_start || eptr->agents[s].policy == 0
                        ? 0 : 1 + (e - frozen_start) % frozen_banks;
                    if (policy > tag) {
                        tag = policy;
                    }
                    int phys = cursors[policy];
                    eptr->agents[s].observations =
                        vec->observations + (size_t)phys * OBS_SIZE;
                    eptr->agents[s].actions = vec->actions + (size_t)phys * NUM_ATNS;
                    eptr->agents[s].rewards = vec->rewards + phys;
                    eptr->agents[s].terminals = vec->terminals + phys;
                    eptr->agents[s].action_mask =
                        vec->action_mask + (size_t)phys * vec->action_mask_size;
                    cursors[policy]++;
                }
                eptr->tag = tag;
                eptr->boundary_reached = 0;
            }
            free(cursors);
            free(counts);
        }
#endif
    pufferl->vec = vec;

    /* Minibatch must not exceed the primary (non-frozen) rollout, and epoch
     * sampling additionally needs primary_batch % minibatch == 0. Frozen-bank
     * rows shrink the trainable set below total_agents, so snap the sampled
     * minibatch to the largest multiple of horizon that satisfies the active
     * constraints. */
    int primary_agents = pufferl->vec->bank_layout[1] * num_buffers;
    int primary_batch = primary_agents * hypers.horizon;
    if (primary_batch > 0) {
        int mb = hypers.minibatch_size;
        mb = (mb / hypers.horizon) * hypers.horizon;
        if (mb < hypers.horizon) mb = hypers.horizon;
        if (mb > primary_batch) mb = primary_batch;
        if (hypers.epoch_sampling) {
            while (mb > hypers.horizon && primary_batch % mb) {
                mb -= hypers.horizon;
            }
        }
        if (mb != hypers.minibatch_size) {
            fprintf(stderr, "train.minibatch_size adjusted %d -> %d "
                "(primary batch %d%s)\n",
                hypers.minibatch_size, mb, primary_batch,
                hypers.epoch_sampling ? ", epoch sampling" : "");
            hypers.minibatch_size = mb;
            char buf[64];
            snprintf(buf, sizeof(buf), "%d", mb);
            puf_ini_put(ini, "train.minibatch_size", buf);
            pufferl->hypers = hypers;
        }
        assert(hypers.minibatch_size <= primary_batch
            && "minibatch_size exceeds primary-policy rollout size");
        assert(!hypers.epoch_sampling
                || primary_batch % hypers.minibatch_size == 0
            && "epoch sampling requires primary batch divisible by minibatch_size");
    }

    // Vec advantage kernel: 128-bit loads need horizon % ADV_VEC_WIDTH == 0
    // (float: 4, bf16: 8). See puff_advantage in algo.cu.
    assert(hypers.horizon % ADV_VEC_WIDTH == 0
        && "train.horizon must be a multiple of ADV_VEC_WIDTH (4 float / 8 bf16)");

    // Profile events: fixed train markers + one block of 3*H rollout events.
    int H = hypers.horizon;
    for (int i = 0; i < 5; i++) {
        assert(cudaEventCreate(&pufferl->profile.events[i]) == cudaSuccess);
    }
    cudaEvent_t* rev = (cudaEvent_t*)xcalloc(3 * (size_t)H * sizeof(cudaEvent_t));
    pufferl->profile.rollout_gpu_start = rev;
    pufferl->profile.rollout_gpu_end = rev + H;
    pufferl->profile.rollout_env_end = rev + 2 * H;
    pufferl->profile.rollout_horizon = H;
    for (int i = 0; i < 3 * H; i++) {
        assert(cudaEventCreate(&rev[i]) == cudaSuccess);
    }
    nvmlInit();
    nvmlDeviceGetHandleByIndex(hypers.gpu_id, &pufferl->nvml_device);

    int input_size = OBS_SIZE;
    int hidden_size = hypers.hidden_size;
    int num_layers = hypers.num_layers;
    int decoder_output_size = is_continuous ? num_action_heads : act_n;
    int train_horizon = hypers.horizon;
    int minibatch_segments = hypers.minibatch_size / train_horizon;
    int inf_batch = total_agents / num_buffers;
    int B_TT = minibatch_segments * train_horizon;
    int horizon = hypers.horizon;
    int batch = total_agents / num_buffers;

    pufferl->policy = build_policy(pufferl->env_name, input_size, hidden_size,
        num_layers, decoder_output_size, is_continuous, hypers.horizon);

    // Dedicated learner stream (always non-default; nonblocking when async).
    if (hypers.async) {
        assert(cudaStreamCreateWithFlags(&pufferl->train_stream, cudaStreamNonBlocking)
            == cudaSuccess);
    } else {
        assert(cudaStreamCreate(&pufferl->train_stream) == cudaSuccess);
    }

    // Create and allocate params
    Allocator* params = &pufferl->params_alloc;
    Allocator* acts = &pufferl->activations_alloc;
    Allocator* grads = &pufferl->grads_alloc;

    // Buffers for weights, grads, and activations
    pufferl->weights = policy_weights_create(&pufferl->policy, params);
    if (hypers.emag_kl_coef > 0.0f) {
        assert(hypers.emag_tau > 0.0f && hypers.emag_tau <= 1.0f
            && "train.emag_tau must be in (0, 1]");
        pufferl->magnet_weights = policy_weights_create(
            &pufferl->policy, &pufferl->magnet_params_alloc);
        pufferl->magnet_activations = policy_reg_train(
            &pufferl->policy, pufferl->magnet_weights,
            &pufferl->magnet_acts_alloc, &pufferl->magnet_grads_alloc, B_TT);
    }
    if (hypers.async) {
        pufferl->actor_weights = policy_weights_create(&pufferl->policy, &pufferl->actor_params_alloc);
    }
    pufferl->train_activations = policy_reg_train(&pufferl->policy, pufferl->weights, acts, grads, B_TT);
    pufferl->buffer_activations = (PolicyActivations*)xcalloc((size_t)num_buffers * sizeof(PolicyActivations));
    pufferl->buffer_states = (PrecisionTensor*)xcalloc((size_t)num_buffers * sizeof(PrecisionTensor));
    for (int i = 0; i < num_buffers; i++) {
        pufferl->buffer_activations[i] = policy_reg_rollout(
            &pufferl->policy, pufferl->weights, acts, inf_batch);
        pufferl->buffer_states[i] = {
            .shape = {num_layers, batch, hidden_size},
        };
        alloc_register(acts, &pufferl->buffer_states[i]);
    }
    int mask_size = pufferl->vec->action_mask_size;
    int rollout_horizon = hypers.async ? 2 * horizon : horizon;
    register_rollout_buffers(pufferl->rollouts,
        acts, rollout_horizon, total_agents, input_size, num_action_heads, mask_size);
    // Carry path: per-slot initial RNN states. reset_every_horizon zeros mb_state instead.
    if (!hypers.reset_every_horizon) {
        int slots = hypers.async ? 2 : 1;
        pufferl->rollouts.initial_states = {
            .shape = {slots, num_layers, total_agents, hidden_size}};
        alloc_register(acts, &pufferl->rollouts.initial_states);
    }
    register_train_buffers(pufferl->train_buf,
        acts, minibatch_segments, train_horizon, input_size,
        hidden_size, num_action_heads, num_layers, mask_size);
    register_rollout_buffers(pufferl->train_rollouts,
        acts, total_agents, horizon, input_size, num_action_heads, mask_size);
    register_ppo_buffers(pufferl->ppo_bufs_puf,
        acts, minibatch_segments, train_horizon,
        decoder_output_size, is_continuous);
    register_prio_buffers(pufferl->prio_bufs,
        acts, hypers.total_agents, minibatch_segments);

    // Extra cuda buffers just reuse activ allocator
    pufferl->rng_offset_puf = {.shape = {num_buffers + 1}};
    alloc_register(acts, &pufferl->rng_offset_puf);

    pufferl->act_sizes_puf  = {.shape = {num_action_heads}};
    alloc_register(acts, &pufferl->act_sizes_puf);

    pufferl->losses_puf = {.shape = {NUM_LOSSES}};
    alloc_register(acts, &pufferl->losses_puf);

    pufferl->advantages_puf = {.shape = {total_agents, horizon}};
    alloc_register(acts, &pufferl->advantages_puf);

    muon_init(&pufferl->muon, params, hypers.momentum, acts);

    // All buffers allocated here
    create_allocator_or_die("params", params);
    if (hypers.async) {
        create_allocator_or_die("actor_params", &pufferl->actor_params_alloc);
    }
    create_allocator_or_die("grads", grads);
    create_allocator_or_die("acts", acts);
    if (hypers.emag_kl_coef > 0.0f) {
        create_allocator_or_die("magnet_params", &pufferl->magnet_params_alloc);
        create_allocator_or_die("magnet_grads", &pufferl->magnet_grads_alloc);
        create_allocator_or_die("magnet_acts", &pufferl->magnet_acts_alloc);
        pufferl->magnet_param_puf = {
            .data = (precision_t*)pufferl->magnet_params_alloc.mem,
            .shape = {pufferl->magnet_params_alloc.total_elems},
        };
    }

    pufferl->grad_puf = {.data = (precision_t*)grads->mem, .shape = {grads->total_elems}};
    pufferl->param_puf = {.data = (precision_t*)params->mem, .shape = {params->total_elems}};
    if (hypers.async) {
        pufferl->actor_param_puf = {
            .data = (precision_t*)pufferl->actor_params_alloc.mem,
            .shape = {pufferl->actor_params_alloc.total_elems},
        };
    }

    ulong init_seed = hypers.seed;
    policy_init_weights(&pufferl->policy, pufferl->weights, &init_seed, pufferl->default_stream);
    master_weights_setup(&pufferl->master_weights, &pufferl->param_puf, true,
        pufferl->default_stream);
    if (hypers.emag_kl_coef > 0.0f) {
        master_weights_setup(&pufferl->magnet_master_weights,
            &pufferl->magnet_param_puf, false, pufferl->default_stream);
        cudaMemcpyAsync(pufferl->magnet_master_weights.data,
            pufferl->master_weights.data,
            numel(pufferl->master_weights.shape) * sizeof(float),
            cudaMemcpyDeviceToDevice, pufferl->default_stream);
        if (USE_BF16) {
            int n = numel(pufferl->magnet_param_puf.shape);
            cast<<<grid_size(n), BLOCK_SIZE, 0, pufferl->default_stream>>>(
                pufferl->magnet_param_puf.data,
                pufferl->magnet_master_weights.data, n);
        }
    }
    if (hypers.async) {
        puf_copy(&pufferl->actor_param_puf, &pufferl->param_puf, pufferl->default_stream);
        cudaStreamSynchronize(pufferl->default_stream);
    }

    // Per-buffer persistent RNG states
    int agents_per_buf = total_agents / num_buffers;
    pufferl->rng_states = (curandStatePhilox4_32_10_t**)xcalloc((size_t)num_buffers * sizeof(curandStatePhilox4_32_10_t*));
    for (int i = 0; i < num_buffers; i++) {
        pufferl->rng_states[i] = (curandStatePhilox4_32_10_t*)xcuda((size_t)agents_per_buf * sizeof(curandStatePhilox4_32_10_t));
        rng_init<<<grid_size(agents_per_buf), BLOCK_SIZE>>>(
            pufferl->rng_states[i], pufferl->seed + i, agents_per_buf);
    }

    // Post-create initialization
    cudaMemcpy(pufferl->act_sizes_puf.data, act_sizes, num_action_heads * sizeof(int),
        cudaMemcpyHostToDevice);
    cudaMemset(pufferl->losses_puf.data, 0, NUM_LOSSES * sizeof(float));
    cudaMemcpy(pufferl->muon.lr_puf.data, &hypers.lr,
        sizeof(float), cudaMemcpyHostToDevice);
    cudaMemset(pufferl->muon.mb_puf.data, 0,
        numel(pufferl->muon.mb_puf.shape) * sizeof(float));

    // Frozen banks (selfplay/match opponents): rollout-only policy.
    // Arch comes from vec.frozen_bank_*; required when banks > 0.
    int num_frozen = vec->num_banks - 1;
    int frozen_hidden = (int)dict_get(&vec_kwargs, "frozen_bank_hidden_size");
    int frozen_layers = (int)dict_get(&vec_kwargs, "frozen_bank_num_layers");
    assert(!(num_frozen > 0 && (frozen_hidden <= 0 || frozen_layers <= 0))
        && "num_frozen_banks requires frozen_bank_hidden_size and frozen_bank_num_layers > 0");
    pufferl->num_frozen_banks = num_frozen;
    if (num_frozen > 0) {
        pufferl->frozen_banks = (WeightBank*)xcalloc((size_t)num_frozen * sizeof(WeightBank));
    }
    for (int b = 0; b < num_frozen; b++) {
        int slice = vec->bank_layout[b + 2] - vec->bank_layout[b + 1];
        assert(slice > 0 && "frozen bank has no agents");
        WeightBank* bank = &pufferl->frozen_banks[b];
        bank->policy = build_policy(pufferl->env_name, input_size, frozen_hidden,
            frozen_layers, decoder_output_size, is_continuous, hypers.horizon);
        bank->weights = policy_weights_create(&bank->policy, &bank->params_alloc);
        bank->buffer_activations = (PolicyActivations*)xcalloc((size_t)num_buffers * sizeof(PolicyActivations));
        bank->buffer_states = (PrecisionTensor*)xcalloc((size_t)num_buffers * sizeof(PrecisionTensor));
        for (int i = 0; i < num_buffers; i++) {
            bank->buffer_activations[i] = policy_reg_rollout(
                &bank->policy, bank->weights, &bank->acts_alloc, slice);
            bank->buffer_states[i] = {.shape = {frozen_layers, slice, frozen_hidden}};
            alloc_register(&bank->acts_alloc, &bank->buffer_states[i]);
        }
        create_allocator_or_die("frozen_params", &bank->params_alloc);
        create_allocator_or_die("frozen_acts", &bank->acts_alloc);
        bank->param_puf = {
            .data = (precision_t*)bank->params_alloc.mem,
            .shape = {bank->params_alloc.total_elems},
        };
        // Weights loaded later via pufferl_load_frozen_bank; no cast yet.
        master_weights_setup(&bank->master_weights, &bank->param_puf, false,
            pufferl->default_stream);
    }

    // CUDA graphs: allocate graph array only; capture on first real use.
    if (hypers.cudagraphs) {
        int rollout_graph_slots = hypers.async ? 2 : 1;
        pufferl->fused_rollout_cudagraphs = (cudaGraphExec_t*)xcalloc((size_t)rollout_graph_slots * horizon * num_buffers * sizeof(cudaGraphExec_t));
    }
    pufferl->streams = (cudaStream_t*)xcalloc((size_t)num_buffers * sizeof(cudaStream_t));
    for (int i = 0; i < num_buffers; i++) {
        if (hypers.async) {
            assert(cudaStreamCreateWithFlags(&pufferl->streams[i], cudaStreamNonBlocking)
                == cudaSuccess);
        } else {
            assert(cudaStreamCreate(&pufferl->streams[i]) == cudaSuccess);
        }
    }

#ifdef PUFFER_GPU_ENV
    puf_envs_reset(vec->envs, vec->gpu_observations, vec->gpu_rewards,
        vec->gpu_terminals, vec->total_agents);
    cudaDeviceSynchronize();
#else
    {
        // zero-init -> BUF_STARTING.
        vec->worker_state = (int*)xcalloc((size_t)vec->buffers * sizeof(int));
        vec->threads = (pthread_t*)xcalloc((size_t)vec->buffers * sizeof(pthread_t));
        VecThreadArg* args = (VecThreadArg*)xcalloc((size_t)vec->buffers * sizeof(VecThreadArg));
        vec->thread_args = args;
        vec->accum = (float*)xcalloc((size_t)vec->buffers * NUM_VEC_PROF * sizeof(float));
    }
    #pragma omp parallel for schedule(static) num_threads(vec->num_workers)
    for (int i = 0; i < vec->size; i++) {
        puf_reset(&vec->envs[i]);
    }
    cudaMemcpy(vec->gpu_observations, vec->observations,
        (size_t)vec->total_agents * OBS_SIZE * sizeof(obs_t),
        cudaMemcpyHostToDevice);
    cudaMemset(vec->gpu_rewards,   0, vec->total_agents * sizeof(float));
    cudaMemset(vec->gpu_terminals, 0, vec->total_agents * sizeof(float));
    cudaMemcpy(vec->gpu_action_mask, vec->action_mask,
        (size_t)vec->total_agents * vec->action_mask_size * sizeof(unsigned char),
        cudaMemcpyHostToDevice);
    cudaDeviceSynchronize();
    {
        VecThreadArg* args = (VecThreadArg*)vec->thread_args;
        for (int i = 0; i < vec->buffers; i++) {
            args[i].pufferl = pufferl;
            args[i].buf = i;
            pthread_create(&vec->threads[i], NULL, vec_thread_main, &args[i]);
        }
        for (int i = 0; i < vec->buffers; i++) {
            while (__atomic_load_n(&vec->worker_state[i], __ATOMIC_SEQ_CST)
                    != BUF_WAITING) {
            }
        }
    }
#endif

    if (hypers.profile) {
        cudaDeviceSynchronize();
        cudaProfilerStart();
    }

    double now = wall_clock();
    pufferl->start_time = now;
    pufferl->last_log_time = now;
    pufferl->last_log_step = 0;

    dict_clear(&vec_kwargs);
    return pufferl;
}

// OS reclaims memory/CUDA context on exit. This is the intended design.
// All memory is allocated up front and static across training.
void close_pufferl(PuffeRL* p) {
    cudaDeviceSynchronize();
    if (p->hypers.profile) {
        cudaProfilerStop();
    }
    nvmlShutdown();
    VecEnv* vec = p->vec;
#ifdef PUFFER_GPU_ENV
    puf_envs_close(vec->envs);
#else
    __atomic_store_n(&vec->shutdown, 1, __ATOMIC_SEQ_CST);
    for (int i = 0; i < vec->buffers; i++) {
        pthread_join(vec->threads[i], NULL);
    }
    for (int i = 0; i < vec->size; i++) {
        puf_close(&vec->envs[i]);
    }
#ifdef MY_VEC_CLOSE
    my_vec_close(vec->envs);
#endif
#endif
    cudaDeviceSynchronize();
    if (p->nccl_comm != nullptr) {
        ncclCommDestroy(p->nccl_comm);
    }
}

// Dashboard
static int puf_dashboard_tty = 0;
static int puf_dashboard_last_rows = 0;
static int puf_dashboard_last_cols = 0;
static int puf_dashboard_frame = 0;

#define PUF_DASH_W 80
#define PUF_DASH_BASE_ROWS 12
#define PUF_DASH_MAX_USER_ROWS 15

// Colors no-op when not a TTY.
#define PUF_A  (puf_dashboard_tty ? "\033[96m" : "")
#define PUF_W  (puf_dashboard_tty ? "\033[97m" : "")
#define PUF_G  (puf_dashboard_tty ? "\033[90m" : "")
#define PUF_R  (puf_dashboard_tty ? "\033[0m" : "")

static void dash_eol(void) {
    if (puf_dashboard_tty) {
        printf("\033[K");
    }
    putchar('\n');
}

static void dash_end(void) {
    if (puf_dashboard_tty) {
        printf("\033[J\033[?2026l");
    }
    fflush(stdout);
}

// Right-align into w cols (truncate if long). Unit letters gray when value looks numeric.
static void dash_cell(const char* s, int w) {
    int n = (int)strlen(s);
    if (n > w) {
        n = w;
    }
    int numeric = 0;
    for (int i = 0; i < n; i++) {
        numeric |= (s[i] >= '0' && s[i] <= '9');
    }
    printf("%s%*s", PUF_W, w - n, "");
    for (int i = 0; i < n; i++) {
        int unit = numeric && strchr("%KMBTGdhms", s[i]);
        printf("%s%c", unit ? PUF_G : PUF_W, s[i]);
    }
    printf("%s", PUF_R);
}

static void dash_rule(const char* left, const char* right) {
    printf("%s%s", PUF_W, left);
    for (int i = 0; i < PUF_DASH_W - 2; i++) printf("─");
    printf("%s%s", right, PUF_R);
    dash_eol();
}

// Stats | Perf | Losses  — fixed field widths sum to 80 with borders/spacing.
static void dash_row(const char* a, const char* av,
        const char* b, const char* bt, const char* bp,
        const char* c, const char* cv) {
    printf("%s│%s %s%-9.9s%s ", PUF_W, PUF_R, PUF_A, a, PUF_R);
    dash_cell(av, 13);
    printf("    %s%-12.12s%s ", PUF_A, b, PUF_R);
    dash_cell(bt, 6);
    putchar(' ');
    dash_cell(bp, 4);
    printf("    %s%-10.10s%s ", PUF_A, c, PUF_R);
    dash_cell(cv, 7);
    printf("    %s│%s", PUF_W, PUF_R);
    dash_eol();
}

static void dash_abbrev(char* out, size_t n, double val) {
    const char* suf[] = {"", "K", "M", "B", "T"};
    int i = 0;
    while (val >= 1000.0 && i < 4) {
        val /= 1000.0;
        i++;
    }
    snprintf(out, n, "%.1f%s", val, suf[i]);
}

// Sub-second → ms; else d/h/m/s (zeros allowed, e.g. "0d 0h 5m 3s").
static void dash_duration(char* out, size_t n, double sec) {
    if (sec < 0) {
        sec = 0;
    }
    if (sec < 1.0) {
        snprintf(out, n, "%.0fms", sec * 1000.0);
        return;
    }
    long s = (long)sec;
    snprintf(out, n, "%ldd %ldh %ldm %lds",
        s / 86400, (s / 3600) % 24, (s / 60) % 60, s % 60);
}

static void dash_perf(char* t, size_t tn, char* p, size_t pn, double part, double total) {
    dash_duration(t, tn, part);
    snprintf(p, pn, "%d%%", total > 0 ? (int)(100.0 * part / total) : 0);
}

// Train-only metrics are absent on pure eval logs; keep last train values when
// present (merge into last_log does not clear missing keys).
static double dash_num(Dict* log, const char* key, double fallback) {
    DictItem* it = dict_find(log, key);
    return it ? it->value : fallback;
}

static void dash_loss(Dict* log, const char* key, char* out, size_t n) {
    DictItem* it = dict_find(log, key);
    if (it) {
        snprintf(out, n, "%.3f", it->value);
    } else {
        out[0] = 0;
    }
}

static void dash_env_pair(Dict* log, const char* left, const char* right) {
    char left_key[128], right_key[128];
    char left_value[32], right_value[32];
    snprintf(left_key, sizeof(left_key), "env/%s", left);
    snprintf(right_key, sizeof(right_key), "env/%s", right);
    snprintf(left_value, sizeof(left_value), "%.3f",
        dash_num(log, left_key, 0.0));
    snprintf(right_value, sizeof(right_value), "%.3f",
        dash_num(log, right_key, 0.0));
    printf("%s│%s %s%-25.25s%s %s%9.9s%s   %s%-25.25s%s %s%9.9s%s    %s│%s",
        PUF_W, PUF_R, PUF_A, left, PUF_R, PUF_W, left_value, PUF_R,
        PUF_A, right, PUF_R, PUF_W, right_value, PUF_R, PUF_W, PUF_R);
    dash_eol();
}

void puf_dashboard_print(Ini* ini, PuffeRL* p, Dict* log, int epoch) {
    puf_dashboard_tty = isatty(STDOUT_FILENO);
    int term_rows = 1000, term_cols = PUF_DASH_W;
    if (puf_dashboard_tty) {
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
            if (ws.ws_row > 0) term_rows = ws.ws_row;
            if (ws.ws_col > 0) term_cols = ws.ws_col;
        }
    }

    const char* env_name = puf_ini_get_str(ini, "base", "env_name");
    double steps = dash_num(log, "agent_steps",
        (double)p->global_step * p->hypers.world_size);
    double sps = dash_num(log, "SPS", 0);
    long configured = (long)puf_ini_get(ini, "train", "total_timesteps");
    long local_batch = (long)p->hypers.total_agents * p->hypers.horizon;
    long local_steps = configured / p->hypers.world_size;
    double target = (double)((local_steps / local_batch) * local_batch * p->hypers.world_size);
    double remain_sec = sps > 0 && target > steps ? (target - steps) / sps : 0;
    double rollout = dash_num(log, "perf/rollout", 0);
    double train_time = dash_num(log, "perf/train", 0);
    double perf_total = rollout + train_time;

    char params[32], steps_s[32], sps_s[32], uptime[64], remaining[64], epoch_s[32];
    dash_abbrev(params, sizeof(params), (double)numel(p->master_weights.shape));
    dash_abbrev(steps_s, sizeof(steps_s), steps);
    dash_abbrev(sps_s, sizeof(sps_s), sps);
    // Uptime keeps sub-hour ms precision; longer runs use d/h/m/s.
    double up = dash_num(log, "uptime", wall_clock() - p->start_time);
    if (up < 0) {
        up = 0;
    }
    long ms = (long)(up * 1000.0 + 0.5);
    if (ms < 1000) {
        snprintf(uptime, sizeof(uptime), "%ldms", ms);
    } else if (ms < 60000) {
        snprintf(uptime, sizeof(uptime), "%lds %03ldms", ms / 1000, ms % 1000);
    } else if (ms < 3600000) {
        snprintf(uptime, sizeof(uptime), "%ldm %02lds %03ldms",
            ms / 60000, (ms / 1000) % 60, ms % 1000);
    } else {
        dash_duration(uptime, sizeof(uptime), up);
    }
    dash_duration(remaining, sizeof(remaining), remain_sec);
    snprintf(epoch_s, sizeof(epoch_s), "%d", epoch);

    if (puf_dashboard_tty) {
        printf("\033[?2026h");
        if (term_rows != puf_dashboard_last_rows || term_cols != puf_dashboard_last_cols) {
            printf("\033[H\033[J");
        } else {
            printf("\033[H");
        }
        puf_dashboard_last_rows = term_rows;
        puf_dashboard_last_cols = term_cols;
    }
    // Tiny terminal: one-line compact summary.
    if (puf_dashboard_tty && (term_cols < PUF_DASH_W || term_rows <= PUF_DASH_BASE_ROWS)) {
        char compact[512];
        snprintf(compact, sizeof(compact),
            "PufferLib 5.0  env=%s  steps=%s  SPS=%s  score=%.3f  epoch=%s  to_go=%s",
            env_name, steps_s, sps_s, dash_num(log, "env/score", 0), epoch_s, remaining);
        printf("%.*s", term_cols > 1 ? term_cols - 1 : term_cols, compact);
        if (term_rows > 1) {
            dash_eol();
        } else if (puf_dashboard_tty) {
            printf("\033[K");
        }
        dash_end();
        return;
    }

    char gpu[16], vram[32], ram[16];
    snprintf(gpu, sizeof(gpu), "%3.0f%%", dict_get(log, "util/gpu_percent"));
    snprintf(vram, sizeof(vram), "%.1f/%.0fG",
        dict_get(log, "util/vram_used_gb"),
        dict_get(log, "util/vram_total_gb"));
    snprintf(ram, sizeof(ram), "%.1fG", dict_get(log, "util/cpu_mem_gb"));

    int fish_span = 18;
    int fish_pos = (fish_span - 3) - (puf_dashboard_frame++ % (fish_span - 2));

    dash_rule("╭", "╮");
    printf("%s│%s %sPufferLib %s5.0%s%*s%s🐡%s%*s%sGPU%s:%s ",
        PUF_W, PUF_R, PUF_A, PUF_W, PUF_R, fish_pos, "", PUF_A, PUF_R,
        fish_span - 2 - fish_pos, "", PUF_A, PUF_G, PUF_R);
    dash_cell(gpu, 4);
    printf("   %sVRAM%s:%s", PUF_A, PUF_G, PUF_R);
    dash_cell(vram, 10);
    printf("    %sRAM%s:%s", PUF_A, PUF_G, PUF_R);
    dash_cell(ram, 6);
    printf("     %s│%s", PUF_W, PUF_R);
    dash_eol();
    printf("%s│%*s│%s", PUF_W, PUF_DASH_W - 2, "", PUF_R);
    dash_eol();

    char et[64], ep[16], emt[64], emp[16], evt[64], evp[16], ct[64], cp[16];
    char tt[64], tp[16], tmt[64], tmp[16], mt[64], mp[16];
    char lp[32], lv[32], le[32], lt[32], lok[32], lkl[32], lcf[32];
    dash_perf(et, sizeof(et), ep, sizeof(ep), rollout, perf_total);
    dash_perf(emt, sizeof(emt), emp, sizeof(emp), dash_num(log, "perf/eval_model", 0), perf_total);
    dash_perf(evt, sizeof(evt), evp, sizeof(evp), dash_num(log, "perf/eval_env", 0), perf_total);
    dash_perf(ct, sizeof(ct), cp, sizeof(cp), dash_num(log, "perf/eval_copy", 0), perf_total);
    dash_perf(tt, sizeof(tt), tp, sizeof(tp), train_time, perf_total);
    dash_perf(tmt, sizeof(tmt), tmp, sizeof(tmp), dash_num(log, "perf/train_model", 0), perf_total);
    dash_perf(mt, sizeof(mt), mp, sizeof(mp), dash_num(log, "perf/train_misc", 0), perf_total);
    dash_loss(log, "loss/policy", lp, sizeof(lp));
    dash_loss(log, "loss/value", lv, sizeof(lv));
    dash_loss(log, "loss/entropy", le, sizeof(le));
    dash_loss(log, "loss/total", lt, sizeof(lt));
    dash_loss(log, "loss/old_kl", lok, sizeof(lok));
    dash_loss(log, "loss/kl", lkl, sizeof(lkl));
    dash_loss(log, "loss/clipfrac", lcf, sizeof(lcf));

    dash_row("Env", env_name, "Evaluate", et, ep, "Losses", lt);
    dash_row("Params", params, "  Model", emt, emp, "policy", lp);
    dash_row("Steps", steps_s, "  Env", evt, evp, "value", lv);
    dash_row("SPS", sps_s, "  Copy", ct, cp, "entropy", le);
    dash_row("Epoch", epoch_s, "Train", tt, tp, "old_kl", lok);
    dash_row("Uptime", uptime, "  Model", tmt, tmp, "kl", lkl);
    dash_row("To go", remaining, "  Misc", mt, mp, "clipfrac", lcf);
    printf("%s│%*s│%s", PUF_W, PUF_DASH_W - 2, "", PUF_R);
    dash_eol();

    if (!strcmp(env_name, "kaggriculture")) {
        static const char* const rows[][2] = {
            {"score", "opponent_score"},
            {"win_rate", "draw_rate"},
            {"land_purchases", "productive_extra_tiles"},
            {"water_coverage", "neglect_deaths"},
            {"planting_day_deaths", "unused_seed_value"},
            {"plants_alive", "weeds"},
            {"animals_alive", "animal_place_actions"},
            {"animal_feed_actions", "animal_care_actions"},
            {"animal_harvest_actions", "fertilizer_collect_actions"},
            {"orders_per_turn", "buy_orders"},
            {"seed_buy_orders", "product_buy_orders"},
            {"animal_buy_orders", "sell_orders"},
            {"hire_orders", "land_orders"},
            {"checkpoint_fraction", "starter_fraction"},
            {"rules_fraction", "specialist_fraction"},
        };
        int row_count = (int)(sizeof(rows) / sizeof(rows[0]));
        for (int row = 0; row < row_count; row++) {
            dash_env_pair(log, rows[row][0], rows[row][1]);
        }
        dash_rule("╰", "╯");
        dash_end();
        return;
    }

    int user_rows = PUF_DASH_MAX_USER_ROWS;
    if (puf_dashboard_tty) {
        user_rows = term_rows - PUF_DASH_BASE_ROWS - 1;
        if (user_rows < 0) user_rows = 0;
        if (user_rows > PUF_DASH_MAX_USER_ROWS) user_rows = PUF_DASH_MAX_USER_ROWS;
    }

    char pending_key[128];
    double pending_val = 0;
    int pending = 0, n = 0, max_items = 2 * user_rows;
    for (int i = 0; i < log->size && n < max_items; i++) {
        const char* key = log->items[i].key;
        if (strncmp(key, "env/", 4) != 0 || strcmp(key, "env/n") == 0) continue;
        const char* sk = key + 4;
        if (!pending) {
            snprintf(pending_key, sizeof(pending_key), "%s", sk);
            pending_val = log->items[i].value;
            pending = 1;
        } else {
            char ls[32], rs[32];
            snprintf(ls, sizeof(ls), "%.3f", pending_val);
            snprintf(rs, sizeof(rs), "%.3f", log->items[i].value);
            printf("%s│%s %s%-25.25s%s %s%9.9s%s   %s%-25.25s%s %s%9.9s%s    %s│%s",
                PUF_W, PUF_R, PUF_A, pending_key, PUF_R, PUF_W, ls, PUF_R,
                PUF_A, sk, PUF_R, PUF_W, rs, PUF_R, PUF_W, PUF_R);
            dash_eol();
            pending = 0;
        }
        n++;
    }
    if (pending) {
        char ls[32];
        snprintf(ls, sizeof(ls), "%.3f", pending_val);
        printf("%s│%s %s%-25.25s%s %s%9.9s%s   %-25.25s %9.9s    %s│%s",
            PUF_W, PUF_R, PUF_A, pending_key, PUF_R, PUF_W, ls, PUF_R, "", "", PUF_W, PUF_R);
        dash_eol();
    }
    dash_rule("╰", "╯");
    dash_end();
}

void log_util(PuffeRL* p, Dict* out) {
    nvmlUtilization_t util;
    nvmlDeviceGetUtilizationRates(p->nvml_device, &util);
    dict_set(out, "util/gpu_percent", (double)util.gpu);

    size_t cuda_free;
    size_t cuda_total;
    cudaMemGetInfo(&cuda_free, &cuda_total);
    dict_set(out, "util/vram_used_gb",
        (double)(cuda_total - cuda_free) / (1024.0 * 1024.0 * 1024.0));
    dict_set(out, "util/vram_total_gb",
        (double)cuda_total / (1024.0 * 1024.0 * 1024.0));

    long rss_kb = 0;
    FILE* status = fopen("/proc/self/status", "r");
    if (status) {
        char line[256];
        while (fgets(line, sizeof(line), status)) {
            if (sscanf(line, "VmRSS: %ld", &rss_kb) == 1) {
                break;
            }
        }
        fclose(status);
    }
    dict_set(out, "util/cpu_mem_gb", (double)rss_kb / (1024.0 * 1024.0));
}

// Eval only refreshes util + env. Do not write SPS/uptime/perf/losses: merging
// into last_log would overwrite the last train snapshot and zero the dashboard.
void trainer_eval_log(PuffeRL* p, Dict* out) {
    double now = wall_clock();
    p->last_log_time = now;
    p->last_log_step = p->global_step;
    log_util(p, out);
    vec_log(p->vec, out, 0);
}

typedef struct {
    Dict* items;
    int size;
    int capacity;
} PufLogHistory;

void puf_log_history_add(PufLogHistory* history, Dict* log) {
    if (history->size == history->capacity) {
        history->capacity = history->capacity ? 2 * history->capacity : 64;
        history->items = (Dict*)realloc(history->items, (size_t)history->capacity * sizeof(Dict));
        if (!history->items) {
            perror("realloc");
            exit(1);
        }
    }

    dict_copy(&history->items[history->size], log);
    history->size++;
}

double rollout_start(PuffeRL* p, int slot) {
    p->rollout_write_slot = slot;
    if (p->hypers.async) {
        int64_t n = numel(p->param_puf.shape);
        cudaMemcpyAsync(p->actor_param_puf.data, p->param_puf.data,
            n * sizeof(precision_t), cudaMemcpyDeviceToDevice, p->default_stream);
        cudaStreamSynchronize(p->default_stream);
    }
    if (p->hypers.reset_every_horizon) {
        for (int i = 0; i < p->hypers.num_buffers; i++) {
            cudaMemsetAsync(p->buffer_states[i].data, 0,
                numel(p->buffer_states[i].shape) * sizeof(precision_t), p->default_stream);
        }
        for (int b = 0; b < p->num_frozen_banks; b++) {
            for (int i = 0; i < p->hypers.num_buffers; i++) {
                PrecisionTensor* st = &p->frozen_banks[b].buffer_states[i];
                cudaMemsetAsync(st->data, 0, numel(st->shape) * sizeof(precision_t),
                    p->default_stream);
            }
        }
        cudaStreamSynchronize(p->default_stream);
    }

    double t0 = wall_clock();
#ifdef PUFFER_GPU_ENV
    VecEnv* vec = p->vec;
    int count = p->hypers.total_agents;
    for (int t = 0; t < p->hypers.horizon; t++) {
        cudaEventRecord(p->profile.rollout_gpu_start[t], p->streams[0]);
        pufferl_forward(p, 0, t, p->streams[0]);
        cudaEventRecord(p->profile.rollout_gpu_end[t], p->streams[0]);
        puf_envs_step(vec->envs, vec->gpu_actions, vec->gpu_observations,
            vec->gpu_rewards, vec->gpu_terminals, 0, count, p->streams[0]);
        cudaEventRecord(p->profile.rollout_env_end[t], p->streams[0]);
    }
#else
    for (int buf = 0; buf < p->vec->buffers; buf++) {
        __atomic_store_n(&p->vec->worker_state[buf], BUF_RUNNING, __ATOMIC_SEQ_CST);
    }
#endif
    return t0;
}

void rollout_finish(PuffeRL* p, double t0) {
#ifdef PUFFER_GPU_ENV
    cudaStreamSynchronize(p->streams[0]);
    float gpu_ms = 0.0f;
    float env_ms = 0.0f;
    for (int t = 0; t < p->hypers.horizon; t++) {
        float ms = 0.0f;
        cudaEventElapsedTime(&ms,
            p->profile.rollout_gpu_start[t], p->profile.rollout_gpu_end[t]);
        gpu_ms += ms;
        cudaEventElapsedTime(&ms,
            p->profile.rollout_gpu_end[t], p->profile.rollout_env_end[t]);
        env_ms += ms;
    }
    p->profile.accum[PROF_EVAL_MODEL] += gpu_ms;
    p->profile.accum[PROF_EVAL_ENV] += env_ms;
    p->profile.accum[PROF_ROLLOUT] += gpu_ms + env_ms;
#else
    for (int buf = 0; buf < p->vec->buffers; buf++) {
        while (__atomic_load_n(&p->vec->worker_state[buf], __ATOMIC_SEQ_CST)
                != BUF_WAITING) {
        }
    }
    float sec = (float)(wall_clock() - t0);
    p->profile.accum[PROF_ROLLOUT] += sec * 1000.0f;

    float eval_prof[NUM_VEC_PROF] = {0};
    for (int buf = 0; buf < p->vec->buffers; buf++) {
        float* src = &p->vec->accum[buf * NUM_VEC_PROF];
        for (int i = 0; i < NUM_VEC_PROF; i++) {
            eval_prof[i] += src[i];
        }
        memset(src, 0, NUM_VEC_PROF * sizeof(float));
    }
    p->profile.accum[PROF_EVAL_MODEL] += eval_prof[VEC_MODEL] / p->vec->buffers;
    p->profile.accum[PROF_EVAL_ENV] += eval_prof[VEC_ENV_STEP] / p->vec->buffers;
    p->profile.accum[PROF_EVAL_COPY] += eval_prof[VEC_COPY] / p->vec->buffers;
#endif
}

void rollouts(PuffeRL* p) {
    double t0 = rollout_start(p, 0);
    rollout_finish(p, t0);
    p->global_step += p->hypers.horizon * p->hypers.total_agents;
}


typedef struct {
    float score;
    float draw;
    float money;
    float opponent_money;
    int games;
} EvalResult;

#define TRAIN_RESULT_MAX_POINTS 64
typedef struct {
    float score;
    float cost;
    float steps;
    int points;
    float scores[TRAIN_RESULT_MAX_POINTS];
    float costs[TRAIN_RESULT_MAX_POINTS];
    float step_points[TRAIN_RESULT_MAX_POINTS];
} TrainResult;

#define EVAL_RENDER 0
#define EVAL_SCORE 1
#define EVAL_MATCH 2

EvalResult run_eval(Ini* ini, TrainContext* ctx, int mode, int verbose);

#define LEAGUE_EVAL_MAX_POLICIES 64

typedef struct {
    char name[256];
    char path[4096];
} LeagueEvalPolicy;

typedef struct {
    LeagueEvalPolicy items[LEAGUE_EVAL_MAX_POLICIES];
    int size;
} LeagueEvalList;

#define SELFPLAY_MAX_BANKS 8
#define SELFPLAY_PATH_MAX 4096
#define SELFPLAY_MEMORY_BOOTSTRAP "<fresh-policy>"

enum {
    PFSP_HARD,
    PFSP_VARIANCE,
};

typedef struct {
    char pending_path[SELFPLAY_PATH_MAX];
    long opp_started_step;
    int num_envs;
    int opponent;
} SelfplayBank;

typedef struct {
    char path[SELFPLAY_PATH_MAX];
    double wins;
    double draws;
    double losses;
    double samples;
    float recent_score;
    float recent_draw;
} SelfplayPayoff;

typedef struct {
    int num_banks;
    int max_size;
    long opp_timeout_steps;
    unsigned int rng;
    char (*pool)[SELFPLAY_PATH_MAX];
    int pool_size;
    char (*external)[SELFPLAY_PATH_MAX];
    float* external_weights;
    int external_size;
    float external_prob;
    float pfsp_alpha;
    float pfsp_uniform_mix;
    int pfsp_mode;
    float payoff_ema;
    SelfplayPayoff* payoffs;
    int payoff_size;
    char payoff_path[SELFPLAY_PATH_MAX];
    SelfplayBank banks[SELFPLAY_MAX_BANKS];
} Selfplay;

int selfplay_payoff(Selfplay* sp, const char* path) {
    for (int i = 0; i < sp->payoff_size; i++) {
        if (strcmp(sp->payoffs[i].path, path) == 0) return i;
    }
    sp->payoffs = (SelfplayPayoff*)realloc(sp->payoffs,
        (size_t)(sp->payoff_size + 1) * sizeof(*sp->payoffs));
    SelfplayPayoff* payoff = &sp->payoffs[sp->payoff_size];
    *payoff = (SelfplayPayoff){0};
    snprintf(payoff->path, sizeof(payoff->path), "%s", path);
    payoff->recent_score = 0.5f;
    return sp->payoff_size++;
}

void selfplay_add_checkpoint(Selfplay* sp, const char* path) {
    if (strcmp(path, SELFPLAY_MEMORY_BOOTSTRAP) != 0) {
        while (access(path, R_OK) != 0) usleep(50000);
    }
    selfplay_payoff(sp, path);
    for (int i = 0; i < sp->pool_size; i++) {
        if (strcmp(sp->pool[i], path) == 0) return;
    }
    if (sp->pool_size == sp->max_size) {
        memmove(sp->pool, sp->pool + 1,
            (size_t)(sp->max_size - 1) * sizeof(*sp->pool));
        sp->pool_size--;
    }
    snprintf(sp->pool[sp->pool_size++], sizeof(sp->pool[0]), "%s", path);
}

int selfplay_add_external(Selfplay* sp, const char* path) {
    selfplay_payoff(sp, path);
    for (int i = 0; i < sp->external_size; i++) {
        if (strcmp(sp->external[i], path) == 0) return i;
    }
    int index = sp->external_size;
    sp->external = (char (*)[SELFPLAY_PATH_MAX])realloc(sp->external,
        (size_t)(sp->external_size + 1) * sizeof(*sp->external));
    snprintf(sp->external[sp->external_size++], sizeof(sp->external[0]), "%s", path);
    if (sp->external_weights) {
        sp->external_weights = (float*)realloc(sp->external_weights,
            (size_t)sp->external_size * sizeof(float));
        sp->external_weights[index] = 1.0f;
    }
    return index;
}

void selfplay_add_external_list(Selfplay* sp, const char* paths) {
    if (!paths[0] || strcmp(paths, "None") == 0) return;
    char* list = strdup(paths);
    for (char* path = list; path;) {
        char* next = strchr(path, ',');
        if (next) *next++ = 0;
        path = puf_ini_trim(path);
        if (*path) selfplay_add_external(sp, path);
        path = next;
    }
    free(list);
}

void selfplay_add_external_league(Selfplay* sp, const char* path) {
    if (!path[0] || strcmp(path, "None") == 0) return;
    Ini league = {0};
    puf_ini_load_file(&league, path);
    int added = 0;
    for (int i = 0; i < league.num_sections; i++) {
        Dict* policy = &league.sections[i];
        if (strncmp(policy->name, "policy.", 7) != 0) continue;
        DictItem* enabled = dict_find(policy, "enabled");
        if (enabled && enabled->value == 0.0) continue;
        DictItem* checkpoint = dict_find(policy, "path");
        DictItem* weight_item = dict_find(policy, "train_weight");
        if (!checkpoint || !checkpoint->str || !checkpoint->str[0]) {
            fprintf(stderr, "%s: [%s] requires path\n", path, policy->name);
            exit(1);
        }
        float weight = weight_item ? (float)weight_item->value : 1.0f;
        if (weight < 0.0f) {
            fprintf(stderr, "%s: [%s] train_weight must be nonnegative\n",
                path, policy->name);
            exit(1);
        }
        int index = selfplay_add_external(sp, checkpoint->str);
        if (!sp->external_weights) {
            sp->external_weights = (float*)malloc(
                (size_t)sp->external_size * sizeof(float));
            for (int j = 0; j < sp->external_size; j++) {
                sp->external_weights[j] = 1.0f;
            }
        }
        sp->external_weights[index] = weight;
        added++;
    }
    puf_ini_free(&league);
    if (!added) {
        fprintf(stderr, "selfplay league %s has no enabled [policy.NAME] sections\n",
            path);
        exit(1);
    }
    float total = 0.0f;
    for (int i = 0; i < sp->external_size; i++) {
        total += sp->external_weights[i];
    }
    if (total <= 0.0f) {
        fprintf(stderr, "selfplay league %s has zero total train_weight\n", path);
        exit(1);
    }
}

static void selfplay_validate_external(Selfplay* sp, size_t expected_bytes) {
    for (int i = 0; i < sp->external_size; i++) {
        struct stat info;
        if (stat(sp->external[i], &info) != 0) {
            fprintf(stderr, "selfplay opponent %s is not readable: %s\n",
                sp->external[i], strerror(errno));
            exit(1);
        }
        if ((size_t)info.st_size != expected_bytes) {
            fprintf(stderr, "selfplay opponent %s is incompatible: got %lld "
                "bytes, expected %zu for the frozen-bank architecture; "
                "start a fresh league or remove it from selfplay.opponent_pool\n",
                sp->external[i], (long long)info.st_size, expected_bytes);
            exit(1);
        }
    }
}

void selfplay_set_external_weights(Selfplay* sp, const char* values) {
    if (!values[0] || strcmp(values, "None") == 0) return;
    free(sp->external_weights);
    sp->external_weights = (float*)calloc((size_t)sp->external_size, sizeof(float));
    char* list = strdup(values);
    char* value = list;
    float total = 0.0f;
    int i = 0;
    while (value && i < sp->external_size) {
        char* next = strchr(value, ',');
        if (next) *next++ = 0;
        sp->external_weights[i] = strtof(puf_ini_trim(value), NULL);
        if (sp->external_weights[i] < 0.0f) break;
        total += sp->external_weights[i++];
        value = next;
    }
    free(list);
    if (i != sp->external_size || value || total <= 0.0f) {
        fprintf(stderr, "selfplay.opponent_pool_weights must contain one "
            "nonnegative weight per external opponent\n");
        exit(1);
    }
}

static inline float selfplay_priority(const Selfplay* sp, float recent_score) {
    if (sp->pfsp_alpha == 0.0f) return 1.0f;
    float score = fminf(1.0f, fmaxf(0.0f, recent_score));
    float priority = sp->pfsp_mode == PFSP_VARIANCE
        ? 4.0f * score * (1.0f - score) : 1.0f - score;
    return powf(priority, sp->pfsp_alpha);
}

const char* selfplay_sample_set(Selfplay* sp,
        char (*paths)[SELFPLAY_PATH_MAX], float* weights, int size) {
    if (sp->pfsp_uniform_mix > 0.0f &&
            rand_r(&sp->rng) / ((float)RAND_MAX + 1.0f)
                < sp->pfsp_uniform_mix) {
        return paths[rand_r(&sp->rng) % (unsigned int)size];
    }
    if (sp->pfsp_alpha == 0.0f && !weights) {
        return paths[rand_r(&sp->rng) % (unsigned int)size];
    }

    float total = 0.0f;
    for (int i = 0; i < size; i++) {
        SelfplayPayoff* payoff = &sp->payoffs[selfplay_payoff(sp, paths[i])];
        float base = weights ? weights[i] : 1.0f;
        total += base * selfplay_priority(sp, payoff->recent_score);
    }
    int use_priority = total > 0.0f;
    if (total == 0.0f) {
        for (int i = 0; i < size; i++) {
            total += weights ? weights[i] : 1.0f;
        }
    }

    float sample = rand_r(&sp->rng) / ((float)RAND_MAX + 1.0f) * total;
    for (int i = 0; i < size; i++) {
        SelfplayPayoff* payoff = &sp->payoffs[selfplay_payoff(sp, paths[i])];
        float base = weights ? weights[i] : 1.0f;
        float priority = use_priority
            ? selfplay_priority(sp, payoff->recent_score) : 1.0f;
        sample -= base * priority;
        if (sample <= 0.0f) return paths[i];
    }
    return paths[size - 1];
}

const char* selfplay_sample(Selfplay* sp) {
    if (sp->external_size &&
            rand_r(&sp->rng) / ((float)RAND_MAX + 1.0f) < sp->external_prob) {
        return selfplay_sample_set(sp, sp->external,
            sp->external_weights, sp->external_size);
    }
    return selfplay_sample_set(sp, sp->pool, NULL, sp->pool_size);
}

void selfplay_record(Selfplay* sp, int opponent,
        float score, float draw, float samples) {
    if (samples == 0.0f) return;
    SelfplayPayoff* payoff = &sp->payoffs[opponent];
    if (payoff->samples) {
        payoff->recent_score += sp->payoff_ema * (score - payoff->recent_score);
        payoff->recent_draw += sp->payoff_ema * (draw - payoff->recent_draw);
    } else {
        payoff->recent_score = score;
        payoff->recent_draw = draw;
    }
    payoff->wins += (score - 0.5f * draw) * samples;
    payoff->draws += draw * samples;
    payoff->losses += (1.0f - score - 0.5f * draw) * samples;
    payoff->samples += samples;
}

void selfplay_write_payoffs(Selfplay* sp) {
    char tmp[SELFPLAY_PATH_MAX + 4];
    snprintf(tmp, sizeof(tmp), "%s.tmp", sp->payoff_path);
    FILE* file = fopen(tmp, "w");
    if (!file) {
        fprintf(stderr, "failed to write %s\n", tmp);
        exit(1);
    }
    fprintf(file, "id\tsamples\twins\tdraws\tlosses\tscore\trecent_score\trecent_draw\tpath\n");
    for (int i = 0; i < sp->payoff_size; i++) {
        SelfplayPayoff* payoff = &sp->payoffs[i];
        double score = payoff->samples
            ? (payoff->wins + 0.5 * payoff->draws) / payoff->samples
            : 0.5;
        fprintf(file, "%d\t%.0f\t%.3f\t%.3f\t%.3f\t%.6f\t%.6f\t%.6f\t%s\n",
            i, payoff->samples, payoff->wins, payoff->draws, payoff->losses,
            score, payoff->recent_score, payoff->recent_draw, payoff->path);
    }
    fclose(file);
    if (rename(tmp, sp->payoff_path) != 0) {
        fprintf(stderr, "failed to publish %s\n", sp->payoff_path);
        exit(1);
    }
}

void selfplay_load_bank(Selfplay* sp, PuffeRL* pufferl,
        int bank, const char* path, long step) {
    if (strcmp(path, SELFPLAY_MEMORY_BOOTSTRAP) == 0) {
        pufferl_copy_frozen_bank_from_learner(pufferl, bank);
    } else {
        pufferl_load_frozen_bank(pufferl, bank, path);
    }
    SelfplayBank* state = &sp->banks[bank];
    state->opponent = selfplay_payoff(sp, path);
    state->opp_started_step = step;
    printf("Selfplay bank %d opponent %d: %s\n", bank, state->opponent, path);
}

typedef struct {
    char section[64];
    char key[64];
} SweepParam;

extern char** environ;

typedef struct {
    int run;
    int random;
    int gp_obs;
    int pareto;
    int fd;
    pid_t pid;
    float* sample;
    TrainResult result;
} SweepJob;

static int sweep_param_included(const char* path, const char* only) {
    if (!only || !*only) return 1;
    while (*only) {
        while (*only == ',' || isspace((unsigned char)*only)) only++;
        const char* end = only;
        while (*end && *end != ',') end++;
        while (end > only && isspace((unsigned char)end[-1])) end--;
        if (end > only) {
            size_t len = (size_t)(end - only);
            for (const char* match = path; *match; match++) {
                if (strlen(match) >= len && strncmp(match, only, len) == 0) return 1;
            }
        }
        only = *end ? end + 1 : end;
    }
    return 0;
}

void run_sweep(Ini* ini, const char* exe_path) {
    // Build SweepSpace + param map from [sweep.<section>.<key>] sections.
    const char* goal = puf_ini_get_str(ini, "sweep", "goal");
    assert((strcmp(goal, "maximize") == 0 || strcmp(goal, "minimize") == 0)
        && "sweep.goal must be maximize or minimize");
    int direction = strcmp(goal, "minimize") == 0 ? -1 : 1;

    SweepParam* params = (SweepParam*)calloc((size_t)ini->num_sections, sizeof(SweepParam));
    SweepSpace* space = sweep_space_create(ini->num_sections, -1, direction);
    int n_params = 0;
    DictItem* only_item = dict_find(puf_ini_section(ini, "sweep", 0), "sweep_only");
    const char* sweep_only = only_item ? only_item->str : NULL;
    for (int i = 0; i < ini->num_sections; i++) {
        Dict* dict = &ini->sections[i];
        if (strncmp(dict->name, "sweep.", 6) != 0) {
            continue;
        }
        const char* path = dict->name + 6;
        if (!sweep_param_included(path, sweep_only)) {
            continue;
        }
        const char* dot = strrchr(path, '.');
        assert(dot && dot != path && dot[1]
            && "expected section [sweep.<section>.<key>]");
        snprintf(params[n_params].section, sizeof(params[n_params].section), "%.*s",
            (int)(dot - path), path);
        snprintf(params[n_params].key, sizeof(params[n_params].key), "%s", dot + 1);

        const char* dist = dict_get_str(dict, "distribution");
        SpaceType type = SPACE_LINEAR;
        int is_integer = 0;
        if (strcmp(dist, "uniform") == 0) {
            type = SPACE_LINEAR;
        } else if (strcmp(dist, "int_uniform") == 0) {
            type = SPACE_LINEAR;
            is_integer = 1;
        } else if (strcmp(dist, "uniform_pow2") == 0) {
            type = SPACE_POW2;
            is_integer = 1;
        } else if (strcmp(dist, "log_normal") == 0) {
            type = SPACE_LOG;
        } else if (strcmp(dist, "logit_normal") == 0) {
            type = SPACE_LOGIT;
        } else {
            assert(0 && "invalid sweep distribution (use uniform/int_uniform/"
                "uniform_pow2/log_normal/logit_normal)");
        }

        float min_v = (float)dict_get(dict, "min");
        float max_v = (float)dict_get(dict, "max");
        const char* scale_s = dict_get_str(dict, "scale");
        float scale;
        if (strcmp(scale_s, "auto") == 0) {
            scale = 0.5f;
        } else if (strcmp(scale_s, "time") == 0) {
            assert(min_v > 0 && max_v > 0
                && "scale=time requires positive min/max");
            scale = 1.0f / (log2f(max_v) - log2f(min_v));
        } else {
            scale = (float)dict_get(dict, "scale");
        }

        space_init(&space->spaces[n_params], type, min_v, max_v, scale, is_integer);
        if (strcmp(dict->name, "sweep.train.total_timesteps") == 0) {
            space->cost_idx = n_params;
        }
        n_params++;
    }
    space->num = n_params;

    int max_runs = (int)puf_ini_get(ini, "sweep", "max_runs");
    int downsample = (int)puf_ini_get(ini, "sweep", "downsample");
    int prune_pareto = (int)puf_ini_get(ini, "sweep", "prune_pareto");
    const char* metric_dist = puf_ini_get_str(ini, "sweep", "metric_distribution");
    assert((strcmp(metric_dist, "linear") == 0 || strcmp(metric_dist, "logit") == 0)
        && "sweep.metric_distribution must be linear or logit");
    int use_logit = strcmp(metric_dist, "logit") == 0;
    float max_cost = (float)puf_ini_get(ini, "sweep", "max_suggestion_cost");
    float early_stop_quantile = (float)puf_ini_get(ini, "sweep", "early_stop_quantile");
    assert(max_runs >= 1 && "sweep.max_runs must be >= 1");
    assert(downsample >= 1 && downsample <= TRAIN_RESULT_MAX_POINTS
        && "sweep.downsample must be in [1, TRAIN_RESULT_MAX_POINTS]");
    int success_cap = max_runs * downsample * 2;
    if (success_cap < 8192) success_cap = 8192;

    // GPU packing: each trial is a full train (launch_train) that may itself
    // use train.gpus for NCCL DP. Concurrent trials = sweep_gpus / train_gpus
    // on disjoint blocks [0,W), [W,2W), ...  e.g. 8 GPUs, train.gpus=2 → 4 trials.
    int total_gpus = 0;
    cudaError_t err = cudaGetDeviceCount(&total_gpus);
    if (err != cudaSuccess || total_gpus < 1) {
        fprintf(stderr, "sweep error: no CUDA devices available\n");
        exit(1);
    }
    int sweep_gpus = (int)puf_ini_get(ini, "sweep", "gpus");
    int train_gpus = (int)puf_ini_get(ini, "train", "gpus");
    assert(sweep_gpus >= 0 && "sweep.gpus must be >= 0");
    if (sweep_gpus == 0) {
        sweep_gpus = total_gpus;
    }
    assert(sweep_gpus <= total_gpus
        && "sweep.gpus exceeds visible CUDA devices");
    assert(train_gpus >= 1 && "train.gpus must be >= 1");
    assert(sweep_gpus >= train_gpus
        && "sweep.gpus must be >= train.gpus");
    if ((int)puf_ini_get(ini, "sweep", "use_gpu")) {
        cudaSetDevice(sweep_gpus - 1);
    }

    int parallel = sweep_gpus / train_gpus;

    ProteinSweep* protein = protein_sweep_create(space,
        10, 256, 50, 0.001f, 50, 750, 4096,
        downsample == 1, prune_pareto, use_logit,
        1.0f, max_cost, 0.1f, -0.8f, early_stop_quantile,
        success_cap, 1024, 5, 73ULL);

    float* samples = (float*)calloc((size_t)(parallel + 1) * space->num, sizeof(float));
    float* sample = samples;
    SweepJob* jobs = (SweepJob*)calloc((size_t)parallel, sizeof(SweepJob));
    int failed_workers = 0;
    int next_run_id = 0;
    int completed = 0;
    int active = 0;
    // Free-list: slot i owns GPUs [i*train_gpus, (i+1)*train_gpus). Refill as
    // soon as any trial exits (waitpid -1), so finished GPUs are never idle
    // while more runs remain.
    while (completed < max_runs || active > 0) {
        for (int i = 0; i < parallel; i++) {
            if (jobs[i].pid || completed + active >= max_runs) continue;

            ProteinSweepInfo info = {0};
            if (!next_run_id) {
                for (int j = 0; j < space->num; j++) {
                    float val = (float)puf_ini_get(ini, params[j].section, params[j].key);
                    float norm = space_normalize(&space->spaces[j], val);
                    if (!isfinite(norm) || norm < -1.0f || norm > 1.0f) {
                        fprintf(stderr, "default %s.%s=%.9g is outside its "
                            "declared sweep range\n", params[j].section,
                            params[j].key, val);
                        exit(1);
                    }
                    sample[j] = norm;
                }
            } else {
                // Invalid train shapes die in the worker (launch_train asserts);
                // failed workers are observed as bad samples below.
                info = protein_sweep_suggest(protein, sample, NAN);
            }

            SweepJob job = {0};
            job.run = next_run_id++;
            job.random = info.is_random;
            job.gp_obs = info.n_gp_obs;
            job.pareto = info.n_pareto;
            job.sample = samples + (size_t)(i + 1) * space->num;
            memcpy(job.sample, sample, (size_t)space->num * sizeof(float));

            if (job.run) {
                for (int p = 0; p < space->num; p++) {
                    float val = space_unnormalize(&space->spaces[p], sample[p]);
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%.9g", val);
                    char key[256];
                    snprintf(key, sizeof(key), "%s.%s", params[p].section, params[p].key);
                    puf_ini_put(ini, key, buf);
                }
            }
            char run_id[128];
            snprintf(run_id, sizeof(run_id), "sweep_%ld_%04d",
                (long)(1000.0 * wall_clock()), job.run);
            puf_ini_put(ini, "base.run_id", run_id);

            // Spawn clean train process (after parent CUDA init for protein).
            // Child runs main→launch_train; train.gpus>1 DP-forks inside that process.
            int pipefd[2];
            assert(pipe(pipefd) == 0);
            char buf[64];
            snprintf(buf, sizeof(buf), "%d", i * train_gpus);
            puf_ini_put(ini, "base.gpu_offset", buf);
            snprintf(buf, sizeof(buf), "%d", pipefd[1]);
            puf_ini_put(ini, "base.result_fd", buf);

            int nkeys = 0;
            for (int s = 0; s < ini->num_sections; s++) {
                nkeys += ini->sections[s].size;
            }
            int argc = nkeys + 4;
            char** argv = (char**)calloc((size_t)argc, sizeof(char*));
            argv[0] = (char*)exe_path;
            argv[1] = (char*)"train";
            argv[2] = (char*)puf_ini_get_str(ini, "base", "env_name");
            int idx = 3;
            char full_key[PUF_DICT_MAX_KEY * 2];
            for (int s = 0; s < ini->num_sections; s++) {
                Dict* dict = &ini->sections[s];
                for (int k = 0; k < dict->size; k++) {
                    snprintf(full_key, sizeof(full_key), "%s.%s",
                        dict->name, dict->items[k].key);
                    DictItem* item = &dict->items[k];
                    char val[128];
                    const char* src = item->str;
                    if (!src) {
                        snprintf(val, sizeof(val), "%.17g", item->value);
                        src = val;
                    }
                    size_t n = strlen(full_key) + strlen(src) + 2;
                    char* out = (char*)malloc(n);
                    snprintf(out, n, "%s=%s", full_key, src);
                    argv[idx++] = out;
                }
            }
            argv[argc - 1] = NULL;

            posix_spawn_file_actions_t actions;
            posix_spawn_file_actions_init(&actions);
            posix_spawn_file_actions_addclose(&actions, pipefd[0]);
            posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
            pid_t pid = 0;
            int err = posix_spawnp(&pid, exe_path, &actions, NULL, argv, environ);
            posix_spawn_file_actions_destroy(&actions);
            for (int a = 3; a < argc - 1; a++) {
                free(argv[a]);
            }
            free(argv);
            if (err != 0) {
                fprintf(stderr, "posix_spawn train failed: %s\n", strerror(err));
                exit(1);
            }
            close(pipefd[1]);
            job.fd = pipefd[0];
            job.pid = pid;
            jobs[i] = job;
            active++;
        }
        if (!active) break;

        int status = 0;
        pid_t done = waitpid(-1, &status, 0);
        assert(done > 0 && "sweep waitpid failed");
        int i = 0;
        for (; i < parallel && jobs[i].pid != done; i++) {}
        assert(i < parallel && "waitpid reaped unknown child");
        SweepJob* job = &jobs[i];
        int ok = read(job->fd, &job->result, sizeof(job->result)) == sizeof(job->result);
        close(job->fd);
        job->pid = 0;
        active--;
        int good = ok && WIFEXITED(status) && WEXITSTATUS(status) == 0;

        if (!good) {
            fprintf(stderr, "sweep worker run=%d failed; marking sample bad\n", job->run);
            protein_sweep_observe(protein, job->sample, NAN, max_cost, 1);
            if (++failed_workers > 1000) {
                fprintf(stderr, "sweep error: too many failed workers\n");
                exit(1);
            }
        } else {
            // Non-selfplay: points[] is a downsampled learning curve.
            // Selfplay: points==1 — final pool-eval winrate (or last train metric).
            for (int pi = 0; pi < job->result.points; pi++) {
                protein_sweep_observe(protein, job->sample,
                    job->result.scores[pi], job->result.costs[pi], 0);
            }
            printf("sweep run=%d score=%.4f cost=%.2f steps=%.0f random=%d gp_obs=%d pareto=%d\n",
                job->run, job->result.score, job->result.cost, job->result.steps,
                job->random, job->gp_obs, job->pareto);
            completed++;
        }
    }

    free(jobs);
    free(samples);
    free(params);
    protein_sweep_destroy(protein);
    sweep_space_destroy(space);
}

static void league_eval_load_manifest(const char* path, LeagueEvalList* list) {
    FILE* fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "could not open league manifest %s: %s\n",
            path, strerror(errno));
        exit(1);
    }

    char* line = NULL;
    size_t cap = 0;
    int lineno = 0;
    while (getline(&line, &cap, fp) >= 0) {
        lineno++;
        char* text = puf_ini_trim(line);
        if (!text[0] || text[0] == '#') continue;

        char* fields[16];
        int nfields = 0;
        char* save = NULL;
        for (char* field = strtok_r(text, "\t", &save);
                field && nfields < 16;
                field = strtok_r(NULL, "\t", &save)) {
            fields[nfields++] = puf_ini_trim(field);
        }
        if (nfields < 2) {
            fprintf(stderr, "%s:%d: expected a tab-separated policy and checkpoint\n",
                path, lineno);
            exit(1);
        }

        char* last = fields[nfields - 1];
        if (strcmp(last, "checkpoint") == 0 || strcmp(last, "source") == 0
                || strcmp(last, "path") == 0) {
            continue;
        }
        const char* name = fields[0];
        if (nfields >= 3) {
            char* end = NULL;
            strtol(fields[0], &end, 10);
            if (end && !*end) name = fields[1];
        }
        if (list->size == LEAGUE_EVAL_MAX_POLICIES) {
            fprintf(stderr, "%s contains more than %d policies\n",
                path, LEAGUE_EVAL_MAX_POLICIES);
            exit(1);
        }
        if (access(last, R_OK) != 0) {
            fprintf(stderr, "%s:%d: checkpoint %s is not readable: %s\n",
                path, lineno, last, strerror(errno));
            exit(1);
        }
        LeagueEvalPolicy* policy = &list->items[list->size++];
        snprintf(policy->name, sizeof(policy->name), "%s", name);
        snprintf(policy->path, sizeof(policy->path), "%s", last);
    }
    free(line);
    fclose(fp);
    if (!list->size) {
        fprintf(stderr, "league manifest %s contains no policies\n", path);
        exit(1);
    }
}

static void league_eval_put_optional(Ini* ini, const char* section,
        const char* key, const char* value) {
    Dict* dict = puf_ini_section(ini, section, 0);
    if (dict_find(dict, key)) puf_ini_set(dict, key, value);
}

static void league_eval_reset(PuffeRL* pufferl) {
    VecEnv* vec = pufferl->vec;
#ifdef PUFFER_GPU_ENV
    puf_envs_reset(vec->envs, vec->gpu_observations, vec->gpu_rewards,
        vec->gpu_terminals, vec->total_agents);
#else
    for (int buf = 0; buf < vec->buffers; buf++) {
        while (__atomic_load_n(&vec->worker_state[buf], __ATOMIC_SEQ_CST)
                != BUF_WAITING) {
        }
    }

    #pragma omp parallel for schedule(static) num_threads(vec->num_workers)
    for (int i = 0; i < vec->size; i++) {
        vec->envs[i].log = (Log){0};
        vec->envs[i].boundary_reached = 0;
        puf_reset(&vec->envs[i]);
    }
    memset(vec->accum, 0,
        (size_t)vec->buffers * NUM_VEC_PROF * sizeof(float));

    size_t obs_bytes = (size_t)vec->total_agents * OBS_SIZE * sizeof(obs_t);
    size_t mask_bytes = (size_t)vec->total_agents * vec->action_mask_size;
    cudaMemcpy(vec->gpu_observations, vec->observations,
        obs_bytes, cudaMemcpyHostToDevice);
    cudaMemset(vec->gpu_rewards, 0,
        (size_t)vec->total_agents * sizeof(float));
    cudaMemset(vec->gpu_terminals, 0,
        (size_t)vec->total_agents * sizeof(float));
    cudaMemcpy(vec->gpu_action_mask, vec->action_mask,
        mask_bytes, cudaMemcpyHostToDevice);
#endif

    for (int buf = 0; buf < vec->buffers; buf++) {
        PrecisionTensor state = pufferl->buffer_states[buf];
        cudaMemset(state.data, 0,
            (size_t)numel(state.shape) * sizeof(precision_t));
        for (int bank = 0; bank < pufferl->num_frozen_banks; bank++) {
            state = pufferl->frozen_banks[bank].buffer_states[buf];
            cudaMemset(state.data, 0,
                (size_t)numel(state.shape) * sizeof(precision_t));
        }
        int agents = vec->agents_per_buffer;
        rng_init<<<grid_size(agents), BLOCK_SIZE>>>(
            pufferl->rng_states[buf], pufferl->seed + buf, agents);
    }
    cudaDeviceSynchronize();
    pufferl->global_step = 0;
}

static void league_eval_episode(PuffeRL* pufferl, int games, Dict* logs) {
    league_eval_reset(pufferl);
    for (;;) {
        rollouts(pufferl);
        int complete = 1;
        for (int bank = 0; bank < pufferl->num_frozen_banks; bank++) {
            dict_clear(&logs[bank]);
            vec_log_tag(pufferl->vec, bank + 1, &logs[bank]);
            if ((int)dict_get(&logs[bank], "n") < games) complete = 0;
        }
        if (complete) return;
    }
}

static void run_league_eval(Ini* ini, TrainContext* ctx) {
    const char* mode = puf_ini_get_str(ini, "league", "mode");
    int matrix = strcmp(mode, "matrix") == 0;
    int screen = strcmp(mode, "screen") == 0;
    if (!matrix && !screen) {
        fprintf(stderr, "league.mode must be matrix or screen\n");
        exit(1);
    }

    LeagueEvalList policies = {0};
    LeagueEvalList candidates = {0};
    LeagueEvalList opponents = {0};
    if (matrix) {
        league_eval_load_manifest(
            puf_ini_get_str(ini, "league", "policy_manifest"), &policies);
        if (policies.size < 2 || policies.size > SELFPLAY_MAX_BANKS + 1) {
            fprintf(stderr, "matrix league evaluation requires 2..%d policies; got %d\n",
                SELFPLAY_MAX_BANKS + 1, policies.size);
            exit(1);
        }
    } else {
        league_eval_load_manifest(
            puf_ini_get_str(ini, "league", "candidate_manifest"), &candidates);
        league_eval_load_manifest(
            puf_ini_get_str(ini, "league", "opponent_manifest"), &opponents);
        if (opponents.size > SELFPLAY_MAX_BANKS) {
            fprintf(stderr, "screen league evaluation supports at most %d opponents; got %d\n",
                SELFPLAY_MAX_BANKS, opponents.size);
            exit(1);
        }
    }

    int games = puf_ini_get_int(ini, "league", "games");
    if (games < 2 || games % 2) {
        fprintf(stderr, "league.games must be a positive even integer of at least 2\n");
        exit(1);
    }
    int banks = matrix ? policies.size - 1 : opponents.size;
    int total_agents = 2 * banks * games;
    char number[64];
    snprintf(number, sizeof(number), "%d", total_agents);
    puf_ini_put(ini, "vec.total_agents", number);
    puf_ini_put(ini, "vec.num_buffers", "1");
    snprintf(number, sizeof(number), "%d", banks);
    puf_ini_put(ini, "vec.num_frozen_banks", number);
    puf_ini_put(ini, "vec.frozen_bank_pct", "1");
    puf_ini_put(ini, "vec.seat_balance", "1");
    snprintf(number, sizeof(number), "%d",
        puf_ini_get_int(ini, "policy", "hidden_size"));
    puf_ini_put(ini, "vec.frozen_bank_hidden_size", number);
    snprintf(number, sizeof(number), "%d",
        puf_ini_get_int(ini, "policy", "num_layers"));
    puf_ini_put(ini, "vec.frozen_bank_num_layers", number);
    puf_ini_put(ini, "selfplay.enabled", "0");
    puf_ini_put(ini, "base.async", "0");
    puf_ini_put(ini, "base.reset_every_horizon", "0");
    snprintf(number, sizeof(number), "%d", ADV_VEC_WIDTH);
    puf_ini_put(ini, "train.horizon", number);
    puf_ini_put(ini, "env.dr", "0");
    puf_ini_put(ini, "env.num_agents", "2");
    puf_ini_put(ini, "env.num_bots", "0");
    league_eval_put_optional(ini, "env", "bot_opponent_fraction", "0");
    league_eval_put_optional(ini, "env", "bot_rules_fraction", "0");

    const char* output_path = puf_ini_get_str(ini, "league", "output");
    if (!output_path[0] || strcmp(output_path, "None") == 0) {
        fprintf(stderr, "league.output is required\n");
        exit(1);
    }
    FILE* output = fopen(output_path, "w");
    if (!output) {
        fprintf(stderr, "could not open league output %s: %s\n",
            output_path, strerror(errno));
        exit(1);
    }

    PuffeRL* pufferl = create_pufferl(ini, ctx);
    Dict logs[SELFPLAY_MAX_BANKS] = {0};
    if (matrix) {
        int waves = puf_ini_get_int(ini, "league", "focal_count");
        if (!waves) waves = policies.size - 1;
        if (waves < 1 || waves >= policies.size) {
            fprintf(stderr, "league.focal_count must be zero or in [1, %d]\n",
                policies.size - 1);
            exit(1);
        }
        int pairs = waves * (2 * policies.size - waves - 1) / 2;
        printf("Native league matrix: policies=%d pairs=%d games=%d banks=%d\n",
            policies.size, pairs, games, banks);
        for (int i = 0; i < waves; i++) {
            puf_load_weights_into(pufferl->master_weights, pufferl->param_puf,
                pufferl->default_stream, policies.items[i].path);
            pufferl_sync_loaded_policy(pufferl);
            int active = policies.size - i - 1;
            for (int bank = 0; bank < banks; bank++) {
                int opponent = i + 1 + (bank < active ? bank : 0);
                pufferl_load_frozen_bank(pufferl, bank,
                    policies.items[opponent].path);
            }
            league_eval_episode(pufferl, games, logs);
            for (int bank = 0; bank < active; bank++) {
                int j = i + bank + 1;
                fprintf(output, "%d\t%d\t%.9g\t%.9g\t%.9g\t%.9g\t%d\n",
                    i, j,
                    dict_get(&logs[bank], "slot_0_score"),
                    dict_get(&logs[bank], "draw_rate"),
                    dict_get(&logs[bank], "score"),
                    dict_get(&logs[bank], "opponent_score"),
                    (int)dict_get(&logs[bank], "n"));
            }
            fflush(output);
            printf("  wave=%d/%d learner=%s opponents=%d\n",
                i + 1, waves, policies.items[i].name, active);
        }
    } else {
        printf("Native league screen: candidates=%d opponents=%d games=%d\n",
            candidates.size, opponents.size, games);
        for (int bank = 0; bank < banks; bank++) {
            pufferl_load_frozen_bank(pufferl, bank, opponents.items[bank].path);
        }
        for (int i = 0; i < candidates.size; i++) {
            puf_load_weights_into(pufferl->master_weights, pufferl->param_puf,
                pufferl->default_stream, candidates.items[i].path);
            pufferl_sync_loaded_policy(pufferl);
            league_eval_episode(pufferl, games, logs);
            for (int bank = 0; bank < banks; bank++) {
                fprintf(output, "%d\t%d\t%.9g\t%.9g\t%.9g\t%.9g\t%d\n",
                    i, bank,
                    dict_get(&logs[bank], "slot_0_score"),
                    dict_get(&logs[bank], "draw_rate"),
                    dict_get(&logs[bank], "score"),
                    dict_get(&logs[bank], "opponent_score"),
                    (int)dict_get(&logs[bank], "n"));
            }
            fflush(output);
            printf("  candidate=%d/%d policy=%s\n",
                i + 1, candidates.size, candidates.items[i].name);
        }
    }
    for (int bank = 0; bank < banks; bank++) dict_clear(&logs[bank]);
    fclose(output);
    close_pufferl(pufferl);
    printf("Wrote %s\n", output_path);
}

EvalResult run_eval(Ini* ini, TrainContext* ctx, int mode, int verbose) {
    int render = mode == EVAL_RENDER;
    int match = mode == EVAL_MATCH;
    EvalResult result = {0};
    long num_games = puf_ini_get(ini, "base", "num_games");
    if (!num_games) {
        num_games = puf_ini_get(ini, "base", "eval_episodes");
    }
    long burnin_games = puf_ini_get(ini, "base", "burnin_games");
    if (!render && (num_games <= 0 || burnin_games < 0)) {
        fprintf(stderr, "eval requires positive num_games and nonnegative burnin_games\n");
        exit(1);
    }
    if (!render) {
        long eval_agents = puf_ini_get(ini, "base", "eval_agents");
        if (!eval_agents && match) {
            eval_agents = puf_ini_get(ini, "selfplay", "eval_agents");
        }
        if (eval_agents <= 0 && match) {
            eval_agents = 8192;
        } else if (eval_agents <= 0) {
            eval_agents = num_games / 8;
            if (eval_agents < 1024) {
                eval_agents = 1024;
            }
            if (eval_agents > 4096) {
                eval_agents = 4096;
            }
            if (eval_agents > num_games && num_games >= 1024) {
                eval_agents = num_games;
            }
        } else if (eval_agents > num_games && num_games >= 1024) {
            eval_agents = num_games;
        }
        eval_agents += (-eval_agents) % (match ? 4 : 2);

        char agents_buf[64];
        snprintf(agents_buf, sizeof(agents_buf), "%ld", eval_agents);
        // Two-player CPU environments need an even number of agents per
        // buffer. A one-buffer tail makes exact short fixed-bot evaluations
        // possible (for example 25 games = 50 agents) without padding games.
#ifdef PUF_GPU_ENV_BANK_LAYOUT
        /* Match-resident GPU environments own one physical batch. Splitting
         * policy buffers would desynchronize the match-to-bank row map. */
        puf_ini_put(ini, "vec.num_buffers", "1");
#else
        puf_ini_put(ini, "vec.num_buffers",
            eval_agents % 4 == 0 ? "2" : "1");
#endif
        puf_ini_put(ini, "vec.total_agents", agents_buf);
    }
    const char* match_enemy_spec = match
        ? puf_ini_get_str(ini, "base", "load_enemy_model_path") : NULL;
    int match_enemy_pass = match_enemy_spec
        && strcmp(match_enemy_spec, "pass") == 0;
    int match_enemy_rules = match_enemy_spec
        && strcmp(match_enemy_spec, "rules") == 0;
    int match_enemy_bot = match_enemy_pass || match_enemy_rules;
    if (match) {
        // Match opponents may use a different policy architecture.
        int enemy_hidden = (int)puf_ini_get(ini, "base", "enemy_hidden_size");
        int enemy_layers = (int)puf_ini_get(ini, "base", "enemy_num_layers");
        if (!enemy_hidden) enemy_hidden = (int)puf_ini_get(ini, "policy", "hidden_size");
        if (!enemy_layers) enemy_layers = (int)puf_ini_get(ini, "policy", "num_layers");
        char hidden_buf[32], layers_buf[32];
        snprintf(hidden_buf, sizeof(hidden_buf), "%d", enemy_hidden);
        snprintf(layers_buf, sizeof(layers_buf), "%d", enemy_layers);
        puf_ini_put(ini, "vec.num_frozen_banks", match_enemy_bot ? "0" : "1");
        puf_ini_put(ini, "vec.frozen_bank_pct", match_enemy_bot ? "0" : "1");
        puf_ini_put(ini, "vec.frozen_bank_hidden_size", hidden_buf);
        puf_ini_put(ini, "vec.frozen_bank_num_layers", layers_buf);
        puf_ini_put(ini, "selfplay.enabled", "0");
        league_eval_put_optional(ini, "env", "dr", "0");
        league_eval_put_optional(ini, "env", "num_agents",
            match_enemy_bot ? "1" : "2");
        league_eval_put_optional(ini, "env", "num_bots",
            match_enemy_bot ? "1" : "0");
        if (match_enemy_bot) {
            league_eval_put_optional(ini, "env", "bot_opponent_fraction", "1");
            league_eval_put_optional(ini, "env", "bot_pass_fraction",
                match_enemy_pass ? "1" : "0");
            league_eval_put_optional(ini, "env", "bot_rules_fraction",
                match_enemy_rules ? "1" : "0");
            league_eval_put_optional(ini, "env", "bot_script_fraction", "0");
            league_eval_put_optional(ini, "env", "bot_adaptive_fraction", "0");
        } else {
            // Environment-specific scripted curricula must not replace either
            // policy during a requested model-vs-model match.
            league_eval_put_optional(ini, "env", "bot_opponent_fraction", "0");
            league_eval_put_optional(ini, "env", "bot_rules_fraction", "0");
        }
    }
    puf_ini_put(ini, "base.reset_every_horizon", "0");
    char eval_horizon[16];
    snprintf(eval_horizon, sizeof(eval_horizon), "%d", ADV_VEC_WIDTH);
    puf_ini_put(ini, "train.horizon", eval_horizon);

    PuffeRL* pufferl = create_pufferl(ini, ctx);
    if (match) {
        char a_path_buf[4096];
        char b_path_buf[4096];
        const char* a_path = puf_checkpoint_path_key(ini,
            "load_model_path", a_path_buf, sizeof(a_path_buf));
        const char* b_path = match_enemy_bot ? NULL : puf_checkpoint_path_key(ini,
            "load_enemy_model_path", b_path_buf, sizeof(b_path_buf));
        if (!a_path || (!match_enemy_bot && !b_path)) {
            fprintf(stderr, "match requires base.load_model_path and base.load_enemy_model_path\n");
            exit(1);
        }
        puf_load_weights_into(pufferl->master_weights,
            pufferl->param_puf, pufferl->default_stream, a_path);
        if (!match_enemy_bot) pufferl_load_frozen_bank(pufferl, 0, b_path);
    } else {
        char resolved_path[4096];
        const char* load_path = puf_checkpoint_path_key(ini,
            "load_model_path", resolved_path, sizeof(resolved_path));
        if (load_path) {
            puf_load_weights_into(pufferl->master_weights, pufferl->param_puf,
                pufferl->default_stream, load_path);
            printf("Loaded weights from %s\n", load_path);
        }
    }
    pufferl_sync_loaded_policy(pufferl);

    Dict baseline = {0};
    long baseline_n = 0;
    while (true) {
        if (render) {
            puf_render(&pufferl->vec->envs[0]);
        }
        rollouts(pufferl);
        Dict log = {0};
        trainer_eval_log(pufferl, &log);
        if (render) {
            puf_dashboard_print(ini, pufferl, &log, 0);
            continue;
        }

        long n = (long)dict_get(&log, "env/n");
        if (match) {
            if (n < num_games) {
                if (verbose && n > 0) {
                    printf("\rgames=%ld/%ld  A=%.3f  B=%.3f  draw=%.3f",
                        n, num_games,
                        dict_get(&log, "env/slot_0_score"),
                        dict_get(&log, "env/slot_1_score"),
                        dict_get(&log, "env/draw_rate"));
                }
                continue;
            }
            result.score = (float)dict_get(&log, "env/slot_0_score");
            result.draw = (float)dict_get(&log, "env/draw_rate");
            result.money = (float)dict_get(&log, "env/score");
            DictItem* opp = dict_find(&log, "env/opponent_score");
            result.opponent_money = opp
                ? (float)opp->value
                : (float)dict_get(&log, "env/slot_1_score");
            result.games = (int)n;
            break;
        }
        if (n == 0) {
            continue;
        }

        if (n == 0) {
            continue;
        }

        if (burnin_games > 0 && baseline_n == 0 && n >= burnin_games) {
            baseline = log;
            baseline_n = n;
            if (verbose) {
                printf("\rbot_eval_burnin=%ld/%ld", n, burnin_games);
            }
            continue;
        }

        double scored_n = n - baseline_n;
        double score = dict_get(&log, "env/score");
        double perf = dict_get(&log, "env/perf");
        if (baseline_n > 0 && scored_n > 0) {
            double base_n = (double)baseline_n;
            double cur_n = (double)n;
            score = (score * cur_n - dict_get(&baseline, "env/score") * base_n) / scored_n;
            perf = (perf * cur_n - dict_get(&baseline, "env/perf") * base_n) / scored_n;
        }
        if (verbose) {
            printf("\rbot_eval=%.0f/%ld  perf=%.4f  score=%.3f",
                scored_n, num_games, perf, score);
        }
        if ((n - baseline_n) >= num_games && (!burnin_games || baseline_n > 0)) {
            result.score = (float)score;
            result.games = (int)scored_n;
            if (verbose) {
                printf("\n{");
                int emitted = 0;
                for (int i = 0; i < log.size; i++) {
                    const char* key = log.items[i].key;
                    if (strncmp(key, "env/", 4) != 0) {
                        continue;
                    }
                    double value = log.items[i].value;
                    if (baseline_n > 0 && scored_n > 0) {
                        DictItem* base = dict_find(&baseline, key);
                        if (base) {
                            value = (value * (double)n -
                                base->value * (double)baseline_n) / scored_n;
                        }
                    }
                    printf("%s\"%s\":%.9g", emitted++ ? "," : "", key, value);
                }
                printf("}");
            }
            break;
        }
    }
    if (!render && verbose) {
        if (match) {
            printf("match_result games=%d score=%.9g draw=%.9g money=%.9g opponent_money=%.9g\n",
                result.games, result.score, result.draw, result.money,
                result.opponent_money);
            printf("\rgames=%d/%ld  A=%.3f  B=%.3f  draw=%.3f\n",
                result.games, num_games, result.score, 1.0f - result.score, result.draw);
        } else {
            printf("\n");
        }
    }
    close_pufferl(pufferl);
    return result;
}

TrainResult run_train(Ini* ini, TrainContext* ctx) {
    int use_selfplay = puf_ini_get(ini, "selfplay", "enabled");
#ifdef PUFFER_GPU_ENV
    // GPU selfplay rotation is host-driven from per-bank completed-episode
    // counters maintained by the environment; tags/boundaries live on device.
#endif
    if (!use_selfplay) {
        puf_ini_put(ini, "vec.num_frozen_banks", "0");
        puf_ini_put(ini, "vec.frozen_bank_pct", "0");
    }

    char run_id[64];
    const char* configured_run_id = puf_ini_get_str(ini, "base", "run_id");
    if (!configured_run_id[0] || strcmp(configured_run_id, "None") == 0) {
        snprintf(run_id, sizeof(run_id), "%ld", (long)(1000.0 * wall_clock()));
        puf_ini_put(ini, "base.run_id", run_id);
    } else {
        snprintf(run_id, sizeof(run_id), "%s", configured_run_id);
    }

    char checkpoint_dir[2048];
    char log_dir[2048];
    snprintf(checkpoint_dir, sizeof(checkpoint_dir), "%s/%s/%s",
        puf_ini_get_str(ini, "base", "checkpoint_dir"),
        puf_ini_get_str(ini, "base", "env_name"), run_id);
    snprintf(log_dir, sizeof(log_dir), "%s/%s",
        puf_ini_get_str(ini, "base", "log_dir"),
        puf_ini_get_str(ini, "base", "env_name"));
    if (ctx->artifact_owner) {
        mkdir_p(checkpoint_dir);
        mkdir_p(log_dir);
    }

    PuffeRL* pufferl = create_pufferl(ini, ctx);
    char resolved_path[4096];
    const char* load_path = puf_checkpoint_path_key(ini,
        "load_model_path", resolved_path, sizeof(resolved_path));
    if (load_path) {
        puf_load_weights_into(pufferl->master_weights, pufferl->param_puf,
            pufferl->default_stream, load_path);
        printf("Loaded weights from %s\n", load_path);
        if (pufferl->hypers.emag_kl_coef > 0.0f) {
            char magnet_path[8192];
            snprintf(magnet_path, sizeof(magnet_path), "%s.emag", load_path);
            if (access(magnet_path, R_OK) == 0) {
                puf_load_weights_into(pufferl->magnet_master_weights,
                    pufferl->magnet_param_puf, pufferl->default_stream,
                    magnet_path);
                printf("Loaded EMA magnet from %s\n", magnet_path);
            } else {
                cudaMemcpyAsync(pufferl->magnet_master_weights.data,
                    pufferl->master_weights.data,
                    numel(pufferl->master_weights.shape) * sizeof(float),
                    cudaMemcpyDeviceToDevice, pufferl->default_stream);
                if (USE_BF16) {
                    int n = numel(pufferl->magnet_param_puf.shape);
                    cast<<<grid_size(n), BLOCK_SIZE, 0,
                        pufferl->default_stream>>>(pufferl->magnet_param_puf.data,
                        pufferl->magnet_master_weights.data, n);
                }
                printf("Initialized EMA magnet from loaded policy\n");
            }
        }
#ifdef PUF_LOAD_HOOK
        PUF_LOAD_HOOK(load_path, ini);
#endif
    }
    // Keep the rollout actor identical to the loaded learner before any
    // self-play bootstrap or first rollout.  This is a no-op apart from the
    // stream fence when async collection is disabled.
    pufferl_sync_loaded_policy(pufferl);
    Selfplay selfplay = {0};
    if (use_selfplay) {
        // A requested checkpoint is the first historical opponent.  A fresh
        // run uses an in-memory frozen copy of the initialized learner rather
        // than manufacturing a disk checkpoint named step zero.
        const char* initial_opponent = load_path
            ? load_path : SELFPLAY_MEMORY_BOOTSTRAP;
        selfplay.num_banks = pufferl->num_frozen_banks;
        if (selfplay.num_banks <= 0 || selfplay.num_banks > SELFPLAY_MAX_BANKS) {
            fprintf(stderr, "selfplay requires 1..%d frozen banks\n", SELFPLAY_MAX_BANKS);
            exit(1);
        }
        selfplay.max_size = (int)puf_ini_get(ini, "selfplay", "max_size");
        assert(selfplay.max_size > 0 && "selfplay.max_size must be positive");
        selfplay.pool = (char (*)[SELFPLAY_PATH_MAX])calloc(
            (size_t)selfplay.max_size, sizeof(*selfplay.pool));
        selfplay.opp_timeout_steps = (long)puf_ini_get(ini, "selfplay", "opp_timeout_steps");
        selfplay.rng = (unsigned int)puf_ini_get(ini, "selfplay", "seed")
            + (unsigned int)pufferl->hypers.rank;
        selfplay.external_prob = (float)puf_ini_get(
            ini, "selfplay", "opponent_pool_prob");
        selfplay.pfsp_alpha = (float)puf_ini_get(ini, "selfplay", "pfsp_alpha");
        selfplay.pfsp_uniform_mix = (float)puf_ini_get(
            ini, "selfplay", "pfsp_uniform_mix");
        const char* pfsp_mode = puf_ini_get_str(ini, "selfplay", "pfsp_mode");
        if (strcmp(pfsp_mode, "hard") == 0) {
            selfplay.pfsp_mode = PFSP_HARD;
        } else if (strcmp(pfsp_mode, "variance") == 0) {
            selfplay.pfsp_mode = PFSP_VARIANCE;
        } else {
            fprintf(stderr, "selfplay.pfsp_mode must be hard or variance\n");
            exit(1);
        }
        selfplay.payoff_ema = (float)puf_ini_get(ini, "selfplay", "payoff_ema");
        assert(selfplay.external_prob >= 0.0f && selfplay.external_prob <= 1.0f
            && "selfplay.opponent_pool_prob must be in [0, 1]");
        assert(selfplay.pfsp_alpha >= 0.0f && "selfplay.pfsp_alpha must be nonnegative");
        assert(selfplay.pfsp_uniform_mix >= 0.0f && selfplay.pfsp_uniform_mix <= 1.0f
            && "selfplay.pfsp_uniform_mix must be in [0, 1]");
        assert(selfplay.payoff_ema > 0.0f && selfplay.payoff_ema <= 1.0f
            && "selfplay.payoff_ema must be in (0, 1]");
        snprintf(selfplay.payoff_path, sizeof(selfplay.payoff_path),
            "%s/payoffs.tsv", checkpoint_dir);
        selfplay_add_external_list(&selfplay,
            puf_ini_get_str(ini, "selfplay", "opponent_pool"));
        selfplay_add_external_league(&selfplay,
            puf_ini_get_str(ini, "selfplay", "opponent_league"));
        char enemy_resolved[4096];
        const char* enemy_path = puf_checkpoint_path_key(ini,
            "load_enemy_model_path", enemy_resolved, sizeof(enemy_resolved));
        if (enemy_path) selfplay_add_external(&selfplay, enemy_path);
        size_t frozen_bytes = (size_t)numel(
            pufferl->frozen_banks[0].master_weights.shape) * sizeof(float);
        selfplay_validate_external(&selfplay, frozen_bytes);
        selfplay_set_external_weights(&selfplay,
            puf_ini_get_str(ini, "selfplay", "opponent_pool_weights"));
        long current_step = pufferl->global_step * pufferl->hypers.world_size;

#ifndef PUFFER_GPU_ENV
        Env* envs = pufferl->vec->envs;
        for (int i = 0; i < pufferl->vec->size; i++) {
            int tag = envs[i].tag;
            if (tag > 0 && tag <= selfplay.num_banks) {
                selfplay.banks[tag - 1].num_envs++;
            }
        }
#else
        /* Each frozen-bank row in the layout belongs to exactly one match, so
         * the row span is the per-bank match count for rotation gating. */
        for (int b = 0; b < selfplay.num_banks; b++) {
            selfplay.banks[b].num_envs =
                pufferl->vec->bank_layout[b + 1]
                - pufferl->vec->bank_layout[b];
        }
#endif

        selfplay_add_checkpoint(&selfplay, initial_opponent);
        printf("Selfplay pool: history=%d external=%d banks=%d external_prob=%.2f "
            "pfsp=%s:%.2f uniform=%.2f\n",
            selfplay.pool_size, selfplay.external_size,
            selfplay.num_banks, selfplay.external_prob, pfsp_mode,
            selfplay.pfsp_alpha, selfplay.pfsp_uniform_mix);
        for (int b = 0; b < selfplay.num_banks; b++) {
            const char* initial_path = selfplay.external_prob > 0.0f
                    && selfplay.external_size >= selfplay.num_banks
                ? selfplay.external[b] : selfplay_sample(&selfplay);
            selfplay_load_bank(&selfplay, pufferl, b,
                initial_path, current_step);
        }
        if (ctx->artifact_owner) selfplay_write_payoffs(&selfplay);
    }

    long total_timesteps = puf_ini_get(ini, "train", "total_timesteps");
    long batch_size = (long)puf_ini_get(ini, "vec", "total_agents") *
        (long)puf_ini_get(ini, "train", "horizon");
    long local_timesteps = total_timesteps / ctx->world_size;
    long train_epochs = local_timesteps / batch_size;
    long eval_epochs = train_epochs / 2;
#ifdef PUF_SWEEP_SCORE
    if (puf_ini_get(ini, "base", "result_fd") > 0) {
        eval_epochs = 0;
    }
#endif
    // Optional multiplier for longer post-train eval (noise studies). Default 1.
    double eval_epoch_mult = puf_ini_get(ini, "base", "eval_epoch_mult");
    if (eval_epoch_mult > 1.0) {
        eval_epochs = (long)fmax(1, (double)eval_epochs * eval_epoch_mult);
    }
    long checkpoint_interval = puf_ini_get(ini, "base", "checkpoint_interval");
    long eval_episodes = puf_ini_get(ini, "base", "eval_episodes");
    // Sweep objective: bare names → env/<name>; keys with '/' used as-is.
    char target_key[128];
    const char* metric = puf_ini_get_str(ini, "sweep", "metric");
    snprintf(target_key, sizeof(target_key), "%s%s",
        strchr(metric, '/') ? "" : "env/", metric);
    Dict last_log = {0};
    PufLogHistory log_history = {0};
    TrainResult result = {0};
    char final_checkpoint[4096] = {0};
    double last_dashboard_time = 0;

    for (long epoch = 0; epoch < train_epochs + eval_epochs; epoch++) {
        if (epoch < train_epochs && pufferl->hypers.async) {
            int prefetch_next = epoch + 1 < train_epochs;
            if (!pufferl->async_bootstrapped) {
                double t0 = rollout_start(pufferl, 0);
                rollout_finish(pufferl, t0);
                pufferl->async_ready_slot = 0;
                pufferl->async_next_slot = 1;
                pufferl->async_bootstrapped = true;
            }

            int ready_slot = pufferl->async_ready_slot;
            int next_slot = pufferl->async_next_slot;
            double t0 = 0.0;
            if (prefetch_next) {
                t0 = rollout_start(pufferl, next_slot);
            }

            pufferl->global_step += pufferl->hypers.horizon * pufferl->hypers.total_agents;
            RolloutBuf train_src = rollout_time_view(&pufferl->rollouts,
                ready_slot * pufferl->hypers.horizon, pufferl->hypers.horizon);
            train_impl(*pufferl, &train_src);

            if (prefetch_next) {
                rollout_finish(pufferl, t0);
                pufferl->async_ready_slot = next_slot;
                pufferl->async_next_slot = ready_slot;
            }
        } else {
            rollouts(pufferl);
            if (epoch < train_epochs) {
                train_impl(*pufferl, NULL);
            }
        }

        bool is_final = epoch == train_epochs - 1;
        bool interval_save = checkpoint_interval > 0 &&
            (epoch + 1) % checkpoint_interval == 0;
        bool should_save = epoch < train_epochs && (interval_save || is_final);
        char saved_checkpoint[4096] = {0};
        if (should_save) {
            snprintf(saved_checkpoint, sizeof(saved_checkpoint),
                "%s/%016ld.bin", checkpoint_dir, pufferl->global_step);
            if (ctx->artifact_owner) {
                puf_save_weights(pufferl, saved_checkpoint);
#ifdef PUF_CHECKPOINT_HOOK
                PUF_CHECKPOINT_HOOK(saved_checkpoint, ini);
#endif
                snprintf(final_checkpoint, sizeof(final_checkpoint),
                    "%s", saved_checkpoint);
            }
        }
        if (use_selfplay && saved_checkpoint[0]) {
            selfplay_add_checkpoint(&selfplay, saved_checkpoint);
        }

        int is_eval = epoch >= train_epochs;
        if (!is_eval && last_log.size &&
                wall_clock() < pufferl->last_log_time + 0.6 && epoch < train_epochs - 1) {
            continue;
        }

        Dict new_log = {0};
        if (is_eval) {
            trainer_eval_log(pufferl, &new_log);
        } else {
            long global_step = pufferl->global_step;
            double now = wall_clock();
            double dt = now - pufferl->last_log_time;
            double sps = dt > 0
                ? (double)(global_step - pufferl->last_log_step) / dt * pufferl->hypers.world_size
                : 0;
            pufferl->last_log_time = now;
            pufferl->last_log_step = global_step;

            dict_set(&new_log, "SPS", sps);
            dict_set(&new_log, "agent_steps", (double)global_step * pufferl->hypers.world_size);
            dict_set(&new_log, "uptime", now - pufferl->start_time);
            dict_set(&new_log, "epoch", (double)pufferl->epoch);

            if (use_selfplay) {
                for (int b = 0; b < selfplay.num_banks; b++) {
                    SelfplayBank* bank = &selfplay.banks[b];
                    Dict payoff = {0};
                    vec_log_tag(pufferl->vec, b + 1, &payoff);
                    char key[64];
                    snprintf(key, sizeof(key), "league/bank_%d_opponent", b);
                    dict_set(&new_log, key, bank->opponent);
                    float samples = (float)dict_get(&payoff, "n");
                    if (samples > 0.0f) {
                        float score = (float)dict_get(&payoff, "slot_0_score");
                        float draw = (float)dict_get(&payoff, "draw_rate");
                        selfplay_record(&selfplay, bank->opponent,
                            score, draw, samples);
                        snprintf(key, sizeof(key), "league/bank_%d_score", b);
                        dict_set(&new_log, key, score);
                        snprintf(key, sizeof(key), "league/bank_%d_draw", b);
                        dict_set(&new_log, key, draw);
                    }
                    dict_clear(&payoff);
                }
                if (ctx->artifact_owner && saved_checkpoint[0]) {
                    selfplay_write_payoffs(&selfplay);
                }
            }
            vec_log(pufferl->vec, &new_log, 1);

            float losses_host[NUM_LOSSES];
            cudaMemcpy(losses_host, pufferl->losses_puf.data, sizeof(losses_host),
                cudaMemcpyDeviceToHost);
            float loss_n = losses_host[LOSS_N];
            float inv_n = loss_n > 0 ? 1.0f / loss_n : 0.0f;
            dict_set(&new_log, "loss/policy", losses_host[LOSS_PG] * inv_n);
            dict_set(&new_log, "loss/value", losses_host[LOSS_VF] * inv_n);
            dict_set(&new_log, "loss/entropy", losses_host[LOSS_ENT] * inv_n);
            dict_set(&new_log, "loss/total", losses_host[LOSS_TOTAL] * inv_n);
            dict_set(&new_log, "loss/old_kl", losses_host[LOSS_OLD_APPROX_KL] * inv_n);
            dict_set(&new_log, "loss/kl", losses_host[LOSS_APPROX_KL] * inv_n);
            dict_set(&new_log, "loss/clipfrac", losses_host[LOSS_CLIPFRAC] * inv_n);
            dict_set(&new_log, "loss/emag_kl", losses_host[LOSS_EMAG_KL] * inv_n);
            cudaMemset(pufferl->losses_puf.data, 0,
                numel(pufferl->losses_puf.shape) * sizeof(float));

            log_util(pufferl, &new_log);

            float train_total = 0;
            for (int i = 0; i < (int)(sizeof(PROF_NAMES) / sizeof(PROF_NAMES[0])); i++) {
                float sec = pufferl->profile.accum[i] / 1000.0f;
                char key[256];
                snprintf(key, sizeof(key), "perf/%s", PROF_NAMES[i]);
                dict_set(&new_log, key, sec);
                if (i >= PROF_TRAIN_MISC) {
                    train_total += sec;
                }
            }
            dict_set(&new_log, "perf/train", train_total);
            memset(pufferl->profile.accum, 0, sizeof(pufferl->profile.accum));
        }
        for (int i = 0; i < new_log.size; i++) {
            DictItem* item = &new_log.items[i];
            dict_set(&last_log, item->key, item->value);
        }
        dict_clear(&new_log);
#ifndef PUFFER_GPU_ENV
        if (use_selfplay && !is_eval) {
            long current_step = pufferl->global_step * pufferl->hypers.world_size;
            Env* envs = pufferl->vec->envs;
            for (int b = 0; b < selfplay.num_banks; b++) {
                SelfplayBank* bank = &selfplay.banks[b];
                int tag = b + 1;
                if (bank->pending_path[0]) {
                    int aligned = 0;
                    for (int i = 0; i < pufferl->vec->size; i++) {
                        if (envs[i].tag == tag && envs[i].boundary_reached) aligned++;
                    }
                    if (aligned >= bank->num_envs) {
                        selfplay_load_bank(&selfplay, pufferl, b,
                            bank->pending_path, current_step);
                        for (int i = 0; i < pufferl->vec->size; i++) {
                            if (envs[i].tag == tag) envs[i].boundary_reached = 0;
                        }
                        bank->pending_path[0] = 0;
                    }
                } else if (selfplay.opp_timeout_steps > 0 &&
                        current_step - bank->opp_started_step >= selfplay.opp_timeout_steps) {
                    snprintf(bank->pending_path, sizeof(bank->pending_path), "%s", selfplay_sample(&selfplay));
                    for (int i = 0; i < pufferl->vec->size; i++) {
                        if (envs[i].tag == tag) envs[i].boundary_reached = 0;
                    }
                }
            }
            dict_set(&last_log, "pool/size", selfplay.pool_size);
            dict_set(&last_log, "pool/external_size", selfplay.external_size);
            dict_set(&last_log, "pool/num_banks", selfplay.num_banks);
            dict_set(&last_log, "league/opponents", selfplay.payoff_size);
        }
#else
        if (use_selfplay && !is_eval) {
            long current_step = pufferl->global_step * pufferl->hypers.world_size;
            int counts[SELFPLAY_MAX_BANKS] = {0};
            puf_envs_selfplay_counts(counts, selfplay.num_banks);
            for (int b = 0; b < selfplay.num_banks; b++) {
                SelfplayBank* bank = &selfplay.banks[b];
                if (bank->pending_path[0]) {
                    if (counts[b] >= bank->num_envs) {
                        selfplay_load_bank(&selfplay, pufferl, b,
                            bank->pending_path, current_step);
                        puf_envs_selfplay_clear(b + 1);
                        bank->pending_path[0] = 0;
                    }
                } else if (selfplay.opp_timeout_steps > 0 &&
                        current_step - bank->opp_started_step
                            >= selfplay.opp_timeout_steps) {
                    snprintf(bank->pending_path, sizeof(bank->pending_path),
                        "%s", selfplay_sample(&selfplay));
                    puf_envs_selfplay_clear(b + 1);
                }
            }
            dict_set(&last_log, "pool/size", selfplay.pool_size);
            dict_set(&last_log, "pool/external_size", selfplay.external_size);
            dict_set(&last_log, "pool/num_banks", selfplay.num_banks);
            dict_set(&last_log, "league/opponents", selfplay.payoff_size);
        }
#endif

        int eval_done = is_eval && dict_get(&last_log, "env/n") > eval_episodes;
        int loop_done = epoch == train_epochs + eval_epochs - 1;
        double now = wall_clock();
        int print_dashboard = !is_eval || eval_done || loop_done || now >= last_dashboard_time + 0.6;
        if (ctx->artifact_owner && print_dashboard) {
            puf_dashboard_print(ini, pufferl, &last_log, (int)pufferl->epoch);
            last_dashboard_time = now;
        }

        // Wait until the objective appears; do not treat negative values as missing.
        if (!dict_find(&last_log, target_key)) {
            continue;
        }
        if (!is_eval) {
            puf_log_history_add(&log_history, &last_log);
        }
        if (eval_done) {
            break;
        }
    }

    // Selfplay: single final observation only (pool eval or last train metric).
    // Normal sweeps: downsampled learning curve for multi-fidelity protein.
    // uptime/agent_steps always from the train log path. target_key only after the
    // train loop has seen at least one objective sample (else leave 0).
    result.cost = (float)dict_get(&last_log, "uptime");
    result.steps = (float)dict_get(&last_log, "agent_steps");
    DictItem* target = dict_find(&last_log, target_key);
    result.score = target ? (float)target->value : 0;

    int points = use_selfplay ? 1 : (int)puf_ini_get(ini, "sweep", "downsample");
    assert(points >= 1 && points <= TRAIN_RESULT_MAX_POINTS
        && "sweep.downsample must be in [1, TRAIN_RESULT_MAX_POINTS]");
    result.points = points;

    if (log_history.size == 0 || points == 1) {
        result.scores[0] = result.score;
        result.costs[0] = result.cost;
        result.step_points[0] = result.steps;
    } else {
        // History entries are only added after target_key appears, so all keys exist.
        float final_steps = (float)dict_get(
            &log_history.items[log_history.size - 1], "agent_steps");
        int cursor = 0;
        for (int p = 0; p < points; p++) {
            float step_target = final_steps * (float)p / (float)(points - 1);
            while (cursor + 1 < log_history.size &&
                    (float)dict_get(&log_history.items[cursor], "agent_steps") < step_target) {
                cursor++;
            }
            Dict* log = &log_history.items[cursor];
            result.scores[p] = (float)dict_get(log, target_key);
            result.costs[p] = (float)dict_get(log, "uptime");
            result.step_points[p] = (float)dict_get(log, "agent_steps");
        }
        result.scores[points - 1] = result.score;
        result.costs[points - 1] = result.cost;
        result.step_points[points - 1] = result.steps;
    }
    close_pufferl(pufferl);

    // Selfplay end rating: seat-balanced matches against a fixed external panel.
    // This replaces the noisy final training rollout as the protein objective.
    if (use_selfplay && final_checkpoint[0] && ctx->artifact_owner) {
        int max_opp = (int)puf_ini_get(ini, "selfplay", "eval_pool_size");
        long games = (long)puf_ini_get(ini, "selfplay", "eval_games");
        if (max_opp > 0 && games > 0) {
            const char* eval_metric = puf_ini_get_str(
                ini, "selfplay", "eval_metric");
            if (strcmp(eval_metric, "winrate") != 0 &&
                    strcmp(eval_metric, "money") != 0) {
                fprintf(stderr, "selfplay.eval_metric must be winrate or money\n");
                exit(1);
            }
            int n_opp = 0;
            char games_buf[32];
            snprintf(games_buf, sizeof(games_buf), "%ld", games);
            puf_ini_put(ini, "base.num_games", games_buf);

            float sum = 0;
            int pool_size = selfplay.external_size + selfplay.pool_size;
            for (int i = 0; i < pool_size && n_opp < max_opp; i++) {
                const char* opponent = i < selfplay.external_size
                    ? selfplay.external[i]
                    : selfplay.pool[i - selfplay.external_size];
                if (strcmp(opponent, final_checkpoint) == 0 ||
                        strcmp(opponent, SELFPLAY_MEMORY_BOOTSTRAP) == 0) {
                    continue;
                }

                puf_ini_put(ini, "base.load_model_path", final_checkpoint);
                puf_ini_put(ini, "base.load_enemy_model_path", opponent);
                EvalResult first = run_eval(ini, ctx, EVAL_MATCH, 0);

                puf_ini_put(ini, "base.load_model_path", opponent);
                puf_ini_put(ini, "base.load_enemy_model_path", final_checkpoint);
                EvalResult second = run_eval(ini, ctx, EVAL_MATCH, 0);

                float winrate = 0.5f * (first.score + 1.0f - second.score);
                float draw = 0.5f * (first.draw + second.draw);
                float money = 0.5f * (first.money + second.opponent_money);
                sum += strcmp(eval_metric, "money") == 0 ? money : winrate;
                n_opp++;
                printf("selfplay_eval vs %s games=%d/seat win=%.4f draw=%.4f "
                    "money=%.1f\n", opponent, first.games, winrate, draw, money);
            }
            if (n_opp) {
                float pool_score = sum / n_opp;
                printf("selfplay_eval metric=%s mean=%.4f n=%d\n",
                    eval_metric, pool_score, n_opp);
                result.score = pool_score;
                result.points = 1;
                result.scores[0] = pool_score;
                result.costs[0] = result.cost;
                result.step_points[0] = result.steps;
                dict_set(&last_log, "selfplay/pool_score", pool_score);
            }
        }
    }

#ifdef PUF_SWEEP_SCORE
    if (ctx->artifact_owner && final_checkpoint[0]
            && puf_ini_get(ini, "base", "result_fd") > 0) {
        double exact_start = wall_clock();
        result.score = PUF_SWEEP_SCORE(final_checkpoint, ini);
        result.cost += (float)(wall_clock() - exact_start);
        result.points = 1;
        result.scores[0] = result.score;
        result.costs[0] = result.cost;
        result.step_points[0] = result.steps;
        dict_set(&last_log, "sweep/exact_score", result.score);
    }
#endif

    if (ctx->artifact_owner) {
        puf_log_history_add(&log_history, &last_log);
        char log_path[4096];
        snprintf(log_path, sizeof(log_path), "%s/%s.ini", log_dir, run_id);

        FILE* fp = fopen(log_path, "w");
        if (!fp) {
            fprintf(stderr, "failed to write log %s\n", log_path);
            exit(1);
        }

        fprintf(fp, "# PufferLib log v1\n");
        puf_ini_write(fp, ini);
        fprintf(fp, "\n[metrics]\n");

        int downsample = (int)puf_ini_get(ini, "sweep", "downsample");
        Dict keys = {0};
        for (int i = 0; i < log_history.size; i++) {
            Dict* log = &log_history.items[i];
            for (int j = 0; j < log->size; j++) {
                if (!dict_find(&keys, log->items[j].key)) {
                    dict_set(&keys, log->items[j].key, 0);
                }
            }
        }
        int metric_points = downsample <= 1 ? 1 : downsample;
        double* out = (double*)calloc((size_t)metric_points, sizeof(double));
        double final_steps = dict_get(
            &log_history.items[log_history.size - 1], "agent_steps");

        for (int k = 0; k < keys.size; k++) {
            const char* key = keys.items[k].key;
            if (strncmp(key, "loss/", 5) == 0) {
                continue;
            }

            // Keys may be sparse across history (e.g. env/* only after first episode).
            double first_value = 0;
            for (int i = 0; i < log_history.size; i++) {
                DictItem* item = dict_find(&log_history.items[i], key);
                if (item) {
                    first_value = item->value;
                    break;
                }
            }

            if (metric_points == 1) {
                DictItem* item = dict_find(&log_history.items[log_history.size - 1], key);
                out[0] = item ? item->value : first_value;
                fprintf(fp, "%s = %.17g\n", key, out[0]);
                continue;
            }

            int out_idx = 0;
            int bin_n = 0;
            double fallback = first_value;
            double bin_sum = 0;
            double next_bin = final_steps / (metric_points - 1);
            for (int i = 0; i < log_history.size; i++) {
                Dict* log = &log_history.items[i];
                DictItem* item = dict_find(log, key);
                if (item) {
                    bin_sum += item->value;
                    bin_n++;
                }

                double steps = dict_get(log, "agent_steps");
                if (steps < next_bin || out_idx >= metric_points - 1) {
                    continue;
                }

                double reduced = bin_n ? bin_sum / bin_n : fallback;
                out[out_idx++] = reduced;
                fallback = reduced;
                bin_n = 0;
                bin_sum = 0;
                next_bin += final_steps / (metric_points - 1);
            }

            DictItem* final_item = dict_find(
                &log_history.items[log_history.size - 1], key);
            out[metric_points - 1] = final_item
                ? final_item->value
                : bin_n ? bin_sum / bin_n : fallback;
            while (out_idx < metric_points - 1) {
                out[out_idx++] = fallback;
            }
            fprintf(fp, "%s = ", key);
            for (int i = 0; i < metric_points; i++) {
                fprintf(fp, "%s%.17g", i ? "," : "", out[i]);
            }
            fputc('\n', fp);
        }

        free(out);
        fclose(fp);
    }
    for (int i = 0; i < log_history.size; i++) {
        dict_clear(&log_history.items[i]);
    }
    free(log_history.items);
    if (use_selfplay && ctx->artifact_owner) selfplay_write_payoffs(&selfplay);
    free(selfplay.pool);
    free(selfplay.external);
    free(selfplay.external_weights);
    free(selfplay.payoffs);
    return result;
}

// Behavioral cloning mode: ./puffer bc kaggriculture [section.key=value ...]
// Plays the selected script tape for bc_steps, collects (obs, expert action)
// pairs, then minimizes -log pi(a_expert | obs). Saves an anchor checkpoint
// that can later regularize self-play via the EMAg KL term.
#if defined(KG_NUM_PLAYERS)
static int run_bc(Ini* ini) {
    TrainContext ctx = {.world_size = 1, .artifact_owner = 1};
    PuffeRL* pufferl = create_pufferl(ini, &ctx);
    int bc_steps = (int)puf_ini_get(ini, "bc", "steps");
    int bc_profile = (int)puf_ini_get(ini, "bc", "profile");
    int bc_epochs = (int)puf_ini_get(ini, "bc", "epochs");
    float bc_lr = (float)puf_ini_get(ini, "bc", "learning_rate");
    if (bc_steps <= 0 || bc_steps > 720) bc_steps = 26;
    if (bc_epochs <= 0) bc_epochs = 2000;
    if (bc_lr <= 0.0f) bc_lr = 0.0003f;

    printf("BC: profile=%d steps=%d epochs=%d lr=%g\n",
        bc_profile, bc_steps, bc_epochs, bc_lr);

    // Play the tape on a single CPU env and record (obs, expert actions).
    Env env = {};
    env.rng = 0;
    puf_init(&env, puf_ini_section(ini, "env", 0));
    puf_reset(&env);

    int obs_size = OBS_SIZE;
    int num_atns = NUM_ATNS;
    int packed_stride = (pufferl->vec->action_mask_size + 7) / 8;
    obs_t* obs_data = (obs_t*)xcalloc((size_t)bc_steps * obs_size);
    float* expert_data = (float*)xcalloc(
        (size_t)bc_steps * num_atns * sizeof(float));
    unsigned char* mask_data = (unsigned char*)xcalloc(
        (size_t)bc_steps * packed_stride);
    if (!obs_data || !expert_data || !mask_data) return 1;

    for (int t = 0; t < bc_steps && !env.game_storage.done; t++) {
        // Expert actions from the tape for player 0 (the learner).
        KGAction expert = {};
        kag_script_action_from_tapes(&env.game_storage, 0,
            bc_profile, &expert, kag_script_tapes);
        kag_script_repair(&env.game_storage, 0, bc_profile, &expert);
        kag_clear_policy_actions(&env.agents[0]);
        kag_set_policy_unit(&env.agents[0], 0,
            expert.farmer.op, expert.farmer.arg, expert.farmer.n);
        for (int h = 0; h < expert.hand_count && h < KG_POLICY_DIRECT_HANDS; h++) {
            kag_set_policy_unit(&env.agents[0], h + 1,
                expert.hands[h].op, expert.hands[h].arg, expert.hands[h].n);
        }
        for (int o = 0; o < expert.market_count && o < KG_POLICY_MARKET_SLOTS; o++) {
            kag_set_policy_market(&env.agents[0], o,
                expert.market[o].op, expert.market[o].item, expert.market[o].n);
        }
        memcpy(obs_data + (size_t)t * obs_size,
            env.agents[0].observations, obs_size);
        memcpy(expert_data + (size_t)t * num_atns,
            env.agents[0].actions, num_atns * sizeof(float));
        int mask_stride = pufferl->vec->action_mask_size;
        for (int byte = 0; byte < packed_stride; byte++) {
            unsigned char bits = 0;
            for (int bit = 0; bit < 8; bit++) {
                int action = byte * 8 + bit;
                if (action < mask_stride
                        && env.agents[0].action_mask[action]) {
                    bits |= (unsigned char)(1u << bit);
                }
            }
            mask_data[(size_t)t * packed_stride + byte] = bits;
        }
        // Step the env with the expert actions so the next obs is on-policy.
        KGAction actions[KG_NUM_PLAYERS] = {expert, {}};
        actions[1].farmer = (KGUnitAction){KG_OP_PASS, -1, 1};
        actions[1].hand_count = env.game_storage.players[1].hand_count;
        for (int h = 0; h < actions[1].hand_count; h++) {
            actions[1].hands[h] = (KGUnitAction){KG_OP_PASS, -1, 1};
        }
        kg_step(&env.game_storage, actions);
        kag_write_all_observations_from_tapes(&env, kag_script_tapes);
    }

    // Upload expert dataset to device.
    precision_t* d_obs = (precision_t*)xcuda(
        (size_t)bc_steps * obs_size * sizeof(precision_t));
    float* d_expert = (float*)xcuda(
        (size_t)bc_steps * num_atns * sizeof(float));
    unsigned char* d_mask = (unsigned char*)xcuda(
        (size_t)bc_steps * packed_stride);
    obs_t* d_obs_raw = (obs_t*)xcuda((size_t)bc_steps * obs_size);
    cudaMemcpy(d_obs_raw, obs_data, (size_t)bc_steps * obs_size,
        cudaMemcpyHostToDevice);
    cast<<<grid_size(bc_steps * obs_size), BLOCK_SIZE, 0,
        pufferl->default_stream>>>(d_obs, d_obs_raw, bc_steps * obs_size);
    cudaMemcpy(d_expert, expert_data,
        (size_t)bc_steps * num_atns * sizeof(float),
        cudaMemcpyHostToDevice);
    cudaMemcpy(d_mask, mask_data,
        (size_t)bc_steps * packed_stride,
        cudaMemcpyHostToDevice);
    free(obs_data); free(expert_data); free(mask_data);

    // BC loop: forward, cross-entropy, backward, muon step.
    int A_total = pufferl->vec->action_mask_size;
    int mask_stride = (A_total + 7) / 8;
    float* grad_logits = (float*)xcuda(
        (size_t)bc_steps * A_total * sizeof(float));
    float* loss_acc = (float*)xcuda(sizeof(float));
    pufferl->muon.lr_puf = {};  // reset, set below
    float lr = bc_lr;
    cudaMemcpy(pufferl->muon.lr_puf.data, &lr, sizeof(float),
        cudaMemcpyHostToDevice);

    // Reuse the train activation buffers sized for B=bc_steps, T=1.
    PrecisionTensor obs_t = {.data = d_obs,
        .shape = {bc_steps, 1, obs_size}};
    PrecisionTensor state = pufferl->buffer_states[0];
    PrecisionTensor terminals = {};

    for (int ep = 0; ep < bc_epochs; ep++) {
        cudaMemsetAsync(grad_logits, 0,
            (size_t)bc_steps * A_total * sizeof(float), pufferl->default_stream);
        cudaMemsetAsync(loss_acc, 0, sizeof(float), pufferl->default_stream);
        cudaMemsetAsync(state.data, 0, numel(state.shape) * sizeof(precision_t),
            pufferl->default_stream);
        PrecisionTensor dec_out = policy_forward_train(&pufferl->policy,
            pufferl->weights, pufferl->train_activations,
            obs_t, state, terminals, pufferl->default_stream);
        (void)dec_out;
        // policy_forward_train returns (B, T, fused_cols); squeeze T=1 for the
        // BC kernel which expects (B, fused_cols).
        PrecisionTensor dec_flat = *puf_squeeze(&dec_out, 1);
        bc_loss_kernel<<<grid_size(bc_steps), BLOCK_SIZE, 0,
            pufferl->default_stream>>>(
            dec_flat.data, d_expert, d_mask, grad_logits, loss_acc,
            pufferl->act_sizes_puf.data, bc_steps, A_total, num_atns,
            mask_stride);
        // Forward the gradient through the network; value/logstd empty.
        FloatTensor grad_logits_t = {.data = grad_logits,
            .shape = {bc_steps, 1, A_total}};
        policy_backward(&pufferl->policy, pufferl->weights,
            pufferl->train_activations, grad_logits_t, FloatTensor(),
            FloatTensor(), pufferl->default_stream);
        muon_step(&pufferl->muon, pufferl->master_weights, pufferl->grad_puf,
            pufferl->hypers.max_grad_norm, pufferl->default_stream);
        cudaDeviceSynchronize();
        if ((ep + 1) % 200 == 0) {
            float loss = 0.0f;
            cudaMemcpy(&loss, loss_acc, sizeof(float), cudaMemcpyDeviceToHost);
            printf("BC epoch %d loss=%.4f\n", ep + 1, loss / bc_steps);
        }
    }

    // Save the anchor.
    char out[4096];
    const char* out_path = puf_ini_get_str(ini, "bc", "output");
    snprintf(out, sizeof(out), "%s", out_path && out_path[0]
        ? out_path : "saved/kaggriculture_bc_anchor.bin");
    puf_save_weights(pufferl, out);
    printf("BC anchor saved to %s\n", out);

    cudaFree(d_obs); cudaFree(d_expert); cudaFree(d_mask);
    cudaFree(d_obs_raw);
    cudaFree(grad_logits); cudaFree(loss_acc);
    close_pufferl(pufferl);
    return 0;
#endif

// Fork DP workers before CUDA initialization. Sweep trials occupy contiguous GPU
// blocks; rank 0 owns the last GPU and writes TrainResult to base.result_fd.
TrainResult launch_train(Ini* ini) {
    int mb = (int)puf_ini_get(ini, "train", "minibatch_size");
    int horizon = (int)puf_ini_get(ini, "train", "horizon");
    int agents = (int)puf_ini_get(ini, "vec", "total_agents");
    int world_size = (int)puf_ini_get(ini, "train", "gpus");
    assert(world_size >= 1 && "train.gpus must be >= 1");
    assert(horizon > 0 && "train.horizon must be positive");
    /* Rollout buffers are sized for the full agent set, so a sampled
     * minibatch larger than horizon*total_agents can never run. Snap it down
     * before the shape asserts; create_pufferl later snaps to the primary
     * (non-frozen) batch when selfplay shrinks the trainable set. */
    if (mb % horizon != 0 || mb > (long)horizon * agents) {
        int max_mb = (int)((long)horizon * agents);
        mb = (mb / horizon) * horizon;
        if (mb > max_mb) mb = max_mb;
        if (mb < horizon) mb = horizon;
        fprintf(stderr, "train.minibatch_size adjusted %d -> %d "
            "(horizon=%d agents=%d)\n",
            (int)puf_ini_get(ini, "train", "minibatch_size"), mb,
            horizon, agents);
        char buf[64];
        snprintf(buf, sizeof(buf), "%d", mb);
        puf_ini_put(ini, "train.minibatch_size", buf);
    }
    assert(mb % horizon == 0
        && "train.minibatch_size must be divisible by train.horizon");
    assert((long)mb <= (long)horizon * agents
        && "train.minibatch_size must be <= train.horizon * vec.total_agents");

    int gpu_offset = (int)puf_ini_get(ini, "base", "gpu_offset");

    ncclUniqueId nccl_id;
    ncclUniqueId* nccl_ptr = NULL;
    if (world_size > 1) {
        ncclGetUniqueId(&nccl_id);
        nccl_ptr = &nccl_id;
    }

    int n_workers = world_size - 1;
    pid_t* pids = (pid_t*)calloc((size_t)n_workers, sizeof(pid_t));
    for (int rank = world_size - 1; rank >= 1; rank--) {
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "fork failed: %s\n", strerror(errno));
            exit(1);
        }
        if (pid == 0) {
            assert(freopen("/dev/null", "w", stdout) == stdout);
            TrainContext child = {
                .rank = rank,
                .world_size = world_size,
                .gpu_id = gpu_offset + rank - 1,
                .artifact_owner = 0,
                .nccl_id = nccl_ptr,
            };
            run_train(ini, &child);
            puf_ini_free(ini);
            exit(0);
        }
        pids[rank - 1] = pid;
    }

    TrainContext host = {
        .rank = 0,
        .world_size = world_size,
        .gpu_id = gpu_offset + world_size - 1,
        .artifact_owner = 1,
        .nccl_id = nccl_ptr,
    };
    TrainResult result = run_train(ini, &host);
    for (int i = 0; i < n_workers; i++) {
        int status = 0;
        waitpid(pids[i], &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            fprintf(stderr, "train rank worker pid %d failed\n", (int)pids[i]);
            exit(1);
        }
    }
    free(pids);

    // Sweep parent reads this over the pipe; CLI train ignores (result_fd=0).
    int result_fd = (int)puf_ini_get(ini, "base", "result_fd");
    if (result_fd > 0) {
        assert(write(result_fd, &result, sizeof(result)) == sizeof(result));
        close(result_fd);
    }
    return result;
}

#ifdef PUFFERLIB_BUILD_MAIN
int main(int argc, char** argv) {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
    if (argc < 3) {
        fprintf(stderr, "usage: %s train|eval|eval_bot|match|league|sweep ENV [section.key=value ...]\n", argv[0]);
        exit(1);
    }

    const char* mode = argv[1];
    Ini ini = {0};
    puf_ini_load_env(&ini, argv[2], argc - 3, argv + 3);
    TrainContext ctx = {.world_size = 1, .artifact_owner = 1};

    if (strcmp(mode, "bc") == 0) {
#if defined(KG_NUM_PLAYERS)
        run_bc(&ini);
#else
        fprintf(stderr, "bc mode requires the kaggriculture env\n");
        exit(1);
#endif
    } else if (strcmp(mode, "train") == 0) {
        launch_train(&ini);
    } else if (strcmp(mode, "sweep") == 0) {
        run_sweep(&ini, argv[0]);
    } else if (strcmp(mode, "eval") == 0 || strcmp(mode, "eval_bot") == 0) {
        if (strcmp(mode, "eval_bot") == 0) {
            puf_ini_put(&ini, "vec.num_frozen_banks", "0");
            puf_ini_put(&ini, "vec.frozen_bank_pct", "0");
            puf_ini_put(&ini, "selfplay.enabled", "0");
            league_eval_put_optional(&ini, "env", "dr", "0");
            league_eval_put_optional(&ini, "env", "num_agents", "1");
            league_eval_put_optional(&ini, "env", "num_bots", "1");
        }
        // Headless score eval (use a separate interactive path if you need render).
        run_eval(&ini, &ctx, EVAL_SCORE, 1);
    } else if (strcmp(mode, "match") == 0) {
        run_eval(&ini, &ctx, EVAL_MATCH, 1);
    } else if (strcmp(mode, "league") == 0) {
        run_league_eval(&ini, &ctx);
    } else {
        fprintf(stderr, "unknown mode: %s\n", mode);
        exit(1);
    }

    puf_ini_free(&ini);
    return 0;
}

#endif
