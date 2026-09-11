#pragma once
#include "puffercpu.h"
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
typedef struct {
    PufferNet* net;
    Weights* weights;
    Ini ini;
    char path[4096];
    uint64_t rng;
    char team[256];
} PKPolicy;
static int pk_file(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}
static void pk_latest(const char* root, char* out, struct timespec* newest) {
    DIR* dir = opendir(root);
    if (!dir) return;
    struct dirent* entry;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        char path[4096];
        if (snprintf(path, sizeof(path), "%s/%s", root, entry->d_name) >= (int)sizeof(path)) continue;
        struct stat st;
        if (lstat(path, &st)) continue;
        if (S_ISDIR(st.st_mode)) pk_latest(path, out, newest);
        size_t n = strlen(path);
        if (S_ISREG(st.st_mode) && n > 4 && !strcmp(path + n - 4, ".bin") &&
                (st.st_mtim.tv_sec > newest->tv_sec ||
                 (st.st_mtim.tv_sec == newest->tv_sec && st.st_mtim.tv_nsec >= newest->tv_nsec))) {
            *newest = st.st_mtim;
            snprintf(out, 4096, "%s", path);
        }
    }
    closedir(dir);
}
static void pk_load_policy(PKPolicy* policy, const char* arg, int emag,
        int override_count, char** overrides) {
    puf_ini_load_env(&policy->ini, "pokemon", 0, NULL);
    if (!strcmp(arg, "random")) {
        snprintf(policy->path, sizeof(policy->path), "random");
    } else {
        if (!strcmp(arg, "latest")) {
            char root[4096];
            snprintf(root, sizeof(root), "%s/pokemon",
                puf_ini_get_str(&policy->ini, "base", "checkpoint_dir"));
            struct timespec newest = {0};
            pk_latest(root, policy->path, &newest);
            if (!*policy->path) { fprintf(stderr, "No Pokemon checkpoints under %s\n", root); exit(1); }
        } else snprintf(policy->path, sizeof(policy->path), "%s", arg);
        if (emag) {
            size_t n = strlen(policy->path);
            if (n < 5 || strcmp(policy->path + n - 5, ".emag")) {
                if (n + 5 >= sizeof(policy->path)) exit(1);
                strcat(policy->path, ".emag");
            }
        }
        char directory[4096], config[4096];
        snprintf(directory, sizeof(directory), "%s", policy->path);
        char* slash = strrchr(directory, '/');
        if (!slash) { fprintf(stderr, "Use a checkpoint path with a parent directory/config.ini\n"); exit(1); }
        *slash = 0;
        snprintf(config, sizeof(config), "%s/config.ini", directory);
        if (!pk_file(config)) {
            const char* run = strrchr(directory, '/');
            run = run ? run + 1 : directory;
            snprintf(config, sizeof(config), "%s/pokemon/%s.ini",
                puf_ini_get_str(&policy->ini, "base", "log_dir"), run);
        }
        if (!pk_file(config)) { fprintf(stderr, "Missing saved run config for %s\n", policy->path); exit(1); }
        Ini saved = {0};
        puf_ini_load_file(&saved, config);
        Dict* rules = puf_ini_section(&saved, "env", 0);
        DictItem* abi = dict_find(rules, "abi_version");
        DictItem* catalog = dict_find(rules, "catalog_sha");
        if (!abi || abi->value != PK_ABI_VERSION || !catalog || !catalog->str || strcmp(catalog->str, PK_CATALOG_SHA)) {
            fprintf(stderr, "Incompatible Pokemon checkpoint catalog/ABI: start a new run with the sourced catalog (ABI 2)\n"); exit(1);
        }
        puf_ini_free(&saved);
        puf_ini_load_file(&policy->ini, config);
    }
    for (int i = 0; i < override_count; i++) puf_ini_apply_arg(&policy->ini, "base", overrides[i], i);
    DictItem* team = dict_find(puf_ini_section(&policy->ini, "env", 0), "learner_team");
    snprintf(policy->team, sizeof(policy->team), "%s", team && team->str ? team->str : "None");
    if (!strcmp(arg, "random")) return;
    int h = puf_ini_get_int(&policy->ini, "policy", "hidden_size");
    int layers = puf_ini_get_int(&policy->ini, "policy", "num_layers");
    if (h < 1 || h > 4096 || layers < 1 || layers > 32) { fprintf(stderr, "Invalid policy architecture\n"); exit(1); }
    size_t encoder = (size_t)h * PK_OBS, decoder = (size_t)(PK_ACTIONS + 1) * h;
    size_t recurrent = (size_t)3 * h * h;
    size_t expected = ((encoder + 7) & ~(size_t)7) + ((decoder + 7) & ~(size_t)7)
        + layers * ((recurrent + 7) & ~(size_t)7);
    struct stat st;
    if (stat(policy->path, &st) || st.st_size != (off_t)(expected * sizeof(float))) {
        fprintf(stderr, "Missing/incompatible checkpoint %s: expected %zu bytes (H=%d L=%d)\n",
            policy->path, expected * sizeof(float), h, layers); exit(1);
    }
    policy->weights = load_weights(policy->path);
    if (!policy->weights) exit(1);
    for (size_t i = 0; i < expected; i++) {
        if (!isfinite(policy->weights->data[i])) { fprintf(stderr, "Non-finite checkpoint weights\n"); exit(1); }
    }
    int sizes[] = {PK_ACTIONS};
    policy->net = make_puffernet(policy->weights, 1, PK_OBS, h, layers, sizes, 1);
    fprintf(stderr, "Loaded %s (hidden=%d layers=%d)\n", policy->path, h, layers);
}
static void pk_reset_policy(PKPolicy* policy, uint64_t seed) {
    policy->rng = seed;
    if (!policy->net) return;
    MinGRU* gru = policy->net->mingru;
    memset(gru->state, 0, (size_t)gru->hidden_size * gru->num_layers * sizeof(float));
    memset(gru->output, 0, (size_t)gru->hidden_size * sizeof(float));
}
static int pk_policy_action(PKPolicy* policy, const uint8_t* obs, const uint8_t* mask, int deterministic) {
    if (!policy->net) return pk_random_action(mask, &policy->rng);
    PufferNet* net = policy->net;
    // Native Pokemon inference casts bytes directly (no OBS_U8_NORMALIZED).
    // Keep the checkpoint's training scale: normalizing here changes the policy.
    for (int i = 0; i < PK_OBS; i++) net->obs[i] = (float)obs[i];
    linear(net->encoder, net->obs);
    mingru(net->mingru, net->encoder->output);
    linear(net->decoder, net->mingru->output);
    float max = -INFINITY;
    int best = -1;
    for (int i = 0; i < PK_ACTIONS; i++) if (mask[i]) {
        float logit = net->decoder->output[i];
        if (!isfinite(logit)) { fprintf(stderr, "Non-finite policy logits\n"); exit(1); }
        if (logit > max) { max = logit; best = i; }
    }
    assert(best >= 0);
    if (deterministic) return best;
    float weights[PK_ACTIONS] = {0}, sum = 0;
    for (int i = 0; i < PK_ACTIONS; i++) if (mask[i]) {
        weights[i] = expf(net->decoder->output[i] - max); sum += weights[i];
    }
    double sample = (pk_random(&policy->rng) >> 11) * 0x1.0p-53 * sum;
    for (int i = 0; i < PK_ACTIONS; i++) if (mask[i]) {
        sample -= weights[i]; if (sample < 0) return i;
    }
    return best;
}
static void pk_free_policy(PKPolicy* policy) {
    if (policy->net) free_puffernet(policy->net);
    free(policy->weights);
    puf_ini_free(&policy->ini);
}
