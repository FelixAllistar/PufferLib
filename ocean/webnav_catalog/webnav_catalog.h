#pragma once
/* Catalog PPO adapter. Only WFView and this adapter's interaction history
 * enter observations. The loaded stock CPU Bend family owns task behavior. */
typedef float obs_t;
#include "pufferenv.h"
#include "../webnav/families/common/loader.h"
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <strings.h>

#define WFC_LANES 4
#define WFC_NODES 64
#define WFC_ACTIONS (1 + WFC_NODES + 2)
#define WFC_NODE_FEATURES 96
#define OBS_SIZE (16 + 192 + WFC_NODES * WFC_NODE_FEATURES)
#define ACT_SIZES {WFC_ACTIONS}
#define NUM_ATNS 1

struct Log {float perf,score,episode_length,n;};
struct Env {
    Log log;
    int num_agents,tag,boundary_reached;
    Agent agents[WFC_LANES];
    WFLoaded family;
    uint32_t rng;
    unsigned task_mask,steps[WFC_LANES];
    unsigned char visits[WFC_LANES][512];
};

static uint32_t wfc_random(Env *env){
    env->rng=env->rng*1664525u+1013904223u;
    uint32_t x=env->rng;x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;
    return x;
}
static unsigned wfc_task(Env *env){
    unsigned n=0;for(unsigned i=0;i<3;i++)n+=(env->task_mask>>i)&1u;
    unsigned rank=wfc_random(env)%n;
    for(unsigned i=0;i<3;i++)if((env->task_mask>>i)&1u)if(!rank--)return i;
    abort();
}
static void wfc_reset_lane(Env *env,unsigned lane){
    uint32_t *r=env->family.words+(size_t)lane*env->family.api->row_words;
    memset(r,0,env->family.api->row_words*sizeof *r);
    r[WF_VERSION]=WF_ABI_VERSION;r[WF_TASK]=wfc_task(env);
    r[WF_OP]=WF_RESET;r[WF_SEED]=wfc_random(env);
    env->steps[lane]=0;memset(env->visits[lane],0,sizeof env->visits[lane]);
}
static void wfc_bytes(const char *s,unsigned n,float *out){
    if(!s)return;
    for(unsigned i=0;i<n&&s[i];i++)out[i]=(unsigned char)s[i]/127.0f;
}
static int wfc_contains(const char *haystack,const char *needle){
    if(!haystack||!needle||!*needle)return 0;
    size_t n=strlen(needle);
    for(const char *p=haystack;*p;p++)if(!strncasecmp(p,needle,n))return 1;
    return 0;
}
static unsigned wfc_ordinal(const char *q){
    if(!q)return 0;
    const char *p=strstr(q,"click the ");if(!p)return 0;
    p+=10;if(!isdigit((unsigned char)*p))return 0;
    unsigned n=0;while(isdigit((unsigned char)*p)&&n<100){n=10*n+(*p++-'0');}
    return n>=1&&n<=9?n:0;
}
static size_t wfc_quoted(const char *q,const char **begin){
    if(!q)return 0;
    const char *a=strchr(q,'"');if(!a)return 0;
    const char *b=strchr(a+1,'"');if(!b)return 0;
    *begin=a+1;return (size_t)(b-a-1);
}
static int wfc_phone_match(const char *q,const char *name){
    if(!q||!name||strncmp(q,"Find ",5))return 0;
    const char *end=strstr(q+5," in the contact book");
    return end&&(size_t)(end-(q+5))==strlen(name)&&!strncmp(q+5,name,(size_t)(end-(q+5)));
}
static int wfc_food_name_match(const char *q,const char *name){
    if(!q||!name||strncmp(q,"Order one of each item: ",24))return 0;
    const char *p=q+24;size_t n=strlen(name);
    while(*p){
        if(!strncmp(p,name,n)&&(p[n]==','||p[n]=='\0'))return 1;
        p=strstr(p,", ");if(!p)break;p+=2;
    }
    return 0;
}
static int wfc_type_match(const char *q,const char *name){
    const char *p=q?strstr(q," items that are "):NULL;
    return p&&name&&!strcmp(p+16,name);
}
static unsigned wfc_page(const WFView *v){
    for(unsigned i=0;i<v->count;i++){
        const WFNode *n=v->nodes+i;
        if((n->flags&WF_SELECTED)&&n->role==WF_BUTTON){
            const char *s=wf_text_get(v,n->name);
            if(s&&!strncmp(s,"Page ",5)&&s[5]>='1'&&s[5]<='5')return (unsigned)(s[5]-'0');
        }
    }
    return 0;
}
static unsigned wfc_quantity(const WFView *v,unsigned parent){
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].ref==parent){
        const char *s=wf_text_get(v,v->nodes[i].value);
        if(s&&isdigit((unsigned char)*s))return (unsigned)strtoul(s,NULL,10);
    }
    return 0;
}
static int wfc_focus(const WFView *v){
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].role==WF_INPUT&&
       (v->nodes[i].flags&WF_FOCUSED))return (int)i;
    return -1;
}
static int wfc_query_matches_input(const WFView *v,const char *q){
    const char *quoted=NULL;size_t n=wfc_quoted(q,&quoted);
    if(!n)return 0;
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].role==WF_INPUT){
        const char *s=wf_text_get(v,v->nodes[i].value);
        if(!s||strlen(s)!=n)return 0;
        return !strncasecmp(s,quoted,n);
    }
    return 0;
}
/* The numeric page and quantity fields are values read from visible widgets.
 * A result's displayed ordinal is preserved even when its title duplicates
 * another title. A fake result never receives a real ordinal. */
