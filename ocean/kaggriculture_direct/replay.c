#include "kaggriculture_direct.h"
#include "../kaggriculture/kag_bc_replay.c"

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
    for (int h = 0; h < 30; h++) {
        kag_action_mask_before(&s,h,mask);
        int off = kag_direct_offset(h), width = h < 20 ? 500 : 1903;
        int id = h < 20 ? KAG_DIRECT_PASS : 0;
        if (h < 20 && h < f->unit_count) {
            KGUnitAction a = h ? (h <= teacher->hand_count ? teacher->hands[h-1]
                : (KGUnitAction){KG_OP_PASS,-1,1}) : teacher->farmer;
            if ((a.op == KG_OP_PICKUP || a.op == KG_OP_PLACE) && a.n < 0 && (unsigned)a.arg < 12) {
                a.n = a.op == KG_OP_PICKUP ? s.shed[a.arg] : f->units[h].inventory[a.arg];
                if (a.op == KG_OP_PLACE && kag_direct_group(a,&f->tiles[kg_tile_index(
                        f->units[h].x,f->units[h].y)]) == 9 && a.n > 0) a.n = 1;
            }
            id = kag_direct_unit_id(a);
        } else if (h >= 20 && h-20 < teacher->market_count) {
            KGMarketOrder a = teacher->market[h-20];
            if (a.op == KG_MARKET_SELL && a.n < 0 && (unsigned)a.item < 9) a.n = s.shed[a.item];
            id = a.op < 0 ? 0 : kag_direct_market_id(a);
        }
        int count = 0, fallback = h < 20 ? KAG_DIRECT_PASS : 0;
        for (int j = 0; j < width; j++) if (mask[off+j]) { count++; fallback = j; }
        if (mask[off+(h < 20 ? KAG_DIRECT_PASS : 0)]) fallback = h < 20 ? KAG_DIRECT_PASS : 0;
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
