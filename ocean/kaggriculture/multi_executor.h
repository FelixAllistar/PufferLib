#pragma once

/* Experimental 2/2. Five (intent, exact batch, region) requests share ONE
 * worker/resource allocator with automatic chores. There are no persistent
 * plans: batches mean worker assignments this turn, including routing.
 * Policy ABI 5; do not interpret an earlier checkpoint as this controller. */
#define KAG_MULTI_SLOTS 5
#define KAG_MULTI_FEED_HEAD 15
#define KAG_MULTI_HARVEST_HEAD 16
#define KAG_MULTI_INTENTS 29
#define KAG_MULTI_MAX_JOBS (KAG_EXPLICIT_MAX_JOBS + KAG_MULTI_SLOTS * KG_MAX_TILES)
enum {
    KAG_MULTI_STOP = 0, KAG_MULTI_PLANT = 1, /* + crop (5) */
    KAG_MULTI_COOP = 6, KAG_MULTI_PASTURE = 7,
    KAG_MULTI_CLEAR = 8, KAG_MULTI_FERTILIZE = 9, KAG_MULTI_HARVEST = 10,
    KAG_MULTI_HARVEST_CROP = 11, /* + crop (5) */
    KAG_MULTI_DELIVER_ITEM = 16, /* + inventory item (12) */
    KAG_MULTI_DELIVER_ALL = 28
};
typedef struct { int x, y, priority, op, arg, request; } KagMultiJob;

KG_HD static inline int kag_multi_cargo(const KGUnitState* u, int intent) {
    if (intent >= KAG_MULTI_DELIVER_ITEM && intent < KAG_MULTI_DELIVER_ALL)
        return u->inventory[intent-KAG_MULTI_DELIVER_ITEM];
    int total = 0;
    for (int i = 0; i < KG_NUM_ITEMS; i++) total += u->inventory[i];
    return total;
}

KG_HD static inline int kag_multi_tile_matches(const KGState* g, int p,
        int intent, int tile, int quadrant) {
    const KGPlayer* f = &g->players[p];
    int x = tile % KG_MAX_BOARD_SIZE, y = tile / KG_MAX_BOARD_SIZE;
    if (x >= g->config.board_size || y >= g->config.board_size) return 0;
    int bit = kg_quadrant(x, y, g->config.board_size);
    if (!(f->unlocked_mask & bit) || (quadrant && bit != quadrant)) return 0;
    const KGTile* t = &f->tiles[tile];
    if ((intent >= KAG_MULTI_PLANT && intent < KAG_MULTI_COOP)
            || intent == KAG_MULTI_COOP || intent == KAG_MULTI_PASTURE)
        return t->kind == KG_TILE_EMPTY;
    if (intent == KAG_MULTI_CLEAR)
        return t->kind == KG_TILE_PLANT || kag_explicit_reclaimable(t);
    if (intent == KAG_MULTI_FERTILIZE)
        return t->kind == KG_TILE_PLANT && t->fertilized_until_day < g->day + 2;
    if (intent == KAG_MULTI_HARVEST)
        return t->kind == KG_TILE_PLANT && kag_ready_units(g, t) > 0;
    if (intent >= KAG_MULTI_HARVEST_CROP && intent < KAG_MULTI_DELIVER_ITEM)
        return t->kind == KG_TILE_PLANT && t->crop == intent-KAG_MULTI_HARVEST_CROP
            && kag_ready_units(g, t) > 0;
    return 0;
}

