#include "../common/family_api.h"
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

static float number(uint32_t bits){float f;memcpy(&f,&bits,sizeof f);return f;}
int main(void){
    assert(!prctl(PR_SET_DUMPABLE,0,0,0,0));
    const WFFamily *f=webnav_family_v2();
    assert(f&&f->abi_version==2&&f->task_count==5&&f->row_words==8192);
    uint32_t *rows=calloc(f->row_words*f->batch_lanes,sizeof *rows);
    assert(rows);
    for(unsigned seed=0;seed<64;seed++){
        for(unsigned lane=0;lane<4;lane++){
            uint32_t *r=rows+lane*f->row_words;
            memset(r,0,f->row_words*sizeof *r);
            r[WF_VERSION]=2;r[WF_TASK]=0;r[WF_OP]=WF_RESET;r[WF_SEED]=seed*4+lane;
        }
        f->batch(rows);signal(SIGABRT,SIG_DFL);
        for(unsigned lane=0;lane<4;lane++){
            uint32_t *r=rows+lane*f->row_words;WFView view;
            assert(!f->validate(r)&&!f->observe(r,&view));
            assert(view.count==5&&r[34]<=20&&r[35]>=1&&r[35]<=3);
            unsigned goal=r[34],box=r[35];
            WFAction set={.kind=WF_SELECT_OPTION,.target=1,.arg0=goal,.elapsed_ms=250};
            assert(!f->action(r,&set));
        }
        f->batch(rows);signal(SIGABRT,SIG_DFL);
        for(unsigned lane=0;lane<4;lane++){
            uint32_t *r=rows+lane*f->row_words;
            assert(r[32]==r[34]&&r[WF_STATUS]==WF_RUNNING);
            WFAction click={.kind=WF_CLICK,.target=r[35]+1,.elapsed_ms=500};
            assert(!f->action(r,&click));
        }
        f->batch(rows);signal(SIGABRT,SIG_DFL);
        for(unsigned lane=0;lane<4;lane++){
            uint32_t *r=rows+lane*f->row_words;
            assert(r[33]==(1u<<(r[35]-1)));
            WFAction submit={.kind=WF_CLICK,.target=5,.elapsed_ms=750};
            assert(!f->action(r,&submit));
        }
        f->batch(rows);signal(SIGABRT,SIG_DFL);
        for(unsigned lane=0;lane<4;lane++){
            uint32_t *r=rows+lane*f->row_words;
            assert(r[WF_STATUS]==WF_TERMINAL&&number(r[WF_RAW_REWARD])==1.0f);
            assert(fabsf(number(r[WF_TIMED_REWARD])-0.925f)<1e-6f);
            uint32_t before=r[33];
            WFAction click={.kind=WF_CLICK,.target=2,.elapsed_ms=1000};
            assert(!f->action(r,&click));
            f->batch(rows);signal(SIGABRT,SIG_DFL);
            assert(r[33]==before&&r[WF_STATUS]==WF_TERMINAL);
        }
    }
    free(rows);puts("PASS: 256 form-sequence generated instances, reward and absorption");
}
