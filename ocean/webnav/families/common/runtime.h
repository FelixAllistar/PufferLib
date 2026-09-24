/* Include once in a family bridge.c AFTER defining WF_TOTAL_WORDS,
 * WF_ARRAY_DEPTH and WF_BATCH_FUNCTION. Generated code uses common read/write.
 * Only WF_BATCH_FUNCTION is public; the generated runtime is DSO-local. */
#ifndef WF_GENERATED
#error WF_GENERATED must name the family generated C file
#endif
#define main wf_unused_cli_main
#include WF_GENERATED
#undef main
_Static_assert(WF_TOTAL_WORDS==(1u<<WF_ARRAY_DEPTH),"family transport array depth mismatch");
static pthread_mutex_t wf_gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t wf_once=PTHREAD_ONCE_INIT;
static Corpus wf_heap;
static void wf_start(void){
    /* Stock pool_stack installs process signal handlers and a thread alternate
     * stack for its CLI diagnostics. A library must preserve its host's setup. */
    struct sigaction old_segv,old_bus;stack_t old_stack;
    if(sigaction(SIGSEGV,NULL,&old_segv)||sigaction(SIGBUS,NULL,&old_bus)||sigaltstack(NULL,&old_stack))abort();
    wf_heap=corpus_setup(false,1,0);io_stk=pool_stack();
    if(sigaction(SIGSEGV,&old_segv,NULL)||sigaction(SIGBUS,&old_bus,NULL)||sigaltstack(&old_stack,NULL))abort();
}
void WF_BATCH_FUNCTION(uint32_t *words){
    pthread_mutex_lock(&wf_gate);pthread_once(&wf_once,wf_start);wf_words=words;
    Env e={wf_heap,ALC[0]};Term m=corpus_eval(wf_heap,term_tsk(MAIN_FID,task_node(e,MAIN_FID,TERM_HOLE,0,0)));
    io_spawn(m);
    while(io_runs.head!=NULL)if(io_step(e,io_pop(&io_runs))>=0)abort();
    if(io_live!=0||io_busy!=0||io_park.head!=NULL)abort();
    wf_words=NULL;pthread_mutex_unlock(&wf_gate);
}
