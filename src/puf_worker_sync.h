#ifndef PUF_WORKER_SYNC_H
#define PUF_WORKER_SYNC_H

#include <pthread.h>
#include <assert.h>

// A horizon-sized handoff, not a per-environment/per-frame barrier. Keep the
// old spin path selectable for matched benchmarks and latency-sensitive envs.
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    int blocking;
} PufWorkerSync;

static inline void puf_worker_sync_init(PufWorkerSync* s, int blocking) {
    s->blocking = blocking;
    int rc = pthread_mutex_init(&s->mutex, NULL);
    assert(rc == 0);
    rc = pthread_cond_init(&s->changed, NULL);
    assert(rc == 0);
}

static inline void puf_worker_publish(PufWorkerSync* s, int* state, int value) {
    if (s->blocking) pthread_mutex_lock(&s->mutex);
    __atomic_store_n(state, value, __ATOMIC_SEQ_CST);
    if (s->blocking) {
        pthread_cond_broadcast(&s->changed);
        pthread_mutex_unlock(&s->mutex);
    }
}

static inline void puf_worker_wait(PufWorkerSync* s, const int* state,
        int wanted, const int* shutdown) {
    if (s->blocking) pthread_mutex_lock(&s->mutex);
    while (__atomic_load_n(state, __ATOMIC_SEQ_CST) != wanted &&
            !__atomic_load_n(shutdown, __ATOMIC_SEQ_CST)) {
        if (s->blocking) pthread_cond_wait(&s->changed, &s->mutex);
    }
    if (s->blocking) pthread_mutex_unlock(&s->mutex);
}

static inline void puf_worker_sync_destroy(PufWorkerSync* s) {
    pthread_cond_destroy(&s->changed);
    pthread_mutex_destroy(&s->mutex);
}

#endif
