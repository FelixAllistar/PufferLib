#pragma once
#include "../../src/ini.h"
#include "goofspiel_obs.h"
#include <math.h>

// CPU-only preflight, also used before the native sweep starts a worker.
// Reject coupled shapes instead of silently changing the sampled parameters.
static inline const char* gs_training_config_error(Ini* ini, int alignment) {
    const char* integer_sections[] = {
        "vec", "vec", "vec", "policy", "policy", "train", "train", "train"};
    const char* integer_keys[] = {"total_agents", "num_buffers", "num_threads", "hidden_size",
        "num_layers", "horizon", "minibatch_size", "total_timesteps"};
    for (int i = 0; i < 8; ++i) {
        double x = puf_ini_get(ini, integer_sections[i], integer_keys[i]);
        if (!isfinite(x) || x < 1 || x > 1e12 || floor(x) != x) {
            return "agents/buffers/threads/model size/horizon/minibatch/budget"
                " must be positive integers";
        }
    }
    long long agents = puf_ini_get(ini, "vec", "total_agents");
    long long buffers = puf_ini_get(ini, "vec", "num_buffers");
    long long threads = puf_ini_get(ini, "vec", "num_threads");
    long long horizon = puf_ini_get(ini, "train", "horizon");
    long long minibatch = puf_ini_get(ini, "train", "minibatch_size");
    long long width = puf_ini_get(ini, "policy", "hidden_size");
    long long layers = puf_ini_get(ini, "policy", "num_layers");
    if (agents > 1000000000 / horizon) {
        return "rollout shape is too large";
    }
    if (agents % (2 * buffers)) {
        return "total_agents must be divisible by 2*num_buffers";
    }
    if (threads < buffers || threads % buffers) {
        return "num_threads must be a multiple of num_buffers and at least num_buffers";
    }
    if (horizon % alignment) {
        return "horizon does not satisfy the trainer's vector alignment";
    }
    if (minibatch % horizon) {
        return "minibatch_size must be divisible by horizon";
    }
    if (agents % (minibatch / horizon)) {
        return "minibatch sequences must divide total_agents";
    }
    if (width < 8 || width > 256 || width % 8 || layers > 16) {
        return "exact-evaluated policy requires width 8-256 (multiple of 8) and 1-16 layers";
    }
    const char* architecture = puf_ini_get_str(ini, "policy", "architecture");
    if (strcmp(architecture, "puffernet") && strcmp(architecture, "transformer")) {
        return "unknown policy.architecture";
    }
    if (!strcmp(architecture, "transformer")) {
        double heads = puf_ini_get(ini, "policy", "transformer_heads");
        if (heads < 1 || floor(heads) != heads || heads > width || width % (int)heads) {
            return "transformer_heads must divide hidden_size";
        }
    }
    long long learner_rows = agents;
    if (puf_ini_get(ini, "selfplay", "enabled")) {
        double policies = puf_ini_get(ini, "vec", "num_policies");
        double fraction = puf_ini_get(ini, "vec", "hist_policy_percent");
        if (policies < 2 || policies > 9 || floor(policies) != policies) {
            return "selfplay requires 2-9 policies";
        }
        if (!(fraction > 0 && fraction <= 1)) {
            return "hist_policy_percent must be in (0,1]";
        }
        long long frozen = (long long)((agents / buffers / 2) * fraction);
        if (frozen < policies - 1) {
            return "not enough frozen games per buffer";
        }
        learner_rows -= frozen * buffers;
    }
    if (learner_rows % (minibatch / horizon)) {
        return "minibatch sequences must divide learner rows";
    }
    if (puf_ini_get(ini, "env", "exact_exploiter")) {
        int banks = puf_ini_get(ini, "env", "exact_exploiter_banks");
        if (!puf_ini_get(ini, "selfplay", "enabled") || banks < 1
            || banks >= puf_ini_get(ini, "vec", "num_policies")) {
            return "exact_exploiter requires selfplay and enough frozen policy banks";
        }
    }
    if (puf_ini_get(ini, "train", "vtrace")
        && puf_ini_get(ini, "train", "vtrace_c_clip")
            > puf_ini_get(ini, "train", "vtrace_rho_clip")) {
        return "vtrace_c_clip must not exceed vtrace_rho_clip";
    }
    double replay = puf_ini_get(ini, "train", "replay_ratio");
    if (!isfinite(replay) || replay <= 0 || replay > 1e6) {
        return "invalid replay_ratio";
    }
    if (replay * learner_rows * horizon / minibatch < 1) {
        return "zero optimizer updates";
    }
    if (puf_ini_get(ini, "train", "total_timesteps")
        < agents * horizon * puf_ini_get(ini, "train", "gpus")) {
        return "training budget is smaller than one rollout";
    }
    double limit = puf_ini_get(ini, "sweep", "memory_limit_mb");
    if (limit > 0) {
        // Four-card estimate: rollout copies, network caches, optimizer, exact eval.
        // Use the buffer counts in algo.cu and native_transformer.cuh, plus headroom.
        int precision = alignment == 4 ? 4 : 2;
        int async = puf_ini_get(ini, "base", "async") != 0;
        int policies = puf_ini_get(ini, "selfplay", "enabled")
            ? puf_ini_get(ini, "vec", "num_policies") : 1;
        int carry = puf_ini_get(ini, "base", "reset_every_horizon") == 0;
        double bytes = ((1 + async) * agents + learner_rows) * horizon
            * (precision * (OBS_SIZE + 4 + GS_NUM_CARDS) + 4.0);
        bytes += precision * width * layers
            * (agents + learner_rows * (1 + carry * (1 + async)));
        bytes += agents * (OBS_SIZE + GS_NUM_CARDS + 96.0)
            + minibatch * (3 * precision + 4 * (GS_NUM_CARDS + 2.0));
        double params, matrix_width;
        if (!strcmp(architecture, "transformer")) {
            double tokens = GS_NUM_CARDS + 1, features = 6 + GS_COMPACT_OBS_SIZE - GS_O_SCORES;
            double heads = puf_ini_get(ini, "policy", "transformer_heads");
            double cache = (18 + 20 * layers) * tokens * width
                + (1 + layers) * heads * tokens * tokens;
            double stats = 4 * tokens * (2 + 4 * layers);
            bytes += (agents + minibatch) * (precision * cache + stats);
            bytes += 512 * (4 * cache + stats); // Exact evaluator's FP32 inference chunk.
            bytes += precision * ((agents + minibatch) * tokens * (width + features)
                + (agents + 3 * minibatch) * width
                + (agents + 2 * minibatch) * (GS_NUM_CARDS + 1));
            bytes += (policies * buffers + 2) * 4.0 * 1024 * 1024;
            params = layers * (12 * width * width + 13 * width)
                + (features + GS_NUM_CARDS + 3) * width;
            matrix_width = 4 * width;
            if (precision == 2) {
                bytes += 4 * params;
            }
        } else {
            bytes += precision * (agents * ((3 * layers + 3) * width + GS_NUM_CARDS + 1)
                + minibatch * (OBS_SIZE + 2 * (GS_NUM_CARDS + 1) + (10 * layers + 4) * width)
                + (minibatch / horizon) * (2 * layers + 1) * width);
            bytes += 3456 * 4.0 * (OBS_SIZE + 5 * width + GS_NUM_CARDS);
            params = 3 * layers * width * width + (OBS_SIZE + GS_NUM_CARDS + 1) * width;
            matrix_width = fmax(OBS_SIZE, 3 * width);
        }
        bytes += params * (precision * (policies + 1 + async) + 8
            + (precision == 2 ? 4 * policies : 0));
        bytes += precision * (2 * width * width + width * matrix_width);
        bytes += 7108 * 4.0 * layers * width + 2 * 1024 * 1024;
        // cuBLAS workspaces, child context, sweep GP growth, graphs and allocation padding.
        bytes = 1.1 * bytes + (256 + 64 * (buffers + 1)) * 1024.0 * 1024;
        if (bytes > limit * 1024 * 1024) {
            return "estimated training/evaluation memory exceeds sweep.memory_limit_mb";
        }
    }
    return NULL;
}

