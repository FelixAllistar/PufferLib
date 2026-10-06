#define SWAT_MOVEMENT_NO_RENDER
#include "../movement_env.h"
#include "../../../src/puffercpu.c"
#include "../locomotion_policy.h"
#include <assert.h>

static void adapter(void) {
    Dict options={0}; dict_set(&options,"max_steps",1); dict_set(&options,"stage",2); dict_set(&options,"role",1);
    Env env={0}; env.rng=73; puf_init(&env,&options);
    float obs[OBS_SIZE],actions[NUM_ATNS]={2,1,1,0,0,0},reward,terminal;
    env.agents[0].observations=obs; env.agents[0].actions=actions;
    env.agents[0].rewards=&reward; env.agents[0].terminals=&terminal;
    puf_reset(&env); assert(obs[10]==0); puf_step(&env);
    assert(terminal==1 && env.log.n==1 && env.log.episode_length==1 && env.log.perf==0);
    assert(swat_training_sim(env.course)->tick==0);
    for(int i=0;i<OBS_SIZE;i++) assert(isfinite(obs[i]));
    puf_step(&env); assert(terminal==1 && env.log.n==2);
    puf_close(&env); dict_clear(&options);
}
static void native_weights(const char* path) {
    enum { H=8,L=1,N=32*H+19*H+3*H*H };
    FILE* file=fopen(path,"wb"); assert(file);
    for(int i=0;i<N;i++) { float weight=.07f*sinf(i*.71f); assert(fwrite(&weight,sizeof(weight),1,file)==1); }
    fclose(file);
    SwatNativeMovement policy={0}; assert(swat_native_movement_load(&policy,path,H,L));
    int sizes[]=SWAT_LOCOMOTION_ACTION_SIZES;
    Weights* storage=load_weights(path); PufferNet* reference=make_puffernet(storage,1,32,H,L,sizes,6);
    SwatSim* sim=calloc(1,sizeof(*sim)); assert(sim); sim->episode=1;
    float obs[32],action[6],expected[6];
    for(int decision=0;decision<20;decision++) {
        sim->tick=decision*4;
        for(int i=0;i<32;i++) obs[i]=sinf(decision+i*.3f);
        linear(reference->encoder,obs); mingru(reference->mingru,reference->encoder->output); linear(reference->decoder,reference->mingru->output);
        multidiscrete(reference->multidiscrete,reference->decoder->output,expected,1,NULL);
        assert(swat_native_movement_forward(&policy,sim,2,obs,action));
        assert(!memcmp(expected,action,sizeof(action)));
        assert(!memcmp(reference->decoder->output,policy.actors[2]->decoder->output,19*sizeof(float)));
        float prior[19]; memcpy(prior,policy.actors[2]->decoder->output,sizeof(prior));
        sim->tick++; obs[0]+=.25f;
        assert(swat_native_movement_forward(&policy,sim,2,obs,action));
        assert(!memcmp(prior,policy.actors[2]->decoder->output,sizeof(prior)));
    }
    // Each actor has its own recurrent state; a new episode clears all state.
    float terminal=1; mingru_zero_term(reference->mingru,&terminal);
    linear(reference->encoder,obs); mingru(reference->mingru,reference->encoder->output); linear(reference->decoder,reference->mingru->output);
    assert(swat_native_movement_forward(&policy,sim,3,obs,action));
    assert(!memcmp(reference->decoder->output,policy.actors[3]->decoder->output,19*sizeof(float)));
    sim->episode++; sim->tick=0;
    assert(swat_native_movement_forward(&policy,sim,2,obs,action));
    assert(!memcmp(reference->decoder->output,policy.actors[2]->decoder->output,19*sizeof(float)));
    PufferNet* previous=policy.actors[2];
    file=fopen(path,"ab"); assert(file); fputc('x',file); fclose(file);
    assert(!swat_native_movement_load(&policy,path,H,L) && policy.actors[2]==previous);
    file=fopen(path,"wb"); assert(file);
    for(int i=0;i<N;i++) { float weight=i==10 ? NAN : 0; fwrite(&weight,sizeof(weight),1,file); } fclose(file);
    assert(!swat_native_movement_load(&policy,path,H,L) && policy.actors[2]==previous);
    swat_native_movement_free(&policy); free_puffernet(reference); free(storage); free(sim); remove(path);
}
int main(int argc,char** argv) {
    adapter(); native_weights(argc>1 ? argv[1] : "movement-native-test.bin");
    puts("PASS native movement: Ocean adapter terminal/reset, exact PufferNet inference, per-actor recurrent isolation, four-tick cadence, episode reset and atomic checkpoint rejection");
    return 0;
}
