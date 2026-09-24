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
static void import(uint32_t *r,unsigned task,const cJSON *j){memset(r,0,8192*4);r[0]=2;r[1]=task;r[12]=(unsigned)num(j,"deadline");r[32]=(unsigned)num(j,"panels");r[33]=(unsigned)num(j,"active");r[34]=boolean(j,"accordion");r[35]=(unsigned)num(j,"target_tab");r[36]=(unsigned)num(j,"medium");r[37]=task==5||task==7;
 const cJSON *nodes=cJSON_GetObjectItemCaseSensitive(j,"nodes");assert(cJSON_IsArray(nodes));r[38]=cJSON_GetArraySize(nodes);assert(r[38]<=128);units(r+6208,256,str(j,"query"));
 for(unsigned i=0;i<r[38];i++){const cJSON *n=cJSON_GetArrayItem(nodes,i);uint32_t *out=r+64+i*48;out[0]=(unsigned)num(n,"kind");out[1]=(unsigned)num(n,"panel");out[2]=boolean(n,"goal");units(out+4,40,str(n,"name"));}
}
int main(int argc,char **argv){
 unsigned episodes=argc>1?strtoul(argv[1],0,10):20;if(!episodes||episodes>10000)return 2;
 WFLoaded f;char error[512];if(wf_open(&f,"build/webnav/families/panels/libpanels.so",error,sizeof error)){fprintf(stderr,"%s\n",error);return 1;}
 char *script=read_file("ocean/webnav/families/panels/browser.js");unsigned total=0;
 for(unsigned task=0;task<9;task++){
  char file[PATH_MAX],resolved[PATH_MAX],url[PATH_MAX+8];snprintf(file,sizeof file,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",f.api->task_names[task]);assert(realpath(file,resolved));snprintf(url,sizeof url,"file://%s",resolved);
  WebCdp c={.input=-1,.output=-1,.pid=-1};const char *chrome=getenv("WEBNAV_CHROME");if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";assert(!web_cdp_start_ready(&c,chrome,url,"Boolean(window.core&&core.cover_div)"));cJSON *j=web_cdp_eval(&c,script);assert(j);cJSON_Delete(j);
  unsigned actions=0,wins=0;
  for(unsigned ep=0;ep<episodes;ep++){
   char js[256];snprintf(js,sizeof js,"__pan.reset(%u,%u)",100000+ep,task);j=web_cdp_eval(&c,js);assert(j);import(f.words,task,j);cJSON_Delete(j);assert(!f.api->validate(f.words));for(unsigned lane=1;lane<4;lane++)memcpy(f.words+8192*lane,f.words,8192*4);
   for(unsigned step=1;step<=100&&!f.words[9];step++){
    WFView v;assert(!wf_observe(&f,0,&v));unsigned ref=0;
    /* Mixed traces: source-target successes, deliberate wrong actions and timeouts. */
    if(ep%5!=4){
     if(ep%5==0&&step==1&&task>=5&&task<=6)ref=f.words[38];
     else if(task==0)ref=ep%5==1?1:f.words[35];
     else if(task==5||task==6)ref=step==1?1:f.words[38];
     else {for(unsigned n=0;n<v.count;n++){unsigned k=v.nodes[n].ref;const uint32_t *r=f.words+64+(k-1)*48;if(r[0]==1&&(ep%5==1||r[2])){ref=k;break;}}
      if(!ref){ref=task==3?2:1+(step-1)%f.words[32];}}
    }
    unsigned now=step*250;snprintf(js,sizeof js,"__pan.tick(%u)",now);j=web_cdp_eval(&c,js);assert(j);cJSON_Delete(j);
    if(ref&&now<f.words[12]){snprintf(js,sizeof js,"__pan.point(%u)",ref);j=web_cdp_eval(&c,js);assert(j&&boolean(j,"visible"));double x=num(j,"x"),y=num(j,"y");cJSON_Delete(j);assert(!web_cdp_click(&c,x,y));}
    WFAction a={.kind=ref?WF_CLICK:WF_WAIT,.target=ref,.elapsed_ms=now};assert(!wf_apply(&f,0,&a));for(unsigned l=1;l<4;l++)memcpy(f.words+8192*l,f.words,8192*4);assert(!wf_batch_checked(&f));signal(SIGABRT,SIG_DFL);
    j=web_cdp_eval(&c,"__pan.snapshot()");assert(j);int done=boolean(j,"done");double raw=num(j,"raw"),reward=num(j,"reward");
    if((unsigned)num(j,"active")!=f.words[33]||done!=(f.words[9]!=0)||(done&&(fabs(raw-real(f.words[10]))>1e-6||fabs(reward-real(f.words[11]))>1e-5))){fprintf(stderr,"Mismatch %s seed=%u step=%u ref=%u active %.0f/%u done %d/%u raw %g/%g reward %g/%g\n",f.api->task_names[task],100000+ep,step,ref,num(j,"active"),f.words[33],done,f.words[9],raw,real(f.words[10]),reward,real(f.words[11]));return 1;}cJSON_Delete(j);actions++;
   }
   assert(f.words[9]);wins+=real(f.words[10])>0.99;total++;
  }
  printf("{\"task\":\"%s\",\"episodes\":%u,\"actions\":%u,\"scripted_successes\":%u,\"state_reward_conformance\":\"PASS\",\"preset\":\"matched original instances; CDP clicks; settled animations; controlled clock\"}\n",f.api->task_names[task],episodes,actions,wins);fflush(stdout);web_cdp_close(&c);
 }
 free(script);wf_close(&f);fprintf(stderr,"PASS: %u original-generated panel episodes\n",total);
}
