#include "../common/family_api.h"
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

static float reward(uint32_t bits){float f;memcpy(&f,&bits,sizeof f);return f;}
static void reset(const WFFamily *f,uint32_t *rows,unsigned task,unsigned seed){
    for(unsigned lane=0;lane<f->batch_lanes;lane++){
        uint32_t *r=rows+(size_t)lane*f->row_words;
        memset(r,0x95,f->row_words*sizeof *r);
        r[WF_VERSION]=2;r[WF_TASK]=task;r[WF_OP]=WF_RESET;r[WF_SEED]=seed;
    }
    f->batch(rows);signal(SIGABRT,SIG_DFL);assert(!f->validate(rows));
}
static void act(const WFFamily *f,uint32_t *rows,unsigned kind,
                unsigned target,unsigned elapsed,const char *text){
    WFAction a={.kind=kind,.target=target,.elapsed_ms=elapsed,
        .text=text,.text_length=text?strlen(text):0u};
    assert(!f->action(rows,&a));
    for(unsigned lane=1;lane<f->batch_lanes;lane++)
        memcpy(rows+(size_t)lane*f->row_words,rows,f->row_words*sizeof *rows);
    f->batch(rows);signal(SIGABRT,SIG_DFL);assert(!f->validate(rows));
}
static int matches(const uint32_t *r,unsigned i){
    const uint32_t *s=r+128u+i*6u;
    return (!r[33]||r[33]==s[3]+1u)&&
        (!r[34]||r[34]==s[2]+1u)&&
        (!r[35]||r[35]==s[4]||r[35]==s[5]);
}
int main(void){
    assert(!prctl(PR_SET_DUMPABLE,0,0,0,0));
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==9u);
    uint32_t *rows=calloc((size_t)f->row_words*f->batch_lanes,sizeof *rows);
    assert(rows);unsigned zero=0,nonzero=0,shape=0,choice=0,addition=0;
    for(unsigned task=4u;task<=8u;task++)for(unsigned seed=0;seed<64u;seed++){
        reset(f,rows,task,seed);WFView view;
        assert(!f->observe(rows,&view));
        if(seed==0u){
            uint32_t dirty[2048];
            memcpy(dirty,rows,sizeof dirty);
            for(unsigned lane=0;lane<f->batch_lanes;lane++){
                uint32_t *r=rows+(size_t)lane*f->row_words;
                memset(r,0,f->row_words*sizeof *r);
                r[WF_VERSION]=2u;r[WF_TASK]=task;r[WF_OP]=WF_RESET;
                r[WF_SEED]=seed;
            }
            f->batch(rows);signal(SIGABRT,SIG_DFL);
            assert(!memcmp(dirty,rows,sizeof dirty));
        }
        if(task==4u||task==5u){
            unsigned count=0;
            for(unsigned i=0;i<rows[32];i++)count+=matches(rows,i);
            zero+=task==5u&&count==0u;
            nonzero+=task==5u&&count>0u;
            assert(count==rows[36]);
            assert(view.count==rows[32]+1u+(task==5u?5u:0u));
            if(task==4u){
                unsigned chosen=0;
                while(chosen<rows[32]&&!matches(rows,chosen))chosen++;
                assert(chosen<rows[32]);
                act(f,rows,WF_CLICK,10u+chosen,100u,NULL);
                assert(rows[WF_STATUS]==WF_TERMINAL&&reward(rows[WF_RAW_REWARD])==1.0f);
                reset(f,rows,task,seed);
                act(f,rows,WF_CLICK,1u,100u,NULL);
                assert(rows[WF_STATUS]==WF_TERMINAL&&reward(rows[WF_RAW_REWARD])==-1.0f);
            }else{
                unsigned chosen=0;
                while(chosen<5u&&rows[64u+chosen]!=count)chosen++;
                assert(chosen<5u);
                act(f,rows,WF_CLICK,40u+chosen,100u,NULL);
                assert(rows[WF_STATUS]==WF_TERMINAL&&reward(rows[WF_RAW_REWARD])==1.0f);
                reset(f,rows,task,seed);
                act(f,rows,WF_CLICK,40u+(chosen+1u)%5u,100u,NULL);
                assert(rows[WF_STATUS]==WF_TERMINAL&&reward(rows[WF_RAW_REWARD])==-1.0f);
            }
            shape++;
        }else if(task==6u||task==7u){
            unsigned chosen;
            if(task==6u){
                assert(view.count==6u&&view.nodes[0].role==WF_CANVAS);
                unsigned vertices=0;
                const char *path=wf_text_get(&view,view.nodes[0].value);
                assert(path);
                for(const char *p=path;*p;p++)vertices+=*p==',';
                assert(vertices>=3u&&vertices<=7u);chosen=vertices-3u;
            }else{
                assert(view.count==6u);
                const char *tag=wf_text_get(&view,view.nodes[0].name);
                assert(tag);
                if(!strcmp(tag,"rect"))chosen=0u;
                else if(!strcmp(tag,"circle"))chosen=1u;
                else if(!strcmp(tag,"polygon"))chosen=2u;
                else if(!strcmp(tag,"text")){
                    const char *value=wf_text_get(&view,view.nodes[0].value);
                    assert(value&&strlen(value)>=6u);
                    chosen=value[5]>='0'&&value[5]<='9'?4u:3u;
                }else abort();
            }
            act(f,rows,WF_CLICK,10u+chosen,100u,NULL);
            assert(rows[WF_STATUS]==WF_TERMINAL&&reward(rows[WF_RAW_REWARD])==1.0f);
            reset(f,rows,task,seed);
            act(f,rows,WF_CLICK,10u+(chosen+1u)%5u,100u,NULL);
            assert(rows[WF_STATUS]==WF_TERMINAL&&reward(rows[WF_RAW_REWARD])==-1.0f);
            choice++;
        }else{
            unsigned left=0,right=0;
            for(unsigned i=0;i<view.count;i++){
                const char *name=wf_text_get(&view,view.nodes[i].name);
                assert(name);
                left+=!strcmp(name,"Left blue block");
                right+=!strcmp(name,"Right blue block");
            }
            assert(left>=1u&&left<=10u&&right>=1u&&right<=10u);
            char exact[4],leading[5];
            snprintf(exact,sizeof exact,"%u",left+right);
            snprintf(leading,sizeof leading,"0%u",left+right);
            act(f,rows,WF_CLICK,1u,100u,NULL);
            act(f,rows,WF_INSERT,1u,200u,leading);
            act(f,rows,WF_CLICK,2u,300u,NULL);
            assert(rows[WF_STATUS]==WF_TERMINAL&&rows[37]==0u&&
                   reward(rows[WF_RAW_REWARD])==-1.0f);
            reset(f,rows,task,seed);
            act(f,rows,WF_CLICK,1u,100u,NULL);
            act(f,rows,WF_INSERT,1u,200u,exact);
            act(f,rows,WF_CLICK,2u,300u,NULL);
            assert(rows[WF_STATUS]==WF_TERMINAL&&rows[37]==0u&&
                   reward(rows[WF_RAW_REWARD])==1.0f);
            reset(f,rows,task,seed);
            act(f,rows,WF_CLICK,2u,15000u,NULL);
            assert(rows[WF_STATUS]==WF_TIMEOUT);
            addition++;
        }
    }
    assert(shape==128u&&choice==128u&&addition==64u&&zero&&nonzero);
    free(rows);
    puts("PASS: 320 shape, canvas, and addition cases; public answers, wrong choices, zero counts, exact text");
}
