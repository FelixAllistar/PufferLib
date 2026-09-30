#define _GNU_SOURCE
#include "../common/loader.h"
#include "../../cdp.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/prctl.h>

#define ROW 8192u
#define LANES 4u
static double number(const cJSON *j,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);
    assert(cJSON_IsNumber(v));return v->valuedouble;
}
static int boolean(const cJSON *j,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);
    assert(cJSON_IsBool(v));return cJSON_IsTrue(v);
}
static const char *string(const cJSON *j,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);
    assert(cJSON_IsString(v));return v->valuestring;
}
static float real(uint32_t bits){float f;memcpy(&f,&bits,4);return f;}
static char *read_file(const char *path){
    FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);
    long n=ftell(f);rewind(f);char *s=calloc((size_t)n+1,1);
    assert(s&&fread(s,1,(size_t)n,f)==(size_t)n);fclose(f);return s;
}
static unsigned units(uint32_t *out,unsigned cap,const char *s){
    size_t n=strlen(s);assert(n<cap);
    for(size_t i=0;i<n;i++){
        unsigned char ch=(unsigned char)s[i];assert(ch>=32&&ch<=126);out[i]=ch;
    }
    return (unsigned)n;
}
static void import(uint32_t *r,unsigned task,const cJSON *j){
    memset(r,0,ROW*sizeof *r);r[WF_VERSION]=2;r[WF_TASK]=task;
    r[WF_DEADLINE]=(unsigned)number(j,"deadline");
    r[32]=(unsigned)number(j,"position");r[33]=(unsigned)number(j,"height");
    r[34]=(unsigned)number(j,"client");r[47]=(unsigned)number(j,"offset");
    units(r+256,256,string(j,"query"));
    if(task==0){
        const cJSON *opts=cJSON_GetObjectItemCaseSensitive(j,"options");
        const cJSON *selected=cJSON_GetObjectItemCaseSensitive(j,"selected");
        const cJSON *allowed=cJSON_GetObjectItemCaseSensitive(j,"allowed");
        assert(cJSON_IsArray(opts)&&cJSON_IsArray(selected)&&cJSON_IsArray(allowed));
        r[36]=(unsigned)cJSON_GetArraySize(opts);assert(r[36]>=8&&r[36]<=11);
        r[48]=(unsigned)number(j,"optionStride");
        for(unsigned i=0;i<r[36];i++){
            const cJSON *name=cJSON_GetArrayItem(opts,(int)i);
            const cJSON *sel=cJSON_GetArrayItem(selected,(int)i);
            const cJSON *goal=cJSON_GetArrayItem(allowed,(int)i);
            assert(cJSON_IsString(name)&&cJSON_IsBool(sel)&&cJSON_IsBool(goal));
            units(r+512+i*64,64,name->valuestring);
            if(cJSON_IsTrue(sel))r[37]|=1u<<i;
            if(cJSON_IsTrue(goal))r[38]|=1u<<i;
        }
        r[39]=(unsigned)number(j,"required");
    }else{
        units(r+1536,3072,string(j,"content"));
        if(task==2)r[35]=boolean(j,"bottom");
        else{
            r[42]=units(r+4608,64,string(j,"input"));
            r[43]=units(r+4736,64,string(j,"goal"));
            r[44]=(unsigned)number(j,"start");r[45]=(unsigned)number(j,"end");
            if(task==3){r[40]=(unsigned)number(j,"mode");r[41]=boolean(j,"enabled");}
        }
    }
}
static void compare(const WFLoaded *f,const cJSON *j,unsigned task,unsigned seed,
                    unsigned phase){
    const uint32_t *r=f->words;int done=boolean(j,"done");
    int mismatch=(unsigned)number(j,"position")!=r[32]||
        done!=(r[WF_STATUS]!=WF_RUNNING);
    if(task==0){
        const cJSON *selected=cJSON_GetObjectItemCaseSensitive(j,"selected");
        for(unsigned i=0;i<r[36];i++)
            mismatch|=cJSON_IsTrue(cJSON_GetArrayItem(selected,(int)i))!=
                      !!(r[37]&(1u<<i));
    }else if(task==1||task==3){
        const char *value=string(j,"input");
        mismatch|=strlen(value)!=r[42];
        for(unsigned i=0;i<r[42]&&i<63;i++)mismatch|=(unsigned char)value[i]!=r[4608+i];
        mismatch|=(unsigned)number(j,"start")!=r[44]||
                  (unsigned)number(j,"end")!=r[45];
        if(task==3)mismatch|=boolean(j,"enabled")!=(r[41]!=0);
    }
    if(done)mismatch|=fabs(number(j,"raw")-real(r[WF_RAW_REWARD]))>1e-6||
                      fabs(number(j,"reward")-real(r[WF_TIMED_REWARD]))>1e-5;
    if(mismatch){
        fprintf(stderr,"scroll mismatch task=%u seed=%u phase=%u pos=%g/%u done=%d/%u raw=%g/%g reward=%g/%g\n",
          task,seed,phase,number(j,"position"),r[32],done,r[WF_STATUS],
          number(j,"raw"),real(r[WF_RAW_REWARD]),
          number(j,"reward"),real(r[WF_TIMED_REWARD]));
        if(task==0){
            const cJSON *metrics=cJSON_GetObjectItemCaseSensitive(j,"selectMetrics");
            char *detail=metrics?cJSON_PrintUnformatted(metrics):NULL;
            fprintf(stderr,"select metrics: %s\n",detail?detail:"unavailable");
            free(detail);
        }
        abort();
    }
}
static cJSON *eval(WebCdp *c,const char *expr){cJSON *j=web_cdp_eval(c,expr);assert(j);return j;}
static void advance(WebCdp *c,unsigned now){
    char js[128];snprintf(js,sizeof js,"__scroll.tick(%u)",now);
    cJSON *j=eval(c,js);cJSON_Delete(j);
}
static void apply(WebCdp *c,WFLoaded *f,unsigned task,unsigned seed,
                  unsigned phase,unsigned kind,unsigned target,unsigned arg0,
                  const char *text,unsigned now){
    advance(c,now);
    cJSON *quoted=cJSON_CreateString(text?text:"");assert(quoted);
    char *encoded=cJSON_PrintUnformatted(quoted);assert(encoded);
    char js[512];int n=snprintf(js,sizeof js,"__scroll.act(%u,%u,%u,%s)",
                           kind,target,arg0,encoded);
    assert(n>0&&(size_t)n<sizeof js);free(encoded);cJSON_Delete(quoted);
    cJSON *j=eval(c,js);
    WFAction a={.kind=kind,.target=target,.arg0=arg0,.elapsed_ms=now,
                .text=text,.text_length=text?strlen(text):0};
    assert(!wf_apply(f,0,&a));
    for(unsigned lane=1;lane<LANES;lane++)
        memcpy(f->words+ROW*lane,f->words,ROW*sizeof(uint32_t));
    assert(!wf_batch_checked(f));signal(SIGABRT,SIG_DFL);
    compare(f,j,task,seed,phase);cJSON_Delete(j);
}
static void timeout(WebCdp *c,WFLoaded *f,unsigned task,unsigned seed){
    unsigned deadline=f->words[WF_DEADLINE];advance(c,deadline);
    WFAction a={.kind=WF_WAIT,.elapsed_ms=deadline};assert(!wf_apply(f,0,&a));
    for(unsigned lane=1;lane<LANES;lane++)
        memcpy(f->words+ROW*lane,f->words,ROW*sizeof(uint32_t));
    assert(!wf_batch_checked(f));signal(SIGABRT,SIG_DFL);
    cJSON *j=eval(c,"__scroll.snapshot()");
    compare(f,j,task,seed,99);cJSON_Delete(j);
}
static void trace(WebCdp *c,WFLoaded *f,unsigned task,unsigned seed,unsigned variant){
    uint32_t *r=f->words;
    if(variant==3){timeout(c,f,task,seed);return;}
    if(task==0){
        if(variant==2){
            /* Native active-selection end persists after selection changes,
             * including an empty selection; cover both anchor directions. */
            unsigned first=(seed/4)%2?r[36]-1:0,second=first?0:r[36]-1;
            apply(c,f,task,seed,0,WF_SCROLL,1,first*r[48],NULL,100);
            apply(c,f,task,seed,1,WF_SELECT_OPTION,first+2,1,NULL,200);
            apply(c,f,task,seed,2,WF_SCROLL,1,second*r[48],NULL,300);
            apply(c,f,task,seed,3,WF_SELECT_OPTION,second+2,1,NULL,400);
            apply(c,f,task,seed,4,WF_SELECT_OPTION,first+2,0,NULL,500);
            apply(c,f,task,seed,5,WF_SCROLL,1,second*r[48],NULL,600);
            apply(c,f,task,seed,6,WF_SELECT_OPTION,second+2,0,NULL,700);
            apply(c,f,task,seed,7,WF_CLICK,16,0,NULL,1500);
            return;
        }
        if(variant!=1){
            unsigned phase=0;
            for(unsigned i=0;i<r[36];i++)if(r[38]&(1u<<i)){
                unsigned now=100+phase*100;
                apply(c,f,task,seed,phase++,WF_SCROLL,1,i*r[48]+3,NULL,now);
                apply(c,f,task,seed,phase++,WF_SELECT_OPTION,i+2,1,NULL,now+50);
            }
        }
        apply(c,f,task,seed,20,WF_CLICK,16,0,NULL,1500);return;
    }
    if(task==1){
        char goal[64];unsigned n=r[43];assert(n<64);
        for(unsigned i=0;i<n;i++)goal[i]=(char)r[4736+i];goal[n]=0;
        const char *value=variant==1?"wrong":goal;
        apply(c,f,task,seed,0,WF_INSERT,2,0,value,100);
        apply(c,f,task,seed,1,WF_CLICK,3,0,NULL,200);return;
    }
    if(task==2){
        unsigned bottom=r[35],where=variant==1?(bottom?0:r[33]):(bottom?r[33]:0);
        apply(c,f,task,seed,0,WF_SCROLL,1,where,NULL,100);
        apply(c,f,task,seed,1,WF_CLICK,2,0,NULL,200);return;
    }
    unsigned mode=r[40];
    if(mode==0){
        apply(c,f,task,seed,0,WF_CLICK,3,0,NULL,100);return;
    }
    if(variant==1){
        apply(c,f,task,seed,0,WF_CLICK,3,0,NULL,100);return;
    }
    apply(c,f,task,seed,0,WF_SCROLL,1,r[33],NULL,100);
    char goal[64];unsigned n=r[43];assert(n<64);
    for(unsigned i=0;i<n;i++)goal[i]=(char)r[4736+i];goal[n]=0;
    apply(c,f,task,seed,1,WF_INSERT,2,0,variant==2?"wrong":goal,200);
    apply(c,f,task,seed,2,WF_CLICK,mode==1?4:3,0,NULL,300);
}
int main(int argc,char **argv){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    unsigned episodes=argc>1?strtoul(argv[1],NULL,10):20;
    if(!episodes||episodes>10000)return 2;
    WFLoaded f;char error[512];
    if(wf_open(&f,"build/webnav/families/scroll/libscroll.so",error,sizeof error)){
        fprintf(stderr,"%s\n",error);return 1;
    }
    char *script=read_file("ocean/webnav/families/scroll/browser.js");
    unsigned total=0;
    for(unsigned task=0;task<4;task++){
        char file[PATH_MAX],resolved[PATH_MAX],url[PATH_MAX+8];
        snprintf(file,sizeof file,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",
                 f.api->task_names[task]);assert(realpath(file,resolved));
        snprintf(url,sizeof url,"file://%s",resolved);
        WebCdp c={.input=-1,.output=-1,.pid=-1};
        const char *chrome=getenv("WEBNAV_CHROME");
        if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
        assert(!web_cdp_start_ready(&c,chrome,url,"Boolean(window.core&&core.cover_div)"));
        cJSON *j=eval(&c,script);cJSON_Delete(j);
        unsigned wins=0;
        for(unsigned ep=0;ep<episodes;ep++){
            unsigned seed=100000+ep;char js[128];
            snprintf(js,sizeof js,"__scroll.reset(%u,%u)",seed,task);
            j=eval(&c,js);import(f.words,task,j);assert(!f.api->validate(f.words));
            compare(&f,j,task,seed,0);cJSON_Delete(j);
            for(unsigned lane=1;lane<LANES;lane++)
                memcpy(f.words+ROW*lane,f.words,ROW*sizeof(uint32_t));
            trace(&c,&f,task,seed,ep%4);
            assert(f.words[WF_STATUS]);wins+=real(f.words[WF_RAW_REWARD])>0.99f;total++;
        }
        printf("{\"task\":\"%s\",\"episodes\":%u,\"wins\":%u,\"matched_instance_state_reward\":\"PASS\"}\n",
            f.api->task_names[task],episodes,wins);fflush(stdout);web_cdp_close(&c);
    }
    free(script);wf_close(&f);
    fprintf(stderr,"PASS: %u original-generated scroll episodes\n",total);
    return 0;
}
