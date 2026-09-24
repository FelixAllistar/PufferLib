#include "bridge.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static float value(const uint32_t *r,unsigned at){float v;memcpy(&v,r+at,sizeof v);return v;}

/* Different task ABIs deliberately occupy adjacent rows. Identical single-
 * task batches alone cannot detect dispatch errors or cross-row writes. */
int main(void) {
    uint32_t w[WEBNAV_BATCH*WEBNAV_WORDS]={0};
    for(unsigned lane=0;lane<WEBNAV_BATCH;lane++) {
        uint32_t *r=w+lane*WEBNAV_WORDS;unsigned tag=lane%11;r[0]=tag;
        if(tag<4) {
            r[1]=tag==1||tag==3?2:1;r[7]=1;r[9]=137;
            r[16]=tag==0?1:tag==1?2:tag==2?3:5;r[18]=1;
            if(r[1]==2)r[19]=1;
        }else if(tag<7) {
            r[1]=tag==5?2:1;r[2]=1;r[5]=137;r[7]=2;
            r[11]=1;r[19]=1;r[96]='X';r[160]='X';
        }else if(tag==7) {
            r[3]=1;r[4]=10;r[5]=60;r[6]=137;
            r[13]=0;r[14]=50;r[15]=60;r[16]=50;r[17]=1;
        }else if(tag==8) {
            r[3]=1;r[4]=1;r[6]=137;r[11]=1;r[12]=1;r[13]=3;
            for(unsigned i=0;i<3;i++){r[17+i]=1;r[32+16*i]='A'+i;}
        }else if(tag==9) {
            r[1]=1;r[5]=1;r[7]=1;r[9]=137;r[16]=1;r[20]=1;
        }else {
            r[1]=1;r[2]=1;r[5]=137;r[7]=2;r[11]=2;
            r[13]=2;r[14]=2;r[160]='X';r[161]='X';
            r[192]='z';r[193]='z';r[224]='X';r[225]='X';
        }
    }
    webnav_batch(w);
    for(unsigned lane=0;lane<WEBNAV_BATCH;lane++) {
        uint32_t *r=w+lane*WEBNAV_WORDS;unsigned tag=lane%11;assert(r[0]==tag);
        if(tag==0||tag==2)assert(r[2]==1&&r[4]==1);
        else if(tag==1||tag==3){assert(r[2]==0&&r[17]==1);r[8]=1;r[9]=274;}
        else if(tag<7){assert(r[3]==0&&r[16]==1&&r[32]=='X');r[7]=10;r[5]=274;}
        else if(tag==7){assert(r[1]==0&&r[10]==1&&r[11]==1);r[4]=70;r[6]=274;}
        else if(tag==8){assert(r[1]==0&&r[10]==1);r[3]=2;r[6]=274;}
        else if(tag==9){assert(r[2]==0&&r[18]==1);r[9]=274;}
        else{assert(r[3]==0&&r[16]==2&&r[32]=='X');r[7]=10;r[5]=274;r[11]=0;}
    }
    webnav_batch(w);
    for(unsigned lane=0;lane<WEBNAV_BATCH;lane++) {
        const uint32_t *r=w+lane*WEBNAV_WORDS;unsigned tag=lane%11;assert(r[0]==tag);
        if(tag<4)assert(r[2]==1&&r[4]==r[5]);
        else if(tag<7||tag==10){unsigned cursor=tag==10?2:1;assert(r[3]==1&&r[4]==1&&r[17]==cursor&&r[18]==cursor);}
        else if(tag==7||tag==8)assert(r[1]==1&&r[2]==1);
        else assert(r[2]==0&&r[18]==0);
        unsigned raw=tag<4||tag==9?11:tag==7||tag==8?8:9;
        float expected=tag==9?0:1,elapsed=tag==0||tag==2?137:274;
        assert(fabsf(value(r,raw)-expected)<1e-6f);
        assert(fabsf(value(r,raw+1)-expected*(1-elapsed/10000))<1e-6f);
    }
    puts("PASS: all 11 task tags coexist across 32 lanes; independent two-step state/reward transitions");
    return 0;
}
