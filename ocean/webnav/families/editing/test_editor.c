#include "../common/family_api.h"
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

static float real(uint32_t bits){float x;memcpy(&x,&bits,4);return x;}
static int source_ok(const uint32_t *r){
    for(unsigned i=0;i<r[37];i++){
        unsigned value=r[2048u+i],inside=i>=r[32]&&i<r[33];
        if(!inside){if(value)return 0;continue;}
        if(r[34]==3u){
            unsigned color=value>>3u;
            if((value&7u)||!color||(color-1u)/5u!=r[35])return 0;
        }else if(value!=(1u<<r[34]))return 0;
    }
    return 1;
}
static void batch(const WFFamily *f,uint32_t *rows,WFAction a[4],unsigned ms){
    int open=0;WFAction menu[4]={{0}};
    for(unsigned lane=0;lane<4u;lane++){
        const uint32_t *r=rows+lane*f->row_words;
        if(a[lane].kind==WF_CLICK&&a[lane].target>=20u&&!r[42]){
            menu[lane]=(WFAction){.kind=WF_CLICK,.target=5u,.elapsed_ms=ms-1u};
            open=1;
        }else menu[lane]=(WFAction){.kind=WF_WAIT,.elapsed_ms=ms-1u};
    }
    if(open){
        for(unsigned lane=0;lane<4u;lane++){
            int rc=f->action(rows+lane*f->row_words,&menu[lane]);
            if(rc)fprintf(stderr,"editor menu rejected lane=%u elapsed=%u\n",lane,ms-1u);
            assert(!rc);
        }
        f->batch(rows);signal(SIGABRT,SIG_DFL);
        for(unsigned lane=0;lane<4u;lane++)
            if(menu[lane].kind==WF_CLICK)
                assert((rows+lane*f->row_words)[42]==1u);
    }
    for(unsigned lane=0;lane<4u;lane++){
        a[lane].elapsed_ms=ms;
        uint32_t *r=rows+lane*f->row_words;
        int rc=f->action(r,&a[lane]);
        if(rc)fprintf(stderr,"editor action rejected lane=%u kind=%u ref=%u arg0=%u arg1=%u len=%zu elapsed=%u status=%u goal=[%u,%u) style=%u color=%u doclen=%u selection=[%u,%u)\n",
            lane,a[lane].kind,a[lane].target,a[lane].arg0,a[lane].arg1,
            a[lane].text_length,ms,r[WF_STATUS],r[32],r[33],r[34],r[35],
            r[37],r[38],r[39]);
        assert(!rc);
    }
    f->batch(rows);signal(SIGABRT,SIG_DFL);
}
static unsigned correct_ref(const uint32_t *r){
    return r[34]==3u?20u+5u*r[35]:2u+r[34];
}
int main(void){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==5u);
    uint32_t *rows=malloc(f->row_words*f->batch_lanes*sizeof *rows);
    assert(rows);unsigned checked=0u,styles_seen=0u;
    for(unsigned seed=0u;seed<64u;seed++)
    for(unsigned trial=0u;trial<8u;trial++){
        memset(rows,0xa5,f->row_words*f->batch_lanes*sizeof *rows);
        for(unsigned lane=0;lane<4u;lane++){
            uint32_t *r=rows+lane*f->row_words;
            r[WF_VERSION]=2u;r[WF_TASK]=3u;
            r[WF_OP]=WF_RESET;r[WF_SEED]=seed+lane;
        }
        f->batch(rows);signal(SIGABRT,SIG_DFL);
        WFAction actions[4]={{0}};
        for(unsigned lane=0;lane<4u;lane++){
            uint32_t *r=rows+lane*f->row_words;WFView before,after;
            assert(!f->validate(r)&&!f->observe(r,&before));
            assert(r[WF_DEADLINE]==15000u&&r[37]>=3u&&r[8191]==0u);
            assert(r[42]==0u);
            int picker=0,swatches=0;
            for(unsigned i=0;i<before.count;i++){
                picker+=before.nodes[i].ref==5u;
                swatches+=before.nodes[i].ref>=20u&&before.nodes[i].ref<=49u;
            }
            assert(picker==1&&!swatches);
            WFAction hidden={.kind=WF_CLICK,.target=20u,.elapsed_ms=1u};
            assert(f->action(r,&hidden));
            for(unsigned i=0;i<r[37];i++)assert(r[2048u+i]==0u);
            if(!trial)styles_seen|=1u<<r[34];
            unsigned saved=r[34];r[34]=(saved+1u)%4u;
            assert(!f->validate(r)&&!f->observe(r,&after)&&
                   !memcmp(&before,&after,sizeof before));
            r[34]=saved;
        }
        if(trial==7u){
            for(unsigned lane=0;lane<4u;lane++)
                actions[lane]=(WFAction){.kind=WF_CLICK,.target=1u};
            batch(f,rows,actions,100u);
            for(unsigned lane=0;lane<4u;lane++){
                uint32_t *r=rows+lane*f->row_words;
                assert(r[WF_STATUS]==(r[36]?WF_TERMINAL:WF_RUNNING));
                if(!r[36]){
                    WFAction wait={.kind=WF_WAIT,.elapsed_ms=15000u};
                    assert(!f->action(r,&wait));
                }else r[WF_OP]=WF_OBSERVE;
            }
            f->batch(rows);signal(SIGABRT,SIG_DFL);
        }else if(trial==5u){
            for(unsigned lane=0;lane<4u;lane++)
                actions[lane]=(WFAction){.kind=WF_WAIT};
            batch(f,rows,actions,15000u);
        }else{
            for(unsigned lane=0;lane<4u;lane++){
                const uint32_t *r=rows+lane*f->row_words;
                actions[lane]=(WFAction){.kind=WF_SELECT_RANGE,
                    .arg0=r[32],.arg1=r[33]-(trial==2u?1u:0u)};
            }
            batch(f,rows,actions,100u);
            for(unsigned lane=0;lane<4u;lane++){
                const uint32_t *r=rows+lane*f->row_words;
                unsigned ref=correct_ref(r);
                if(trial==1u||trial==4u)
                    ref=r[34]==3u?2u:20u+5u*((r[35]+1u)%6u);
                actions[lane]=(WFAction){.kind=WF_CLICK,.target=ref};
            }
            batch(f,rows,actions,200u);
            if(trial==3u||trial==6u){
                for(unsigned lane=0;lane<4u;lane++){
                    const uint32_t *r=rows+lane*f->row_words;
                    unsigned begin=r[32],end=r[33];
                    if(trial==3u&&r[36]){
                        unsigned other=r[34]==0u?3u:2u;
                        actions[lane]=(WFAction){.kind=WF_CLICK,.target=other};
                        continue;
                    }
                    if(trial==3u){begin=0u;end=1u;}
                    actions[lane]=(WFAction){.kind=WF_SELECT_RANGE,
                        .arg0=begin,.arg1=end};
                }
                batch(f,rows,actions,300u);
                if(trial==3u){
                    for(unsigned lane=0;lane<4u;lane++){
                        const uint32_t *r=rows+lane*f->row_words;
                        if(r[36])actions[lane]=(WFAction){.kind=WF_WAIT};
                        else actions[lane]=(WFAction){.kind=WF_CLICK,
                            .target=r[34]==0u?3u:2u};
                    }
                    batch(f,rows,actions,400u);
                }else{
                    for(unsigned lane=0;lane<4u;lane++){
                        const uint32_t *r=rows+lane*f->row_words;
                        actions[lane]=(WFAction){.kind=WF_CLICK,
                            .target=r[34]==3u?
                              20u+5u*((r[35]+1u)%6u):
                              (r[34]==0u?3u:2u)};
                    }
                    batch(f,rows,actions,400u);
                }
            }
            for(unsigned lane=0;lane<4u;lane++)
                actions[lane]=(WFAction){.kind=WF_CLICK,.target=1u};
            batch(f,rows,actions,500u);
        }
        for(unsigned lane=0;lane<4u;lane++){
            const uint32_t *r=rows+lane*f->row_words;
            int good=trial!=5u&&source_ok(r);
            unsigned status=trial==5u||
                (trial==7u&&!r[36])?WF_TIMEOUT:WF_TERMINAL;
            if(r[WF_STATUS]!=status)fprintf(stderr,
                "editor status mismatch seed=%u trial=%u lane=%u single=%u goal=[%u,%u) style=%u color=%u first_style=%u expected=%u actual=%u raw=%08x\n",
                seed,trial,lane,r[36],r[32],r[33],r[34],r[35],
                r[2048],status,r[WF_STATUS],r[WF_RAW_REWARD]);
            assert(r[WF_STATUS]==status);
            assert(real(r[WF_RAW_REWARD])==(good?1.0f:-1.0f));
            float timed=good?1.0f-500.0f/15000.0f:-1.0f;
            assert(fabsf(real(r[WF_TIMED_REWARD])-timed)<1e-6f);
            checked++;
        }
    }
    assert(styles_seen==15u);
    free(rows);printf("PASS: %u independent editor selection/format/color outcomes\n",checked);
    return 0;
}