KG_HD static inline int kag_multi_capacity(const KGState* g, int p,
        int intent, int quadrant) {
    if (intent <= 0 || intent >= KAG_MULTI_INTENTS) return 0;
    int count = 0;
    if (intent >= KAG_MULTI_DELIVER_ITEM) {
        const KGPlayer* f = &g->players[p];
        if (kg_shed_total(f) >= g->config.shed_capacity) return 0;
        for (int u = 0; u < f->unit_count; u++)
            count += kag_multi_cargo(&f->units[u],intent) > 0
                && (!quadrant || kg_quadrant(f->units[u].x,f->units[u].y,g->config.board_size) == quadrant);
        return count;
    }
    for (int t = 0; t < KG_MAX_TILES; t++)
        count += kag_multi_tile_matches(g, p, intent, t, quadrant);
    if (intent >= KAG_MULTI_PLANT && intent < KAG_MULTI_COOP) {
        int seeds = g->players[p].seeds[intent - KAG_MULTI_PLANT];
        if (count > seeds) count = seeds;
    }
    if (intent == KAG_MULTI_FERTILIZE) {
        int stock = kag_macro_item_stock(&g->players[p], KG_ITEM_FERTILIZER);
        if (count > stock) count = stock;
    }
    return count;
}

KG_HD static inline void kag_multi_write_mask(Env* env, int p) {
    unsigned char* mask = env->agents[p].action_mask;
    const KGState* g = &env->game_storage;
    for (int h = 0; h < KG_POLICY_UNIT_HEADS; h++) mask[h * 44] = 1;
    for (int s = 0; s < KAG_MULTI_SLOTS; s++) {
        unsigned char* out = mask + 3 * s * 44;
        for (int intent = 1; intent < KAG_MULTI_INTENTS; intent++)
            out[intent] = kag_multi_capacity(g, p, intent, 0) > 0;
        for (int q = 0; q < 44; q++) out[44 + q] = 1;
        for (int q = 1; q < 5; q++)
            out[88 + q] = (g->players[p].unlocked_mask & kag_macro_target_from_bin(q)) != 0;
    }
    /* 0 automatic procurement; 1 explicit purchases; 2 also reserve shed
     * wheat from automatic pickup (e.g. for a policy-requested sale). */
    mask[KAG_MULTI_FEED_HEAD * 44 + 1] = mask[KAG_MULTI_FEED_HEAD * 44 + 2] = 1;
    mask[KAG_MULTI_HARVEST_HEAD * 44 + 1] = 1; /* 0 ripe auto-harvest, 1 explicit crop harvest only */
    kag_write_market_slots(env, g, &g->players[p], mask);
}

KG_HD static inline void kag_multi_add(KagMultiJob* jobs, int* count,
        int x, int y, int priority, int op, int arg, int request) {
    if (*count < KAG_MULTI_MAX_JOBS)
        jobs[(*count)++] = (KagMultiJob){x, y, priority, op, arg, request};
}

/* This function never buys/sells anything and never mutates Env. The market
 * prefix masker calls it too, so worker-side shed transfers agree exactly. */
