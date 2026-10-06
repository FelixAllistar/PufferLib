#pragma once

// ABI 6: direct worker actions, no macro requests or route executor.
// Reuse simulator/metric helpers without invoking the legacy controller.
#define KagActionMaskState KagLegacyActionMaskState
#define kag_write_observation kag_legacy_write_observation
#define kag_write_mask kag_legacy_write_mask
#define kag_action_mask_begin kag_legacy_action_mask_begin
#define kag_action_mask_before kag_legacy_action_mask_before
#define kag_action_mask_commit kag_legacy_action_mask_commit
#define kag_action_head_active kag_legacy_action_head_active
#define kag_sample_cpu_logits kag_legacy_sample_cpu_logits
#define kag_decode_multi_action kag_legacy_decode_multi_action
#define kag_policy_reset kag_legacy_policy_reset
#include "../kaggriculture/policy.h"
#undef KagActionMaskState
#undef kag_write_observation
#undef kag_write_mask
#undef kag_action_mask_begin
#undef kag_action_mask_before
#undef kag_action_mask_commit
#undef kag_action_head_active
#undef kag_sample_cpu_logits
#undef kag_decode_multi_action
#undef kag_policy_reset

#undef KAG_POLICY_VERSION
#define KAG_POLICY_VERSION 6
#undef KAG_OBSERVATION_ENTITIES
#define KAG_OBSERVATION_ENTITIES 4
#undef KG_POLICY_DIRECT_HANDS
#define KG_POLICY_DIRECT_HANDS 19
#undef KG_POLICY_UNITS
#define KG_POLICY_UNITS 20
#undef KG_POLICY_UNIT_HEADS
#define KG_POLICY_UNIT_HEADS 20
#undef KG_POLICY_UNIT_COMMANDS
#define KG_POLICY_UNIT_COMMANDS 500
#undef KAG_ACTION_HEADS
#define KAG_ACTION_HEADS 30
#undef KAG_ACTION_SIZES
#define KAG_ACTION_SIZES {500,500,500,500,500,500,500,500,500,500, \
    500,500,500,500,500,500,500,500,500,500, \
    1903,1903,1903,1903,1903,1903,1903,1903,1903,1903}
#undef KAG_TASK_LOGITS
#define KAG_TASK_LOGITS (20 * 500)
#undef KAG_MARKET_LOGITS
#define KAG_MARKET_LOGITS (10 * 1903)
// KAG_ALL_LOGITS and KG_POLICY_ACTION_MASK_SIZE expand the updated widths.
#undef KAG_ENTITY_OBS_SIZE
#define KAG_ENTITY_OBS_SIZE 5856
#define KAG_DIRECT_CELLS 416
#define KAG_DIRECT_OWN_UNITS 5216
#define KAG_DIRECT_OTHER_UNITS 5696
#define KAG_DIRECT_UNIT_WIDTH 24
#define KAG_DIRECT_CELL_WIDTH 24
#define KAG_DIRECT_MARKET_WIDTH 1903
#define KAG_DIRECT_PASS 4
#define KAG_DIRECT_DROP 245
#define KAG_DIRECT_SELL 1003

KG_HD KGUnitAction kag_direct_unit(int id) {
    KGUnitAction a = {KG_OP_PASS, -1, 1};
    if (id < 0 || id >= 500) return a;
    if (id < 5) {
        const int ops[] = {KG_OP_NORTH, KG_OP_SOUTH, KG_OP_EAST, KG_OP_WEST, KG_OP_PASS};
        a.op = ops[id];
    } else if (id < 245 || (id >= 246 && id < 486)) {
        int k = id < 245 ? id - 5 : id - 246;
        a.op = id < 245 ? KG_OP_PICKUP : KG_OP_PLACE;
        a.arg = k / 20;
        a.n = 1 + k % 20;
    } else if (id == 245) a.op = KG_OP_DROP;
    else if (id < 491) { a.op = KG_OP_PLANT; a.arg = id - 486; }
    else {
        const int ops[] = {KG_OP_WATER, KG_OP_HARVEST, KG_OP_FERTILIZE,
            KG_OP_BUILD_COOP, KG_OP_BUILD_PASTURE, KG_OP_DIG, KG_OP_FEED,
            KG_OP_COLLECT_FERTILIZER, KG_OP_CARE};
        a.op = ops[id - 491];
    }
    return a;
}

