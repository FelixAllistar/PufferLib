#define _GNU_SOURCE
#include "../common/loader.h"
#include "../../cdp.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

#define ROW 1024u
static const char *const names[]={"Phonecall","Food","Party","Meeting","Gym"};
static const unsigned month_days[]={31,29,31,30,31,30,31,31,30,31,30,31};
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
static const WFNode *find(const WFView *v,unsigned ref){
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].ref==ref)return v->nodes+i;
    return NULL;
}
static float real(uint32_t bits){float f;memcpy(&f,&bits,4);return f;}
static char *read_file(const char *path){
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));
    long n=ftell(f);assert(n>=0);rewind(f);
    char *s=calloc((size_t)n+1,1);assert(s&&fread(s,1,(size_t)n,f)==(size_t)n);
    fclose(f);return s;
}
static void units(uint32_t *dst,unsigned cap,const char *s){
    size_t n=strlen(s);assert(n<cap);
    for(size_t i=0;i<n;i++){assert((unsigned char)s[i]>=32&&(unsigned char)s[i]<=126);dst[i]=(unsigned char)s[i];}
}
static unsigned date_goal(const char *q){
    unsigned m=0,d=0,before=0;assert(sscanf(q,"Select %u/%u/2016",&m,&d)==2);
    assert(m>=1&&m<=12&&d>=1&&d<=month_days[m-1]);
    for(unsigned i=1;i<m;i++)before+=month_days[i-1];return before+d;
}
static unsigned name_id(const char *name){
    for(unsigned i=0;i<5;i++)if(!strcmp(name,names[i]))return i;
    assert(0);return 0;
}
static unsigned expected_name(const char *query){
    const char *p=strstr(query," event named \"");assert(p);p+=14;
    const char *end=strchr(p,'"');assert(end&&end-p<32);
    char name[32];memcpy(name,p,(size_t)(end-p));name[end-p]=0;
    return name_id(name);
}
static unsigned duration(const char *q){
    if(strstr(q,"0.5 hours")||strstr(q,"30 mins"))return 1;
    if(strstr(q,"1.5 hours")||strstr(q,"90 mins"))return 3;
    assert(strstr(q,"1 hour")||strstr(q,"60 mins"));return 2;
}
static unsigned window_id(const char *q){
    if(strstr(q,"8AM and 12PM"))return 0;
    if(strstr(q,"12PM and 4PM"))return 1;
    assert(strstr(q,"4PM and 8PM"));return 2;
}
static void import_original(uint32_t *r,unsigned task,unsigned seed,const cJSON *j){
    memset(r,0,ROW*sizeof *r);r[WF_VERSION]=2;r[WF_TASK]=task;
    r[WF_OP]=WF_OBSERVE;r[WF_SEED]=seed;r[WF_DEADLINE]=(unsigned)num(j,"deadline");
    units(r+256,192,str(j,"query"));
    if(task<4){r[32]=12;r[35]=date_goal(str(j,"query"));return;}
    r[36]=window_id(str(j,"query"));r[37]=duration(str(j,"query"));
    r[38]=expected_name(str(j,"query"));r[39]=48;r[40]=48;
    const cJSON *events=field(j,"events");assert(cJSON_IsArray(events)&&cJSON_GetArraySize(events)==3);
    for(unsigned i=0;i<3;i++){
        const cJSON *e=cJSON_GetArrayItem(events,(int)i);assert(e);
        r[48+i]=(unsigned)num(e,"start");
        r[51+i]=r[48+i]+(unsigned)num(e,"duration");
        r[54+i]=name_id(str(e,"name"));
    }
}
static void compare(WFLoaded *f,const cJSON *j,unsigned task,unsigned step){
    WFView v;assert(!wf_observe(f,0,&v));
    assert(!strcmp(wf_text_get(&v,v.instruction),str(j,"query")));
    assert(v.deadline_ms==(unsigned)num(j,"deadline"));
    if(task<4){
        const WFNode *input=find(&v,1);assert(input);
        assert(!strcmp(wf_text_get(&v,input->value),str(j,"value")));
        if(!!find(&v,36)!=boolean(j,"open")){
            fprintf(stderr,"popup mismatch task=%u step=%u model=%d browser=%d query=%s\n",
                    task,step,!!find(&v,36),boolean(j,"open"),str(j,"query"));abort();
        }
        if(boolean(j,"open")){
            const WFNode *title=find(&v,36);assert(title);
            const char *caption=wf_text_get(&v,title->name);
            assert(!strncmp(caption,str(j,"month"),strlen(str(j,"month"))));
        }
    }else{
        assert(!!find(&v,52)==boolean(j,"draft"));
        assert(!!find(&v,53)==boolean(j,"modal"));
        const WFNode *area=find(&v,56);assert(area);
        assert(fabs(area->scroll_y-num(j,"scroll"))<=1.0);
        if(boolean(j,"modal")){
            const WFNode *input=find(&v,53);assert(input);
            assert(!strcmp(wf_text_get(&v,input->value),str(j,"eventName")));
        }
    }
    assert(!!f->words[WF_STATUS]==boolean(j,"done"));
    if(boolean(j,"done")){
        if(fabs(real(f->words[WF_RAW_REWARD])-num(j,"raw"))>1e-6||
           fabs(real(f->words[WF_TIMED_REWARD])-num(j,"reward"))>1e-5){
            fprintf(stderr,"reward mismatch task=%u step=%u\n",task,step);abort();
        }
    }
}
static cJSON *eval(WebCdp *c,const char *expression){
    cJSON *j=web_cdp_eval(c,expression);assert(j);return j;
}
static void act(WebCdp *c,WFLoaded *f,unsigned task,unsigned kind,unsigned ref,
                unsigned arg,const char *name,unsigned now,unsigned step){
    char js[256];snprintf(js,sizeof js,"__cal.advance(%u);__cal.act(%u,%u,%u,\"%s\")",
                           now,kind,ref,arg,name?name:"");
    cJSON *j=eval(c,js);
    WFAction a={.kind=kind,.target=ref,.arg0=arg,.elapsed_ms=now,
                .text=name,.text_length=name?strlen(name):0};
    assert(!wf_apply(f,0,&a));
    for(unsigned lane=1;lane<f->api->batch_lanes;lane++)
        memcpy(f->words+(size_t)lane*ROW,f->words,ROW*sizeof *f->words);
    assert(!wf_batch_checked(f));compare(f,j,task,step);cJSON_Delete(j);
}
int main(int argc,char **argv){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    unsigned episodes=argc>1?(unsigned)strtoul(argv[1],NULL,10):2;
    if(!episodes||episodes>100)return 2;
    WFLoaded f;char error[512];
    if(wf_open(&f,"build/webnav/families/calendar/libcalendar.so",error,sizeof error)){
        fprintf(stderr,"%s\n",error);return 1;
    }
    char *script=read_file("ocean/webnav/families/calendar/browser.js");
    unsigned compared=0;
    for(unsigned task=0;task<5;task++){
        char file[PATH_MAX],resolved[PATH_MAX],url[PATH_MAX+8];
        snprintf(file,sizeof file,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",f.api->task_names[task]);
        assert(realpath(file,resolved));snprintf(url,sizeof url,"file://%s",resolved);
        WebCdp c={.input=-1,.output=-1,.pid=-1};
        const char *chrome=getenv("WEBNAV_CHROME");
        if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
        assert(!web_cdp_start_ready(&c,chrome,url,"Boolean(window.core&&core.cover_div)"));
        cJSON *loaded=eval(&c,script);assert(cJSON_IsTrue(loaded));cJSON_Delete(loaded);
        for(unsigned ep=0;ep<episodes;ep++){
            unsigned seed=940000+task*1000+ep,step=0;char js[80];
            snprintf(js,sizeof js,"__cal.reset(%u)",seed);
            cJSON *j=eval(&c,js);import_original(f.words,task,seed,j);
            assert(!f.api->validate(f.words));
            for(unsigned lane=1;lane<f.api->batch_lanes;lane++)
                memcpy(f.words+(size_t)lane*ROW,f.words,ROW*sizeof *f.words);
            compare(&f,j,task,step);cJSON_Delete(j);
            if(ep%5==4){
                j=eval(&c,"__cal.advance(20000);__cal.export()");
                WFAction wait={.kind=WF_WAIT,.elapsed_ms=20000};assert(!wf_apply(&f,0,&wait));
                for(unsigned lane=1;lane<f.api->batch_lanes;lane++)
                    memcpy(f.words+(size_t)lane*ROW,f.words,ROW*sizeof *f.words);
                assert(!wf_batch_checked(&f));compare(&f,j,task,++step);cJSON_Delete(j);
            }else if(task<4){
                if(ep%5==2)act(&c,&f,task,WF_CLICK,2,0,NULL,100,++step);
                else{
                    unsigned goal=f.words[35],month=1,day=goal;
                    while(day>month_days[month-1])day-=month_days[month++-1];
                    act(&c,&f,task,WF_CLICK,1,0,NULL,100,++step);
                    for(unsigned m=12;m>month;m--)
                        act(&c,&f,task,WF_CLICK,3,0,NULL,200+(12-m)*100,++step);
                    act(&c,&f,task,WF_CLICK,day+4,0,NULL,1600,++step);
                    act(&c,&f,task,WF_CLICK,2,0,NULL,1700,++step);
                }
            }else{
                unsigned start=16+f.words[36]*8,dur=f.words[37];
                const char *name=names[f.words[38]];
                act(&c,&f,task,WF_SCROLL,56,start,NULL,100,++step);
                act(&c,&f,task,WF_POINTER_DOWN,start+1,0,NULL,200,++step);
                act(&c,&f,task,WF_POINTER_MOVE,start+dur,0,NULL,300,++step);
                act(&c,&f,task,WF_POINTER_UP,52,0,NULL,400,++step);
                if(ep%5==3)act(&c,&f,task,WF_CLICK,54,0,NULL,500,++step);
                else{
                    act(&c,&f,task,WF_INSERT,53,0,ep%5==2?"Wrong":name,500,++step);
                    act(&c,&f,task,WF_CLICK,55,0,NULL,600,++step);
                }
            }
            compared++;
        }
        web_cdp_close(&c);
    }
    free(script);wf_close(&f);
    printf("calendar original-page episodes compared: %u\n",compared);
    return 0;
}
