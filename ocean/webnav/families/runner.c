#define _POSIX_C_SOURCE 200809L
#include "common/loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include "click/expert_synonyms.h"
#include "menus/public_controller.h"
#include "forms/public_controller.h"
#include "numeric/public_controller.h"
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static int word(const char *q,const char *s){size_t n=strlen(s);if(!n)return 0;const char *p=q;while((p=strstr(p,s))){if((p==q||!isalnum((unsigned char)p[-1]))&&!isalnum((unsigned char)p[n]))return 1;p++;}return 0;}
static int synonym_requested(const char *query,const char *label){
 for(unsigned g=0;g<sizeof(click_synonyms)/sizeof(*click_synonyms);g++){
  int same=0;for(unsigned i=0;click_synonyms[g][i];i++)same|=!strcmp(label,click_synonyms[g][i]);
  if(same)for(unsigned i=0;click_synonyms[g][i];i++)if(word(query,click_synonyms[g][i]))return 1;
 }return 0;
}
static unsigned choose_click(const WFView *v){
 const char *q=wf_text_get(v,v->instruction);unsigned submit=0;
 for(unsigned i=0;i<v->count;i++){const WFNode *n=v->nodes+i;const char *name=wf_text_get(v,n->name);if(n->role==WF_BUTTON&&!strcmp(name,"Submit"))submit=n->ref;}
 if(submit){for(unsigned i=0;i<v->count;i++){const WFNode *n=v->nodes+i;if(n->role!=WF_CHECKBOX)continue;const char *name=wf_text_get(v,n->name);int desired=strstr(q,"words similar to")?synonym_requested(q,name):word(q,name);if(desired!=!!(n->flags&WF_CHECKED))return n->ref;}return submit;}
 if(strstr(q,"Focus into")){unsigned wanted=strstr(q,"1st")?0:strstr(q,"2nd")?1:2,count=0;for(unsigned i=0;i<v->count;i++)if(v->nodes[i].role==WF_INPUT&&count++==wanted)return v->nodes[i].ref;}
 if(strstr(q,"widget")){unsigned role=strstr(q,"\"radio\"")?WF_RADIO:strstr(q,"\"checkbox\"")?WF_CHECKBOX:strstr(q,"\"textarea\"")?WF_TEXTAREA:strstr(q,"\"text\"")?WF_INPUT:WF_BUTTON;for(unsigned i=0;i<v->count;i++)if(v->nodes[i].role==role)return v->nodes[i].ref;}
 for(unsigned i=0;i<v->count;i++){const WFNode *n=v->nodes+i;const char *name=wf_text_get(v,n->name);if(word(q,name)||(!strcmp(name,"Close")&&strstr(q,"\"x\"")))return n->ref;}
 return v->count==1?v->nodes[0].ref:0;
}
static unsigned choose(const WFView *v,unsigned char *visited){
 const char *q=wf_text_get(v,v->instruction);
 const char *begin=strchr(q,'"'),*end=begin?strchr(begin+1,'"'):NULL;
 for(unsigned i=0;i<v->count;i++){const WFNode *n=v->nodes+i;const char *name=wf_text_get(v,n->name);if(n->role==WF_LINK&&name&&begin&&end&&strlen(name)==(size_t)(end-begin-1)&&!strncmp(name,begin+1,(size_t)(end-begin-1)))return n->ref;}
 int open=0;for(unsigned i=0;i<v->count;i++)open|=(v->nodes[i].flags&WF_EXPANDED)!=0;
 for(unsigned i=0;i<v->count;i++){const WFNode *n=v->nodes+i;const char *name=wf_text_get(v,n->name);
  if(n->role==WF_BUTTON&&open&&name&&!strcmp(name,"Submit"))return n->ref;
  if(n->role==WF_TAB&&name&&*name&&strstr(q,name))return n->ref;
 }
 for(unsigned i=0;i<v->count;i++){const WFNode *n=v->nodes+i;if(n->role==WF_TAB&&n->ref<=WF_MAX_NODES&&!visited[n->ref]){visited[n->ref]=1;return n->ref;}}
 return 0;
}
static float number(uint32_t b){float f;memcpy(&f,&b,4);return f;}
int main(int argc,char **argv){
 if(argc<2||argc>5){fprintf(stderr,"usage: %s FAMILY_SO [episodes_per_task=100] [trace=0] [task-name]\n",argv[0]);return 2;}
 unsigned episodes=argc>2?strtoul(argv[2],0,10):100;int trace=argc>3?atoi(argv[3]):0;if(!episodes||episodes>10000)return 2;
 WFLoaded f;char error[512];if(wf_open(&f,argv[1],error,sizeof error)){fprintf(stderr,"%s\n",error);return 1;}
 if(strcmp(f.api->family,"panels")&&strcmp(f.api->family,"click")&&strcmp(f.api->family,"menus")&&strcmp(f.api->family,"forms")&&strcmp(f.api->family,"numeric")){fprintf(stderr,"No public controller for family %s\n",f.api->family);wf_close(&f);return 2;}
 unsigned total=0,failures=0;double start=now();
 for(unsigned task=0;task<f.api->task_count;task++){
  if(argc>4 && strcmp(argv[4],f.api->task_names[task]))continue;
  unsigned wins=0,actions=0,timeouts=0;
  for(unsigned ep=0;ep<episodes;ep++){
   if(wf_reset(&f,task,0x80010000u+ep))return 1;unsigned char visited[WF_MAX_NODES+1]={0};
   for(unsigned step=1;step<=1000&&!f.words[WF_STATUS];step++){
    WFView view;if(wf_observe(&f,0,&view))return 1;WFAction a={0};int result=0;
    if(!strcmp(f.api->family,"menus"))result=menus_public_action(&view,&a);
    else if(!strcmp(f.api->family,"forms"))result=forms_public_action(&view,&a);
    else if(!strcmp(f.api->family,"numeric"))result=numeric_public_action(&view,&a);
    else {unsigned target=!strcmp(f.api->family,"click")?choose_click(&view):choose(&view,visited);a.kind=target?WF_CLICK:WF_WAIT;a.target=target;}
    if(result){fprintf(stderr,"No public action: %s seed=%u step=%u query=%s\n",f.api->task_names[task],0x80010000u+ep,step,wf_text_get(&view,view.instruction));for(unsigned i=0;i<view.count;i++){const WFNode *n=view.nodes+i;fprintf(stderr,"  ref=%u role=%u flags=%u name=[%s] value=[%s]\n",n->ref,n->role,n->flags,wf_text_get(&view,n->name),wf_text_get(&view,n->value));}return 1;}
    a.elapsed_ms=step*250;
    if(trace)fprintf(stderr,"%s seed=%u step=%u query=%s kind=%u target=%u\n",f.api->task_names[task],0x80010000u+ep,step,wf_text_get(&view,view.instruction),a.kind,a.target);
    if(wf_apply(&f,0,&a)){fprintf(stderr,"Rejected action: task=%s step=%u kind=%u ref=%u\n",f.api->task_names[task],step,a.kind,a.target);return 1;}
    for(unsigned l=1;l<f.api->batch_lanes;l++)f.words[(size_t)l*f.api->row_words+WF_OP]=WF_OBSERVE;
    if(wf_batch_checked(&f))return 1;actions++;
   }
   if(!f.words[WF_STATUS])return 1;wins+=number(f.words[WF_RAW_REWARD])>=0.999f;timeouts+=f.words[WF_STATUS]==WF_TIMEOUT;total++;
  }
  failures+=episodes-wins;
  printf("{\"task\":\"%s\",\"episodes\":%u,\"successes\":%u,\"timeouts\":%u,\"actions\":%u,\"controller\":\"public-scripted; not learned RL\"}\n",f.api->task_names[task],episodes,wins,timeouts,actions);fflush(stdout);
 }
 fprintf(stderr,"%u episodes in %.3f seconds; bounded generators, not source-distribution parity\n",total,now()-start);wf_close(&f);return failures||!total?1:0;
}
