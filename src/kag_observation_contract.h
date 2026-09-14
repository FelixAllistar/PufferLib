#pragma once

#include "ini.h"
#include <unistd.h>

/* Host-side policy/observation association for end-of-training two-seat eval.
 * This helper is independent of CUDA so its selection rules can be unit tested.
 * Non-Kaggriculture environments are unchanged. */
typedef struct {
    int enabled;
    int learner;
    int frozen;
    int executor;
    int frozen_executor;
    int hidden;
    int layers;
    int alignment;
} KagObservationContract;

static inline KagObservationContract kag_observation_contract(Ini* ini) {
    KagObservationContract result = {0, 2, -1, 0, -1, 0, 0, 8};
#ifdef PRECISION_FLOAT
    result.alignment = 4;
#endif
    if (strcmp(puf_ini_get_str(ini, "base", "env_name"), "kaggriculture") != 0)
        return result;
    result.enabled = 1;
    result.hidden = puf_ini_get_int(ini, "policy", "hidden_size");
    result.layers = puf_ini_get_int(ini, "policy", "num_layers");
    if (result.hidden < 8 || result.hidden % 8 || result.layers < 1) {
        fprintf(stderr, "Entity policy requires hidden_size divisible by 8 and num_layers >= 1\n");
        exit(1);
    }
    Dict* env = puf_ini_section(ini, "env", 0);
    result.learner = dict_find(env, "observation_version")
        ? (int)dict_get(env, "observation_version") : 2;
    result.frozen = dict_find(env, "frozen_observation_version")
        ? (int)dict_get(env, "frozen_observation_version") : -1;
    result.executor = dict_find(env, "macro_executor_version")
        ? (int)dict_get(env, "macro_executor_version") : 0;
    result.frozen_executor = dict_find(env, "frozen_macro_executor_version")
        ? (int)dict_get(env, "frozen_macro_executor_version") : -1;
    if (result.learner != 2 || (result.frozen != -1 && result.frozen != 2)
            || result.executor != 0 || (result.frozen_executor != -1 && result.frozen_executor != 0)) {
        fprintf(stderr, "Kaggriculture requires fresh entity v2 policies in every bank (obs=2, executor=0)\n");
        exit(1);
    }
    return result;
}

static inline int kag_observation_mixed(KagObservationContract contract) {
    return contract.enabled && ((contract.frozen >= 0 && contract.frozen != contract.learner)
        || (contract.frozen_executor >= 0 && contract.frozen_executor != contract.executor));
}

static inline int kag_observation_pool_compatible(KagObservationContract contract,
        float external_probability, int external_size) {
    return !kag_observation_mixed(contract)
        || (external_probability == 1.0f && external_size > 0);
}

static inline void kag_observation_pair(Ini* ini, KagObservationContract contract,
        int external_opponent, int reverse) {
    if (!contract.enabled) return;
    int opponent_version = external_opponent && contract.frozen >= 0
        ? contract.frozen : contract.learner;
    Dict* env = puf_ini_section(ini, "env", 0);
    dict_set(env, "observation_version", reverse ? opponent_version : contract.learner);
    dict_set(env, "frozen_observation_version", reverse ? contract.learner : opponent_version);
    int opponent_executor = external_opponent && contract.frozen_executor >= 0
        ? contract.frozen_executor : contract.executor;
    dict_set(env, "macro_executor_version", reverse ? opponent_executor : contract.executor);
    dict_set(env, "frozen_macro_executor_version", reverse ? contract.executor : opponent_executor);
}

static inline void kag_observation_restore(Ini* ini, KagObservationContract contract) {
    if (!contract.enabled) return;
    Dict* env = puf_ini_section(ini, "env", 0);
    dict_set(env, "observation_version", contract.learner);
    dict_set(env, "frozen_observation_version", contract.frozen);
    dict_set(env, "macro_executor_version", contract.executor);
    dict_set(env, "frozen_macro_executor_version", contract.frozen_executor);
}

/* Save semantics alongside every new weight file, even if the run ID is later
 * reused or its INI is edited. Untagged and old policies are rejected. */
static inline void kag_observation_save_contract(const char* checkpoint, KagObservationContract c) {
    if (!c.enabled) return;
    const char* suffixes[] = {".obs_version", ".executor_version", ".policy_version",
        ".hidden_size", ".num_layers", ".param_alignment"};
    int values[] = {c.learner, c.executor, 2, c.hidden, c.layers, c.alignment};
    for (int i = 0; i < 6; i++) {
        char path[8192], temporary[8256];
        snprintf(path, sizeof(path), "%s%s", checkpoint, suffixes[i]);
        snprintf(temporary, sizeof(temporary), "%s.tmp.%d", path, (int)getpid());
        FILE* out = fopen(temporary, "w");
        if (!out || fprintf(out, "%d\n", values[i]) < 0 || fclose(out) != 0
                || rename(temporary, path) != 0) {
            fprintf(stderr, "Cannot save policy contract: %s\n", path);
            exit(1);
        }
    }
}

static inline void kag_observation_save(const char* checkpoint, Ini* ini) {
    kag_observation_save_contract(checkpoint, kag_observation_contract(ini));
}

static inline int kag_checkpoint_integer(const char* checkpoint, const char* suffix) {
    char path[8192], extra;
    snprintf(path, sizeof(path), "%s%s", checkpoint, suffix);
    FILE* file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "Missing fresh-policy metadata: %s. Start a new run; old weights are unsupported.\n", path);
        exit(1);
    }
    int value = -1;
    int fields = fscanf(file, "%d %c", &value, &extra);
    fclose(file);
    if (fields != 1) {
        fprintf(stderr, "Malformed fresh-policy metadata: %s\n", path); exit(1);
    }
    return value;
}

static inline void kag_executor_check_load(const char* checkpoint,
        KagObservationContract contract, int frozen) {
    (void)frozen;
    if (!contract.enabled || !checkpoint) return;
    const char* suffixes[] = {".policy_version", ".obs_version", ".executor_version",
        ".hidden_size", ".num_layers", ".param_alignment"};
    const int expected[] = {2, 2, 0, contract.hidden, contract.layers, contract.alignment};
    for (int i = 0; i < 6; i++) {
        int actual = kag_checkpoint_integer(checkpoint, suffixes[i]);
        if (actual != expected[i]) {
            fprintf(stderr, "Policy contract mismatch: %s%s must contain %d (got %d).\n",
                checkpoint, suffixes[i], expected[i], actual);
            exit(1);
        }
    }
}
