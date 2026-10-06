#ifndef SWAT_MOVEMENT_ENV_H
#define SWAT_MOVEMENT_ENV_H
#include "sim.h"
#include "locomotion.h"
#include "render.h"
typedef float obs_t;
#include "pufferenv.h"
#define OBS_SIZE SWAT_LOCOMOTION_OBS
#define NUM_ATNS SWAT_LOCOMOTION_HEADS
#define ACT_SIZES SWAT_LOCOMOTION_ACTION_SIZES
#define PUF_STEPS_PER_SEC 15

struct Log { float perf,episode_return,episode_length,score,n; };
struct Env {
    Log log;
    Agent agents[1];
    int num_agents,tag,boundary_reached;
    unsigned int rng;
    void* course;
    SwatView* view;
    int stage,role,max_steps,steps;
    float episode_return;
    float progress_reward,step_cost,success_reward,fall_penalty;
};

static int swat_movement_setting(Dict* kwargs,const char* key,int fallback,int minimum,int maximum) {
    DictItem* item=dict_find(kwargs,key); double value=item ? item->value : fallback;
    if(!isfinite(value) || value<minimum || value>maximum || floor(value)!=value) {
        fprintf(stderr,"swat movement: %s must be an integer from %d to %d\n",key,minimum,maximum); exit(1);
    }
    return (int)value;
}
static float swat_movement_reward(Dict* kwargs,const char* key,float fallback) {
    DictItem* item=dict_find(kwargs,key); double value=item ? item->value : fallback;
    if(!isfinite(value) || value<0 || value>1000) { fprintf(stderr,"swat movement: %s must be finite and in [0,1000]\n",key); exit(1); }
    return (float)value;
}
void puf_init(Env* env,Dict* kwargs) {
#ifdef SWAT_NATIVE_TRAINER
    DictItem* task=dict_find(kwargs,"task");
    if(!task || !task->str || strcmp(task->str,"movement")) {
        fprintf(stderr,"swat: this puffer was built for env.task=movement; rebuild after changing task\n"); exit(1);
    }
#endif
    env->num_agents=1; env->agents[0].policy=0;
    env->rng^=(unsigned int)swat_movement_setting(kwargs,"seed",2718,0,2147483647);
    env->stage=swat_movement_setting(kwargs,"stage",-1,-1,4);
    env->role=swat_movement_setting(kwargs,"role",-1,-1,1);
    env->max_steps=swat_movement_setting(kwargs,"max_steps",300,1,10000);
    env->progress_reward=swat_movement_reward(kwargs,"progress_reward",.15f);
    env->step_cost=swat_movement_reward(kwargs,"step_cost",.002f);
    env->success_reward=swat_movement_reward(kwargs,"success_reward",2);
    env->fall_penalty=swat_movement_reward(kwargs,"fall_penalty",2);
    env->course=swat_training_create(env->rng,0,0);
    if(!env->course) { fprintf(stderr,"swat movement: course allocation failed\n"); exit(1); }
}
void puf_reset(Env* env) {
    int stage=env->stage<0 ? (int)(swat_random(&env->rng)%5) : env->stage;
    int role=env->role<0 ? (int)(swat_random(&env->rng)%2) : env->role;
    swat_training_reset(env->course,swat_random(&env->rng),role,stage);
    swat_training_limit(env->course,env->max_steps);
    swat_training_rewards(env->course,env->progress_reward,env->step_cost,env->success_reward,env->fall_penalty);
    env->steps=0; env->episode_return=0;
    swat_training_observe(env->course,env->agents[0].observations);
}
void puf_step(Env* env) {
    float reward=0;
    int terminal=swat_training_step(env->course,env->agents[0].actions,env->agents[0].observations,&reward);
    env->steps++; env->episode_return+=reward;
    *env->agents[0].rewards=reward; *env->agents[0].terminals=terminal!=0;
    if(terminal) {
        env->log.perf+=terminal==1; env->log.score+=terminal==1;
        env->log.episode_return+=env->episode_return; env->log.episode_length+=env->steps; env->log.n++;
        puf_reset(env);
    }
}
void puf_log(Log* log,Dict* out) {
    dict_set(out,"perf",log->perf); dict_set(out,"score",log->score);
    dict_set(out,"episode_return",log->episode_return); dict_set(out,"episode_length",log->episode_length); dict_set(out,"n",log->n);
}
void puf_render(Env* env) {
#ifndef SWAT_MOVEMENT_NO_RENDER
    if(!env->view) {
        env->view=(SwatView*)calloc(1,sizeof(SwatView));
        if(!env->view) exit(1);
        swat_view_init(env->view,false);
    }
    SwatSim* sim=swat_training_sim(env->course);
    env->view->actor=swat_training_actor(env->course);
    BeginDrawing(); swat_view_draw(env->view,sim,true,70); EndDrawing();
#endif
}
void puf_close(Env* env) {
#ifndef SWAT_MOVEMENT_NO_RENDER
    if(env->view) { swat_view_close(env->view); free(env->view); env->view=NULL; }
#endif
    swat_training_close(env->course); env->course=NULL;
}
#endif
