/* Stock 2.0.35 runtime embedding. The bridge transports words only. */
#define main pg9_unused_cli_main
#include PG9_GENERATED
#undef main

static pthread_mutex_t pg9_gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t pg9_once = PTHREAD_ONCE_INIT;
static u64 *pg9_heap;

static void pg9_start(void) {
    struct sigaction old_segv, old_bus;
    stack_t old_stack;
    if (sigaction(SIGSEGV, NULL, &old_segv) ||
        sigaction(SIGBUS, NULL, &old_bus) || sigaltstack(NULL, &old_stack)) abort();
    pg9_heap = corpus_setup(false, 1, 0);
    io_stk = pool_stack();
    if (sigaction(SIGSEGV, &old_segv, NULL) ||
        sigaction(SIGBUS, &old_bus, NULL) || sigaltstack(&old_stack, NULL)) abort();
}

void pg9_execute(uint32_t *words) {
    pthread_mutex_lock(&pg9_gate);
    pthread_once(&pg9_once, pg9_start);
    pg9_words = words;
    Env e = {pg9_heap, ALC[0]};
    Term m = corpus_eval(pg9_heap, term_tsk(MAIN_FID,
        task_node(e, MAIN_FID, TERM_HOLE, 0, 0)));
    io_spawn(m);
    while (io_runs != NULL) io_step(e, io_pop(&io_runs));
    if (io_live != 0 || io_busy != 0 || io_park != NULL) abort();
    pg9_words = NULL;
    pthread_mutex_unlock(&pg9_gate);
}
