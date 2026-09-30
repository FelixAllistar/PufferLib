#pragma once
/* Public numeric-family PPO transport. Task dynamics remain in the Bend DSO. */
typedef float obs_t;
#include "pufferenv.h"
#include "../webnav/families/common/loader.h"
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WNN_LANES 8u
#define WNN_NODES 29u
#define WNN_NODE_FEATURES 48u
#define WNN_GLOBAL_FEATURES 128u
#define OBS_SIZE (WNN_GLOBAL_FEATURES + WNN_NODES * WNN_NODE_FEATURES)
#define WNN_NUMBER_MIN (-99)
#define WNN_NUMBER_MAX 108
#define WNN_CLICK 1u
#define WNN_SELECT_ALL (WNN_CLICK + WNN_NODES)
#define WNN_BACKSPACE (WNN_SELECT_ALL + 1u)
#define WNN_DELETE (WNN_BACKSPACE + 1u)
#define WNN_LEFT (WNN_DELETE + 1u)
#define WNN_RIGHT (WNN_LEFT + 1u)
#define WNN_HOME (WNN_RIGHT + 1u)
#define WNN_END (WNN_HOME + 1u)
#define WNN_ENTER (WNN_END + 1u)
#define WNN_NUMBER (WNN_ENTER + 1u)
#define WNN_NUMBER_COUNT ((unsigned)(WNN_NUMBER_MAX-WNN_NUMBER_MIN+1))
#define WNN_LITERAL (WNN_NUMBER + WNN_NUMBER_COUNT)
#define WNN_LITERAL_COUNT 12u /* 0..9, minus, decimal point */
#define WNN_SET_X (WNN_LITERAL + WNN_LITERAL_COUNT)
#define WNN_SET_Y (WNN_SET_X + 155u)
#define WNN_MOVE (WNN_SET_Y + 126u)
#define WNN_HOT_CLICK (WNN_MOVE + 1u)
#define WNN_ACTIONS (WNN_HOT_CLICK + 1u)
#define ACT_SIZES {WNN_ACTIONS}
#define NUM_ATNS 1

struct Log { float perf, score, episode_length, n; };
struct Env {
    Log log;
    int num_agents;
    int tag, boundary_reached;
    Agent agents[WNN_LANES];
    WFLoaded family;
    uint32_t rng;
    unsigned task_mask, steps[WNN_LANES];
    unsigned char visited[WNN_LANES][WNN_NODES+1];
    unsigned char known[WNN_LANES][WNN_NODES+1];
    float remembered[WNN_LANES][WNN_NODES+1];
    unsigned x[WNN_LANES], y[WNN_LANES];
    unsigned last_x[WNN_LANES], last_y[WNN_LANES];
    unsigned probes[WNN_LANES], clicks[WNN_LANES];
};

static uint32_t wnn_random(Env *env) {
    env->rng=env->rng*1664525u+1013904223u;
    uint32_t x=env->rng;x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;
    return x;
}
static unsigned wnn_task(Env *env) {
    unsigned n=0;for(unsigned i=0;i<9;i++)n+=(env->task_mask>>i)&1u;
    unsigned rank=wnn_random(env)%n;
    for(unsigned i=0;i<9;i++)if((env->task_mask>>i)&1u)if(!rank--)return i;
    abort();
}
static void wnn_reset_lane(Env *env,unsigned lane) {
    uint32_t *r=env->family.words+(size_t)lane*env->family.api->row_words;
    memset(r,0,env->family.api->row_words*sizeof *r);
    r[WF_VERSION]=WF_ABI_VERSION;r[WF_TASK]=wnn_task(env);
    r[WF_OP]=WF_RESET;r[WF_SEED]=wnn_random(env);
    env->steps[lane]=env->probes[lane]=env->clicks[lane]=0;
    env->x[lane]=env->last_x[lane]=77;
    env->y[lane]=env->last_y[lane]=62;
    memset(env->visited[lane],0,sizeof env->visited[lane]);
    memset(env->known[lane],0,sizeof env->known[lane]);
    memset(env->remembered[lane],0,sizeof env->remembered[lane]);
}
/* Numeric tokens are public text features. This parser never evaluates an
 * expression or constructs a candidate action from a per-episode answer. */
