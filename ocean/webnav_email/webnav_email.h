#pragma once
/* PPO adapter for the stock CPU Bend email family. The only policy inputs are
 * WFView and interaction history collected from earlier WFViews. */
typedef float obs_t;
#include "pufferenv.h"
#include "../webnav/families/common/loader.h"
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <strings.h>

#define WFE_LANES 4
#define WFE_NODES 40
#define WFE_CANDIDATES 32
#define WFE_PRINTABLE 95
#define WFE_NODE_FEATURES 104
#define WFE_CANDIDATE_FEATURES 176
#define WFE_PREFIX 1056
#define OBS_SIZE (WFE_PREFIX + WFE_NODES*WFE_NODE_FEATURES + WFE_CANDIDATES*WFE_CANDIDATE_FEATURES)
#define WFE_CLICK_BASE 1
#define WFE_SELECT (WFE_CLICK_BASE + WFE_NODES)
#define WFE_BACKSPACE (WFE_SELECT + 1)
#define WFE_COPY_BASE (WFE_BACKSPACE + 1)
#define WFE_CHAR_BASE (WFE_COPY_BASE + WFE_CANDIDATES)
#define WFE_ACTIONS (WFE_CHAR_BASE + WFE_PRINTABLE)
#define ACT_SIZES {WFE_ACTIONS}
#define NUM_ATNS 1

typedef struct {char text[160];unsigned length,kind,source;} WFECandidate;
typedef struct {WFECandidate items[WFE_CANDIDATES];} WFECatalog;
typedef struct {
    unsigned char visited[256];
    char sender[11][32],body[160];
    unsigned senders,clicks,edits,last_ref,last_kind,selected_ref;
} WFEHistory;
struct Log {float perf,score,episode_length,n;};
struct Env {
    Log log;
    int num_agents,tag,boundary_reached;
    Agent agents[WFE_LANES];
    WFLoaded family;
    uint32_t rng;
    unsigned task_mask,steps[WFE_LANES];
    WFEHistory history[WFE_LANES];
};

static uint32_t wfe_random(Env *env) {
    env->rng=env->rng*1664525u+1013904223u;
    uint32_t x=env->rng;x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;
    return x;
}
static unsigned wfe_task(Env *env) {
    unsigned count=0;
    for(unsigned i=0;i<10;i++)count+=(env->task_mask>>i)&1u;
    unsigned rank=wfe_random(env)%count;
    for(unsigned i=0;i<10;i++)if((env->task_mask>>i)&1u)if(!rank--)return i;
    abort();
}
static void wfe_reset_lane(Env *env,unsigned lane) {
    uint32_t *r=env->family.words+(size_t)lane*env->family.api->row_words;
    memset(r,0,env->family.api->row_words*sizeof *r);
    r[WF_VERSION]=WF_ABI_VERSION;r[WF_TASK]=wfe_task(env);
    r[WF_OP]=WF_RESET;r[WF_SEED]=wfe_random(env);
    env->steps[lane]=0;
    memset(env->history+lane,0,sizeof env->history[lane]);
}
static void wfe_bytes(const char *s,unsigned n,float *out) {
    if(!s)return;
    for(unsigned i=0;i<n&&s[i];i++)out[i]=(unsigned char)s[i]/127.0f;
}
static const char *wfe_find(const char *haystack,const char *needle) {
    if(!haystack||!needle||!*needle)return NULL;
    size_t n=strlen(needle);
    for(const char *p=haystack;*p;p++)if(!strncasecmp(p,needle,n))return p;
    return NULL;
}
static int wfe_contains(const char *haystack,const char *needle) {
    if(!haystack||!needle||!*needle)return 0;
    return wfe_find(haystack,needle)!=NULL;
}
/* A public history of names matters after opening a message: other inbox
 * senders cease to be visible, yet can be forward recipients. */
