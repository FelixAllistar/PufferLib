#include "../common/family_api.h"
#include <string.h>
#define ROW 2048u
void family_click_batch(uint32_t *words);
static const char *tasks[]={"click-test","click-test-2","click-test-transfer","click-dialog","click-dialog-2","click-widget","focus-text-2","click-checkboxes-transfer","click-checkboxes-large","click-checkboxes-soft"};
static int ascii(const uint32_t *s,unsigned cap){for(unsigned i=0;i<cap;i++){if(!s[i])return 1;if(s[i]<32||s[i]>126)return 0;}return 0;}
static int valid(const uint32_t *r){
 if(r[0]!=2||r[1]>=10||r[2]>2)return -1;if(r[2]==1)return r[6]>1?-1:0;
 if(r[9]>2||r[12]!=(r[1]==8?20000u:10000u)||r[32]>2||r[33]<1||r[33]>16||r[34]>r[33]||r[8]<r[35]||!ascii(r+768,256))return -1;
 unsigned boxes=0;for(unsigned i=0;i<r[33];i++){const uint32_t *n=r+64+i*40;if(n[0]>6||n[1]>1||n[2]>1||!ascii(n+4,28)||n[34]>10000||n[35]>10000)return -1;boxes+=n[0]==2;}
 if(r[32]==2&&!boxes)return -1;return 0;
}
static int text(WFView *v,const uint32_t *s,unsigned cap,WFText *out){char buf[256];unsigned n=0;for(;n<cap&&s[n];n++)buf[n]=(char)s[n];return wf_text_add(v,buf,n,out);}
static int observe(const uint32_t *r,WFView *v){if(valid(r)||r[2]==1)return -1;wf_view_init(v,r[8],r[12]);if(text(v,r+768,256,&v->instruction))return -1;
 for(unsigned i=0;i<r[33];i++){const uint32_t *n=r+64+i*40;WFNode *o=v->nodes+v->count++;o->ref=i+1;o->role=n[0]==6?WF_TEXTAREA:n[0];o->flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE;if(n[1])o->flags|=WF_CHECKED;if(r[34]==i+1)o->flags|=WF_FOCUSED;o->x=(float)(int32_t)n[32];o->y=(float)(int32_t)n[33];o->width=n[34];o->height=n[35];if(text(v,n+4,28,&o->name)||wf_text_add(v,"",0,&o->value))return -1;
 }return 0;
}
static int action(uint32_t *r,const WFAction *a){if(valid(r)||a->elapsed_ms<r[8]||a->text_length||(a->kind!=WF_WAIT&&a->kind!=WF_CLICK)|| (a->kind==WF_CLICK&&(!a->target||a->target>r[33])))return -1;r[2]=2;r[4]=a->kind;r[5]=a->target;r[8]=a->elapsed_ms;return 0;}
static const WFFamily api={2,ROW,8,10,"click",tasks,valid,family_click_batch,observe,action};
WF_EXPORT const WFFamily *webnav_family_v2(void){return &api;}
