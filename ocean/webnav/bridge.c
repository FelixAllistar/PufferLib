/* The generated file is pinned to stock Bend 2.0.6. Runtime internals here
 * are isolated and covered by repeated-call and concurrent-caller tests. */
#define main webnav_bend_unused_cli
#ifndef WEBNAV_GENERATED
#define WEBNAV_GENERATED "webnav_generated.c"
#endif
#include WEBNAV_GENERATED
#undef main
#include "bridge.h"
#ifdef WEBNAV_VALIDATE_MINIWOB
#include "miniwob/sequence_select/validation.h"
#include "miniwob/tree/validation.h"
#include "miniwob/autocomplete/validation.h"
#endif
_Static_assert(WEBNAV_BATCH == 32 && WEBNAV_WORDS == 256 && WEBNAV_OBS == 128 && WEBNAV_ACTIONS == 13,
    "Update Bend Main/Game and foreign effects together with the ABI");

static pthread_mutex_t webnav_gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t webnav_once = PTHREAD_ONCE_INIT;
static Corpus webnav_heap;

static void webnav_start(void) {
    webnav_heap = corpus_setup(false, 1, 0);
    io_stk = pool_stack();
}

void webnav_batch(uint32_t words[WEBNAV_BATCH * WEBNAV_WORDS]) {
#ifdef WEBNAV_VALIDATE_MINIWOB
    for (unsigned lane=0;lane<WEBNAV_BATCH;lane++) {
        const uint32_t *r=words+lane*WEBNAV_WORDS;
        if(r[0]==11){if(r[1]>=12)abort();continue;}
        if(r[0]==7||r[0]==8){if(!webnav_sequence_select_valid(r))abort();continue;}
        if(r[0]==9){if(!webnav_tree_valid(r))abort();continue;}
        if(r[0]==10){if(!webnav_autocomplete_valid(r))abort();continue;}
        if(r[0]>=4&&r[0]<=6){
            unsigned count=r[0]==5?2:1,capacity=count==1?64:32;
            if(r[1]!=count||r[2]>count+1||r[3]>2||r[4]>1||r[5]<r[6]||r[7]>11||r[11]>64)abort();
            for(unsigned j=0;j<count;j++){
                const uint32_t *m=r+16+4*j;
                if(m[0]>capacity||m[3]>capacity||m[1]>m[2]||m[2]>m[0])abort();
                for(unsigned k=0;k<m[0];k++)if(r[32+32*j+k]<32||r[32+32*j+k]>126)abort();
                for(unsigned k=0;k<m[3];k++)if(r[96+32*j+k]<32||r[96+32*j+k]>126)abort();
                if(r[3]==0&&r[5]<10000&&r[7]==2&&r[2]==j+1&&m[0]+r[11]-(m[2]-m[1])>capacity)abort();
            }
            for(unsigned k=0;k<r[11];k++)if(r[160+k]<32||r[160+k]>126)abort();
            continue;
        }
        if(r[7]==3){if(r[0]!=1&&r[0]!=3)abort();continue;}
        if(r[0]>3||r[1]>16||r[2]>2||r[3]>r[1]||r[4]>r[5]||r[5]>16||r[6]>1||r[7]>2||r[9]<r[10]||(r[2]==1&&r[5]==0))abort();
        for(unsigned i=0;i<r[1];i++)if(r[16+3*i]>5||r[17+3*i]>1||r[18+3*i]>1)abort();
    }
#endif
    pthread_mutex_lock(&webnav_gate);
    pthread_once(&webnav_once, webnav_start);
    webnav_words = words;
    Env e = {webnav_heap, ALC[0]};
    Term m = corpus_eval(webnav_heap, term_tsk(MAIN_FID,
        task_node(e, MAIN_FID, TERM_HOLE, 0, 0)));
    io_spawn(m);
    while (io_runs.head != NULL) {
        if (io_step(e, io_pop(&io_runs)) >= 0) abort();
    }
    /* Only synchronous read/write effects belong in this bridge. */
    if (io_live != 0 || io_busy != 0 || io_park.head != NULL) abort();
    webnav_words = NULL;
    pthread_mutex_unlock(&webnav_gate);
}
