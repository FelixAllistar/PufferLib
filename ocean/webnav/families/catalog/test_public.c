#include "../common/family_api.h"
#include "public_controller.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

static int run(const WFFamily *f,uint32_t *rows,unsigned task,unsigned seed){
    memset(rows,0xa5,f->row_words*f->batch_lanes*sizeof *rows);
    for(unsigned lane=0;lane<f->batch_lanes;lane++){
        uint32_t *r=rows+lane*f->row_words;
        r[WF_VERSION]=WF_ABI_VERSION;r[WF_TASK]=task;
        r[WF_OP]=WF_RESET;r[WF_SEED]=seed+lane;
    }
    f->batch(rows);signal(SIGABRT,SIG_DFL);
    for(unsigned lane=0;lane<f->batch_lanes;lane++){
        uint32_t *r=rows+lane*f->row_words;WFView before,after;
        assert(!f->validate(r)&&!f->observe(r,&before));
        unsigned saved=r[33],changed=task==0u?1u+(saved%5u):
            task==1u?(saved+1u)%12u:(saved+1u)%9u;
        if(task==1u&&changed==r[34])changed=(changed+1u)%12u;
        assert(changed!=saved);r[33]=changed;
        int valid=f->validate(r),projected=valid?-1:f->observe(r,&after);
        int altered=projected?-1:memcmp(&before,&after,sizeof before);
        if(valid||projected||altered)
            fprintf(stderr,"private-view failure task=%u seed=%u lane=%u old=%u new=%u second=%u validate=%d observe=%d altered=%d\n",
                task,seed,lane,saved,changed,r[34],valid,projected,altered);
        assert(!valid&&!projected&&!altered);
        r[33]=saved;
    }
    for(unsigned step=0;step<64u;step++){
        unsigned running=0;
        for(unsigned lane=0;lane<f->batch_lanes;lane++){
            uint32_t *r=rows+lane*f->row_words;WFView v;
            assert(!f->observe(r,&v));WFAction a;
            char scratch[128];
            if(r[WF_STATUS]==WF_RUNNING){
                int rc=catalog_public_next(&v,scratch,sizeof scratch,&a);
                if(rc){
                    const char *q=wf_text_get(&v,v.instruction);
                    fprintf(stderr,"public parse failure task=%u seed=%u lane=%u step=%u query=%s\n",
                        task,seed,lane,step,q?q:"<invalid>");
                }
                assert(!rc);running++;
            }else a=(WFAction){.kind=WF_WAIT,.elapsed_ms=r[WF_ELAPSED]+100u};
            assert(!f->action(r,&a));
        }
        if(!running)break;
        f->batch(rows);signal(SIGABRT,SIG_DFL);
    }
    for(unsigned lane=0;lane<f->batch_lanes;lane++){
        const uint32_t *r=rows+lane*f->row_words;
        if(r[WF_STATUS]!=WF_TERMINAL||r[WF_RAW_REWARD]!=1065353216u){
            WFView v;assert(!f->observe(r,&v));
            const char *q=wf_text_get(&v,v.instruction);
            fprintf(stderr,"public failure task=%u seed=%u lane=%u status=%u raw=%08x elapsed=%u page/mode=%u target=%u aux=%u query=%s\n",
                task,seed,lane,r[WF_STATUS],r[WF_RAW_REWARD],r[WF_ELAPSED],
                r[32],r[33],r[34],q?q:"<invalid>");
        }
        assert(r[WF_STATUS]==WF_TERMINAL&&r[WF_RAW_REWARD]==1065353216u);
    }
    return (int)f->batch_lanes;
}
int main(void){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==3u);
    uint32_t *rows=malloc(f->row_words*f->batch_lanes*sizeof *rows);assert(rows);
    unsigned solved=0;
    for(unsigned task=0;task<3u;task++)
        for(unsigned seed=0;seed<64u;seed++)solved+=(unsigned)run(f,rows,task,seed);
    free(rows);printf("PASS: %u public-only generated catalog episodes\n",solved);
    return 0;
}
