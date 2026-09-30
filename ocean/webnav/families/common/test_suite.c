#include "suite.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

static const uint32_t *row(const WFSuite *s,size_t family,size_t lane){
    const WFLoaded *loaded=s->families+family;
    return loaded->words+lane*loaded->api->row_words;
}
static size_t family_bytes(const WFSuite *s,size_t family){
    const WFFamily *a=s->families[family].api;
    return (size_t)a->row_words*a->batch_lanes*sizeof(uint32_t);
}
static void public_view(const WFSuite *s,size_t lane){
    WFView view;
    assert(!wf_suite_observe(s,lane,&view));
    assert(wf_view_valid(&view));
}

int main(int argc,char **argv){
    assert(!prctl(PR_SET_DUMPABLE,0,0,0,0));
    if(argc<2){fprintf(stderr,"usage: %s FAMILY_DSO [FAMILY_DSO ...]\n",argv[0]);return 2;}
    WFSuite s={0};char error[512];
    assert(!wf_suite_open(&s,(const char *const *)(argv+1),(size_t)(argc-1),error,sizeof error));
    size_t lanes=0,tasks=0;
    for(size_t f=0;f<s.family_count;f++){
        const WFFamily *a=s.families[f].api;
        assert(s.lane_offsets[f]==lanes&&s.task_offsets[f]==tasks);
        lanes+=a->batch_lanes;tasks+=a->task_count;
        assert(s.lane_offsets[f+1]==lanes&&s.task_offsets[f+1]==tasks);
        for(size_t l=0;l<a->batch_lanes;l++){
            size_t found,local;
            assert(!wf_suite_find_lane(&s,s.lane_offsets[f]+l,&found,&local));
            assert(found==f&&local==l);
        }
        for(size_t t=0;t<a->task_count;t++){
            size_t found,local;
            size_t global=s.task_offsets[f]+t;
            assert(!wf_suite_find_task(&s,global,&found,&local));
            assert(found==f&&local==t);
            const char *name=wf_suite_task_name(&s,global);
            assert(name&&strstr(name,a->family)&&strstr(name,a->task_names[t]));
            for(size_t previous=0;previous<global;previous++)
                assert(strcmp(name,wf_suite_task_name(&s,previous)));
        }
    }
    assert(lanes==s.total_lanes&&tasks==s.total_tasks);
    assert(wf_suite_find_lane(&s,lanes,NULL,NULL)<0);
    assert(wf_suite_find_task(&s,tasks,NULL,NULL)<0);
    assert(!wf_suite_reset_all(&s,12345u));
    for(size_t f=0;f<s.family_count;f++)for(size_t l=0;l<s.families[f].api->batch_lanes;l++){
        const uint32_t *r=row(&s,f,l);
        assert(r[WF_OP]==WF_RESET&&r[WF_TASK]==l%s.families[f].api->task_count);
        assert(r[WF_SEED]==12345u+(uint32_t)(s.lane_offsets[f]+l));
    }
    assert(!wf_suite_reset_all(&s,12345u)); /* same seeds and task mapping */
    assert(!wf_suite_flush(&s));
    for(size_t lane=0;lane<lanes;lane++)public_view(&s,lane);

    /* An action staged before a reset must not leak into the next episode. */
    size_t first=s.lane_offsets[0],first_task=s.task_offsets[0];
    WFSuiteResult result;
    assert(!wf_suite_result(&s,first,&result));
    WFAction wait={.kind=WF_WAIT,.elapsed_ms=result.deadline_ms};
    assert(!wf_suite_apply(&s,first,&wait));
    assert(!wf_suite_reset_lane(&s,first,first_task,707u));
    const uint32_t *clean=row(&s,0,0);
    assert(clean[WF_OP]==WF_RESET&&clean[WF_SEED]==707u&&clean[WF_ACTION]==0);
    for(size_t word=WF_HEADER_WORDS;word<s.families[0].api->row_words;word++)assert(clean[word]==0);
    assert(!wf_suite_flush(&s));
    public_view(&s,first);

    /* Keep every other lane byte-for-byte stable while first lanes visit all
     * local tasks. This catches accidental replay of STEP or RESET rows. */
    unsigned char **untouched=calloc(s.family_count,sizeof *untouched);
    assert(untouched);
    for(size_t f=0;f<s.family_count;f++){
        untouched[f]=malloc(family_bytes(&s,f));assert(untouched[f]);
        memcpy(untouched[f],s.families[f].words,family_bytes(&s,f));
    }
    for(size_t f=0;f<s.family_count;f++){
        const WFFamily *a=s.families[f].api;
        size_t global_lane=s.lane_offsets[f];
        for(size_t local_task=0;local_task<a->task_count;local_task++){
            size_t global_task=s.task_offsets[f]+local_task;
            uint32_t seed=1000u+(uint32_t)global_task;
            assert(!wf_suite_reset_lane(&s,global_lane,global_task,seed));
            assert(!wf_suite_flush(&s));
            public_view(&s,global_lane);
            assert(!wf_suite_result(&s,global_lane,&result));
            assert(result.global_task==global_task&&result.status==WF_RUNNING);
            wait=(WFAction){.kind=WF_WAIT,.elapsed_ms=result.deadline_ms};
            assert(!wf_suite_apply(&s,global_lane,&wait));
            assert(!wf_suite_flush(&s));
            assert(!wf_suite_result(&s,global_lane,&result));
            assert(result.status!=WF_RUNNING&&result.global_task==global_task);
            public_view(&s,global_lane);
            for(size_t other=0;other<s.family_count;other++)if(other!=f)
                assert(!memcmp(untouched[other],s.families[other].words,family_bytes(&s,other)));
            for(size_t lane=1;lane<a->batch_lanes;lane++){
                size_t bytes=(size_t)a->row_words*sizeof(uint32_t);
                assert(!memcmp(untouched[f]+lane*bytes,row(&s,f,lane),bytes));
            }
            memcpy(untouched[f],s.families[f].words,family_bytes(&s,f));
            assert(!wf_suite_flush(&s)); /* clean flush has no work */
            assert(!memcmp(untouched[f],s.families[f].words,family_bytes(&s,f)));
        }
        memcpy(untouched[f],s.families[f].words,family_bytes(&s,f));
    }
    if(s.family_count>1)
        assert(wf_suite_reset_lane(&s,s.lane_offsets[0],s.task_offsets[1],0)<0);
    for(size_t f=0;f<s.family_count;f++)free(untouched[f]);
    free(untouched);
    printf("PASS: suite transport %zu families, %zu lanes, %zu global tasks\n",
           s.family_count,s.total_lanes,s.total_tasks);
    wf_suite_close(&s);
    return 0;
}
