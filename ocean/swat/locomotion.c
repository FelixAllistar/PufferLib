#include "locomotion.h"
#include "sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static SwatMovementPolicy movement_policy;
static void* movement_context;
void swat_locomotion_set_policy(SwatMovementPolicy policy,void* context) {
    movement_policy=policy; movement_context=context;
}
void swat_locomotion_observe(const SwatSim* s,int actor,b3Pos goal,float out[SWAT_LOCOMOTION_OBS]) {
    memset(out,0,SWAT_LOCOMOTION_OBS*sizeof(float));
    const SwatActor* a=&s->actors[actor]; const SwatController* c=&a->controller;
    b3Pos feet=swat_body_feet_position(&c->body); b3Vec3 d=b3SubPos(goal,feet),v=b3Body_GetLinearVelocity(c->body.body);
    b3Vec3 forward=swat_direction(c->yaw,0),right=swat_controller_right(c); float distance=b3Length(d);
    out[0]=swat_clamp(b3Dot(d,forward)/6,-1,1); out[1]=swat_clamp(b3Dot(d,right)/6,-1,1); out[2]=swat_clamp(d.y/3,-1,1); out[3]=fminf(distance/12,1);
    out[4]=b3Dot(v,forward)/5; out[5]=b3Dot(v,right)/5; out[6]=swat_clamp(v.y/15,-1,1);
    out[7]=c->body.onGround; out[8]=c->body.crouched; out[9]=c->stamina; out[10]=a->role==SWAT_OFFICER;
    out[11]=distance>.45f; out[12]=d.x*d.x+d.z*d.z>1e-8f ? b3Dot(swat_normalize(swat_v(d.x,0,d.z)),forward) : 1;
    out[13]=d.x*d.x+d.z*d.z>1e-8f ? b3Dot(swat_normalize(swat_v(d.x,0,d.z)),right) : 0;
    b3Vec3 directions[5];
    for(int col=0;col<5;col++) directions[col]=swat_direction(c->yaw+(col-2)*30*SWAT_RAD,0);
    for(int row=0;row<3;row++) for(int col=0;col<5;col++) {
        float height=fminf(.30f+row*.65f,c->body.totalHeight-.10f);
        SwatHit hit=swat_world_ray(&s->world,b3OffsetPos(feet,swat_v(0,height,0)),directions[col],3,c->body.body);
        out[14+row*5+col]=hit.hit ? hit.distance/3 : 1;
    }
    for(int i=0;i<3;i++) {
        b3Pos probe=b3OffsetPos(feet,swat_add(swat_mul(forward,.6f*(i+1)),swat_v(0,.6f,0)));
        SwatHit hit=swat_world_ray(&s->world,probe,swat_v(0,-1,0),1.2f,c->body.body);
        out[29+i]=hit.kind==SWAT_HIT_WORLD ? hit.distance/1.2f : 1;
    }
}
static int action_value(float value,int limit) { return isfinite(value) ? (int)swat_clamp(value,0,(float)limit) : 0; }
SwatInput swat_locomotion_decode(const float a[SWAT_LOCOMOTION_HEADS]) {
    const float yaw[]={-6,-1,0,1,6}; SwatInput in=swat_neutral_input();
    in.yaw_delta=yaw[action_value(a[0],4)]*SWAT_RAD; in.forward=action_value(a[1],2)-1; in.strafe=action_value(a[2],2)-1;
    in.crouch=action_value(a[3],1); in.jump=action_value(a[4],1); in.gait=(SwatGait)action_value(a[5],2); return in;
}
bool swat_locomotion_input(const SwatSim* s,int actor,b3Pos goal,SwatInput* input) {
    if(!movement_policy || !input || actor<0 || actor>=s->actor_count || !s->actors[actor].present) return false;
    float obs[SWAT_LOCOMOTION_OBS],action[SWAT_LOCOMOTION_HEADS];
    swat_locomotion_observe(s,actor,goal,obs);
    if(s->actors[actor].role!=SWAT_OFFICER && s->actors[actor].role!=SWAT_SUSPECT) return false;
    if(!movement_policy(movement_context,s,actor,obs,action)) return false;
    SwatInput movement=swat_locomotion_decode(action);
    input->yaw_delta=movement.yaw_delta; input->forward=movement.forward; input->strafe=movement.strafe;
    input->crouch=movement.crouch; input->jump=movement.jump; input->gait=movement.gait; return true;
}

