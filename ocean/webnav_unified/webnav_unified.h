#pragma once
/* One PPO policy interface; all task dynamics/generation stay in CPU Bend. */
typedef float obs_t;
#include "pufferenv.h"
#include "../webnav/unified/policy.h"
#include "../webnav/unified/semantic.h"
#include "../webnav/unified/manifest.h"
#include "../webnav/families/common/loader.h"
#include <assert.h>
#ifdef __linux__
#include <sys/prctl.h>
/* WSL's pipe core handler can stall a failed assertion, even with ulimit -c 0.
 * Disable it before main, including trainer configuration assertions. */
__attribute__((constructor)) static void wnu_disable_core(void) {
    prctl(PR_SET_DUMPABLE,0,0,0,0);
}
#endif
#define OBS_SIZE WU_OBS_SIZE
#define NUM_ATNS 1
#define ACT_SIZES {WU_ACTIONS}
struct Log {
    float perf,score,episode_length,n,invalid_actions,incomplete;
    float task_success[WU_TASK_COUNT],task_episodes[WU_TASK_COUNT];
};
struct Env {
    Log log;
    int num_agents,tag,boundary_reached;
    Agent agents[8];
    WFLoaded family;
    WUState state[8];
    unsigned family_index,step_ms,max_steps;
    int fixed_task; /* host evaluation/curriculum selector; never encoded */
    uint32_t rng;
};
static uint32_t wnu_random(Env *e) {
    e->rng=e->rng*1664525u+1013904223u;
    uint32_t x=e->rng;x^=x>>16;x*=0x7feb352du;x^=x>>15;
    return x;
}
static void wnu_error(const char *message) {
    fprintf(stderr,"Unified WebNav: %s\n",message);exit(1);
}
static void wnu_stage_reset(Env *e,unsigned lane) {
    uint32_t *r=e->family.words+(size_t)lane*e->family.api->row_words;
    memset(r,0,e->family.api->row_words*sizeof *r);
    r[WF_VERSION]=WF_ABI_VERSION;r[WF_TASK]=e->fixed_task>=0?(unsigned)e->fixed_task:wnu_random(e)%e->family.api->task_count;
    r[WF_OP]=WF_RESET;r[WF_SEED]=wnu_random(e);
    memset(e->state+lane,0,sizeof e->state[lane]);
}
static void wnu_flush(Env *e) {
    if(wf_batch_checked(&e->family))wnu_error("Bend family batch rejected");
    for(int l=0;l<e->num_agents;l++)e->family.words[(size_t)l*e->family.api->row_words+WF_OP]=WF_OBSERVE;
}
static void wnu_public(Env *e,unsigned lane,WFView *v,WUCapabilities *caps) {
    uint32_t *r=e->family.words+(size_t)lane*e->family.api->row_words;
    int observed=wf_observe(&e->family,lane,v);
    int capable=observed?0:wu_capabilities(e->family.api,r[WF_TASK],v,caps);
    if(observed||capable){
        fprintf(stderr,"Unified WebNav: family=%s task=%u lane=%u observation=%d capabilities=%d\n",
            e->family.api->family,r[WF_TASK],lane,observed,capable);
        wnu_error("public observation/capability failure");
    }
}
static void wnu_observe(Env *e) {
    for(int l=0;l<e->num_agents;l++) {
        WFView v;WUCapabilities caps;wnu_public(e,(unsigned)l,&v,&caps);
        if(wu_project(&v,&caps,e->state+l,(float*)e->agents[l].observations,e->agents[l].action_mask))
            wnu_error("shared encoder failure");
    }
}
void puf_init(Env *e,Dict *kwargs) {
    const WUFamilySpec *spec=wu_families+e->family_index;
    char error[512];
    if(wf_open(&e->family,spec->library,error,sizeof error))wnu_error(error);
    if(strcmp(e->family.api->family,spec->name)||e->family.api->batch_lanes!=spec->lanes||e->family.api->task_count!=spec->tasks)
        wnu_error("registry/library metadata mismatch");
    e->num_agents=(int)spec->lanes;
    e->step_ms=(unsigned)dict_get(kwargs,"step_ms");
    e->max_steps=(unsigned)dict_get(kwargs,"max_episode_steps");
    e->fixed_task=dict_find(kwargs,"task")?(int)dict_get(kwargs,"task"):-1;
    if(e->fixed_task>=(int)spec->tasks||e->fixed_task< -1)wnu_error("invalid fixed task selector");
    if(!e->step_ms||!e->max_steps)wnu_error("positive step_ms/max_episode_steps required");
    for(int l=0;l<e->num_agents;l++)e->agents[l].policy=0;
}
#define MY_VEC_INIT
Env *my_vec_init(int *size,int *starts,int *counts,Dict *vk,Dict *ek) {
    int total=(int)dict_get(vk,"total_agents"),buffers=(int)dict_get(vk,"num_buffers");
    uint32_t mask=(uint32_t)dict_get(ek,"family_mask");
    if(wu_semantic_init((int)dict_get(ek,"semantic")))wnu_error("pinned frozen text encoder failed to load");
    if(!mask||mask>=(1u<<WU_FAMILY_COUNT)||dict_get(vk,"num_policies")!=1)
        wnu_error("choose valid family_mask and num_policies=1");
    unsigned cycle_lanes=0,cycle_envs=0;
    for(unsigned f=0;f<WU_FAMILY_COUNT;f++)if(mask&(1u<<f)){cycle_lanes+=wu_families[f].lanes;cycle_envs++;}
    if(total<=0||buffers<=0||total%buffers||((unsigned)(total/buffers))%cycle_lanes)
        wnu_error("each buffer's agent count must be a multiple of selected family cycle lanes");
    unsigned cycles=(unsigned)(total/buffers)/cycle_lanes;
    unsigned per_buffer=cycles*cycle_envs;
    *size=(int)(per_buffer*(unsigned)buffers);
    Env *envs=(Env*)calloc((size_t)*size,sizeof *envs);
    if(!envs)wnu_error("environment allocation failed");
    unsigned seed=(unsigned)dict_get(ek,"seed_offset"),index=0;
    for(int b=0;b<buffers;b++){
        starts[b]=(int)index;counts[b]=(int)per_buffer;
        for(unsigned c=0;c<cycles;c++)for(unsigned f=0;f<WU_FAMILY_COUNT;f++)if(mask&(1u<<f)){
            envs[index].family_index=f;envs[index].rng=seed+index*2654435761u;
            puf_init(envs+index,ek);index++;
        }
    }
    return envs;
}
void puf_reset(Env *e) {
    for(int l=0;l<e->num_agents;l++)wnu_stage_reset(e,(unsigned)l);
    wnu_flush(e);wnu_observe(e);
}
void puf_step(Env *e) {
    int dirty=0;
    for(int l=0;l<e->num_agents;l++){
        Agent *agent=e->agents+l;WUState *state=e->state+l;
        WFView v;WUCapabilities caps;wnu_public(e,(unsigned)l,&v,&caps);
        float raw_choice=agent->actions[0];
        unsigned choice=isfinite(raw_choice)&&raw_choice>=0&&raw_choice<WU_ACTIONS?(unsigned)raw_choice:0;
        if(!agent->action_mask[choice])choice=0;
        uint32_t elapsed=v.deadline_ms-v.elapsed_ms<e->step_ms?v.deadline_ms:v.elapsed_ms+e->step_ms;
        WFAction action={0};int execute=wu_decode(&v,&caps,state,choice,elapsed,&action);
        if(execute<0)wnu_error("shared action decode rejected masked choice");
        state->steps++;agent->rewards[0]=0;agent->terminals[0]=0;
        if(state->steps>=e->max_steps){action=(WFAction){0};action.kind=WF_WAIT;action.elapsed_ms=v.deadline_ms;execute=1;}
        if(execute){
            if(wf_apply(&e->family,(unsigned)l,&action)){
                /* Missing public gesture state is reported, never repaired by
                 * peeking at the private model. Rejected commands advance time. */
                e->log.invalid_actions++;
                action=(WFAction){0};action.kind=WF_WAIT;action.elapsed_ms=elapsed;
                if(wf_apply(&e->family,(unsigned)l,&action))wnu_error("family rejected WAIT");
            }
            dirty=1;
        }
    }
    if(dirty)wnu_flush(e);
    int resets=0;
    for(int l=0;l<e->num_agents;l++){
        uint32_t *r=e->family.words+(size_t)l*e->family.api->row_words;
        if(r[WF_STATUS]==WF_RUNNING)continue;
        float reward;memcpy(&reward,r+WF_RAW_REWARD,sizeof reward);
        unsigned task=wu_families[e->family_index].global_tasks[r[WF_TASK]];
        e->agents[l].rewards[0]=reward;e->agents[l].terminals[0]=1;
        e->log.perf+=reward>=0.999f;e->log.score+=reward;e->log.n++;
        e->log.episode_length+=e->state[l].steps;
        e->log.task_success[task]+=reward>=0.999f;e->log.task_episodes[task]++;
        WFView v;WUCapabilities caps;wnu_public(e,(unsigned)l,&v,&caps);e->log.incomplete+=caps.incomplete!=0;
        wnu_stage_reset(e,(unsigned)l);resets=1;
    }
    if(resets)wnu_flush(e);
    wnu_observe(e);
}
void puf_log(Log *log,Dict *out) {
    dict_set(out,"perf",log->perf);dict_set(out,"score",log->score);
    dict_set(out,"episode_length",log->episode_length);
    dict_set(out,"invalid_actions_per_episode",log->invalid_actions);
    dict_set(out,"incomplete_fraction",log->incomplete);
    /* History/export expects the same columns at every epoch. A zero success
     * rate with zero episode fraction means unevaluated in this window. */
    for(unsigned t=0;t<WU_TASK_COUNT;t++){
        char key[128];snprintf(key,sizeof key,"task/%s/success",wu_task_names[t]);
        dict_set(out,key,log->task_episodes[t]>0?log->task_success[t]/log->task_episodes[t]:0);
        snprintf(key,sizeof key,"task/%s/episode_fraction",wu_task_names[t]);
        dict_set(out,key,log->n>0?log->task_episodes[t]/log->n:0);
    }
}
void puf_render(Env *e){(void)e;}
void puf_close(Env *e){wf_close(&e->family);}
