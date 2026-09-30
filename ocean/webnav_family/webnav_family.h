#pragma once
/* First trainable family-v2 environment. Task transitions and generation run
 * in the checked stock-CPU-Bend click library; this file projects public
 * observations and routes PufferLib actions through the family ABI. */
typedef float obs_t;
#include "pufferenv.h"
#include "../webnav/families/common/loader.h"
#include "../webnav/text_encoder.h"
#include <assert.h>
#include <math.h>
#include <strings.h>

#define WFC_LANES 8
#define WFC_NODES 16
#define WFC_ACTIONS (1 + WFC_NODES)
#define WFC_NODE_FEATURES 84
#define OBS_SIZE (8 + 128 + WFC_NODES * WFC_NODE_FEATURES)
#define ACT_SIZES {WFC_ACTIONS}
#define NUM_ATNS 1

struct Log { float perf, score, episode_length, n; };
struct Env {
    Log log;
    int num_agents;
    int tag, boundary_reached;
    Agent agents[WFC_LANES];
    WFLoaded family;
    uint32_t rng;
    unsigned task_mask, data_mode, semantic_enabled, steps[WFC_LANES];
    unsigned semantic_valid[WFC_LANES];
    float semantic[WFC_LANES][WFC_NODES];
};

static WebTextEncoder *wfc_text_encoder;
static void wfc_text_init(void) {
    if(wfc_text_encoder)return;
    wfc_text_encoder=web_text_load("build/webnav/reference/potion-tokenizer.json",
        "build/webnav/reference/potion-model.safetensors");
    if(!wfc_text_encoder){fputs("WebNav family: pinned Potion text assets unavailable or invalid\n",stderr);abort();}
}
/* Query-word to control-name similarity is a public observation feature.
 * The model is frozen; task targets and original synonym groups are not used. */
static void wfc_semantic_scores(const WFView *v,float out[WFC_NODES]) {
    memset(out,0,WFC_NODES*sizeof *out);
    const char *q=wf_text_get(v,v->instruction),*prefix="Select words similar to ";
    size_t prefix_len=strlen(prefix);
    if(strncmp(q,prefix,prefix_len))return;
    const char *end=strstr(q+prefix_len," and click Submit.");
    if(!end)return;
    float requested[WFC_NODES][WEB_TEXT_DIM];
    unsigned count=0;
    for(const char *at=q+prefix_len;at<end&&count<WFC_NODES;) {
        while(at<end&&*at==' ')at++;
        const char *next=at;
        while(next<end&&*next!=',')next++;
        const char *word_end=next;
        while(word_end>at&&word_end[-1]==' ')word_end--;
        if(word_end>at&&web_text_encode(wfc_text_encoder,at,(size_t)(word_end-at),requested[count],NULL)==0)count++;
        at=next<end?next+1:end;
    }
    for(unsigned i=0;i<v->count&&i<WFC_NODES;i++) {
        if(v->nodes[i].role!=WF_CHECKBOX)continue;
        const char *name=wf_text_get(v,v->nodes[i].name);
        float name_vec[WEB_TEXT_DIM];
        if(!name||web_text_encode(wfc_text_encoder,name,strlen(name),name_vec,NULL))continue;
        float best=-1.0f;
        for(unsigned j=0;j<count;j++) {
            float similarity=0;
            for(unsigned k=0;k<WEB_TEXT_DIM;k++)similarity+=name_vec[k]*requested[j][k];
            if(similarity>best)best=similarity;
        }
        out[i]=best<0?0:best;
    }
}

