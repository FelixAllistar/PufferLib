#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define KAG_DIRECT_POLICY 1
#define KAG_WITH_PAIRED_CRITIC 1
#include "../policy.h"
#include "../../kaggriculture/critic.h"

static KGState game;
static KagPolicy policy;
static unsigned char mask[KAG_ALL_LOGITS];
static float obs[KAG_ENTITY_OBS_SIZE], other[KAG_ENTITY_OBS_SIZE];
static float logits[KAG_ALL_LOGITS], actions[KAG_ACTION_HEADS];

static void reset(void) {
    KGConfig c; kg_config_default(&c); c.weed_spawn_chance = 0;
    kg_init(&game, &c);
    policy = (KagPolicy){.market_slots=10, .max_hands=19};
    kag_policy_reset(&policy,&game,0);
}

static void catalog(void) {
    for (int i = 0; i < 44; i++) for (int n = 1; n <= 20; n++) {
        KGUnitAction a = kag_direct_unit(i,n);
        assert(kag_direct_unit_id(a) == i);
        assert(a.n == (kag_direct_unit_quantity(i) ? n : 1));
    }
    for (int i = 0; i < 22; i++) for (int n = 1; n <= 100; n++) {
        KGMarketOrder a = kag_direct_market(i,n);
        assert(kag_direct_market_id(a) == i);
        assert(a.n == (kag_direct_market_quantity(i) ? n : 1));
    }
    assert(kag_direct_market(0,1).op == -1);
    assert(kag_direct_unit(500,1).op == KG_OP_PASS);
    const int sizes[] = KAG_ACTION_SIZES;
    assert(sizeof(sizes)/sizeof(*sizes) == KAG_ACTION_HEADS);
    int sum = 0; for (int h = 0; h < KAG_ACTION_HEADS; h++) {
        assert(sum == kag_direct_offset(h) && sizes[h] == kag_direct_width(h)); sum += sizes[h];
    }
    assert(sum == 2500 && sum == KAG_ALL_LOGITS && KAG_ENTITY_OBS_SIZE == 3000);
    assert(KAG_TRAIN_OBS_SIZE == 3256 && KAG_TRAIN_OBS_SIZE%8 == 0);
}

static void commit(KagActionMaskState* s, int command_head, int command, int n) {
    kag_action_mask_commit(s,command_head,command);
    kag_action_mask_commit(s,command_head+1,n-1);
}

static void singleton(KagActionMaskState* s, int h, int selected) {
    kag_action_mask_before(s,h,mask);
    for (int i = 0; i < kag_direct_width(h); i++)
        assert(mask[kag_direct_offset(h)+i] == (i == selected));
}

static void prefix(void) {
    reset();
    KGPlayer* f = &game.players[0];
    f->hand_count = 1; f->unit_count = 2;
    f->units[0].x = f->units[1].x = 0;
    f->units[0].y = f->units[1].y = 0;
    f->seeds[0] = 1;
    KagActionMaskState s; kag_action_mask_begin(&s,&game,&policy,0);
    kag_action_mask_before(&s,0,mask);
    assert(!mask[1] && !mask[4] && mask[2] && mask[3]);
    assert(mask[18]);
    kag_action_mask_commit(&s,0,18);
    singleton(&s,1,0);
    kag_action_mask_commit(&s,1,0);
    kag_action_mask_before(&s,2,mask);
    for (int c = 0; c < 5; c++) assert(!mask[kag_direct_offset(2)+18+c]);
    singleton(&s,4,KAG_DIRECT_PASS);
    singleton(&s,5,0);
    // A different tile still cannot spend the already-reserved seed.
    f->units[1].x = 1;
    kag_action_mask_before(&s,2,mask); assert(!mask[kag_direct_offset(2)+18]);
}

static void private_observation(void) {
    reset();
    kag_write_observation(&policy,&game,0,obs);
    game.players[1].shed[0] += 77;
    game.players[1].seeds[0] += 99;
    game.players[1].units[0].inventory[8] += 123;
    kag_write_observation(&policy,&game,0,other);
    assert(!memcmp(obs,other,sizeof(obs)));
    game.players[0].units[0].inventory[8] += 3;
    kag_write_observation(&policy,&game,0,other);
    assert(memcmp(obs,other,sizeof(obs)));
    for (int i = 0; i < KAG_ENTITY_OBS_SIZE; i++) assert(isfinite(other[i]));
}

static void forced_sales(void) {
    reset();
    game.step = game.config.episode_steps-2;
    game.day = game.step/24; game.hour = game.step%24;
    KGPlayer* f = &game.players[0];
    f->units[0].x = 4; f->units[0].y = 4;
    f->units[0].inventory[4] = 5;
    f->units[0].inventory_order[0] = 4;
    f->units[0].inventory_order_count = 1;
    f->shed[5] = 7;
    KagActionMaskState s; kag_action_mask_begin(&s,&game,&policy,0);
    singleton(&s,0,KAG_DIRECT_DROP);
    commit(&s,0,KAG_DIRECT_DROP,1);
    singleton(&s,1,0);
    assert(s.shed[4] == 5);
    int sales = 0;
    for (int h = 40; h < KAG_ACTION_HEADS; h++) {
        kag_action_mask_before(&s,h,mask);
        int off = kag_direct_offset(h), count=0, chosen=-1;
        for (int i = 0; i < kag_direct_width(h); i++) if (mask[off+i]) {count++;chosen=i;}
        assert(count == 1); // zero log probability, entropy and policy gradient
        sales += !(h%2) && chosen != 0;
        kag_action_mask_commit(&s,h,chosen);
    }
    assert(sales == 2 && s.shed[4] == 0 && s.shed[5] == 0);
}

