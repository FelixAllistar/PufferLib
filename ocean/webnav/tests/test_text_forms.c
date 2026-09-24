#include "bridge.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void run(uint32_t *w){for(unsigned i=1;i<32;i++)memcpy(w+i*256,w,256*sizeof *w);webnav_batch(w);}
static void put(uint32_t *w,unsigned at,const char *s){for(unsigned i=0;s[i];i++)w[at+i]=(unsigned char)s[i];}
static void reference(char *s,unsigned *start,unsigned *end,unsigned command){
 unsigned n=(unsigned)strlen(s),a=*start,b=*end;
 if(command==2||command==3||command==4){
  const char *insert=command==2?"XY":"";unsigned add=(unsigned)strlen(insert);
  if(a==b&&command==3&&a)a--;
  if(a==b&&command==4&&b<n)b++;
  memmove(s+a+add,s+b,n-b+1);memcpy(s+a,insert,add);a+=add;b=a;
 }else if(command==5){a=a==b&&a?a-1:a;b=a;}
 else if(command==6){b=a==b&&b<n?b+1:b;a=b;}
 else if(command==7){a=b=0;}
 else if(command==8){a=b=n;}
 else if(command==9){a=0;b=n;}
 *start=a;*end=b;
}
int main(void){uint32_t w[8192];unsigned cases=0;
 for(unsigned n=0;n<=5;n++)for(unsigned mask=0;mask<(1u<<n);mask++)for(unsigned a=0;a<=n;a++)for(unsigned b=a;b<=n;b++)for(unsigned command=2;command<=9;command++){
  memset(w,0,sizeof w);w[0]=4;w[1]=1;w[2]=1;w[7]=command;w[11]=2;w[16]=n;w[17]=a;w[18]=b;w[19]=6;put(w,96,"target");put(w,160,"XY");
  char expected[64]={0};for(unsigned i=0;i<n;i++)w[32+i]=expected[i]=(mask>>i)&1?'a':'b';unsigned start=a,end=b;
  reference(expected,&start,&end,command);run(w);
  assert(w[3]==0&&w[2]==1&&w[16]==strlen(expected)&&w[17]==start&&w[18]==end&&w[19]==6);
  for(unsigned i=0;i<32;i++)assert(w[32+i]==(i<strlen(expected)?(unsigned char)expected[i]:0));
  for(unsigned i=0;i<6;i++)assert(w[96+i]==(unsigned char)"target"[i]);cases++;
 }
 /* Maximum supported field, selection replacement and unrelated-field frame. */
 memset(w,0,sizeof w);w[0]=5;w[1]=2;w[2]=1;w[7]=2;w[11]=2;w[16]=32;w[17]=0;w[18]=32;
 for(unsigned i=0;i<32;i++)w[32+i]='a';put(w,160,"XY");w[20]=3;w[21]=1;w[22]=2;put(w,64,"abc");
 run(w);assert(w[16]==2&&w[17]==2&&w[18]==2&&w[32]=='X'&&w[33]=='Y');assert(w[20]==3&&w[21]==1&&w[22]==2&&w[64]=='a'&&w[65]=='b'&&w[66]=='c');
 /* No focus, invalid focus target, and submit goal uses both fields exactly. */
 w[2]=0;w[7]=3;uint32_t before[256];memcpy(before,w,sizeof before);run(w);assert(!memcmp(w,before,sizeof before));
 w[7]=1;w[8]=99;memcpy(before,w,sizeof before);run(w);assert(!memcmp(w,before,sizeof before));
 w[19]=2;w[23]=3;put(w,96,"XY");put(w,128,"abc");w[7]=10;w[5]=137;run(w);assert(w[3]==1&&w[4]==1&&w[2]==3);
 memcpy(before,w,sizeof before);w[7]=3;w[5]=10000;run(w);for(unsigned i=0;i<256;i++)if(i!=5&&i!=7)assert(w[i]==before[i]);
 memset(w,0,sizeof w);w[0]=4;w[1]=1;w[7]=10;w[5]=10000;run(w);assert(w[3]==2&&w[4]==0);
 /* Full 64-character single-field payload, value and goal must not overlap. */
 memset(w,0,sizeof w);w[0]=4;w[1]=1;w[2]=1;w[7]=2;w[11]=64;w[16]=64;w[18]=64;w[19]=64;
 for(unsigned i=0;i<64;i++){w[32+i]='a';w[96+i]='b';w[160+i]='b';}
 run(w);assert(w[16]==64&&w[17]==64&&w[18]==64);for(unsigned i=0;i<64;i++)assert(w[32+i]=='b'&&w[96+i]=='b');
 w[7]=10;run(w);assert(w[3]==1&&w[4]==1);
 /* Mixed lane routing: old click widgets and both new text shapes. */
 memset(w,0,sizeof w);for(unsigned i=0;i<32;i++){uint32_t *r=w+i*256;if(i%3==0){r[0]=0;r[1]=1;r[7]=1;r[16]=1;r[18]=1;}else{r[0]=i%3==1?4:5;r[1]=r[0]==4?1:2;r[2]=1;r[7]=2;r[11]=1;r[160]='a'+i%26;}}
 webnav_batch(w);for(unsigned i=0;i<32;i++){const uint32_t *r=w+i*256;if(i%3==0)assert(r[2]==1&&r[4]==1);else assert(r[3]==0&&r[16]==1&&r[32]=='a'+i%26);}
 printf("PASS: %u exhaustive ASCII edit/selection cases; maximum field, unrelated field, unfocused/invalid action, exact submit, terminal/deadline and mixed-lane routing\n",cases);
}
