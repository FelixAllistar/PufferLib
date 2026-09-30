#define _GNU_SOURCE
#include "../common/loader.h"
#include "../../cdp.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

#define ROW 2048u
static unsigned comparisons;
static double number(const cJSON *j,const char *key){
    const cJSON *x=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsNumber(x));return x->valuedouble;
}
static const char *string(const cJSON *j,const char *key){
    const cJSON *x=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsString(x));return x->valuestring;
}
static const cJSON *array(const cJSON *j,const char *key){
    const cJSON *x=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsArray(x));return x;
}
static int boolean(const cJSON *j,const char *key){
    const cJSON *x=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsBool(x));return cJSON_IsTrue(x);
}
static float real(uint32_t bits){float f;memcpy(&f,&bits,4);return f;}
static char *read_file(const char *path){
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));
    long n=ftell(f);assert(n>=0);rewind(f);char *s=calloc((size_t)n+1,1);
    assert(s&&fread(s,1,(size_t)n,f)==(size_t)n);fclose(f);return s;
}
static void put(uint32_t *r,unsigned at,unsigned cap,const char *s){
    size_t n=strlen(s);assert(n<cap);
    for(size_t i=0;i<n;i++){assert((unsigned char)s[i]>=32&&(unsigned char)s[i]<=126);r[at+i]=(unsigned char)s[i];}
}
static cJSON *eval(WebCdp *c,const char *js){cJSON *j=web_cdp_eval(c,js);assert(j);return j;}
static void import_browser(WFLoaded *f,unsigned task,const cJSON *j){
    uint32_t *r=f->words;memset(r,0,ROW*4*sizeof *r);
    r[WF_VERSION]=WF_ABI_VERSION;r[WF_TASK]=task;r[WF_DEADLINE]=(unsigned)number(j,"deadline");
    put(r,512,256,string(j,"instruction"));
    if(task==0){
        const cJSON *xs=array(j,"options");r[32]=(unsigned)cJSON_GetArraySize(xs);
        assert(r[32]>=3&&r[32]<=9);
        r[42]=(unsigned)number(j,"selected");
        const cJSON *target=cJSON_GetObjectItemCaseSensitive(j,"goal");assert(cJSON_IsString(target));
        int found=0;
        for(unsigned i=0;i<r[32];i++){
            const cJSON *x=cJSON_GetArrayItem(xs,(int)i);assert(cJSON_IsString(x));
            put(r,1024+i*64,64,x->valuestring);
            if(!strcmp(x->valuestring,target->valuestring)){r[43]=i;found++;}
        }
        assert(found==1);
    }else if(task==1||task==2){
        const cJSON *xs=array(j,"sliders");assert(cJSON_GetArraySize(xs)==(task==1?1:3));
        const cJSON *first=cJSON_GetArrayItem(xs,0);
        unsigned bias=task==1?100:0;
        r[33]=(unsigned)((int)number(first,"min")+(int)bias);
        r[34]=(unsigned)((int)number(first,"max")+(int)bias);
        r[35]=!strcmp(string(first,"orientation"),"vertical");
        for(int i=0;i<cJSON_GetArraySize(xs);i++){
            const cJSON *x=cJSON_GetArrayItem(xs,i);
            r[36+i*2]=(unsigned)((int)number(x,"value")+(int)bias);
            const cJSON *goal=cJSON_GetObjectItemCaseSensitive(j,"goal");
            r[37+i*2]=(unsigned)((int)(task==1?goal->valuedouble:
                cJSON_GetArrayItem(goal,i)->valuedouble)+(int)bias);
        }
    }else if(task==3){
        r[33]=0;r[34]=0;r[36]=(uint32_t)atoi(string(j,"value"));
        r[37]=(uint32_t)(int)number(j,"goal");
    }else{
        const char *color=string(j,"color");r[46]=(unsigned)number(j,"goal");
        r[47]=r[48]=r[49]=(unsigned)strlen(color);assert(r[49]<32);
        put(r,768,32,color);
    }
    for(unsigned lane=1;lane<4;lane++)memcpy(f->words+lane*ROW,r,ROW*sizeof *r);
    for(unsigned lane=0;lane<4;lane++)assert(!f->api->validate(f->words+lane*ROW));
}
static void compare(WFLoaded *f,const cJSON *j){
    WFView v;assert(!wf_observe(f,0,&v));uint32_t *r=f->words;
    assert(!strcmp(wf_text_get(&v,v.instruction),string(j,"instruction")));
    assert(v.deadline_ms==(unsigned)number(j,"deadline"));
    assert((r[WF_STATUS]!=WF_RUNNING)==boolean(j,"done"));
    if(r[WF_STATUS]!=WF_RUNNING){
        /* Original JS computes in double; the Bend model emits F32. */
        assert(fabs(real(r[WF_RAW_REWARD])-number(j,"raw"))<2e-5);
        assert(fabs(real(r[WF_TIMED_REWARD])-number(j,"reward"))<2e-5);
    }
    unsigned t=r[WF_TASK];
    if(t==0){
        assert(r[42]==(unsigned)number(j,"selected"));
        const cJSON *xs=array(j,"options");assert(cJSON_GetArraySize(xs)==(int)r[32]);
        for(unsigned i=0;i<r[32];i++){
            const char *name=NULL;
            for(unsigned k=0;k<v.count;k++)if(v.nodes[k].ref==10+i)name=wf_text_get(&v,v.nodes[k].name);
            assert(name&&!strcmp(name,cJSON_GetArrayItem(xs,(int)i)->valuestring));
        }
    }else if(t==1||t==2){
        const cJSON *xs=array(j,"sliders");
        for(int i=0;i<cJSON_GetArraySize(xs);i++){
            const cJSON *x=cJSON_GetArrayItem(xs,i);
            int value=(int)r[36+i*2]-(t==1?100:0);
            if(value!=(int)number(x,"value")){
                fprintf(stderr,"slider mismatch task=%u slider=%d model=%d browser=%g range=%g..%g orientation=%s action=%u fraction=%u\n",
                        t,i,value,number(x,"value"),number(x,"min"),number(x,"max"),
                        string(x,"orientation"),r[WF_ACTION],r[WF_ARG0]);abort();
            }
        }
    }else if(t==3){
        if((int32_t)r[36]!=atoi(string(j,"value"))){
            fprintf(stderr,"spinner mismatch model=%d browser=%s action=%u target=%u elapsed=%u\n",
                    (int32_t)r[36],string(j,"value"),r[WF_ACTION],r[WF_TARGET],r[WF_ELAPSED]);abort();
        }
    }
    else{
        char color[32]={0};for(unsigned i=0;i<r[49];i++)color[i]=(char)r[768+i];
        assert(!strcasecmp(color,string(j,"color")));
    }
    comparisons++;
}
static cJSON *act(WFLoaded *f,WebCdp *c,const char *js,unsigned kind,unsigned ref,
                   unsigned arg0,unsigned arg1,const char *text,unsigned ms){
    char clock_js[64];snprintf(clock_js,sizeof clock_js,"__controls.advance(%u)",ms);
    cJSON *x=eval(c,clock_js);cJSON_Delete(x);
    cJSON *j=eval(c,js);
    WFAction a={.kind=kind,.target=ref,.arg0=arg0,.arg1=arg1,
                .text=text,.text_length=text?strlen(text):0,.elapsed_ms=ms};
    assert(!wf_apply(f,0,&a));assert(!wf_batch_checked(f));compare(f,j);return j;
}
static void solve(WFLoaded *f,WebCdp *c,const cJSON *initial,unsigned task){
    char js[128];unsigned ms=100;
    if(task==0){
        const char *wanted=string(initial,"goal");
        const cJSON *options=array(initial,"options");
        unsigned target=(unsigned)cJSON_GetArraySize(options);
        for(unsigned i=0;i<(unsigned)cJSON_GetArraySize(options);i++){
            const cJSON *item=cJSON_GetArrayItem(options,(int)i);
            assert(cJSON_IsString(item));
            if(!strcmp(item->valuestring,wanted))target=i;
        }
        assert(target<(unsigned)cJSON_GetArraySize(options));
        snprintf(js,sizeof js,"__controls.select(%u)",target);
        cJSON *j=act(f,c,js,WF_SELECT_OPTION,1,target,0,NULL,ms);cJSON_Delete(j);
    }else if(task==1||task==2){
        unsigned count=task==1?1:3;
        const cJSON *sliders=array(initial,"sliders");
        const cJSON *combo=task==2?array(initial,"goal"):NULL;
        for(unsigned i=0;i<count;i++){
            const cJSON *slider=cJSON_GetArrayItem(sliders,(int)i);assert(slider);
            int low=(int)number(slider,"min"),high=(int)number(slider,"max");
            int target=task==1?(int)number(initial,"goal"):
                (int)cJSON_GetArrayItem(combo,(int)i)->valuedouble;
            assert(high>low&&target>=low&&target<=high);
            unsigned frac=(unsigned)((target-low)*1000/(high-low));
            if(!strcmp(string(slider,"orientation"),"vertical"))frac=1000-frac;
            snprintf(js,sizeof js,"__controls.slide(%u,%u)",i,frac);
            cJSON *j=act(f,c,js,WF_CLICK,i+1,frac,0,NULL,ms);cJSON_Delete(j);ms+=40;
        }
    }else if(task==3){
        int delta=(int)number(initial,"goal")-atoi(string(initial,"value"));
        for(int i=0;i<abs(delta);i++){
            cJSON *j=act(f,c,delta>0?"__controls.spin(true)":"__controls.spin(false)",
                         WF_CLICK,delta>0?1:2,0,0,NULL,ms++);cJSON_Delete(j);
        }
    }else{
        unsigned desired=(unsigned)number(initial,"goal");char hex[7];snprintf(hex,sizeof hex,"%06X",desired);
        snprintf(js,sizeof js,"__controls.type('%s')",hex);
        WFAction select={.kind=WF_SELECT_ALL,.target=1,.elapsed_ms=ms};
        assert(!wf_apply(f,0,&select));assert(!wf_batch_checked(f));
        cJSON *j=act(f,c,js,WF_INSERT,1,0,0,hex,ms+1);cJSON_Delete(j);ms+=40;
    }
    cJSON *end=act(f,c,"__controls.submit()",WF_CLICK,task==3?3:task==2?4:2,
                   0,0,NULL,ms+40);assert(boolean(end,"done"));cJSON_Delete(end);
}
int main(int argc,char **argv){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    unsigned episodes=argc>1?(unsigned)strtoul(argv[1],NULL,10):8;
    if(!episodes||episodes>100)return 2;
    WFLoaded f;char error[512];
    if(wf_open(&f,"build/webnav/families/controls/libcontrols.so",error,sizeof error)){
        fprintf(stderr,"%s\n",error);return 1;
    }
    char *script=read_file("ocean/webnav/families/controls/browser.js");
    for(unsigned task=0;task<6;task++){
        char file[PATH_MAX],resolved[PATH_MAX],url[PATH_MAX+8];
        snprintf(file,sizeof file,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",f.api->task_names[task]);
        assert(realpath(file,resolved));snprintf(url,sizeof url,"file://%s",resolved);
        WebCdp c={.input=-1,.output=-1,.pid=-1};
        const char *chrome=getenv("WEBNAV_CHROME");
        if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
        assert(!web_cdp_start_ready(&c,chrome,url,"Boolean(window.core&&core.cover_div)"));
        cJSON *installed=eval(&c,script);cJSON_Delete(installed);
        for(unsigned ep=0;ep<episodes;ep++){
            unsigned seed=50000+task*1000+ep;char js[80];
            snprintf(js,sizeof js,"__controls.reset(%u,%u)",task,seed);
            cJSON *initial=eval(&c,js);import_browser(&f,task,initial);compare(&f,initial);
            if(ep%3==0)solve(&f,&c,initial,task);
            else if(ep%3==1){
                cJSON *wrong=act(&f,&c,"__controls.submit()",WF_CLICK,
                    task==3?3:task==2?4:2,0,0,NULL,100);cJSON_Delete(wrong);
            }else{
                unsigned deadline=f.words[WF_DEADLINE];
                snprintf(js,sizeof js,"__controls.advance(%u)",deadline);
                cJSON *timeout=eval(&c,js);
                WFAction wait={.kind=WF_WAIT,.elapsed_ms=deadline};
                assert(!wf_apply(&f,0,&wait));assert(!wf_batch_checked(&f));
                compare(&f,timeout);cJSON_Delete(timeout);
            }
            cJSON_Delete(initial);
        }
        web_cdp_close(&c);
    }
    free(script);wf_close(&f);
    printf("PASS: %u original-page event/value/reward comparisons\n",comparisons);
    return 0;
}
