#include "../common/family_api.h"
#include <string.h>

#define ROW 8192u
#define NODE_BASE 64u
#define NODE_STRIDE 40u
#define QUERY_BASE 6400u
#define MAX_MENU_NODES 155u

void family_menus_batch(uint32_t *words);
static const char *tasks[]={"click-menu","click-menu-2"};

static int ascii(const uint32_t *s,unsigned cap){
 for(unsigned i=0;i<cap;i++){if(!s[i])return 1;if(s[i]<32||s[i]>126)return 0;}
 return 0;
}
static int valid(const uint32_t *r){
 if(r[WF_VERSION]!=WF_ABI_VERSION||r[WF_TASK]>=2||r[WF_OP]>WF_STEP)return -1;
 if(r[WF_OP]==WF_RESET)return 0;
 if(r[WF_STATUS]>WF_TIMEOUT||r[WF_DEADLINE]!=10000u||r[32]<1||r[32]>MAX_MENU_NODES||
    r[33]>1||r[34]>r[32]||r[35]<1||r[35]>r[32]||r[36]>1||
    !ascii(r+QUERY_BASE,256))return -1;
 unsigned goals=0,buttons=0;
 for(unsigned i=0;i<r[32];i++){
  const uint32_t *n=r+NODE_BASE+i*NODE_STRIDE;
  if(n[0]>1||n[1]>i||n[2]>1||n[3]>1||n[4]>1||n[5]>8||
     n[36]>1||n[37]>1||!ascii(n+6,26))return -1;
  if(n[2]){if(n[0]||n[4]||!n[3])return -1;goals++;}
  if(n[0]){if(r[WF_TASK]!=1||i!=0||n[1]||n[4])return -1;buttons++;}
  if(r[WF_TASK]==0&&n[5])return -1;
 }
 if(goals!=1||buttons!=(r[WF_TASK]==1?1u:0u))return -1;
 return 0;
}
static int add_ascii(WFView *v,const uint32_t *s,unsigned cap,WFText *out){
 char buf[257];unsigned n=0;
 for(;n<cap&&n<256&&s[n];n++)buf[n]=(char)s[n];
 return wf_text_add(v,buf,n,out);
}
static int icon_text(uint32_t icon,const char **text){
 switch(icon){
  case 1:*text="disk icon";return 0;
  case 2:*text="seek-start icon";return 0;
  case 3:*text="stop icon";return 0;
  case 4:*text="play icon";return 0;
  case 5:*text="seek-end icon";return 0;
  case 6:*text="zoomin icon";return 0;
  case 7:*text="zoomout icon";return 0;
  case 8:*text="print icon";return 0;
  default:*text="";return 0;
 }
}
static int observe(const uint32_t *r,WFView *v){
 if(valid(r)||r[WF_OP]==WF_RESET)return -1;
 wf_view_init(v,r[WF_ELAPSED],r[WF_DEADLINE]);
 if(add_ascii(v,r+QUERY_BASE,256,&v->instruction))return -1;
 for(unsigned i=0;i<r[32];i++){
  const uint32_t *n=r+NODE_BASE+i*NODE_STRIDE;
  if(!n[36])continue;
  if(v->count==WF_MAX_NODES){v->omitted++;continue;}
  WFNode *o=v->nodes+v->count++;
  o->ref=i+1;o->parent=n[1];o->role=n[0]||n[4]?WF_BUTTON:WF_OPTION;
  o->flags=WF_VISIBLE;
  if(n[3])o->flags|=WF_ENABLED|WF_CLICKABLE;
  if(n[37])o->flags|=WF_EXPANDED;
  if(r[34]==i+1)o->flags|=WF_SELECTED;
  o->x=8.0f;o->y=(float)(v->count-1)*24.0f;o->width=128.0f;o->height=22.0f;
  const char *icon="";icon_text(n[5],&icon);
  if(add_ascii(v,n+6,26,&o->name)||wf_text_add(v,icon,strlen(icon),&o->value))return -1;
 }
 return 0;
}
static int action(uint32_t *r,const WFAction *a){
 if(valid(r)||a->elapsed_ms<r[WF_ELAPSED]||a->text_length)return -1;
 if(a->kind!=WF_WAIT&&a->kind!=WF_CLICK&&a->kind!=WF_POINTER_MOVE)return -1;
 if(a->kind==WF_WAIT){if(a->target)return -1;}
 else if(!a->target||a->target>r[32])return -1;
 r[WF_OP]=WF_STEP;r[WF_ACTION]=a->kind;r[WF_TARGET]=a->target;r[WF_ELAPSED]=a->elapsed_ms;
 return 0;
}
static const WFFamily api={WF_ABI_VERSION,ROW,4,2,"menus",tasks,valid,family_menus_batch,observe,action};
WF_EXPORT const WFFamily *webnav_family_v2(void){return &api;}
