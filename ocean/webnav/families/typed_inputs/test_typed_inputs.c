#include "../common/family_api.h"
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

#define ROW 1024u
#define LANES 4u
static float real(uint32_t bits){float x;memcpy(&x,&bits,4);return x;}

static void reset(const WFFamily *f,uint32_t *rows,unsigned task,unsigned seed){
    for(unsigned lane=0;lane<LANES;lane++){
        uint32_t *r=rows+lane*ROW;
        r[WF_VERSION]=WF_ABI_VERSION;r[WF_TASK]=task;
        r[WF_OP]=WF_RESET;r[WF_SEED]=seed+lane;
    }
    f->batch(rows);
    signal(SIGABRT,SIG_DFL);
    for(unsigned lane=0;lane<LANES;lane++)assert(!f->validate(rows+lane*ROW));
}

static void action(const WFFamily *f,uint32_t *rows,unsigned kind,
                   unsigned target,const char *text,unsigned now){
    WFAction a={.kind=kind,.target=target,.elapsed_ms=now,
                .text=text,.text_length=text?strlen(text):0};
    assert(!f->action(rows,&a));
    f->batch(rows);
    signal(SIGABRT,SIG_DFL);
    assert(!f->validate(rows));
}

static void public_answer(const WFView *v,char *out,size_t cap){
    const char *query=wf_text_get(v,v->instruction);
    unsigned month=0,day=0,year=0;
    assert(query&&sscanf(query,"Enter %u/%u/%u as the date",&month,&day,&year)==3);
    assert(month>=1&&month<=12&&day>=1&&day<=31&&year>=2010&&year<=2019);
    assert(snprintf(out,cap,"%04u-%02u-%02u",year,month,day)==10);
}

static void public_time(const WFView *v,char *out,size_t cap){
    const char *query=wf_text_get(v,v->instruction);
    unsigned hour=0,minute=0;char meridian[3]={0};
    assert(query&&sscanf(query,"Enter %u:%u %2s as the time",
                         &hour,&minute,meridian)==3);
    assert(hour>=1&&hour<=12&&minute<=59);
    assert(!strcmp(meridian,"AM")||!strcmp(meridian,"PM"));
    hour%=12;if(!strcmp(meridian,"PM"))hour+=12;
    assert(snprintf(out,cap,"%02u:%02u",hour,minute)==5);
}

static void public_unicode(const WFView *v,char *out,size_t cap){
    const char *query=wf_text_get(v,v->instruction);
    assert(query&&!strncmp(query,"Click on the \"",14));
    const char *begin=query+14,*end=strchr(begin,'"');
    assert(end&&(size_t)(end-begin)<cap);
    memcpy(out,begin,(size_t)(end-begin));out[end-begin]=0;
    assert(!strcmp(end,"\" button."));
}

static unsigned matching_button(const WFView *v,const char *wanted,int match){
    for(unsigned i=0;i<v->count;i++){
        const WFNode *n=v->nodes+i;
        const char *name=wf_text_get(v,n->name);
        if(n->role==WF_BUTTON&&name&&(!strcmp(name,wanted)==!!match))
            return n->ref;
    }
    return 0;
}

