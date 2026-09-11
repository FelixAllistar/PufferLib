#include <assert.h>
#include "../kag_experiment_view.c"

int main(void) {
    KGConfig cfg; kg_config_default(&cfg); cfg.weed_spawn_chance=0;
    KGState* g=kg_create(&cfg);
    Env* ctx=kg_experiment_context_create(1,1,1,0);
    assert(ctx);
    unsigned char obs[OBS_SIZE], mask[KG_POLICY_ACTION_MASK_SIZE];
    kg_experiment_context_view(ctx,g,0,obs,mask);
    assert(!mask[KAG_MACRO_DIVERSIFY]);
    kg_experiment_context_view(ctx,g,1,obs,mask);
    assert(mask[KAG_MACRO_DIVERSIFY]);
    float requests[NUM_ATNS]={0}; requests[0]=KAG_MACRO_ANIMAL_BASE+KG_COW;
    requests[1]=1; requests[2]=1;
    KGAction actions[2]={0};
    kg_experiment_context_action(ctx,g,0,requests,&actions[0]);
    assert(ctx->macro_intent[0]==KAG_MACRO_ANIMAL_BASE+KG_COW);
    assert(ctx->macro_quantity[0]==2 && ctx->macro_target[0]==1);
    kg_step(g,actions);
    kg_experiment_context_view(ctx,g,0,obs,mask);
    unsigned char direct[OBS_SIZE];
    ctx->agents[0].observations=direct;
    kag_write_observation(ctx,0);
    assert(memcmp(direct,obs,OBS_SIZE)==0);
    /* Capability gate only: explicit requests plus automatic operations must
     * actually establish and milk cows. This is not learned-policy evidence. */
    while(!kg_done(g)) {
        memset(actions,0,sizeof(actions));
        memset(requests,0,sizeof(requests));
        int turn=kg_state_step(g);
        requests[0]=turn<30 ? KAG_MACRO_ANIMAL_BASE+KG_COW : KAG_MACRO_HOLD;
        requests[1]=1;
        if(turn%24==0)requests[0]=KAG_MACRO_HIRE;
        else if(turn%24==8)requests[0]=KAG_MACRO_CASH_OUT;
        kg_experiment_context_action(ctx,g,0,requests,&actions[0]);
        kg_step(g,actions);
    }
    assert(g->production_product_units[0][KG_ITEM_MILK]>0);
    printf("PASS context: persistent macro state, per-policy versions, explicit cows produce %.0f milk\n",
        (double)g->production_product_units[0][KG_ITEM_MILK]);
    kg_experiment_context_free(ctx); kg_destroy(g);
    return 0;
}