KG_HD int kag_direct_unit_id(KGUnitAction a) {
    for (int id = 0; id < 500; id++) {
        KGUnitAction b = kag_direct_unit(id);
        if (a.op == b.op && (b.op != KG_OP_PLANT || a.arg == b.arg)
            && ((b.op != KG_OP_PICKUP && b.op != KG_OP_PLACE)
                || (a.arg == b.arg && a.n == b.n))) return id;
    }
    return -1;
}

KG_HD KGMarketOrder kag_direct_market(int id) {
    KGMarketOrder a = {-1, -1, 1};
    if (id <= 0 || id >= 1903) return a;
    if (id <= 500) { a.op = KG_MARKET_BUY_SEED; a.item = (id - 1) / 100; }
    else if (id <= 700) {
        a.op = KG_MARKET_BUY_PRODUCT;
        a.item = id <= 600 ? KG_ITEM_WHEAT : KG_ITEM_FERTILIZER;
    } else if (id <= 1000) {
        a.op = KG_MARKET_BUY_ANIMAL; a.item = KG_ITEM_GOOSE + (id - 701) / 100;
    } else if (id == 1001) { a.op = KG_MARKET_HIRE; return a; }
    else if (id == 1002) { a.op = KG_MARKET_BUY_LAND; return a; }
    else { a.op = KG_MARKET_SELL; a.item = (id - 1003) / 100; }
    a.n = id >= 1003 ? 1 + (id - 1003) % 100 : 1 + (id - 1) % 100;
    return a;
}

KG_HD int kag_direct_market_id(KGMarketOrder a) {
    if (a.op == KG_MARKET_HIRE) return 1001;
    if (a.op == KG_MARKET_BUY_LAND) return 1002;
    if (a.n < 1 || a.n > 100) return -1;
    if (a.op == KG_MARKET_BUY_SEED && (unsigned)a.item < 5) return 1 + 100*a.item + a.n-1;
    if (a.op == KG_MARKET_BUY_PRODUCT && (a.item == 0 || a.item == 8))
        return 501 + (a.item == 8 ? 100 : 0) + a.n-1;
    if (a.op == KG_MARKET_BUY_ANIMAL && a.item >= 9 && a.item < 12)
        return 701 + 100*(a.item-9) + a.n-1;
    if (a.op == KG_MARKET_SELL && (unsigned)a.item < 9) return 1003 + 100*a.item + a.n-1;
    return -1;
}

KG_HD int kag_direct_offset(int h) { return h < 20 ? 500*h : 10000 + 1903*(h-20); }
KG_HD int kag_action_head_active(const float* actions, int h) { (void)actions; (void)h; return 1; }

KG_HD void kag_policy_reset(KagPolicy* policy, const KGState* game, int source) {
    assert(policy->max_hands > 0 && policy->max_hands <= 19);
    assert(game->players[0].unit_count <= 20 && game->players[1].unit_count <= 20
        && "direct reset bank supports at most twenty units per seat");
    // Only the legacy reset's capacity assertion differs; no controller runs.
    int hands = policy->max_hands;
    policy->max_hands = hands > 16 ? 16 : hands;
    kag_legacy_policy_reset(policy, game, source);
    policy->max_hands = hands;
}

KG_HD int kag_direct_fertilize(const KGState* g, const KGTile* t) {
    if (t->kind != KG_TILE_PLANT) return 0;
    const KGCropDef* c = &KG_CROP_DEFS[t->crop];
    int last = (g->config.episode_steps-1)/g->config.turns_per_day;
    for (int d = g->day; d < g->day+3; d++) {
        if (d <= t->fertilized_until_day || d > last) continue;
        if (!c->ongoing) {
            int age = d-t->planted_day;
            if (age >= (c->max_yield_day+1)/2 && age <= c->max_yield_day
                && !(d == g->day && t->watered_today) && t->yield_units < c->max_yield-1) return 1;
        } else {
            int since = d+1-t->planted_day-c->first_yield_day;
            if (since >= 0 && since%c->interval == 0 && since/c->interval+1 <= c->max_yield
                && d < last) return 1;
        }
    }
    return 0;
}