typedef struct Training {
    SwatSim sim; b3Pos goal; int actor,steps,limit;
    float distance,progress_reward,step_cost,success_reward,fall_penalty; bool done;
    int doors[SWAT_MAX_OBJECTS],door_count,object_count,generation;
} Training;
static bool training_door_nearby(Training* env) {
    const SwatWorld* w=&env->sim.world;
    if(env->object_count!=w->count || env->generation!=w->generation) {
        env->door_count=0; env->object_count=w->count; env->generation=w->generation;
        for(int i=0;i<w->count;i++) if(w->objects[i].door) env->doors[env->door_count++]=i;
    }
    if(!env->door_count || env->sim.actors[env->actor].last_interact) return false;
    b3Pos eye=swat_controller_eye(&env->sim.actors[env->actor].controller);
    for(int i=0;i<env->door_count;i++) {
        const SwatObject* door=&w->objects[env->doors[i]];
        if(!door->active || door->door_open) continue;
        // A conservative sphere contains the door at every rotation. Outside
        // it no sample of the original 1.7m interaction cone can hit the door.
        b3Vec3 delta=b3SubPos(eye,door->center); float reach=1.701f+b3Length(door->half);
        if(b3Dot(delta,delta)<=reach*reach) return true;
    }
    return false;
}
void swat_training_reset(void* pointer,uint32_t seed,int role,int stage) {
    Training* env=pointer; if(!env || role<0 || role>1 || stage<0 || stage>4) return; swat_sim_close(&env->sim); memset(env,0,sizeof(*env));
    SwatSim* s=&env->sim; s->config=swat_default_config(); s->config.mission=SWAT_RANGE; s->config.hostile_fire=false; env->limit=300; s->config.max_ticks=1200; s->rng=seed ? seed : 1;
    env->progress_reward=.15f; env->step_cost=.002f; env->success_reward=env->fall_penalty=2;
    // Box3D reserves world slots from a process-wide pool. Pair creation with
    // the same lock used by swat_sim_reset/close; construction and stepping of
    // the reserved, separate worlds can still run in parallel.
#pragma omp critical(swat_world_lifecycle)
    { swat_world_init(&s->world); }
    swat_world_box(&s->world,(b3Pos){0,-.5f,0},swat_v(10,.5f,8),SWAT_CONCRETE,0);
    float offset=(swat_rand01(&s->rng)-.5f)*.6f; b3Pos start={-4,0,offset}; env->goal=(b3Pos){4,0,offset};
    if(stage==1) {
        for(int i=0;i<6;i++) swat_world_box(&s->world,(b3Pos){i*.5f+.25f,(i+1)*.1f,offset},swat_v(.25f,(i+1)*.1f,.9f),SWAT_WOOD,0);
        swat_world_box(&s->world,(b3Pos){4.5f,.6f,offset},swat_v(1.5f,.6f,.9f),SWAT_WOOD,0); env->goal.y=1.2f;
    } else if(stage==2) {
        swat_world_box(&s->world,(b3Pos){0,1.9f,offset},swat_v(1.2f,.75f,1.4f),SWAT_CONCRETE,0);
        for(int side=-1;side<=1;side+=2) swat_world_box(&s->world,(b3Pos){0,1.5f,offset+side*1.55f},swat_v(5,1.5f,.15f),SWAT_CONCRETE,0);
    } else if(stage==3 || stage==4) {
        swat_build_framed_wall(&s->world,(b3Pos){0,0,offset},0,16,2.8f,0,stage==3 ? 1.2f : 0,0,2.1f,false);
        if(stage==4) {
            int target=-1; float nearest=1e9f;
            for(int i=0;i<s->world.count;i++) if(s->world.objects[i].part==SWAT_PART_SKIN) {
                float d=b3Distance(s->world.objects[i].center,(b3Pos){0,1.3f,offset}); if(d<nearest) { nearest=d; target=i; }
            }
            if(target>=0) swat_world_breach(&s->world,target,(b3Pos){0,1.55f,offset});
        }
    }
    env->actor=role==SWAT_SUSPECT ? 1 : 0;
    swat_sim_spawn_actor(s,0,SWAT_OFFICER,env->actor ? (b3Pos){-7,0,6} : start,0);
    if(env->actor) swat_sim_spawn_actor(s,1,SWAT_SUSPECT,start,0);
    s->actor_count=env->actor+1; s->actors[env->actor].controller.yaw=(swat_rand01(&s->rng)-.5f)*2*SWAT_PI;
    SwatInput inputs[SWAT_MAX_ACTORS]; for(int i=0;i<SWAT_MAX_ACTORS;i++) inputs[i]=swat_neutral_input();
    for(int t=0;t<8;t++) swat_sim_step_inputs(s,inputs);
    s->tick=0; env->distance=b3Distance(swat_body_feet_position(&s->actors[env->actor].controller.body),env->goal);
}
void* swat_training_create(uint32_t seed,int role,int stage) {
    if(role<0 || role>1 || stage<0 || stage>4) return NULL;
    Training* env=calloc(1,sizeof(*env)); if(env) swat_training_reset(env,seed,role,stage); return env;
}
void swat_training_observe(void* pointer,float* observation) {
    Training* env=pointer; swat_locomotion_observe(&env->sim,env->actor,env->goal,observation);
}
int swat_training_step(void* pointer,const float* action,float* observation,float* reward) {
    Training* env=pointer; if(env->done) { *reward=0; swat_training_observe(env,observation); return 2; }
    SwatInput inputs[SWAT_MAX_ACTORS]; for(int i=0;i<SWAT_MAX_ACTORS;i++) inputs[i]=swat_neutral_input();
    inputs[env->actor]=swat_locomotion_decode(action);
    for(int frame=0;frame<4;frame++) {
        SwatActor* actor=&env->sim.actors[env->actor];
        inputs[env->actor].interact=false;
        if(training_door_nearby(env)) {
            SwatHit hit=swat_context_hit(&env->sim,env->actor,1.7f);
            inputs[env->actor].interact=hit.kind==SWAT_HIT_WORLD && env->sim.world.objects[hit.index].door && !env->sim.world.objects[hit.index].door_open && !actor->last_interact;
        }
        swat_sim_step_inputs(&env->sim,inputs);
    }
    env->steps++; b3Pos feet=swat_body_feet_position(&env->sim.actors[env->actor].controller.body); float distance=b3Distance(feet,env->goal);
    *reward=env->progress_reward*(env->distance-distance)-env->step_cost; env->distance=distance;
    int terminal=distance<.5f && fabs(feet.y-env->goal.y)<.3f ? 1 : env->steps>=env->limit || env->sim.end!=SWAT_RUNNING ? 2 : feet.y< -2 ? 3 : 0;
    if(terminal==1) *reward+=env->success_reward; else if(terminal==3) *reward-=env->fall_penalty;
    env->done=terminal!=0; swat_training_observe(env,observation); return terminal;
}
void swat_training_close(void* pointer) { Training* env=pointer; if(env) { swat_sim_close(&env->sim); free(env); } }
SwatSim* swat_training_sim(void* pointer) { Training* env=pointer; return env ? &env->sim : NULL; }
int swat_training_actor(void* pointer) { Training* env=pointer; return env ? env->actor : -1; }
void swat_training_limit(void* pointer,int steps) {
    Training* env=pointer; if(env && steps>=1 && steps<=10000) { env->limit=steps; env->sim.config.max_ticks=4*steps; }
}
void swat_training_rewards(void* pointer,float progress,float step_cost,float success,float fall_penalty) {
    Training* env=pointer;
    if(!env || !isfinite(progress) || !isfinite(step_cost) || !isfinite(success) || !isfinite(fall_penalty) ||
       progress<0 || step_cost<0 || success<0 || fall_penalty<0) return;
    env->progress_reward=progress; env->step_cost=step_cost; env->success_reward=success; env->fall_penalty=fall_penalty;
}
