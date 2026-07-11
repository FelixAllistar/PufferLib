// No-Python native API for a PufferLib fork.
//
// This file is meant to live inside PufferLib/src beside bindings.cu. It should
// be compiled into libpuffer_<env>_<precision>.* by `puffers build`.

#include "puffer_api.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdio>

#include "pufferlib.cu"

struct PufferHandle {
    std::unique_ptr<PuffeRL> pufferl;
    std::string last_error;
};

static int fail(PufferHandle* handle, const std::string& message) {
    if (handle) {
        handle->last_error = message;
    }
    return -1;
}

static HypersT hypers_from_config(const PufferConfig* config) {
    HypersT hypers;
    hypers.horizon = config->horizon;
    hypers.total_agents = config->total_agents;
    hypers.num_buffers = config->num_buffers;
    hypers.num_atns = 0;
    hypers.hidden_size = config->hidden_size;
    hypers.num_layers = config->num_layers;
    hypers.lr = config->learning_rate;
    hypers.min_lr_ratio = config->min_lr_ratio;
    hypers.anneal_lr = config->anneal_lr != 0;
    hypers.beta1 = config->beta1;
    hypers.beta2 = config->beta2;
    hypers.eps = config->eps;
    hypers.minibatch_size = config->minibatch_size;
    hypers.replay_ratio = config->replay_ratio;
    hypers.total_timesteps = (long)config->total_timesteps;
    hypers.max_grad_norm = config->max_grad_norm;
    hypers.clip_coef = config->clip_coef;
    hypers.vf_clip_coef = config->vf_clip_coef;
    hypers.vf_coef = config->vf_coef;
    hypers.ent_coef = config->ent_coef;
    hypers.min_ent_coef_ratio = config->min_ent_coef_ratio;
    hypers.anneal_ent_coef = config->anneal_ent_coef != 0;
    hypers.gamma = config->gamma;
    hypers.gae_lambda = config->gae_lambda;
    hypers.vtrace_rho_clip = config->vtrace_rho_clip;
    hypers.vtrace_c_clip = config->vtrace_c_clip;
    hypers.prio_alpha = config->prio_alpha;
    hypers.prio_beta0 = config->prio_beta0;
    hypers.reset_state = config->reset_state != 0;
    hypers.cudagraphs = config->cudagraphs;
    hypers.profile = config->profile != 0;
    hypers.rank = config->rank;
    hypers.world_size = config->world_size;
    hypers.gpu_id = config->gpu_id;
    hypers.nccl_id = config->nccl_id ? std::string(config->nccl_id) : std::string();
    hypers.num_threads = config->num_threads;
    hypers.seed = config->seed;
    return hypers;
}

static Dict* make_vec_dict(const PufferConfig* config) {
    Dict* dict = create_dict(16);
    dict_set(dict, "total_agents", config->total_agents);
    dict_set(dict, "num_buffers", config->num_buffers);
    dict_set(dict, "num_threads", config->num_threads);
    // Optional self-play/frozen-bank fields are intentionally absent unless the
    // Rust config layer adds them to this ABI later.
    return dict;
}

static Dict* make_env_dict(const PufferConfig* config) {
    int capacity = (int)config->env_item_count + 64;
    Dict* dict = create_dict(capacity);
    for (size_t i = 0; i < config->env_item_count; i++) {
        const PufferConfigItem& item = config->env_items[i];
        if (item.key != nullptr) {
            dict_set(dict, item.key, item.value);
        }
    }
    return dict;
}

static int save_weights_impl(PufferHandle* handle, const char* path) {
    PuffeRL& pufferl = *handle->pufferl;
    int64_t nbytes = numel(pufferl.master_weights.shape) * sizeof(float);
    std::vector<char> buf(nbytes);
    cudaMemcpy(buf.data(), pufferl.master_weights.data, nbytes, cudaMemcpyDeviceToHost);
    FILE* f = fopen(path, "wb");
    if (!f) return fail(handle, std::string("failed to open model for writing: ") + path);
    size_t written = fwrite(buf.data(), 1, nbytes, f);
    fclose(f);
    if ((int64_t)written != nbytes) {
        return fail(handle, std::string("short write while saving model: ") + path);
    }
    return 0;
}

static int load_weights_impl(PufferHandle* handle, const char* path) {
    PuffeRL& pufferl = *handle->pufferl;
    int64_t nbytes = numel(pufferl.master_weights.shape) * sizeof(float);
    FILE* f = fopen(path, "rb");
    if (!f) return fail(handle, std::string("failed to open model for reading: ") + path);
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size != nbytes) {
        fclose(f);
        return fail(handle, "weight file size mismatch: expected " + std::to_string(nbytes)
            + " bytes, got " + std::to_string(file_size));
    }
    std::vector<char> buf(nbytes);
    size_t nread = fread(buf.data(), 1, nbytes, f);
    fclose(f);
    if ((int64_t)nread != nbytes) {
        return fail(handle, std::string("short read while loading model: ") + path);
    }
    cudaMemcpy(pufferl.master_weights.data, buf.data(), nbytes, cudaMemcpyHostToDevice);
    if (USE_BF16) {
        int n = numel(pufferl.param_puf.shape);
        cast<<<grid_size(n), BLOCK_SIZE, 0, pufferl.default_stream>>>(
            pufferl.param_puf.data, pufferl.master_weights.data, n);
    }
    cudaStreamSynchronize(pufferl.default_stream);
    cudaDeviceSynchronize();
    return 0;
}

