/* Public-observation adapter. Reuses the production encoder, prefix sampler,
 * and executor; never reconstructs an opponent's private inventory. */
#include "ocean/kaggriculture/kaggriculture.h"
#define API __attribute__((visibility("default")))
typedef struct { Env env; float obs[OBS_SIZE], actions[NUM_ATNS];
    unsigned char mask[KG_POLICY_ACTION_MASK_SIZE]; int last_step; } Export;
API void* export_create(void) {
    Export* x=calloc(1,sizeof(*x)); if(!x)return NULL; Env* e=&x->env;
    e->num_agents=2; e->macro_mode=2; e->macro_executor_version=2;
    e->observation_version=3; e->macro_decision_interval=1;
    e->frozen_macro_mode=e->frozen_macro_executor_version=-1;
    e->frozen_observation_version=e->frozen_macro_decision_interval=-1;
    e->frozen_macro_score_features=-1;
    e->policy_market_slots=10; e->policy_max_hands=16; e->macro_score_scale=10000;
    kg_config_default(&e->game_storage.config); e->rng=97; x->last_step=-1;
    return x;
}
API void export_free(Export* x) { free(x); }
API uint32_t export_rng(Export* x) { return x->env.rng; }
API void export_begin(Export* x,int step,int day,int hour) {
    KGConfig c=x->env.game_storage.config;
    memset(&x->env.game_storage,0,sizeof(KGState));
    x->env.game_storage.config=c;
    x->env.game_storage.step=step;x->env.game_storage.day=day;x->env.game_storage.hour=hour;
}
/* All fields cross a fixed int32 interface, not compiler-dependent structs. */
API void export_farm(Export* x,int p,const int* v) {
    KGPlayer* f=&x->env.game_storage.players[p];
    f->money=v[0];f->unlocked_mask=v[1];f->hires_today=v[2];
    f->hand_count=v[3];f->unit_count=v[3]+1;
    for(int u=0;u<f->unit_count;u++) kg_set_unit_position(f,u,v[4+2*u],v[5+2*u]);
}
API void export_tile(Export* x,int p,int i,const int* v) {
    KGPlayer* f=&x->env.game_storage.players[p];KGTile* t=&f->tiles[i];
    kg_set_player_tile(f,i,v[0]);
    t->crop=v[1];t->animal=v[2];t->planted_day=v[3];t->placed_day=v[4];
    t->fertilized_until_day=v[5];t->watered_today=v[6];t->consecutive_unwatered=v[7];
    t->yield_units=v[8];t->max_lifespan_step=v[9];t->consecutive_unfed=v[10];
    t->fed_today=v[11];t->cared_today=v[12];t->fertilizer_available=v[13];t->pending_care_bonus=v[14];
}
API void export_private(Export* x,int p,const int* shed,const int* seeds) {
    memcpy(x->env.game_storage.players[p].shed,shed,12*sizeof(int));
    memcpy(x->env.game_storage.players[p].seeds,seeds,5*sizeof(int));
}
API void export_inventory(Export* x,int p,int u,int item,int n) {
    kg_inventory_add(&x->env.game_storage.players[p].units[u],item,n);
}
API void export_market(Export* x,const int* inventory,const int* prices,const int* shops,int count) {
    KGState* g=&x->env.game_storage;memcpy(g->market.inventory,inventory,9*sizeof(int));
    memcpy(g->market.prices,prices,9*sizeof(int));g->shop_count=count;
    memcpy(g->unlocked_shops,shops,count*sizeof(int));
}
API int export_view(Export* x,int p,float* out) {
    Env* e=&x->env;int step=e->game_storage.step;
    if(x->last_step<0) {
        if(step!=0)return 0; /* History cannot be invented for a mid-game start. */
        kag_reward_reset(e,p);kag_reset_land_buy_delay(e,p);
    } else if(step==x->last_step+1) {
        kag_reward_step(e,p,0);kag_update_land_buy_delay(e,p);
    } else if(step!=x->last_step)return 0;
    x->last_step=step;e->agents[p].observations=out;
    kag_write_observation(e,p);return 1;
}
API void export_action(Export* x,int p,const float* logits,int deterministic,KGAction* out) {
    Env* e=&x->env;e->agents[p].actions=x->actions;e->agents[p].action_mask=x->mask;
    kag_write_mask(e,p);kag_sample_cpu_logits(e,p,logits,deterministic);
    kag_decode_policy_action(out,&e->agents[p],&e->game_storage,p,e);
}
API void export_debug(Export* x,float* actions,unsigned char* mask) {
    memcpy(actions,x->actions,sizeof(x->actions));memcpy(mask,x->mask,sizeof(x->mask));
}
