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

static double num(const cJSON *j,const char *k){const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,k);assert(cJSON_IsNumber(v));return v->valuedouble;}
static unsigned boolean(const cJSON *j,const char *k){const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,k);assert(cJSON_IsBool(v));return cJSON_IsTrue(v);}
static const char *str(const cJSON *j,const char *k){const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,k);assert(cJSON_IsString(v));return v->valuestring;}
static float real(uint32_t x){float f;memcpy(&f,&x,4);return f;}
static char *read_file(const char *name){FILE *f=fopen(name,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>=0);rewind(f);char *s=calloc(n+1,1);assert(s&&fread(s,1,n,f)==(size_t)n);fclose(f);return s;}
static void units(uint32_t *dst,unsigned cap,const char *s,unsigned n){assert(n<=cap);for(unsigned i=0;i<n;i++){assert((unsigned char)s[i]>=32&&(unsigned char)s[i]<=126);dst[i]=(unsigned char)s[i];}}
static void import(uint32_t *r,unsigned task,const cJSON *j){
    memset(r,0,2048*sizeof *r);r[0]=2;r[1]=task;r[12]=10000;uint32_t *b=r+32;b[0]=10;b[1]=1;
    const char *q=str(j,"query"),*p=strchr(q,'"'),*e=p?strchr(p+1,'"'):NULL;assert(e);
    b[13]=(unsigned)(e-p-1);units(b+160,32,p+1,b[13]);
    const char *s=strchr(e+1,'"'),*t=s?strchr(s+1,'"'):NULL;b[12]=t!=NULL;
    b[14]=t?(unsigned)(t-s-1):2;units(b+192,32,t?s+1:"zz",b[14]);
    units(r+320,127,q,(unsigned)strlen(q));
    assert(!strcmp(str(j,"value"),"")&&!boolean(j,"menu"));
}
static void key(WebCdp *c,const char *name,unsigned code,unsigned modifiers){
    for(unsigned i=0;i<2;i++){
        cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"type",i?"keyUp":"keyDown");
        cJSON_AddStringToObject(p,"key",name);cJSON_AddNumberToObject(p,"windowsVirtualKeyCode",code);
        cJSON_AddNumberToObject(p,"nativeVirtualKeyCode",code);cJSON_AddNumberToObject(p,"modifiers",modifiers);
        cJSON *r=web_cdp_call(c,"Input.dispatchKeyEvent",p);assert(r);cJSON_Delete(r);
    }
}
static void dispatch(WebCdp *c,const WFAction *a){
    char js[80];cJSON *j;
    if(a->kind==WF_CLICK){snprintf(js,sizeof js,"__ac.point(%u)",a->target);j=web_cdp_eval(c,js);assert(j&&!cJSON_IsNull(j));double x=num(j,"x"),y=num(j,"y");cJSON_Delete(j);assert(!web_cdp_click(c,x,y));}
    else if(a->kind==WF_INSERT){cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"text",a->text);j=web_cdp_call(c,"Input.insertText",p);assert(j);cJSON_Delete(j);}
    else if(a->kind==WF_SELECT_ALL)key(c,"a",65,2);
    else if(a->kind==WF_BACKSPACE)key(c,"Backspace",8,0);
    else if(a->kind==WF_ENTER)key(c,"Enter",13,0);
    else if(a->kind==WF_KEY_DOWN)key(c,a->arg0==40?"ArrowDown":"ArrowUp",a->arg0,0);
    else assert(a->kind==WF_WAIT);
}
static void compare(WFLoaded *f,const cJSON *j,unsigned seed,unsigned step){
    WFView v;assert(!wf_observe(f,0,&v));const uint32_t *b=f->words+32;
    const cJSON *items=cJSON_GetObjectItemCaseSensitive(j,"items");assert(cJSON_IsArray(items));
    int mismatch=boolean(j,"done")!=(f->words[9]!=0)||strcmp(str(j,"value"),wf_text_get(&v,v.nodes[0].value));
    mismatch|=(unsigned)num(j,"start")!=b[17]||(unsigned)num(j,"end")!=b[18];
    mismatch|=boolean(j,"menu")!=(b[15]!=0)||(unsigned)cJSON_GetArraySize(items)!=b[20]||(unsigned)num(j,"active")!=b[21];
    for(unsigned i=0;i<b[20]&&i<64;i++){const cJSON *item=cJSON_GetArrayItem(items,i);if(!cJSON_IsString(item)||strcmp(item->valuestring,wf_text_get(&v,v.nodes[i+2].name)))mismatch=1;}
    if(f->words[9])mismatch|=fabs(num(j,"raw")-real(f->words[10]))>1e-6||fabs(num(j,"reward")-real(f->words[11]))>1e-5;
    if(mismatch){fprintf(stderr,"autocomplete mismatch seed=%u step=%u value=%s/%s selection=%.0f,%.0f/%u,%u menu=%u/%u count=%d/%u active=%.0f/%u done=%u/%u\n",seed,step,str(j,"value"),wf_text_get(&v,v.nodes[0].value),num(j,"start"),num(j,"end"),b[17],b[18],boolean(j,"menu"),b[15],cJSON_GetArraySize(items),b[20],num(j,"active"),b[21],boolean(j,"done"),f->words[9]);abort();}
}
static void transition(WebCdp *c,WFLoaded *f,unsigned seed,unsigned step,
                       WFAction a,unsigned ms){
    char js[80];snprintf(js,sizeof js,"__ac.tick(%u)",ms);
    cJSON *j=web_cdp_eval(c,js);assert(j);cJSON_Delete(j);
    if(ms<10000)dispatch(c,&a);
    a.elapsed_ms=ms;assert(!wf_apply(f,0,&a));assert(!wf_batch_checked(f));
    signal(SIGABRT,SIG_DFL);
    j=web_cdp_eval(c,"__ac.settled()");assert(j);compare(f,j,seed,step);cJSON_Delete(j);
}
static unsigned delay_trace(WebCdp *c,WFLoaded *f,unsigned seed,unsigned variant){
    unsigned n=0;
#define DO(ms,...) transition(c,f,seed,++n,(WFAction){__VA_ARGS__},ms)
    DO(0,.kind=WF_CLICK,.target=1);
    if(variant==7){
        DO(9800,.kind=WF_INSERT,.text="Al",.text_length=2);
        DO(10000,.kind=WF_WAIT);
    }else{
        DO(100,.kind=WF_INSERT,.text="Al",.text_length=2);
        if(variant==5){
            DO(399,.kind=WF_WAIT);DO(400,.kind=WF_WAIT);
            DO(410,.kind=WF_SELECT_ALL);
            DO(450,.kind=WF_INSERT,.text="Ba",.text_length=2);
            DO(749,.kind=WF_WAIT);DO(750,.kind=WF_WAIT);
            DO(760,.kind=WF_CLICK,.target=3);
            DO(800,.kind=WF_CLICK,.target=2);
        }else{
            DO(150,.kind=WF_KEY_DOWN,.arg0=40);
            DO(160,.kind=WF_KEY_DOWN,.arg0=40);
            DO(170,.kind=WF_ENTER);
            DO(399,.kind=WF_WAIT);DO(400,.kind=WF_WAIT);
            DO(410,.kind=WF_CLICK,.target=3);
            DO(500,.kind=WF_CLICK,.target=2);
        }
    }
#undef DO
    return n;
}
int main(int argc,char **argv){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    unsigned episodes=argc>1?strtoul(argv[1],0,10):100;
    if(!episodes||episodes>10000)return 2;
    WFLoaded f;char error[512];
    assert(!wf_open(&f,"build/webnav/families/autocomplete/libautocomplete.so",error,sizeof error));
    char *script=read_file("ocean/webnav/families/autocomplete/browser.js");
    for(unsigned task=0;task<2;task++){
        char path[PATH_MAX],file[PATH_MAX],url[PATH_MAX+8];
        snprintf(path,sizeof path,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",f.api->task_names[task]);
        assert(realpath(path,file));snprintf(url,sizeof url,"file://%s",file);
        WebCdp c={.input=-1,.output=-1,.pid=-1};
        const char *chrome=getenv("WEBNAV_CHROME");
        if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
        assert(!web_cdp_start_ready(&c,chrome,url,"Boolean(window.core&&core.cover_div)"));
        cJSON *j=web_cdp_eval(&c,script);assert(j);cJSON_Delete(j);
        unsigned actions=0,wins=0;
        for(unsigned ep=0;ep<episodes;ep++){
            unsigned seed=920000+ep,variant=ep%(task?8:5);char js[128];
            snprintf(js,sizeof js,"__ac.reset(%u)",seed);j=web_cdp_eval(&c,js);assert(j);
            import(f.words,task,j);assert(!f.api->validate(f.words));compare(&f,j,seed,0);cJSON_Delete(j);
            for(unsigned l=1;l<4;l++)memcpy(f.words+l*2048,f.words,2048*sizeof *f.words);
            if(variant>=5)actions+=delay_trace(&c,&f,seed,variant);
            else for(unsigned step=1;step<=32&&!f.words[9];step++){
                WFView v;assert(!wf_observe(&f,0,&v));char scratch[128];
                WFAction a={.kind=WF_WAIT};unsigned ms=variant==4?10000:step*200;
                if(variant!=4){
                    assert(!autocomplete_public_next(&v,scratch,sizeof scratch,&a));
                    if(variant==1){if(step==2)a=(WFAction){.kind=WF_INSERT,.text="invalid",.text_length=7};if(step>=3)a=(WFAction){.kind=WF_CLICK,.target=2};}
                    if(variant==2){if(step==3)a=(WFAction){.kind=WF_KEY_DOWN,.arg0=40};if(step==4)a=(WFAction){.kind=WF_KEY_DOWN,.arg0=38};}
                    if(variant==3){if(step==3)a=(WFAction){.kind=WF_SELECT_ALL};if(step==4)a=(WFAction){.kind=WF_BACKSPACE};}
                }
                transition(&c,&f,seed,step,a,ms);actions++;
            }
            assert(f.words[9]);wins+=real(f.words[10])>0.99;
        }
        printf("{\"task\":\"%s\",\"episodes\":%u,\"actions\":%u,\"scripted_successes\":%u,\"differential\":\"PASS\",\"preset\":\"original-generated constraints, CDP input/menu/keys, original search callbacks on controlled clock\"}\n",f.api->task_names[task],episodes,actions,wins);
        web_cdp_close(&c);
    }
    free(script);wf_close(&f);return 0;
}
