#define _GNU_SOURCE
#include "../common/loader.h"
#include "../../cdp.h"
#include <assert.h>
#include <math.h>
#include <limits.h>
#include <signal.h>
#include <sys/prctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROW 8192u
#define NODE_BASE 64u
#define STRIDE 40u
#define QUERY_BASE 6400u
static double num(const cJSON *j,const char *key){const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsNumber(v));return v->valuedouble;}
static int boolean(const cJSON *j,const char *key){const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsBool(v));return cJSON_IsTrue(v);}
static const char *str(const cJSON *j,const char *key){const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsString(v));return v->valuestring;}
static float real(uint32_t bits){float f;memcpy(&f,&bits,4);return f;}
static char *read_file(const char *path){FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>=0);rewind(f);char *s=calloc((size_t)n+1,1);assert(s&&fread(s,1,(size_t)n,f)==(size_t)n);fclose(f);return s;}
static void units(uint32_t *out,unsigned cap,const char *s){assert(strlen(s)<cap);for(unsigned i=0;s[i];i++){assert((unsigned char)s[i]>=32&&(unsigned char)s[i]<=126);out[i]=(unsigned char)s[i];}}
static cJSON *eval(WebCdp *c,const char *expression){cJSON *j=web_cdp_eval(c,expression);assert(j);return j;}
static void move_mouse(WebCdp *c,double x,double y){cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"type","mouseMoved");cJSON_AddNumberToObject(p,"x",x);cJSON_AddNumberToObject(p,"y",y);cJSON *r=web_cdp_call(c,"Input.dispatchMouseEvent",p);assert(r);cJSON_Delete(r);}
static cJSON *point(WebCdp *c,unsigned ref){char js[128];snprintf(js,sizeof js,"__menus.point(%u)",ref);cJSON *j=eval(c,js);assert(cJSON_IsObject(j)&&boolean(j,"visible"));return j;}
static void import(uint32_t *r,unsigned task,const cJSON *j){
 memset(r,0,ROW*4);r[0]=WF_ABI_VERSION;r[1]=task;r[12]=(unsigned)num(j,"deadline");r[32]=0;
 r[33]=boolean(j,"open");r[34]=(unsigned)num(j,"hover");units(r+QUERY_BASE,256,str(j,"instruction"));
 const cJSON *items=cJSON_GetObjectItemCaseSensitive(j,"nodes");assert(cJSON_IsArray(items));r[32]=(unsigned)cJSON_GetArraySize(items);assert(r[32]>0&&r[32]<=155);
 for(unsigned i=0;i<r[32];i++){
  const cJSON *n=cJSON_GetArrayItem(items,(int)i);uint32_t *o=r+NODE_BASE+i*STRIDE;
  o[0]=(unsigned)num(n,"kind");o[1]=(unsigned)num(n,"parent");o[2]=boolean(n,"goal");o[3]=boolean(n,"enabled");o[4]=boolean(n,"children");o[5]=(unsigned)num(n,"icon");
  units(o+6,26,str(n,"name"));o[36]=boolean(n,"visible");o[37]=boolean(n,"expanded");
  if(o[2])r[35]=i+1;
 }
 assert(r[35]);r[36]=strstr(str(j,"instruction"),"icon")?1:0;
}
static uint32_t *node(uint32_t *r,unsigned ref){assert(ref&&ref<=r[32]);return r+NODE_BASE+(ref-1)*STRIDE;}
static void compare(WFLoaded *f,const cJSON *j,unsigned step,unsigned ref){
 uint32_t *r=f->words;WFView v;assert(!wf_observe(f,0,&v));
 if(strcmp(wf_text_get(&v,v.instruction),str(j,"instruction"))||boolean(j,"open")!=(int)r[33]||
    (int)boolean(j,"done")!=(r[9]!=0)||num(j,"deadline")!=r[12]||
    (r[9]&&(fabs(num(j,"raw")-real(r[10]))>1e-6||fabs(num(j,"reward")-real(r[11]))>1e-5))){
  fprintf(stderr,"menu mismatch task=%u step=%u ref=%u open=%d/%u done=%d/%u raw=%g/%g reward=%g/%g\n",r[1],step,ref,boolean(j,"open"),r[33],boolean(j,"done"),r[9],num(j,"raw"),real(r[10]),num(j,"reward"),real(r[11]));abort();
 }
 const cJSON *visible=cJSON_GetObjectItemCaseSensitive(j,"visible"),*expanded=cJSON_GetObjectItemCaseSensitive(j,"expanded");assert(cJSON_IsArray(visible)&&cJSON_IsArray(expanded));
 if(cJSON_GetArraySize(visible)!=(int)v.count){char *dump=cJSON_PrintUnformatted(visible);fprintf(stderr,"visibility mismatch task=%u step=%u ref=%u hover=%u browser=%s model=",r[1],step,ref,r[34],dump);free(dump);for(unsigned i=0;i<v.count;i++)fprintf(stderr,"%u,",v.nodes[i].ref);fprintf(stderr,"\n");exit(1);}
 for(unsigned i=0;i<v.count;i++){const WFNode *n=v.nodes+i;assert(n->ref==(unsigned)cJSON_GetArrayItem(visible,(int)i)->valuedouble);if(n->flags&WF_EXPANDED){int found=0;for(int k=0;k<cJSON_GetArraySize(expanded);k++)found|=(unsigned)cJSON_GetArrayItem(expanded,k)->valuedouble==n->ref;assert(found);}}
}
static void step_action(WFLoaded *f,WebCdp *c,unsigned kind,unsigned ref,unsigned elapsed,unsigned step){
 char js[128];snprintf(js,sizeof js,"__menus.tick(%u)",elapsed);cJSON *t=eval(c,js);cJSON_Delete(t);
 if(kind==WF_POINTER_MOVE){cJSON *p=point(c,ref);move_mouse(c,num(p,"x"),num(p,"y"));cJSON_Delete(p);}
 else if(kind==WF_CLICK){cJSON *p=point(c,ref);assert(!web_cdp_click(c,num(p,"x"),num(p,"y")));cJSON_Delete(p);}
 WFAction a={.kind=kind,.target=ref,.elapsed_ms=elapsed};assert(!wf_apply(f,0,&a));for(unsigned lane=1;lane<4;lane++)memcpy(f->words+lane*ROW,f->words,ROW*4);assert(!wf_batch_checked(f));
 cJSON *j=eval(c,"new Promise(resolve=>setTimeout(()=>resolve(__menus.snapshot()),350))");compare(f,j,step,ref);cJSON_Delete(j);
}
static unsigned ancestors(uint32_t *r,unsigned ref,unsigned *out){unsigned n=0,p=node(r,ref)[1];while(p){assert(n<4);out[n++]=p;p=node(r,p)[1];}for(unsigned i=0;i<n/2;i++){unsigned x=out[i];out[i]=out[n-1-i];out[n-1-i]=x;}return n;}
static void solve(WFLoaded *f,WebCdp *c,unsigned target,unsigned elapsed,unsigned *step){
 uint32_t *r=f->words;
 if(r[1]==1&&!r[33])step_action(f,c,WF_CLICK,1,elapsed++,(*step)++);
 unsigned parents[4],n=ancestors(r,target,parents);
 for(unsigned i=0;i<n;i++)step_action(f,c,WF_POINTER_MOVE,parents[i],elapsed++,(*step)++);
 step_action(f,c,WF_CLICK,target,elapsed,(*step)++);
}
int main(int argc,char **argv){
 prctl(PR_SET_DUMPABLE,0,0,0,0);
 unsigned episodes=argc>1?strtoul(argv[1],NULL,10):12;if(!episodes||episodes>200)return 2;
 WFLoaded f;char error[512];if(wf_open(&f,"build/webnav/families/menus/libmenus.so",error,sizeof error)){fprintf(stderr,"%s\n",error);return 1;}
 char *script=read_file("ocean/webnav/families/menus/browser.js");unsigned total=0;
 for(unsigned task=0;task<2;task++){
  char file[PATH_MAX],resolved[PATH_MAX],url[PATH_MAX+8];snprintf(file,sizeof file,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",f.api->task_names[task]);assert(realpath(file,resolved));snprintf(url,sizeof url,"file://%s",resolved);
  WebCdp c={.input=-1,.output=-1,.pid=-1};const char *chrome=getenv("WEBNAV_CHROME");if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
  assert(!web_cdp_start_ready(&c,chrome,url,"Boolean(window.core&&core.cover_div)"));cJSON *injected=eval(&c,script);cJSON_Delete(injected);
  for(unsigned ep=0;ep<episodes;ep++){
   unsigned seed=200000+task*10000+ep;char js[192];snprintf(js,sizeof js,"__menus.reset(%u,%u)",seed,task);cJSON *j=eval(&c,js);import(f.words,task,j);for(unsigned lane=1;lane<4;lane++)memcpy(f.words+lane*ROW,f.words,ROW*4);assert(!f.api->validate(f.words));compare(&f,j,0,0);cJSON_Delete(j);unsigned step=1;
   if(ep%6==0){solve(&f,&c,f.words[35],100,&step);assert(f.words[9]==WF_TERMINAL&&f.words[10]==1065353216u);}
   else if(ep%6==1){uint32_t *r=f.words;if(task==1)step_action(&f,&c,WF_CLICK,1,100,step++);unsigned wrong=0;for(unsigned i=1;i<=r[32];i++){uint32_t *n=node(r,i);if(!n[0]&&!n[2]&&!n[4]&&n[3]){wrong=i;break;}}assert(wrong);solve(&f,&c,wrong,200,&step);assert(f.words[9]==WF_TERMINAL&&f.words[10]==3212836864u);}
   else if(ep%6==2){step_action(&f,&c,WF_WAIT,0,10000,step++);assert(f.words[9]==WF_TIMEOUT);}
   else if(ep%6==4){if(task==1)step_action(&f,&c,WF_CLICK,1,100,step++);unsigned branch=0;for(unsigned i=1;i<=f.words[32];i++)if(node(f.words,i)[4]&&!node(f.words,i)[1]){branch=i;break;}step_action(&f,&c,branch?WF_CLICK:WF_WAIT,branch,200,step++);}
   else if(ep%6==5){if(task==1){step_action(&f,&c,WF_CLICK,1,100,step++);unsigned disabled=0;for(unsigned i=1;i<=f.words[32];i++)if(!node(f.words,i)[3]){disabled=i;break;}assert(disabled);step_action(&f,&c,WF_CLICK,disabled,200,step++);}else step_action(&f,&c,WF_WAIT,0,10000,step++);}
   else if(task==1){step_action(&f,&c,WF_CLICK,1,100,step++);step_action(&f,&c,WF_CLICK,1,200,step++);step_action(&f,&c,WF_WAIT,0,300,step++);assert(f.words[9]==WF_RUNNING);}
   else {unsigned target=f.words[35];unsigned parents[4],n=ancestors(f.words,target,parents);for(unsigned i=0;i<n;i++)step_action(&f,&c,WF_POINTER_MOVE,parents[i],100+i,step++);step_action(&f,&c,WF_WAIT,0,500,step++);assert(f.words[9]==WF_RUNNING);}
   total++;
  }
  printf("{\"task\":\"%s\",\"episodes\":%u,\"state_reward_visibility_conformance\":\"PASS\",\"preset\":\"matched original instances; settled jQuery hover/click; controlled clock\"}\n",f.api->task_names[task],episodes);fflush(stdout);
  web_cdp_close(&c);
 }
 free(script);wf_close(&f);fprintf(stderr,"PASS: %u original-generated menu episodes; CDP mouse hover/click, source query and visible submenu conformance\n",total);return 0;
}
