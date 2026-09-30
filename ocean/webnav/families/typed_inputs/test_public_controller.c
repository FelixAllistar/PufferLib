#include "../common/family_api.h"
#include "public_controller.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

#define ROW 1024u
#define LANES 4u
static float real(uint32_t bits){float x;memcpy(&x,&bits,4);return x;}

int main(void){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==3);
    uint32_t *rows=calloc(LANES*ROW,sizeof *rows);assert(rows);
    unsigned solved=0;
    for(unsigned task=0;task<3;task++)for(unsigned seed=1;seed<400;seed+=17){
        for(unsigned lane=0;lane<LANES;lane++){
            uint32_t *r=rows+lane*ROW;
            r[WF_VERSION]=2;r[WF_TASK]=task;r[WF_OP]=WF_RESET;
            r[WF_SEED]=seed+lane;
        }
        f->batch(rows);signal(SIGABRT,SIG_DFL);
        for(unsigned step=1;step<=2&&rows[WF_STATUS]==WF_RUNNING;step++){
            WFView view;assert(!f->observe(rows,&view));
            char scratch[32];WFAction a={.elapsed_ms=step*100};
            assert(!typed_inputs_public_action(&view,&a,scratch,sizeof scratch));
            assert(!f->action(rows,&a));f->batch(rows);signal(SIGABRT,SIG_DFL);
            assert(!f->validate(rows));
        }
        assert(rows[WF_STATUS]==WF_TERMINAL&&real(rows[WF_RAW_REWARD])==1.0f);
        solved++;
    }
    free(rows);
    printf("typed_inputs public-view instances solved: %u\n",solved);
    return 0;
}
