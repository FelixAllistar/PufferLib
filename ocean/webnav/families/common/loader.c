#define _GNU_SOURCE
#include "loader.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static int power_two(uint32_t n){return n&&!(n&(n-1));}
static int fail(WFLoaded *f,char *error,size_t size,const char *message){if(error&&size)snprintf(error,size,"%s",message);wf_close(f);return -1;}
void wf_close(WFLoaded *f){if(!f)return;free(f->words);if(f->handle)dlclose(f->handle);*f=(WFLoaded){0};}
int wf_open(WFLoaded *out,const char *library,char *error,size_t size){
 if(!out||!library)return -1;*out=(WFLoaded){0};
 out->handle=dlopen(library,RTLD_NOW|RTLD_LOCAL|RTLD_NODELETE);if(!out->handle)return fail(out,error,size,dlerror());
 WFGetFamily get=(WFGetFamily)dlsym(out->handle,"webnav_family_v2");
 if(!get)return fail(out,error,size,"missing webnav_family_v2 descriptor");
 const WFFamily *a=out->api=get();
 if(!a||a->abi_version!=WF_ABI_VERSION||!a->family||!a->task_names||!a->task_count||a->task_count>125||
    !power_two(a->row_words)||a->row_words<WF_HEADER_WORDS||a->row_words>65536||
    !power_two(a->batch_lanes)||a->batch_lanes>128||(uint64_t)a->row_words*a->batch_lanes>(1u<<22)||
    !a->batch||!a->validate||!a->observe||!a->action)return fail(out,error,size,"invalid family descriptor");
 for(unsigned i=0;i<a->task_count;i++){if(!a->task_names[i]||!*a->task_names[i])return fail(out,error,size,"missing task name");for(unsigned j=0;j<i;j++)if(!strcmp(a->task_names[i],a->task_names[j]))return fail(out,error,size,"duplicate task name");}
 out->words=calloc((size_t)a->row_words*a->batch_lanes,sizeof(uint32_t));
 if(!out->words)return fail(out,error,size,"family allocation failed");
 if(error&&size)*error=0;return 0;
}
int wf_batch_checked(WFLoaded *f){
 if(!f||!f->api||!f->words)return -1;const WFFamily *a=f->api;
 for(unsigned i=0;i<a->batch_lanes;i++){
  const uint32_t *r=f->words+(size_t)i*a->row_words;
  if(r[WF_VERSION]!=WF_ABI_VERSION||r[WF_TASK]>=a->task_count||r[WF_OP]>WF_STEP||a->validate(r))return -1;
 }
 a->batch(f->words);
 for(unsigned i=0;i<a->batch_lanes;i++){
  const uint32_t *r=f->words+(size_t)i*a->row_words;
  float raw,timed;memcpy(&raw,r+WF_RAW_REWARD,4);memcpy(&timed,r+WF_TIMED_REWARD,4);
  if(r[WF_VERSION]!=WF_ABI_VERSION||r[WF_TASK]>=a->task_count||r[WF_ERROR]||r[WF_STATUS]>WF_TIMEOUT||!r[WF_DEADLINE]||!isfinite(raw)||!isfinite(timed)||a->validate(r))return -1;
 }
 return 0;
}
int wf_reset(WFLoaded *f,uint32_t task,uint32_t seed){
 if(!f||!f->api||task>=f->api->task_count)return -1;
 for(unsigned i=0;i<f->api->batch_lanes;i++){uint32_t *r=f->words+(size_t)i*f->api->row_words;memset(r,0,f->api->row_words*sizeof *r);r[WF_VERSION]=WF_ABI_VERSION;r[WF_TASK]=task;r[WF_OP]=WF_RESET;r[WF_SEED]=seed+i;}
 return wf_batch_checked(f);
}
int wf_view_valid(const WFView *v){
 if(!v||v->version!=WF_ABI_VERSION||v->count>WF_MAX_NODES||v->text_bytes>WF_TEXT_BYTES||!v->deadline_ms||!wf_text_get(v,v->instruction))return 0;
 for(unsigned i=0;i<v->count;i++){const WFNode *n=v->nodes+i;if(!n->ref||n->role>WF_TEXTAREA||!(n->flags&WF_VISIBLE)||!wf_text_get(v,n->name)||!wf_text_get(v,n->value)||
   !isfinite(n->x)||!isfinite(n->y)||!isfinite(n->width)||!isfinite(n->height)||n->width<0||n->height<0||n->selection_start>n->selection_end||
   !isfinite(n->scroll_x)||!isfinite(n->scroll_y)||!isfinite(n->scroll_max_x)||!isfinite(n->scroll_max_y))return 0;
  for(unsigned j=0;j<i;j++)if(n->ref==v->nodes[j].ref)return 0;
 }
 return 1;
}
int wf_observe(WFLoaded *f,unsigned lane,WFView *out){
 if(!f||!f->api||!out||lane>=f->api->batch_lanes)return -1;
 if(f->api->observe(f->words+(size_t)lane*f->api->row_words,out))return -1;
 return wf_view_valid(out)?0:-1;
}
int wf_apply(WFLoaded *f,unsigned lane,const WFAction *action){
 if(!f||!f->api||!action||lane>=f->api->batch_lanes||action->kind>WF_SELECT_OPTION||(!action->text&&action->text_length))return -1;
 uint32_t *r=f->words+(size_t)lane*f->api->row_words;
 if(action->elapsed_ms<r[WF_ELAPSED])return -1;
 return f->api->action(r,action);
}
