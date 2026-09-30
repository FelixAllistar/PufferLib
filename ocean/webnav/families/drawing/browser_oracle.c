#define _GNU_SOURCE
#include "../common/loader.h"
#include "public_controller.h"
#include "../../cdp.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
static double num(const cJSON *j,const char *key){const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsNumber(v));return v->valuedouble;}
static int boolean(const cJSON *j,const char *key){const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsBool(v));return cJSON_IsTrue(v);}
static const char *str(const cJSON *j,const char *key){const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsString(v));return v->valuestring;}
static float real(uint32_t bits){float x;memcpy(&x,&bits,4);return x;}
static char *read_file(const char *name){FILE *f=fopen(name,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>=0);rewind(f);char *s=calloc(n+1,1);assert(s&&fread(s,1,n,f)==(size_t)n);fclose(f);return s;}
static cJSON *eval(WebCdp *c,const char *js){cJSON *j=web_cdp_eval(c,js);assert(j);return j;}
static void import(uint32_t *r,unsigned task,const cJSON *j){
 memset(r,0,2048*sizeof *r);r[0]=2;r[1]=task;r[12]=10000;
 r[32]=(unsigned)lround(num(j,"cx")*65536);r[33]=(unsigned)lround(num(j,"cy")*65536);
 const char *q=str(j,"query");r[34]=strstr(q,"horizontal")!=NULL;assert(strlen(q)<256);
 for(unsigned i=0;q[i];i++){assert((unsigned char)q[i]>=32&&(unsigned char)q[i]<=126);r[768+i]=(unsigned char)q[i];}
}
static void compare(WFLoaded *f,const cJSON *j,unsigned seed,unsigned step){
 const uint32_t *r=f->words;WFView v;assert(!wf_observe(f,0,&v));
 const cJSON *points=cJSON_GetObjectItemCaseSensitive(j,"points");assert(cJSON_IsArray(points));
 int mismatch=cJSON_GetArraySize(points)!=(int)r[36]||boolean(j,"active")!=(int)r[37]||boolean(j,"done")!=(r[9]!=0);
 mismatch|=strcmp(str(j,"query"),wf_text_get(&v,v.instruction))!=0;
 for(unsigned i=0;i<r[36]&&i<(unsigned)cJSON_GetArraySize(points);i++){
  const cJSON *p=cJSON_GetArrayItem(points,i),*x=cJSON_GetArrayItem(p,0),*y=cJSON_GetArrayItem(p,1);assert(cJSON_IsNumber(x)&&cJSON_IsNumber(y));
  int delta=fabs(x->valuedouble-r[128+2*(r[36]-i-1)]/256.0)>0.002||fabs(y->valuedouble-r[129+2*(r[36]-i-1)]/256.0)>0.002;
  if(delta)fprintf(stderr,"  point %u browser=%.8f,%.8f model=%.8f,%.8f\n",i,x->valuedouble,y->valuedouble,r[128+2*(r[36]-i-1)]/256.0,r[129+2*(r[36]-i-1)]/256.0);
  mismatch|=delta;
 }
 if(r[9])mismatch|=fabs(num(j,"raw")-real(r[10]))>1e-5||fabs(num(j,"reward")-real(r[11]))>1e-5;
 if(r[9]==WF_TERMINAL)mismatch|=fabs(num(j,"width")-real(r[38]))>0.003||fabs(num(j,"height")-real(r[39]))>0.003;
 if(mismatch){fprintf(stderr,"drawing mismatch task=%u seed=%u step=%u samples=%d/%u active=%d/%u done=%d/%u raw=%g/%g bbox=%g,%g/%g,%g\n",r[1],seed,step,cJSON_GetArraySize(points),r[36],boolean(j,"active"),r[37],boolean(j,"done"),r[9],num(j,"raw"),real(r[10]),num(j,"width"),num(j,"height"),real(r[38]),real(r[39]));abort();}
}
static void mouse(WebCdp *c,unsigned kind,double x,double y,int held){
 cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"type",kind==WF_POINTER_DOWN?"mousePressed":kind==WF_POINTER_UP?"mouseReleased":"mouseMoved");
 cJSON_AddNumberToObject(p,"x",x);cJSON_AddNumberToObject(p,"y",y);
 cJSON_AddStringToObject(p,"button",kind==WF_POINTER_MOVE&&!held?"none":"left");
 cJSON_AddNumberToObject(p,"buttons",kind==WF_POINTER_UP?0:kind==WF_POINTER_DOWN||held?1:0);
 if(kind!=WF_POINTER_MOVE)cJSON_AddNumberToObject(p,"clickCount",1);
 cJSON *j=web_cdp_call(c,"Input.dispatchMouseEvent",p);assert(j);cJSON_Delete(j);
}
static void transition(WebCdp *c,WFLoaded *f,unsigned seed,unsigned step,WFAction a,unsigned ms){
 char js[100];snprintf(js,sizeof js,"__dw.tick(%u)",ms);cJSON *j=eval(c,js);cJSON_Delete(j);
 if(ms<10000){
  if(a.kind==WF_CLICK){j=eval(c,"__dw.submit()");double x=num(j,"x"),y=num(j,"y");cJSON_Delete(j);assert(!web_cdp_click(c,x,y));}
  else if(a.kind>=WF_POINTER_DOWN&&a.kind<=WF_POINTER_UP){snprintf(js,sizeof js,"__dw.point(%.8f,%.8f)",a.arg0/256.0,a.arg1/256.0);j=eval(c,js);double x=num(j,"x"),y=num(j,"y");cJSON_Delete(j);mouse(c,a.kind,x,y,f->words[37]);}
  else assert(a.kind==WF_WAIT);
 }
 a.elapsed_ms=ms;assert(!wf_apply(f,0,&a));assert(!wf_batch_checked(f));signal(SIGABRT,SIG_DFL);
 j=eval(c,"__dw.snapshot()");compare(f,j,seed,step);cJSON_Delete(j);
}
static WFAction pointer(unsigned kind,double x,double y){return (WFAction){.kind=kind,.target=1,.arg0=(unsigned)lround(x*256),.arg1=(unsigned)lround(y*256)};}
int main(int argc,char **argv){
 prctl(PR_SET_DUMPABLE,0,0,0,0);unsigned episodes=argc>1?strtoul(argv[1],0,10):20;if(!episodes||episodes>1000)return 2;
 WFLoaded f;char error[512];assert(!wf_open(&f,"build/webnav/families/drawing/libdrawing.so",error,sizeof error));char *script=read_file("ocean/webnav/families/drawing/browser.js");
 for(unsigned task=0;task<2;task++){
  char path[PATH_MAX],file[PATH_MAX],url[PATH_MAX+8];snprintf(path,sizeof path,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",f.api->task_names[task]);assert(realpath(path,file));snprintf(url,sizeof url,"file://%s",file);
  WebCdp c={.input=-1,.output=-1,.pid=-1};const char *chrome=getenv("WEBNAV_CHROME");if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
  assert(!web_cdp_start_ready(&c,chrome,url,"Boolean(window.core&&core.cover_div)"));cJSON *j=eval(&c,script);cJSON_Delete(j);unsigned actions=0,wins=0;
  for(unsigned ep=0;ep<episodes;ep++){
   mouse(&c,WF_POINTER_UP,0,0,0);unsigned seed=950000+ep,variant=ep%6,step=0;char js[80];snprintf(js,sizeof js,"__dw.reset(%u)",seed);j=eval(&c,js);import(f.words,task,j);assert(!f.api->validate(f.words));compare(&f,j,seed,step);cJSON_Delete(j);
   for(unsigned lane=1;lane<4;lane++)memcpy(f.words+lane*2048,f.words,2048*sizeof *f.words);
   if(variant==0){transition(&c,&f,seed,++step,(WFAction){.kind=WF_CLICK,.target=2},100);}
   else if(variant==1){transition(&c,&f,seed,++step,(WFAction){.kind=WF_WAIT},10000);}
   else if(variant==2){
    double x=f.words[32]/65536.0,y=f.words[33]/65536.0;
    transition(&c,&f,seed,++step,pointer(WF_POINTER_DOWN,x,y),100);
    transition(&c,&f,seed,++step,pointer(WF_POINTER_MOVE,x+2,y+2),200);
    transition(&c,&f,seed,++step,pointer(WF_POINTER_UP,x+2,y+2),300);
    transition(&c,&f,seed,++step,(WFAction){.kind=WF_CLICK,.target=2},400);
   }else{
    if(variant==3){transition(&c,&f,seed,++step,pointer(WF_POINTER_MOVE,20,20),50);transition(&c,&f,seed,++step,pointer(WF_POINTER_DOWN,20,20),100);transition(&c,&f,seed,++step,pointer(WF_POINTER_MOVE,30,40),200);transition(&c,&f,seed,++step,pointer(WF_POINTER_UP,30,40),300);transition(&c,&f,seed,++step,pointer(WF_POINTER_DOWN,50,50),400);transition(&c,&f,seed,++step,pointer(WF_POINTER_UP,50,50),500);transition(&c,&f,seed,++step,(WFAction){.kind=WF_CLICK,.target=2},600);}
    else for(unsigned n=0;n<40&&!f.words[9];n++){
     WFView v;assert(!wf_observe(&f,0,&v));WFAction a;assert(!drawing_public_action(&v,&a));
     if(variant==5&&task==1&&a.kind==WF_POINTER_MOVE){a.arg0=20*256;a.arg1=20*256;}
     step++;transition(&c,&f,seed,step,a,step*100);
    }
   }
   assert(f.words[9]);actions+=step;wins+=real(f.words[10])>=0.999;
  }
  printf("{\"task\":\"%s\",\"episodes\":%u,\"actions\":%u,\"full_credit\":%u,\"differential\":\"PASS\",\"preset\":\"original SVG/D3 pointer events, source-generated center, controlled clock, F32 scoring\"}\n",f.api->task_names[task],episodes,actions,wins);fflush(stdout);web_cdp_close(&c);
 }
 free(script);wf_close(&f);return 0;
}
