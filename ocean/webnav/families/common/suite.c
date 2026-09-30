#include "suite.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int error_message(char *error,size_t size,const char *message){
    if(error&&size)snprintf(error,size,"%s",message);
    return -1;
}

void wf_suite_close(WFSuite *s){
    if(!s)return;
    if(s->families)for(size_t f=0;f<s->family_count;f++)wf_close(s->families+f);
    if(s->task_names)for(size_t t=0;t<s->total_tasks;t++)free(s->task_names[t]);
    free(s->task_names);free(s->dirty);free(s->task_offsets);
    free(s->lane_offsets);free(s->families);
    *s=(WFSuite){0};
}

static int stage_reset(WFSuite *s,size_t f,size_t lane,uint32_t task,uint32_t seed){
    WFLoaded *loaded=s->families+f;
    uint32_t *r=loaded->words+(size_t)lane*loaded->api->row_words;
    memset(r,0,(size_t)loaded->api->row_words*sizeof *r);
    r[WF_VERSION]=WF_ABI_VERSION;r[WF_TASK]=task;r[WF_OP]=WF_RESET;r[WF_SEED]=seed;
    s->dirty[f]=1;
    return 0;
}

int wf_suite_open(WFSuite *s,const char *const *libraries,size_t count,
                  char *error,size_t error_size){
    if(!s||!libraries||!count||count>SIZE_MAX/sizeof(WFLoaded)-1)
        return error_message(error,error_size,"invalid suite arguments");
    *s=(WFSuite){0};
    s->family_count=count;
    s->families=calloc(count,sizeof *s->families);
    s->lane_offsets=calloc(count+1,sizeof *s->lane_offsets);
    s->task_offsets=calloc(count+1,sizeof *s->task_offsets);
    s->dirty=calloc(count,sizeof *s->dirty);
    if(!s->families||!s->lane_offsets||!s->task_offsets||!s->dirty)
        goto allocation_error;
    for(size_t f=0;f<count;f++){
        if(!libraries[f]||wf_open(s->families+f,libraries[f],error,error_size)){
            if(!libraries[f])error_message(error,error_size,"null family library path");
            wf_suite_close(s);return -1;
        }
        const WFFamily *a=s->families[f].api;
        for(size_t prior=0;prior<f;prior++)
            if(!strcmp(a->family,s->families[prior].api->family)){
                error_message(error,error_size,"duplicate family name");
                wf_suite_close(s);return -1;
            }
        if(s->total_lanes>UINT32_MAX-a->batch_lanes||
           s->total_tasks>UINT32_MAX-a->task_count){
            error_message(error,error_size,"suite index overflow");
            wf_suite_close(s);return -1;
        }
        s->total_lanes+=a->batch_lanes;s->total_tasks+=a->task_count;
        s->lane_offsets[f+1]=s->total_lanes;
        s->task_offsets[f+1]=s->total_tasks;
    }
    s->task_names=calloc(s->total_tasks,sizeof *s->task_names);
    if(!s->task_names)goto allocation_error;
    for(size_t f=0;f<count;f++){
        const WFFamily *a=s->families[f].api;
        for(size_t t=0;t<a->task_count;t++){
            const char *local=a->task_names[t];
            size_t n=strlen(a->family),m=strlen(local);
            if(n>SIZE_MAX-m-4)goto allocation_error;
            char *name=malloc(n+m+4);
            if(!name)goto allocation_error;
            snprintf(name,n+m+4,"%s / %s",a->family,local);
            for(size_t previous=0;previous<s->task_offsets[f]+t;previous++)
                if(!strcmp(name,s->task_names[previous])){
                    free(name);
                    error_message(error,error_size,"duplicate global task name");
                    wf_suite_close(s);return -1;
                }
            s->task_names[s->task_offsets[f]+t]=name;
        }
        for(size_t lane=0;lane<a->batch_lanes;lane++)
            stage_reset(s,f,lane,(uint32_t)(lane%a->task_count),
                        (uint32_t)(s->lane_offsets[f]+lane));
    }
    if(error&&error_size)*error=0;
    return 0;
allocation_error:
    error_message(error,error_size,"suite allocation failed");
    wf_suite_close(s);return -1;
}

