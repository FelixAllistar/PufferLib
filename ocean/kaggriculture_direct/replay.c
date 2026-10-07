#include "kaggriculture_direct.h"
#include "../kaggriculture/kag_bc_replay.c"

// Architecture-independent diagnostic rollout bridge. Uses exactly the native
// ABI-7 prefix support/decoder, with no route planner or strategic executor.
int kag_direct_sample_step(KagBCReplay* r, int seat, const float* logits,
        int greedy, unsigned int* rng, float* heads, float* rewards) {
    if (!r || (unsigned)seat >= 2 || !logits || !rng || !heads || !rewards
            || kg_done(&r->env.game)) return 0;
    unsigned char mask[KAG_ALL_LOGITS];
    for (int i=0;i<KAG_ALL_LOGITS;i++) if (!isfinite(logits[i])) return 0;
    kag_sample_cpu_logits(&r->env.policy,&r->env.game,seat,logits,greedy,rng,heads,mask);
    KGAction pair[2] = {0};
    kag_decode_multi_action(&pair[seat],heads,&r->env.game,seat,&r->env.policy);
    kg_rule_action(&r->env.game,1-seat,&pair[1-seat]);
    kag_apply_actions(&r->env,pair);
    memcpy(rewards,r->rewards,sizeof(r->rewards));
    return 1;
}

// Exact representable primitive labels. Unsupported/blocked/forced components
// are ignored, never relabelled as strategic requests. Original replay actions
// still advance the simulator, preserving verified replay states.
int kag_direct_project(KagBCReplay* r, int p, const KGAction* teacher, float* labels,
        float* history, unsigned char* mask, int* counts) {
    if (!r || (unsigned)p >= 2 || !teacher || !labels || !history || !mask || !counts) return 0;
    const KGPlayer* f = &r->env.game.players[p];
    if (f->unit_count > 20 || teacher->hand_count > 19 || teacher->market_count > 10) return 0;
    KagActionMaskState s;
    kag_action_mask_begin(&s,&r->env.game,&r->env.policy,p);
    int command = -1, quantity = 0;
    for (int h = 0; h < KAG_ACTION_HEADS; h++) {
        kag_action_mask_before(&s,h,mask);
        int off = kag_direct_offset(h), width = kag_direct_width(h);
        if (!(h%2)) { command = 0; quantity = 0; }
        int unit = h/2, slot = (h-40)/2;
        if (!(h%2) && h < 40 && unit < f->unit_count) {
            KGUnitAction a = unit ? (unit <= teacher->hand_count ? teacher->hands[unit-1]
                : (KGUnitAction){KG_OP_PASS,-1,1}) : teacher->farmer;
            if ((a.op == KG_OP_PICKUP || a.op == KG_OP_PLACE) && a.n < 0 && (unsigned)a.arg < 12) {
                a.n = a.op == KG_OP_PICKUP ? s.shed[a.arg] : f->units[unit].inventory[a.arg];
                if (a.op == KG_OP_PLACE && kag_direct_group(a,&f->tiles[kg_tile_index(
                        f->units[unit].x,f->units[unit].y)]) == 9 && a.n > 0) a.n = 1;
            }
            command = kag_direct_unit_id(a);
            quantity = kag_direct_unit_quantity(command) ? a.n-1 : 0;
        } else if (!(h%2) && h >= 40 && slot < teacher->market_count) {
            KGMarketOrder a = teacher->market[slot];
            if (a.op == KG_MARKET_SELL && a.n < 0 && (unsigned)a.item < 9) a.n = s.shed[a.item];
            command = kag_direct_market_id(a);
            quantity = kag_direct_market_quantity(command) ? a.n-1 : 0;
        }
        // A filtered command must never teach a quantity under its fallback's
        // support. Keep canonical history for later prefix masks, not labels.
        int id = h%2 ? ((int)history[h-1] == command ? quantity : -1) : command;
        int count = 0, fallback = 0;
        for (int j = 0; j < width; j++) if (mask[off+j]) { count++; fallback = j; }
        if (mask[off]) fallback = 0;
        if (!count) return 0;
        labels[h] = -1;
        int selected = fallback;
        if (count == 1) counts[1]++;
        else if (id >= 0 && id < width && mask[off+id]) { labels[h] = id; selected = id; counts[0]++; }
        else counts[2]++;
        history[h] = selected;
        kag_action_mask_commit(&s,h,selected);
    }
    return 1;
}
