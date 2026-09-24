#include "../common/family_api.h"
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static float number(uint32_t bits){float f;memcpy(&f,&bits,4);return f;}
static unsigned next(unsigned *s){*s^=*s<<13;*s^=*s>>17;*s^=*s<<5;return *s;}
int main(void){
 const WFFamily *f=webnav_family_v2();uint32_t *rows=calloc(f->row_words*f->batch_lanes,4),*before=malloc(f->row_words*4);WFView v,other;unsigned rng=73,cases=0;
 assert(rows&&before);
 for(unsigned task=0;task<9;task++)for(unsigned seed=0;seed<64;seed++){
  for(unsigned l=0;l<4;l++){uint32_t *r=rows+l*f->row_words;memset(r,0xa5,f->row_words*4);r[0]=2;r[1]=task;r[2]=1;r[3]=seed+l;assert(!f->validate(r));}
  f->batch(rows);signal(SIGABRT,SIG_DFL);
  for(unsigned step=0;step<48;step++){
   for(unsigned l=0;l<4;l++){
    uint32_t *r=rows+l*f->row_words;assert(!f->validate(r));assert(!f->observe(r,&v));memcpy(before,r,f->row_words*4);
    for(unsigned i=0;i<r[38];i++)r[64+48*i+2]^=1;r[35]=0;r[36]=0;
    assert(!f->observe(r,&other));assert(!memcmp(&v,&other,sizeof v));memcpy(r,before,f->row_words*4);
    unsigned chosen=next(&rng)%(r[38]+1),expected_status=r[9],active=r[33];float expected_raw=number(r[10]),expected_timed=number(r[11]);
    unsigned now=r[8]+500;r[2]=2;r[4]=chosen?1:0;r[5]=chosen;r[8]=now;
    if(!expected_status){
     if(now>=r[12]){expected_status=2;expected_raw=expected_timed=-1;}
     else if(chosen){const uint32_t *n=r+64+48*(chosen-1);int finish=0,success=0;
      if(n[0]==0){active=r[34]&&active==n[1]?0:n[1];if(r[35]){finish=1;success=n[1]==r[35];}else if(r[36]&&n[1]==2){finish=1;success=r[36]==1;}}
      else if(n[0]==1&&n[1]==active){finish=1;success=n[2];}
      else if(n[0]==2){finish=1;success=active==1;active=2;}
      if(finish){expected_status=1;expected_raw=success?1:-1;expected_timed=success?1-(float)now/r[12]:-1;}
     }
    }
    /* Expected values kept in reserved slots, outside task state. */
    r[16]=expected_status;r[17]=active;memcpy(r+18,&expected_raw,4);memcpy(r+19,&expected_timed,4);
   }
   f->batch(rows);
   for(unsigned l=0;l<4;l++){uint32_t *r=rows+l*f->row_words;assert(r[9]==r[16]&&r[33]==r[17]);assert(fabsf(number(r[10])-number(r[18]))<1e-6f);assert(fabsf(number(r[11])-number(r[19]))<1e-6f);cases++;}
  }
 }
 free(before);free(rows);printf("PASS: %u independent panel transitions, all nine presets, hidden-goal noninterference, four lanes, terminal absorption and task deadlines\n",cases);
}
