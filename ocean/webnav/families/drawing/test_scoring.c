#include "../common/family_api.h"
#include "public_controller.h"
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
static uint32_t rows[8192];
static const WFFamily *f;
static float real(uint32_t bits){float x;memcpy(&x,&bits,4);return x;}
static void reset(unsigned task,unsigned seed){
 for(unsigned lane=0;lane<4;lane++){uint32_t *r=rows+2048*lane;r[0]=2;r[1]=task;r[2]=WF_RESET;r[3]=seed+lane;r[6]=0;}
 f->batch(rows);signal(SIGABRT,SIG_DFL);for(unsigned lane=0;lane<4;lane++)assert(!f->validate(rows+2048*lane));
}
static void apply(WFAction a){
 assert(!f->action(rows,&a));for(unsigned lane=1;lane<4;lane++)rows[lane*2048+2]=WF_OBSERVE;
 f->batch(rows);signal(SIGABRT,SIG_DFL);assert(!f->validate(rows));
}
static void point(unsigned kind,double x,double y,unsigned ms){
 apply((WFAction){.kind=kind,.target=1,.arg0=(unsigned)lround(x*256),.arg1=(unsigned)lround(y*256),.elapsed_ms=ms});
}
static void submit(unsigned ms){point(WF_POINTER_UP,0,0,ms);apply((WFAction){.kind=WF_CLICK,.target=2,.elapsed_ms=ms});}
static void reward(double expected){assert(rows[9]==WF_TERMINAL);if(fabs(real(rows[10])-expected)>1e-5){fprintf(stderr,"drawing reward got %g expected %g\n",real(rows[10]),expected);assert(0);}double scaled=expected>0?expected*(1-rows[8]/10000.0):expected;assert(fabs(real(rows[11])-scaled)<1e-5);}
static void center(void){rows[32]=80*65536;rows[33]=60*65536;rows[34]=1;}
static void ring(double radius,unsigned ms){
 for(unsigned i=0;i<=32;i++){double angle=i*6.283185307179586/32;point(i?WF_POINTER_MOVE:WF_POINTER_DOWN,80+radius*cos(angle),60+radius*sin(angle),ms);}
}
int main(void){
 prctl(PR_SET_DUMPABLE,0,0,0,0);f=webnav_family_v2();
 reset(0,1);submit(100);reward(-1);
 reset(1,1);center();point(WF_POINTER_DOWN,40,10,100);point(WF_POINTER_MOVE,60,110,200);point(WF_POINTER_MOVE,80,60,300);submit(500);reward(1); /* last-point deviation bug */
 reset(1,1);center();point(WF_POINTER_DOWN,50,60,100);point(WF_POINTER_MOVE,80,60,200);point(WF_POINTER_MOVE,110,60,300);submit(500);reward(1);
 reset(1,1);center();point(WF_POINTER_DOWN,80,30,100);point(WF_POINTER_MOVE,80,60,200);point(WF_POINTER_MOVE,80,90,300);submit(500);reward(0.12);
 /* D3's >10 px filter collapses small closed rings; raw bounds are wrong. */
 const double radii[]={2,3,6,12,20},expected[]={-0.25,-0.25,0.25,0.5,1};
 /* Independent pinned D3 curveBasis context, sampled at 4096 points/segment. */
 const double widths[]={0,0,9.395574854065984,22.740740707626784,39.33333331346512};
 const double heights[]={0,0,6.580437707663805,19.873890031529292,37.99999998013179};
 for(unsigned i=0;i<5;i++){reset(0,i);center();ring(radii[i],500);submit(1000);reward(expected[i]);assert(fabs(real(rows[38])-widths[i])<0.003&&fabs(real(rows[39])-heights[i])<0.003);}
 /* Radial variance and negative reward bands, derived by reference_bounds.cjs. */
 const double spread[]={2,6,10,16,22,28,34,40,46,52,58,64};
 const double radial[]={-.25,-.25,.35,.25,.3,.1,-.1,-.3,-.5,-.7,-.9,-1};
 const double rw[]={1,4,12.33333333,19,25.66666667,32.33333333,40.66666667,47.33333333,54,60.66666667,69,75.66666667};
 const double rh[]={2,5,12.66666667,19.33333333,26,32.66666667,41,47.66666667,54.33333333,61,69.33333333,76};
 for(unsigned n=0;n<12;n++){
  reset(0,n);rows[32]=75*65536;rows[33]=55*65536;double d=spread[n]/sqrt(2);
  for(unsigned i=0;i<32;i++){
   double x=75,y=55;if(i&1){unsigned k=i%8;x+=k==1||k==7?d:-d;y+=k<5?d:-d;}
   point(i?WF_POINTER_MOVE:WF_POINTER_DOWN,x,y,500);
  }
  submit(1000);reward(radial[n]);assert(fabs(real(rows[38])-rw[n])<0.003&&fabs(real(rows[39])-rh[n])<0.003);
 }
 reset(0,3);center();ring(20,100);submit(200);reward(1);uint32_t raw=rows[10],timed=rows[11];apply((WFAction){.kind=WF_WAIT,.elapsed_ms=8000});assert(rows[10]==raw&&rows[11]==timed);
 reset(0,9);point(WF_POINTER_DOWN,50,50,9999);point(WF_POINTER_UP,50,50,10000);assert(rows[9]==WF_TIMEOUT&&real(rows[10])==-1);
 unsigned solved=0;uint32_t clean[8192];
 for(unsigned task=0;task<2;task++)for(unsigned seed=0;seed<64;seed++){
  memset(rows,0xa5,sizeof rows);reset(task,seed);memcpy(clean,rows,sizeof rows);
  memset(rows,0x5a,sizeof rows);reset(task,seed);assert(!memcmp(clean,rows,sizeof rows));
  for(unsigned step=1;step<40&&!rows[9];step++){
   WFView v,other;assert(!f->observe(rows,&v));unsigned private_direction=rows[34];rows[34]^=1;assert(!f->observe(rows,&other));assert(!memcmp(&v,&other,sizeof v));rows[34]=private_direction;
   WFAction a;assert(!drawing_public_action(&v,&a));a.elapsed_ms=step*250;apply(a);
  }
  reward(1);solved++;
 }
 printf("PASS: original line quirk, direction/offset, smoothed circle size bands, absorption/deadline; %u public solves, 512 dirty rows and private-field independence\n",solved);
}