KG_HD int kag_direct_care(const KGState* g, const KGTile* t) {
    if ((t->kind == KG_TILE_COOP || t->kind == KG_TILE_PASTURE) && !kg_is_animal_tile(t)) return 1;
    if (!kg_is_animal_tile(t) || t->cared_today) return 0;
    const KGAnimalDef* a = &KG_ANIMAL_DEFS[t->animal];
    int last = (g->config.episode_steps-1)/g->config.turns_per_day;
    for (int d = g->day+1; d < last; d++) {
        int since = d+1-t->placed_day-a->first_yield_day;
        if (since >= 0 && since%a->interval == 0) return 1;
    }
    return 0;
}

// This compact prefix stores only own resources, public prices and tile claims.
// It does not clone/step an environment or run a route planner during sampling.
typedef struct {
    const KGState* game;
    const KagPolicy* policy;
    int player, seeds[5], shed[12], market[9], held[12];
    int money, hires, hands, lands, prepared, forced[9];
    uint16_t claims[100];
    float choices[KAG_ACTION_HEADS];
} KagActionMaskState;

KG_HD void kag_action_mask_begin(KagActionMaskState* s, const KGState* g,
        const KagPolicy* policy, int p) {
    memset(s, 0, sizeof(*s));
    s->game = g; s->policy = policy; s->player = p;
    const KGPlayer* f = &g->players[p];
    memcpy(s->seeds, f->seeds, sizeof(s->seeds));
    memcpy(s->shed, f->shed, sizeof(s->shed));
    memcpy(s->market, g->market.inventory, sizeof(s->market));
    s->money = f->money; s->hires = f->hires_today; s->hands = f->hand_count;
    s->lands = kag_popcount(f->unlocked_mask);
    for (int u = 0; u < f->unit_count; u++)
        for (int i = 0; i < 12; i++) s->held[i] += f->units[u].inventory[i];
    for (int u = 0; u < 20; u++) s->choices[u] = KAG_DIRECT_PASS;
}

KG_HD int kag_direct_group(KGUnitAction a, const KGTile* t) {
    switch (a.op) {
    case KG_OP_WATER: return 0;
    case KG_OP_FERTILIZE: return 1;
    case KG_OP_HARVEST: return 2;
    case KG_OP_DIG: return 3;
    case KG_OP_BUILD_COOP: case KG_OP_BUILD_PASTURE: return 4;
    case KG_OP_PLANT: return 5;
    case KG_OP_CARE: return 6;
    case KG_OP_FEED: return 7;
    case KG_OP_COLLECT_FERTILIZER: return 8;
    case KG_OP_PLACE:
        if (a.arg >= 9 && a.arg < 12 && !kg_is_animal_tile(t)
            && t->kind == KG_ANIMAL_DEFS[a.arg-9].structure) return 9;
        return -1;
    default: return -1;
    }
}

KG_HD int kag_direct_adjacent(const KGUnitState* u, int board) {
    KGPosition pos = {u->x, u->y};
    return kg_is_shed_adjacent(&pos, board);
}
KG_HD int kag_direct_total(const int* v, int n) {
    int total = 0; for (int i = 0; i < n; i++) total += v[i]; return total;
}

KG_HD void kag_direct_prepare_sales(KagActionMaskState* s) {
    if (s->prepared) return;
    s->prepared = 1;
    const KGState* g = s->game;
    int final = g->step >= g->config.episode_steps-3;
    int night = g->hour == g->config.turns_per_day-1;
    if (final) {
        for (int i = 0; i < 9; i++) s->forced[i] = s->shed[i];
    } else if (night) {
        int excess = kag_direct_total(s->shed, 12) + kag_direct_total(s->held, 12)
            - g->config.shed_capacity;
        int reserve = 0;
        for (int t = 0; t < 100; t++) reserve += kg_is_animal_tile(&g->players[s->player].tiles[t]);
        reserve -= s->held[KG_ITEM_WHEAT];
        if (reserve < 0) reserve = 0;
        for (int n = 0; n < excess; n++) {
            int best = -1;
            for (int i = 0; i < 9; i++) {
                int available = s->shed[i] - s->forced[i] - (i == KG_ITEM_WHEAT ? reserve : 0);
                if (available <= 0) continue;
                int price = kg_market_price(i, s->market[i] + s->forced[i]);
                int other = best < 0 ? 0 : kg_market_price(best, s->market[best] + s->forced[best]);
                if (best < 0 || (int64_t)price*KG_MARKET_DEFS[best].base
                    > (int64_t)other*KG_MARKET_DEFS[i].base) best = i;
            }
            if (best < 0) break;
            s->forced[best]++;
        }
    }
}

