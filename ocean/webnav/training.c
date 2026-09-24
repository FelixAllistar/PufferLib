#include "training.h"
#include "miniwob/training/countries.h"
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
const char *wt_task_names[12]={"click-button","click-link","focus-text","click-button-sequence","choose-list","click-checkboxes","click-option","enter-text","login-user","read-table","navigate-tree","use-autocomplete-nodelay"};
static void packed(const uint32_t *r,unsigned at,unsigned bytes,char *out,unsigned cap){unsigned n=0;for(;n<bytes&&n+1<cap;n++){unsigned c=(r[at+n/4]>>(8*(n%4)))&255;if(!c)break;out[n]=(char)c;}out[n]=0;}
static void units(const uint32_t *r,unsigned at,unsigned n,char *out,unsigned cap){unsigned i=0;for(;i<n&&i+1<cap&&r[at+i];i++)out[i]=(char)r[at+i];out[i]=0;}
static void name(WTNode *n,const char *s){snprintf(n->name,sizeof n->name,"%s",s);}
static WTNode *node(WTView *v,unsigned ref,unsigned role,unsigned flags){if(v->count==WT_NODES){v->omitted++;return NULL;}WTNode *n=&v->nodes[v->count++];n->ref=ref;n->role=role;n->flags=flags;n->y=(float)(v->count-1)/WT_NODES;return n;}
static void span(WTView *v,const char *s,unsigned source){if(!*s||strlen(s)>64)return;for(unsigned i=0;i<v->spans;i++)if(!strcmp(v->copy[i],s))return;if(v->spans==WT_SPANS)return;snprintf(v->copy[v->spans],65,"%s",s);v->copy_node[v->spans++]=source;}
static void quoted(WTView *v){const char *p=v->query;while((p=strchr(p,'"'))){const char *e=strchr(++p,'"');if(!e)break;size_t n=(size_t)(e-p);if(n&&n<=64){char text[65];memcpy(text,p,n);text[n]=0;span(v,text,0);}p=e+1;}}
void wt_request(uint32_t *r,unsigned task,uint32_t seed){assert(task<12);memset(r,0,256*sizeof *r);r[0]=11;r[1]=task;r[2]=seed;}
unsigned wt_done(const uint32_t *r){return r[0]==7||r[0]==8?r[1]:(r[0]>=4&&r[0]<=6)||r[0]==10?r[3]:r[2];}
static float reward_at(const uint32_t *r,int raw){unsigned i=r[0]==7||r[0]==8?8:(r[0]>=4&&r[0]<=6)||r[0]==10?9:11;float f;uint32_t b=r[i+!raw];memcpy(&f,&b,4);return f;}
float wt_reward(const uint32_t *r){return reward_at(r,0);}float wt_raw_reward(const uint32_t *r){return reward_at(r,1);}
int wt_view(const uint32_t *r,WTView *v){
 memset(v,0,sizeof *v);unsigned tag=r[0],click=WT_VISIBLE|WT_ENABLED|WT_CLICKABLE;
 if(tag>10)return -1;
 v->elapsed=tag==7||tag==8?r[6]:((tag>=4&&tag<=6)||tag==10?r[5]:r[9]);
 if(tag==1||tag==3)units(r,128,128,v->query,sizeof v->query);
 else packed(r,tag==10?22:224,tag==10?40:tag==6?64:128,v->query,sizeof v->query);
 if(tag<=3){
  if(r[1]>16)return -1;
  for(unsigned i=0;i<r[1];i++){unsigned role=r[16+3*i];WTNode *n=node(v,i+1,role==1?WT_BUTTON:role==2?WT_CHECKBOX:role==3?WT_INPUT:role==4?WT_LINK:role==5?WT_RADIO:WT_OTHER,click|(r[17+3*i]?WT_CHECKED:0)|(r[3]==i+1?WT_FOCUSED:0));if(!n)continue;
   if(tag==1||tag==3)units(r,64+8*i,8,n->name,65);else packed(r,64+8*i,32,n->name,65);
  }
 }else if(tag>=4&&tag<=6){
  unsigned count=tag==5?2:1;
  for(unsigned i=0;i<count;i++){WTNode *n=node(v,i+1,WT_INPUT,click|(r[2]==i+1?WT_FOCUSED:0));name(n,tag==5?(i?"Password":"Username"):"Text");units(r,32+32*i,r[16+4*i],n->value,65);n->start=r[17+4*i];n->end=r[18+4*i];n->capacity=tag==5?32:64;n->insert_limit=64;}
  name(node(v,count+1,WT_BUTTON,click),tag==5?"Login":"Submit");
  if(tag==6){for(unsigned i=0;i<4;i++){WTNode *n=node(v,4+i,WT_CELL,WT_VISIBLE);packed(r,240+2*i,8,n->name,65);n->parent=100+i/2;}}
 }else if(tag==7){
  for(unsigned i=0;i<2;i++){WTNode *n=node(v,i+1,WT_BUTTON,click);name(n,i?"TWO":"ONE");n->x=(float)r[13+2*i]/160;n->y=(float)r[14+2*i]/210;}
 }else if(tag==8){
  name(node(v,1,WT_SELECT,WT_VISIBLE|WT_ENABLED),"Select");
  packed(r,32+16*r[10],64,v->nodes[0].value,65);
  for(unsigned i=0;i<r[13]&&i<9;i++){WTNode *n=node(v,2+i,WT_OPTION,click|(r[10]==i?WT_CHECKED:0));if(n){packed(r,32+16*i,64,n->name,65);n->parent=1;}}
  name(node(v,11,WT_BUTTON,click),"Submit");
 }else if(tag==9){
  if(r[1]>8)return -1;
  for(unsigned i=0;i<r[1];i++){const uint32_t *t=r+16+6*i;if(!t[5])continue;WTNode *n=node(v,i+1,t[0]?WT_FOLDER:WT_FILE,click|(t[2]?WT_EXPANDED:0));n->parent=t[3];packed(r,64+8*i,32,n->name,65);}
 }else{
  WTNode *n=node(v,1,WT_INPUT,click|(r[2]==1?WT_FOCUSED:0));name(n,"Country");n->flags|=WT_MENU;n->capacity=64;n->insert_limit=32;units(r,32,r[16],n->value,65);n->start=r[17];n->end=r[18];name(node(v,2,WT_BUTTON,click),"Submit");
  if(r[15]){char term[65];units(r,96,r[19],term,65);unsigned matched=0;for(unsigned i=0;i<sizeof(wt_countries)/sizeof(wt_countries[0]);i++){if(strncasecmp(wt_countries[i],term,strlen(term)))continue;n=node(v,3+matched,WT_OPTION,click|(r[21]==matched+1?WT_CHECKED:0));if(n){name(n,wt_countries[i]);n->parent=1;}matched++;}if(matched!=r[20])return -1;}
 }
 quoted(v);
 for(unsigned i=0;i<v->count;i++)if(v->nodes[i].role==WT_CELL)span(v,v->nodes[i].name,v->nodes[i].ref);
 return 0;
}
void wt_mask(const WTView *v,unsigned char mask[WT_ACTIONS]){
 memset(mask,0,WT_ACTIONS);mask[0]=1;const WTNode *focus=NULL;for(unsigned i=0;i<v->count;i++){const WTNode *n=v->nodes+i;mask[1+i]=(n->flags&(WT_VISIBLE|WT_ENABLED|WT_CLICKABLE))==(WT_VISIBLE|WT_ENABLED|WT_CLICKABLE);if(n->role==WT_INPUT&&(n->flags&WT_FOCUSED))focus=n;}
 for(unsigned i=0;i<v->spans;i++)mask[1+WT_NODES+i]=focus&&strlen(v->copy[i])<=focus->insert_limit&&strlen(focus->value)+strlen(v->copy[i])-(focus->end-focus->start)<=focus->capacity;
 for(unsigned i=1+WT_NODES+WT_SPANS;i<WT_ACTIONS;i++)mask[i]=focus&&(i<1+WT_NODES+WT_SPANS+7||(focus->flags&WT_MENU));
}
int wt_action(uint32_t *r,const WTView *v,unsigned a,unsigned elapsed){
 unsigned char mask[WT_ACTIONS];wt_mask(v,mask);int valid=a<WT_ACTIONS&&mask[a];if(!valid)a=0;unsigned tag=r[0];
 unsigned cmd=0,arg=0,arg2=0;const char *payload=NULL;
 if(a&&a<=WT_NODES){unsigned ref=v->nodes[a-1].ref;
  if(tag<=3||tag==9){cmd=1;arg=ref-1;}
  else if(tag==7){cmd=1;arg=r[13+2*(ref-1)]+20;arg2=r[14+2*(ref-1)]+20;}
  else if(tag==8){cmd=ref==11?2:1;arg=ref>=2?ref-2:0;}
  else if(tag==10){cmd=ref==1?1:ref==2?10:14;arg=ref>=3?ref-3:0;}
  else{cmd=ref<=r[1]?1:10;arg=ref-1;}
 }else if(a>WT_NODES&&a<=WT_NODES+WT_SPANS){cmd=2;payload=v->copy[a-1-WT_NODES];}
 else if(a){static const unsigned keys[]={3,9,7,8,5,6,4,15,13,12};cmd=keys[a-(1+WT_NODES+WT_SPANS)];if(tag!=10&&cmd>11)cmd=0;}
 if(tag<=3||tag==9){r[7]=cmd;r[8]=arg;r[9]=elapsed;}
 else if(tag==7||tag==8){r[3]=cmd;r[4]=arg;r[5]=arg2;r[6]=elapsed;}
 else{
  r[7]=cmd;r[8]=arg;r[5]=elapsed;r[11]=0;
  if(payload){unsigned n=(unsigned)strlen(payload),cap=tag==10?32:64;unsigned focus=r[2];unsigned length=tag==10?r[16]:focus&&focus<=r[1]?r[16+4*(focus-1)]:0;unsigned start=tag==10?r[17]:focus&&focus<=r[1]?r[17+4*(focus-1)]:0;unsigned end=tag==10?r[18]:focus&&focus<=r[1]?r[18+4*(focus-1)]:0;unsigned fieldcap=tag==5?32:64;
   if(n>cap||length+n-(end-start)>fieldcap){r[7]=0;return -1;}
   r[11]=n;for(unsigned i=0;i<n;i++)r[(tag==10?224:160)+i]=(unsigned char)payload[i];
  }
 }
 return valid?0:-1;
}
/* Direct-mapped exact-string cache. IDs never enter rollouts or checkpoints. */
#define CACHE_N 512
typedef struct {uint64_t hash;char text[129];float vec[256];} Entry;
struct WTFeatures {WebTextEncoder *encoder;Entry cache[CACHE_N];};
WTFeatures *wt_features_new(int potion){WTFeatures *f=calloc(1,sizeof *f);if(!f)return NULL;if(potion){f->encoder=web_text_load("build/webnav/reference/potion-tokenizer.json","build/webnav/reference/potion-model.safetensors");if(!f->encoder){free(f);return NULL;}}return f;}
void wt_features_free(WTFeatures *f){if(f){web_text_free(f->encoder);free(f);}}
static void vector(WTFeatures *f,const char *s,float out[256]){memset(out,0,256*sizeof(float));if(!f->encoder||!*s)return;uint64_t h=1469598103934665603ULL;for(const unsigned char *p=(const unsigned char*)s;*p;p++)h=(h^*p)*1099511628211ULL;Entry *e=f->cache+h%CACHE_N;if(e->hash!=h||strcmp(e->text,s)){if(web_text_encode(f->encoder,s,strlen(s),e->vec,NULL)){fprintf(stderr,"text encoding failed\n");abort();}e->hash=h;snprintf(e->text,sizeof e->text,"%s",s);}memcpy(out,e->vec,256*sizeof(float));}
static void project(const float *v,float *out){for(unsigned i=0;i<256;i++)out[i%32]+=v[i]*((i/32)&1?-0.35355339f:0.35355339f);}
static void bytes(const char *s,unsigned n,float *out){size_t len=strlen(s);for(unsigned i=0;i<n;i++){unsigned c=i<len?(unsigned char)s[i]:0;for(unsigned b=0;b<8;b++)out[i*8+b]=(float)((c>>b)&1);}}
static float contains(const char *query,const char *text){if(!*text)return 0;size_t n=strlen(text);const char *p=query;while((p=strstr(p,text))){if((p==query||!isalnum((unsigned char)p[-1]))&&!isalnum((unsigned char)p[n]))return 1;p++;}return 0;}
void wt_features(WTFeatures *f,const WTView *v,float out[WT_FEATURES]){
 memset(out,0,WT_FEATURES*sizeof(float));out[0]=(float)v->count/WT_NODES;out[1]=(float)v->spans/WT_SPANS;out[2]=(float)v->elapsed/10000;out[3]=(float)v->omitted;out[4]=(float)v->text_clipped;
 float qvec[256];vector(f,v->query,qvec);bytes(v->query,128,out+8);project(qvec,out+8+1024);
 unsigned off=8+1024+32;
 for(unsigned i=0;i<v->count;i++){const WTNode *n=v->nodes+i;float *d=out+off+i*WT_NODE_FEATURES;bytes(n->name,32,d);bytes(n->value,32,d+256);d+=512;d[0]=1;if(n->role<11)d[1+n->role]=1;for(unsigned b=0;b<6;b++)d[12+b]=(float)((n->flags>>b)&1);d[18]=n->x;d[19]=n->y;d[20]=(float)n->start/64;d[21]=(float)n->end/64;d[22]=contains(v->query,n->name);d[23]=contains(v->query,n->value);d[24]=strlen(n->name)>32;d[25]=strlen(n->value)>32;d[26]=(float)strlen(n->name)/64;d[27]=(float)strlen(n->value)/64;d[28]=(float)n->parent/128;d[31]=!!(n->flags&WT_MENU);
  for(unsigned j=0;j<v->count;j++)if(v->nodes[j].parent&&v->nodes[j].parent==n->parent&&contains(v->query,v->nodes[j].name))d[29]=1;
  float nv[256];vector(f,n->name,nv);for(unsigned j=0;j<256;j++)d[30]+=qvec[j]*nv[j];project(nv,d+32);
 }
 off+=WT_NODES*WT_NODE_FEATURES;
 for(unsigned i=0;i<v->spans;i++){float *d=out+off+i*WT_SPAN_FEATURES;bytes(v->copy[i],32,d);d[256]=1;d[257]=(float)strlen(v->copy[i])/64;d[258]=strlen(v->copy[i])>32;for(unsigned j=0;j<v->count;j++){const WTNode *n=v->nodes+j;if(n->role==WT_INPUT&&(n->flags&WT_FOCUSED))d[259]=!strcmp(n->value,v->copy[i]);if(n->ref==v->copy_node[i]){d[260]=contains(v->query,n->name);for(unsigned k=0;k<v->count;k++)if(n->parent&&v->nodes[k].parent==n->parent&&contains(v->query,v->nodes[k].name))d[261]=1;}}}
}
int wt_expert(const WTView *v){
 unsigned char mask[WT_ACTIONS];wt_mask(v,mask);
 if(v->count==2&&!strcmp(v->nodes[0].name,"ONE")&&!strcmp(v->nodes[1].name,"TWO"))return v->elapsed?2:1;
 /* This solver reads only public text and state. It is not policy training. */
 if(v->count>=2&&(v->nodes[0].flags&WT_MENU)&&v->spans){
  const WTNode *n=v->nodes;
  if(!(n->flags&WT_FOCUSED))return 1;
  char wanted[129];snprintf(wanted,sizeof wanted,"%s%s",v->copy[0],v->spans>1?v->copy[1]:"");
  if(!strcmp(n->value,wanted))return 2;
  if(v->spans>1&&!strcmp(n->value,v->copy[0]))return 1+WT_NODES+1;
  if(*n->value&&!(n->start==0&&n->end==strlen(n->value)))return 1+WT_NODES+WT_SPANS+1;
  return 1+WT_NODES;
 }

 for(unsigned i=0;i<v->count;i++){const WTNode *n=v->nodes+i;if(n->role==WT_CHECKBOX){int wanted=contains(v->query,n->name);if(wanted!=!!(n->flags&WT_CHECKED))return 1+i;}if(n->role==WT_RADIO&&contains(v->query,n->name)&&!(n->flags&WT_CHECKED))return 1+i;}
 for(unsigned i=0;i<v->count;i++){const WTNode *n=v->nodes+i;if((n->role==WT_LINK||n->role==WT_FILE||n->role==WT_FOLDER||n->role==WT_OPTION)&&contains(v->query,n->name)&&!(n->flags&WT_CHECKED))return 1+i;}
 for(unsigned i=0;i<v->count;i++)if(v->nodes[i].role==WT_INPUT){const WTNode *n=v->nodes+i;unsigned wanted=i<v->spans?i:0;for(unsigned j=0;j<v->spans;j++)if(v->copy_node[j]){for(unsigned k=0;k<v->count;k++)if(v->nodes[k].ref==v->copy_node[j]){unsigned parent=v->nodes[k].parent;for(unsigned z=0;z<v->count;z++)if(parent&&v->nodes[z].parent==parent&&contains(v->query,v->nodes[z].name)&&strcmp(v->nodes[k].name,v->nodes[z].name))wanted=j;}}
  if(v->spans&&!strcmp(n->value,v->copy[wanted]))continue;if(!(n->flags&WT_FOCUSED))return 1+i;if(v->spans){if(*n->value&&!(n->start==0&&n->end==strlen(n->value)))return 1+WT_NODES+WT_SPANS+1;return 1+WT_NODES+wanted;}}
 for(unsigned i=0;i<v->count;i++){const WTNode *n=v->nodes+i;if(n->role==WT_BUTTON&&(contains(v->query,n->name)||!strcmp(n->name,"Submit")||!strcmp(n->name,"Login")))return 1+i;}
 for(unsigned i=0;i<v->count;i++)if(v->nodes[i].role==WT_FOLDER&&!(v->nodes[i].flags&WT_EXPANDED))return 1+i;
 for(unsigned i=1;i<WT_ACTIONS;i++)if(mask[i])return i;return 0;
}
