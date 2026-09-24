#include "training.h"
#include "training_contract.h"
#include "miniwob/tree/validation.h"
#include "miniwob/sequence_select/validation.h"
#include "miniwob/autocomplete/validation.h"
#include <assert.h>
#include <signal.h>
#include <sys/resource.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void mutate_goal(uint32_t *r){unsigned tag=r[0];if(tag<=3){for(unsigned i=0;i<r[1];i++)r[18+3*i]^=1;}else if(tag>=4&&tag<=6){for(unsigned i=0;i<r[1];i++)if(r[19+4*i])r[96+32*i]^=1;}else if(tag==8)r[11]=(r[11]+1)%r[13];else if(tag==9){for(unsigned i=0;i<r[1];i++)r[17+6*i]^=1;}else if(tag==10){r[160]^=1;r[192]^=1;r[12]^=1;}}
int main(void){
 struct rlimit no_core={0,0};setrlimit(RLIMIT_CORE,&no_core);
 uint32_t words[8192],clean[8192],other[256];WTView a,b;WTFeatures *f=wt_features_new(0);assert(f);float *fa=malloc(WT_FEATURES*sizeof(float)),*fb=malloc(WT_FEATURES*sizeof(float));assert(fa&&fb);
 unsigned cases=0,steps=0;
 for(unsigned task=0;task<12;task++)for(unsigned base=0;base<128;base+=32){
  for(unsigned i=0;i<32;i++)wt_request(words+256*i,task,base+i);webnav_batch(words);signal(SIGABRT,SIG_DFL);
  memcpy(clean,words,sizeof words);
  memset(words,0xa5,sizeof words);
  for(unsigned i=0;i<32;i++){words[256*i]=11;words[256*i+1]=task;words[256*i+2]=base+i;}
  webnav_batch(words);assert(!memcmp(clean,words,sizeof words));
  for(unsigned i=0;i<32;i++){uint32_t *r=words+256*i;assert(!wt_done(r));assert(!wt_view(r,&a)&&a.count&&*a.query);if(r[0]==9)assert(webnav_tree_valid(r));if(r[0]==7||r[0]==8)assert(webnav_sequence_select_valid(r));if(r[0]==10)assert(webnav_autocomplete_valid(r));
   memcpy(other,r,sizeof other);mutate_goal(other);assert(!wt_view(other,&b));assert(!memcmp(&a,&b,sizeof a));unsigned char ma[WT_ACTIONS],mb[WT_ACTIONS];wt_mask(&a,ma);wt_mask(&b,mb);assert(!memcmp(ma,mb,sizeof ma));wt_features(f,&a,fa);wt_features(f,&b,fb);assert(!memcmp(fa,fb,WT_FEATURES*sizeof(float)));for(unsigned k=0;k<WT_FEATURES;k++)assert(isfinite(fa[k]));cases++;
  }
  for(unsigned step=1;step<=40;step++){for(unsigned i=0;i<32;i++){uint32_t *r=words+256*i;assert(!wt_view(r,&a));unsigned char mask[WT_ACTIONS];wt_mask(&a,mask);unsigned legal[WT_ACTIONS],n=0;for(unsigned k=0;k<WT_ACTIONS;k++)if(mask[k])legal[n++]=k;assert(n);unsigned action=legal[(base*13+i*17+step*19)%n];wt_action(r,&a,action,step*250);}webnav_batch(words);steps+=32;}
  for(unsigned i=0;i<32;i++)assert(wt_done(words+256*i));
 }
 memset(&a,0,sizeof a);strcpy(a.query,"English to French");wt_features(f,&a,fa);strcpy(a.query,"French to English");wt_features(f,&a,fb);assert(memcmp(fa,fb,WT_FEATURES*sizeof(float)));
 WTFeatures *semantic=wt_features_new(1);assert(semantic);
 memset(&a,0,sizeof a);a.count=1;strcpy(a.query,"buy this item");strcpy(a.nodes[0].name,"Add to cart");a.nodes[0].role=WT_BUTTON;a.nodes[0].flags=WT_VISIBLE|WT_ENABLED|WT_CLICKABLE;
 wt_features(semantic,&a,fa);wt_features(semantic,&a,fb);assert(!memcmp(fa,fb,WT_FEATURES*sizeof(float)));
 wt_features(f,&a,fb);assert(memcmp(fa,fb,WT_FEATURES*sizeof(float)));for(unsigned i=0;i<8+1024;i++)assert(fa[i]==fb[i]);
 wt_features_free(semantic);
 assert(wt_contract_write("build/webnav/training/test-contract",0,128,2,17,4095)==0);int p,h,l;assert(!wt_contract_read("build/webnav/training/test-contract",&p,&h,&l)&&p==0&&h==128&&l==2);
 free(fa);free(fb);wt_features_free(f);printf("PASS: %u generated instances, %u mixed legal-action transitions, private-goal observation/mask/feature noninterference, ordered bytes, finite features and checkpoint contract\n",cases,steps);return 0;
}