KG_HD int kag_direct_forced_sale(const KagActionMaskState* s) {
    int best = -1;
    for (int i = 0; i < 9; i++) {
        if (s->forced[i] <= 0 || s->shed[i] <= 0) continue;
        if (best < 0 || (int64_t)kg_market_price(i, s->market[i])*s->forced[i]
            > (int64_t)kg_market_price(best, s->market[best])*s->forced[best]) best = i;
    }
    if (best < 0) return 0;
    int n = s->forced[best];
    if (n > s->shed[best]) n = s->shed[best];
    if (n > 100) n = 100;
    return 1003 + 100*best + n-1;
}

KG_HD void kag_direct_mask_row(KagActionMaskState* s, int h, unsigned char* row) {
    int width = h < 20 ? 500 : 1903;
    memset(row, 0, width);
    const KGState* g = s->game;
    const KGPlayer* f = &g->players[s->player];
    if (h < 20) {
        row[KAG_DIRECT_PASS] = 1;
        if (h >= f->unit_count || g->done) return;
        const KGUnitState* u = &f->units[h];
        int tile = kg_tile_index(u->x, u->y), board = g->config.board_size;
        const KGTile* t = &f->tiles[tile];
        if (g->step == g->config.episode_steps-2 && kag_direct_adjacent(u, board)
            && kag_direct_total(u->inventory, 12) > 0) {
            row[KAG_DIRECT_PASS] = 0; row[KAG_DIRECT_DROP] = 1; return;
        }
        // Final B's rule-aware support, not a hand-written task selector.
        memset(row, 1, width);
        row[0] = u->y > 0; row[1] = u->y+1 < board;
        row[2] = u->x+1 < board; row[3] = u->x > 0;
        row[493] = u->inventory[KG_ITEM_FERTILIZER] > 0 && kag_direct_fertilize(g, t);
        row[499] = kag_direct_care(g, t);
        for (int id = 0; id < 500; id++) {
            KGUnitAction a = kag_direct_unit(id);
            int group = kag_direct_group(a, t);
            if ((group >= 0 && (s->claims[tile] & (1u << group)))
                || (a.op == KG_OP_PLANT && s->seeds[a.arg] <= 0)) row[id] = 0;
        }
        row[KAG_DIRECT_PASS] = 1;
        return;
    }
    row[0] = 1;
    if (g->done || h-20 >= s->policy->market_slots
        || h-20 >= g->config.max_market_orders_per_turn) return;
    kag_direct_prepare_sales(s);
    int forced = kag_direct_forced_sale(s);
    if (forced) { row[0] = 0; row[forced] = 1; return; }
    if (g->step >= g->config.episode_steps-3) return;
    int room = g->config.shed_capacity-kag_direct_total(s->shed, 12);
    for (int item = 0; item < 9; item++)
        for (int n = 1; n <= 100 && n <= s->shed[item]; n++) row[1003+item*100+n-1] = 1;
    for (int kind = 0; kind < 10; kind++) {
        int64_t cost = 0;
        for (int n = 1; n <= 100; n++) {
            int price = kind < 5 ? KG_CROP_DEFS[kind].seed_cost : kind < 7
                ? kg_market_price(kind == 5 ? 0 : 8, s->market[kind == 5 ? 0 : 8]-n)
                : KG_ANIMAL_DEFS[kind-7].cost;
            cost += price;
            if (cost > s->money || (kind >= 5 && n > room)) break;
            row[1+100*kind+n-1] = 1;
        }
    }
    row[1001] = s->hands < s->policy->max_hands && g->hour != g->config.turns_per_day-1
        && s->money >= kg_hire_cost(s->hires, g->config.farm_hand_cost_mult);
    row[1002] = s->lands < 4 && s->lands > 0 && s->money >= (1000 << (s->lands-1))
        && kag_land_buy_delay_ready(s->policy, g, s->player);
}

