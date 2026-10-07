#include <assert.h>
#include "../replay.c"

static KagBCReplay replay;
static float labels[KAG_ACTION_HEADS], history[KAG_ACTION_HEADS];
static unsigned char mask[KAG_ALL_LOGITS];

static void reset(void) {
    memset(&replay,0,sizeof(replay));
    KGConfig config; kg_config_default(&config);
    kg_init(&replay.env.game,&config);
    replay.env.policy.market_slots = 10;
    replay.env.policy.max_hands = 19;
    kag_policy_reset(&replay.env.policy,&replay.env.game,0);
}

static void project(KGAction* action) {
    int counts[3] = {0};
    assert(kag_direct_project(&replay,0,action,labels,history,mask,counts));
    assert(counts[0]+counts[1]+counts[2] == KAG_ACTION_HEADS);
    KagActionMaskState s;
    kag_action_mask_begin(&s,&replay.env.game,&replay.env.policy,0);
    unsigned char check[KAG_ALL_LOGITS];
    for (int h = 0; h < KAG_ACTION_HEADS; h++) {
        int off = kag_direct_offset(h), width = kag_direct_width(h), support = 0;
        kag_action_mask_before(&s,h,check);
        assert(!memcmp(check+off,mask+off,width));
        for (int i = 0; i < width; i++) support += mask[off+i];
        assert(mask[off+(int)history[h]]);
        if (support == 1) assert(labels[h] == -1);
        if (labels[h] >= 0) assert(labels[h] == history[h]);
        kag_action_mask_commit(&s,h,(int)history[h]);
    }
}

int main(void) {
    reset();
    KGPlayer* f = &replay.env.game.players[0];
    f->units[0].x = f->units[0].y = 4;
    f->shed[8] = 12;
    KGAction action = {0};
    action.farmer = (KGUnitAction){KG_OP_PICKUP,8,7};
    action.market_count = 2;
    action.market[0] = (KGMarketOrder){-1,-1,1};
    action.market[1] = (KGMarketOrder){KG_MARKET_BUY_SEED,0,3};
    project(&action);
    assert(labels[0] == 13 && labels[1] == 6);
    assert(labels[40] == 0 && labels[41] == -1);
    assert(labels[42] == 1 && labels[43] == 2);

    // Unsupported quantities do not teach a partial fallback action.
    action.farmer.n = 21;
    action.market[1].n = 101;
    project(&action);
    assert(labels[0] == -1 && labels[1] == -1 && history[0] == 0);
    assert(labels[42] == -1 && labels[43] == -1 && history[42] == 0);

    // "All" pickup and SELL are resolved from the same resource prefix.
    action.farmer.n = -1;
    action.market[1] = (KGMarketOrder){KG_MARKET_SELL,4,-1};
    f->shed[4] = 9;
    project(&action);
    assert(labels[0] == 13 && labels[1] == 11);
    assert(labels[42] == KAG_DIRECT_SELL+4 && labels[43] == 8);

    // Forced final actions ignore both command and quantity teacher targets.
    replay.env.game.step = 718; replay.env.game.day = 29; replay.env.game.hour = 22;
    kg_inventory_add(&f->units[0],4,5);
    project(&action);
    assert(labels[0] == -1 && labels[1] == -1 && history[0] == KAG_DIRECT_DROP);
    for (int h = 40; h < KAG_ACTION_HEADS; h++) assert(labels[h] == -1);

    // Architecture-independent rollout sampling uses the same prefix argmax.
    reset();
    float logits[KAG_ALL_LOGITS], expected[KAG_ACTION_HEADS], sampled[KAG_ACTION_HEADS], rewards[2];
    for (int i=0;i<KAG_ALL_LOGITS;i++) logits[i] = ((i*13)%43-21)/8.0f;
    KagActionMaskState prefix;
    kag_action_mask_begin(&prefix,&replay.env.game,&replay.env.policy,0);
    for (int h=0;h<KAG_ACTION_HEADS;h++) {
        kag_action_mask_before(&prefix,h,mask);
        int off=kag_direct_offset(h), width=kag_direct_width(h), best=-1;
        for (int i=0;i<width;i++) if(mask[off+i] && (best<0 || logits[off+i]>logits[off+best])) best=i;
        assert(best>=0); expected[h]=best;
        kag_action_mask_commit(&prefix,h,best);
    }
    unsigned int rng=17;
    assert(kag_direct_sample_step(&replay,0,logits,1,&rng,sampled,rewards));
    assert(!memcmp(expected,sampled,sizeof(expected)) && rng==17 && replay.env.game.step==1);
    puts("compact BC: quantities, filtered commands, forced targets and prefix parity PASS");
}