static uint32_t wfc_random(Env *env) {
    env->rng = env->rng * 1664525u + 1013904223u;
    /* Do not use an LCG low bit for both task and mode selection: adjacent
     * draws would otherwise correlate task parity with data mode. */
    uint32_t x=env->rng;
    x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;
    return x;
}
static unsigned wfc_task(Env *env) {
    unsigned count=0;
    for(unsigned i=0;i<10;i++)count+=(env->task_mask>>i)&1u;
    unsigned rank=wfc_random(env)%count;
    for(unsigned i=0;i<10;i++)if((env->task_mask>>i)&1u) {
        if(!rank--)return i;
    }
    abort();
}
static void wfc_reset_lane(Env *env,unsigned lane) {
    uint32_t *r=env->family.words+(size_t)lane*env->family.api->row_words;
    memset(r,0,env->family.api->row_words*sizeof *r);
    r[WF_VERSION]=WF_ABI_VERSION;
    r[WF_TASK]=wfc_task(env);
    r[WF_OP]=WF_RESET;
    r[WF_SEED]=wfc_random(env);
    r[6]=env->data_mode==2?(wfc_random(env)&1u):env->data_mode;
    env->steps[lane]=0;
    env->semantic_valid[lane]=0;
}
static void wfc_bytes(const char *s,unsigned count,float *dst) {
    unsigned i=0;
    for(;i<count&&s[i];i++)dst[i]=(unsigned char)s[i]/127.0f;
}
static int wfc_contains(const char *q,const char *name) {
    if(!*name)return 0;
    size_t n=strlen(name);
    for(const char *p=q;*p;p++)if(!strncasecmp(p,name,n))return 1;
    return 0;
}
static unsigned wfc_requested_role(const char *q) {
    const char *open=strchr(q,'"');
    if(!open)return 0;
    const char *close=strchr(open+1,'"');
    if(!close)return 0;
    size_t len=(size_t)(close-open-1);
    const char *s=open+1;
    if(len==6&&!strncasecmp(s,"button",6))return WF_BUTTON;
    if(len==8&&!strncasecmp(s,"checkbox",8))return WF_CHECKBOX;
    if(len==4&&!strncasecmp(s,"text",4))return WF_INPUT;
    if(len==8&&!strncasecmp(s,"textarea",8))return WF_TEXTAREA;
    if(len==5&&!strncasecmp(s,"radio",5))return WF_RADIO;
    return 0;
}
static int wfc_requested_ordinal(const char *q) {
    if(strstr(q,"1st"))return 0;
    if(strstr(q,"2nd"))return 1;
    if(strstr(q,"3rd"))return 2;
    return -1;
}
static void wfc_project(const WFView *v,const float *semantic,float *out,unsigned char *mask) {
    memset(out,0,OBS_SIZE*sizeof *out);
    memset(mask,0,WFC_ACTIONS);
    mask[0]=1;
    const char *q=wf_text_get(v,v->instruction);
    unsigned requested_role=wfc_requested_role(q);
    int requested_ordinal=wfc_requested_ordinal(q);
    out[4]=requested_ordinal>=0;
    out[5]=requested_role&&strstr(q," widget.")!=NULL;
    out[6]=!strncmp(q,"Select words similar to ",24);
    out[7]=!strncmp(q,"Select ",7);
    out[0]=(float)v->count/WFC_NODES;
    out[1]=(float)v->elapsed_ms/v->deadline_ms;
    out[2]=(float)v->omitted;
    out[3]=(float)v->text_truncated;
    wfc_bytes(q,128,out+8);
    for(unsigned i=0;i<v->count&&i<WFC_NODES;i++) {
        const WFNode *n=v->nodes+i;
        const char *name=wf_text_get(v,n->name),*value=wf_text_get(v,n->value);
        float *d=out+8+128+i*WFC_NODE_FEATURES;
        wfc_bytes(name,32,d);
        wfc_bytes(value,16,d+32);
        d[48]=1;
        if(n->role<=WF_TEXTAREA)d[49+n->role]=1;
        d[66]=!!(n->flags&WF_CHECKED);
        d[67]=!!(n->flags&WF_FOCUSED);
        d[68]=!!(n->flags&WF_SELECTED);
        d[69]=!!(n->flags&WF_EXPANDED);
        d[70]=wfc_contains(q,name);
        d[71]=wfc_contains(q,value);
        d[72]=n->x/160.0f;
        d[73]=n->y/210.0f;
        d[74]=n->width/160.0f;
        d[75]=n->height/210.0f;
        d[76]=(float)strlen(name)/32;
        d[77]=(float)n->ref/WFC_NODES;
        d[78]=!!(n->flags&WF_ENABLED);
        d[79]=!!(n->flags&WF_CLICKABLE);
        d[80]=semantic?semantic[i]:0;
        d[81]=requested_role&&n->role==requested_role;
        d[82]=requested_ordinal>=0&&i==(unsigned)requested_ordinal;
        mask[i+1]=(n->flags&(WF_VISIBLE|WF_ENABLED|WF_CLICKABLE))==
                  (WF_VISIBLE|WF_ENABLED|WF_CLICKABLE);
    }
}
static void wfc_observe(Env *env) {
    for(unsigned i=0;i<WFC_LANES;i++) {
        WFView v;
        if(wf_observe(&env->family,i,&v))abort();
        if(env->semantic_enabled&&!env->semantic_valid[i]) {
            wfc_semantic_scores(&v,env->semantic[i]);
            env->semantic_valid[i]=1;
        }
        wfc_project(&v,env->semantic_enabled?env->semantic[i]:NULL,
            (float*)env->agents[i].observations,env->agents[i].action_mask);
    }
}
void puf_init(Env *env,Dict *kwargs) {
    env->num_agents=WFC_LANES;
    int mask=(int)dict_get(kwargs,"task_mask");
    int mode=(int)dict_get(kwargs,"data_mode");
    if(mask<=0||mask>=1024)abort();
    if(mode<0||mode>2)abort();
    env->task_mask=(unsigned)mask;
    env->data_mode=(unsigned)mode;
    env->semantic_enabled=dict_get(kwargs,"semantic")!=0;
    if(env->semantic_enabled)wfc_text_init();
    char error[256];
    if(wf_open(&env->family,"build/webnav/families/click/libclick.so",error,sizeof error)) {
        fprintf(stderr,"WebNav click family: %s\n",error);abort();
    }
    if(strcmp(env->family.api->family,"click")||env->family.api->batch_lanes!=WFC_LANES||
       env->family.api->task_count<10)abort();
    for(unsigned i=0;i<WFC_LANES;i++)env->agents[i].policy=0;
}
#define MY_VEC_INIT
Env* my_vec_init(int *size,int *starts,int *counts,Dict *vk,Dict *ek) {
    int total=(int)dict_get(vk,"total_agents"),buffers=(int)dict_get(vk,"num_buffers");
    assert(buffers>0&&total>=WFC_LANES*buffers&&total%(WFC_LANES*buffers)==0);
    *size=total/WFC_LANES;
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
    for(unsigned i=0;i<WFC_LANES;i++)wfc_reset_lane(env,i);
    if(wf_batch_checked(&env->family))abort();
    wfc_observe(env);
}
void puf_step(Env *env) {
    for(unsigned i=0;i<WFC_LANES;i++) {
        Agent *agent=&env->agents[i];
        WFView v;
        if(wf_observe(&env->family,i,&v))abort();
        int choice=(int)agent->actions[0];
        if(choice<0||choice>=WFC_ACTIONS||!agent->action_mask[choice]||
           (choice>0&&(unsigned)choice>v.count))choice=0;
        WFAction a={0};
        a.kind=choice?WF_CLICK:WF_WAIT;
        a.target=choice?v.nodes[choice-1].ref:0;
        a.elapsed_ms=v.elapsed_ms+250;
        if(wf_apply(&env->family,i,&a))abort();
        agent->rewards[0]=0;
        agent->terminals[0]=0;
        env->steps[i]++;
    }
    if(wf_batch_checked(&env->family))abort();
    int any_reset=0;
    for(unsigned i=0;i<WFC_LANES;i++) {
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
            wfc_reset_lane(env,i);
            any_reset=1;
        }else r[WF_OP]=WF_OBSERVE;
    }
    if(any_reset&&wf_batch_checked(&env->family))abort();
    wfc_observe(env);
}
void puf_log(Log *log,Dict *out) {
    dict_set(out,"perf",log->perf);
    dict_set(out,"score",log->score);
    dict_set(out,"episode_length",log->episode_length);
}
void puf_render(Env *env){(void)env;}
void puf_close(Env *env){wf_close(&env->family);}
