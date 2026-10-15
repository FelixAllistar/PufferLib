#pragma once

// Shared by the native command and standalone evaluators. No CUDA needed.
#include "../../src/ini.h"
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    time_t seconds;
    long nanos;
    char path[4096];
} GSLatestCheckpoint;

static inline void gs_find_latest_checkpoint(const char* directory,
        GSLatestCheckpoint* latest) {
    DIR* dir = opendir(directory);
    if (!dir) return;
    struct dirent* entry;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char path[4096];
        int length = snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
        if (length < 0 || (size_t)length >= sizeof(path)) continue;
        struct stat st;
        // Do not follow directory symlinks or select incomplete empty files.
        if (lstat(path, &st)) continue;
        if (S_ISDIR(st.st_mode)) {
            gs_find_latest_checkpoint(path, latest);
        } else if (S_ISREG(st.st_mode) && st.st_size > 0 && length >= 4
                && !strcmp(path + length - 4, ".bin")) {
#ifdef __APPLE__
            struct timespec modified = st.st_mtimespec;
#else
            struct timespec modified = st.st_mtim;
#endif
            if (!latest->path[0] || modified.tv_sec > latest->seconds
                    || (modified.tv_sec == latest->seconds && modified.tv_nsec > latest->nanos)
                    || (modified.tv_sec == latest->seconds && modified.tv_nsec == latest->nanos
                        && strcmp(path, latest->path) > 0)) {
                latest->seconds = modified.tv_sec;
                latest->nanos = modified.tv_nsec;
                snprintf(latest->path, sizeof(latest->path), "%s", path);
            }
        }
    }
    closedir(dir);
}

static inline const char* gs_resolve_checkpoint(Ini* ini, const char* requested,
        char* output, size_t capacity) {
    if (requested && requested[0] && strcmp(requested, "None")
            && strcmp(requested, "latest")) return requested;
    char root[4096];
    int length = snprintf(root, sizeof(root), "%s/goofspiel",
        puf_ini_get_str(ini, "base", "checkpoint_dir"));
    if (length < 0 || (size_t)length >= sizeof(root)) {
        fprintf(stderr, "Goofspiel checkpoint directory is too long\n");
        return NULL;
    }
    GSLatestCheckpoint latest = {0};
    gs_find_latest_checkpoint(root, &latest);
    if (!latest.path[0]) {
        fprintf(stderr, "No Goofspiel .bin checkpoints found in %s\n", root);
        return NULL;
    }
    if (strlen(latest.path) >= capacity) return NULL;
    snprintf(output, capacity, "%s", latest.path);
    return output;
}

static inline void gs_save_checkpoint_settings(const char* checkpoint, Ini* ini) {
    char target[4096], temporary[4096];
    int n = snprintf(target, sizeof(target), "%s.ini", checkpoint);
    int m = snprintf(temporary, sizeof(temporary), "%s.ini.XXXXXX", checkpoint);
    if (n < 0 || (size_t)n >= sizeof(target) || m < 0 || (size_t)m >= sizeof(temporary)) {
        fprintf(stderr, "Goofspiel checkpoint settings path is too long\n");
        exit(1);
    }
    int fd = mkstemp(temporary);
    FILE* file = fd < 0 ? NULL : fdopen(fd, "w");
    if (!file) { perror("Goofspiel checkpoint settings"); exit(1); }
    puf_ini_write(file, ini);
    int error = ferror(file);
    if (fclose(file) || error || rename(temporary, target)) {
        perror("Goofspiel checkpoint settings");
        unlink(temporary);
        exit(1);
    }
}

// New snapshots have a sidecar, including while training is still running.
// Older runs fall back to their completed run log. Only game/model settings
// are restored: never log destinations, training budgets, or opponent paths.
static inline int gs_load_checkpoint_settings(Ini* ini, const char* checkpoint,
        int argc, char** argv) {
    if (!strcmp(checkpoint, "uniform")) return 0;
    char settings[4096];
    int n = snprintf(settings, sizeof(settings), "%s.ini", checkpoint);
    if (n < 0 || (size_t)n >= sizeof(settings)) return 0;
    FILE* file = fopen(settings, "r");
    if (!file) {
        const char* last = strrchr(checkpoint, '/');
        if (!last) return 0;
        const char* run = last;
        while (run > checkpoint && run[-1] != '/') --run;
        n = snprintf(settings, sizeof(settings), "%s/goofspiel/%.*s.ini",
            puf_ini_get_str(ini, "base", "log_dir"), (int)(last - run), run);
        if (n < 0 || (size_t)n >= sizeof(settings)) return 0;
        file = fopen(settings, "r");
    }
    if (!file) return 0;
    fclose(file);
    Ini recorded = {0};
    puf_ini_load_file(&recorded, settings);
    const char* sections[] = {"policy", "env"};
    for (int s = 0; s < 2; ++s) {
        Dict* source = puf_ini_section(&recorded, sections[s], 1);
        Dict* target = puf_ini_section(ini, sections[s], 0);
        for (int k = 0; k < source->size; ++k) {
            DictItem* item = source->items + k;
            if (!dict_find(target, item->key)) continue;
            char key[2 * PUF_DICT_MAX_KEY], number[64];
            snprintf(key, sizeof(key), "%s.%s", sections[s], item->key);
            snprintf(number, sizeof(number), "%.17g", item->value);
            puf_ini_put(ini, key, item->str ? item->str : number);
        }
    }
    puf_ini_free(&recorded);
    // Explicit CLI choices still win, including deliberate negative tests.
    for (int i = 0; i < argc; ++i) puf_ini_apply_arg(ini, "base", argv[i], i);
    printf("Checkpoint settings: %s\n", settings);
    return 1;
}