static int find_index(const size_t *offsets,size_t count,size_t index,
                      size_t *family,size_t *local){
    if(!offsets||index>=offsets[count])return -1;
    for(size_t f=0;f<count;f++)if(index<offsets[f+1]){
        if(family)*family=f;
        if(local)*local=index-offsets[f];
        return 0;
    }
    return -1;
}
int wf_suite_find_lane(const WFSuite *s,size_t index,size_t *family,size_t *lane){
    return s?find_index(s->lane_offsets,s->family_count,index,family,lane):-1;
}
int wf_suite_find_task(const WFSuite *s,size_t index,size_t *family,size_t *task){
    return s?find_index(s->task_offsets,s->family_count,index,family,task):-1;
}
const char *wf_suite_task_name(const WFSuite *s,size_t task){
    return s&&s->task_names&&task<s->total_tasks?s->task_names[task]:NULL;
}
int wf_suite_reset_all(WFSuite *s,uint32_t seed){
    if(!s||!s->families||s->poisoned)return -1;
    for(size_t f=0;f<s->family_count;f++){
        const WFFamily *a=s->families[f].api;
        for(size_t lane=0;lane<a->batch_lanes;lane++)
            stage_reset(s,f,lane,(uint32_t)(lane%a->task_count),
                        seed+(uint32_t)(s->lane_offsets[f]+lane));
    }
    return 0;
}
int wf_suite_reset_lane(WFSuite *s,size_t index,size_t global_task,uint32_t seed){
    size_t family,lane,task_family,local_task;
    if(!s||s->poisoned||wf_suite_find_lane(s,index,&family,&lane)||
       wf_suite_find_task(s,global_task,&task_family,&local_task)||family!=task_family)
        return -1;
    return stage_reset(s,family,lane,(uint32_t)local_task,seed);
}
int wf_suite_flush(WFSuite *s){
    if(!s||!s->families||s->poisoned)return -1;
    for(size_t f=0;f<s->family_count;f++)if(s->dirty[f]){
        WFLoaded *loaded=s->families+f;
        if(wf_batch_checked(loaded)){s->poisoned=1;return -1;}
        for(size_t lane=0;lane<loaded->api->batch_lanes;lane++)
            loaded->words[(size_t)lane*loaded->api->row_words+WF_OP]=WF_OBSERVE;
        s->dirty[f]=0;
    }
    return 0;
}
int wf_suite_observe(const WFSuite *s,size_t index,WFView *view){
    size_t family,lane;
    if(!s||s->poisoned||!view||wf_suite_find_lane(s,index,&family,&lane))return -1;
    const WFLoaded *loaded=s->families+family;
    if(loaded->words[lane*loaded->api->row_words+WF_OP]!=WF_OBSERVE)return -1;
    return wf_observe(s->families+family,(unsigned)lane,view);
}
int wf_suite_apply(WFSuite *s,size_t index,const WFAction *action){
    size_t family,lane;
    if(!s||s->poisoned||!action||wf_suite_find_lane(s,index,&family,&lane))return -1;
    WFLoaded *loaded=s->families+family;
    uint32_t *r=loaded->words+lane*loaded->api->row_words;
    if(r[WF_OP]!=WF_OBSERVE)return -1;
    if(wf_apply(loaded,(unsigned)lane,action))return -1;
    if(r[WF_OP]!=WF_STEP){s->poisoned=1;return -1;}
    s->dirty[family]=1;
    return 0;
}
int wf_suite_result(const WFSuite *s,size_t index,WFSuiteResult *out){
    size_t family,lane;
    if(!s||s->poisoned||!out||wf_suite_find_lane(s,index,&family,&lane))return -1;
    const WFLoaded *loaded=s->families+family;
    const uint32_t *r=loaded->words+lane*loaded->api->row_words;
    if(r[WF_OP]!=WF_OBSERVE||r[WF_TASK]>=loaded->api->task_count)return -1;
    WFSuiteResult result={0};
    result.status=r[WF_STATUS];result.elapsed_ms=r[WF_ELAPSED];
    result.deadline_ms=r[WF_DEADLINE];
    result.global_task=s->task_offsets[family]+r[WF_TASK];
    memcpy(&result.raw_reward,r+WF_RAW_REWARD,sizeof result.raw_reward);
    memcpy(&result.timed_reward,r+WF_TIMED_REWARD,sizeof result.timed_reward);
    *out=result;return 0;
}
