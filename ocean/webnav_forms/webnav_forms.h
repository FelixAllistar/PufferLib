#pragma once
/* Public-view trainer adapter for the checked stock-CPU-Bend forms family.
 * Candidate text comes only from the instruction and visible node values. */
typedef float obs_t;
#include "pufferenv.h"
#include "../webnav/families/common/loader.h"
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <strings.h>

#define WFF_LANES 4
#define WFF_NODES 16
#define WFF_CANDIDATES 16
#define WFF_ACTIONS (1 + WFF_NODES + 1 + WFF_CANDIDATES)
#define WFF_NODE_FEATURES 96
#define WFF_CANDIDATE_FEATURES 64
#define OBS_SIZE (16 + 192 + WFF_NODES*WFF_NODE_FEATURES + WFF_CANDIDATES*WFF_CANDIDATE_FEATURES)
#define ACT_SIZES {WFF_ACTIONS}
#define NUM_ATNS 1

typedef struct {
    char text[256],source_name[64];
    unsigned length,kind,source_index,source_role;
} WFFCandidate;
typedef struct {WFFCandidate c[WFF_CANDIDATES];} WFFCatalog;
struct Log {float perf,score,episode_length,n;};
struct Env {
    Log log;
    int num_agents,tag,boundary_reached;
    Agent agents[WFF_LANES];
    WFLoaded family;
    uint32_t rng;
    unsigned task_mask,data_mode,progress_reward,steps[WFF_LANES];
};

static uint32_t wff_random(Env *env) {
    env->rng=env->rng*1664525u+1013904223u;
    uint32_t x=env->rng;x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;
    return x;
}
static unsigned wff_task(Env *env) {
    unsigned count=0;for(unsigned i=0;i<8;i++)count+=(env->task_mask>>i)&1u;
    unsigned rank=wff_random(env)%count;
    for(unsigned i=0;i<8;i++)if((env->task_mask>>i)&1u)if(!rank--)return i;
    abort();
}
static void wff_reset_lane(Env *env,unsigned lane) {
    uint32_t *r=env->family.words+(size_t)lane*env->family.api->row_words;
    memset(r,0,env->family.api->row_words*sizeof *r);
    r[WF_VERSION]=WF_ABI_VERSION;r[WF_TASK]=wff_task(env);
    r[WF_OP]=WF_RESET;r[WF_SEED]=wff_random(env);
    r[6]=env->data_mode==2?(wff_random(env)&1u):env->data_mode;
    env->steps[lane]=0;
}
static void wff_bytes(const char *s,unsigned cap,float *dst) {
    if(!s)return;
    for(unsigned i=0;i<cap&&s[i];i++)dst[i]=(unsigned char)s[i]/127.0f;
}
static void wff_candidate(WFFCatalog *cat,unsigned slot,const char *text,size_t len,
                          unsigned kind,unsigned source_index,unsigned source_role,const char *source_name) {
    if(slot>=WFF_CANDIDATES||!text||!len||len>255)return;
    WFFCandidate *c=&cat->c[slot];
    for(size_t i=0;i<len;i++)if((unsigned char)text[i]<32||(unsigned char)text[i]>126)return;
    memcpy(c->text,text,len);c->text[len]=0;c->length=(unsigned)len;
    c->kind=kind;c->source_index=source_index;c->source_role=source_role;
    if(source_name)snprintf(c->source_name,sizeof c->source_name,"%s",source_name);
}
static int wff_quoted(const char *q,unsigned index,const char **start,size_t *len) {
    for(unsigned i=0;i<=index;i++){
        const char *open=strchr(q,'"');if(!open)return 0;
        const char *close=strchr(open+1,'"');if(!close)return 0;
        if(i==index){*start=open+1;*len=(size_t)(close-open-1);return 1;}
        q=close+1;
    }
    return 0;
}
static void wff_catalog(const WFView *v,WFFCatalog *cat) {
    memset(cat,0,sizeof *cat);
    const char *q=wf_text_get(v,v->instruction),*quoted;size_t len;
    if(wff_quoted(q,0,&quoted,&len)) {
        wff_candidate(cat,0,quoted,len,1,0,0,NULL);
        if(len&&len<=255) {
            char upper[256],lower[256];
            for(size_t i=0;i<len;i++){
                upper[i]=(char)toupper((unsigned char)quoted[i]);
                lower[i]=(char)tolower((unsigned char)quoted[i]);
            }
            wff_candidate(cat,1,upper,len,2,0,0,NULL);
            wff_candidate(cat,2,lower,len,3,0,0,NULL);
        }
    }
    if(wff_quoted(q,1,&quoted,&len))wff_candidate(cat,3,quoted,len,4,0,0,NULL);
    unsigned field=0,statik=0;
    for(unsigned i=0;i<v->count;i++){
        const WFNode *n=v->nodes+i;
        const char *name=wf_text_get(v,n->name),*value=wf_text_get(v,n->value);
        if(n->role==WF_INPUT||n->role==WF_TEXTAREA){
            if(field<4)wff_candidate(cat,4+field,value,strlen(value),5,i,n->role,name);
            field++;
        }else if(value&&*value&&statik<8){
            const char *source_name=name;
            if(n->role==WF_CELL&&i&&v->nodes[i-1].role==WF_CELL){
                const char *key=wf_text_get(v,v->nodes[i-1].name);
                if(key&&*key)source_name=key;
            }
            wff_candidate(cat,8+statik,value,strlen(value),6,i,n->role,source_name);
            statik++;
        }
    }
}
static int wff_focused(const WFView *v) {
    for(unsigned i=0;i<v->count;i++)if((v->nodes[i].flags&WF_FOCUSED)&&
        (v->nodes[i].role==WF_INPUT||v->nodes[i].role==WF_TEXTAREA))return (int)i;
    return -1;
}
static int wff_key_match(const char *field,const char *source) {
    if(!field||!source)return 0;
    size_t n=strlen(field);
    return n>1&&field[n-1]==':'&&strlen(source)==n-1&&!strncasecmp(field,source,n-1);
}
/* Public table cells and field labels suffice to measure editing progress.
 * This optional training potential does not change Bend's raw score or the
 * unshaped original-page evaluation. */
