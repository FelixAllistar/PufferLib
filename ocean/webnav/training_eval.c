#define _POSIX_C_SOURCE 200809L
#include "training.h"
#include "training_policy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+1e-9*t.tv_nsec;}
/* Optional policy sampling diagnoses greedy loops without changing training. */
static unsigned sample_action(WTPolicy *p,const WTView *v,unsigned *rng){
 unsigned char mask[WT_ACTIONS];wt_mask(v,mask);const float *logits=wt_policy_logits(p);double weights[WT_ACTIONS],sum=0;float top=-INFINITY;
 for(unsigned i=0;i<WT_ACTIONS;i++)if(mask[i]&&logits[i]>top)top=logits[i];
 for(unsigned i=0;i<WT_ACTIONS;i++){weights[i]=mask[i]?exp((double)logits[i]-top):0;sum+=weights[i];}
 *rng^=*rng<<13;*rng^=*rng>>17;*rng^=*rng<<5;double pick=(*rng/4294967296.0)*sum;
 unsigned last=0;for(unsigned i=0;i<WT_ACTIONS;i++)if(mask[i]){last=i;if(pick<weights[i])return i;pick-=weights[i];}return last;
}
int main(int argc,char **argv){
 if(argc<2||argc>7){fprintf(stderr,"usage: training_eval expert|checkpoint [episodes_per_task=100] [task_mask=4095] [first_seed=2147583648] [trace=0] [sample_seed=0 (greedy)]\n");return 2;}
 unsigned episodes=argc>2?(unsigned)strtoul(argv[2],0,0):100,mask=argc>3?(unsigned)strtoul(argv[3],0,0):4095,first=argc>4?(unsigned)strtoul(argv[4],0,0):2147583648u,trace=argc>5?(unsigned)atoi(argv[5]):0;
 if(!episodes||episodes>100000||!mask||mask>4095||first<0x80000000u||first>UINT32_MAX-episodes)return 2;
 unsigned sample_seed=argc>6?(unsigned)strtoul(argv[6],0,0):0,sample_rng=sample_seed;
 WTPolicy *policy=NULL;if(strcmp(argv[1],"expert")){policy=wt_policy_load(argv[1]);if(!policy)return 1;}
 uint32_t words[8192];WTView view;unsigned all=0,wins=0;double start=now();
 for(unsigned task=0;task<12;task++)if(mask&(1u<<task)){unsigned success=0,timeouts=0,invalid=0,actions=0;double reward=0;
  for(unsigned ep=0;ep<episodes;ep++){for(unsigned l=0;l<32;l++)wt_request(words+l*256,task,first+ep);webnav_batch(words);if(policy)wt_policy_reset(policy);
   for(unsigned step=1;step<=40&&!wt_done(words);step++){if(wt_view(words,&view))return 1;unsigned action=policy?(unsigned)wt_policy_action(policy,&view):(unsigned)wt_expert(&view);
    if(policy&&sample_seed)action=sample_action(policy,&view,&sample_rng);
    if(trace){fprintf(stderr,"%s seed=%u step=%u query=%s action=%u",wt_task_names[task],first+ep,step,view.query,action);if(action&&action<=WT_NODES&&action<=view.count)fprintf(stderr," target=%s",view.nodes[action-1].name);if(action>WT_NODES&&action<=WT_NODES+WT_SPANS)fprintf(stderr," copy=%s",view.copy[action-1-WT_NODES]);fputc('\n',stderr);}
    invalid+=wt_action(words,&view,action,step*250)!=0;for(unsigned l=1;l<32;l++)memcpy(words+l*256,words,256*sizeof(uint32_t));webnav_batch(words);actions++;
   }
   if(!wt_done(words)){fprintf(stderr,"missing deadline\n");return 1;}success+=wt_raw_reward(words)>=0.999f;timeouts+=wt_done(words)==2;reward+=wt_reward(words);
  }
  printf("{\"task\":\"%s\",\"episodes\":%u,\"success\":%u,\"success_rate\":%.6f,\"timeouts\":%u,\"invalid\":%u,\"actions\":%u,\"mean_reward\":%.6f,\"first_seed\":%u,\"sample_seed\":%u,\"controller\":\"%s\"}\n",wt_task_names[task],episodes,success,(double)success/episodes,timeouts,invalid,actions,reward/episodes,first,sample_seed,policy?(sample_seed?"learned-sampled":"learned-greedy"):"public-scripted");fflush(stdout);wins+=success;all+=episodes;
 }
 if(sample_seed)fprintf(stderr,"policy sampling seed=%u\n",sample_seed);
 fprintf(stderr,"%u/%u successes; %.3f seconds; held-out seed range, not guaranteed unseen strings/templates\n",wins,all,now()-start);wt_policy_free(policy);return 0;
}
