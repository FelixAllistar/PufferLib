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
static const cJSON *member(const cJSON *j,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(v);return v;
}
static const cJSON *item(const cJSON *a,int i){
    const cJSON *v=cJSON_GetArrayItem(a,i);assert(v);return v;
}
static unsigned num(const cJSON *j,const char *key){
    const cJSON *v=member(j,key);assert(cJSON_IsNumber(v));
    assert(v->valuedouble>=0&&v->valuedouble<=UINT_MAX);
    return (unsigned)v->valuedouble;
}
static double real_number(const cJSON *j,const char *key){
    const cJSON *v=member(j,key);assert(cJSON_IsNumber(v));return v->valuedouble;
}
static int yes(const cJSON *j,const char *key){
    const cJSON *v=member(j,key);assert(cJSON_IsBool(v));return cJSON_IsTrue(v);
}
static const char *str(const cJSON *j,const char *key){
    const cJSON *v=member(j,key);assert(cJSON_IsString(v));return v->valuestring;
}
static float bits_float(uint32_t bits){float f;memcpy(&f,&bits,sizeof f);return f;}
static char *read_file(const char *path){
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));
    long n=ftell(f);assert(n>=0);rewind(f);
    char *s=calloc((size_t)n+1,1);
    assert(s&&fread(s,1,(size_t)n,f)==(size_t)n);fclose(f);return s;
}
static void text_words(uint32_t *r,unsigned at,unsigned cap,const char *s){
    size_t n=strlen(s);assert(n<cap);
    for(size_t i=0;i<n;i++){
        assert((unsigned char)s[i]>=32&&(unsigned char)s[i]<=126);
        r[at+(unsigned)i]=(unsigned char)s[i];
    }
}
static unsigned airport_index(const cJSON *catalog,const char *name){
    for(int i=0;i<6;i++)if(!strcmp(item(catalog,i)->valuestring,name))
        return (unsigned)i+1;
    assert(!"sampled airport absent from catalog");return 0;
}
static unsigned day_index(const char *query){
    const char *p=strstr(query," on ");assert(p);p+=4;
    unsigned month=0,day=0,year=0;
    assert(sscanf(p,"%u/%u/%u",&month,&day,&year)==3&&year==2016);
    assert((month==10&&day>=1&&day<=31)||
           (month==11&&day>=1&&day<=30)||
           (month==12&&day>=1&&day<=30));
    return (month==10?0u:month==11?31u:61u)+day;
}
static void flight_pairs(uint32_t *r,unsigned base,const cJSON *a){
    int count=cJSON_GetArraySize(a);assert(count>=3&&count<=4);
    for(int i=0;i<count;i++){
        const cJSON *f=item(a,i);
        r[base+4u*(unsigned)i]=num(f,"price");
        r[base+4u*(unsigned)i+1u]=num(f,"duration");
    }
}
static void import_original(uint32_t *r,unsigned task,unsigned seed,const cJSON *j){
    memset(r,0,ROW*sizeof *r);
    r[WF_VERSION]=WF_ABI_VERSION;r[WF_TASK]=task;r[WF_OP]=WF_OBSERVE;
    r[WF_SEED]=seed;r[WF_DEADLINE]=num(j,"deadline");
    text_words(r,512,255,str(j,"query"));
    const cJSON *fixture=member(j,"fixture");
    if(task==2){
        const char *q=str(j,"query");
        r[32]=strstr(q,"cheapest cost")?0u:
              strstr(q,"shortest duration")?1u:
              strstr(q,"longest duration")?2u:3u;
        r[33]=1; /* Invisible date has no effect on ticket ranking. */
        r[40]=4;
        const cJSON *tickets=member(fixture,"tickets");
        assert(cJSON_GetArraySize(tickets)==4);
        for(unsigned i=0;i<4;i++){
            const cJSON *f=item(tickets,(int)i);
            r[64+4*i]=num(f,"price");r[65+4*i]=num(f,"duration");
        }
        return;
    }
    const cJSON *catalog=member(fixture,"airports");
    assert(cJSON_GetArraySize(catalog)==6);
    r[35]=airport_index(catalog,str(fixture,"origin"));
    r[36]=airport_index(catalog,str(fixture,"destination"));
    r[37]=day_index(str(j,"query"));
    r[38]=strstr(str(j,"query"),"shortest one-way")?1u:0u;
    r[40]=(unsigned)cJSON_GetArraySize(member(fixture,"real"));
    r[44]=(unsigned)cJSON_GetArraySize(member(fixture,"fake"));
    r[43]=6;
    flight_pairs(r,64,member(fixture,"real"));
    flight_pairs(r,128,member(fixture,"fake"));
    for(unsigned i=0;i<6;i++){
        const cJSON *name=item(catalog,(int)i);assert(cJSON_IsString(name));
        size_t len=strlen(name->valuestring);assert(len>0&&len<=96);
        unsigned at=1024+128*(i+1);r[at]=(uint32_t)len;
        text_words(r,at+1,127,name->valuestring);
    }
}
static const WFNode *node(const WFView *v,unsigned ref){
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].ref==ref)return &v->nodes[i];
    return NULL;
}
static void compare(WFLoaded *f,const cJSON *j,unsigned task,unsigned ep,unsigned step){
    const uint32_t *r=f->words;WFView v;assert(!wf_observe(f,0,&v));
    assert(!strcmp(wf_text_get(&v,v.instruction),str(j,"query")));
    assert(v.elapsed_ms==num(j,"elapsed"));
    assert(v.deadline_ms==num(j,"deadline"));
    assert(!!r[WF_STATUS]==yes(j,"done"));
    if(r[WF_STATUS]){
        double raw=real_number(j,"raw"),reward=real_number(j,"reward");
        if(fabs(raw-bits_float(r[WF_RAW_REWARD]))>1e-6||
           fabs(reward-bits_float(r[WF_TIMED_REWARD]))>1e-5){
            fprintf(stderr,"travel reward mismatch task=%u episode=%u step=%u browser=%g/%g model=%g/%g\n",
                task,ep,step,raw,reward,bits_float(r[WF_RAW_REWARD]),bits_float(r[WF_TIMED_REWARD]));
            abort();
        }
    }
    if(task==2){
        const cJSON *flights=member(j,"flights");assert(cJSON_GetArraySize(flights)==4);
        for(unsigned i=0;i<4;i++){
            const WFNode *n=node(&v,i+1);assert(n&&n->role==WF_BUTTON);
            const cJSON *src=item(flights,(int)i);char label[64],data[80];
            snprintf(label,sizeof label,"Book for $%u",num(src,"price"));
            snprintf(data,sizeof data,"%uh %um; data-duration=%ums",
                num(src,"duration")/3600000u,
                (num(src,"duration")/60000u)%60u,num(src,"duration"));
            assert(!strcmp(wf_text_get(&v,n->name),label));
            assert(!strcmp(wf_text_get(&v,n->value),data));
        }
        return;
    }
    assert(r[39]==num(j,"phase")&&r[41]==num(j,"errors"));
    if(!r[39]){
        assert(v.count==102);
        assert(!strcmp(wf_text_get(&v,node(&v,1)->value),str(j,"origin")));
        assert(!strcmp(wf_text_get(&v,node(&v,2)->value),str(j,"destination")));
        assert(!strcmp(wf_text_get(&v,node(&v,3)->value),str(j,"date")));
    }else{
        const cJSON *flights=member(j,"flights");
        unsigned count=r[42]?r[44]:r[40];
        assert((unsigned)cJSON_GetArraySize(flights)==count);
        assert(v.count==count+1);
        for(unsigned i=0;i<count;i++){
            const WFNode *n=node(&v,i+6);assert(n&&n->role==WF_BUTTON);
            const cJSON *src=item(flights,(int)i);char label[64],data[80];
            snprintf(label,sizeof label,"Book flight for $%u",num(src,"price"));
            snprintf(data,sizeof data,"%uh %um; data-duration=%ums",
                num(src,"duration")/3600000u,
                (num(src,"duration")/60000u)%60u,num(src,"duration"));
            assert(!strcmp(wf_text_get(&v,n->name),label));
            assert(!strcmp(wf_text_get(&v,n->value),data));
        }
    }
}
static void apply(WFLoaded *f,const WFAction *action){
    assert(!wf_apply(f,0,action));
    for(unsigned lane=1;lane<f->api->batch_lanes;lane++)
        memcpy(f->words+(size_t)lane*ROW,f->words,ROW*sizeof *f->words);
    assert(!wf_batch_checked(f));
    signal(SIGABRT,SIG_DFL);
}
static cJSON *eval(WebCdp *c,const char *script){
    cJSON *j=web_cdp_eval(c,script);assert(j);return j;
}
static cJSON *select_field(WebCdp *c,WFLoaded *f,unsigned field,unsigned value,
                            unsigned now,unsigned task,unsigned ep,unsigned *step){
    char js[128];
    if(field==3&&value==92)snprintf(js,sizeof js,"__tr.select(3,'12/31/2016')");
    else if(field==3)snprintf(js,sizeof js,"__tr.select(3,__tr.goalDate)");
    else if(value==7)snprintf(js,sizeof js,"__tr.select(%u,'Invalid airport')",field);
    else if(value==0)snprintf(js,sizeof js,"__tr.select(%u,'')",field);
    else snprintf(js,sizeof js,"__tr.select(%u,__tr.catalog[%u])",field,value-1);
    cJSON *j=eval(c,js);
    WFAction a={.kind=WF_SELECT_OPTION,.target=field,.arg0=value,.elapsed_ms=now};
    apply(f,&a);compare(f,j,task,ep,++*step);return j;
}
static cJSON *click(WebCdp *c,WFLoaded *f,unsigned ref,unsigned now,
                    unsigned task,unsigned ep,unsigned *step){
    char js[64];
    if(task==2)snprintf(js,sizeof js,"__tr.book(%u)",ref-1);
    else if(ref==4)snprintf(js,sizeof js,"__tr.search()");
    else if(ref==5)snprintf(js,sizeof js,"__tr.back()");
    else snprintf(js,sizeof js,"__tr.book(%u)",ref-6);
    cJSON *j=eval(c,js);WFAction a={.kind=WF_CLICK,.target=ref,.elapsed_ms=now};
    apply(f,&a);compare(f,j,task,ep,++*step);return j;
}
static unsigned best_flight(const uint32_t *r,unsigned base,unsigned count,unsigned duration){
    unsigned best=0,score=UINT_MAX;
    for(unsigned i=0;i<count;i++){
        unsigned value=r[base+4*i+duration];
        if(value<score){score=value;best=i;}
    }
    return best;
}
static unsigned ticket_choice(const uint32_t *r,unsigned ep){
    unsigned criterion=r[32],duration=criterion==1||criterion==2;
    unsigned want_max=criterion>=2,best=0,score=r[64+duration];
    for(unsigned i=1;i<4;i++){
        unsigned value=r[64+4*i+duration];
        if(want_max?value>score:value<score){score=value;best=i;}
    }
    return ep%3u==0?best:(best+ep)%4u;
}
int main(int argc,char **argv){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    unsigned episodes=argc>1?(unsigned)strtoul(argv[1],NULL,10):4u;
    if(!episodes||episodes>200)return 2;
    WFLoaded f;char error[512];
    if(wf_open(&f,"build/webnav/families/travel/libtravel.so",error,sizeof error)){
        fprintf(stderr,"%s\n",error);return 1;
    }
    char *script=read_file("ocean/webnav/families/travel/browser.js");
    for(unsigned task=0;task<3;task++){
        char path[PATH_MAX],resolved[PATH_MAX],url[PATH_MAX+8];
        snprintf(path,sizeof path,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",f.api->task_names[task]);
        assert(realpath(path,resolved));snprintf(url,sizeof url,"file://%s",resolved);
        WebCdp c={.input=-1,.output=-1,.pid=-1};
        const char *chrome=getenv("WEBNAV_CHROME");
        if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
        assert(!web_cdp_start_ready(&c,chrome,url,"Boolean(window.core&&core.cover_div)"));
        cJSON *loaded=eval(&c,script);assert(cJSON_IsTrue(loaded));cJSON_Delete(loaded);
        unsigned successes=0,actions=0;
        for(unsigned ep=0;ep<episodes;ep++){
            unsigned seed=940000u+task*1000u+ep,step=0;
            char js[96];snprintf(js,sizeof js,"__tr.reset(%u)",seed);
            cJSON *j=eval(&c,js);import_original(f.words,task,seed,j);
            unsigned wrong=0;
            if(task!=2){
                const cJSON *fixture=member(j,"fixture");
                wrong=airport_index(member(fixture,"airports"),str(fixture,"wrong"));
            }
            assert(!f.api->validate(f.words));compare(&f,j,task,ep,step);cJSON_Delete(j);
            unsigned now=250;
            j=eval(&c,"__tr.advance(250)");cJSON_Delete(j);
            if(task==2){
                unsigned pick=ticket_choice(f.words,ep);
                j=click(&c,&f,pick+1,now,task,ep,&step);
                successes+=f.words[WF_RAW_REWARD]==1065353216u;
                cJSON_Delete(j);actions++;continue;
            }
            if(ep%6u==4u){
                now=30000;j=eval(&c,"__tr.advance(30000)");
                WFAction wait={.kind=WF_WAIT,.elapsed_ms=now};apply(&f,&wait);
                compare(&f,j,task,ep,++step);cJSON_Delete(j);actions++;continue;
            }
            unsigned from=f.words[35],to=f.words[36],date=f.words[37];
            if(ep%6u==1u){
                j=select_field(&c,&f,1,wrong,now,task,ep,&step);cJSON_Delete(j);actions++;
            }else if(ep%6u==2u){
                j=select_field(&c,&f,1,7,now,task,ep,&step);cJSON_Delete(j);actions++;
            }else{
                j=select_field(&c,&f,1,from,now,task,ep,&step);cJSON_Delete(j);actions++;
            }
            j=select_field(&c,&f,2,to,now,task,ep,&step);cJSON_Delete(j);actions++;
            j=select_field(&c,&f,3,ep%6u==3u?92u:date,now,task,ep,&step);
            cJSON_Delete(j);actions++;
            j=click(&c,&f,4,now,task,ep,&step);cJSON_Delete(j);actions++;
            if(ep%6u==2u){
                j=select_field(&c,&f,1,from,now,task,ep,&step);cJSON_Delete(j);actions++;
                j=click(&c,&f,4,now,task,ep,&step);cJSON_Delete(j);actions++;
            }
            if(ep%6u==5u){
                j=click(&c,&f,5,now,task,ep,&step);cJSON_Delete(j);actions++;
                j=click(&c,&f,4,now,task,ep,&step);cJSON_Delete(j);actions++;
            }
            assert(f.words[39]);
            unsigned count=f.words[42]?f.words[44]:f.words[40];
            unsigned pick=best_flight(f.words,f.words[42]?128:64,count,f.words[38]);
            j=click(&c,&f,pick+6,now,task,ep,&step);
            successes+=f.words[WF_RAW_REWARD]==1065353216u;
            cJSON_Delete(j);actions++;
        }
        printf("{\"task\":\"%s\",\"episodes\":%u,\"actions\":%u,\"scripted_successes\":%u,\"differential\":\"PASS\",\"preset\":\"pinned original HTML; reset-precommitted real/fake tables; atomic form selection; one wrong Search per trace\"}\n",
            f.api->task_names[task],episodes,actions,successes);
        fflush(stdout);web_cdp_close(&c);signal(SIGABRT,SIG_DFL);
    }
    free(script);wf_close(&f);return 0;
}