KG_HD void kag_action_mask_before(KagActionMaskState* s, int h, unsigned char* mask) {
    kag_direct_mask_row(s,h,mask+kag_direct_offset(h));
}

KG_HD void kag_action_mask_commit(KagActionMaskState* s, int h, int id) {
    s->choices[h] = id;
    const KGState* g = s->game;
    const KGPlayer* f = &g->players[s->player];
    if (h < 20) {
        if (h >= f->unit_count) return;
        const KGUnitState* u = &f->units[h];
        int tile = kg_tile_index(u->x, u->y);
        const KGTile* t = &f->tiles[tile];
        KGUnitAction a = kag_direct_unit(id);
        int group = kag_direct_group(a, t);
        if (group >= 0) s->claims[tile] |= 1u << group;
        if (a.op == KG_OP_PLANT) s->seeds[a.arg]--;
        int adjacent = kag_direct_adjacent(u, g->config.board_size);
        int room = g->config.shed_capacity-kag_direct_total(s->shed, 12);
        if (room < 0) room = 0;
        if (a.op == KG_OP_DROP && adjacent) {
            for (int j = 0; j < u->inventory_order_count; j++) {
                int i = u->inventory_order[j], n = u->inventory[i];
                s->held[i] -= n;
                if (n > room) n = room;
                s->shed[i] += n; room -= n;
            }
        } else if (a.op == KG_OP_PICKUP && adjacent) {
            int n = a.n < s->shed[a.arg] ? a.n : s->shed[a.arg];
            s->shed[a.arg] -= n; s->held[a.arg] += n;
        } else if (a.op == KG_OP_PLACE) {
            int n = a.n < u->inventory[a.arg] ? a.n : u->inventory[a.arg];
            if (group == 9) { if (n > 0) s->held[a.arg]--; }
            else if (adjacent) {
                if (n > room) n = room;
                s->shed[a.arg] += n; s->held[a.arg] -= n;
            }
        } else if (a.op == KG_OP_HARVEST) {
            int i = t->kind == KG_TILE_PLANT ? t->crop : kg_is_animal_tile(t)
                ? KG_ANIMAL_DEFS[t->animal].product : -1;
            if (i >= 0) s->held[i] += kag_ready_units(g, t);
        } else if (a.op == KG_OP_COLLECT_FERTILIZER && kg_is_animal_tile(t) && t->fertilizer_available)
            s->held[8]++;
        else if (a.op == KG_OP_FERTILIZE && t->kind == KG_TILE_PLANT && u->inventory[8] > 0)
            s->held[8]--;
        else if (a.op == KG_OP_FEED && kg_is_animal_tile(t) && !t->fed_today && u->inventory[0] > 0)
            s->held[0]--;
        return;
    }
    KGMarketOrder a = kag_direct_market(id);
    if (a.op < 0) return;
    if (a.op == KG_MARKET_HIRE) {
        s->money -= kg_hire_cost(s->hires++, g->config.farm_hand_cost_mult); s->hands++;
    } else if (a.op == KG_MARKET_BUY_LAND) { s->money -= 1000 << (s->lands-1); s->lands++; }
    else if (a.op == KG_MARKET_SELL) {
        int n = a.n < s->shed[a.item] ? a.n : s->shed[a.item];
        s->shed[a.item] -= n;
        s->forced[a.item] -= n;
        if (s->forced[a.item] < 0) s->forced[a.item] = 0;
        for (int j = 0; j < n; j++) {
            int price = kg_market_price(a.item,s->market[a.item]);
            s->money += price;
            if (price > 1) s->market[a.item]++;
        }
    } else if (a.op == KG_MARKET_BUY_SEED) {
        s->money -= a.n*KG_CROP_DEFS[a.item].seed_cost; s->seeds[a.item] += a.n;
    } else if (a.op == KG_MARKET_BUY_ANIMAL) {
        s->money -= a.n*KG_ANIMAL_DEFS[a.item-9].cost; s->shed[a.item] += a.n;
    } else {
        s->shed[a.item] += a.n;
        for (int j = 0; j < a.n; j++) s->money -= kg_market_price(a.item, --s->market[a.item]);
    }
}

