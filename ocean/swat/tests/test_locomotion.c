#include "locomotion.h"
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
int main(int argc,char** argv) {
    curriculum();
    puts("PASS locomotion: authoritative stairs/crouch/door/breach traversal for both roles, deterministic resets, terminal reward once");
    return 0;
}
