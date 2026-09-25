#include "bridge.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static void run(uint32_t *w,unsigned command,unsigned index){for(int i=0;i<32;i++){if(i)memcpy(w+i*256,w,256*sizeof *w);w[i*256+7]=command;w[i*256+8]=index;}webnav_batch(w);}
static float reward(const uint32_t *w,unsigned offset){float x;memcpy(&x,w+offset,sizeof x);return x;}
int main(void){uint32_t w[8192]={0};unsigned checked_cases=0;
 for(unsigned n=2;n<=6;n++)for(unsigned target=0;target<(1u<<n);target++)for(unsigned selected=0;selected<(1u<<n);selected++){
   memset(w,0,sizeof w);w[0]=1;w[1]=n+1;unsigned correct=0;for(unsigned i=0;i<n;i++){w[16+i*3]=2;w[17+i*3]=(selected>>i)&1;w[18+i*3]=(target>>i)&1;correct+=w[17+i*3]==w[18+i*3];}w[16+n*3]=1;
   uint32_t before[256];memcpy(before,w,sizeof before);run(w,1,n+8);for(unsigned i=0;i<256;i++)if(i!=7&&i!=8)assert(w[i]==before[i]);
   run(w,1,0);run(w,1,0);for(unsigned i=0;i<n;i++)assert(w[17+i*3]==((selected>>i)&1));
   run(w,1,n);assert(w[2]==1&&w[3]==n+1&&w[4]==correct&&w[5]==n&&w[6]==(2*correct>n));
   memcpy(before,w,sizeof before);run(w,2,0);for(unsigned i=0;i<256;i++)if(i!=7&&i!=8)assert(w[i]==before[i]);checked_cases++;
 }
 /* Deadline wins over an otherwise successful click; reward and completion
    time remain frozen after termination, even when the input clock advances. */
 const unsigned times[]={0,137,5000,9999,10000,10001,UINT32_MAX};
 for(unsigned t=0;t<sizeof times/sizeof *times;t++)for(unsigned good=0;good<2;good++){
   memset(w,0,sizeof w);w[1]=1;w[16]=1;w[18]=good;w[9]=times[t];run(w,1,0);
   const int expired=times[t]>=10000;
   const float raw=expired||!good?-1.0f:1.0f;
   const float timed=expired||!good?-1.0f:1.0f-(float)times[t]/10000.0f;
   assert(w[2]==(expired?2u:1u)&&w[10]==times[t]);
   assert(fabsf(reward(w,11)-raw)<1e-6f&&fabsf(reward(w,12)-timed)<1e-6f);
   uint32_t before[256];memcpy(before,w,sizeof before);w[9]=UINT32_MAX;run(w,1,0);
   for(unsigned i=0;i<256;i++)if(i!=9)assert(w[i]==before[i]);
 }
 memset(w,0,sizeof w);w[1]=1;w[16]=1;w[18]=1;w[9]=9999;run(w,0,0);
 assert(w[2]==0&&w[10]==9999&&reward(w,12)==0.0f);w[9]=10000;run(w,0,0);
 assert(w[2]==2&&reward(w,12)==-1.0f);
 /* Different lane payloads: row state never bleeds into its neighbors. */
 memset(w,0,sizeof w);for(unsigned i=0;i<32;i++){uint32_t *r=w+i*256;r[0]=0;r[1]=1;r[7]=1;r[8]=0;r[16]=1;r[18]=i%2;}
 webnav_batch(w);for(unsigned i=0;i<32;i++){const uint32_t *r=w+i*256;assert(r[2]==1&&r[4]==i%2&&r[5]==1&&r[6]==i%2);}
 printf("PASS: %u exhaustive checkbox target/state cases (2..6 controls), reversible toggles, invalid targets, outcome absorption, reward/deadline boundaries and 32 independent lanes\n",checked_cases);return 0;}
