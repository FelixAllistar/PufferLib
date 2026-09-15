#include "../src/puf_worker_sync.h"
#include "../src/puf_training_stop.h"
#include <thread>
#include <vector>
#include <sys/wait.h>
#include <stdio.h>

static void handoffs(int blocking, int workers) {
    PufWorkerSync sync;
    puf_worker_sync_init(&sync, blocking);
    int shutdown = 0;
    std::vector<int> states(workers, 0), inputs(workers), outputs(workers);
    std::vector<std::thread> threads;
    for (int i = 0; i < workers; ++i) threads.emplace_back([&, i] {
        puf_worker_publish(&sync, &states[i], 1);
        for (;;) {
            puf_worker_wait(&sync, &states[i], 2, &shutdown);
            if (__atomic_load_n(&shutdown, __ATOMIC_SEQ_CST)) break;
            outputs[i] = inputs[i] * 7 + i;
            puf_worker_publish(&sync, &states[i], 1);
        }
    });
    // Spin mode is intentionally CPU-hungry; keep the legacy comparison short
    // so this test is usable alongside an active four-core training job.
    for (int round = 0; round < (blocking ? 2000 : 16); ++round) {
        for (int i = 0; i < workers; ++i) {
            puf_worker_wait(&sync, &states[i], 1, &shutdown);
            inputs[i] = round;
            puf_worker_publish(&sync, &states[i], 2);
        }
        for (int i = 0; i < workers; ++i) {
            puf_worker_wait(&sync, &states[i], 1, &shutdown);
            assert(outputs[i] == round * 7 + i);
        }
    }
    // Shutdown must wake idle workers (including workers not yet asleep).
    puf_worker_publish(&sync, &shutdown, 1);
    for (auto& t : threads) t.join();
    puf_worker_sync_destroy(&sync);
}

int main() {
    alarm(30);
    for (int blocking : {0, 1}) for (int workers : {1, 4})
        handoffs(blocking, workers);
    for (int sig : {SIGINT, SIGTERM}) {
        pid_t child = fork();
        assert(child >= 0);
        if (!child) {
            PufTrainingStop stop(true);
            assert(!stop.requested());
            raise(sig);
            assert(stop.requested());
            raise(sig);
            _exit(1);
        }
        int status = 0;
        assert(waitpid(child, &status, 0) == child);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 128 + sig);
    }
    puts("worker handoffs, visibility, idle shutdown, and stop signals: PASS");
}
