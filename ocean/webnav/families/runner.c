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
#include "calendar/public_controller.h"
#include "controls/public_controller.h"
#include "email/public_controller.h"
#include "tree/public_controller.h"
#include "autocomplete/public_controller.h"
#include "social/public_controller.h"
#include "scroll/public_controller.h"
#include "composite_forms/public_controller.h"
#include "travel/public_controller.h"
#include "catalog/public_controller.h"
#include "typed_inputs/public_controller.h"
#include "market/public_controller.h"
#include "board/public_controller.h"
#include "drawing/public_controller.h"
#include "geometry/public_controller.h"
#include "editing/public_controller.h"
#include "drag/public_controller.h"
#include "visual/public_controller.h"
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
 if(strstr(q,"ONE, then click button TWO")){
  unsigned first=0,second=0;int first_focused=0;
  for(unsigned i=0;i<v->count;i++){const WFNode *n=v->nodes+i;const char *name=wf_text_get(v,n->name);if(!strcmp(name,"ONE")){first=n->ref;first_focused=!!(n->flags&WF_FOCUSED);}if(!strcmp(name,"TWO"))second=n->ref;}
  return first_focused?second:first;
 }
 for(unsigned i=0;i<v->count;i++){const WFNode *n=v->nodes+i;const char *name=wf_text_get(v,n->name);if(n->role==WF_BUTTON&&!strcmp(name,"Submit"))submit=n->ref;}
 if(submit){for(unsigned i=0;i<v->count;i++){const WFNode *n=v->nodes+i;if(n->role!=WF_CHECKBOX&&n->role!=WF_RADIO)continue;const char *name=wf_text_get(v,n->name);int desired=strstr(q,"words similar to")?synonym_requested(q,name):word(q,name);if(n->role==WF_RADIO){if(desired&&!(n->flags&WF_CHECKED))return n->ref;}else if(desired!=!!(n->flags&WF_CHECKED))return n->ref;}return submit;}
 if(strstr(q,"Focus into")){unsigned wanted=strstr(q,"2nd")?1:strstr(q,"3rd")?2:0,count=0;for(unsigned i=0;i<v->count;i++)if(v->nodes[i].role==WF_INPUT&&count++==wanted)return v->nodes[i].ref;}
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
 if(strcmp(f.api->family,"panels")&&strcmp(f.api->family,"click")&&strcmp(f.api->family,"menus")&&strcmp(f.api->family,"forms")&&strcmp(f.api->family,"numeric")&&strcmp(f.api->family,"calendar")&&strcmp(f.api->family,"controls")&&strcmp(f.api->family,"email")&&strcmp(f.api->family,"tree")&&strcmp(f.api->family,"autocomplete")&&strcmp(f.api->family,"social")&&strcmp(f.api->family,"scroll")&&strcmp(f.api->family,"composite_forms")&&strcmp(f.api->family,"travel")&&strcmp(f.api->family,"catalog")&&strcmp(f.api->family,"typed_inputs")&&strcmp(f.api->family,"market")&&strcmp(f.api->family,"board")&&strcmp(f.api->family,"drawing")&&strcmp(f.api->family,"geometry")&&strcmp(f.api->family,"editing")&&strcmp(f.api->family,"drag")&&strcmp(f.api->family,"visual")){fprintf(stderr,"No public controller for family %s\n",f.api->family);wf_close(&f);return 2;}
 unsigned total=0,failures=0;double start=now();
 for(unsigned task=0;task<f.api->task_count;task++){
  if(argc>4 && strcmp(argv[4],f.api->task_names[task]))continue;
  unsigned wins=0,draws=0,quality=0,actions=0,timeouts=0;double reward_sum=0,minimum_reward=1;
  for(unsigned ep=0;ep<episodes;ep++){
   if(wf_reset(&f,task,0x80010000u+ep))return 1;unsigned char visited[WF_MAX_NODES+1]={0};DragPublic drag={.task=task};
   for(unsigned step=1;step<=1000&&!f.words[WF_STATUS];step++){
    WFView view;if(wf_observe(&f,0,&view))return 1;WFAction a={0};int result=0;char scratch[512]={0};
    if(!strcmp(f.api->family,"menus"))result=menus_public_action(&view,&a);
    else if(!strcmp(f.api->family,"forms"))result=forms_public_action(&view,&a);
    else if(!strcmp(f.api->family,"numeric"))result=numeric_public_action(&view,&a);
    else if(!strcmp(f.api->family,"calendar"))result=calendar_public_action(&view,&a);
    else if(!strcmp(f.api->family,"controls"))result=controls_public_next(&view,task,step-1,scratch,&a);
    else if(!strcmp(f.api->family,"email"))result=email_public_next(&view,scratch,sizeof scratch,&a);
    else if(!strcmp(f.api->family,"tree"))result=tree_public_next(&view,&a);
    else if(!strcmp(f.api->family,"autocomplete"))result=autocomplete_public_next(&view,scratch,sizeof scratch,&a);
    else if(!strcmp(f.api->family,"social"))result=social_public_next(&view,scratch,sizeof scratch,&a);
    else if(!strcmp(f.api->family,"scroll"))result=scroll_public_action(&view,&a);
    else if(!strcmp(f.api->family,"composite_forms"))result=composite_public_action(&view,&a);
    else if(!strcmp(f.api->family,"travel"))result=travel_public_action(&view,&a);
    else if(!strcmp(f.api->family,"catalog"))result=catalog_public_next(&view,scratch,sizeof scratch,&a);
    else if(!strcmp(f.api->family,"typed_inputs"))result=typed_inputs_public_action(&view,&a,scratch,sizeof scratch);
    else if(!strcmp(f.api->family,"market"))result=market_public_next(&view,&a);
    else if(!strcmp(f.api->family,"board"))result=board_public_action(&view,&a);
    else if(!strcmp(f.api->family,"drawing"))result=drawing_public_action(&view,&a);
    else if(!strcmp(f.api->family,"geometry"))result=geometry_public_next(&view,task,step-1,&a);
    else if(!strcmp(f.api->family,"editing"))result=editing_public_next(&view,scratch,sizeof scratch,&a);
    else if(!strcmp(f.api->family,"drag"))result=drag_public_next(&drag,&view,step*250,&a);
    else if(!strcmp(f.api->family,"visual"))result=visual_public_action(&view,&a);
    else {unsigned target=!strcmp(f.api->family,"click")?choose_click(&view):choose(&view,visited);a.kind=target?WF_CLICK:WF_WAIT;a.target=target;}
    if(result){fprintf(stderr,"No public action: %s seed=%u step=%u query=%s\n",f.api->task_names[task],0x80010000u+ep,step,wf_text_get(&view,view.instruction));for(unsigned i=0;i<view.count;i++){const WFNode *n=view.nodes+i;fprintf(stderr,"  ref=%u role=%u flags=%u name=[%s] value=[%s]\n",n->ref,n->role,n->flags,wf_text_get(&view,n->name),wf_text_get(&view,n->value));}return 1;}
    if(strcmp(f.api->family,"market"))a.elapsed_ms=step*250;
    if(trace)fprintf(stderr,"%s seed=%u step=%u query=%s kind=%u target=%u\n",f.api->task_names[task],0x80010000u+ep,step,wf_text_get(&view,view.instruction),a.kind,a.target);
    if(wf_apply(&f,0,&a)){fprintf(stderr,"Rejected action: task=%s step=%u kind=%u ref=%u\n",f.api->task_names[task],step,a.kind,a.target);return 1;}
    for(unsigned l=1;l<f.api->batch_lanes;l++)f.words[(size_t)l*f.api->row_words+WF_OP]=WF_OBSERVE;
    if(wf_batch_checked(&f))return 1;actions++;
   }
   if(!f.words[WF_STATUS])return 1;float reward=number(f.words[WF_RAW_REWARD]);
   int win=reward>=0.999f,draw=!strcmp(f.api->family,"board")&&f.words[WF_STATUS]==WF_TERMINAL&&reward==-0.5f;
   wins+=win;draws+=draw;quality+=!strcmp(f.api->family,"geometry")?reward>=0.95f:(win||draw);
   reward_sum+=reward;if(reward<minimum_reward)minimum_reward=reward;timeouts+=f.words[WF_STATUS]==WF_TIMEOUT;total++;
  }
  failures+=episodes-quality;
  printf("{\"task\":\"%s\",\"episodes\":%u,\"successes\":%u,\"draws\":%u,\"timeouts\":%u,\"actions\":%u,\"public_checks_passed\":%u,\"mean_raw_reward\":%.6f,\"min_raw_reward\":%.6f,\"controller\":\"public-scripted; not learned RL\"}\n",f.api->task_names[task],episodes,wins,draws,timeouts,actions,quality,reward_sum/episodes,minimum_reward);fflush(stdout);
 }
 fprintf(stderr,"%u episodes in %.3f seconds; bounded generators, not source-distribution parity\n",total,now()-start);wf_close(&f);return failures||!total?1:0;
}
