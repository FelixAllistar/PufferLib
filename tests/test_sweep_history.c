#define _POSIX_C_SOURCE 200809L
#include "../src/sweep_history.h"
#include <assert.h>
#include <sys/wait.h>

typedef struct { int rows; float best; } State;
static void replay(void* ptr, const float* values, float score, float cost, int failed) {
    State* state = ptr;
    assert(values[0] >= 32 && values[0] <= 64 && cost > 0);
    state->rows++;
    if (!failed && score > state->best) state->best = score;
}

static void reject(const char* path, PufSweepHistoryParam* defs) {
    pid_t pid = fork(); assert(pid >= 0);
    if (!pid) {
        int count = 0, next = 0; State state = {0};
        FILE* fp = puf_sweep_history_open(path, defs, 2, replay, &state, &count, &next);
        fclose(fp);
        exit(0);
    }
    int status; assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 1);
}

int main(void) {
    char directory[] = "/tmp/puffer-history-XXXXXX";
    assert(mkdtemp(directory));
    char path[256]; snprintf(path, sizeof(path), "%s/history", directory);
    PufSweepHistoryParam defs[2] = {
        {"train.horizon", "uniform_pow2", 32, 64},
        {"train.learning_rate", "log_normal", .0001f, .003f},
    };
    int count = 0, next = 0; State state = {0};
    FILE* fp = puf_sweep_history_open(path, defs, 2, replay, &state, &count, &next);
    assert(count == 0 && next == 0);
    reject(path, defs); // A second writer cannot own the same history.
    float values[] = {64, .001f};
    puf_sweep_history_append(fp, 0, values, 2, .75f, 20, 0);
    puf_sweep_history_append(fp, 3, values, 2, NAN, 40, 1);
    fclose(fp);
    fp = puf_sweep_history_open(path, defs, 2, replay, &state, &count, &next);
    assert(count == 1 && next == 4 && state.rows == 2 && state.best == .75f);
    puf_sweep_history_append(fp, 4, values, 2, .9f, 10, 0);
    fclose(fp);
    count = next = 0; state.rows = 0;
    fp = puf_sweep_history_open(path, defs, 2, replay, &state, &count, &next);
    assert(count == 2 && next == 5 && state.rows == 3 && state.best == .9f);
    fclose(fp);
    defs[0].max = 128;
    reject(path, defs); // Changed ranges cannot silently corrupt normalization.
    defs[0].max = 64;
    fp = fopen(path, "a"); assert(fp);
    fprintf(fp, "5 .8 10 0 64"); fclose(fp);
    reject(path, defs); // Partial writes cannot be mistaken for completed trials.
    assert(unlink(path) == 0 && rmdir(directory) == 0);
    puts("Sweep history: replay, counting, append, locking, range and truncation checks passed");
    return 0;
}