static void budget(void) {
    reset();
    game.players[0].money = 10;
    KagActionMaskState s; kag_action_mask_begin(&s,&game,&policy,0);
    kag_action_mask_before(&s,40,mask);
    int off = kag_direct_offset(40);
    assert(mask[off+1]);
    kag_action_mask_commit(&s,40,1);
    singleton(&s,41,0); // one seed, not an unaffordable quantity
    kag_action_mask_commit(&s,41,0);
    assert(s.money == 0);
    kag_action_mask_before(&s,42,mask);
    assert(!mask[kag_direct_offset(42)+1]);
}

static void overflow_and_floor(void) {
    reset();
    game.hour=23;
    KGPlayer* f=&game.players[0];
    memset(f->shed,0,sizeof(f->shed));
    f->shed[0]=50; f->shed[6]=40;
    kg_inventory_add(&f->units[0],0,10);
    kg_inventory_add(&f->units[0],4,20);
    for(int i=0;i<60;i++) kg_new_animal(f,i,KG_COW,0);
    KagActionMaskState s; kag_action_mask_begin(&s,&game,&policy,0);
    kag_direct_prepare_sales(&s);
    assert(s.forced[0]==0 && s.forced[6]==20); // retain feed wheat
    reset();
    f=&game.players[0]; f->shed[6]=2; game.market.inventory[6]=10000000;
    kag_action_mask_begin(&s,&game,&policy,0);
    int money=s.money, inventory=s.market[6];
    commit(&s,40,KAG_DIRECT_SELL+6,2);
    assert(s.money==money+2 && s.market[6]==inventory && s.shed[6]==0);
}

static void quantity_and_slots(void) {
    reset();
    KGPlayer* f = &game.players[0];
    f->units[0].x = f->units[0].y = 4;
    f->shed[8] = 17;
    KagActionMaskState s; kag_action_mask_begin(&s,&game,&policy,0);
    kag_action_mask_commit(&s,0,5+8); // pickup fertilizer
    kag_action_mask_before(&s,1,mask);
    for (int q = 0; q < 20; q++) assert(mask[kag_direct_offset(1)+q]);
    kag_action_mask_commit(&s,1,16);
    assert(s.shed[8] == 0 && s.held[8] == 17);
    memset(actions,0,sizeof(actions));
    actions[0] = 5+8; actions[1] = 16;
    actions[42] = 1; actions[43] = 6; // slot 0 remains NOOP; slot 1 buys 7 seeds
    KGAction out; kag_decode_multi_action(&out,actions,&game,0,&policy);
    assert(out.farmer.op == KG_OP_PICKUP && out.farmer.arg == 8 && out.farmer.n == 17);
    assert(out.market_count == 10 && out.market[0].op == -1);
    assert(out.market[1].op == KG_MARKET_BUY_SEED && out.market[1].item == 0 && out.market[1].n == 7);
    kag_action_mask_begin(&s,&game,&policy,0);
    commit(&s,40,0,1); singleton(&s,41,0);
    commit(&s,42,11,1); singleton(&s,43,0); // hire has no learned quantity
}

static void full_games(void) {
    reset();
    unsigned int rng = 111;
    for (int k = 0; k < 719; k++) {
        KGAction pair[2];
        for (int p = 0; p < 2; p++) {
            kag_sample_cpu_logits(&policy,&game,p,logits,0,&rng,actions,mask);
            KagActionMaskState s; kag_action_mask_begin(&s,&game,&policy,p);
            unsigned char check[KAG_ALL_LOGITS];
            for (int h = 0; h < KAG_ACTION_HEADS; h++) {
                kag_action_mask_before(&s,h,check);
                int off=kag_direct_offset(h), n=kag_direct_width(h);
                assert(!memcmp(check+off,mask+off,n));
                assert(check[off+(int)actions[h]]);
                kag_action_mask_commit(&s,h,(int)actions[h]);
            }
            kag_decode_multi_action(&pair[p],actions,&game,p,&policy);
            kag_write_observation(&policy,&game,p,obs);
            for (int i=0;i<KAG_ENTITY_OBS_SIZE;i++) assert(isfinite(obs[i]));
        }
        kg_step(&game,pair);
        kag_policy_step(&policy,&game);
    }
    assert(kg_done(&game));
}

int main(void) {
    catalog(); prefix(); private_observation(); forced_sales(); budget(); overflow_and_floor(); quantity_and_slots(); full_games();
    puts("direct policy: catalog, privacy, prefix masks, forced loss support, budgets and full games PASS");
    return 0;
}