static unsigned wnn_numbers(const char *s,float *out,unsigned cap) {
    if(!s)return 0;
    unsigned n=0;
    for(const char *p=s;*p&&n<cap;) {
        if(isdigit((unsigned char)*p) || (*p=='-'&&isdigit((unsigned char)p[1])&&
           (p==s||!isalnum((unsigned char)p[-1])))) {
            char *end;long value=strtol(p,&end,10);
            out[n++]=fmaxf(-1000.0f,fminf(1000.0f,(float)value))/100.0f;p=end;
        } else p++;
    }
    return n;
}
static int wnn_single_number(const char *s,float *out) {
    if(!s||!*s)return 0;
    char *end;long value=strtol(s,&end,10);
    if(end==s||*end||value<-100000||value>100000)return 0;
    *out=fmaxf(-1000.0f,fminf(1000.0f,(float)value))/100.0f;return 1;
}
static void wnn_chars(const char *s,float *out,unsigned bins,float scale) {
    if(!s)return;
    for(const unsigned char *p=(const unsigned char*)s;*p;p++)if(*p>=32&&*p<128)
        out[*p-32]+=scale;
    (void)bins;
}
static unsigned wnn_canvas(const WFView *v) {
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].role==WF_CANVAS&&
       (v->nodes[i].flags&(WF_VISIBLE|WF_ENABLED|WF_CLICKABLE))==
       (WF_VISIBLE|WF_ENABLED|WF_CLICKABLE))return v->nodes[i].ref;
    return 0;
}
static const WFNode *wnn_input(const WFView *v) {
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].role==WF_INPUT&&
       (v->nodes[i].flags&(WF_VISIBLE|WF_ENABLED))==(WF_VISIBLE|WF_ENABLED))return v->nodes+i;
    return NULL;
}
static void wnn_project(const WFView *v,Env *env,unsigned lane,float *out,unsigned char *mask) {
    memset(out,0,OBS_SIZE*sizeof *out);
    memset(mask,0,WNN_ACTIONS);mask[0]=1;
    const char *q=wf_text_get(v,v->instruction);
    out[0]=(float)v->elapsed_ms/v->deadline_ms;
    out[1]=(float)v->count/WNN_NODES;
    out[2]=(float)v->omitted;out[3]=(float)v->text_truncated;
    wnn_chars(q,out+4,96,1.0f/32.0f);
    wnn_numbers(q,out+100,4);
    out[116]=(float)env->x[lane]/154.0f;out[117]=(float)env->y[lane]/125.0f;
    out[118]=(float)env->last_x[lane]/154.0f;out[119]=(float)env->last_y[lane]/125.0f;
    out[120]=(float)env->probes[lane]/60.0f;out[121]=(float)env->clicks[lane]/60.0f;
    unsigned canvas=wnn_canvas(v);
    const WFNode *input=wnn_input(v);
    out[122]=canvas!=0;out[123]=input!=NULL;
    unsigned current_length=0;
    if(input){const char *s=wf_text_get(v,input->value);current_length=s?(unsigned)strlen(s):0;}
    for(unsigned i=0;i<v->count&&i<WNN_NODES;i++) {
        const WFNode *n=v->nodes+i;
        const char *name=wf_text_get(v,n->name),*value=wf_text_get(v,n->value);
        float *d=out+WNN_GLOBAL_FEATURES+i*WNN_NODE_FEATURES;
        d[0]=1;if(n->role<=WF_TEXTAREA)d[1+n->role]=1;
        d[18]=!!(n->flags&WF_VISIBLE);d[19]=!!(n->flags&WF_ENABLED);
        d[20]=!!(n->flags&WF_CLICKABLE);d[21]=!!(n->flags&WF_CHECKED);
        d[22]=!!(n->flags&WF_SELECTED);d[23]=!!(n->flags&WF_FOCUSED);
        d[24]=wnn_single_number(value,&d[25]);
        float vals[2]={0};if(wnn_numbers(name,vals,2)){d[26]=1;d[27]=vals[0];}
        if(n->ref<=WNN_NODES) {
            if(n->role==WF_BUTTON&&d[24]){env->known[lane][n->ref]=1;env->remembered[lane][n->ref]=d[25];}
            d[28]=env->known[lane][n->ref];d[29]=env->remembered[lane][n->ref];
            d[30]=(float)env->visited[lane][n->ref]/8.0f;
        }
        d[31]=(float)n->ref/WNN_NODES;
        d[32]=(float)n->selection_start/23.0f;
        d[33]=(float)n->selection_end/23.0f;
        d[34]=(float)n->capacity/23.0f;
        d[35]=(float)(value?strlen(value):0)/23.0f;
        /* Ordered text is retained through stable character positions; the
         * small public vocabulary also exposes the actual displayed numbers. */
        for(unsigned k=0;k<8&&name&&name[k];k++)d[36+k]=(unsigned char)name[k]/127.0f;
        d[44]=n->x/200.0f;d[45]=n->y/200.0f;
        d[46]=n->width/200.0f;d[47]=n->height/200.0f;
        if(n->role==WF_TEXT&&name&&!strcmp(name,"Problem")) {
            wnn_numbers(value,out+104,4);
            if(value){
                out[108]=strchr(value,'+')!=NULL;
                out[109]=strchr(value,'-')!=NULL;
                out[110]=strchr(value,'x')!=NULL;
                out[111]=value[0]=='x';
            }
        }
        if(n->role==WF_TEXT&&name&&!strcmp(name,"Feedback"))wnn_numbers(value,out+124,2);
        if(n->role==WF_TEXT&&name&&!strcmp(name,"Temperature")&&value) {
            out[112]=!strcmp(value,"HOT");out[113]=!strcmp(value,"WARM");
            out[114]=!strcmp(value,"COLD");out[115]=!strcmp(value,"ICE COLD");
        }
        if(n->role==WF_BUTTON||n->role==WF_CHECKBOX||n->role==WF_INPUT||
           (n->role==WF_TEXT&&canvas))
            mask[WNN_CLICK+i]=(n->flags&(WF_VISIBLE|WF_ENABLED|WF_CLICKABLE))==
                                  (WF_VISIBLE|WF_ENABLED|WF_CLICKABLE);
    }
    if(input) {
        unsigned cap=input->capacity;
        if(cap>23)cap=23;
        /* The checked transport bounds insertion by current length before
         * replacement. Select-all then backspace clears a full field. */
        unsigned room=current_length<cap?cap-current_length:0;
        mask[WNN_SELECT_ALL]=1;mask[WNN_BACKSPACE]=1;mask[WNN_DELETE]=1;
        mask[WNN_LEFT]=1;mask[WNN_RIGHT]=1;mask[WNN_HOME]=1;mask[WNN_END]=1;
        if(q&&strstr(q,"Guess the number"))mask[WNN_ENTER]=1;
        for(unsigned i=0;i<WNN_NUMBER_COUNT;i++){
            char s[16];int len=snprintf(s,sizeof s,"%d",WNN_NUMBER_MIN+(int)i);
            mask[WNN_NUMBER+i]=len>0&&(unsigned)len<=room;
        }
        for(unsigned i=0;i<WNN_LITERAL_COUNT;i++)mask[WNN_LITERAL+i]=room>=1;
    }
    if(canvas){
        memset(mask+WNN_SET_X,1,155);
        memset(mask+WNN_SET_Y,1,126);
        mask[WNN_MOVE]=1;mask[WNN_HOT_CLICK]=1;
    }
}
static void wnn_observe(Env *env) {
    for(unsigned i=0;i<WNN_LANES;i++) {
        WFView v;if(wf_observe(&env->family,i,&v))abort();
        wnn_project(&v,env,i,(float*)env->agents[i].observations,
                    env->agents[i].action_mask);
    }
}
static WFAction wnn_action(Env *env,unsigned lane,const WFView *v,unsigned choice,
                            char text[16]) {
    WFAction a={0};a.kind=WF_WAIT;
    a.elapsed_ms=v->elapsed_ms+250u;
    if(a.elapsed_ms>v->deadline_ms)a.elapsed_ms=v->deadline_ms;
    if(choice>=WNN_CLICK&&choice<WNN_CLICK+v->count&&choice<WNN_CLICK+WNN_NODES) {
        const WFNode *n=v->nodes+choice-WNN_CLICK;
        a.kind=WF_CLICK;a.target=n->ref;
        if(n->role==WF_CANVAS){a.arg0=env->x[lane];a.arg1=env->y[lane];}
        if(n->ref<=WNN_NODES&&env->visited[lane][n->ref]<255)env->visited[lane][n->ref]++;
        env->clicks[lane]++;
    }else if(choice>=WNN_SELECT_ALL&&choice<=WNN_ENTER) {
        static const unsigned kinds[]={WF_SELECT_ALL,WF_BACKSPACE,WF_DELETE,WF_LEFT,WF_RIGHT,WF_HOME,WF_END,WF_ENTER};
        const WFNode *n=wnn_input(v);if(n){a.kind=kinds[choice-WNN_SELECT_ALL];a.target=n->ref;}
    }else if(choice>=WNN_NUMBER&&choice<WNN_LITERAL) {
        const WFNode *n=wnn_input(v);if(n){
            int len=snprintf(text,16,"%d",WNN_NUMBER_MIN+(int)(choice-WNN_NUMBER));
            a.kind=WF_INSERT;a.target=n->ref;a.text=text;a.text_length=(size_t)len;
        }
    }else if(choice>=WNN_LITERAL&&choice<WNN_SET_X) {
        const WFNode *n=wnn_input(v);if(n){
            unsigned k=choice-WNN_LITERAL;
            text[0]=k<10?(char)('0'+k):k==10?'-':'.';text[1]=0;
            a.kind=WF_INSERT;a.target=n->ref;a.text=text;a.text_length=1;
        }
    }else if(choice>=WNN_SET_X&&choice<WNN_SET_Y)env->x[lane]=choice-WNN_SET_X;
    else if(choice>=WNN_SET_Y&&choice<WNN_MOVE)env->y[lane]=choice-WNN_SET_Y;
    else if(choice==WNN_MOVE||choice==WNN_HOT_CLICK) {
        unsigned ref=wnn_canvas(v);
        if(ref){a.kind=choice==WNN_MOVE?WF_POINTER_MOVE:WF_CLICK;
            a.target=ref;a.arg0=env->x[lane];a.arg1=env->y[lane];
            env->last_x[lane]=env->x[lane];env->last_y[lane]=env->y[lane];
            env->probes[lane]++;
            if(choice==WNN_HOT_CLICK)env->clicks[lane]++;
        }
    }
    return a;
}
void puf_init(Env *env,Dict *kwargs) {
    env->num_agents=WNN_LANES;
    int mask=(int)dict_get(kwargs,"task_mask");
    if(mask<=0||mask>511)abort();env->task_mask=(unsigned)mask;
    char error[256];
    if(wf_open(&env->family,"build/webnav/families/numeric/libnumeric.so",error,sizeof error)){
        fprintf(stderr,"WebNav numeric: %s\n",error);abort();
    }
    if(env->family.api->batch_lanes!=WNN_LANES||env->family.api->task_count!=9||
       strcmp(env->family.api->family,"numeric"))abort();
    for(unsigned i=0;i<WNN_LANES;i++)env->agents[i].policy=0;
}
#define MY_VEC_INIT
Env* my_vec_init(int *size,int *starts,int *counts,Dict *vk,Dict *ek) {
    int total=(int)dict_get(vk,"total_agents"),buffers=(int)dict_get(vk,"num_buffers");
    assert(buffers>0&&total>0&&total%(WNN_LANES*buffers)==0);
    *size=total/WNN_LANES;
    int per_buffer=*size/buffers;
    Env *envs=(Env*)calloc((size_t)*size,sizeof *envs);if(!envs)abort();
    unsigned offset=(unsigned)dict_get(ek,"seed_offset");
    for(int b=0;b<buffers;b++){starts[b]=b*per_buffer;counts[b]=per_buffer;}
    for(int e=0;e<*size;e++){envs[e].rng=offset+(uint32_t)e*2654435761u;puf_init(envs+e,ek);}
    return envs;
}
void puf_reset(Env *env) {
    for(unsigned i=0;i<WNN_LANES;i++)wnn_reset_lane(env,i);
    if(wf_batch_checked(&env->family))abort();wnn_observe(env);
}
void puf_step(Env *env) {
    for(unsigned i=0;i<WNN_LANES;i++) {
        Agent *agent=env->agents+i;WFView v;
        if(wf_observe(&env->family,i,&v))abort();
        int choice=(int)agent->actions[0];
        if(choice<0||choice>=(int)WNN_ACTIONS||!agent->action_mask[choice])choice=0;
        char text[16]={0};WFAction a=wnn_action(env,i,&v,(unsigned)choice,text);
        if(wf_apply(&env->family,i,&a)){fprintf(stderr,"numeric rejected task=%u lane=%u choice=%d kind=%u ref=%u len=%zu\n",env->family.words[(size_t)i*env->family.api->row_words+WF_TASK],i,choice,a.kind,a.target,a.text_length);abort();}
        agent->rewards[0]=0;agent->terminals[0]=0;env->steps[i]++;
    }
    if(wf_batch_checked(&env->family))abort();
    int any_reset=0;
    for(unsigned i=0;i<WNN_LANES;i++) {
        uint32_t *r=env->family.words+(size_t)i*env->family.api->row_words;
        if(r[WF_STATUS]!=WF_RUNNING){
            float raw;memcpy(&raw,r+WF_RAW_REWARD,sizeof raw);
            Agent *agent=env->agents+i;
            agent->rewards[0]=raw;agent->terminals[0]=1;
            env->log.perf+=raw>=0.999f;env->log.score+=raw;
            env->log.episode_length+=env->steps[i];env->log.n++;
            wnn_reset_lane(env,i);any_reset=1;
        } else r[WF_OP]=WF_OBSERVE;
    }
    if(any_reset&&wf_batch_checked(&env->family))abort();
    wnn_observe(env);
}
void puf_log(Log *log,Dict *out) {
    dict_set(out,"perf",log->perf);dict_set(out,"score",log->score);
    dict_set(out,"episode_length",log->episode_length);
}
void puf_render(Env *env){(void)env;}
void puf_close(Env *env){wf_close(&env->family);}
