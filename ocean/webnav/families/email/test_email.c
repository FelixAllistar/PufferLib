#include "../common/family_api.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

#define ROW 8192u
void family_email_batch(uint32_t *);
extern const WFFamily *webnav_family_v2(void);
static const WFFamily *api;
static uint32_t *rows;
static unsigned cases;

static uint32_t *lane(unsigned i){return rows+i*ROW;}
static float as_float(uint32_t bits){float value;memcpy(&value,&bits,sizeof value);return value;}
static void expect_reward(float expected){
    float actual=as_float(lane(0)[11]);
    float distance=actual>expected?actual-expected:expected-actual;
    assert(distance<0.00001f);
}
static void batch(void){api->batch(rows);}
static void reset(unsigned task,unsigned seed){
    for(unsigned i=0;i<4;i++){
        uint32_t *r=lane(i);memset(r,0,ROW*sizeof *r);
        r[0]=WF_ABI_VERSION;r[1]=task;r[2]=WF_RESET;r[3]=seed+i;
    }
    batch();
    for(unsigned i=0;i<4;i++)assert(!api->validate(lane(i)));
}
static void send(unsigned kind,unsigned ref,unsigned ms,const char *text){
    WFAction a={.kind=kind,.target=ref,.elapsed_ms=ms,.text=text,
                .text_length=text?strlen(text):0u};
    assert(!api->action(lane(0),&a));batch();assert(!api->validate(lane(0)));
}
static const WFNode *find(const WFView *v,unsigned ref){
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].ref==ref)return v->nodes+i;
    return NULL;
}
static WFView view(void){
    WFView v;assert(!api->observe(lane(0),&v));
    assert(wf_text_get(&v,v.instruction));
    for(unsigned i=0;i<v.count;i++){
        assert(wf_text_get(&v,v.nodes[i].name));
        assert(wf_text_get(&v,v.nodes[i].value));
    }
    return v;
}
static unsigned target_from_instruction(const WFView *v){
    const char *query=wf_text_get(v,v->instruction);assert(query);
    const char *begin=NULL,*end=NULL;
    if((begin=strstr(query,"Find the email by "))){
        begin+=18;end=strstr(begin," and ");
    }else if((begin=strstr(query,"Reply to "))){
        begin+=9;end=strstr(begin,"'s email");
    }else if((begin=strstr(query,"Forward the email from "))){
        begin+=23;end=strstr(begin," to ");
    }else if((begin=strstr(query,"Delete the email from "))){
        begin+=22;end=strchr(begin,'.');
    }else if((begin=strstr(query,"Mark the email from "))){
        begin+=20;end=strstr(begin," as important");
    }
    assert(begin&&end&&end>begin);
    char wanted[64];size_t n=(size_t)(end-begin);assert(n<sizeof wanted);
    memcpy(wanted,begin,n);wanted[n]=0;
    unsigned found=0;
    for(unsigned i=0;i<v->count;i++){
        const WFNode *n=v->nodes+i;
        if(n->ref<10u||n->ref>=106u||(n->ref-10u)%8u)continue;
        const char *name=wf_text_get(v,n->name);assert(name);
        if(!strcmp(wanted,name)){
            assert(!found);found=n->ref;
        }
    }
    assert(found);return found;
}
static unsigned action_from_instruction(const char *q){
    if(strstr(q,"trash icon")||strstr(q,"Delete the email"))return 2u;
    if(strstr(q,"star icon")||strstr(q,"Mark the email"))return 3u;
    if(strstr(q,"reply")||strstr(q,"Reply to"))return 0u;
    assert(strstr(q,"forward")||strstr(q,"Forward"));return 1u;
}
static void quoted_reply(const char *query,char out[160]){
    const char *a=strchr(query,'"');assert(a);const char *b=strchr(a+1,'"');assert(b);
    size_t n=(size_t)(b-a-1);assert(n<160u);memcpy(out,a+1,n);out[n]=0;
}
static void forward_recipient(const char *query,char out[64]){
    const char *p=strstr(query," to ");assert(p);p+=4;
    const char *end=strchr(p,'.');assert(end&&end>p);
    size_t n=(size_t)(end-p);assert(n<64u);memcpy(out,p,n);out[n]=0;
}
static void solve(unsigned task,unsigned seed){
    reset(task,seed);WFView first=view();
    const char *query=wf_text_get(&first,first.instruction);assert(query);
    unsigned target=target_from_instruction(&first),action=action_from_instruction(query);
    uint32_t lane1[ROW];memcpy(lane1,lane(1),sizeof lane1);
    if(action==2u)send(WF_CLICK,target+2u,100,NULL);
    else if(action==3u)send(WF_CLICK,target+1u,100,NULL);
    else {
        send(WF_CLICK,target,100,NULL);
        assert(lane(0)[33]==2u);
        if(action==0u){
            char reply[160];quoted_reply(query,reply);
            send(WF_CLICK,113,200,NULL);send(WF_CLICK,117,300,NULL);
            send(WF_INSERT,0,400,reply);send(WF_CLICK,116,500,NULL);
        }else{
            char recipient[64];forward_recipient(query,recipient);
            send(WF_CLICK,114,200,NULL);send(WF_CLICK,120,300,NULL);
            send(WF_INSERT,0,400,recipient);send(WF_CLICK,119,500,NULL);
        }
    }
    assert(lane(0)[9]==WF_TERMINAL&&lane(0)[10]==1065353216u);
    expect_reward(1.0f-(float)(action==2u||action==3u?100u:500u)/30000.0f);
    assert(!memcmp(lane1,lane(1),sizeof lane1));
    uint32_t before[ROW];memcpy(before,lane(0),sizeof before);
    send(WF_WAIT,0,600,NULL);
    assert(!memcmp(before+9,lane(0)+9,3u*sizeof *before));
    assert(!memcmp(before+32,lane(0)+32,(5500u-32u)*sizeof *before));
    cases++;
}
static void failures(unsigned task,unsigned seed){
    reset(task,seed);WFView v=view();unsigned target=target_from_instruction(&v);
    unsigned action=action_from_instruction(wf_text_get(&v,v.instruction));
    unsigned wrong=target==10u?18u:10u;
    if(action==2u)send(WF_CLICK,wrong+2u,100,NULL);
    else if(action==3u)send(WF_CLICK,wrong+1u,100,NULL);
    else if(action==0u){
        send(WF_CLICK,target,100,NULL);send(WF_CLICK,113,200,NULL);
        send(WF_CLICK,117,300,NULL);send(WF_INSERT,0,400,"wrong");
        send(WF_CLICK,116,500,NULL);
    }else{
        send(WF_CLICK,target,100,NULL);send(WF_CLICK,114,200,NULL);
        send(WF_CLICK,120,300,NULL);send(WF_INSERT,0,400,"wrong");
        send(WF_CLICK,119,500,NULL);
    }
    assert(lane(0)[9]==WF_TERMINAL&&lane(0)[10]==3212836864u);
    expect_reward(-1.0f);
    cases++;
    reset(task,seed+97u);send(WF_WAIT,0,30000,NULL);
    assert(lane(0)[9]==WF_TIMEOUT&&lane(0)[10]==3212836864u);
    expect_reward(-1.0f);
    cases++;
}
static void navigation(unsigned task,unsigned seed){
    reset(task,seed);WFView original=view();
    unsigned target=target_from_instruction(&original);
    const WFNode *n=find(&original,target);assert(n);
    char name[64];const char *s=wf_text_get(&original,n->name);assert(s);
    snprintf(name,sizeof name,"%s",s);
    send(WF_CLICK,1,100,NULL);
    assert(lane(0)[33]==1u);
    send(WF_INSERT,0,200,name);
    WFView searched=view();assert(find(&searched,target+3u));
    /* The search-result icon bubbles to the thread handler. */
    send(WF_CLICK,target+4u,300,NULL);
    assert(lane(0)[33]==2u&&lane(0)[34]==(target-10u)/8u+1u);
    send(WF_CLICK,112,400,NULL);
    assert(lane(0)[33]==0u);
    send(WF_CLICK,1,500,NULL);
    assert(lane(0)[33]==1u&&lane(0)[36]==strlen(name));
    send(WF_CLICK,3,600,NULL);
    assert(lane(0)[33]==0u&&lane(0)[36]==0u);
    send(WF_CLICK,1,700,NULL);
    WFView stale=view();assert(find(&stale,target+3u));
    cases++;
}
static void irrelevant_icons(void){
    reset(7,999);WFView v=view();unsigned target=target_from_instruction(&v);
    send(WF_CLICK,target+1u,100,NULL);
    assert(lane(0)[9]==WF_RUNNING&&lane(0)[33]==0u);
    WFView after=view();const WFNode *star=find(&after,target+1u);
    assert(star&&(star->flags&WF_CHECKED));
    send(WF_CLICK,target+2u,200,NULL);
    assert(lane(0)[9]==WF_RUNNING&&lane(0)[33]==0u);
    cases++;
}
int main(void){
    prctl(PR_SET_DUMPABLE,0);
    api=webnav_family_v2();assert(api&&api->task_count==10u&&api->row_words==ROW&&api->batch_lanes==4u);
    rows=calloc(ROW*4u,sizeof *rows);assert(rows);
    for(unsigned task=0;task<10;task++)for(unsigned seed=0;seed<16;seed++){
        solve(task,1000u+seed);failures(task,5000u+seed);navigation(task,9000u+seed);
    }
    irrelevant_icons();free(rows);
    printf("PASS: %u independent public-query email cases\n",cases);return 0;
}
