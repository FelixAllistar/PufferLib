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

#define ROW 1024u
static const char *const labels[]={"ÖK","Cancél","♥♥♥","确定","取消","ヘルプ"};

static const cJSON *field(const cJSON *j,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(v);return v;
}
static const char *str(const cJSON *j,const char *key){
    const cJSON *v=field(j,key);assert(cJSON_IsString(v));return v->valuestring;
}
static double num(const cJSON *j,const char *key){
    const cJSON *v=field(j,key);assert(cJSON_IsNumber(v));return v->valuedouble;
}
static int boolean(const cJSON *j,const char *key){
    const cJSON *v=field(j,key);assert(cJSON_IsBool(v));return cJSON_IsTrue(v);
}
static float real(uint32_t bits){float f;memcpy(&f,&bits,4);return f;}
static char *read_file(const char *path){
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));
    long n=ftell(f);assert(n>=0);rewind(f);
    char *s=calloc((size_t)n+1,1);assert(s&&fread(s,1,(size_t)n,f)==(size_t)n);
    fclose(f);return s;
}
static void copy_units(uint32_t *dst,unsigned cap,const cJSON *array){
    assert(cJSON_IsArray(array));int n=cJSON_GetArraySize(array);
    if(n<0||(unsigned)n>=cap){
        fprintf(stderr,"original text has %d scalars; row capacity is %u including NUL\n",n,cap);
        abort();
    }
    for(int i=0;i<n;i++){
        const cJSON *v=cJSON_GetArrayItem(array,i);
        assert(cJSON_IsNumber(v)&&v->valuedouble>=32&&v->valuedouble<=0x10ffff);
        dst[i]=(uint32_t)v->valuedouble;
    }
}
static unsigned label_id(const char *text){
    for(unsigned i=0;i<6;i++)if(!strcmp(text,labels[i]))return i;
    fprintf(stderr,"unknown Unicode button label: %s\n",text);abort();
}
static unsigned goal_id(const char *query){
    const char *begin=strstr(query,"Click on the \"");assert(begin);begin+=14;
    const char *end=strchr(begin,'"');assert(end&&(size_t)(end-begin)<64);
    char goal[64];memcpy(goal,begin,(size_t)(end-begin));goal[end-begin]=0;
    return label_id(goal);
}
static void import_original(uint32_t *r,unsigned task,unsigned seed,const cJSON *j){
    memset(r,0,ROW*sizeof *r);
    r[WF_VERSION]=2;r[WF_TASK]=task;r[WF_OP]=WF_OBSERVE;r[WF_SEED]=seed;
    r[WF_DEADLINE]=(unsigned)num(j,"deadline");
    copy_units(r+256,128,field(j,"queryUnits"));
    const char *query=str(j,"query");
    if(task==0){
        unsigned month=0,day=0,year=0;
        assert(sscanf(query,"Enter %u/%u/%u as the date",&month,&day,&year)==3);
        r[32]=year;r[33]=month;r[34]=day;
    }else if(task==1){
        unsigned hour=0,minute=0;char meridian[3]={0};
        assert(sscanf(query,"Enter %u:%u %2s as the time",&hour,&minute,meridian)==3);
        assert(hour>=1&&hour<=12&&minute<=59);
        r[32]=hour%12+(!strcmp(meridian,"PM")?12:0);
        assert(!strcmp(meridian,"AM")||!strcmp(meridian,"PM"));
        r[33]=minute;
    }else{
        r[32]=goal_id(query);
        const cJSON *slots=field(j,"slots");
        assert(cJSON_IsArray(slots)&&cJSON_GetArraySize(slots)==6);
        for(unsigned i=0;i<6;i++){
            const cJSON *slot=cJSON_GetArrayItem(slots,(int)i);assert(slot);
            r[40+i]=(unsigned)num(slot,"kind");
            if(r[40+i]==2)r[48+i]=label_id(str(slot,"text"));
            copy_units(r+512+i*64,64,field(slot,"units"));
        }
    }
}
static const WFNode *find(const WFView *v,unsigned ref){
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].ref==ref)return v->nodes+i;
    return NULL;
}
static void compare(WFLoaded *f,const cJSON *j,unsigned task,unsigned seed,unsigned phase){
    WFView v;assert(!wf_observe(f,0,&v));
    const char *query=wf_text_get(&v,v.instruction);
    if(!query||strcmp(query,str(j,"query"))){
        fprintf(stderr,"query mismatch task=%u seed=%u phase=%u\n",task,seed,phase);abort();
    }
    assert(v.deadline_ms==(unsigned)num(j,"deadline"));
    if(task<2){
        const WFNode *input=find(&v,1),*submit=find(&v,2);
        assert(input&&submit&&v.count==2);
        const char *value=wf_text_get(&v,input->value);
        if(!value||strcmp(value,str(j,"value"))){
            fprintf(stderr,"input mismatch task=%u seed=%u phase=%u browser=%s model=%s\n",
                    task,seed,phase,str(j,"value"),value?value:"(invalid)");abort();
        }
    }else{
        const cJSON *slots=field(j,"slots");
        assert(v.count==6&&cJSON_IsArray(slots));
        for(unsigned i=0;i<6;i++){
            const cJSON *slot=cJSON_GetArrayItem(slots,(int)i);
            const WFNode *node=find(&v,i+3);assert(slot&&node);
            unsigned kind=(unsigned)num(slot,"kind");
            assert(node->role==(kind==2?WF_BUTTON:kind==1?WF_INPUT:WF_TEXT));
            const char *name=wf_text_get(&v,node->name);
            const char *value=wf_text_get(&v,node->value);
            if(!name||strcmp(name,str(slot,"text"))||!value||strcmp(value,"")){
                fprintf(stderr,"slot mismatch seed=%u phase=%u slot=%u\n",seed,phase,i);abort();
            }
        }
    }
    assert(!!f->words[WF_STATUS]==boolean(j,"done"));
    if(boolean(j,"done")&&
       (fabs(real(f->words[WF_RAW_REWARD])-num(j,"raw"))>1e-6||
        fabs(real(f->words[WF_TIMED_REWARD])-num(j,"reward"))>1e-5)){
        fprintf(stderr,"reward mismatch task=%u seed=%u phase=%u\n",task,seed,phase);abort();
    }
}
static cJSON *eval(WebCdp *c,const char *expression){
    cJSON *j=web_cdp_eval(c,expression);assert(j);return j;
}
static void action(WebCdp *c,WFLoaded *f,unsigned task,unsigned seed,
                   unsigned kind,unsigned ref,const char *value,unsigned now,
                   unsigned phase){
    char js[256];
    snprintf(js,sizeof js,"__ti.advance(%u);__ti.act(%u,%u,\"%s\")",
             now,kind,ref,value?value:"");
    cJSON *j=eval(c,js);
    WFAction a={.kind=kind,.target=ref,.elapsed_ms=now,
                .text=value,.text_length=value?strlen(value):0};
    assert(!wf_apply(f,0,&a));
    for(unsigned lane=1;lane<f->api->batch_lanes;lane++)
        memcpy(f->words+(size_t)lane*ROW,f->words,ROW*sizeof *f->words);
    assert(!wf_batch_checked(f));signal(SIGABRT,SIG_DFL);
    compare(f,j,task,seed,phase);cJSON_Delete(j);
}
static void public_step(WebCdp *c,WFLoaded *f,unsigned task,unsigned seed,
                        unsigned now,unsigned phase){
    WFView view;assert(!wf_observe(f,0,&view));
    char scratch[32];WFAction a={.elapsed_ms=now};
    assert(!typed_inputs_public_action(&view,&a,scratch,sizeof scratch));
    action(c,f,task,seed,a.kind,a.target,a.text,now,phase);
}
static unsigned wrong_button(const WFView *v){
    const char *query=wf_text_get(v,v->instruction);
    const char *begin=strstr(query,"Click on the \"");assert(begin);begin+=14;
    const char *end=strchr(begin,'"');assert(end);
    for(unsigned i=0;i<v->count;i++){
        const WFNode *n=v->nodes+i;
        const char *name=wf_text_get(v,n->name);
        if(n->role==WF_BUTTON&&name&&
           (strlen(name)!=(size_t)(end-begin)||memcmp(name,begin,(size_t)(end-begin))))
            return n->ref;
    }
    return 0;
}
int main(int argc,char **argv){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    setenv("TZ","UTC",1);
    unsigned episodes=argc>1?(unsigned)strtoul(argv[1],NULL,10):8;
    if(!episodes||episodes>100)return 2;
    WFLoaded f;char error[512];
    if(wf_open(&f,"build/webnav/families/typed_inputs/libtyped_inputs.so",
               error,sizeof error)){fprintf(stderr,"%s\n",error);return 1;}
    char *script=read_file("ocean/webnav/families/typed_inputs/browser.js");
    unsigned compared=0,wrong_unicode=0;
    for(unsigned task=0;task<3;task++){
        char file[PATH_MAX],resolved[PATH_MAX],url[PATH_MAX+8];
        snprintf(file,sizeof file,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",f.api->task_names[task]);
        assert(realpath(file,resolved));snprintf(url,sizeof url,"file://%s",resolved);
        WebCdp c={.input=-1,.output=-1,.pid=-1};
        const char *chrome=getenv("WEBNAV_CHROME");
        if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
        assert(!web_cdp_start_ready(&c,chrome,url,"Boolean(window.core&&core.cover_div)"));
        cJSON *params=cJSON_CreateObject();assert(params);
        cJSON_AddStringToObject(params,"timezoneId","UTC");
        cJSON *response=web_cdp_call(&c,"Emulation.setTimezoneOverride",params);
        assert(response);cJSON_Delete(response); /* web_cdp_call owns params. */
        params=cJSON_CreateObject();assert(params);
        cJSON_AddStringToObject(params,"locale","en-US");
        response=web_cdp_call(&c,"Emulation.setLocaleOverride",params);
        assert(response);cJSON_Delete(response); /* web_cdp_call owns params. */
        cJSON *installed=eval(&c,script);assert(cJSON_IsTrue(installed));cJSON_Delete(installed);
        for(unsigned ep=0;ep<episodes;ep++){
            unsigned seed=950000+task*1000+ep,phase=0;char js[80];
            snprintf(js,sizeof js,"__ti.reset(%u)",seed);
            cJSON *j=eval(&c,js);
            import_original(f.words,task,seed,j);
            assert(!f.api->validate(f.words));
            for(unsigned lane=1;lane<f.api->batch_lanes;lane++)
                memcpy(f.words+(size_t)lane*ROW,f.words,ROW*sizeof *f.words);
            compare(&f,j,task,seed,phase);cJSON_Delete(j);
            if(ep%4==2){
                action(&c,&f,task,seed,WF_WAIT,0,NULL,task==2?10000:20000,++phase);
            }else if(task<2){
                if(ep%4==1){
                    action(&c,&f,task,seed,WF_CLICK,2,NULL,100,++phase);
                }else{
                    if(ep%4==3)
                        action(&c,&f,task,seed,WF_INSERT,1,
                               task==0?"2023-02-29":"24:00",100,++phase);
                    public_step(&c,&f,task,seed,200,++phase);
                    public_step(&c,&f,task,seed,300,++phase);
                }
            }else if(ep%4==1){
                WFView v;assert(!wf_observe(&f,0,&v));
                unsigned wrong=wrong_button(&v);
                if(wrong){action(&c,&f,task,seed,WF_CLICK,wrong,NULL,100,++phase);wrong_unicode++;}
                else public_step(&c,&f,task,seed,100,++phase);
            }else public_step(&c,&f,task,seed,100,++phase);
            compared++;
        }
        web_cdp_close(&c);
    }
    free(script);wf_close(&f);
    printf("typed_inputs original-page episodes compared: %u (wrong Unicode clicks: %u)\n",
           compared,wrong_unicode);
    return 0;
}
