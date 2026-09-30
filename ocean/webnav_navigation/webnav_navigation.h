#pragma once
/* Shared panels/menus learner: stock CPU Bend owns generation, transitions
 * and rewards. C projects only public observations and interaction history. */
typedef float obs_t;
#include "pufferenv.h"
#include "../webnav/families/common/loader.h"
#include <assert.h>
#include <math.h>
#include <strings.h>

#define WFN_LANES 4
#define WFN_NODES 128
#define WFN_ACTIONS (1 + WFN_NODES)
#define WFN_NODE_FEATURES 32
#define OBS_SIZE (8 + WFN_NODES * WFN_NODE_FEATURES)
#define ACT_SIZES {WFN_ACTIONS}
#define NUM_ATNS 1

struct Log { float perf, score, episode_length, n; };
struct Env {
    Log log;
    int num_agents;
    int tag, boundary_reached;
    Agent agents[WFN_LANES];
    WFLoaded family;
    uint32_t rng;
    unsigned task_mask, family_id, task_count, steps[WFN_LANES];
    unsigned char visited[WFN_LANES][256];
};

static uint32_t wfn_random(Env *env) {
    env->rng = env->rng * 1664525u + 1013904223u;
    /* Do not use an LCG low bit for both task and mode selection: adjacent
     * draws would otherwise correlate task parity with data mode. */
    uint32_t x=env->rng;
    x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;
    return x;
}
static unsigned wfn_task(Env *env) {
    unsigned count=0;
    for(unsigned i=0;i<env->task_count;i++)count+=(env->task_mask>>i)&1u;
    unsigned rank=wfn_random(env)%count;
    for(unsigned i=0;i<env->task_count;i++)if((env->task_mask>>i)&1u) {
        if(!rank--)return i;
    }
    abort();
}
static void wfn_reset_lane(Env *env,unsigned lane) {
    uint32_t *r=env->family.words+(size_t)lane*env->family.api->row_words;
    memset(r,0,env->family.api->row_words*sizeof *r);
    r[WF_VERSION]=WF_ABI_VERSION;
    r[WF_TASK]=wfn_task(env);
    r[WF_OP]=WF_RESET;
    r[WF_SEED]=wfn_random(env);
    env->steps[lane]=0;
    memset(env->visited[lane],0,sizeof env->visited[lane]);
}
/* Exact text matching and history are deterministic public features, not
 * a pretrained semantic encoder. Geometry is excluded because the Bend view
 * uses ordinal coordinates whereas original pages have pixel coordinates. */
static int wfn_contains(const char *q,const char *name) {
    if(!q||!name||!*name)return 0;
    size_t n=strlen(name);
    for(const char *p=q;*p;p++)if(!strncasecmp(p,name,n))return 1;
    return 0;
}
/* Quoted link instructions identify an exact visible label. Searching the
 * entire sentence falsely matches distractors such as "the" or "link". */