static inline void gs_prepare_sweep(Ini* ini) {
    int transformer = !strcmp(puf_ini_get_str(ini, "policy", "architecture"), "transformer");
    // Fixed ranges follow CLI values; model-specific no-op knobs are not searched.
    for (int s = 0; s < ini->num_sections; ++s) {
        Dict* section = ini->sections + s;
        if (strncmp(section->name, "sweep.", 6)) {
            continue;
        }
        int inactive = (!transformer && !strcmp(section->name, "sweep.policy.transformer_heads"))
            || (transformer && !strcmp(section->name, "sweep.base.reset_every_horizon"));
        if (!inactive && dict_get(section, "min") != dict_get(section, "max")) {
            continue;
        }
        const char* key = section->name + 6;
        const char* dot = strrchr(key, '.');
        if (!dot) {
            continue;
        }
        char parent[PUF_DICT_MAX_KEY], value[64];
        snprintf(parent, sizeof(parent), "%.*s", (int)(dot - key), key);
        snprintf(value, sizeof(value), "%.17g", puf_ini_get(ini, parent, dot + 1));
        char bound[2 * PUF_DICT_MAX_KEY];
        snprintf(bound, sizeof(bound), "%s.min", section->name);
        puf_ini_put(ini, bound, value);
        snprintf(bound, sizeof(bound), "%s.max", section->name);
        puf_ini_put(ini, bound, value);
    }
#ifdef __CUDACC__
    size_t available, total;
    assert(cudaSetDevice(puf_ini_get(ini, "base", "gpu_offset")) == cudaSuccess);
    assert(cudaMemGetInfo(&available, &total) == cudaSuccess);
    double limit = puf_ini_get(ini, "sweep", "memory_limit_mb");
    assert(isfinite(limit) && limit >= 0);
    limit = fmin(limit ? limit : available / 1048576.0, available / 1048576.0);
    char value[64];
    snprintf(value, sizeof(value), "%.9g", limit);
    puf_ini_put(ini, "sweep.memory_limit_mb", value);
    printf("Goofspiel sweep memory estimate limit: %.0f MiB\n", limit);
#endif
}
