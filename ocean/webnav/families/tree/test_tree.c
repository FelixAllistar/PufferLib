#include "../common/family_api.h"
#include "public_controller.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>

/* Run the independent preserved tree oracle against the freshly built v2
 * runtime, not against a stale legacy archive. Only transport is adapted. */
void webnav_tree_batch(uint32_t *legacy){
    const WFFamily *f=webnav_family_v2();uint32_t rows[8*512];
    for(unsigned block=0;block<4;block++){
        memset(rows,0,sizeof rows);
        for(unsigned i=0;i<8;i++){
            uint32_t *r=rows+i*512;r[0]=2;r[2]=WF_STEP;r[12]=10000;
            memcpy(r+32,legacy+(block*8+i)*256,256*sizeof *r);
        }
        f->batch(rows);
        for(unsigned i=0;i<8;i++)memcpy(legacy+(block*8+i)*256,rows+i*512+32,256*sizeof *rows);
    }
}
#define main legacy_tree_tests
#include "../../miniwob/tree/test_tree.c"
#undef main

int main(void){
    prctl(PR_SET_DUMPABLE,0,0,0,0);assert(!legacy_tree_tests());
    const WFFamily *f=webnav_family_v2();uint32_t rows[8*512];unsigned solved=0;
    for(unsigned seed=0;seed<256;seed++){
        memset(rows,0xa9,sizeof rows);
        for(unsigned i=0;i<8;i++){uint32_t *r=rows+i*512;r[0]=2;r[1]=0;r[2]=WF_RESET;r[3]=seed+i;r[6]=0;}
        f->batch(rows);
        for(unsigned i=0;i<8;i++)assert(!f->validate(rows+i*512));
        for(unsigned step=1;step<16&&!rows[9];step++){
            WFView v,other;assert(!f->observe(rows,&v));
            rows[49]^=1;assert(!f->observe(rows,&other));assert(!memcmp(&v,&other,sizeof v));rows[49]^=1;
            WFAction a;assert(!tree_public_next(&v,&a));a.elapsed_ms=step*100;
            assert(!f->action(rows,&a));f->batch(rows);assert(!f->validate(rows));
        }
        assert(rows[9]==1&&rows[10]==1065353216u);solved++;
    }
    printf("PASS: %u generated public-only tree solves, reset lanes and private-target noninterference\n",solved);
    return 0;
}
