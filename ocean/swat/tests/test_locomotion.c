#include "locomotion.h"
#include "sim.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void curriculum(void) {
    assert(!swat_training_create(1,2,0) && !swat_training_create(1,0,5));
    for(int stage=0;stage<5;stage++) for(int role=0;role<2;role++) {
        void* env=swat_training_create(73,role,stage); assert(env);
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
int main(int argc,char** argv) {
    clearance_filter(); curriculum();
    puts("PASS locomotion: authoritative stairs/crouch/door/breach traversal for both roles, deterministic resets, terminal reward once");
    return 0;
}
