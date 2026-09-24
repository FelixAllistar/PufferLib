#define _GNU_SOURCE
#include "bridge.h"
#include "cdp.h"
#include "dom.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int evaluate(WebCdp *c,const char *s){cJSON *r=web_cdp_eval(c,s);if(!r)return -1;cJSON_Delete(r);return 0;}
static int call(WebCdp *c,const char *method,cJSON *p){cJSON *r=web_cdp_call(c,method,p);if(!r)return -1;cJSON_Delete(r);return 0;}
static char *read_file(const char *path){FILE *f=fopen(path,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);char *s=calloc((size_t)n+1,1);if(!s||fread(s,1,(size_t)n,f)!=(size_t)n){free(s);s=NULL;}fclose(f);return s;}
static int ascii(const char *s,unsigned capacity){if(strlen(s)>capacity)return 0;for(;*s;s++)if((unsigned char)*s<32||(unsigned char)*s>126)return 0;return 1;}
static void put(uint32_t *r,unsigned at,const char *s){for(unsigned i=0;s[i];i++)r[at+i]=(unsigned char)s[i];}
static void value(const uint32_t *r,unsigned field,char *s){unsigned n=r[16+4*field];for(unsigned i=0;i<n;i++)s[i]=(char)r[32+32*field+i];s[n]=0;}
static const WebDomNode *node(const WebDom *o,unsigned ref){for(unsigned i=0;i<o->count;i++)if(o->nodes[i].ref==ref)return o->nodes+i;return NULL;}
static int key(WebCdp *c,const char *name,unsigned code,unsigned modifiers){
 for(int up=0;up<2;up++){cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"type",up?"keyUp":"keyDown");cJSON_AddStringToObject(p,"key",name);cJSON_AddNumberToObject(p,"windowsVirtualKeyCode",code);cJSON_AddNumberToObject(p,"modifiers",modifiers);if(call(c,"Input.dispatchKeyEvent",p))return -1;}return 0;
}
static int perform(WebCdp *c,const WebDom *o,const unsigned *refs,unsigned count,unsigned command,unsigned target,const char *text){
 if(command==1||command==10){unsigned at=command==10?count:target;if(at>=count&&command==1)return 0;const WebDomNode *n=node(o,refs[at]);if(!n||web_cdp_click(c,n->x+n->width/2,n->y+n->height/2))return -1;return command==1?key(c,"End",35,0):0;}
 if(command==2){cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"text",text);return call(c,"Input.insertText",p);}
 switch(command){case 3:return key(c,"Backspace",8,0);case 4:return key(c,"Delete",46,0);case 5:return key(c,"ArrowLeft",37,0);case 6:return key(c,"ArrowRight",39,0);case 7:return key(c,"Home",36,0);case 8:return key(c,"End",35,0);case 9:return key(c,"a",65,2);default:return 0;}
}
static int compare(const WebDom *o,const cJSON *r,const cJSON *initial,const uint32_t *w,const unsigned *refs,unsigned count){
 cJSON *expected=cJSON_Duplicate(initial,1),*nodes=cJSON_GetObjectItem(expected,"nodes");
 for(unsigned i=0;i<count;i++){
  char text[65];value(w,i,text);const cJSON *f=cJSON_GetArrayItem(cJSON_GetObjectItem(r,"fields"),(int)i);
  if(!f||strcmp(text,cJSON_GetObjectItem(f,"value")->valuestring)||(unsigned)cJSON_GetObjectItem(f,"start")->valuedouble!=w[17+4*i]||(unsigned)cJSON_GetObjectItem(f,"end")->valuedouble!=w[18+4*i]){fprintf(stderr,"Text/selection mismatch field=%u expected=%s [%u,%u] browser=%s [%g,%g]\n",i,text,w[17+4*i],w[18+4*i],f?cJSON_GetObjectItem(f,"value")->valuestring:"missing",f?cJSON_GetObjectItem(f,"start")->valuedouble:-1,f?cJSON_GetObjectItem(f,"end")->valuedouble:-1);cJSON_Delete(expected);return -1;}
 }
 cJSON *n; cJSON_ArrayForEach(n,nodes){unsigned ref=(unsigned)cJSON_GetObjectItem(n,"ref")->valuedouble,flags=(unsigned)cJSON_GetObjectItem(n,"flags")->valuedouble;flags&=~WEB_FOCUSED;
  for(unsigned i=0;i<=count;i++)if(ref==refs[i]){if(w[2]==i+1)flags|=WEB_FOCUSED;if(i<count){char text[65];value(w,i,text);cJSON_ReplaceItemInObject(n,"value",cJSON_CreateString(text));}}
  cJSON_SetNumberValue(cJSON_GetObjectItem(n,"flags"),flags);
 }
 WebDom wanted;int bad=web_dom_parse(&wanted,expected)||memcmp(o,&wanted,sizeof wanted);cJSON_Delete(expected);if(bad){fprintf(stderr,"Full DOM projection mismatch\n");return -1;}
 int done=cJSON_IsTrue(cJSON_GetObjectItem(r,"done"));if(done!=(w[3]!=0)){fprintf(stderr,"Termination mismatch\n");return -1;}
 if(done){float raw,timed;memcpy(&raw,w+9,sizeof raw);memcpy(&timed,w+10,sizeof timed);if(fabs(raw-cJSON_GetObjectItem(r,"raw")->valuedouble)>1e-6||fabs(timed-cJSON_GetObjectItem(r,"reward")->valuedouble)>1e-6||cJSON_GetObjectItem(r,"elapsed")->valuedouble!=w[5]||w[6]!=w[5]){fprintf(stderr,"Reward/time mismatch\n");return -1;}}
 return 0;
}
static int transition(WebCdp *c,WebDom *o,const cJSON *initial,uint32_t *w,const unsigned *refs,unsigned count,unsigned command,unsigned target,const char *text,unsigned *actions){
 unsigned delta=command==11?10000:137;char expr[100];snprintf(expr,sizeof expr,"__tf.advance(%u);true",delta);if(evaluate(c,expr)||perform(c,o,refs,count,command,target,text))return -1;
 w[7]=command;w[8]=target;w[11]=(unsigned)strlen(text);memset(w+160,0,64*sizeof *w);put(w,160,text);w[5]+=delta;
 for(unsigned i=1;i<32;i++)memcpy(w+i*256,w,256*sizeof *w);webnav_batch(w);
 cJSON *r=web_cdp_eval(c,"__tf.snapshot()");if(!r)return -1;int bad=web_dom_parse(o,cJSON_GetObjectItem(r,"obs"))||compare(o,r,initial,w,refs,count);cJSON_Delete(r);(*actions)++;return bad?-1:0;
}
int main(int argc,char **argv){
 const char *task=argc>1?argv[1]:"enter-text";unsigned tag=!strcmp(task,"enter-text")?4:!strcmp(task,"login-user")?5:!strcmp(task,"read-table")?6:0;int episodes=argc>2?atoi(argv[2]):200;
 if(!tag||episodes<1){fprintf(stderr,"usage: text_oracle enter-text|login-user|read-table [episodes=200]\n");return 2;}
 unsigned count=tag==5?2:1;WebCdp *c=calloc(1,sizeof *c);WebDom *o=calloc(1,sizeof *o);if(!c||!o)return 1;c->input=c->output=-1;c->pid=-1;int status=1; cJSON *initial=NULL;
 char path[4096],file[4096],url[4300];snprintf(path,sizeof path,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",task);if(!realpath(path,file)){perror(path);goto done;}snprintf(url,sizeof url,"file://%s",file);
 const char *chrome=getenv("WEBNAV_CHROME");if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
 if(web_cdp_start_ready(c,chrome,url,"Boolean(window.core&&core.cover_div)"))goto done;
 char *script=read_file("ocean/webnav/web/dom_snapshot.js");if(!script)goto done;int bad=evaluate(c,script);free(script);if(bad)goto done;
 if(evaluate(c,"(()=>{window.__tf={clock:0,elapsed:0};const D=Date;window.Date=class extends D{constructor(...a){super(...(a.length?a:[__tf.clock]))}static now(){return __tf.clock}};const end=core.endEpisode;core.endEpisode=function(r,t,why){if(core.EP_TIMER!==null)__tf.elapsed=Date.now()-core.ept0;return end(r,t,why)};__tf.reset=seed=>{document.activeElement.blur();__tf.clock=0;__tf.elapsed=0;Math.seedrandom(String(seed));core.startEpisodeReal();clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer()};__tf.advance=ms=>{__tf.clock+=ms;if(!WOB_DONE_GLOBAL&&__tf.clock-core.ept0>=core.EPISODE_MAX_TIME)core.endEpisode(-1,false,'timed out')};__tf.snapshot=()=>({obs:webnavDOM(),fields:[...document.querySelectorAll('#area input')].map(e=>({value:e.value,start:e.selectionStart,end:e.selectionEnd})),done:WOB_DONE_GLOBAL,raw:WOB_RAW_REWARD_GLOBAL,reward:WOB_REWARD_GLOBAL,elapsed:__tf.elapsed});__tf.goals=()=>{const q=document.querySelector('#query').textContent;if(document.querySelector('#tab')){const key=document.querySelector('#query .bold').textContent;return [[...document.querySelectorAll('#tab tr')].find(r=>r.cells[0].textContent===key).cells[1].textContent]}return [...q.matchAll(/\"([^\"]*)\"/g)].map(x=>x[1])};return true})()"))goto done;
 unsigned actions=0,success=0,wrong=0,timeouts=0;uint32_t w[8192];
 for(int ep=0;ep<episodes;ep++){
  char expr[100];snprintf(expr,sizeof expr,"__tf.reset(%d);true",200000+ep);if(evaluate(c,expr))goto done;
  cJSON *snap=web_cdp_eval(c,"__tf.snapshot()");if(!snap)goto done;if(web_dom_parse(o,cJSON_GetObjectItem(snap,"obs"))){cJSON_Delete(snap);goto done;}initial=cJSON_Duplicate(cJSON_GetObjectItem(snap,"obs"),1);cJSON_Delete(snap);
  unsigned refs[3]={0},found=0;for(unsigned i=0;i<o->count;i++){if(o->nodes[i].role==WEB_ROLE_TEXTBOX){if(found==count)goto done;refs[found++]=o->nodes[i].ref;}else if(o->nodes[i].role==WEB_ROLE_BUTTON)refs[count]=o->nodes[i].ref;}if(found!=count||!refs[count])goto done;
  cJSON *goals=web_cdp_eval(c,"__tf.goals()");if(!cJSON_IsArray(goals)||cJSON_GetArraySize(goals)!=(int)count){cJSON_Delete(goals);goto done;}
  char targets[2][65]={{0}};memset(w,0,sizeof w);w[0]=tag;w[1]=count;
  for(unsigned i=0;i<count;i++){const char *g=cJSON_GetArrayItem(goals,(int)i)->valuestring;if(!g||!ascii(g,count==1?64:32)){fprintf(stderr,"Unsupported goal at seed %d (no silent omission)\n",200000+ep);cJSON_Delete(goals);goto done;}strcpy(targets[i],g);w[19+4*i]=(unsigned)strlen(g);put(w,96+32*i,g);}cJSON_Delete(goals);
  int schedule=ep%6;
#define DO(cmd,idx,text) do{if(transition(c,o,initial,w,refs,count,cmd,idx,text,&actions)){fprintf(stderr,"task=%s seed=%d schedule=%d command=%u action=%u\n",task,200000+ep,schedule,(unsigned)(cmd),actions);goto done;}}while(0)
  if(schedule==1){DO(10,0,"");}
  else if(schedule==4){DO(1,0,"");DO(2,0,"x");DO(11,0,"");}
  else{
   if(schedule==5){DO(2,0,"ignored");DO(1,99,"");}
   for(unsigned field=0;field<count;field++){
    DO(1,field,"");
    if(schedule==2){DO(2,0,"junk");DO(7,0,"");DO(4,0,"");DO(8,0,"");DO(3,0,"");DO(9,0,"");}
    if(schedule==3){DO(2,0,"AZ");DO(7,0,"");DO(6,0,"");DO(2,0,"x");DO(5,0,"");DO(4,0,"");DO(8,0,"");DO(3,0,"");DO(9,0,"");}
    DO(2,0,targets[field]);
   }
   DO(10,0,"");
  }
#undef DO
  if(w[3]==2)timeouts++;else if(w[4])success++;else wrong++;
  cJSON_Delete(initial);initial=NULL;
 }
 printf("{\"task\":\"%s\",\"episodes\":%d,\"actions\":%u,\"success\":%u,\"wrong\":%u,\"timeouts\":%u,\"conformance\":\"PASS\",\"preset\":\"ASCII focus-end/insert/edit/submit v1; one field <=64 or two <=32 characters\",\"scope\":\"original seeded HTML, real CDP input and keys, complete DOM-v2 projection, caret/selection and reward; controlled clock; no generator or unrestricted-input parity\"}\n",task,episodes,actions,success,wrong,timeouts);status=0;
done:
 cJSON_Delete(initial);web_cdp_close(c);free(c);free(o);return status;
}