int main(void){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    const WFFamily *f=webnav_family_v2();
    assert(f&&f->task_count==3&&f->row_words==ROW&&f->batch_lanes==LANES);
    uint32_t *rows=calloc(LANES*ROW,sizeof *rows);assert(rows);
    WFView before,after;
    char answer[16];
    for(unsigned seed=1;seed<1400;seed+=97){
        reset(f,rows,0,seed);
        assert(!f->observe(rows,&before));
        assert(before.count==2&&before.deadline_ms==20000);
        public_answer(&before,answer,sizeof answer);
        uint32_t changed[ROW];memcpy(changed,rows,sizeof changed);
        changed[32]=changed[32]==2019?2018:changed[32]+1;
        assert(!f->observe(changed,&after));
        assert(!memcmp(&before,&after,sizeof before));
        action(f,rows,WF_INSERT,1,answer,100);
        assert(!f->observe(rows,&after));
        assert(!strcmp(wf_text_get(&after,after.nodes[0].value),answer));
        action(f,rows,WF_CLICK,2,NULL,200);
        assert(rows[WF_STATUS]==WF_TERMINAL&&real(rows[WF_RAW_REWARD])==1.0f);
        assert(fabsf(real(rows[WF_TIMED_REWARD])-0.99f)<1e-5f);
    }
    reset(f,rows,0,11);
    action(f,rows,WF_INSERT,1,"2023-02-29",100);
    assert(!f->observe(rows,&after));
    assert(!strcmp(wf_text_get(&after,after.nodes[0].value),""));
    action(f,rows,WF_INSERT,1,"2024-02-29",200);
    assert(!f->observe(rows,&after));
    assert(!strcmp(wf_text_get(&after,after.nodes[0].value),"2024-02-29"));
    action(f,rows,WF_CLICK,2,NULL,300);
    assert(rows[WF_STATUS]==WF_TERMINAL&&real(rows[WF_RAW_REWARD])==-1.0f);
    reset(f,rows,0,12);
    action(f,rows,WF_WAIT,0,NULL,20000);
    assert(rows[WF_STATUS]==WF_TIMEOUT&&real(rows[WF_RAW_REWARD])==-1.0f);

    unsigned time_variation=0,unicode_variation=0;
    char first_time[16]="";
    for(unsigned seed=5;seed<1000;seed+=37){
        reset(f,rows,1,seed);
        assert(!f->observe(rows,&before));
        assert(before.count==2&&before.deadline_ms==20000);
        public_time(&before,answer,sizeof answer);
        if(!first_time[0])strcpy(first_time,answer);
        else time_variation|=strcmp(first_time,answer)!=0;
        uint32_t changed[ROW];memcpy(changed,rows,sizeof changed);
        changed[32]=(changed[32]+1)%24;
        assert(!f->observe(changed,&after));
        assert(!memcmp(&before,&after,sizeof before));
        action(f,rows,WF_INSERT,1,answer,100);
        assert(!f->observe(rows,&after));
        assert(!strcmp(wf_text_get(&after,after.nodes[0].value),answer));
        action(f,rows,WF_CLICK,2,NULL,200);
        assert(rows[WF_STATUS]==WF_TERMINAL&&real(rows[WF_RAW_REWARD])==1.0f);
    }
    assert(time_variation);
    reset(f,rows,1,19);
    action(f,rows,WF_INSERT,1,"24:00",100);
    assert(!f->observe(rows,&after));
    assert(!strcmp(wf_text_get(&after,after.nodes[0].value),""));
    action(f,rows,WF_INSERT,1,"00:00",200);
    assert(!f->observe(rows,&after));
    assert(!strcmp(wf_text_get(&after,after.nodes[0].value),"00:00"));

    char first_label[64]="",wanted[64];int found_wrong=0;
    for(unsigned seed=1;seed<1000;seed+=29){
        reset(f,rows,2,seed);
        assert(!f->observe(rows,&before));
        assert(before.count==6&&before.deadline_ms==10000);
        public_unicode(&before,wanted,sizeof wanted);
        if(!first_label[0])strcpy(first_label,wanted);
        else unicode_variation|=strcmp(first_label,wanted)!=0;
        for(unsigned i=0;i<before.count;i++){
            assert(wf_text_get(&before,before.nodes[i].name));
            assert(wf_text_get(&before,before.nodes[i].value));
        }
        uint32_t changed[ROW];memcpy(changed,rows,sizeof changed);
        changed[32]=(changed[32]+1)%6;
        assert(!f->observe(changed,&after));
        assert(!memcmp(&before,&after,sizeof before));
        unsigned right=matching_button(&before,wanted,1);
        assert(right>=3&&right<=8);
        unsigned wrong=matching_button(&before,wanted,0);
        if(wrong&&!found_wrong){
            action(f,rows,WF_CLICK,wrong,NULL,100);
            assert(rows[WF_STATUS]==WF_TERMINAL&&real(rows[WF_RAW_REWARD])==-1.0f);
            found_wrong=1;
        }else{
            action(f,rows,WF_CLICK,right,NULL,100);
            assert(rows[WF_STATUS]==WF_TERMINAL&&real(rows[WF_RAW_REWARD])==1.0f);
            assert(fabsf(real(rows[WF_TIMED_REWARD])-0.99f)<1e-5f);
        }
    }
    assert(unicode_variation&&found_wrong);
    reset(f,rows,2,83);
    action(f,rows,WF_WAIT,0,NULL,10000);
    assert(rows[WF_STATUS]==WF_TIMEOUT&&real(rows[WF_RAW_REWARD])==-1.0f);

    uint32_t *fresh=calloc(LANES*ROW,sizeof *fresh);assert(fresh);
    for(unsigned task=0;task<3;task++){
        memset(rows,0xa5,LANES*ROW*sizeof *rows);
        reset(f,rows,task,103+task);
        reset(f,fresh,task,103+task);
        assert(!memcmp(rows,fresh,LANES*ROW*sizeof *rows));
    }
    free(fresh);
    free(rows);puts("typed_inputs three-task source regression passed");return 0;
}
