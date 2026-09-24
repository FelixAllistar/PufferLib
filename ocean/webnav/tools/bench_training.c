#define _POSIX_C_SOURCE 200809L
#include "training.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <sys/resource.h>
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
int main(int argc,char **argv){unsigned loops=argc>1?(unsigned)atoi(argv[1]):1000;int potion=argc>2?atoi(argv[2]):0;if(!loops||loops>100000||potion<0||potion>1)return 2;double start=now();WTFeatures *f=wt_features_new(potion);if(!f)return 1;double load=now()-start;uint32_t words[8192];unsigned ticks[32]={0},episodes=32;WTView views[32];float *out=malloc(WT_FEATURES*sizeof(float));if(!out)return 1;for(unsigned i=0;i<32;i++)wt_request(words+256*i,i%12,i);webnav_batch(words);double model=0,features=0,reset=0;volatile float consume=0;
 for(unsigned turn=0;turn<loops;turn++){
  start=now();for(unsigned i=0;i<32;i++){if(wt_view(words+256*i,views+i))return 1;wt_features(f,views+i,out);consume+=out[(turn+i)%WT_FEATURES];}features+=now()-start;
  for(unsigned i=0;i<32;i++){unsigned char mask[WT_ACTIONS];wt_mask(views+i,mask);unsigned actions[WT_ACTIONS],n=0;for(unsigned a=0;a<WT_ACTIONS;a++)if(mask[a])actions[n++]=a;wt_action(words+256*i,views+i,actions[(turn*19+i*13)%n],++ticks[i]*250);}
  start=now();webnav_batch(words);model+=now()-start;int needs=0;
  for(unsigned i=0;i<32;i++)if(wt_done(words+256*i)){wt_request(words+256*i,i%12,episodes++);ticks[i]=0;needs=1;}else wt_action(words+256*i,views+i,0,ticks[i]*250);
  if(needs){start=now();webnav_batch(words);reset+=now()-start;}
 }
 struct rusage ru;getrusage(RUSAGE_SELF,&ru);printf("{\"potion\":%d,\"steps\":%u,\"episodes_started\":%u,\"load_seconds\":%.6f,\"bend_step_seconds\":%.6f,\"autoreset_batch_seconds\":%.6f,\"public_view_features_seconds\":%.6f,\"combined_measured_sps\":%.3f,\"peak_rss_kib\":%ld,\"feature_floats\":%d,\"scope\":\"single CPU thread; 32 lanes; mixed task generators/actions; excludes policy, GPU copies, browser and action-selection overhead\"}\n",potion,loops*32,episodes,load,model,reset,features,loops*32/(model+reset+features),ru.ru_maxrss,WT_FEATURES);free(out);wt_features_free(f);return !isfinite(consume);}
