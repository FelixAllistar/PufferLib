/* Loader fixture only; this is not a task implementation or benchmark port. */
#include "../common/family_api.h"
#include <string.h>
#ifndef MOCK_NAME
#define MOCK_NAME "mock-a"
#endif
static const char *names[]={MOCK_NAME};
static unsigned counter;
static int valid(const uint32_t *r){return r[WF_VERSION]==2&&r[WF_TASK]==0&&r[WF_OP]<=2?0:-1;}
static void batch(uint32_t *w){for(unsigned i=0;i<2;i++){uint32_t *r=w+32*i;if(r[WF_OP]==WF_RESET){r[WF_STATUS]=0;r[WF_DEADLINE]=15000;r[WF_ERROR]=0;r[WF_RAW_REWARD]=r[WF_TIMED_REWARD]=0;}else if(r[WF_OP]==WF_STEP){r[14]=++counter;}r[WF_OP]=WF_OBSERVE;}}
static int observe(const uint32_t *r,WFView *v){wf_view_init(v,r[WF_ELAPSED],r[WF_DEADLINE]);return wf_text_add(v,MOCK_NAME,strlen(MOCK_NAME),&v->instruction);}
static int action(uint32_t *r,const WFAction *a){r[WF_OP]=WF_STEP;r[WF_ACTION]=a->kind;r[WF_ELAPSED]=a->elapsed_ms;return 0;}
static const WFFamily api={2,32,2,1,MOCK_NAME,names,valid,batch,observe,action};
WF_EXPORT const WFFamily *webnav_family_v2(void){return &api;}
