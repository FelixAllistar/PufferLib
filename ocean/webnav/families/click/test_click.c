#include "../common/family_api.h"
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static float number(uint32_t b){float f;memcpy(&f,&b,4);return f;}
static unsigned next(unsigned *s){*s^=*s<<13;*s^=*s>>17;*s^=*s<<5;return *s;}
/* Source-derived generator constraints, separate from the transition oracle. */
static void check_reset(const uint32_t *r,unsigned task,unsigned mode){
 static const unsigned controls[]={1,2,2,1,3,5,3};
 assert(r[9]==0&&r[34]==0&&r[12]==(task==8?20000u:10000u));
 unsigned goals=0,boxes=0;
 for(unsigned i=0;i<r[33];i++){const uint32_t *n=r+64+i*40;assert(!n[1]);goals+=n[2];boxes+=n[0]==2;}
 if(task<7){assert(r[33]==controls[task]);assert(goals>=1);}
 if(task==2){assert(r[66]==(mode==0));assert(r[106]==(mode==1));}
 if(task==6){assert(goals==1);for(unsigned i=0;i<3;i++)assert(r[64+i*40]==3);}
 if(task>=7){assert(boxes==r[33]-1&&r[64+boxes*40]==1);assert(goals<=boxes);}
 if(task==7){assert(boxes>=3&&boxes<=6);assert(mode?(goals>=4&&goals<=6):goals<=3);}
 if(task==8){assert(boxes>=7&&boxes<=12&&goals>=5&&goals<=12);}
 if(task==9){assert(boxes>=3&&boxes<=6&&goals>=1&&goals<=5&&goals<boxes);}
}
int main(void){const WFFamily *f=webnav_family_v2();uint32_t *rows=calloc(8*2048,4),copy[2048];WFView v,other;unsigned rng=123,cases=0;
 for(unsigned task=0;task<10;task++)for(unsigned seed=0;seed<64;seed++)for(unsigned mode=0;mode<2;mode++){
  for(unsigned l=0;l<8;l++){uint32_t *r=rows+2048*l;memset(r,0xb9,2048*4);r[0]=2;r[1]=task;r[2]=1;r[3]=seed+l;r[6]=mode;assert(!f->validate(r));}f->batch(rows);signal(SIGABRT,SIG_DFL);
  for(unsigned l=0;l<8;l++)check_reset(rows+2048*l,task,mode);
  for(unsigned step=1;step<=45;step++){
   for(unsigned l=0;l<8;l++){uint32_t *r=rows+2048*l;assert(!f->validate(r));assert(!f->observe(r,&v));memcpy(copy,r,sizeof copy);for(unsigned i=0;i<r[33];i++)r[66+i*40]^=1;assert(!f->observe(r,&other));assert(!memcmp(&v,&other,sizeof v));memcpy(r,copy,sizeof copy);
    unsigned ref=next(&rng)%(r[33]+2),done=r[9],focus=r[34],checked=0;for(unsigned i=0;i<r[33];i++)checked|=r[65+i*40]<<i;float raw=number(r[10]),reward=number(r[11]);unsigned elapsed=step*500;
    if(!done){if(elapsed>=r[12]){done=2;raw=reward=-1;}else if(ref&&ref<=r[33]){const uint32_t *n=r+64+40*(ref-1);if(n[0]==2)checked^=1u<<(ref-1);if(n[0]==5)for(unsigned i=0;i<r[33];i++)if(r[64+40*i]==5){checked&=~(1u<<i);if(i==ref-1)checked|=1u<<i;}focus=r[32]==1?0:ref;
     if(r[32]!=2){done=1;raw=n[2]?1:-1;}else if(n[0]==1){unsigned correct=0,count=0;for(unsigned i=0;i<r[33];i++)if(r[64+40*i]==2){count++;correct+=((checked>>i)&1)==r[66+40*i];}done=1;raw=(2.0f*correct-count)/count;}
     if(done)reward=raw>0?raw*(1-(float)elapsed/r[12]):raw;
    }}r[2]=2;r[4]=ref?1:0;r[5]=ref;r[8]=elapsed;r[16]=done;r[17]=focus;r[18]=checked;memcpy(r+19,&raw,4);memcpy(r+20,&reward,4);
   }f->batch(rows);
   for(unsigned l=0;l<8;l++){uint32_t *r=rows+2048*l;assert(r[9]==r[16]&&r[34]==r[17]);assert(fabsf(number(r[10])-number(r[19]))<1e-6&&fabsf(number(r[11])-number(r[20]))<1e-6);for(unsigned i=0;i<r[33];i++)assert(r[65+40*i]==((r[18]>>i)&1));cases++;}
  }
 }free(rows);printf("PASS: %u independent click/focus/checkbox transitions, ten tasks, both transfer modes, private-target noninterference, terminal/deadline behavior\n",cases);}
