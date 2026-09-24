/* Standalone CPU bridge for the tree lane.  The shared webnav bridge uses the
 * same stock Bend runtime pattern; this copy keeps the tree tests self
 * contained and does not alter shared dispatch/build files. */
#define main webnav_tree_bend_unused_cli
#ifndef WEBNAV_TREE_GENERATED
#define WEBNAV_TREE_GENERATED "tree_generated.c"
#endif
#include WEBNAV_TREE_GENERATED
#undef main

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

static pthread_mutex_t webnav_tree_gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t webnav_tree_once = PTHREAD_ONCE_INIT;
static Corpus webnav_tree_heap;

static void webnav_tree_start(void) {
    webnav_tree_heap = corpus_setup(false, 1, 0);
    io_stk = pool_stack();
}

void webnav_tree_batch(uint32_t words[32u * 256u]) {
    pthread_mutex_lock(&webnav_tree_gate);
    pthread_once(&webnav_tree_once, webnav_tree_start);
    webnav_words = words;
    Env e = {webnav_tree_heap, ALC[0]};
    Term m = corpus_eval(webnav_tree_heap, term_tsk(MAIN_FID,
        task_node(e, MAIN_FID, TERM_HOLE, 0, 0)));
    io_spawn(m);
    while (io_runs.head != NULL) {
        if (io_step(e, io_pop(&io_runs)) >= 0) abort();
    }
    if (io_live != 0 || io_busy != 0 || io_park.head != NULL) abort();
    webnav_words = NULL;
    pthread_mutex_unlock(&webnav_tree_gate);
}
