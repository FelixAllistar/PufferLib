#define _GNU_SOURCE
#include "../common/loader.h"
#include "../../cdp.h"
#include "public_controller.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

static double num(const cJSON *j,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);
    assert(cJSON_IsNumber(v));return v->valuedouble;
}
static const char *str(const cJSON *j,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);
    assert(cJSON_IsString(v));return v->valuestring;
}
static int flag(const cJSON *j,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);
    assert(cJSON_IsBool(v));return cJSON_IsTrue(v);
}
static float real(uint32_t bits){float f;memcpy(&f,&bits,4);return f;}
static char *read_file(const char *path){
    FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);
    long n=ftell(f);rewind(f);char *s=calloc((size_t)n+1u,1u);
    assert(s&&fread(s,1,(size_t)n,f)==(size_t)n);fclose(f);return s;
}
static int units(uint32_t *out,unsigned cap,const char *s){
    size_t n=strlen(s);if(n>=cap)return -1;
    for(size_t i=0;i<n;i++){
        unsigned ch=(unsigned char)s[i];if(ch<32u||ch>126u)return -1;
        out[i]=ch;
    }
    return 0;
}
static int same_units(const uint32_t *p,const char *s,unsigned cap){
    size_t n=strlen(s);if(n>=cap)return 0;
    for(size_t i=0;i<n;i++)if(p[i]!=(unsigned char)s[i])return 0;
    return p[n]==0u;
}
static int parse_food(uint32_t *r,const cJSON *j){
    const char *q=str(j,"query");
    const cJSON *items=cJSON_GetObjectItemCaseSensitive(j,"items");
    if(!cJSON_IsArray(items)||cJSON_GetArraySize(items)!=12)return -1;
    memset(r,0,8192u*sizeof *r);
    r[WF_VERSION]=2;r[WF_TASK]=1;r[WF_DEADLINE]=20000u;
    if(units(r+3000,512u,q))return -1;
    const char *types[]={"dairy","gluten-free","meat","peanuts","vegan"};
    const char *courses[]={"appetizer","entree","dessert"};
    for(unsigned i=0;i<12u;i++){
        const cJSON *item=cJSON_GetArrayItem(items,(int)i),
                    *tags=cJSON_GetObjectItemCaseSensitive(item,"types");
        uint32_t *p=r+64u+i*128u;
        if((unsigned)num(item,"id")!=i||
           units(p+8,120u,str(item,"name"))||!cJSON_IsArray(tags))return -1;
        for(int t=0;t<cJSON_GetArraySize(tags);t++){
            const cJSON *tag=cJSON_GetArrayItem(tags,t);if(!cJSON_IsString(tag))return -1;
            unsigned bit=0;for(;bit<5u;bit++)if(!strcmp(tag->valuestring,types[bit]))break;
            if(bit==5u)return -1;p[1]|=1u<<bit;
        }
        for(unsigned c=0;c<3u;c++)if(!strcmp(str(item,"course"),courses[c]))p[2]=c;
    }
    if(!strncmp(q,"Order one of each item: ",24u)){
        r[32]=0u;r[33]=r[34]=12u;
        for(unsigned i=0;i<12u;i++){
            const char *name=str(cJSON_GetArrayItem(items,(int)i),"name");
            if(strstr(q,name)){
                if(r[33]==12u)r[33]=i;else if(r[34]==12u)r[34]=i;
                else return -1;
            }
        }
        if(r[33]==12u||r[34]==12u)return -1;
        r[35]=0u;r[36]=2u;
    }else{
        unsigned required=0u;char type[64];
        if(sscanf(q,"Order %u items that are %63s",&required,type)!=2||
           required<2u||required>4u)return -1;
        unsigned code=0;for(;code<5u;code++)if(!strcmp(type,types[code]))break;
        if(code==5u)return -1;
        r[32]=1u;r[33]=0u;r[34]=1u;r[35]=code;r[36]=required;
    }
    return 0;
}
static int parse_phone(uint32_t *r,const cJSON *j){
    const cJSON *p=cJSON_GetObjectItemCaseSensitive(j,"problem"),
                *contacts=cJSON_GetObjectItemCaseSensitive(p,"contacts");
    if(!cJSON_IsArray(contacts)||cJSON_GetArraySize(contacts)!=5)return -1;
    memset(r,0,8192u*sizeof *r);
    r[WF_VERSION]=2;r[WF_TASK]=0;r[WF_DEADLINE]=15000u;
    r[32]=(unsigned)num(j,"page");r[33]=1u+(unsigned)num(p,"index");r[35]=5u;
    const char *property=str(p,"property");
    r[34]=!strcmp(property,"phone")?0u:!strcmp(property,"email")?1u:2u;
    if(units(r+3000,512u,str(j,"query")))return -1;
    for(unsigned i=0;i<5u;i++){
        const cJSON *c=cJSON_GetArrayItem(contacts,(int)i);
        uint32_t *out=r+64u+i*512u;
        if(units(out,64u,str(c,"person"))||
           units(out+64,64u,str(c,"phone"))||
           units(out+128,128u,str(c,"email"))||
           units(out+256,128u,str(c,"address")))return -1;
    }
    return r[32]>=1u&&r[32]<=5u?0:-1;
}
static int parse_search(uint32_t *r,const cJSON *j){
    const cJSON *p=cJSON_GetObjectItemCaseSensitive(j,"problem"),
                *results=cJSON_GetObjectItemCaseSensitive(p,"results");
    if(!cJSON_IsArray(results)||cJSON_GetArraySize(results)!=9)return -1;
    r[32]=0u;r[33]=(unsigned)num(p,"expectedIndex");
    r[34]=(unsigned)strlen(str(p,"expectedSearch"));
    memset(r+2700,0,64u*sizeof *r);
    memset(r+3000,0,512u*sizeof *r);
    if(units(r+2700,64u,str(p,"expectedSearch"))||
       units(r+3000,512u,str(j,"query")))return -1;
    for(unsigned i=0;i<9u;i++){
        const cJSON *item=cJSON_GetArrayItem(results,(int)i);
        uint32_t *out=r+64u+i*256u;
        memset(out,0,256u*sizeof *r);
        if(units(out,64u,str(item,"title"))||
           units(out+64,96u,str(item,"url"))||
           units(out+160,96u,str(item,"desc")))return -1;
    }
    return 0;
}
static int compare(const uint32_t *r,unsigned task,const cJSON *j,
                   unsigned seed,unsigned step,unsigned ref){
    int bad=flag(j,"done")!=(r[WF_STATUS]!=WF_RUNNING);
    bad|=strcmp(str(j,"query"),"")==0;
    if(task==0u){
        const uint32_t *p=r+64u+(r[32]-1u)*512u;
        bad|=(unsigned)num(j,"page")!=r[32];
        bad|=!same_units(p,str(j,"name"),64u)||
              !same_units(p+64,str(j,"phone"),64u)||
              !same_units(p+128,str(j,"email"),128u)||
              !same_units(p+256,str(j,"address"),128u);
    }else if(task==1u){
        const cJSON *items=cJSON_GetObjectItemCaseSensitive(j,"items");
        bad|=!cJSON_IsArray(items)||cJSON_GetArraySize(items)!=12;
        for(unsigned i=0;i<12u&&!bad;i++){
            const cJSON *p=cJSON_GetArrayItem(items,(int)i);
            bad|=(unsigned)num(p,"qty")!=r[64u+i*128u];
        }
    }else{
        const cJSON *results=cJSON_GetObjectItemCaseSensitive(j,"results");
        bad|=(unsigned)num(j,"page")!=r[32];
        bad|=!same_units(r+2500,str(j,"input"),128u);
        bad|=(unsigned)num(j,"start")!=r[36]||
              (unsigned)num(j,"end")!=r[37];
        bad|=!cJSON_IsArray(results)||
              cJSON_GetArraySize(results)!=(r[32]?3:0);
        if(r[32]&&!bad)for(unsigned i=0;i<3u;i++){
            const cJSON *p=cJSON_GetArrayItem(results,(int)i);
            int expected=r[39]?(int)((r[32]-1u)*3u+i):-1;
            bad|=(int)num(p,"index")!=expected;
            if(r[39])bad|=!same_units(r+64u+expected*256u,
                                     str(p,"title"),64u);
        }
    }
    if(r[WF_STATUS]!=WF_RUNNING){
        bad|=fabs(num(j,"raw")-real(r[WF_RAW_REWARD]))>1e-6;
        bad|=fabs(num(j,"reward")-real(r[WF_TIMED_REWARD]))>1e-5;
    }
    if(bad){
        char *details=cJSON_PrintUnformatted(j);
        fprintf(stderr,"Catalog browser mismatch task=%u seed=%u step=%u ref=%u status=%u page/mode=%u raw=%g/%g browser=%s\n",
            task,seed,step,ref,r[WF_STATUS],r[32],
            real(r[WF_RAW_REWARD]),num(j,"raw"),details?details:"<null>");
        free(details);return -1;
    }
    return 0;
}
static void key(WebCdp *c,const char *name,unsigned code,unsigned modifiers){
    for(unsigned i=0;i<2u;i++){
        cJSON *p=cJSON_CreateObject();
        cJSON_AddStringToObject(p,"type",i?"keyUp":"keyDown");
        cJSON_AddStringToObject(p,"key",name);
        cJSON_AddNumberToObject(p,"windowsVirtualKeyCode",code);
        cJSON_AddNumberToObject(p,"nativeVirtualKeyCode",code);
        cJSON_AddNumberToObject(p,"modifiers",modifiers);
        cJSON *r=web_cdp_call(c,"Input.dispatchKeyEvent",p);assert(r);cJSON_Delete(r);
    }
}
static int dispatch(WebCdp *c,const WFAction *a,unsigned task,
                    unsigned seed,unsigned step){
    cJSON *j;char js[80];
    if(a->kind==WF_CLICK){
        snprintf(js,sizeof js,"__catalog.point(%u)",a->target);
        j=web_cdp_eval(c,js);assert(j);
        if(!flag(j,"visible")){
            char *details=cJSON_PrintUnformatted(j);
            fprintf(stderr,"Invisible catalog control task=%u seed=%u step=%u ref=%u point=%s\n",
                task,seed,step,a->target,details?details:"<null>");
            free(details);cJSON_Delete(j);return -1;
        }
        double x=num(j,"x"),y=num(j,"y");cJSON_Delete(j);
        if(web_cdp_click(c,x,y))return -1;
    }else if(a->kind==WF_INSERT){
        cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"text",a->text);
        j=web_cdp_call(c,"Input.insertText",p);assert(j);cJSON_Delete(j);
    }else if(a->kind==WF_SELECT_ALL)key(c,"a",65u,2u);
    else if(a->kind==WF_BACKSPACE)key(c,"Backspace",8u,0u);
    else if(a->kind!=WF_WAIT)return -1;
    return 0;
}
static int choose(const WFFamily *api,const uint32_t *r,unsigned task,
                  unsigned scenario,unsigned step,int *injected,
                  WFAction *a,char *scratch){
    *a=(WFAction){.kind=WF_WAIT};
    if(scenario==4u)return 0;
    WFView v;if(api->observe(r,&v))return -1;
    if(task==0u){
        if(scenario==0u)return catalog_public_next(&v,scratch,128u,a);
        unsigned desired=scenario>=2u?(r[33]==5u?4u:r[33]+1u):r[33];
        if(r[32]!=desired){
            a->kind=WF_CLICK;a->target=r[32]<desired?r[32]+1u:r[32]-1u;
            return 0;
        }
        a->kind=WF_CLICK;
        a->target=100u+((scenario&1u)?(r[34]+1u)%3u:r[34]);return 0;
    }
    if(task==1u){
        if(scenario==1u){a->kind=WF_CLICK;a->target=1u;return 0;}
        if(scenario==2u){a->kind=WF_CLICK;
            a->target=step==1u?16u*(r[33]+1u)+1u:1u;return 0;}
        if(scenario==3u&&*injected){a->kind=WF_CLICK;a->target=1u;return 0;}
        if(catalog_public_next(&v,scratch,128u,a))return -1;
        if(scenario==3u&&a->kind==WF_CLICK&&a->target==1u&&!*injected){
            *injected=1;a->target=16u*(r[33]+1u)+1u;
        }
        return 0;
    }
    if(scenario==1u){
        if(step==1u){a->kind=WF_CLICK;a->target=1u;return 0;}
        if(step==2u){a->kind=WF_INSERT;a->text="zzzz";a->text_length=4u;return 0;}
        a->kind=WF_CLICK;a->target=step==3u?2u:200u;return 0;
    }
    if(scenario==5u){
        if(step==1u||step==4u){a->kind=WF_CLICK;a->target=1u;return 0;}
        if(step==2u){a->kind=WF_INSERT;a->text="bad";a->text_length=3u;return 0;}
        if(step==3u){a->kind=WF_CLICK;a->target=2u;return 0;}
        if(step==5u){a->kind=WF_SELECT_ALL;return 0;}
        if(step==6u)return catalog_public_next(&v,scratch,128u,a);
        if(step==7u){a->kind=WF_CLICK;a->target=11u;return 0;}
        unsigned page=r[33]/3u+1u;
        a->kind=WF_CLICK;a->target=r[32]==page?100u+r[33]:9u+page;
        return 0;
    }
    if(catalog_public_next(&v,scratch,128u,a))return -1;
    if(scenario==2u&&a->kind==WF_CLICK&&a->target>=100u&&a->target<=108u)
        a->target=100u+(r[33]/3u)*3u+(r[33]+1u)%3u;
    if(scenario==3u&&a->kind==WF_CLICK&&a->target>=100u&&a->target<=108u&&!*injected){
        *injected=1;a->target=2u;
    }
    return 0;
}
int main(int argc,char **argv){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    unsigned episodes=argc>1?strtoul(argv[1],NULL,10):20u;
    if(!episodes||episodes>10000u)return 2;
    WFLoaded f;char error[512];
    if(wf_open(&f,"build/webnav/families/catalog/libcatalog.so",error,sizeof error)){
        fprintf(stderr,"%s\n",error);return 1;
    }
    char *script=read_file("ocean/webnav/families/catalog/browser.js");
    unsigned total=0u,actions=0u;
    for(unsigned task=0;task<3u;task++){
        char file[PATH_MAX],resolved[PATH_MAX],url[PATH_MAX+8];
        snprintf(file,sizeof file,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",f.api->task_names[task]);
        assert(realpath(file,resolved));snprintf(url,sizeof url,"file://%s",resolved);
        WebCdp c={.input=-1,.output=-1,.pid=-1};
        const char *chrome=getenv("WEBNAV_CHROME");
        if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
        assert(!web_cdp_start_ready(&c,chrome,url,"Boolean(window.core&&core.cover_div)"));
        cJSON *j=web_cdp_eval(&c,script);assert(j);cJSON_Delete(j);
        unsigned wins=0u;
        for(unsigned ep=0;ep<episodes;ep++){
            unsigned seed=100000u+ep,scenario=ep%6u;int injected=0;
            char js[96];snprintf(js,sizeof js,"__catalog.reset(%u)",seed);
            j=web_cdp_eval(&c,js);assert(j);
            int imported;
            if(task==2u){
                memset(f.words,0,f.api->row_words*f.api->batch_lanes*sizeof *f.words);
                for(unsigned lane=0;lane<f.api->batch_lanes;lane++){
                    uint32_t *r=f.words+lane*f.api->row_words;
                    r[WF_VERSION]=2;r[WF_TASK]=2;r[WF_OP]=WF_RESET;r[WF_SEED]=seed;
                }
                assert(!wf_batch_checked(&f));signal(SIGABRT,SIG_DFL);
                imported=parse_search(f.words,j);
            }else imported=task==0u?parse_phone(f.words,j):parse_food(f.words,j);
            if(imported){
                fprintf(stderr,"Original catalog instance exceeds wire capacity: task=%u seed=%u\n",task,seed);
                return 1;
            }
            cJSON_Delete(j);assert(!f.api->validate(f.words));
            for(unsigned step=1u;step<=64u&&f.words[WF_STATUS]==WF_RUNNING;step++){
                WFAction a;char scratch[128];
                if(choose(f.api,f.words,task,scenario,step,&injected,&a,scratch))return 1;
                unsigned now=scenario==4u?f.words[WF_DEADLINE]:step*100u;
                snprintf(js,sizeof js,"__catalog.advance(%u)",now);
                j=web_cdp_eval(&c,js);assert(j);cJSON_Delete(j);
                if(now<f.words[WF_DEADLINE]&&dispatch(&c,&a,task,seed,step))return 1;
                a.elapsed_ms=now;assert(!wf_apply(&f,0,&a));
                for(unsigned lane=1u;lane<f.api->batch_lanes;lane++)
                    memcpy(f.words+lane*f.api->row_words,f.words,
                           f.api->row_words*sizeof *f.words);
                assert(!wf_batch_checked(&f));signal(SIGABRT,SIG_DFL);
                j=web_cdp_eval(&c,"__catalog.snapshot()");assert(j);
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
    fprintf(stderr,"PASS: %u original catalog episodes, %u independent browser/model actions\n",total,actions);
    return 0;
}