static int wfn_link_match(const char *q,const char *name) {
    if(!q||!name||!*name)return 0;
    const char *open=strchr(q,'"');
    if(!open)return wfn_contains(q,name);
    const char *close=strchr(open+1,'"');
    return close&&(size_t)(close-open-1)==strlen(name)&&!strncmp(open+1,name,(size_t)(close-open-1));
}
static unsigned wfn_action_kind(unsigned family,const WFView *v,unsigned choice) {
    if(!choice||choice>v->count)return WF_WAIT;
    const WFNode *n=v->nodes+choice-1;
    const char *name=wf_text_get(v,n->name);
    return family==1&&n->role==WF_BUTTON&&strcmp(name,"Menu") ? WF_POINTER_MOVE:WF_CLICK;
}
static void wfn_project(const WFView *v,const unsigned char *visited,float *out,unsigned char *mask) {
    memset(out,0,OBS_SIZE*sizeof *out);
    memset(mask,0,WFN_ACTIONS);mask[0]=1;
    const char *q=wf_text_get(v,v->instruction);
    out[0]=(float)v->count/WFN_NODES;
    out[1]=v->deadline_ms?(float)v->elapsed_ms/v->deadline_ms:0;
    out[2]=(float)v->omitted;out[3]=(float)v->text_truncated;
    for(unsigned i=0;i<v->count&&i<WFN_NODES;i++) {
        const WFNode *n=v->nodes+i;
        const char *name=wf_text_get(v,n->name),*value=wf_text_get(v,n->value);
        float *d=out+8+i*WFN_NODE_FEATURES;
        d[0]=1;
        if(n->role<=WF_TEXTAREA)d[1+n->role]=1;
        d[18]=!!(n->flags&WF_SELECTED);d[19]=!!(n->flags&WF_EXPANDED);
        d[20]=n->role==WF_LINK?wfn_link_match(q,name):wfn_contains(q,name);d[21]=wfn_contains(q,value);
        d[22]=n->ref<256&&visited?!!visited[n->ref]:0;
        d[23]=n->parent<256&&visited?!!visited[n->parent]:0;
        d[24]=(float)n->ref/256;d[25]=(float)n->parent/256;
        d[26]=!!(n->flags&WF_ENABLED);d[27]=!!(n->flags&WF_CLICKABLE);
        d[28]=!!(n->flags&WF_CHECKED);d[29]=!!(n->flags&WF_FOCUSED);
        d[30]=name&&(!strcasecmp(name,"Submit"));
        d[31]=name&&(!strcmp(name,"Menu"));
        mask[i+1]=(n->flags&(WF_VISIBLE|WF_ENABLED|WF_CLICKABLE))==
                  (WF_VISIBLE|WF_ENABLED|WF_CLICKABLE);
        out[4]+=d[20]*(n->role==WF_LINK||n->role==WF_OPTION);
        out[5]+=!d[22]*(n->role==WF_TAB||n->role==WF_BUTTON);
    }
    out[4]=out[4]>0;out[5]/=WFN_NODES;
}
static void wfn_observe(Env *env) {
    for(unsigned i=0;i<WFN_LANES;i++) {
        WFView v;
        if(wf_observe(&env->family,i,&v))abort();
        wfn_project(&v,env->visited[i],
            (float*)env->agents[i].observations,env->agents[i].action_mask);
    }
}
void puf_init(Env *env,Dict *kwargs) {
    env->num_agents=WFN_LANES;
    int mask=(int)dict_get(kwargs,"task_mask");
    int family=(int)dict_get(kwargs,"family");
    if(family<0||family>1)abort();
    env->family_id=(unsigned)family;env->task_count=family?2:9;
    if(mask<=0||(unsigned)mask>=(1u<<env->task_count))abort();
    env->task_mask=(unsigned)mask;
    char error[256];
    const char *path=family?"build/webnav/families/menus/libmenus.so":"build/webnav/families/panels/libpanels.so";
    if(wf_open(&env->family,path,error,sizeof error)) {
        fprintf(stderr,"WebNav navigation: %s\n",error);abort();
    }
    if(env->family.api->batch_lanes!=WFN_LANES||env->family.api->task_count!=env->task_count)abort();
    for(unsigned i=0;i<WFN_LANES;i++)env->agents[i].policy=0;
}
#define MY_VEC_INIT
Env* my_vec_init(int *size,int *starts,int *counts,Dict *vk,Dict *ek) {
    int total=(int)dict_get(vk,"total_agents"),buffers=(int)dict_get(vk,"num_buffers");
    assert(buffers>0&&total>=WFN_LANES*buffers&&total%(WFN_LANES*buffers)==0);
    *size=total/WFN_LANES;
    int per_buffer=*size/buffers;
    Env *envs=(Env*)calloc(*size,sizeof *envs);
    if(!envs)abort();
    unsigned offset=(unsigned)dict_get(ek,"seed_offset");
    for(int b=0;b<buffers;b++){starts[b]=b*per_buffer;counts[b]=per_buffer;}
    for(int e=0;e<*size;e++){
        envs[e].rng=offset+(uint32_t)e*2654435761u;
        puf_init(&envs[e],ek);
    }
    return envs;
}
void puf_reset(Env *env) {
    for(unsigned i=0;i<WFN_LANES;i++)wfn_reset_lane(env,i);
    if(wf_batch_checked(&env->family))abort();
    wfn_observe(env);
}
void puf_step(Env *env) {
    for(unsigned i=0;i<WFN_LANES;i++) {
        Agent *agent=&env->agents[i];
        WFView v;
        if(wf_observe(&env->family,i,&v))abort();
        int choice=(int)agent->actions[0];
        if(choice<0||choice>=WFN_ACTIONS||!agent->action_mask[choice]||
           (choice>0&&(unsigned)choice>v.count))choice=0;
        WFAction a={0};
        a.kind=wfn_action_kind(env->family_id,&v,(unsigned)choice);
        a.target=choice?v.nodes[choice-1].ref:0;
        if(a.target<256&&a.target)env->visited[i][a.target]=1;
        a.elapsed_ms=v.elapsed_ms+250;
        if(a.elapsed_ms>v.deadline_ms)a.elapsed_ms=v.deadline_ms;
        if(wf_apply(&env->family,i,&a))abort();
        agent->rewards[0]=0;
        agent->terminals[0]=0;
        env->steps[i]++;
    }
    if(wf_batch_checked(&env->family))abort();
    int any_reset=0;
    for(unsigned i=0;i<WFN_LANES;i++) {
        uint32_t *r=env->family.words+(size_t)i*env->family.api->row_words;
        if(r[WF_STATUS]!=WF_RUNNING) {
            float raw;memcpy(&raw,r+WF_RAW_REWARD,sizeof raw);
            int win=raw>=0.999f;
            env->agents[i].rewards[0]=raw;
            env->agents[i].terminals[0]=1;
            env->log.perf+=win;
            env->log.score+=raw;
            env->log.episode_length+=env->steps[i];
            env->log.n++;
            wfn_reset_lane(env,i);
            any_reset=1;
        }else r[WF_OP]=WF_OBSERVE;
    }
    if(any_reset&&wf_batch_checked(&env->family))abort();
    wfn_observe(env);
}
void puf_log(Log *log,Dict *out) {
    dict_set(out,"perf",log->perf);
    dict_set(out,"score",log->score);
    dict_set(out,"episode_length",log->episode_length);
}
void puf_render(Env *env){(void)env;}
void puf_close(Env *env){wf_close(&env->family);}
