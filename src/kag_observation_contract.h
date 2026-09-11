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
} KagObservationContract;

static inline KagObservationContract kag_observation_contract(Ini* ini) {
    KagObservationContract result = {0, 0, -1, 0, -1};
    if (strcmp(puf_ini_get_str(ini, "base", "env_name"), "kaggriculture") != 0)
        return result;
    result.enabled = 1;
    Dict* env = puf_ini_section(ini, "env", 0);
    result.learner = dict_find(env, "observation_version")
        ? (int)dict_get(env, "observation_version") : 0;
    result.frozen = dict_find(env, "frozen_observation_version")
        ? (int)dict_get(env, "frozen_observation_version") : -1;
    result.executor = dict_find(env, "macro_executor_version")
        ? (int)dict_get(env, "macro_executor_version") : 0;
    result.frozen_executor = dict_find(env, "frozen_macro_executor_version")
        ? (int)dict_get(env, "frozen_macro_executor_version") : -1;
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
 * reused or its INI is edited. Historical untagged files still default to v0. */
static inline void kag_observation_save(const char* checkpoint, Ini* ini) {
    KagObservationContract c = kag_observation_contract(ini);
    if (!c.enabled) return;
    const char* suffixes[] = {".obs_version", ".executor_version"};
    int values[] = {c.learner, c.executor};
    for (int i = 0; i < 2; i++) {
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

static inline void kag_executor_check_load(const char* checkpoint,
        KagObservationContract contract, int frozen) {
    if (!contract.enabled || !checkpoint) return;
    int expected = frozen && contract.frozen_executor >= 0
        ? contract.frozen_executor : contract.executor;
    char path[8192];
    snprintf(path, sizeof(path), "%s.executor_version", checkpoint);
    FILE* file = fopen(path, "r");
    int actual = 0; /* Historical models predate versioned executors. */
    if (file) {
        char extra;
        int fields = fscanf(file, "%d %c", &actual, &extra);
        fclose(file);
        if (fields != 1 || actual < 0 || actual > 1) {
            fprintf(stderr, "Invalid executor metadata: %s\n", path);
            exit(1);
        }
    }
    if (actual != expected) {
        fprintf(stderr, "Executor mismatch for %s: checkpoint=%d runtime=%d. "
            "Use the checkpoint's executor or start a fresh model; do not relabel old weights.\n",
            checkpoint, actual, expected);
        exit(1);
    }
}
