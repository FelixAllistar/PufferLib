#define _GNU_SOURCE
#include "../common/loader.h"
#include "../../cdp.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <signal.h>
static double num(const cJSON *j,const char *key){const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsNumber(v));return v->valuedouble;}
static int boolean(const cJSON *j,const char *key){const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsBool(v));return cJSON_IsTrue(v);}
static const char *str(const cJSON *j,const char *key){const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsString(v));return v->valuestring;}
static float real(uint32_t b){float f;memcpy(&f,&b,4);return f;}
static char *read_file(const char *path){FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);char *s=calloc(n+1,1);assert(s&&fread(s,1,n,f)==(size_t)n);fclose(f);return s;}
static void units(uint32_t *out,unsigned cap,const char *s){assert(strlen(s)<cap);for(unsigned i=0;s[i];i++){assert((unsigned char)s[i]>=32&&(unsigned char)s[i]<=126);out[i]=(unsigned char)s[i];}}
static void import(uint32_t *r,unsigned task,unsigned mode,const cJSON *j){memset(r,0,2048*4);r[0]=2;r[1]=task;r[6]=mode;r[12]=(unsigned)num(j,"deadline");r[32]=task>=7?2:task==6?1:0;
 const cJSON *nodes=cJSON_GetObjectItemCaseSensitive(j,"nodes");assert(cJSON_IsArray(nodes));r[33]=cJSON_GetArraySize(nodes);assert(r[33]<=16);units(r+768,256,str(j,"query"));
 for(unsigned i=0;i<r[33];i++){const cJSON *n=cJSON_GetArrayItem(nodes,i);uint32_t *out=r+64+i*40;out[0]=(unsigned)num(n,"role");out[1]=boolean(n,"checked");out[2]=boolean(n,"goal");units(out+4,28,str(n,"name"));out[32]=(uint32_t)(int32_t)num(n,"x");out[33]=(uint32_t)(int32_t)num(n,"y");out[34]=(unsigned)num(n,"width");out[35]=(unsigned)num(n,"height");}
}
int main(int argc,char **argv){
 unsigned episodes=argc>1?strtoul(argv[1],0,10):20;if(!episodes||episodes>10000)return 2;
 WFLoaded f;char error[512];if(wf_open(&f,"build/webnav/families/click/libclick.so",error,sizeof error)){fprintf(stderr,"%s\n",error);return 1;}
 char *script=read_file("ocean/webnav/families/click/browser.js");unsigned total=0;
 for(unsigned task=0;task<10;task++)for(unsigned mode=0;mode<2;mode++){
  char file[PATH_MAX],resolved[PATH_MAX],url[PATH_MAX+8];snprintf(file,sizeof file,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",f.api->task_names[task]);assert(realpath(file,resolved));snprintf(url,sizeof url,"file://%s",resolved);
  WebCdp c={.input=-1,.output=-1,.pid=-1};const char *chrome=getenv("WEBNAV_CHROME");if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";assert(!web_cdp_start_ready(&c,chrome,url,"Boolean(window.core&&core.cover_div)"));cJSON *j=web_cdp_eval(&c,script);assert(j);cJSON_Delete(j);
  unsigned actions=0,wins=0;
  for(unsigned ep=0;ep<episodes;ep++){
   char js[256];snprintf(js,sizeof js,"__cl.reset(%u,%u,%u)",100000+ep,task,mode);j=web_cdp_eval(&c,js);assert(j);import(f.words,task,mode,j);cJSON_Delete(j);assert(!f.api->validate(f.words));for(unsigned lane=1;lane<8;lane++)memcpy(f.words+2048*lane,f.words,2048*4);
   for(unsigned step=1;step<=100&&!f.words[9];step++){
    WFView v;assert(!wf_observe(&f,0,&v));unsigned ref=0;
    /* Deliberate success/failure/timeout schedules are conformance coverage. */
    if(ep%5!=4){
     if(task<7){for(unsigned i=0;i<f.words[33];i++)if((ep%5==1?!f.words[66+40*i]:f.words[66+40*i])){ref=i+1;break;}if(!ref)ref=1;}
     else {for(unsigned i=0;i<f.words[33];i++){const uint32_t *n=f.words+64+i*40;if(n[0]==2&&n[1]!=(ep%5==1?!n[2]:n[2])){ref=i+1;break;}}if(!ref)ref=f.words[33];}
    }
    unsigned now=step*250;snprintf(js,sizeof js,"__cl.tick(%u)",now);j=web_cdp_eval(&c,js);assert(j);cJSON_Delete(j);
    if(ref&&now<f.words[12]){snprintf(js,sizeof js,"__cl.point(%u)",ref);j=web_cdp_eval(&c,js);assert(j);if(cJSON_IsNull(j)){ref=0;cJSON_Delete(j);}else{double x=num(j,"x"),y=num(j,"y");cJSON_Delete(j);assert(!web_cdp_click(&c,x,y));}}
    WFAction a={.kind=ref?WF_CLICK:WF_WAIT,.target=ref,.elapsed_ms=now};assert(!wf_apply(&f,0,&a));for(unsigned l=1;l<8;l++)memcpy(f.words+2048*l,f.words,2048*4);assert(!wf_batch_checked(&f));signal(SIGABRT,SIG_DFL);
    j=web_cdp_eval(&c,"__cl.snapshot()");assert(j);int done=boolean(j,"done");double raw=num(j,"raw"),reward=num(j,"reward");
    int mismatch=done!=(f.words[9]!=0)||(done&&(fabs(raw-real(f.words[10]))>1e-6||fabs(reward-real(f.words[11]))>1e-5));
    const cJSON *selected=cJSON_GetObjectItemCaseSensitive(j,"checked");for(unsigned i=0;i<f.words[33];i++)if(f.words[64+i*40]==2||f.words[64+i*40]==5)mismatch|=cJSON_IsTrue(cJSON_GetArrayItem(selected,i))!=(int)f.words[65+i*40];
    if(mismatch){fprintf(stderr,"Mismatch %s mode=%u seed=%u step=%u ref=%u done %d/%u raw %g/%g reward %g/%g\n",f.api->task_names[task],mode,100000+ep,step,ref,done,f.words[9],raw,real(f.words[10]),reward,real(f.words[11]));cJSON_Delete(j);web_cdp_close(&c);free(script);wf_close(&f);return 1;}cJSON_Delete(j);actions++;
   }
   assert(f.words[9]);wins+=real(f.words[10])>0.99;total++;
  }
  printf("{\"task\":\"%s\",\"data_mode\":%u,\"episodes\":%u,\"actions\":%u,\"scripted_successes\":%u,\"state_reward_conformance\":\"PASS\",\"preset\":\"matched original instances; CDP clicks; unobscured control clicks; controlled clock\"}\n",f.api->task_names[task],mode,episodes,actions,wins);fflush(stdout);web_cdp_close(&c);
 }
 free(script);wf_close(&f);fprintf(stderr,"PASS: %u original-generated click-family episodes\n",total);
}
