#define _GNU_SOURCE
#include "bridge.h"
#include "cdp.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int eval(WebCdp *c,const char *js){cJSON *r=web_cdp_eval(c,js);if(!r)return -1;cJSON_Delete(r);return 0;}
static int call(WebCdp *c,const char *method,cJSON *p){cJSON *r=web_cdp_call(c,method,p);if(!r)return -1;cJSON_Delete(r);return 0;}
static unsigned num(const cJSON *o,const char *k){return (unsigned)cJSON_GetNumberValue(cJSON_GetObjectItem(o,k));}
static int flag(const cJSON *o,const char *k){return cJSON_IsTrue(cJSON_GetObjectItem(o,k));}
static const char *str(const cJSON *o,const char *k){return cJSON_GetStringValue(cJSON_GetObjectItem(o,k));}
static int ascii(const char *s,unsigned cap){if(!s||strlen(s)>cap)return 0;for(;*s;s++)if((unsigned char)*s<32||(unsigned char)*s>126)return 0;return 1;}
static void put(uint32_t *r,unsigned at,const char *s){for(unsigned i=0;s[i];i++)r[at+i]=(unsigned char)s[i];}
static int equal(const uint32_t *r,unsigned at,unsigned n,const char *s){if(!s||strlen(s)!=n)return 0;for(unsigned i=0;i<n;i++)if(r[at+i]!=(unsigned char)s[i])return 0;return 1;}
static void batch(uint32_t *w){for(unsigned i=1;i<32;i++)memcpy(w+i*256,w,256*sizeof *w);webnav_batch(w);}
static int key(WebCdp *c,const char *name,unsigned code,unsigned modifiers){
    for(unsigned up=0;up<2;up++){cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"type",up?"keyUp":"keyDown");cJSON_AddStringToObject(p,"key",name);cJSON_AddNumberToObject(p,"windowsVirtualKeyCode",code);cJSON_AddNumberToObject(p,"modifiers",modifiers);if(call(c,"Input.dispatchKeyEvent",p))return -1;}return 0;
}
static int click(WebCdp *c,const char *expression){
    char js[1024];snprintf(js,sizeof js,"(()=>{const e=%s;if(!e)return null;const b=e.getBoundingClientRect();return {x:b.x+b.width/2,y:b.y+b.height/2}})()",expression);
    cJSON *p=web_cdp_eval(c,js);if(!cJSON_IsObject(p)){cJSON_Delete(p);return -1;}
    double x=cJSON_GetNumberValue(cJSON_GetObjectItem(p,"x")),y=cJSON_GetNumberValue(cJSON_GetObjectItem(p,"y"));cJSON_Delete(p);return web_cdp_click(c,x,y);
}
static int perform(WebCdp *c,const uint32_t *r,unsigned command,unsigned index,const char *text){
    if(command==1)return click(c,"document.querySelector('#tags')")||key(c,"End",35,0);
    if(command==2){cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"text",text);return call(c,"Input.insertText",p);}
    /* The menu can cover the button. This high-level submit preset first
     * clicks blank page space to blur/close it, then clicks the real button.
     * It never invokes the handler or assigns the input value directly. */
    if(command==10)return web_cdp_click(c,240,240)||click(c,"document.querySelector('#subbtn')");
    if(command==14){if(!r[15]||index>=r[20])return 0;char js[256];snprintf(js,sizeof js,"$('#tags').autocomplete('instance').menu.element.children('.ui-menu-item')[%u]",index);return click(c,js);}
    switch(command){case 3:return key(c,"Backspace",8,0);case 4:return key(c,"Delete",46,0);case 5:return key(c,"ArrowLeft",37,0);case 6:return key(c,"ArrowRight",39,0);case 7:return key(c,"Home",36,0);case 8:return key(c,"End",35,0);case 9:return key(c,"a",65,2);case 12:return key(c,"ArrowDown",40,0);case 13:return key(c,"ArrowUp",38,0);case 15:return key(c,"Enter",13,0);default:return 0;}
}
static cJSON *snapshot(WebCdp *c){
    /* delay=0 search callbacks registered by input run before this callback.
     * Await the event loop, without advancing the controlled episode clock. */
    return web_cdp_eval(c,"new Promise(resolve=>setTimeout(()=>resolve(__ac.snapshot()),0))");
}
static int compare(const cJSON *s,const uint32_t *r){
    if(!equal(r,32,r[16],str(s,"value"))||r[17]!=num(s,"start")||r[18]!=num(s,"end")||r[2]!=num(s,"focus")||r[15]!=num(s,"menu")||r[20]!=num(s,"count")||r[21]!=num(s,"active")||(r[15]&&!equal(r,96,r[19],str(s,"term")))||flag(s,"done")!=(r[3]!=0)){
        char *json=cJSON_PrintUnformatted(s);fprintf(stderr,"autocomplete mismatch browser=%s\nmodel focus=%u fieldlen=%u selection=%u/%u menu=%u count=%u active=%u termlen=%u outcome=%u\n",json?json:"null",r[2],r[16],r[17],r[18],r[15],r[20],r[21],r[19],r[3]);free(json);return -1;
    }
    if(r[3]){float raw,timed;memcpy(&raw,r+9,4);memcpy(&timed,r+10,4);if(fabs(raw-cJSON_GetNumberValue(cJSON_GetObjectItem(s,"raw")))>1e-6||fabs(timed-cJSON_GetNumberValue(cJSON_GetObjectItem(s,"reward")))>1e-6||r[6]!=num(s,"elapsed")){fprintf(stderr,"autocomplete reward/time mismatch\n");return -1;}}
    return 0;
}
static int step(WebCdp *c,uint32_t *w,unsigned command,unsigned index,const char *text,unsigned *actions){
    unsigned delta=command==11?10000:137;char js[128];snprintf(js,sizeof js,"__ac.advance(%u);true",delta);if(eval(c,js))return -1;
    if(w[3]==0&&w[5]+delta<10000&&perform(c,w,command,index,text))return -1;
    w[7]=command;w[8]=index;w[5]+=delta;w[11]=(unsigned)strlen(text);memset(w+224,0,32*sizeof *w);put(w,224,text);batch(w);(*actions)++;
    cJSON *s=snapshot(c);if(!s)return -1;int bad=compare(s,w);cJSON_Delete(s);return bad;
}
int main(int argc,char **argv){
    int episodes=argc>1?atoi(argv[1]):200;if(episodes<1)return 2;int status=1;
    WebCdp *c=calloc(1,sizeof *c);if(!c)return 1;c->input=c->output=-1;c->pid=-1;
    char path[4096],url[4300];if(!realpath("build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/use-autocomplete-nodelay.html",path))goto done;
    snprintf(url,sizeof url,"file://%s",path);const char *chrome=getenv("WEBNAV_CHROME");if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
    if(web_cdp_start_ready(c,chrome,url,"Boolean(window.core&&core.cover_div)"))goto done;
    if(eval(c,
        "(()=>{window.__ac={clock:0,elapsed:0};const D=Date;window.Date=class extends D{constructor(...a){super(...(a.length?a:[__ac.clock]))}static now(){return __ac.clock}};"
        "const end=core.endEpisode;core.endEpisode=function(r,t,why){if(core.EP_TIMER!==null)__ac.elapsed=Date.now()-core.ept0;return end(r,t,why)};"
        "__ac.reset=seed=>{document.activeElement.blur();const a=$('#tags').autocomplete('instance');if(a){clearTimeout(a.searching);$('#tags').autocomplete('destroy')}__ac.clock=0;__ac.elapsed=0;Math.seedrandom(String(seed));core.startEpisodeReal();clearTimeout(core.EP_TIMER);core.EP_TIMER=-1;core.clearTimer()};"
        "__ac.advance=ms=>{__ac.clock+=ms;if(!WOB_DONE_GLOBAL&&__ac.clock-core.ept0>=core.EPISODE_MAX_TIME)core.endEpisode(-1,false,'timed out')};"
        "__ac.goals=()=>{const parts=[...document.querySelector('#query').textContent.matchAll(/\"([^\"]*)\"/g)].map(x=>x[1]);const candidates=ui_utils.COUNTRIES.filter(s=>s.startsWith(parts[0]));const target=candidates.find(s=>parts.length===1||s.endsWith(parts[1]));return {prefix:parts[0],suffix:parts[1]||'zz',match_end:parts.length===2,target,index:candidates.indexOf(target)}};"
        "__ac.snapshot=()=>{const e=document.querySelector('#tags'),a=$('#tags').autocomplete('instance'),m=a.menu.element,open=m.is(':visible'),items=m.children('.ui-menu-item'),active=open&&a.menu.active?items.index(a.menu.active)+1:0;return {value:e.value,start:e.selectionStart,end:e.selectionEnd,focus:document.activeElement===e?1:document.activeElement===document.querySelector('#subbtn')?2:0,menu:open?(active?2:1):0,count:open?items.length:0,active,term:a.term||'',done:WOB_DONE_GLOBAL,raw:WOB_RAW_REWARD_GLOBAL,reward:WOB_REWARD_GLOBAL,elapsed:__ac.elapsed}};return true})()"))goto done;
    unsigned actions=0,success=0,wrong=0,timeouts=0;uint32_t w[8192];
    for(int ep=0;ep<episodes;ep++){
        char js[128];snprintf(js,sizeof js,"__ac.reset(%d);true",400000+ep);if(eval(c,js))goto done;
        cJSON *g=web_cdp_eval(c,"__ac.goals()");if(!g)goto done;
        char prefix[6],suffix[6],target[65];if(!ascii(str(g,"prefix"),5)||!ascii(str(g,"suffix"),5)||!ascii(str(g,"target"),64)){cJSON_Delete(g);goto done;}
        strcpy(prefix,str(g,"prefix"));strcpy(suffix,str(g,"suffix"));strcpy(target,str(g,"target"));unsigned choice=num(g,"index");
        memset(w,0,sizeof w);w[0]=10;w[1]=1;w[12]=flag(g,"match_end");w[13]=(unsigned)strlen(prefix);w[14]=(unsigned)strlen(suffix);put(w,160,prefix);put(w,192,suffix);cJSON_Delete(g);
        cJSON *s=snapshot(c);if(!s||compare(s,w)){cJSON_Delete(s);goto done;}cJSON_Delete(s);
#define STEP(cmd,idx,text) do{if(step(c,w,cmd,idx,text,&actions)){fprintf(stderr,"autocomplete seed=%d schedule=%d command=%u\n",400000+ep,ep%8,(unsigned)(cmd));goto done;}}while(0)
        unsigned mode=(unsigned)ep%8;
        if(mode==1){STEP(10,0,"");}
        else{
            STEP(1,0,"");
            if(mode==0){char chunk[33];size_t n=strlen(target),first=n>32?32:n;memcpy(chunk,target,first);chunk[first]=0;STEP(2,0,chunk);if(n>32){STEP(2,0,target+32);}}
            else if(mode==6){char invented[32];snprintf(invented,sizeof invented,"%s-not-a-country-%s",prefix,w[12]?suffix:"");STEP(2,0,invented);}
            else{
                STEP(2,0,prefix);
                if(mode==2){STEP(12,0,"");STEP(13,0,"");STEP(12,0,"");STEP(15,0,"");}
                if(mode==3){STEP(14,choice,"");}
                if(mode==4){STEP(13,0,"");STEP(12,0,"");STEP(9,0,"");char lower[6];strcpy(lower,prefix);if(lower[0]>='A'&&lower[0]<='Z')lower[0]+='a'-'A';STEP(2,0,lower);}
                if(mode==5){STEP(11,0,"");}
                if(mode==7){STEP(14,choice,"");STEP(12,0,"");STEP(12,0,"");STEP(15,0,"");}
            }
            if(w[3]==0){STEP(10,0,"");}
        }
        if(w[3]==2)timeouts++;else if(w[4])success++;else wrong++;
        STEP(0,0,"");
#undef STEP
    }
    printf("{\"task\":\"use-autocomplete-nodelay\",\"episodes\":%d,\"actions\":%u,\"success\":%u,\"wrong\":%u,\"timeouts\":%u,\"conformance\":\"PASS\",\"scope\":\"original seeded goals/handlers, real CDP input/keys/menu clicks, field/caret/focus/menu count/active/term and reward; controlled clock and settled zero-delay callbacks; fresh widget each reset; no full DOM, generator, arbitrary mouse or wall-clock parity\"}\n",episodes,actions,success,wrong,timeouts);status=0;
done:web_cdp_close(c);free(c);return status;
}