extern "C" PufferHandle* puffer_create(const PufferConfig* config) {
    if (config == nullptr || config->abi_version != PUFFER_ABI_VERSION || config->env_name == nullptr) {
        return nullptr;
    }

    std::unique_ptr<PufferHandle> handle(new PufferHandle());
    try {
        int device_count = 0;
        cudaGetDeviceCount(&device_count);
        if (device_count <= 0) {
            handle->last_error = "CUDA is not available";
            return handle.release();
        }

        HypersT hypers = hypers_from_config(config);
        Dict* vec_dict = make_vec_dict(config);
        Dict* env_dict = make_env_dict(config);
        handle->pufferl = create_pufferl_impl(hypers, std::string(config->env_name), vec_dict, env_dict);
        if (!handle->pufferl) {
            handle->last_error = "CUDA OOM: failed to allocate training buffers";
        }
    } catch (const std::exception& e) {
        handle->last_error = e.what();
    }
    return handle.release();
}

extern "C" int puffer_rollouts(PufferHandle* handle) {
    if (!handle || !handle->pufferl) return fail(handle, "puffer handle is not initialized");
    PuffeRL& pufferl = *handle->pufferl;
    double t0 = wall_clock();
    if (pufferl.hypers.reset_state) {
        for (int i = 0; i < pufferl.hypers.num_buffers; i++) {
            puf_zero(&pufferl.buffer_states[i], pufferl.default_stream);
        }
        for (int b = 0; b < pufferl.num_frozen_banks; b++) {
            for (int i = 0; i < pufferl.hypers.num_buffers; i++) {
                puf_zero(&pufferl.frozen_banks[b].buffer_states[i], pufferl.default_stream);
            }
        }
    }
    static_vec_omp_step(pufferl.vec);
    float sec = (float)(wall_clock() - t0);
    pufferl.profile.accum[PROF_ROLLOUT] += sec * 1000.0f;
    float eval_prof[NUM_EVAL_PROF];
    static_vec_read_profile(pufferl.vec, eval_prof);
    pufferl.profile.accum[PROF_EVAL_GPU] += eval_prof[EVAL_GPU];
    pufferl.profile.accum[PROF_EVAL_ENV] += eval_prof[EVAL_ENV_STEP];
    pufferl.global_step += pufferl.hypers.horizon * pufferl.hypers.total_agents;
    return 0;
}

extern "C" int puffer_train_step(PufferHandle* handle) {
    if (!handle || !handle->pufferl) return fail(handle, "puffer handle is not initialized");
    train_impl(*handle->pufferl);
    return 0;
}

extern "C" int puffer_eval_step(PufferHandle* handle) {
    return puffer_rollouts(handle);
}

extern "C" int puffer_load_weights(PufferHandle* handle, const char* path) {
    if (!handle || !handle->pufferl) return fail(handle, "puffer handle is not initialized");
    if (!path) return fail(handle, "model path is null");
    return load_weights_impl(handle, path);
}

extern "C" int puffer_save_weights(PufferHandle* handle, const char* path) {
    if (!handle || !handle->pufferl) return fail(handle, "puffer handle is not initialized");
    if (!path) return fail(handle, "model path is null");
    return save_weights_impl(handle, path);
}

extern "C" int puffer_render(PufferHandle* handle, int env_id) {
    if (!handle || !handle->pufferl) return fail(handle, "puffer handle is not initialized");
    static_vec_render(handle->pufferl->vec, env_id);
    return 0;
}

extern "C" int puffer_log_json(PufferHandle* handle, char* out, size_t out_len) {
    if (!handle || !handle->pufferl) return fail(handle, "puffer handle is not initialized");
    if (!out || out_len == 0) return fail(handle, "log buffer is empty");
    PuffeRL& pufferl = *handle->pufferl;
    int gpus = pufferl.hypers.world_size;
    double now = wall_clock();
    double dt = now - pufferl.last_log_time;
    long sps = dt > 0 ? (long)((pufferl.global_step - pufferl.last_log_step) / dt) : 0;
    pufferl.last_log_time = now;
    pufferl.last_log_step = pufferl.global_step;
    snprintf(out, out_len,
        "{\"SPS\":%ld,\"agent_steps\":%ld,\"uptime\":%.6f,\"epoch\":%ld}",
        sps * gpus,
        pufferl.global_step * gpus,
        now - pufferl.start_time,
        pufferl.epoch);
    return 0;
}

extern "C" int puffer_close(PufferHandle* handle) {
    if (!handle) return 0;
    if (handle->pufferl) {
        if (handle->pufferl->hypers.cudagraphs < 0) {
            handle->pufferl->train_cudagraph = nullptr;
            if (handle->pufferl->fused_rollout_cudagraphs == nullptr) {
                size_t graph_count = (size_t)handle->pufferl->hypers.horizon *
                    (size_t)handle->pufferl->hypers.num_buffers;
                handle->pufferl->fused_rollout_cudagraphs =
                    (cudaGraphExec_t*)calloc(graph_count, sizeof(cudaGraphExec_t));
            }
        }
        close_impl(*handle->pufferl);
        handle->pufferl.reset();
    }
    delete handle;
    return 0;
}

extern "C" const char* puffer_last_error(PufferHandle* handle) {
    if (!handle || handle->last_error.empty()) {
        return "unknown puffer backend error";
    }
    return handle->last_error.c_str();
}
