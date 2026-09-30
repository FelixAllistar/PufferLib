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
static float real(uint32_t bits){float f;memcpy(&f,&bits,4);return f;}
int main(void){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==1);
    uint32_t *rows=calloc(LANES*ROW,sizeof *rows);assert(rows);
    unsigned wins=0,draws=0,losses=0;
    for(unsigned seed=1;seed<=256;seed++){
        for(unsigned lane=0;lane<LANES;lane++){
            uint32_t *r=rows+lane*ROW;
            r[WF_VERSION]=2;r[WF_TASK]=0;r[WF_OP]=WF_RESET;
            r[WF_SEED]=seed+lane;
        }
        f->batch(rows);signal(SIGABRT,SIG_DFL);
        for(unsigned move=0;move<5&&rows[WF_STATUS]==WF_RUNNING;move++){
            WFView view;assert(!f->observe(rows,&view));
            WFAction a={.elapsed_ms=100+move*400};
            assert(!board_public_action(&view,&a));
            assert(!f->action(rows,&a));f->batch(rows);signal(SIGABRT,SIG_DFL);
            assert(!f->validate(rows));
        }
        assert(rows[WF_STATUS]==WF_TERMINAL);
        float reward=real(rows[WF_RAW_REWARD]);
        if(reward==1.0f)wins++;
        else if(reward==-0.5f)draws++;
        else if(reward==-0.75f)losses++;
        else assert(0);
    }
    assert(wins+draws+losses==256&&wins>0);
    free(rows);
    printf("board public-view outcomes over 256 games: %u wins, %u draws, %u losses\n",
           wins,draws,losses);
    return 0;
}
