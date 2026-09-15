#pragma once

#include "ini.h"
#include <unistd.h>
#include <math.h>
#include "../ocean/kaggriculture/entity_contract.h"

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
    int mode;
    int frozen_mode;
    int interval;
    int frozen_interval;
    int score_features;
    int frozen_score_features;
} KagObservationContract;

static inline int kag_controller_valid(int mode, int executor) {
    return mode >= 0 && mode <= 3 && executor >= 0 && executor <= 1
        && (!executor || mode == 1 || mode == 2);
}

static inline int kag_contract_int(Dict* env, const char* key, int fallback,
        int low, int high) {
    double value = dict_find(env, key) ? dict_get(env, key) : fallback;
    if (!isfinite(value) || value < low || value > high || value != (int)value) {
        fprintf(stderr, "Invalid env.%s=%g: expected an integer from %d through %d.\n",
            key, value, low, high);
        exit(1);
    }
    return (int)value;
}

static inline void kag_check_controller(int mode, int executor, int frozen) {
    if (kag_controller_valid(mode, executor)) return;
    fprintf(stderr, "Incompatible Kaggriculture %scontroller: macro_mode=%d, executor=%d. "
        "Modes 0 and 3 require executor 0; executor 1 only works with modes 1/2. "
        "Set env.%smacro_executor_version=0%s.\n",
        frozen ? "frozen " : "learner ", mode, executor, frozen ? "frozen_" : "",
        frozen ? " or set BOTH frozen_macro_mode and frozen_macro_executor_version to -1 to inherit" : "");
    exit(1);
}

static inline KagObservationContract kag_observation_contract(Ini* ini) {
    KagObservationContract result = {0, KAG_OBSERVATION_ENTITIES, -1, 0, -1, 0, 0, 8,
        3, -1, 1, -1, 0, -1};
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
    result.learner = kag_contract_int(env, "observation_version", KAG_OBSERVATION_ENTITIES, 0, 3);
    result.frozen = kag_contract_int(env, "frozen_observation_version", -1, -1, 3);
    if (result.learner != KAG_OBSERVATION_ENTITIES
            || (result.frozen != -1 && result.frozen != KAG_OBSERVATION_ENTITIES)) {
        fprintf(stderr, "This build requires env.observation_version=3 and frozen_observation_version=3 (or -1). Old v0/v1/v2 observations are unsupported.\n");
        exit(1);
    }
    result.mode = kag_contract_int(env, "macro_mode", 3, 0, 3);
    result.executor = kag_contract_int(env, "macro_executor_version", 0, 0, 1);
    result.frozen_mode = kag_contract_int(env, "frozen_macro_mode", -1, -1, 3);
    result.frozen_executor = kag_contract_int(env, "frozen_macro_executor_version", -1, -1, 1);
    result.interval = kag_contract_int(env, "macro_decision_interval", 1, 1, 1000000);
    result.frozen_interval = kag_contract_int(env, "frozen_macro_decision_interval", -1, -1, 1000000);
    if (result.frozen_interval == 0) {
        fprintf(stderr, "env.frozen_macro_decision_interval=0 is invalid; use -1 to inherit or a positive interval.\n");
        exit(1);
    }
    result.score_features = kag_contract_int(env, "macro_score_features", 0, 0, 1);
    result.frozen_score_features = kag_contract_int(env, "frozen_macro_score_features", -1, -1, 1);
    int fm = result.frozen_mode < 0 ? result.mode : result.frozen_mode;
    int fe = result.frozen_executor < 0 ? result.executor : result.frozen_executor;
    kag_check_controller(result.mode, result.executor, 0);
    kag_check_controller(fm, fe, 1);
    if (result.mode != 1) result.interval = 1;
    if (fm != 1 && result.frozen_interval >= 0) result.frozen_interval = 1;
    return result;
}

static inline int kag_observation_mixed(KagObservationContract contract) {
    return contract.enabled && ((contract.frozen >= 0 && contract.frozen != contract.learner)
        || (contract.frozen_executor >= 0 && contract.frozen_executor != contract.executor)
        || (contract.frozen_mode >= 0 && contract.frozen_mode != contract.mode)
        || (contract.frozen_interval >= 0 && contract.frozen_interval != contract.interval)
        || (contract.frozen_score_features >= 0 && contract.frozen_score_features != contract.score_features));
}

static inline int kag_observation_pool_compatible(KagObservationContract contract,
        float external_probability, int external_size) {
    /* v3 binds each populated bank from its own checkpoint metadata. Rolling
     * snapshots and heterogeneous external controllers can share the pool. */
    (void)contract; (void)external_probability; (void)external_size;
    return 1;
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
    int opponent_mode = external_opponent && contract.frozen_mode >= 0 ? contract.frozen_mode : contract.mode;
    int opponent_interval = external_opponent && contract.frozen_interval >= 0 ? contract.frozen_interval : contract.interval;
    int opponent_features = external_opponent && contract.frozen_score_features >= 0 ? contract.frozen_score_features : contract.score_features;
    dict_set(env, "macro_mode", reverse ? opponent_mode : contract.mode);
    dict_set(env, "frozen_macro_mode", reverse ? contract.mode : opponent_mode);
    dict_set(env, "macro_decision_interval", reverse ? opponent_interval : contract.interval);
    dict_set(env, "frozen_macro_decision_interval", reverse ? contract.interval : opponent_interval);
    dict_set(env, "macro_score_features", reverse ? opponent_features : contract.score_features);
    dict_set(env, "frozen_macro_score_features", reverse ? contract.score_features : opponent_features);
}

