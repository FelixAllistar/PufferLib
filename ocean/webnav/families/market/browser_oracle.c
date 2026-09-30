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

#define ROW 8192u
static const cJSON *item(const cJSON *a,int i){
    const cJSON *v=cJSON_GetArrayItem(a,i);assert(v);return v;
}
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
static float real(uint32_t bits){float f;memcpy(&f,&bits,sizeof f);return f;}
static char *read_file(const char *path){
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));
    long n=ftell(f);assert(n>=0);rewind(f);
    char *s=calloc((size_t)n+1u,1u);
    assert(s&&fread(s,1,(size_t)n,f)==(size_t)n);fclose(f);return s;
}
static void ascii(uint32_t *r,unsigned offset,unsigned cap,const char *s){
    size_t n=strlen(s);assert(n<cap);
    for(size_t i=0;i<n;i++){
        unsigned char ch=(unsigned char)s[i];assert(ch>=32u&&ch<=126u);
        r[offset+(unsigned)i]=ch;
    }
}
static uint32_t cents(double price){
    assert(isfinite(price)&&price>=0.01&&price<=210.0);
    double scaled=price*100.0;
    assert(fabs(scaled-round(scaled))<1e-6);
    return (uint32_t)lround(scaled);
}
static void import_original(uint32_t *r,unsigned seed,const cJSON *j){
    memset(r,0,ROW*sizeof *r);
    r[WF_VERSION]=2;r[WF_TASK]=0;r[WF_OP]=WF_OBSERVE;
    r[WF_SEED]=seed;r[WF_DEADLINE]=(uint32_t)number(j,"deadline");
    r[35]=100;
    const cJSON *prices=cJSON_GetObjectItemCaseSensitive(j,"prices");
    assert(cJSON_IsArray(prices)&&cJSON_GetArraySize(prices)==100);
    for(unsigned i=0;i<100;i++){
        const cJSON *price=item(prices,(int)i);assert(cJSON_IsNumber(price));
        r[64+i]=cents(price->valuedouble);
    }
    r[34]=r[64+75];
    ascii(r,512,256,string(j,"query"));
    ascii(r,1000,16,string(j,"symbol"));
}
static const WFNode *node(const WFView *v,unsigned ref){
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].ref==ref)return &v->nodes[i];
    return NULL;
}
static void compare(WFLoaded *f,const cJSON *j,unsigned seed,unsigned step){
    const uint32_t *r=f->words;WFView v;assert(!wf_observe(f,0,&v));
    assert(v.count==3&&v.elapsed_ms==(uint32_t)number(j,"elapsed"));
    assert(v.deadline_ms==(uint32_t)number(j,"deadline"));
    assert(!strcmp(wf_text_get(&v,v.instruction),string(j,"query")));
    assert(!strcmp(wf_text_get(&v,node(&v,3)->value),string(j,"symbol")));
    assert(!strcmp(wf_text_get(&v,node(&v,2)->value),string(j,"price")));
    assert(r[32]==(uint32_t)number(j,"tick"));
    assert(!!r[WF_STATUS]==boolean(j,"done"));
    if(r[WF_STATUS]){
        double raw=number(j,"raw"),reward=number(j,"reward");
        if(fabs(raw-real(r[WF_RAW_REWARD]))>1e-6||
           fabs(reward-real(r[WF_TIMED_REWARD]))>1e-5){
            fprintf(stderr,"market reward mismatch seed=%u step=%u browser=%g/%g model=%g/%g\n",
                seed,step,raw,reward,real(r[WF_RAW_REWARD]),real(r[WF_TIMED_REWARD]));
            abort();
        }
    }
}
static cJSON *eval(WebCdp *c,const char *source){
    cJSON *j=web_cdp_eval(c,source);assert(j);return j;
}
static void apply(WFLoaded *f,const WFAction *a){
    assert(!wf_apply(f,0,a));
    for(unsigned lane=1;lane<f->api->batch_lanes;lane++)
        memcpy(f->words+(size_t)lane*ROW,f->words,ROW*sizeof *f->words);
    assert(!wf_batch_checked(f));signal(SIGABRT,SIG_DFL);
}
static void wait_until(WebCdp *c,WFLoaded *f,unsigned ms,
                       unsigned seed,unsigned *step){
    char js[64];snprintf(js,sizeof js,"__mk.advance(%u)",ms);
    cJSON *j=eval(c,js);
    WFAction a={.kind=WF_WAIT,.elapsed_ms=ms};apply(f,&a);
    compare(f,j,seed,++*step);cJSON_Delete(j);
}
static void buy_at(WebCdp *c,WFLoaded *f,unsigned ms,
                   unsigned seed,unsigned *step){
    char js[64];snprintf(js,sizeof js,"__mk.advance(%u)",ms);
    cJSON *j=eval(c,js);cJSON_Delete(j);
    j=eval(c,"__mk.point()");assert(boolean(j,"visible"));
    double x=number(j,"x"),y=number(j,"y");cJSON_Delete(j);
    assert(!web_cdp_click(c,x,y));
    j=eval(c,"__mk.snapshot(false)");
    WFAction a={.kind=WF_CLICK,.target=1u,.elapsed_ms=ms};apply(f,&a);
    compare(f,j,seed,++*step);cJSON_Delete(j);
}
int main(int argc,char **argv){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    unsigned episodes=argc>1?(unsigned)strtoul(argv[1],NULL,10):10u;
    if(!episodes||episodes>200u)return 2;
    WFLoaded f;char error[512];
    if(wf_open(&f,"build/webnav/families/market/libmarket.so",error,sizeof error)){
        fprintf(stderr,"%s\n",error);return 1;
    }
    char *script=read_file("ocean/webnav/families/market/browser.js");
    char file[PATH_MAX],resolved[PATH_MAX],url[PATH_MAX+8];
    snprintf(file,sizeof file,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/stock-market.html");
    assert(realpath(file,resolved));snprintf(url,sizeof url,"file://%s",resolved);
    WebCdp c={.input=-1,.output=-1,.pid=-1};
    const char *chrome=getenv("WEBNAV_CHROME");
    if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
    assert(!web_cdp_start_ready(&c,chrome,url,"Boolean(window.core&&core.cover_div)"));
    cJSON *j=eval(&c,script);assert(cJSON_IsTrue(j));cJSON_Delete(j);
    unsigned actions=0,wins=0;
    for(unsigned ep=0;ep<episodes;ep++){
        unsigned seed=980000u+ep,step=0;
        char js[80];snprintf(js,sizeof js,"__mk.reset(%u)",seed);
        j=eval(&c,js);import_original(f.words,seed,j);
        assert(!f.api->validate(f.words));compare(&f,j,seed,step);cJSON_Delete(j);
        switch(ep%5u){
            case 0u: buy_at(&c,&f,50,seed,&step);actions++;break;
            case 1u: wait_until(&c,&f,100,seed,&step);
                     buy_at(&c,&f,100,seed,&step);actions+=2;break;
            case 2u: wait_until(&c,&f,100,seed,&step);
                     wait_until(&c,&f,500,seed,&step);
                     wait_until(&c,&f,7600,seed,&step);
                     buy_at(&c,&f,7600,seed,&step);actions+=4;break;
            case 3u:{
                unsigned index=75u;
                for(unsigned i=0;i<99u;i++)if(f.words[64+i]>f.words[34]){
                    index=i;break;
                }
                unsigned ms=(index+1u)*100u;
                wait_until(&c,&f,ms,seed,&step);
                buy_at(&c,&f,ms,seed,&step);actions+=2;break;
            }
            default: wait_until(&c,&f,9999,seed,&step);
                     wait_until(&c,&f,10000,seed,&step);actions+=2;break;
        }
        assert(f.words[WF_STATUS]!=WF_RUNNING);
        wins+=f.words[WF_RAW_REWARD]==1065353216u;
    }
    printf("{\"task\":\"stock-market\",\"episodes\":%u,\"actions\":%u,\"scripted_successes\":%u,\"differential\":\"PASS\",\"preset\":\"pinned original HTML; reset-owned price stream; clean-page display; controlled 100 ms callbacks\"}\n",
        episodes,actions,wins);
    fflush(stdout);web_cdp_close(&c);free(script);wf_close(&f);
    return 0;
}
