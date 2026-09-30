#include "../common/family_api.h"
#include "public_controller.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>

void autocomplete_batch(uint32_t *legacy){
    const WFFamily *f=webnav_family_v2();uint32_t rows[4*2048];
    for(unsigned block=0;block<8;block++){
        memset(rows,0,sizeof rows);
        for(unsigned i=0;i<4;i++){uint32_t *r=rows+i*2048;r[0]=2;r[2]=WF_STEP;r[12]=10000;memcpy(r+32,legacy+(block*4+i)*256,256*sizeof *r);}
        f->batch(rows);
        for(unsigned i=0;i<4;i++)memcpy(legacy+(block*4+i)*256,rows+i*2048+32,256*sizeof *rows);
    }
}
#define main legacy_autocomplete_tests
#include "../../miniwob/autocomplete/test_autocomplete.c"
#undef main

int main(void){
    prctl(PR_SET_DUMPABLE,0,0,0,0);assert(!legacy_autocomplete_tests());
    const WFFamily *f=webnav_family_v2();uint32_t rows[4*2048];unsigned solved=0;
    for(unsigned seed=0;seed<128;seed++){
        memset(rows,0xe9,sizeof rows);
        for(unsigned i=0;i<4;i++){uint32_t *r=rows+i*2048;r[0]=2;r[1]=0;r[2]=WF_RESET;r[3]=seed+i;r[6]=0;}
        f->batch(rows);for(unsigned i=0;i<4;i++)assert(!f->validate(rows+i*2048));
        for(unsigned step=1;step<16&&!rows[9];step++){
            WFView v,other;assert(!f->observe(rows,&v));assert(!v.omitted);
            unsigned old=rows[192];rows[192]='x';assert(!f->observe(rows,&other));assert(!memcmp(&v,&other,sizeof v));rows[192]=old;
            WFAction a;char scratch[64];assert(!autocomplete_public_next(&v,scratch,sizeof scratch,&a));a.elapsed_ms=step*100;
            assert(!f->action(rows,&a));f->batch(rows);assert(!f->validate(rows));
        }
        assert(rows[9]==1&&rows[10]==1065353216u);solved++;
    }
    printf("PASS: %u autocomplete generated public menu solves, dirty reset lanes and target noninterference\n",solved);return 0;
}
