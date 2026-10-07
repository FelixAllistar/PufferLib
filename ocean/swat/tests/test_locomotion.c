#include "locomotion.h"
#include "sim.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void curriculum(void) {
    assert(!swat_training_create(1,2,0) && !swat_training_create(1,0,5));
    for(int stage=0;stage<5;stage++) for(int role=0;role<2;role++) {
        void* env=swat_training_create(73,role,stage); assert(env);
        SwatSim* sim=swat_training_sim(env); int learners=0;
        for(int i=0;i<sim->actor_count;i++) learners+=sim->actors[i].present;
        assert(learners==1 && sim->actors[swat_training_actor(env)].role==(SwatRole)role);
        float obs[32],initial[32],again[32],reward=0,actions[6]={2,1,1,0,0,0};
        swat_training_observe(env,initial);
        for(int i=0;i<32;i++) assert(isfinite(initial[i]));
        assert(initial[10]==(role==0));
        swat_training_step(env,actions,obs,&reward); assert(isfinite(reward));
        swat_training_reset(env,73,role,stage); swat_training_observe(env,again);
        assert(!memcmp(initial,again,sizeof(initial)));
        int status=0;
        for(int step=0;step<300 && !status;step++) {
            float error=atan2f(again[13],again[12]);
            actions[0]=error<-.07f ? 0 : error<-.015f ? 1 : error>.07f ? 4 : error>.015f ? 3 : 2;
            actions[1]=fabsf(error)<.9f ? 2 : 1;
            actions[3]=again[26]<.65f && again[21]>again[26]+.08f;
            status=swat_training_step(env,actions,again,&reward);
            assert(isfinite(reward)); for(int i=0;i<32;i++) assert(isfinite(again[i]));
        }
        assert(status==1 && reward>1);
        assert(!swat_training_sim(env)->navigation);
        assert(swat_training_step(env,actions,again,&reward)==2 && reward==0);
        swat_training_close(env);
    }
}
static void clearance_filter(void) {
    uint32_t random=73;
    for(int stage=0;stage<5;stage++) {
        void* env=swat_training_create(73,0,stage); assert(env);
        SwatSim* sim=swat_training_sim(env); SwatBody* body=&sim->actors[0].controller.body;
        b3Pos feet=swat_body_feet_position(body);
        swat_sim_spawn_actor(sim,1,SWAT_OFFICER,b3OffsetPos(feet,swat_v(1.5f,0,0)),0); sim->actor_count=2;
        uint64_t filtered=body->queryMask;
        assert(filtered!=UINT64_MAX && filtered!=sim->actors[1].controller.body.queryMask);
        for(int i=0;i<200;i++) {
            b3Pos from=b3OffsetPos(feet,swat_v(0,swat_rand01(&random)*.25f,0));
            b3Pos to=b3OffsetPos(from,swat_v((swat_rand01(&random)-.5f)*6,(swat_rand01(&random)-.5f)*2,(swat_rand01(&random)-.5f)*6));
            float radius=.7f+.3f*swat_rand01(&random),height=.5f+.5f*swat_rand01(&random);
            SwatTraceResult fast=swat_body_trace_body(body,from,to,radius,height);
            body->queryMask=UINT64_MAX;
            SwatTraceResult reference=swat_body_trace_body(body,from,to,radius,height);
            body->queryMask=filtered;
            assert(!memcmp(&fast,&reference,sizeof(fast)));
        }
        SwatTraceResult other=swat_body_trace_body(body,feet,b3OffsetPos(feet,swat_v(2,0,0)),1,1);
        assert(other.hit && B3_ID_EQUALS(b3Shape_GetBody(other.shapeId),sim->actors[1].controller.body.body));
        swat_training_close(env);
    }
    puts("PASS 1000 character sweeps: broad-phase self exclusion matches callback filtering exactly and still hits other actors");
}
static void movement_step_equivalence(void) {
    uint32_t random=19; const int sizes[]=SWAT_LOCOMOTION_ACTION_SIZES;
    for(int stage=0;stage<5;stage++) for(int role=0;role<2;role++) {
        void* course=swat_training_create(73,role,stage);
        void* reference=swat_training_create(73,role,stage);
        float obs[32],expected[32],reward,action[6]; int episodes=0;
        swat_training_observe(course,obs);
        for(int step=0;step<600;step++) {
            float error=atan2f(obs[13],obs[12]);
            action[0]=error<-.07f ? 0 : error<-.015f ? 1 : error>.07f ? 4 : error>.015f ? 3 : 2;
            action[1]=fabsf(error)<.9f ? 2 : 1; action[2]=1;
            action[3]=obs[26]<.65f && obs[21]>obs[26]+.08f; action[4]=0; action[5]=1;
            if(step>=300) for(int j=0;j<6;j++) action[j]=swat_random(&random)%sizes[j];
            SwatSim* sim=swat_training_sim(reference); int actor=swat_training_actor(reference);
            SwatInput inputs[SWAT_MAX_ACTORS];
            for(int i=0;i<SWAT_MAX_ACTORS;i++) inputs[i]=swat_neutral_input();
            inputs[actor]=swat_locomotion_decode(action);
            for(int frame=0;frame<4;frame++) {
                SwatHit hit=swat_context_hit(sim,actor,1.7f);
                inputs[actor].interact=hit.kind==SWAT_HIT_WORLD && sim->world.objects[hit.index].door &&
                    !sim->world.objects[hit.index].door_open && !sim->actors[actor].last_interact;
                swat_sim_step_inputs(sim,inputs);
            }
            int terminal=swat_training_step(course,action,obs,&reward);
            swat_training_observe(reference,expected);
            assert(!memcmp(obs,expected,sizeof(obs)));
            assert(sim->tick==swat_training_sim(course)->tick && sim->end==swat_training_sim(course)->end);
            if(terminal) {
                uint32_t seed=73+(++episodes);
                swat_training_reset(course,seed,role,stage); swat_training_reset(reference,seed,role,stage);
                swat_training_observe(course,obs);
            }
        }
        swat_training_close(course); swat_training_close(reference);
    }
    puts("PASS 6000 movement decisions: optimized courses exactly match full game stepping across both roles and all five stages");
}
int main(int argc,char** argv) {
    clearance_filter(); curriculum(); movement_step_equivalence();
    puts("PASS locomotion: authoritative stairs/crouch/door/breach traversal for both roles, deterministic resets, terminal reward once");
    return 0;
}