static void wfe_record(WFEHistory *h,const WFView *v) {
    for(unsigned i=0;i<v->count;i++){
        const WFNode *n=v->nodes+i;
        const char *name=wf_text_get(v,n->name);
        const char *value=wf_text_get(v,n->value);
        if(n->role==WF_BUTTON&&n->parent==0&&n->ref>=10&&n->ref<106&&
           (n->ref-10)%8==0&&name&&*name&&strlen(name)<32){
            int found=0;
            for(unsigned j=0;j<h->senders;j++)found|=!strcmp(h->sender[j],name);
            if(!found&&h->senders<11)strcpy(h->sender[h->senders++],name);
        }
        if(n->ref==111&&n->role==WF_TEXT&&value&&strlen(value)<sizeof h->body)
            strcpy(h->body,value);
    }
}
static void wfe_add(WFECatalog *cat,unsigned *at,const char *s,size_t len,unsigned kind,unsigned source) {
    if(!s||!len||len>159||*at>=WFE_CANDIDATES)return;
    while(len&&isspace((unsigned char)*s)){s++;len--;}
    while(len&&isspace((unsigned char)s[len-1]))len--;
    if(!len||len>159)return;
    for(size_t i=0;i<len;i++)if((unsigned char)s[i]<32||(unsigned char)s[i]>126)return;
    for(unsigned i=0;i<*at;i++)if(cat->items[i].length==len&&!memcmp(cat->items[i].text,s,len))return;
    WFECandidate *c=cat->items+(*at)++;
    memcpy(c->text,s,len);c->text[len]=0;c->length=(unsigned)len;c->kind=kind;c->source=source;
}
/* All candidates are literal public spans. These heuristics only propose
 * choices; they neither choose an operation nor supply a private answer. */