KG_HD static inline void kag_multi_work(KGAction* a, const Agent* agent,
        const KGState* g, int p) {
    const KGPlayer* f = &g->players[p];
    memset(a, 0, sizeof(*a));
    a->farmer = (KGUnitAction){KG_OP_PASS, -1, 1};
    a->hand_count = f->hand_count;
    for (int u = 0; u < a->hand_count; u++) a->hands[u] = a->farmer;
    int intent[KAG_MULTI_SLOTS] = {0}, left[KAG_MULTI_SLOTS] = {0};
    int region[KAG_MULTI_SLOTS] = {0}, fertilizer = 0;
    for (int s = 0; s < KAG_MULTI_SLOTS; s++) {
        intent[s] = kag_discrete_index(agent->actions[3*s], KAG_MULTI_INTENTS);
        if (!intent[s]) break;
        left[s] = 1 + kag_discrete_index(agent->actions[3*s+1], 44);
        region[s] = kag_macro_target_from_bin(kag_discrete_index(agent->actions[3*s+2], 5));
        if (intent[s] == KAG_MULTI_FERTILIZE) fertilizer += left[s];
    }
    int seeds[KG_NUM_CROPS]; memcpy(seeds, f->seeds, sizeof(seeds));
    int pickup[KG_NUM_ITEMS] = {0}, carried[KG_NUM_ITEMS] = {0};
    int housing[KG_NUM_ANIMALS] = {0};
    for (int u = 0; u < f->unit_count; u++) for (int i = 0; i < KG_NUM_ITEMS; i++)
        carried[i] += f->units[u].inventory[i];
    int unfed = 0;
    for (int t = 0; t < KG_MAX_TILES; t++)
        unfed += kg_is_animal_tile(&f->tiles[t]) && !f->tiles[t].fed_today;
    pickup[KG_ITEM_WHEAT] = unfed - carried[KG_ITEM_WHEAT];
    if (agent->actions[KAG_MULTI_FEED_HEAD] == 2) pickup[KG_ITEM_WHEAT] = 0;
    pickup[KG_ITEM_FERTILIZER] = fertilizer - carried[KG_ITEM_FERTILIZER];
    for (int s = 0; s < KG_NUM_ANIMALS; s++) {
        int room = kag_macro_animal_room(f, s);
        for (int other = 0; other < KG_NUM_ANIMALS; other++)
            if (KG_ANIMAL_DEFS[other].structure == KG_ANIMAL_DEFS[s].structure)
                room -= carried[KG_ITEM_GOOSE + other];
        pickup[KG_ITEM_GOOSE+s] = room;
        housing[s] = room;
    }
    for (int i = 0; i < KG_NUM_ITEMS; i++) {
        if (pickup[i] < 0) pickup[i] = 0;
        if (pickup[i] > f->shed[i]) pickup[i] = f->shed[i];
    }
    KagMultiJob jobs[KAG_MULTI_MAX_JOBS]; int count = 0;
    for (int t = 0; t < KG_MAX_TILES; t++) {
        const KGTile* tile = &f->tiles[t];
        int x = t % KG_MAX_BOARD_SIZE, y = t / KG_MAX_BOARD_SIZE;
        if (kg_is_animal_tile(tile)) {
            if (!tile->fed_today) kag_multi_add(jobs,&count,x,y,0,KG_OP_FEED,-1,-1);
            if (tile->yield_units > 0) kag_multi_add(jobs,&count,x,y,2,KG_OP_HARVEST,-1,-1);
            if (!tile->cared_today) kag_multi_add(jobs,&count,x,y,3,KG_OP_CARE,-1,-1);
            if (tile->fertilizer_available) kag_multi_add(jobs,&count,x,y,4,KG_OP_COLLECT_FERTILIZER,-1,-1);
        } else if (tile->kind == KG_TILE_PLANT) {
            const KGCropDef* d = &KG_CROP_DEFS[tile->crop];
            if (kag_macro_plant_needs_water(g,tile))
                kag_multi_add(jobs,&count,x,y,tile->consecutive_unwatered ? 0 : 3,KG_OP_WATER,-1,-1);
            else if (agent->actions[KAG_MULTI_HARVEST_HEAD] != 1 && tile->yield_units > 0
                    && g->day - tile->planted_day >= d->first_yield_day
                    && (d->ongoing || g->day - tile->planted_day >= d->max_yield_day
                        || g->config.episode_steps - g->step <= g->config.turns_per_day))
                kag_multi_add(jobs,&count,x,y,2,KG_OP_HARVEST,-1,-1);
        } else for (int s = 0; s < KG_NUM_ANIMALS; s++) {
            if (carried[KG_ITEM_GOOSE+s] > 0 && tile->kind == KG_ANIMAL_DEFS[s].structure
                    && tile->animal == KG_ANIMAL_INVALID)
                kag_multi_add(jobs,&count,x,y,1,KG_OP_PLACE,KG_ITEM_GOOSE+s,-1);
        }
        for (int s = 0; s < KAG_MULTI_SLOTS; s++) {
            if (!intent[s] || !kag_multi_tile_matches(g,p,intent[s],t,region[s])) continue;
            int op = KG_OP_PASS, arg = -1;
            if (intent[s] < KAG_MULTI_COOP) { op = KG_OP_PLANT; arg = intent[s]-1; }
            else if (intent[s] == KAG_MULTI_COOP) op = KG_OP_BUILD_COOP;
            else if (intent[s] == KAG_MULTI_PASTURE) op = KG_OP_BUILD_PASTURE;
            else if (intent[s] == KAG_MULTI_CLEAR) op = KG_OP_DIG;
            else if (intent[s] == KAG_MULTI_FERTILIZE) { op = KG_OP_FERTILIZE; arg = KG_ITEM_FERTILIZER; }
            else if (intent[s] == KAG_MULTI_HARVEST || (intent[s] >= KAG_MULTI_HARVEST_CROP
                    && intent[s] < KAG_MULTI_DELIVER_ITEM)) op = KG_OP_HARVEST;
            /* Explicit clear/early-harvest beats upkeep at that target, but
             * consumes its batch budget; other plants keep automatic chores. */
            kag_multi_add(jobs,&count,x,y,op == KG_OP_FERTILIZE ? -2
                : (op == KG_OP_HARVEST || op == KG_OP_DIG) ? -1 : 4,op,arg,s);
        }
    }
    KGPosition access[4]; kg_shed_access_count(g->config.board_size,access);
    for (int s = 0; s < KAG_MULTI_SLOTS; s++) if (intent[s] >= KAG_MULTI_DELIVER_ITEM)
        for (int k = 0; k < 4; k++) kag_multi_add(jobs,&count,access[k].x,access[k].y,0,
            KG_OP_DROP,intent[s]-KAG_MULTI_DELIVER_ITEM,s);
    for (int i = 0; i < KG_NUM_ITEMS; i++) if (pickup[i] > 0)
        for (int s = 0; s < 4; s++) kag_multi_add(jobs,&count,access[s].x,access[s].y,
            i == KG_ITEM_WHEAT ? 0 : 1,KG_OP_PICKUP,i,-1);
    unsigned char used[KG_MAX_HANDS+1] = {0}, claimed[KG_MAX_TILES] = {0};
    int pickup_claimed[KG_NUM_ITEMS] = {0};
    int shed_room = g->config.shed_capacity-kg_shed_total(f);
    for (int assigned = 0; assigned < f->unit_count; assigned++) {
        int best_u = -1, best_j = -1, best_cost = INT_MAX;
        for (int u = 0; u < f->unit_count; u++) if (!used[u]) {
            const KGUnitState* unit = &f->units[u];
            for (int j = 0; j < count; j++) {
                const KagMultiJob* job = &jobs[j];
                if (job->request >= 0 && left[job->request] <= 0) continue;
                int delivery = job->op == KG_OP_DROP;
                if (!delivery && job->op != KG_OP_PICKUP && claimed[kg_tile_index(job->x,job->y)]) continue;
                if (delivery) {
                    if (shed_room <= 0 || !kag_multi_cargo(unit,intent[job->request])) continue;
                    if (region[job->request] && kg_quadrant(unit->x,unit->y,g->config.board_size)
                            != region[job->request]) continue;
                    /* PLACE livestock on empty compatible housing would not
                     * deliver it. Route to another shed access in that case. */
                    int item = job->arg;
                    if (item >= KG_NUM_ITEMS && kag_multi_cargo(unit,KAG_MULTI_DELIVER_ALL) > shed_room)
                        item = unit->inventory_order_count ? unit->inventory_order[0] : -1;
                    if (item >= KG_ITEM_GOOSE && item < KG_NUM_ITEMS) {
                        const KGTile* t = &f->tiles[kg_tile_index(job->x,job->y)];
                        if (t->kind == KG_ANIMAL_DEFS[item-KG_ITEM_GOOSE].structure
                                && t->animal == KG_ANIMAL_INVALID) continue;
                    }
                }
                if (job->op == KG_OP_PLANT && seeds[job->arg] <= 0) continue;
                if (job->op == KG_OP_FEED && !unit->inventory[KG_ITEM_WHEAT]) continue;
                if ((job->op == KG_OP_FERTILIZE || job->op == KG_OP_PLACE) && !unit->inventory[job->arg]) continue;
                if (job->op == KG_OP_PICKUP) {
                    if (pickup_claimed[job->arg] || unit->inventory[job->arg]) continue;
                    if (job->arg >= KG_ITEM_GOOSE && housing[job->arg-KG_ITEM_GOOSE] <= 0) continue;
                    int livestock = 0;
                    for (int s = 0; s < KG_NUM_ANIMALS; s++) livestock += unit->inventory[KG_ITEM_GOOSE+s];
                    if (livestock) continue;
                    if (job->arg >= KG_ITEM_GOOSE && unfed && unit->inventory[KG_ITEM_WHEAT]) continue;
                }
                int dist = kag_abs((int)unit->x-job->x) + kag_abs((int)unit->y-job->y);
                /* A selected strategic batch owns its worker budget. Complete
                 * feasible local requests before routing requests, then use
                 * the remaining workers for automatic chores. In particular,
                 * WATER must not steal a local FERTILIZE worker: fertilizing
                 * before watering can change that day's crop yield. */
                int cost = (job->request >= 0 ? 0 : 4096)
                    + (dist ? 1024 : 0) + job->priority*32 + dist;
                if (cost < best_cost) { best_cost = cost; best_u = u; best_j = j; }
            }
        }
        if (best_u < 0) break;
        const KagMultiJob* job = &jobs[best_j];
        const KGUnitState* unit = &f->units[best_u];
        KGUnitAction* cmd = best_u ? &a->hands[best_u-1] : &a->farmer;
        int local = unit->x == job->x && unit->y == job->y;
        used[best_u] = 1;
        if (job->op == KG_OP_DROP) {
            int total = kag_multi_cargo(unit,intent[job->request]);
            int item = job->arg, n = total;
            int drop_all = item >= KG_NUM_ITEMS && total <= shed_room;
            if (!drop_all) {
                if (item >= KG_NUM_ITEMS) item = unit->inventory_order[0];
                n = unit->inventory[item];
                if (n > shed_room) n = shed_room;
            }
            *cmd = local ? (KGUnitAction){drop_all ? KG_OP_DROP : KG_OP_PLACE,drop_all ? -1 : item,n}
                : (KGUnitAction){kag_bot_route(f,unit,job->x,job->y),-1,1};
            shed_room -= n; /* reserve room even for a routed delivery */
            left[job->request]--;
        } else if (job->op == KG_OP_PICKUP) {
            pickup_claimed[job->arg] = 1;
            if (job->arg >= KG_ITEM_GOOSE) {
                int animal = job->arg-KG_ITEM_GOOSE;
                for (int s = 0; s < KG_NUM_ANIMALS; s++)
                    if (KG_ANIMAL_DEFS[s].structure == KG_ANIMAL_DEFS[animal].structure) housing[s]--;
            }
            int n = job->arg >= KG_ITEM_GOOSE ? 1 : pickup[job->arg];
            *cmd = local ? (KGUnitAction){KG_OP_PICKUP,job->arg,n}
                : (KGUnitAction){kag_bot_route(f,unit,job->x,job->y),-1,1};
        } else {
            claimed[kg_tile_index(job->x,job->y)] = 1;
            *cmd = local ? (KGUnitAction){job->op,job->arg,1}
                : (KGUnitAction){kag_bot_route(f,unit,job->x,job->y),-1,1};
            if (job->request >= 0) left[job->request]--;
            if (job->op == KG_OP_PLANT) seeds[job->arg]--;
        }
    }
}