static unsigned wff_table_progress(const WFView *v) {
    unsigned correct=0;
    for(unsigned i=0;i<v->count;i++){
        const WFNode *field=v->nodes+i;
        if(field->role!=WF_INPUT)continue;
        const char *label=wf_text_get(v,field->name),*current=wf_text_get(v,field->value);
        if(!current||!*current)continue;
        for(unsigned j=1;j<v->count;j++){
            const WFNode *key=v->nodes+j-1,*value=v->nodes+j;
            if(key->role!=WF_CELL||value->role!=WF_CELL)continue;
            if(wff_key_match(label,wf_text_get(v,key->name))&&
               !strcmp(current,wf_text_get(v,value->value))){correct++;break;}
        }
    }
    return correct;
}
static void wff_project(const WFView *v,const WFFCatalog *cat,float out[OBS_SIZE],unsigned char mask[WFF_ACTIONS]) {
    memset(out,0,OBS_SIZE*sizeof *out);memset(mask,0,WFF_ACTIONS);mask[0]=1;
    const char *q=wf_text_get(v,v->instruction);int focus=wff_focused(v);
    const WFNode *f=focus>=0?v->nodes+focus:NULL;
    const char *field_name=f?wf_text_get(v,f->name):NULL;
    const char *current=f?wf_text_get(v,f->value):NULL;
    out[0]=(float)v->count/WFF_NODES;
    out[1]=(float)v->elapsed_ms/v->deadline_ms;
    out[2]=focus>=0;out[3]=focus>=0?(float)(focus+1)/WFF_NODES:0;
    out[4]=strstr(q,"all upper case")!=NULL;
    out[5]=strstr(q,"all lower case")!=NULL;
    out[6]=strstr(q,"Copy the text")!=NULL;
    out[7]=strstr(q,"corresponds with each label")!=NULL;
    out[8]=strstr(q,"username")!=NULL;
    out[9]=strstr(q,"password")!=NULL;
    out[10]=strstr(q,"Type the text below")!=NULL;
    out[11]=strstr(q,"1st text area")!=NULL;
    out[12]=strstr(q,"2nd text area")!=NULL;
    out[13]=strstr(q,"3rd text area")!=NULL;
    for(unsigned i=0;i<v->count;i++){
        const char *name=wf_text_get(v,v->nodes[i].name);
        if(name&&!strcmp(name,"Popup Cancel"))out[14]=1;
    }
    wff_bytes(q,192,out+16);
    for(unsigned i=0;i<v->count&&i<WFF_NODES;i++){
        const WFNode *n=v->nodes+i;float *d=out+16+192+i*WFF_NODE_FEATURES;
        const char *name=wf_text_get(v,n->name),*value=wf_text_get(v,n->value);
        wff_bytes(name,32,d);wff_bytes(value,32,d+32);
        if(n->role<=WF_TEXTAREA)d[64+n->role]=1;
        d[81]=!!(n->flags&WF_VISIBLE);d[82]=!!(n->flags&WF_ENABLED);
        d[83]=!!(n->flags&WF_CLICKABLE);d[84]=!!(n->flags&WF_FOCUSED);
        d[85]=n->selection_start==0&&n->selection_end==strlen(value)&&*value;
        d[86]=(float)(n->selection_end-n->selection_start)/255;
        d[87]=(float)strlen(value)/255;d[88]=(float)(i+1)/WFF_NODES;
        mask[1+i]=(n->flags&(WF_VISIBLE|WF_ENABLED|WF_CLICKABLE))==
            (WF_VISIBLE|WF_ENABLED|WF_CLICKABLE);
    }
    if(f&&(f->flags&WF_ENABLED)){
        size_t length=strlen(current);
        mask[17]=length&&!(f->selection_start==0&&f->selection_end==length);
    }
    for(unsigned i=0;i<WFF_CANDIDATES;i++){
            const WFFCandidate *c=cat->c+i;
            if(!c->length)continue;
            float *d=out+16+192+WFF_NODES*WFF_NODE_FEATURES+i*WFF_CANDIDATE_FEATURES;
            d[0]=1;if(c->kind<=6)d[c->kind]=1;
            d[7]=(float)(c->source_index+1)/WFF_NODES;
            d[8]=(float)c->length/255;
            d[9]=f&&wff_key_match(field_name,c->source_name);
            d[10]=*c->source_name&&strstr(q,c->source_name)!=NULL;
            d[11]=f&&strlen(current)==c->length&&!memcmp(current,c->text,c->length);
            d[12]=(float)c->source_role/WF_TEXTAREA;
            wff_bytes(c->text,32,d+13);
            wff_bytes(c->source_name,16,d+45);
            int selected=f&&(!*current||(f->selection_start==0&&f->selection_end==strlen(current)));
            mask[18+i]=f&&(f->flags&WF_ENABLED)&&selected&&!d[11];
    }
}
static void wff_observe(Env *env) {
    for(unsigned lane=0;lane<WFF_LANES;lane++){
        WFView v;WFFCatalog cat;
        if(wf_observe(&env->family,lane,&v))abort();
        wff_catalog(&v,&cat);
        wff_project(&v,&cat,(float*)env->agents[lane].observations,env->agents[lane].action_mask);
    }
}
void puf_init(Env *env,Dict *kwargs) {
    env->num_agents=WFF_LANES;
    int mask=(int)dict_get(kwargs,"task_mask");
    int mode=(int)dict_get(kwargs,"data_mode");
    int shaping=(int)dict_get(kwargs,"progress_reward");
    if(mask<=0||mask>255)abort();
    if(mode<0||mode>2)abort();
    if(shaping<0||shaping>1)abort();
    env->task_mask=(unsigned)mask;
    env->data_mode=(unsigned)mode;
    env->progress_reward=(unsigned)shaping;
    char error[256];
    if(wf_open(&env->family,"build/webnav/families/forms/libforms.so",error,sizeof error)){
        fprintf(stderr,"WebNav forms family: %s\n",error);abort();
    }
    if(strcmp(env->family.api->family,"forms")||env->family.api->batch_lanes!=WFF_LANES||
        env->family.api->task_count<8)abort();
    for(unsigned lane=0;lane<WFF_LANES;lane++)env->agents[lane].policy=0;
}
#define MY_VEC_INIT
Env* my_vec_init(int *size,int *starts,int *counts,Dict *vk,Dict *ek) {
    int total=(int)dict_get(vk,"total_agents"),buffers=(int)dict_get(vk,"num_buffers");
    assert(buffers>0&&total>=WFF_LANES*buffers&&total%(WFF_LANES*buffers)==0);
    *size=total/WFF_LANES;int per_buffer=*size/buffers;
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
    for(unsigned lane=0;lane<WFF_LANES;lane++)wff_reset_lane(env,lane);
    if(wf_batch_checked(&env->family))abort();
    wff_observe(env);
}
void puf_step(Env *env) {
    unsigned before[WFF_LANES]={0};
    for(unsigned lane=0;lane<WFF_LANES;lane++){
        Agent *agent=env->agents+lane;WFView v;WFFCatalog cat;
        if(wf_observe(&env->family,lane,&v))abort();
        unsigned task=env->family.words[(size_t)lane*env->family.api->row_words+WF_TASK];
        if(env->progress_reward&&task==6)before[lane]=wff_table_progress(&v);
        wff_catalog(&v,&cat);
        int choice=(int)agent->actions[0];
        if(choice<0||choice>=WFF_ACTIONS||!agent->action_mask[choice])choice=0;
        WFAction action={.kind=WF_WAIT,.elapsed_ms=v.elapsed_ms+250};
        if(choice>=1&&choice<=WFF_NODES){action.kind=WF_CLICK;action.target=v.nodes[choice-1].ref;}
        else if(choice==17)action.kind=WF_SELECT_ALL;
        else if(choice>=18){
            WFFCandidate *c=cat.c+(choice-18);
            action.kind=WF_INSERT;action.text=c->text;action.text_length=c->length;
        }
        if(wf_apply(&env->family,lane,&action))abort();
        agent->rewards[0]=0;agent->terminals[0]=0;env->steps[lane]++;
    }
    if(wf_batch_checked(&env->family))abort();
    int any_reset=0;
    for(unsigned lane=0;lane<WFF_LANES;lane++){
        uint32_t *r=env->family.words+(size_t)lane*env->family.api->row_words;
        if(env->progress_reward&&r[WF_TASK]==6){
            unsigned after=0;
            if(r[WF_STATUS]==WF_RUNNING){WFView v;if(wf_observe(&env->family,lane,&v))abort();after=wff_table_progress(&v);}
            env->agents[lane].rewards[0]+=0.25f*(0.95f*after-before[lane]);
        }
        if(r[WF_STATUS]!=WF_RUNNING){
            float raw;memcpy(&raw,r+WF_RAW_REWARD,sizeof raw);
            env->agents[lane].rewards[0]+=raw;env->agents[lane].terminals[0]=1;
            env->log.perf+=raw>=0.999f;env->log.score+=raw;
            env->log.episode_length+=env->steps[lane];env->log.n++;
            wff_reset_lane(env,lane);any_reset=1;
        }else r[WF_OP]=WF_OBSERVE;
    }
    if(any_reset&&wf_batch_checked(&env->family))abort();
    wff_observe(env);
}
void puf_log(Log *log,Dict *out) {
    dict_set(out,"perf",log->perf);dict_set(out,"score",log->score);
    dict_set(out,"episode_length",log->episode_length);
}
void puf_render(Env *env){(void)env;}
void puf_close(Env *env){wf_close(&env->family);}