KG_HD void kag_write_mask(KagPolicy* policy, const KGState* g, int p, unsigned char* mask) {
    KagActionMaskState s;
    kag_action_mask_begin(&s, g, policy, p);
    for (int h = 0; h < KAG_ACTION_HEADS; h++) kag_action_mask_before(&s, h, mask);
}

KG_HD void kag_decode_multi_action(KGAction* out, const float* actions, const KGState* g,
        int p, KagPolicy* policy) {
    (void)policy;
    memset(out, 0, sizeof(*out));
    out->farmer = kag_direct_unit(kag_discrete_index(actions[0], 500));
    out->hand_count = g->players[p].hand_count;
    for (int u = 1; u <= out->hand_count; u++)
        out->hands[u-1] = u < 20 ? kag_direct_unit(kag_discrete_index(actions[u], 500))
            : (KGUnitAction){KG_OP_PASS,-1,1};
    for (int slot = 0; slot < policy->market_slots && slot < g->config.max_market_orders_per_turn; slot++) {
        KGMarketOrder a = kag_direct_market(kag_discrete_index(actions[20+slot], 1903));
        // Preserve slot positions: removing NOOPs changes simultaneous price resolution.
        out->market[out->market_count++] = a;
    }
}

KG_HD void kag_sample_cpu_logits(KagPolicy* policy, const KGState* g, int p,
        const float* logits, int deterministic, unsigned int* rng, float* actions,
        unsigned char* mask) {
    KagActionMaskState s;
    kag_action_mask_begin(&s, g, policy, p);
    for (int h = 0; h < 30; h++) {
        kag_action_mask_before(&s, h, mask);
        int start = kag_direct_offset(h), n = h < 20 ? 500 : 1903, best = -1;
        float maximum = -INFINITY, sum = 0;
        for (int i = 0; i < n; i++) if (mask[start+i]) {
            if (best < 0 || logits[start+i] > maximum) { maximum = logits[start+i]; best = i; }
        }
        assert(best >= 0);
        if (!deterministic) {
            for (int i = 0; i < n; i++) if (mask[start+i]) sum += expf(logits[start+i]-maximum);
            *rng = 1664525u**rng + 1013904223u;
            float draw = (*rng >> 8)*(1.0f/16777216.0f)*sum;
            for (int i = 0; i < n; i++) if (mask[start+i]) {
                best = i; draw -= expf(logits[start+i]-maximum); if (draw < 0) break;
            }
        }
        actions[h] = best;
        kag_action_mask_commit(&s, h, best);
    }
}

