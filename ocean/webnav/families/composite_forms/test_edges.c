#include "../common/family_api.h"
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

static float real(uint32_t bits){float f;memcpy(&f,&bits,4);return f;}
static void reset(const WFFamily *f,uint32_t *rows,unsigned task,unsigned seed){
    for(unsigned lane=0;lane<f->batch_lanes;lane++){
        uint32_t *r=rows+lane*f->row_words;
        memset(r,0xf3,f->row_words*sizeof *r);
        r[WF_VERSION]=2;r[WF_TASK]=task;r[WF_OP]=WF_RESET;r[WF_SEED]=seed;
    }
    f->batch(rows);signal(SIGABRT,SIG_DFL);
    assert(!f->validate(rows));
}
static void apply(const WFFamily *f,uint32_t *rows,unsigned kind,
                  unsigned target,unsigned arg0,const char *text,unsigned now){
    WFAction a={.kind=kind,.target=target,.arg0=arg0,.elapsed_ms=now,
                .text=text,.text_length=text?strlen(text):0};
    assert(!f->action(rows,&a));
    for(unsigned lane=1;lane<f->batch_lanes;lane++)
        memcpy(rows+lane*f->row_words,rows,f->row_words*sizeof *rows);
    f->batch(rows);signal(SIGABRT,SIG_DFL);
    assert(!f->validate(rows));
}
static void goal(char *out,const uint32_t *r,unsigned field){
    assert(r[44+field]<=63u);
    for(unsigned i=0;i<r[44+field];i++)out[i]=(char)r[1024+128*field+i];
    out[r[44+field]]=0;
}
static void expect(const uint32_t *r,unsigned status,float raw){
    assert(r[WF_STATUS]==status&&fabsf(real(r[WF_RAW_REWARD])-raw)<1e-6f);
}
static void private_noninterference(const WFFamily *f,uint32_t *r,unsigned task){
    WFView first,second;assert(!f->observe(r,&first));
    uint32_t old34=r[34],old35=r[35],oldgoal=r[1024];
    if(task==0)r[34]=(old34+1)%21;
    else if(task==1)r[34]=(old34%3)+1;
    else if(task==2)r[34]=(old34%5)+1;
    else r[1024]=oldgoal=='x'?'y':'x';
    assert(!f->validate(r)&&!f->observe(r,&second));
    assert(!memcmp(&first,&second,sizeof first));
    r[34]=old34;r[35]=old35;r[1024]=oldgoal;
}
int main(void){
    assert(!prctl(PR_SET_DUMPABLE,0,0,0,0));
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==5);
    uint32_t *rows=calloc((size_t)f->row_words*f->batch_lanes,sizeof *rows);
    assert(rows);
    for(unsigned task=0;task<5;task++)for(unsigned seed=0;seed<16;seed++){
        reset(f,rows,task,seed);private_noninterference(f,rows,task);
        unsigned deadline=rows[WF_DEADLINE];
        apply(f,rows,WF_WAIT,0,0,NULL,deadline-1);expect(rows,WF_RUNNING,0.0f);
        apply(f,rows,WF_WAIT,0,0,NULL,deadline);expect(rows,WF_TIMEOUT,-1.0f);
        unsigned before=rows[32],raw=rows[WF_RAW_REWARD];
        apply(f,rows,WF_WAIT,0,0,NULL,deadline+1);
        assert(rows[32]==before&&rows[WF_RAW_REWARD]==raw);

        reset(f,rows,task,seed);
        unsigned submit=task==0?5:task==1?7:task==2?2:4;
        apply(f,rows,WF_CLICK,submit,0,NULL,250);
        expect(rows,WF_TERMINAL,-1.0f);
        before=rows[32];raw=rows[WF_RAW_REWARD];
        apply(f,rows,WF_WAIT,0,0,NULL,500);
        assert(rows[32]==before&&rows[WF_RAW_REWARD]==raw);
    }
    for(unsigned seed=0;seed<16;seed++){
        reset(f,rows,1,seed);
        unsigned other=rows[34]==1?2:1,box=rows[35];char wanted[64];
        goal(wanted,rows,box-1);
        apply(f,rows,WF_CLICK,other,0,NULL,250);
        apply(f,rows,WF_INSERT,box+3,0,wanted,500);
        unsigned extra=box==1?5:4;
        apply(f,rows,WF_INSERT,extra,0,"X",750);
        apply(f,rows,WF_CLICK,7,0,NULL,1000);expect(rows,WF_TERMINAL,-1.0f);

        reset(f,rows,2,seed);
        apply(f,rows,WF_SELECT_OPTION,1,rows[34],NULL,250);
        unsigned wrong=rows[35]==1?3:2;
        apply(f,rows,WF_CLICK,wrong,0,NULL,500);expect(rows,WF_TERMINAL,-1.0f);

        for(unsigned task=3;task<=4;task++){
            reset(f,rows,task,seed);
            char genre[64],director[64],year[64],modified[64];
            goal(genre,rows,0);goal(director,rows,1);goal(year,rows,2);
            snprintf(modified,sizeof modified," %s ",genre);
            for(char *p=modified;*p;p++)*p=(char)toupper((unsigned char)*p);
            apply(f,rows,WF_INSERT,1,0,"wrong",250);
            apply(f,rows,WF_INSERT,1,0,modified,500);
            snprintf(modified,sizeof modified," %s ",director);
            for(char *p=modified;*p;p++)*p=(char)toupper((unsigned char)*p);
            apply(f,rows,WF_INSERT,2,0,modified,750);
            apply(f,rows,WF_INSERT,3,0,year,1000);
            apply(f,rows,WF_CLICK,4,0,NULL,1250);expect(rows,WF_TERMINAL,1.0f);

            reset(f,rows,task,seed);
            apply(f,rows,WF_INSERT,1,0,genre,250);
            apply(f,rows,WF_INSERT,2,0,director,500);
            snprintf(modified,sizeof modified," %s",year);
            apply(f,rows,WF_INSERT,3,0,modified,750);
            apply(f,rows,WF_CLICK,4,0,NULL,1000);expect(rows,WF_TERMINAL,-1.0f);
        }
    }
    free(rows);
    puts("PASS: independent empty/wrong/extra-field, replacement, case+trim, exact-year, private-view, deadline and absorption edges");
}
