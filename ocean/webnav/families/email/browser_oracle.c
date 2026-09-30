#define _GNU_SOURCE
#include "../common/loader.h"
#include "../../cdp.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <sys/prctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROW 8192u
typedef struct {
    unsigned action,index;
    char name[32],reply[160],recipient[64];
} OracleGoal;
static double number(const cJSON *j,const char *key){const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsNumber(v));return v->valuedouble;}
static int boolean(const cJSON *j,const char *key){const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsBool(v));return cJSON_IsTrue(v);}
static const char *string(const cJSON *j,const char *key){const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);assert(cJSON_IsString(v));return v->valuestring;}
static float real(uint32_t bits){float f;memcpy(&f,&bits,4);return f;}
static char *read_file(const char *path){
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>=0);
    rewind(f);char *s=calloc((size_t)n+1u,1);assert(s&&fread(s,1,(size_t)n,f)==(size_t)n);
    fclose(f);return s;
}
static cJSON *eval(WebCdp *c,const char *expr){cJSON *j=web_cdp_eval(c,expr);assert(j);return j;}
static int units(uint32_t *out,unsigned cap,const char *s){
    size_t len=strlen(s);if(len>=cap)return -1;
    for(size_t i=0;i<len;i++){
        unsigned c=(unsigned char)s[i];if(c<32u||c>126u)return -1;
        out[i]=c;
    }
    return (int)len;
}
static int import(uint32_t *r,unsigned task,const cJSON *initial){
    memset(r,0,ROW*sizeof *r);r[0]=WF_ABI_VERSION;r[1]=task;r[12]=(unsigned)number(initial,"deadline");
    if(units(r+3200,1024,string(initial,"query"))<0)return -1;
    const cJSON *emails=cJSON_GetObjectItemCaseSensitive(initial,"emails");
    assert(cJSON_IsArray(emails));int count=cJSON_GetArraySize(emails);
    if(count<3||count>11)return -1;r[32]=(unsigned)count;
    for(int i=0;i<count;i++){
        const cJSON *e=cJSON_GetArrayItem(emails,i);uint32_t *m=r+64u+(unsigned)i*256u;
        int n=units(m+8,32,string(e,"name"));if(n<0)return -1;m[0]=(unsigned)n;
        n=units(m+40,40,string(e,"subject"));if(n<0)return -1;m[1]=(unsigned)n;
        n=units(m+80,160,string(e,"body"));if(n<0)return -1;m[2]=(unsigned)n;
    }
    const cJSON *goal=cJSON_GetObjectItemCaseSensitive(initial,"goal");assert(cJSON_IsObject(goal));
    r[40]=(unsigned)number(goal,"action");r[41]=(unsigned)number(goal,"index");
    if(units(r+5100,160,string(goal,"reply"))<0||
       units(r+5300,64,string(goal,"recipient"))<0)return -1;
    return 0;
}
static OracleGoal original_goal(const cJSON *initial){
    OracleGoal g={0};
    const cJSON *goal=cJSON_GetObjectItemCaseSensitive(initial,"goal");
    const cJSON *emails=cJSON_GetObjectItemCaseSensitive(initial,"emails");
    g.action=(unsigned)number(goal,"action");g.index=(unsigned)number(goal,"index");
    assert(g.action<4u&&g.index>=1u&&g.index<=(unsigned)cJSON_GetArraySize(emails));
    const cJSON *target=cJSON_GetArrayItem(emails,(int)g.index-1);
    assert(strlen(string(target,"name"))<sizeof g.name);
    assert(strlen(string(goal,"reply"))<sizeof g.reply);
    assert(strlen(string(goal,"recipient"))<sizeof g.recipient);
    strcpy(g.name,string(target,"name"));
    strcpy(g.reply,string(goal,"reply"));
    strcpy(g.recipient,string(goal,"recipient"));
    return g;
}
static cJSON *point(WebCdp *c,unsigned ref){
    char expr[128];snprintf(expr,sizeof expr,"__email.preparePoint(%u)",ref);
    cJSON *j=eval(c,expr);assert(cJSON_IsObject(j)&&boolean(j,"visible"));return j;
}
static void type_text(WebCdp *c,const char *s,int search){
    cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"text",s);
    cJSON *j=web_cdp_call(c,"Input.insertText",p);assert(j);cJSON_Delete(j);
    if(search){j=eval(c,"document.querySelector('#search-input').dispatchEvent(new KeyboardEvent('keyup',{bubbles:true}))");cJSON_Delete(j);}
}
static int browser_has(const cJSON *array,unsigned ref){
    assert(cJSON_IsArray(array));
    for(int i=0;i<cJSON_GetArraySize(array);i++)if((unsigned)cJSON_GetArrayItem(array,i)->valuedouble==ref)return 1;
    return 0;
}
static void compare(WFLoaded *f,const cJSON *j,unsigned task,unsigned episode,unsigned step){
    const uint32_t *r=f->words;WFView v;
    if(wf_observe(f,0,&v)){
        int projected=f->api->observe(r,&v);
        fprintf(stderr,"email projection failure task=%u episode=%u step=%u row_valid=%d projected=%d nodes=%u bytes=%u screen=%u\n",
                task,episode,step,f->api->validate(r),projected,v.count,v.text_bytes,r[33]);
        for(unsigned i=0;i<v.count&&i<WF_MAX_NODES;i++)fprintf(stderr,"  ref=%u role=%u flags=%u name=%u:%u value=%u:%u\n",
                v.nodes[i].ref,v.nodes[i].role,v.nodes[i].flags,v.nodes[i].name.offset,v.nodes[i].name.length,
                v.nodes[i].value.offset,v.nodes[i].value.length);
        abort();
    }
    const char *q=wf_text_get(&v,v.instruction);assert(q);
    if(strcmp(q,string(j,"query"))||r[33]!=(unsigned)number(j,"screen")||
       r[34]!=(unsigned)number(j,"selected")||
       (r[9]!=0u)!=boolean(j,"done")||
       (r[9]&&(fabs(real(r[10])-number(j,"raw"))>1e-6||
                 fabs(real(r[11])-number(j,"reward"))>1e-4))){
        fprintf(stderr,"email state mismatch task=%u episode=%u step=%u screen=%u/%g selected=%u/%g status=%u raw=%g/%g reward=%g/%g\n",
            task,episode,step,r[33],number(j,"screen"),r[34],number(j,"selected"),r[9],real(r[10]),number(j,"raw"),real(r[11]),number(j,"reward"));
        abort();
    }
    const struct {unsigned offset;const char *key;} fields[]={
        {4300,"search"},{4500,"reply"},{4700,"recipient"},{4800,"forward"}};
    for(unsigned fidx=0;fidx<4;fidx++){
        if((fidx==1u&&r[33]!=3u)||(fidx>=2u&&r[33]!=4u))continue;
        char text[161]={0};unsigned len=0;while(len<160u&&r[fields[fidx].offset+len]){
            text[len]=(char)r[fields[fidx].offset+len];len++;
        }
        if(strcmp(text,string(j,fields[fidx].key))){
            fprintf(stderr,"email field mismatch task=%u episode=%u step=%u field=%s\n",task,episode,step,fields[fidx].key);abort();
        }
    }
    const cJSON *visible=cJSON_GetObjectItemCaseSensitive(j,"visible");assert(cJSON_IsArray(visible));
    unsigned model_visible=0;
    for(unsigned i=0;i<v.count;i++){
        const WFNode *n=v.nodes+i;if(n->role==WF_TEXT)continue;
        model_visible++;
        if(!browser_has(visible,n->ref)){
            fprintf(stderr,"email missing visible ref task=%u episode=%u step=%u ref=%u\n",task,episode,step,n->ref);abort();
        }
    }
    assert(model_visible==(unsigned)cJSON_GetArraySize(visible));
    const cJSON *stars=cJSON_GetObjectItemCaseSensitive(j,"inboxStars");assert(cJSON_IsArray(stars));
    for(unsigned i=0;i<r[32];i++){
        const cJSON *star=cJSON_GetArrayItem(stars,(int)i);
        assert(cJSON_IsBool(star));
        assert((int)r[64u+i*256u+3u]==cJSON_IsTrue(star));
    }
    if(r[33]==2u)assert((int)r[64u+(r[34]-1u)*256u+5u]==boolean(j,"readStar"));
    const cJSON *results=cJSON_GetObjectItemCaseSensitive(j,"searchResults");assert(cJSON_IsArray(results));
    if(r[33]==1u)for(unsigned i=0;i<r[32];i++){
        int model_visible=0;for(unsigned k=0;k<v.count;k++)model_visible|=v.nodes[k].ref==13u+8u*i;
        assert(model_visible==browser_has(results,i+1u));
    }
}
static cJSON *snapshot(WebCdp *c){return eval(c,"__email.snapshot()");}
static void step(WFLoaded *f,WebCdp *c,unsigned task,unsigned ep,unsigned *counter,
                 unsigned kind,unsigned ref,unsigned ms,const char *text){
    char expr[128];snprintf(expr,sizeof expr,"__email.tick(%u)",ms);
    cJSON *tick=eval(c,expr);cJSON_Delete(tick);
    if(kind==WF_CLICK){cJSON *p=point(c,ref);assert(!web_cdp_click(c,number(p,"x"),number(p,"y")));cJSON_Delete(p);}
    else if(kind==WF_INSERT)type_text(c,text,ref==2u);
    WFAction a={.kind=kind,.target=kind==WF_CLICK?ref:0u,.elapsed_ms=ms,
                .text=text,.text_length=text?strlen(text):0u};
    assert(!wf_apply(f,0,&a));assert(!wf_batch_checked(f));
    cJSON *j=snapshot(c);compare(f,j,task,ep,(*counter)++);cJSON_Delete(j);
}
static void exercise(WFLoaded *f,WebCdp *c,unsigned task,unsigned ep,const OracleGoal *oracle){
    const uint32_t *r=f->words;
    unsigned target=oracle->index,wrong=target==1u?2u:1u,goal=oracle->action,stepno=1u;
    unsigned target_ref=10u+8u*(target-1u),wrong_ref=10u+8u*(wrong-1u);
    if(ep%7u==0u){
        if(goal==2u||goal==3u)step(f,c,task,ep,&stepno,WF_CLICK,target_ref+(goal==2u?2u:1u),100,NULL);
        else{
            step(f,c,task,ep,&stepno,WF_CLICK,target_ref,100,NULL);
            if(goal==0u){
                step(f,c,task,ep,&stepno,WF_CLICK,113,200,NULL);
                step(f,c,task,ep,&stepno,WF_CLICK,117,300,NULL);
                step(f,c,task,ep,&stepno,WF_INSERT,117,400,oracle->reply);
                step(f,c,task,ep,&stepno,WF_CLICK,116,500,NULL);
            }else{
                step(f,c,task,ep,&stepno,WF_CLICK,114,200,NULL);
                step(f,c,task,ep,&stepno,WF_CLICK,120,300,NULL);
                step(f,c,task,ep,&stepno,WF_INSERT,120,400,oracle->recipient);
                step(f,c,task,ep,&stepno,WF_CLICK,119,500,NULL);
            }
        }
        assert(r[9]==WF_TERMINAL&&r[10]==1065353216u);
    }else if(ep%7u==1u){
        if(goal==2u||goal==3u)step(f,c,task,ep,&stepno,WF_CLICK,wrong_ref+(goal==2u?2u:1u),100,NULL);
        else{
            step(f,c,task,ep,&stepno,WF_CLICK,target_ref,100,NULL);
            step(f,c,task,ep,&stepno,WF_CLICK,goal==0u?113u:114u,200,NULL);
            step(f,c,task,ep,&stepno,WF_CLICK,goal==0u?117u:120u,300,NULL);
            step(f,c,task,ep,&stepno,WF_INSERT,0,400,"wrong");
            step(f,c,task,ep,&stepno,WF_CLICK,goal==0u?116u:119u,500,NULL);
        }
        assert(r[9]==WF_TERMINAL&&r[10]==3212836864u);
    }else if(ep%7u==2u){
        step(f,c,task,ep,&stepno,WF_WAIT,0,30000,NULL);
        assert(r[9]==WF_TIMEOUT);
    }else if(ep%7u==3u){
        step(f,c,task,ep,&stepno,WF_CLICK,1,100,NULL);
        step(f,c,task,ep,&stepno,WF_CLICK,2,200,NULL);
        step(f,c,task,ep,&stepno,WF_INSERT,2,300,oracle->name);
        step(f,c,task,ep,&stepno,WF_CLICK,target_ref+4u,400,NULL);
        step(f,c,task,ep,&stepno,WF_CLICK,112,500,NULL);
        step(f,c,task,ep,&stepno,WF_CLICK,1,600,NULL);
        step(f,c,task,ep,&stepno,WF_CLICK,3,700,NULL);
        step(f,c,task,ep,&stepno,WF_CLICK,1,800,NULL);
    }else if(ep%7u==4u){
        step(f,c,task,ep,&stepno,WF_CLICK,1,100,NULL);
        step(f,c,task,ep,&stepno,WF_CLICK,3,200,NULL);
        step(f,c,task,ep,&stepno,WF_CLICK,1,300,NULL);
    }else if(ep%7u==5u){
        step(f,c,task,ep,&stepno,WF_CLICK,wrong_ref+1u,100,NULL);
        if(goal!=3u)assert(r[9]==WF_RUNNING);
    }else{
        step(f,c,task,ep,&stepno,WF_CLICK,wrong_ref+2u,100,NULL);
        if(goal!=2u)assert(r[9]==WF_RUNNING);
    }
}
int main(int argc,char **argv){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    unsigned episodes=argc>1?strtoul(argv[1],NULL,10):14u;if(!episodes||episodes>140u)return 2;
    WFLoaded f;char error[512];if(wf_open(&f,"build/webnav/families/email/libemail.so",error,sizeof error)){
        fprintf(stderr,"%s\n",error);return 1;
    }
    char *script=read_file("ocean/webnav/families/email/browser.js");unsigned total=0,unsupported=0;
    for(unsigned task=0;task<10;task++){
        char file[PATH_MAX],resolved[PATH_MAX],url[PATH_MAX+8];
        snprintf(file,sizeof file,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",f.api->task_names[task]);
        assert(realpath(file,resolved));snprintf(url,sizeof url,"file://%s",resolved);
        WebCdp c={.input=-1,.output=-1,.pid=-1};const char *chrome=getenv("WEBNAV_CHROME");
        if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
        assert(!web_cdp_start_ready(&c,chrome,url,"Boolean(document.readyState==='complete'&&window.core&&core.cover_div)"));
        cJSON *injected=eval(&c,script);cJSON_Delete(injected);
        for(unsigned ep=0;ep<episodes;ep++){
            unsigned seed=400000u+task*10000u+ep;char expr[128];snprintf(expr,sizeof expr,"__email.reset(%u)",seed);
            cJSON *initial=eval(&c,expr);
            if(import(f.words,task,initial)){unsupported++;cJSON_Delete(initial);continue;}
            OracleGoal oracle=original_goal(initial);
            for(unsigned i=1;i<4;i++)memcpy(f.words+i*ROW,f.words,ROW*sizeof *f.words);
            assert(!f.api->validate(f.words));compare(&f,initial,task,ep,0u);cJSON_Delete(initial);
            exercise(&f,&c,task,ep,&oracle);total++;
        }
        web_cdp_close(&c);
    }
    free(script);wf_close(&f);
    printf("email browser source: compared=%u unsupported_bounds=%u\n",total,unsupported);
    return unsupported?2:0;
}