KG_HD void kag_write_observation(KagPolicy* policy, const KGState* g, int p, float* out) {
    memset(out, 0, KAG_ENTITY_OBS_SIZE*sizeof(float));
    const KGPlayer* me = &g->players[p];
    const KGPlayer* opp = &g->players[1-p];
    float board = (float)(g->config.board_size-1), episode = (float)g->config.episode_steps;
    out[0] = me->money/100000.0f; out[1] = opp->money/100000.0f;
    out[2] = ((float)me->money-opp->money)/100000.0f;
    out[3] = g->step/episode; out[4] = g->day/30.0f;
    out[5] = g->hour/(float)g->config.turns_per_day;
    out[6] = (episode-g->step)/episode;
    for (int q = 0; q < 4; q++) {
        out[7+q] = !!(me->unlocked_mask & (1<<q)); out[11+q] = !!(opp->unlocked_mask & (1<<q));
    }
    out[15] = me->hires_today/20.0f; out[16] = opp->hires_today/20.0f;
    out[17] = me->unit_count/20.0f; out[18] = opp->unit_count/20.0f;
    for (int i = 0; i < g->shop_count; i++) out[19+g->unlocked_shops[i]] = 1;
    out[27] = kg_hire_cost(me->hires_today,g->config.farm_hand_cost_mult)/10000.0f;
    out[28] = g->config.shed_capacity/100.0f;
    out[29] = (g->config.shed_capacity-kg_shed_total(me))/100.0f;
    out[30] = kag_land_buy_delay_ready(policy,g,p);
    out[KAG_OBS_RESET_SOURCE_INDEX] = policy->reset_source;
    out[32] = policy->history[p].start_step/episode;
    out[33] = policy->history[p].start_cash/100000.0f;
    for (int i = 0; i < 5; i++) out[40+i] = me->seeds[i]/100.0f;
    for (int i = 0; i < 12; i++) out[45+i] = me->shed[i]/100.0f;
    out[57] = g->config.starting_money/100000.0f;
    out[58] = g->config.turns_per_day/24.0f;
    out[59] = g->config.episode_steps/720.0f;
    out[60] = policy->max_hands/19.0f; out[61] = policy->market_slots/10.0f;
    out[62] = g->config.town_shop_sell_interval/24.0f;
    out[63] = g->config.town_center_sell_interval/24.0f;
    for (int limb = 0; limb < 4; limb++) {
        out[120+limb] = (((uint32_t)me->money >> (8*limb)) & 255u)/256.0f;
        out[124+limb] = (((uint32_t)opp->money >> (8*limb)) & 255u)/256.0f;
    }
    for (int i = 0; i < 9; i++) {
        float* r = out+128+i*32;
        r[i] = 1; r[9] = g->market.prices[i]/1000.0f;
        r[10] = (g->market.inventory[i]-10000)/1000.0f;
        r[11] = me->shed[i]/100.0f;
        r[12] = i < 5 ? me->seeds[i]/100.0f : 0;
        r[13] = KG_MARKET_DEFS[i].base/1000.0f;
        for (int u = 0; u < me->unit_count; u++) r[14] += me->units[u].inventory[i]/100.0f;
    }
    for (int side = 0; side < 2; side++) {
        const KGPlayer* f = &g->players[side ? 1-p : p];
        for (int tile = 0; tile < 100; tile++) {
            const KGTile* t = &f->tiles[tile];
            float* r = out+KAG_DIRECT_CELLS+(side*100+tile)*24;
            r[t->kind] = 1;
            if (t->kind == KG_TILE_PLANT) r[7+t->crop] = 1;
            if (kg_is_animal_tile(t)) r[12+t->animal] = 1;
            r[15] = t->yield_units/20.0f;
            r[16] = t->kind == KG_TILE_PLANT ? (g->day-t->planted_day)/30.0f
                : kg_is_animal_tile(t) ? (g->day-t->placed_day)/30.0f : 0;
            r[17] = t->watered_today; r[18] = t->fed_today; r[19] = t->cared_today;
            r[20] = t->consecutive_unwatered/2.0f; r[21] = t->consecutive_unfed/2.0f;
            r[22] = t->fertilizer_available;
            r[23] = t->kind == KG_TILE_PLANT ? (t->fertilized_until_day-g->day)/3.0f
                : t->pending_care_bonus/2.0f;
        }
        for (int u = 0; u < 20 && u < f->unit_count; u++) {
            const KGUnitState* unit = &f->units[u];
            float* r = out+(side ? KAG_DIRECT_OTHER_UNITS+u*8 : KAG_DIRECT_OWN_UNITS+u*24);
            r[0] = 1; r[1] = unit->x/board; r[2] = unit->y/board; r[3] = u == 0;
            r[4] = kag_direct_adjacent(unit,g->config.board_size);
            if (!side) for (int i = 0; i < 12; i++) r[8+i] = unit->inventory[i]/100.0f;
        }
    }
    // Critic summary is observation-derived; private opponent inventories never enter the actor.
    for (int side = 0; side < 2; side++) {
        const KGPlayer* f = &g->players[side ? 1-p : p];
        for (int t = 0; t < 100; t++) {
            const KGTile* tile = &f->tiles[t];
            if (tile->kind == KG_TILE_PLANT) out[64+side*24+tile->crop] += .01f;
            if (kg_is_animal_tile(tile)) out[69+side*24+tile->animal] += .01f;
            out[72+side*24] += kag_ready_units(g,tile)/1000.0f;
        }
    }
    for (int i = 0; i < 8; i++) out[112+i] = g->market.prices[i]/1000.0f;
}
