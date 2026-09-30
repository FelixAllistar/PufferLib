#define _GNU_SOURCE
#include "../common/loader.h"
#include "../../cdp.h"
#include "public_controller.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/prctl.h>

static double number(const cJSON *j,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsNumber(v));return v->valuedouble;
}
static int boolean(const cJSON *j,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsBool(v));return cJSON_IsTrue(v);
}
static const char *string(const cJSON *j,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsString(v));return v->valuestring;
}
static float real(uint32_t bits){float f;memcpy(&f,&bits,4);return f;}
static char *read_file(const char *path){
    FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
    char *s=calloc((size_t)n+1u,1u);assert(s&&fread(s,1,(size_t)n,f)==(size_t)n);
    fclose(f);return s;
}
static int units(uint32_t *out,unsigned cap,const char *s){
    if(!out||!s||strlen(s)>=cap)return -1;
    for(unsigned i=0;s[i];i++){
        unsigned ch=(unsigned char)s[i];if(ch<32u||ch>126u)return -1;out[i]=ch;
    }
    return 0;
}
static int username_equal(const uint32_t *r,unsigned a,unsigned b){
    const uint32_t *x=r+64u+(a-1u)*256u+64u,*y=r+64u+(b-1u)*256u+64u;
    for(unsigned i=0;i<64;i++)if(x[i]!=y[i])return 0;
    return 1;
}
static int import_original(uint32_t *r,unsigned task,const cJSON *j){
    memset(r,0,8192u*sizeof *r);r[WF_VERSION]=2;r[WF_TASK]=task;
    r[WF_DEADLINE]=(unsigned)number(j,"deadline");r[32]=(unsigned)number(j,"count");
    if(r[32]>(task?11u:9u)||r[32]<(task?6u:5u))return -1;
    const char *q=string(j,"query");if(units(r+3000,512,q))return -1;
    char wanted[128];unsigned slot,amount;int single;
    if(social_public_parse(q,wanted,sizeof wanted,&slot,&amount,&single)||single!=(task==0u))return -1;
    r[35]=task?slot:(slot<3u?slot:slot-1u);
    const cJSON *posts=cJSON_GetObjectItemCaseSensitive(j,"posts");
    if(!cJSON_IsArray(posts)||cJSON_GetArraySize(posts)!=(int)r[32])return -1;
    unsigned matches=0;
    for(unsigned i=0;i<r[32];i++){
        const cJSON *p=cJSON_GetArrayItem(posts,(int)i);uint32_t *out=r+64u+i*256u;
        if(units(out,64,string(p,"name"))||
           units(out+64,64,string(p,"username"))||
           units(out+128,96,string(p,"body"))||
           units(out+224,28,string(p,"time")))return -1;
        out[252]=(unsigned)number(p,"active");
        if(!strcmp(string(p,"username"),wanted)){matches++;if(!r[34])r[34]=i+1u;}
    }
    if(!r[34]||!matches)return -1;
    r[36]=task==2u?amount:matches;
    return r[36]>=1u&&r[36]<=matches?0:-1;
}
static int compare(const uint32_t *r,unsigned task,const cJSON *j,
                   unsigned seed,unsigned step,unsigned ref){
    unsigned browser_menu=(unsigned)number(j,"menu");
    int browser_done=boolean(j,"done");
    if(browser_menu!=r[33]||browser_done!=(r[WF_STATUS]!=WF_RUNNING))goto mismatch;
    const cJSON *posts=cJSON_GetObjectItemCaseSensitive(j,"posts");
    if(!cJSON_IsArray(posts)||cJSON_GetArraySize(posts)!=(int)r[32])goto mismatch;
    for(unsigned i=0;i<r[32];i++){
        const cJSON *p=cJSON_GetArrayItem(posts,(int)i);
        if((unsigned)number(p,"active")!=r[64u+i*256u+252u])goto mismatch;
    }
    if(browser_done&&(fabs(number(j,"raw")-real(r[WF_RAW_REWARD]))>1e-6||
       fabs(number(j,"reward")-real(r[WF_TIMED_REWARD]))>1e-5))goto mismatch;
    return 0;
 mismatch:
    fprintf(stderr,"Mismatch task=%s seed=%u step=%u ref=%u menu=%u/%u done=%d/%u raw=%g/%g timed=%g/%g\n",
      task==0u?"social-media":task==1u?"social-media-all":"social-media-some",
      seed,step,ref,browser_menu,r[33],browser_done,r[WF_STATUS],
      number(j,"raw"),real(r[WF_RAW_REWARD]),number(j,"reward"),real(r[WF_TIMED_REWARD]));
    return -1;
}
static unsigned control(unsigned task,unsigned goal){
    return task?goal:(goal<3u?goal:goal+1u);
}
static unsigned wrong_user(const uint32_t *r){
    for(unsigned i=1;i<=r[32];i++)if(!username_equal(r,i,r[34]))return i;
    return 0;
}
static int choose(const WFFamily *api,const uint32_t *r,unsigned task,
                  unsigned scenario,unsigned step,WFAction *a){
    *a=(WFAction){.kind=WF_WAIT};
    unsigned goal=control(task,r[35]),target=r[34];
    if(scenario==4u)return 0;
    if(scenario==0u||(scenario==3u&&task==0u&&step>=3u)){
        WFView v;char scratch[128];
        if(api->observe(r,&v)||social_public_next(&v,scratch,sizeof scratch,a))return -1;
        return 0;
    }
    unsigned ref=0;
    if(task==0u){
        if(scenario==1u&&step==1u)ref=target*16u+(r[35]+1u)%3u;
        if(scenario==2u){
            unsigned other=wrong_user(r);if(!other)return -1;
            if(step==1u)ref=other*16u+(goal>=4u?3u:goal);
            if(step==2u&&goal>=4u)ref=other*16u+goal;
        }
        if(scenario==3u&&(step==1u||step==2u))ref=target*16u+3u;
    }else{
        if(scenario==1u&&step==1u)ref=1u;
        if(scenario==2u){if(step==1u)ref=target*16u+(goal+1u)%4u;
            if(step==2u)ref=1u;}
        if(scenario==3u){if(step==1u){unsigned other=wrong_user(r);
            if(!other)return -1;ref=other*16u+goal;}if(step==2u)ref=1u;}
    }
    if(ref){a->kind=WF_CLICK;a->target=ref;}
    return 0;
}
int main(int argc,char **argv){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    unsigned episodes=argc>1?strtoul(argv[1],0,10):20u;
    if(!episodes||episodes>10000u)return 2;
    WFLoaded f;char error[512];
    if(wf_open(&f,"build/webnav/families/social/libsocial.so",error,sizeof error)){
        fprintf(stderr,"%s\n",error);return 1;
    }
    char *script=read_file("ocean/webnav/families/social/browser.js");
    unsigned total=0,actions=0;
    for(unsigned task=0;task<3;task++){
        char file[PATH_MAX],resolved[PATH_MAX],url[PATH_MAX+8];
        snprintf(file,sizeof file,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",f.api->task_names[task]);
        assert(realpath(file,resolved));snprintf(url,sizeof url,"file://%s",resolved);
        WebCdp c={.input=-1,.output=-1,.pid=-1};
        const char *chrome=getenv("WEBNAV_CHROME");
        if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
        assert(!web_cdp_start_ready(&c,chrome,url,"Boolean(window.core&&core.cover_div)"));
        cJSON *j=web_cdp_eval(&c,script);assert(j);cJSON_Delete(j);
        j=web_cdp_eval(&c,"__social.preload()");
        if(!j||!boolean(j,"ready")){
            fprintf(stderr,"Original social icon preload failed for %s\n",f.api->task_names[task]);
            return 1;
        }
        cJSON_Delete(j);
        unsigned wins=0;
        for(unsigned ep=0;ep<episodes;ep++){
            unsigned seed=100000u+ep,scenario=ep%5u;
            char js[256];snprintf(js,sizeof js,"__social.reset(%u)",seed);
            j=web_cdp_eval(&c,js);assert(j);
            if(import_original(f.words,task,j)){
                fprintf(stderr,"Original social instance exceeds wire capacity or is not ASCII: task=%u seed=%u\n",task,seed);return 1;
            }
            cJSON_Delete(j);assert(!f.api->validate(f.words));
            for(unsigned step=1;step<=32u&&f.words[WF_STATUS]==WF_RUNNING;step++){
                WFAction a;if(choose(f.api,f.words,task,scenario,step,&a))return 1;
                unsigned now=scenario==4u?f.words[WF_DEADLINE]:step*200u;
                snprintf(js,sizeof js,"__social.advance(%u)",now);
                j=web_cdp_eval(&c,js);assert(j);cJSON_Delete(j);
                if(a.kind==WF_CLICK&&now<f.words[WF_DEADLINE]){
                    snprintf(js,sizeof js,"__social.point(%u)",a.target);
                    j=web_cdp_eval(&c,js);
                    if(!j||!boolean(j,"visible")){
                        char *details=j?cJSON_PrintUnformatted(j):NULL;
                        WFView v;const char *query="<projection failed>";
                        if(!f.api->observe(f.words,&v)){
                            const char *found=wf_text_get(&v,v.instruction);
                            if(found)query=found;
                        }
                        fprintf(stderr,"Invisible original control task=%s seed=%u step=%u ref=%u query=%s point=%s\n",
                          f.api->task_names[task],seed,step,a.target,query,
                          details?details:"<null>");
                        free(details);return 1;
                    }
                    double x=number(j,"x"),y=number(j,"y");cJSON_Delete(j);
                    assert(!web_cdp_click(&c,x,y));
                }
                a.elapsed_ms=now;assert(!wf_apply(&f,0,&a));
                for(unsigned lane=1;lane<f.api->batch_lanes;lane++)
                    memcpy(f.words+lane*f.api->row_words,f.words,f.api->row_words*sizeof *f.words);
                assert(!wf_batch_checked(&f));signal(SIGABRT,SIG_DFL);
                j=web_cdp_eval(&c,"__social.snapshot()");assert(j);
                if(compare(f.words,task,j,seed,step,a.target))return 1;
                cJSON_Delete(j);actions++;
            }
            assert(f.words[WF_STATUS]!=WF_RUNNING);
            wins+=real(f.words[WF_RAW_REWARD])>0.99f;total++;
        }
        printf("{\"task\":\"%s\",\"episodes\":%u,\"scripted_successes\":%u,\"matched_original\":\"PASS\"}\n",
          f.api->task_names[task],episodes,wins);fflush(stdout);web_cdp_close(&c);
    }
    free(script);wf_close(&f);
    fprintf(stderr,"PASS: %u original social episodes, %u independent browser/model actions\n",total,actions);
    return 0;
}