static void wfe_catalog(const WFView *v,const WFEHistory *h,WFECatalog *cat) {
    memset(cat,0,sizeof *cat);
    unsigned at=0;
    const char *q=wf_text_get(v,v->instruction);
    if(q){
        for(const char *p=q;*p&&at<8;p++)if(*p=='"'){
            const char *end=strchr(p+1,'"');if(!end)break;
            wfe_add(cat,&at,p+1,(size_t)(end-p-1),1,0);p=end;
        }
        const char *keys[]={" to "," from "," by "," for "," saying "," with "," at "};
        for(unsigned k=0;k<sizeof keys/sizeof *keys;k++){
            const char *p=q;
            while((p=wfe_find(p,keys[k]))&&at<20){
                p+=strlen(keys[k]);const char *end=p;
                while(*end&&*end!='.'&&*end!=','&&*end!='"'&&*end!=';'&&*end!='?')end++;
                size_t n=(size_t)(end-p);
                if(n>=7&&!strncasecmp(p+n-7," please",7))n-=7;
                wfe_add(cat,&at,p,n,2+k,0);
            }
        }
        for(const char *p=q;*p&&at<24;p++)if(*p=='\''&&p[1]=='s'){
            const char *start=p;
            while(start>q&&isalpha((unsigned char)start[-1]))start--;
            wfe_add(cat,&at,start,(size_t)(p-start),10,0);
        }
    }
    for(unsigned i=0;i<h->senders&&at<WFE_CANDIDATES;i++)
        wfe_add(cat,&at,h->sender[i],strlen(h->sender[i]),11,i+1);
    if(*h->body&&at<WFE_CANDIDATES)
        wfe_add(cat,&at,h->body,strlen(h->body),12,0);
    for(unsigned i=0;i<v->count&&at<WFE_CANDIDATES;i++){
        const WFNode *n=v->nodes+i;const char *s=wf_text_get(v,n->value);
        if((n->role==WF_TEXT||n->role==WF_BUTTON)&&s&&*s)
            wfe_add(cat,&at,s,strlen(s),13,i+1);
    }
}
static const WFNode *wfe_focus(const WFView *v) {
    for(unsigned i=0;i<v->count;i++)if((v->nodes[i].flags&WF_FOCUSED)&&
        (v->nodes[i].role==WF_INPUT||v->nodes[i].role==WF_TEXTAREA))return v->nodes+i;
    return NULL;
}
static void wfe_project(const WFView *v,const WFEHistory *h,const WFECatalog *cat,
                        float *out,unsigned char *mask) {
    memset(out,0,OBS_SIZE*sizeof *out);memset(mask,0,WFE_ACTIONS);mask[0]=1;
    const char *q=wf_text_get(v,v->instruction);
    const WFNode *f=wfe_focus(v);
    const char *current=f?wf_text_get(v,f->value):NULL;
    unsigned len=current?(unsigned)strlen(current):0;
    unsigned cap=f&&f->capacity?f->capacity-1:0;
    unsigned selected=f&&len&&h->selected_ref==f->ref;
    out[0]=(float)v->count/WFE_NODES;
    out[1]=v->deadline_ms?(float)v->elapsed_ms/v->deadline_ms:0;
    out[2]=f!=NULL;out[3]=f?(float)f->ref/128:0;
    out[4]=(float)len/159;out[5]=(float)cap/159;out[6]=selected;
    out[7]=(float)h->senders/11;out[8]=(float)h->clicks/120;
    out[9]=(float)h->edits/120;out[10]=(float)h->last_ref/128;
    out[11]=(float)h->last_kind/WF_SELECT_ALL;
    out[12]=wfe_contains(q,"reply");out[13]=wfe_contains(q,"forward");
    out[14]=wfe_contains(q,"delete")||wfe_contains(q,"trash");
    out[15]=wfe_contains(q,"important")||wfe_contains(q,"star");
    wfe_bytes(q,1024,out+32);
    for(unsigned i=0;i<v->count&&i<WFE_NODES;i++){
        const WFNode *n=v->nodes+i;
        const char *name=wf_text_get(v,n->name),*value=wf_text_get(v,n->value);
        float *d=out+WFE_PREFIX+i*WFE_NODE_FEATURES;
        wfe_bytes(name,32,d);wfe_bytes(value,40,d+32);
        d[72]=1;
        if(n->role<=WF_TEXTAREA)d[73+n->role]=1;
        d[90]=!!(n->flags&WF_VISIBLE);d[91]=!!(n->flags&WF_ENABLED);
        d[92]=!!(n->flags&WF_CLICKABLE);d[93]=!!(n->flags&WF_FOCUSED);
        d[94]=!!(n->flags&WF_CHECKED);d[95]=wfe_contains(q,name);
        d[96]=wfe_contains(q,value);d[97]=(float)n->ref/128;
        d[98]=(float)n->parent/128;
        d[99]=n->ref<256&&h->visited[n->ref];
        d[100]=n->parent<256&&h->visited[n->parent];
        d[101]=(float)(i+1)/WFE_NODES;
        d[102]=(float)n->capacity/160;
        d[103]=n->ref==h->last_ref;
        mask[WFE_CLICK_BASE+i]=(n->flags&(WF_VISIBLE|WF_ENABLED|WF_CLICKABLE))==
            (WF_VISIBLE|WF_ENABLED|WF_CLICKABLE);
    }
    if(f&&(f->flags&WF_ENABLED)){
        mask[WFE_SELECT]=len&&!selected;
        mask[WFE_BACKSPACE]=len>0;
    }
    for(unsigned i=0;i<WFE_CANDIDATES;i++){
        const WFECandidate *c=cat->items+i;
        if(!c->length)continue;
        float *d=out+WFE_PREFIX+WFE_NODES*WFE_NODE_FEATURES+i*WFE_CANDIDATE_FEATURES;
        wfe_bytes(c->text,160,d);
        d[160]=1;d[161]=(float)c->length/159;
        d[162]=(float)c->kind/16;d[163]=(float)c->source/40;
        d[164]=wfe_contains(q,c->text);
        d[165]=f&&c->length==len&&!memcmp(c->text,current,len);
        d[166]=f&&f->role==WF_INPUT;d[167]=f&&f->role==WF_TEXTAREA;
        d[168]=f&&f->ref==2;d[169]=f&&f->ref==117;
        d[170]=f&&f->ref==120;d[171]=f&&f->ref==121;
        mask[WFE_COPY_BASE+i]=f&&(f->flags&WF_ENABLED)&&
            (selected?c->length<=cap:len+c->length<=cap)&&!d[165];
    }
    if(f&&(f->flags&WF_ENABLED)&&(selected||len<cap))for(unsigned i=0;i<WFE_PRINTABLE;i++)
        mask[WFE_CHAR_BASE+i]=1;
}
static void wfe_observe(Env *env) {
    for(unsigned lane=0;lane<WFE_LANES;lane++){
        WFView v;WFECatalog cat;
        if(wf_observe(&env->family,lane,&v))abort();
        wfe_record(env->history+lane,&v);
        wfe_catalog(&v,env->history+lane,&cat);
        wfe_project(&v,env->history+lane,&cat,
            (float*)env->agents[lane].observations,env->agents[lane].action_mask);
    }
}
void puf_init(Env *env,Dict *kwargs) {
    env->num_agents=WFE_LANES;
    int mask=(int)dict_get(kwargs,"task_mask");
    if(mask<=0||mask>1023)abort();
    env->task_mask=(unsigned)mask;
    char error[256];
    if(wf_open(&env->family,"build/webnav/families/email/libemail.so",error,sizeof error)){
        fprintf(stderr,"WebNav email: %s\n",error);abort();
    }
    if(strcmp(env->family.api->family,"email")||env->family.api->batch_lanes!=WFE_LANES||
       env->family.api->task_count!=10)abort();
    for(unsigned lane=0;lane<WFE_LANES;lane++)env->agents[lane].policy=0;
}
#define MY_VEC_INIT
Env* my_vec_init(int *size,int *starts,int *counts,Dict *vk,Dict *ek) {
    int total=(int)dict_get(vk,"total_agents"),buffers=(int)dict_get(vk,"num_buffers");
    assert(buffers>0&&total>=WFE_LANES*buffers&&total%(WFE_LANES*buffers)==0);
    *size=total/WFE_LANES;int per_buffer=*size/buffers;
    Env *envs=(Env*)calloc(*size,sizeof *envs);if(!envs)abort();
    unsigned offset=(unsigned)dict_get(ek,"seed_offset");
    for(int b=0;b<buffers;b++){starts[b]=b*per_buffer;counts[b]=per_buffer;}
    for(int e=0;e<*size;e++){
        envs[e].rng=offset+(uint32_t)e*2654435761u;
        puf_init(envs+e,ek);
    }
    return envs;
}
void puf_reset(Env *env) {
    for(unsigned lane=0;lane<WFE_LANES;lane++)wfe_reset_lane(env,lane);
    if(wf_batch_checked(&env->family))abort();
    wfe_observe(env);
}
void puf_step(Env *env) {
    for(unsigned lane=0;lane<WFE_LANES;lane++){
        Agent *agent=env->agents+lane;WFView v;WFECatalog cat;
        WFEHistory *h=env->history+lane;
        if(wf_observe(&env->family,lane,&v))abort();
        wfe_record(h,&v);wfe_catalog(&v,h,&cat);
        int choice=(int)agent->actions[0];
        if(choice<0||choice>=WFE_ACTIONS||!agent->action_mask[choice])choice=0;
        WFAction a={.kind=WF_WAIT,.elapsed_ms=v.elapsed_ms+250};
        char literal[2]={0};
        if(choice>=WFE_CLICK_BASE&&choice<WFE_SELECT){
            unsigned i=(unsigned)(choice-WFE_CLICK_BASE);
            if(i<v.count){a.kind=WF_CLICK;a.target=v.nodes[i].ref;h->clicks++;
                if(a.target<256)h->visited[a.target]=1;h->last_ref=a.target;}
            h->selected_ref=0;
        }else if(choice==WFE_SELECT){
            a.kind=WF_SELECT_ALL;h->edits++;
            const WFNode *f=wfe_focus(&v);h->selected_ref=f?f->ref:0;
        }
        else if(choice==WFE_BACKSPACE){a.kind=WF_BACKSPACE;h->edits++;h->selected_ref=0;}
        else if(choice>=WFE_COPY_BASE&&choice<WFE_CHAR_BASE){
            const WFECandidate *c=cat.items+(choice-WFE_COPY_BASE);
            a.kind=WF_INSERT;a.text=c->text;a.text_length=c->length;h->edits++;h->selected_ref=0;
        }else if(choice>=WFE_CHAR_BASE){
            literal[0]=(char)(32+choice-WFE_CHAR_BASE);
            a.kind=WF_INSERT;a.text=literal;a.text_length=1;h->edits++;h->selected_ref=0;
        }
        if(a.elapsed_ms>v.deadline_ms)a.elapsed_ms=v.deadline_ms;
        h->last_kind=a.kind;
        if(wf_apply(&env->family,lane,&a))abort();
        agent->rewards[0]=0;agent->terminals[0]=0;env->steps[lane]++;
    }
    if(wf_batch_checked(&env->family))abort();
    int reset=0;
    for(unsigned lane=0;lane<WFE_LANES;lane++){
        uint32_t *r=env->family.words+(size_t)lane*env->family.api->row_words;
        if(r[WF_STATUS]!=WF_RUNNING){
            float raw;memcpy(&raw,r+WF_RAW_REWARD,sizeof raw);
            env->agents[lane].rewards[0]=raw;env->agents[lane].terminals[0]=1;
            env->log.perf+=raw>=0.999f;env->log.score+=raw;
            env->log.episode_length+=env->steps[lane];env->log.n++;
            wfe_reset_lane(env,lane);reset=1;
        }else r[WF_OP]=WF_OBSERVE;
    }
    if(reset&&wf_batch_checked(&env->family))abort();
    wfe_observe(env);
}
void puf_log(Log *log,Dict *out) {
    dict_set(out,"perf",log->perf);dict_set(out,"score",log->score);
    dict_set(out,"episode_length",log->episode_length);
}
void puf_render(Env *env){(void)env;}
void puf_close(Env *env){wf_close(&env->family);}
