#include "../common/family_api.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROW 8192u
#define NODE_BASE 64u
#define STRIDE 40u
static const WFFamily *family;
static uint32_t *rows;
static unsigned cases;

static uint32_t *row(unsigned lane){return rows+(size_t)lane*ROW;}
static uint32_t *node(uint32_t *r,unsigned ref){assert(ref&&ref<=r[32]);return r+NODE_BASE+(ref-1)*STRIDE;}
static void run(void){family->batch(rows);for(unsigned i=0;i<4;i++)assert(!family->validate(row(i)));}
static void send(unsigned lane,unsigned kind,unsigned ref,unsigned elapsed){
 WFAction a={.kind=kind,.target=ref,.elapsed_ms=elapsed};assert(!family->action(row(lane),&a));run();cases++;
}
static unsigned path_to(uint32_t *r,unsigned target,unsigned *out){
 unsigned n=0,p=node(r,target)[1];
 while(p){assert(n<4);out[n++]=p;p=node(r,p)[1];}
 for(unsigned i=0;i<n/2;i++){unsigned x=out[i];out[i]=out[n-1-i];out[n-1-i]=x;}
 return n;
}
static void click_target(unsigned lane,unsigned ref,unsigned elapsed){
 uint32_t *r=row(lane);unsigned parents[4],n=path_to(r,ref,parents);
 if(r[1]==1&&r[33]==0){send(lane,WF_CLICK,1,elapsed++);}
 for(unsigned i=0;i<n;i++)send(lane,WF_POINTER_MOVE,parents[i],elapsed++);
 send(lane,WF_CLICK,ref,elapsed);
}
static void reset_all(void){
 for(unsigned task=0;task<2;task++)for(unsigned seed=0;seed<48;seed++){
  for(unsigned l=0;l<4;l++){uint32_t *r=row(l);memset(r,0,ROW*4);r[WF_VERSION]=WF_ABI_VERSION;r[WF_TASK]=task;r[WF_OP]=WF_RESET;r[WF_SEED]=seed*4+l;}
  run();
  for(unsigned l=0;l<4;l++){
   uint32_t *r=row(l);assert(r[WF_OP]==WF_OBSERVE&&r[WF_STATUS]==WF_RUNNING&&r[32]>0&&r[32]<=100&&r[12]==10000);
   WFView v;assert(!family->observe(r,&v));assert(v.version==WF_ABI_VERSION&&v.deadline_ms==10000&&v.instruction.length);
   if(task==0){assert(v.count==3);assert(r[33]==1);for(unsigned i=0;i<3;i++)assert(node(r,i+1)[36]==1);}
   else {assert(v.count==1&&v.nodes[0].ref==1&&r[33]==0);assert(!strcmp(wf_text_get(&v,v.nodes[0].name),"Menu"));}
   uint32_t *goal=node(r,r[35]);assert(goal[2]&&goal[3]&&!goal[4]);
   /* The goal, target reference and icon query variant are private. */
   uint32_t copy[ROW];memcpy(copy,r,sizeof copy);uint32_t old_target=copy[35];copy[35]=old_target==1?2:1;
   uint32_t *old=node(copy,old_target);uint32_t *other=node(copy,copy[35]);
   if(!other[0]&&!other[4]&&!other[2]&&other[3]){old[2]=0;other[2]=1;}
   WFView hidden;assert(!family->observe(copy,&hidden));assert(!memcmp(&v,&hidden,sizeof v));
   cases++;
  }
 }
}
static void correct_paths_and_rewards(void){
 for(unsigned task=0;task<2;task++)for(unsigned seed=0;seed<64;seed++){
  for(unsigned l=0;l<4;l++){uint32_t *r=row(l);memset(r,0,ROW*4);r[0]=2;r[1]=task;r[2]=1;r[3]=seed*4+l;}
  run();uint32_t *r=row(0);unsigned target=r[35];click_target(0,target,200);
  assert(r[9]==WF_TERMINAL&&r[10]==1065353216u);float reward;memcpy(&reward,r+11,4);assert(reward>0.90f&&reward<1.0f);
  WFView v;assert(!family->observe(r,&v));assert(v.count>0);cases++;
 }
}
static void wrong_leaf_and_terminal_absorption(void){
 for(unsigned task=0;task<2;task++){
  for(unsigned l=0;l<4;l++){uint32_t *r=row(l);memset(r,0,ROW*4);r[0]=2;r[1]=task;r[2]=1;r[3]=700+l;}
  run();uint32_t *r=row(0);if(task==1)send(0,WF_CLICK,1,10);
  unsigned wrong=0;for(unsigned i=1;i<=r[32];i++){uint32_t *n=node(r,i);if(!n[0]&&!n[4]&&!n[2]){wrong=i;break;}}
  assert(wrong);click_target(0,wrong,20);assert(r[9]==WF_TERMINAL&&r[10]==3212836864u);
  uint32_t before[ROW];memcpy(before,r,sizeof before);WFAction a={.kind=WF_WAIT,.elapsed_ms=100};assert(!family->action(r,&a));run();assert(r[9]==WF_TERMINAL&&r[10]==3212836864u);assert(r[33]==before[33]&&r[34]==before[34]);cases++;
 }
}
static void timeout_and_lane_isolation(void){
 for(unsigned l=0;l<4;l++){uint32_t *r=row(l);memset(r,0,ROW*4);r[0]=2;r[1]=l%2;r[2]=1;r[3]=900+l;}
 run();for(unsigned l=0;l<4;l++)send(l,WF_WAIT,0,10000);
 for(unsigned l=0;l<4;l++){uint32_t *r=row(l);assert(r[9]==WF_TIMEOUT&&r[10]==3212836864u&&r[11]==3212836864u);}
 for(unsigned l=0;l<4;l++){uint32_t *r=row(l);memset(r,0,ROW*4);r[0]=2;r[1]=1;r[2]=1;r[3]=1000+l;}
 run();uint32_t before[ROW];memcpy(before,row(1),sizeof before);send(0,WF_CLICK,1,1);assert(!memcmp(before,row(1),sizeof before));cases++;
}
int main(void){
 family=webnav_family_v2();assert(family&&family->task_count==2&&family->row_words==ROW&&family->batch_lanes==4);
 rows=calloc((size_t)ROW*4,sizeof *rows);assert(rows);
 reset_all();correct_paths_and_rewards();wrong_leaf_and_terminal_absorption();timeout_and_lane_isolation();
 free(rows);printf("PASS: %u menu reset/projection, private-goal noninterference, open/hover/click, reward, timeout and lane-isolation cases\n",cases);return 0;
}
