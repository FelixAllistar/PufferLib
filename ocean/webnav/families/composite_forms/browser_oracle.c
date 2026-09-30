#define _GNU_SOURCE
#include "../common/loader.h"
#include "../../cdp.h"
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

#define ROW 8192u
static double number(const cJSON *j,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsNumber(v));
    return v->valuedouble;
}
static int boolean(const cJSON *j,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsBool(v));
    return cJSON_IsTrue(v);
}
static const char *string(const cJSON *j,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsString(v));
    return v->valuestring;
}
static float real(uint32_t bits){float f;memcpy(&f,&bits,sizeof f);return f;}
static char *read_file(const char *path){
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));
    long n=ftell(f);assert(n>=0);rewind(f);char *s=calloc((size_t)n+1,1);
    assert(s&&fread(s,1,(size_t)n,f)==(size_t)n);fclose(f);return s;
}
static void put(uint32_t *dst,const char *s,unsigned cap){
    size_t n=strlen(s);assert(n<=cap);
    for(size_t i=0;i<n;i++){assert((unsigned char)s[i]>=32&&
                                 (unsigned char)s[i]<=126);dst[i]=(unsigned char)s[i];}
}
static const cJSON *item(const cJSON *a,int i){
    const cJSON *x=cJSON_GetArrayItem(a,i);assert(x);return x;
}
static void import_original(uint32_t *r,unsigned task,unsigned seed,const cJSON *j){
    memset(r,0,ROW*sizeof *r);r[WF_VERSION]=2;r[WF_TASK]=task;
    r[WF_SEED]=seed;r[WF_DEADLINE]=(uint32_t)number(j,"deadline");
    r[32]=(uint32_t)number(j,"selected");r[33]=(uint32_t)number(j,"checked");
    r[34]=(uint32_t)number(j,"requested");r[35]=(uint32_t)number(j,"secondary");
    r[36]=(uint32_t)number(j,task==0?"width":"layout");
    r[37]=(uint32_t)number(j,"order");
    put(r+128,string(j,"query"),254);
    const cJSON *values=cJSON_GetObjectItemCaseSensitive(j,"values");
    const cJSON *goals=cJSON_GetObjectItemCaseSensitive(j,"goals");
    assert(cJSON_IsArray(values)&&cJSON_IsArray(goals));
    for(int i=0;i<cJSON_GetArraySize(values);i++){
        const char *s=item(values,i)->valuestring;
        r[40+i]=(uint32_t)strlen(s);put(r+512+i*128,s,63);
    }
    for(int i=0;i<cJSON_GetArraySize(goals);i++){
        const char *s=item(goals,i)->valuestring;
        r[44+i]=(uint32_t)strlen(s);put(r+1024+i*128,s,63);
    }
}
static void compare(const uint32_t *r,const cJSON *j,unsigned task,
                    unsigned seed,unsigned step){
    const cJSON *values=cJSON_GetObjectItemCaseSensitive(j,"values");
    int bad=(unsigned)number(j,"selected")!=r[32]||
        (task==0&&(unsigned)number(j,"checked")!=r[33])||
        boolean(j,"done")!=(r[WF_STATUS]!=WF_RUNNING);
    for(int i=0;i<cJSON_GetArraySize(values);i++){
        const char *s=item(values,i)->valuestring;
        if(strlen(s)!=r[40+i])bad=1;
        for(unsigned k=0;k<r[40+i];k++)if((unsigned char)s[k]!=r[512+i*128+k])bad=1;
    }
    if(r[WF_STATUS]!=WF_RUNNING){
        if(fabs(number(j,"raw")-real(r[WF_RAW_REWARD]))>1e-6||
           fabs(number(j,"reward")-real(r[WF_TIMED_REWARD]))>1e-5)bad=1;
    }
    if(bad){fprintf(stderr,"Mismatch task=%u seed=%u step=%u selected=%g/%u done=%d/%u raw=%g/%g reward=%g/%g\n",
        task,seed,step,number(j,"selected"),r[32],boolean(j,"done"),r[WF_STATUS],
        number(j,"raw"),real(r[WF_RAW_REWARD]),number(j,"reward"),
        real(r[WF_TIMED_REWARD]));abort();}
}
static void step(WFLoaded *f,WebCdp *c,unsigned task,unsigned seed,
                 unsigned action,unsigned target,unsigned arg0,
                 const char *text,unsigned now,unsigned index){
    cJSON *str=cJSON_CreateString(text?text:"");assert(str);
    char *encoded=cJSON_PrintUnformatted(str);assert(encoded);cJSON_Delete(str);
    size_t cap=strlen(encoded)+128;char *js=malloc(cap);assert(js);
    snprintf(js,cap,"__cf.apply(%u,%u,%u,%s,%u)",action,target,arg0,encoded,now);
    cJSON *browser=web_cdp_eval(c,js);assert(browser);
    free(js);free(encoded);
    WFAction a={.kind=action,.target=target,.arg0=arg0,.elapsed_ms=now,
                .text=text,.text_length=text?strlen(text):0};
    assert(!wf_apply(f,0,&a));
    for(unsigned lane=1;lane<4;lane++)
        memcpy(f->words+lane*ROW,f->words,ROW*sizeof *f->words);
    assert(!wf_batch_checked(f));signal(SIGABRT,SIG_DFL);
    compare(f->words,browser,task,seed,index);cJSON_Delete(browser);
}
static void goal(char *out,const uint32_t *r,unsigned index){
    assert(r[44+index]<=63);
    for(unsigned i=0;i<r[44+index];i++)out[i]=(char)r[1024+index*128+i];
    out[r[44+index]]=0;
}
int main(int argc,char **argv){
    assert(!prctl(PR_SET_DUMPABLE,0,0,0,0));
    unsigned episodes=argc>1?strtoul(argv[1],0,10):12;
    if(!episodes||episodes>1000)return 2;
    WFLoaded f;char error[512];
    if(wf_open(&f,"build/webnav/families/composite_forms/libcomposite_forms.so",
               error,sizeof error)){fprintf(stderr,"%s\n",error);return 1;}
    char *script=read_file("ocean/webnav/families/composite_forms/browser.js");
    for(unsigned task=0;task<5;task++){
        char path[PATH_MAX],resolved[PATH_MAX],url[PATH_MAX+8];
        snprintf(path,sizeof path,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",f.api->task_names[task]);
        assert(realpath(path,resolved));snprintf(url,sizeof url,"file://%s",resolved);
        WebCdp c={.input=-1,.output=-1,.pid=-1};
        const char *chrome=getenv("WEBNAV_CHROME");
        if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
        assert(!web_cdp_start_ready(&c,chrome,url,"Boolean(window.core&&core.cover_div)"));
        cJSON *j=web_cdp_eval(&c,script);assert(j&&cJSON_IsTrue(j));cJSON_Delete(j);
        for(unsigned ep=0;ep<episodes;ep++){
            unsigned seed=100000+ep,ix=0;char js[80];
            snprintf(js,sizeof js,"__cf.reset(%u)",seed);
            j=web_cdp_eval(&c,js);assert(j);
            import_original(f.words,task,seed,j);compare(f.words,j,task,seed,ix++);
            cJSON_Delete(j);assert(!f.api->validate(f.words));
            for(unsigned lane=1;lane<4;lane++)memcpy(f.words+lane*ROW,f.words,ROW*sizeof *f.words);
            if(ep%5==4){step(&f,&c,task,seed,WF_WAIT,0,0,NULL,f.words[12],ix++);continue;}
            if(task==0){
                unsigned wanted=ep%5==1?(f.words[34]+1)%21:f.words[34];
                step(&f,&c,task,seed,WF_SELECT_OPTION,1,wanted,NULL,250,ix++);
                step(&f,&c,task,seed,WF_CLICK,f.words[35]+1,0,NULL,500,ix++);
                if(ep%5==2)step(&f,&c,task,seed,WF_CLICK,2+(f.words[35]%3),0,NULL,625,ix++);
                step(&f,&c,task,seed,WF_CLICK,5,0,NULL,750,ix++);
            }else if(task==1){
                unsigned radio=ep%5==1?(f.words[34]%3)+1:f.words[34];
                step(&f,&c,task,seed,WF_CLICK,radio,0,NULL,250,ix++);
                char value[64];goal(value,f.words,f.words[35]-1);
                step(&f,&c,task,seed,WF_INSERT,f.words[35]+3,0,value,500,ix++);
                step(&f,&c,task,seed,WF_CLICK,7,0,NULL,750,ix++);
            }else if(task==2){
                unsigned wanted=ep%5==1?(f.words[34]%6)+1:f.words[34];
                step(&f,&c,task,seed,WF_SELECT_OPTION,1,wanted,NULL,250,ix++);
                step(&f,&c,task,seed,WF_CLICK,f.words[35]+1,0,NULL,500,ix++);
            }else{
                for(unsigned field=0;field<3;field++){
                    char value[64];goal(value,f.words,field);
                    if(field==2&&ep%5==1)value[0]=value[0]=='1'?'2':'1';
                    step(&f,&c,task,seed,WF_INSERT,field+1,0,value,
                         250*(field+1),ix++);
                }
                step(&f,&c,task,seed,WF_CLICK,4,0,NULL,1000,ix++);
            }
            assert(f.words[WF_STATUS]==WF_TERMINAL);
        }
        printf("{\"task\":\"%s\",\"episodes\":%u,\"matched_instance_differential\":\"PASS\"}\n",
               f.api->task_names[task],episodes);fflush(stdout);
        web_cdp_close(&c);
    }
    free(script);wf_close(&f);
}
