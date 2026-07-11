#ifndef PUFFERS_PUFFER_API_H
#define PUFFERS_PUFFER_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PUFFER_ABI_VERSION 1u

typedef struct PufferHandle PufferHandle;

typedef enum PufferPrecision {
    PUFFER_PRECISION_BF16 = 0,
    PUFFER_PRECISION_F32 = 1,
} PufferPrecision;

typedef struct PufferConfigItem {
    const char* key;
    double value;
} PufferConfigItem;

typedef struct PufferConfig {
    uint32_t abi_version;
    const char* env_name;
    int gpu_id;
    int rank;
    int world_size;
    const char* nccl_id;
    PufferPrecision precision;

    int total_agents;
    int num_buffers;
    int num_threads;

    int horizon;
    uint64_t total_timesteps;
    int minibatch_size;
    float replay_ratio;

    int hidden_size;
    int num_layers;
    int expansion_factor;

    float learning_rate;
    float min_lr_ratio;
    int anneal_lr;
    float beta1;
    float beta2;
    float eps;
    float gamma;
    float gae_lambda;
    float ent_coef;
    float min_ent_coef_ratio;
    int anneal_ent_coef;
    float vf_coef;
    float vf_clip_coef;
    float clip_coef;
    float max_grad_norm;
    float vtrace_rho_clip;
    float vtrace_c_clip;
    float prio_alpha;
    float prio_beta0;
    int reset_state;
    int cudagraphs;
    int profile;
    int seed;

    const char* env_config_json;
    const PufferConfigItem* env_items;
    size_t env_item_count;
} PufferConfig;

PufferHandle* puffer_create(const PufferConfig* config);
int puffer_rollouts(PufferHandle* handle);
int puffer_train_step(PufferHandle* handle);
int puffer_eval_step(PufferHandle* handle);
int puffer_load_weights(PufferHandle* handle, const char* path);
int puffer_save_weights(PufferHandle* handle, const char* path);
int puffer_render(PufferHandle* handle, int env_id);
int puffer_log_json(PufferHandle* handle, char* out, size_t out_len);
int puffer_close(PufferHandle* handle);
const char* puffer_last_error(PufferHandle* handle);

#ifdef __cplusplus
}
#endif

#endif
