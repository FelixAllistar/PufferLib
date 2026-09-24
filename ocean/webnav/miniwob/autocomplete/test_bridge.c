/* Standalone CPU harness for the autocomplete wire. The shared webnav bridge
 * uses the same Bend runtime; this copy keeps the lane independently runnable
 * while integration selects tag 10 in its dispatch. */
#define main autocomplete_bend_unused_cli
#include "../../../../build/webnav/autocomplete/autocomplete_generated.c"
#undef main

#include "../../bridge.h"
#include "validation.h"

#include <pthread.h>
#include <stdlib.h>

static pthread_mutex_t autocomplete_gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t autocomplete_once = PTHREAD_ONCE_INIT;
static Corpus autocomplete_heap;

static void autocomplete_start(void) {
    autocomplete_heap = corpus_setup(false, 1, 0);
    io_stk = pool_stack();
}

void autocomplete_batch(uint32_t words[WEBNAV_BATCH * WEBNAV_WORDS]) {
    for (unsigned lane = 0; lane < WEBNAV_BATCH; ++lane)
        if (!webnav_autocomplete_valid(words + lane * WEBNAV_WORDS)) abort();

    pthread_mutex_lock(&autocomplete_gate);
    pthread_once(&autocomplete_once, autocomplete_start);
    webnav_words = words;
    Env e = {autocomplete_heap, ALC[0]};
    Term m = corpus_eval(autocomplete_heap, term_tsk(MAIN_FID,
        task_node(e, MAIN_FID, TERM_HOLE, 0, 0)));
    io_spawn(m);
    while (io_runs.head != NULL) {
        if (io_step(e, io_pop(&io_runs)) >= 0) abort();
    }
    if (io_live != 0 || io_busy != 0 || io_park.head != NULL) abort();
    webnav_words = NULL;
    pthread_mutex_unlock(&autocomplete_gate);
}