static void wfc_project(const WFView *v,const unsigned char visits[512],
                        float out[OBS_SIZE],unsigned char mask[WFC_ACTIONS]){
    memset(out,0,OBS_SIZE*sizeof *out);memset(mask,0,WFC_ACTIONS);mask[0]=1;
    const char *q=wf_text_get(v,v->instruction);
    unsigned page=wfc_page(v),ordinal=wfc_ordinal(q);
    int focus=wfc_focus(v);const char *quoted=NULL;
    size_t quoted_len=wfc_quoted(q,&quoted);
    out[0]=(float)v->count/WFC_NODES;
    out[1]=v->deadline_ms?(float)v->elapsed_ms/v->deadline_ms:0;
    out[2]=(float)page/5;out[3]=(float)ordinal/9;
    out[4]=focus>=0;out[5]=wfc_query_matches_input(v,q);
    out[6]=q&&!strncmp(q,"Find ",5);out[7]=q&&!strncmp(q,"Order ",6);
    out[8]=q&&strstr(q," items that are ")!=NULL;
    out[9]=q&&strstr(q,"phone number")!=NULL;
    out[10]=q&&strstr(q,"their email")!=NULL;
    out[11]=q&&strstr(q,"their address")!=NULL;
    out[12]=(float)v->omitted;out[13]=(float)v->text_truncated;
    if(q&&!strncmp(q,"Order ",6)&&isdigit((unsigned char)q[6]))
        out[14]=(float)strtoul(q+6,NULL,10)/4;
    wfc_bytes(q,192,out+16);
    for(unsigned i=0;i<v->count&&i<WFC_NODES;i++){
        const WFNode *n=v->nodes+i;
        const char *name=wf_text_get(v,n->name),*value=wf_text_get(v,n->value);
        float *d=out+16+192+i*WFC_NODE_FEATURES;
        wfc_bytes(name,32,d);wfc_bytes(value,16,d+32);
        if(n->role<=WF_TEXTAREA)d[48+n->role]=1;
        d[65]=(float)n->ref/512;d[66]=(float)n->parent/512;
        d[67]=(float)(i+1)/WFC_NODES;
        d[68]=!!(n->flags&WF_SELECTED);d[69]=!!(n->flags&WF_FOCUSED);
        d[70]=!!(n->flags&WF_ENABLED);d[71]=!!(n->flags&WF_CLICKABLE);
        d[72]=n->ref<512&&visits?fminf(visits[n->ref],4)/4.0f:0;
        d[73]=wfc_contains(q,name);d[74]=wfc_contains(q,value);
        d[75]=n->role==WF_TEXT&&n->ref==99&&wfc_phone_match(q,name);
        d[76]=n->role==WF_TEXT&&wfc_food_name_match(q,name);
        d[77]=wfc_type_match(q,name);
        d[78]=n->role==WF_TEXT&&n->ref>=18&&n->ref<=194?
              (float)wfc_quantity(v,n->ref)/8:0;
        d[79]=n->role==WF_TEXT&&n->ref>=18&&n->ref<=194&&value?
              (float)strtoul(value,NULL,10)/8:0;
        if(n->role==WF_TEXT&&n->ref>=18&&n->ref<=194&&value)
            out[15]+=(float)strtoul(value,NULL,10)/8;
        d[80]=n->role==WF_BUTTON&&name&&!strncmp(name,"Page ",5)?
              (float)strtoul(name+5,NULL,10)/5:0;
        d[81]=n->role==WF_LINK&&n->ref>=100&&n->ref<=108?
              (float)(n->ref-99)/9:0;
        d[82]=n->role==WF_LINK&&n->ref>=100&&n->ref<=108&&
              ordinal==n->ref-99;
        d[83]=n->role==WF_LINK&&n->ref>=200&&n->ref<=202;
        d[84]=n->role==WF_INPUT&&value&&quoted_len==strlen(value)&&
              !strncasecmp(value,quoted?quoted:"",quoted_len);
        d[85]=n->role==WF_INPUT&&value?(float)strlen(value)/128:0;
        d[86]=n->role==WF_INPUT&&value&&n->selection_start==0&&
              n->selection_end==strlen(value)&&*value;
        d[87]=n->role==WF_INPUT?(float)n->selection_start/128:0;
        d[88]=n->role==WF_INPUT?(float)n->selection_end/128:0;
        d[89]=n->role==WF_BUTTON&&name&&!strcmp(name,"Add");
        d[90]=n->role==WF_BUTTON&&name&&!strcmp(name,"Remove");
        d[91]=n->role==WF_BUTTON&&name&&!strcmp(name,"Order!");
        d[92]=n->role==WF_BUTTON&&name&&!strcmp(name,"Search");
        d[93]=n->parent?fminf(wfc_quantity(v,n->parent),8)/8.0f:0;
        d[94]=n->role==WF_LINK&&n->ref>=100&&n->ref<=102;
        d[95]=n->role==WF_BUTTON&&name&&!strncmp(name,"Page ",5)&&
              !(n->flags&WF_SELECTED);
        mask[i+1]=(n->flags&(WF_VISIBLE|WF_ENABLED|WF_CLICKABLE))==
                  (WF_VISIBLE|WF_ENABLED|WF_CLICKABLE);
    }
    if(focus>=0){
        const WFNode *input=v->nodes+focus;const char *value=wf_text_get(v,input->value);
        if(value){
            mask[1+WFC_NODES]=*value&&!(input->selection_start==0&&
                                           input->selection_end==strlen(value));
            size_t effective=strlen(value)-(input->selection_end-input->selection_start);
            mask[2+WFC_NODES]=quoted_len>0&&quoted_len<128&&
                effective+quoted_len<128&&
                !(quoted_len==strlen(value)&&!strncasecmp(value,quoted,quoted_len));
        }
    }
}
static void wfc_observe(Env *env){
    for(unsigned lane=0;lane<WFC_LANES;lane++){
        WFView v;if(wf_observe(&env->family,lane,&v))abort();
        wfc_project(&v,env->visits[lane],
                    (float*)env->agents[lane].observations,
                    env->agents[lane].action_mask);
    }
}
static WFAction wfc_action(const WFView *v,unsigned choice){
    WFAction a={.kind=WF_WAIT,.elapsed_ms=v->elapsed_ms+250};
    if(a.elapsed_ms>v->deadline_ms)a.elapsed_ms=v->deadline_ms;
    if(choice>=1&&choice<=v->count&&choice<=WFC_NODES){
        a.kind=WF_CLICK;a.target=v->nodes[choice-1].ref;
    }else if(choice==1+WFC_NODES)a.kind=WF_SELECT_ALL;
    else if(choice==2+WFC_NODES){
        const char *q=wf_text_get(v,v->instruction),*start=NULL;
        size_t n=wfc_quoted(q,&start);
        if(start&&n){a.kind=WF_INSERT;a.text=start;a.text_length=n;}
    }
    return a;
}
void puf_init(Env *env,Dict *kwargs){
    env->num_agents=WFC_LANES;
    int mask=(int)dict_get(kwargs,"task_mask");if(mask<1||mask>7)abort();
    env->task_mask=(unsigned)mask;
    char error[256];
    if(wf_open(&env->family,"build/webnav/families/catalog/libcatalog.so",error,sizeof error)){
        fprintf(stderr,"WebNav catalog: %s\n",error);abort();
    }
    if(strcmp(env->family.api->family,"catalog")||
       env->family.api->batch_lanes!=WFC_LANES||
       env->family.api->task_count!=3)abort();
    for(unsigned lane=0;lane<WFC_LANES;lane++)env->agents[lane].policy=0;
}
#define MY_VEC_INIT
Env* my_vec_init(int *size,int *starts,int *counts,Dict *vk,Dict *ek){
    int total=(int)dict_get(vk,"total_agents"),buffers=(int)dict_get(vk,"num_buffers");
    assert(buffers>0&&total>=WFC_LANES*buffers&&total%(WFC_LANES*buffers)==0);
    *size=total/WFC_LANES;int per_buffer=*size/buffers;
    Env *envs=(Env*)calloc((size_t)*size,sizeof *envs);if(!envs)abort();
    unsigned offset=(unsigned)dict_get(ek,"seed_offset");
    for(int b=0;b<buffers;b++){starts[b]=b*per_buffer;counts[b]=per_buffer;}
    for(int e=0;e<*size;e++){
        envs[e].rng=offset+(uint32_t)e*2654435761u;
        puf_init(envs+e,ek);
    }
    return envs;
}
void puf_reset(Env *env){
    for(unsigned lane=0;lane<WFC_LANES;lane++)wfc_reset_lane(env,lane);
    if(wf_batch_checked(&env->family))abort();wfc_observe(env);
}
void puf_step(Env *env){
    for(unsigned lane=0;lane<WFC_LANES;lane++){
        WFView v;if(wf_observe(&env->family,lane,&v))abort();
        Agent *agent=env->agents+lane;int choice=(int)agent->actions[0];
        if(choice<0||choice>=WFC_ACTIONS||!agent->action_mask[choice]||
           (choice>0&&choice<=WFC_NODES&&(unsigned)choice>v.count))choice=0;
        WFAction a=wfc_action(&v,(unsigned)choice);
        if(a.target&&a.target<512&&env->visits[lane][a.target]<255)
            env->visits[lane][a.target]++;
        if(wf_apply(&env->family,lane,&a))abort();
        agent->rewards[0]=0;agent->terminals[0]=0;env->steps[lane]++;
    }
    if(wf_batch_checked(&env->family))abort();
    int reset=0;
    for(unsigned lane=0;lane<WFC_LANES;lane++){
        uint32_t *r=env->family.words+(size_t)lane*env->family.api->row_words;
        if(r[WF_STATUS]!=WF_RUNNING){
            float raw;memcpy(&raw,r+WF_RAW_REWARD,sizeof raw);
            env->agents[lane].rewards[0]=raw;
            env->agents[lane].terminals[0]=1;
            env->log.perf+=raw>=0.999f;env->log.score+=raw;
            env->log.episode_length+=env->steps[lane];env->log.n++;
            wfc_reset_lane(env,lane);reset=1;
        }else r[WF_OP]=WF_OBSERVE;
    }
    if(reset&&wf_batch_checked(&env->family))abort();
    wfc_observe(env);
}
void puf_log(Log *log,Dict *out){
    dict_set(out,"perf",log->perf);dict_set(out,"score",log->score);
    dict_set(out,"episode_length",log->episode_length);
}
void puf_render(Env *env){(void)env;}
void puf_close(Env *env){wf_close(&env->family);}
