/* Test-only: full-state oracle never included in a submission. */
#include "entity_bridge.c"
#include "src/puffercpu.h"
API void* oracle_model(const char* path) {
    Weights* w=load_weights(path);
    if(!w || w->size-7!=kag_parameter_count(256,2,8))return NULL;
    return kag_cpu_make(w,1,256,2,8);
}
API void oracle_forward(KagCpuPolicy* model,float* obs,float* out) {
    memcpy(out,kag_cpu_forward(model,obs),1979*sizeof(float));
}
API void oracle_rng(Export* x,uint32_t rng) { x->env.rng=rng; }
API Export* oracle_create(int seed) {
    Export* x=export_create();KGConfig c;kg_config_default(&c);c.seed=seed;
    kg_init(&x->env.game_storage,&c);
    for(int p=0;p<2;p++){kag_reward_reset(&x->env,p);kag_reset_land_buy_delay(&x->env,p);}
    return x;
}
API const char* oracle_snapshot(Export* x){return kg_snapshot_json(&x->env.game_storage);}
API void oracle_string_free(const char* x){kg_free_string(x);}
API void oracle_view(Export* x,int p,float* out) {
    x->env.agents[p].observations=out;kag_write_observation(&x->env,p);
}
API void oracle_step(Export* x,KGAction* actions) {
    kg_step(&x->env.game_storage,actions);
    for(int p=0;p<2;p++){kag_reward_step(&x->env,p,kg_done(&x->env.game_storage));kag_update_land_buy_delay(&x->env,p);}
}
