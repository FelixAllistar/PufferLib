/* Standalone CPU bridge for the training tree generator. */
#define main webnav_tree_generate_bend_unused_cli
#ifndef WEBNAV_TREE_GENERATE_C
#define WEBNAV_TREE_GENERATE_C "tree_generate_generated.c"
#endif
#include WEBNAV_TREE_GENERATE_C
#undef main

#include "../../bridge.h"

#include <pthread.h>
#include <stdlib.h>

static pthread_mutex_t tree_generate_gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t tree_generate_once = PTHREAD_ONCE_INIT;
static Corpus tree_generate_heap;

static void tree_generate_start(void) {
    tree_generate_heap = corpus_setup(false, 1, 0);
    io_stk = pool_stack();
}

void webnav_tree_generate_batch(uint32_t words[WEBNAV_BATCH * WEBNAV_WORDS]) {
    pthread_mutex_lock(&tree_generate_gate);
    pthread_once(&tree_generate_once, tree_generate_start);
    webnav_words = words;
    Env e = {tree_generate_heap, ALC[0]};
    Term m = corpus_eval(tree_generate_heap, term_tsk(MAIN_FID,
        task_node(e, MAIN_FID, TERM_HOLE, 0, 0)));
    io_spawn(m);
    while (io_runs.head != NULL) {
        if (io_step(e, io_pop(&io_runs)) >= 0) abort();
    }
    if (io_live != 0 || io_busy != 0 || io_park.head != NULL) abort();
    webnav_words = NULL;
    pthread_mutex_unlock(&tree_generate_gate);
}
