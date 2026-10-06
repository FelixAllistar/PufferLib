// Included by the player after src/puffercpu.c. Native PufferNet weights and
// inference are shared with the trainer; no separate Python/MLP format.
#ifndef SWAT_LOCOMOTION_POLICY_H
#define SWAT_LOCOMOTION_POLICY_H
typedef struct SwatNativeMovement {
    Weights* weights;
    PufferNet* actors[SWAT_MAX_ACTORS];
    const SwatSim* scene;
    int episode,last_tick[SWAT_MAX_ACTORS],next_tick[SWAT_MAX_ACTORS];
    float held[SWAT_MAX_ACTORS][SWAT_LOCOMOTION_HEADS];
} SwatNativeMovement;

static bool swat_native_movement_forward(void* pointer,const SwatSim* sim,int actor,
                                        const float obs[SWAT_LOCOMOTION_OBS],float action[SWAT_LOCOMOTION_HEADS]) {
    SwatNativeMovement* policy=pointer;
    if(!policy || !sim || actor<0 || actor>=SWAT_MAX_ACTORS) return false;
    for(int i=0;i<SWAT_LOCOMOTION_OBS;i++) if(!isfinite(obs[i])) return false;
    if(policy->scene!=sim || policy->episode!=sim->episode) {
        policy->scene=sim; policy->episode=sim->episode;
        for(int i=0;i<SWAT_MAX_ACTORS;i++) {
            float terminal=1; mingru_zero_term(policy->actors[i]->mingru,&terminal);
            policy->next_tick[i]=0; policy->last_tick[i]=-1;
        }
    }
    PufferNet* net=policy->actors[actor];
    if(sim->tick<policy->last_tick[actor]) {
        float terminal=1; mingru_zero_term(net->mingru,&terminal); policy->next_tick[actor]=sim->tick;
    }
    if(sim->tick>=policy->next_tick[actor]) {
        linear(net->encoder,(float*)obs); mingru(net->mingru,net->encoder->output);
        linear(net->decoder,net->mingru->output);
        multidiscrete(net->multidiscrete,net->decoder->output,policy->held[actor],1,NULL);
        policy->next_tick[actor]=sim->tick+4;
    }
    policy->last_tick[actor]=sim->tick;
    memcpy(action,policy->held[actor],sizeof(policy->held[actor])); return true;
}
static void swat_native_movement_free(SwatNativeMovement* policy) {
    if(!policy) return;
    swat_locomotion_set_policy(NULL,NULL);
    for(int i=0;i<SWAT_MAX_ACTORS;i++) if(policy->actors[i]) free_puffernet(policy->actors[i]);
    free(policy->weights); memset(policy,0,sizeof(*policy));
}
static bool swat_native_movement_load(SwatNativeMovement* policy,const char* path,int hidden,int layers) {
    if(!policy || !path || hidden<8 || hidden>1024 || hidden%8 || layers<1 || layers>16) return false;
    long long expected=(long long)SWAT_LOCOMOTION_OBS*hidden+(long long)(SWAT_LOCOMOTION_LOGITS+1)*hidden+(long long)layers*3*hidden*hidden;
    FILE* file=fopen(path,"rb"); if(!file) return false;
    if(fseek(file,0,SEEK_END)!=0 || ftell(file)!=expected*(long long)sizeof(float)) { fclose(file); return false; }
    fclose(file);
    Weights* weights=load_weights(path); if(!weights || weights->size-7!=expected) { free(weights); return false; }
    for(int i=0;i<expected;i++) if(!isfinite(weights->data[i])) { free(weights); return false; }
    SwatNativeMovement next={0}; next.weights=weights;
    int sizes[]=SWAT_LOCOMOTION_ACTION_SIZES;
    for(int i=0;i<SWAT_MAX_ACTORS;i++) {
        weights->idx=0; next.actors[i]=make_puffernet(weights,1,SWAT_LOCOMOTION_OBS,hidden,layers,sizes,SWAT_LOCOMOTION_HEADS);
    }
    swat_native_movement_free(policy); *policy=next;
    swat_locomotion_set_policy(swat_native_movement_forward,policy); return true;
}
#endif
