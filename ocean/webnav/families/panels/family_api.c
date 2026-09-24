#include "../common/family_api.h"
#include <string.h>
#include <stdio.h>
#define ROW 8192u
#define LANES 4u
#define NODE_BASE 64u
#define NODE_STRIDE 48u
#define QUERY 6208u
void family_panels_batch(uint32_t *words);
static const char *tasks[]={"click-tab","click-tab-2","click-tab-2-easy","click-tab-2-medium","click-tab-2-hard","click-collapsible","click-collapsible-nodelay","click-collapsible-2","click-collapsible-2-nodelay"};
static int ascii(const uint32_t *s,unsigned cap){for(unsigned i=0;i<cap;i++){if(!s[i])return 1;if(s[i]<32||s[i]>126)return 0;}return 0;}
static int valid(const uint32_t *r){
 if(r[WF_VERSION]!=2||r[WF_TASK]>=9||r[WF_OP]>2)return -1;
 if(r[WF_OP]==WF_RESET)return 0;
 if(r[WF_STATUS]>2||r[WF_DEADLINE]!=(r[WF_TASK]==4?20000u:10000u)||r[32]<1||r[32]>6||r[33]>r[32]||r[34]>1||r[35]>r[32]||r[36]>2||r[37]>1||r[38]>128||r[38]<r[32]||r[8]<r[39]||!ascii(r+QUERY,256))return -1;
 for(unsigned i=0;i<r[38];i++){const uint32_t *n=r+NODE_BASE+i*NODE_STRIDE;if(n[0]>2||n[1]>r[32]||n[2]>1||(!n[1]&&n[0]!=2)||!ascii(n+4,40))return -1;}
 return 0;
}
static int add_units(WFView *v,const uint32_t *words,unsigned cap,WFText *out){char s[257];unsigned n=0;for(;n<cap&&n<256&&words[n];n++)s[n]=(char)words[n];return wf_text_add(v,s,n,out);}
static int observe(const uint32_t *r,WFView *v){
 if(valid(r)||r[WF_OP]==WF_RESET)return -1;wf_view_init(v,r[WF_ELAPSED],r[WF_DEADLINE]);
 if(add_units(v,r+QUERY,256,&v->instruction))return -1;
 for(unsigned i=0;i<r[38];i++){const uint32_t *n=r+NODE_BASE+i*NODE_STRIDE;
  if(n[0]==1&&n[1]!=r[33])continue;
  if(v->count==WF_MAX_NODES){v->omitted++;continue;}WFNode *out=v->nodes+v->count++;
  out->ref=i+1;out->parent=n[0]==1?n[1]:0;out->role=n[0]==0?WF_TAB:n[0]==1?WF_LINK:WF_BUTTON;
  out->flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE;if(n[0]==0&&n[1]==r[33])out->flags|=r[34]?WF_EXPANDED:WF_SELECTED;
  /* The projection uses ordinal layout; original page geometry is not asserted. */
  out->y=(float)(v->count-1);out->width=1;out->height=1;
  if(add_units(v,n+4,40,&out->name)||wf_text_add(v,"",0,&out->value))return -1;
 }
 return 0;
}
static int action(uint32_t *r,const WFAction *a){
 if(valid(r)||a->elapsed_ms<r[WF_ELAPSED]||a->text_length||(a->kind!=WF_WAIT&&a->kind!=WF_CLICK))return -1;
 if(a->kind==WF_CLICK){if(!a->target||a->target>r[38])return -1;const uint32_t *n=r+NODE_BASE+(a->target-1)*NODE_STRIDE;if(n[0]==1&&n[1]!=r[33])return -1;}
 r[WF_OP]=WF_STEP;r[WF_ACTION]=a->kind;r[WF_TARGET]=a->target;r[WF_ELAPSED]=a->elapsed_ms;return 0;
}
static const WFFamily api={2,ROW,LANES,9,"panels",tasks,valid,family_panels_batch,observe,action};
WF_EXPORT const WFFamily *webnav_family_v2(void){return &api;}