static inline void kag_observation_restore(Ini* ini, KagObservationContract contract) {
    if (!contract.enabled) return;
    Dict* env = puf_ini_section(ini, "env", 0);
    dict_set(env, "observation_version", contract.learner);
    dict_set(env, "frozen_observation_version", contract.frozen);
    dict_set(env, "macro_executor_version", contract.executor);
    dict_set(env, "frozen_macro_executor_version", contract.frozen_executor);
    dict_set(env, "macro_mode", contract.mode);
    dict_set(env, "frozen_macro_mode", contract.frozen_mode);
    dict_set(env, "macro_decision_interval", contract.interval);
    dict_set(env, "frozen_macro_decision_interval", contract.frozen_interval);
    dict_set(env, "macro_score_features", contract.score_features);
    dict_set(env, "frozen_macro_score_features", contract.frozen_score_features);
}

/* Save semantics alongside every new weight file, even if the run ID is later
 * reused or its INI is edited. Untagged and old policies are rejected. */
static inline void kag_observation_save_contract(const char* checkpoint, KagObservationContract c) {
    if (!c.enabled) return;
    const char* suffixes[] = {".obs_version", ".executor_version", ".policy_version",
        ".hidden_size", ".num_layers", ".param_alignment", ".macro_mode",
        ".macro_decision_interval", ".macro_score_features"};
    int values[] = {c.learner, c.executor, KAG_POLICY_VERSION, c.hidden, c.layers, c.alignment,
        c.mode, c.interval, c.score_features};
    for (int i = 0; i < 9; i++) {
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
    if (!contract.enabled || !checkpoint) return;
    int executor = frozen && contract.frozen_executor >= 0 ? contract.frozen_executor : contract.executor;
    int mode = frozen && contract.frozen_mode >= 0 ? contract.frozen_mode : contract.mode;
    int interval = frozen && contract.frozen_interval >= 0 ? contract.frozen_interval : contract.interval;
    if (mode != 1) interval = 1;
    int features = frozen && contract.frozen_score_features >= 0 ? contract.frozen_score_features : contract.score_features;
    const char* suffixes[] = {".policy_version", ".obs_version", ".executor_version",
        ".hidden_size", ".num_layers", ".param_alignment", ".macro_mode",
        ".macro_decision_interval", ".macro_score_features"};
    const int expected[] = {KAG_POLICY_VERSION, KAG_OBSERVATION_ENTITIES, executor,
        contract.hidden, contract.layers, contract.alignment, mode, interval, features};
    for (int i = 0; i < 9; i++) {
        int actual = kag_checkpoint_integer(checkpoint, suffixes[i]);
        if (actual != expected[i]) {
            fprintf(stderr, "Policy contract mismatch: %s%s must contain %d (got %d).\n",
                checkpoint, suffixes[i], expected[i], actual);
            fprintf(stderr, "Loading weights does not change their controller. Match the checkpoint's mode, executor, score_features and H/L, or use base.load_model_path=None for a fresh policy. Use an explicit checkpoint path when switching experiments.\n");
            exit(1);
        }
    }
}

/* Standalone viewer/league entry point: derive semantic settings from explicit
 * metadata, not today's INI or the size of a weight file. */
static inline KagObservationContract kag_checkpoint_contract(const char* path) {
    KagObservationContract c = {1, KAG_OBSERVATION_ENTITIES, -1, 0, -1, 0, 0, 8,
        3, -1, 1, -1, 0, -1};
    c.hidden = kag_checkpoint_integer(path, ".hidden_size");
    c.layers = kag_checkpoint_integer(path, ".num_layers");
    c.alignment = kag_checkpoint_integer(path, ".param_alignment");
    c.mode = kag_checkpoint_integer(path, ".macro_mode");
    c.executor = kag_checkpoint_integer(path, ".executor_version");
    c.interval = kag_checkpoint_integer(path, ".macro_decision_interval");
    c.score_features = kag_checkpoint_integer(path, ".macro_score_features");
    if (c.hidden < 8 || c.hidden % 8 || c.layers < 1
            || (c.alignment != 4 && c.alignment != 8)
            || !kag_controller_valid(c.mode, c.executor) || c.interval < 1
            || (c.mode != 1 && c.interval != 1) || c.score_features < 0 || c.score_features > 1) {
        fprintf(stderr, "Invalid controller metadata: %s\n", path); exit(1);
    }
    kag_executor_check_load(path, c, 0);
    return c;
}
