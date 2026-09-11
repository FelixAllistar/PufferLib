/* Executor v1: strategy belongs to PPO. Included after the legacy macro code.
 * v0 remains unchanged for existing checkpoints. No Python in GPU execution. */
#define KAG_EXPLICIT_RECLAIM 35
#define KAG_EXPLICIT_FERTILIZE 36
#define KAG_EXPLICIT_MAX_JOBS 320

typedef struct { int x,y,priority,op,arg,n; } KagExplicitJob;

KG_HD static inline int kag_explicit_reclaimable(const KGTile* t) {
    return t->kind==KG_TILE_WEED || ((t->kind==KG_TILE_COOP
        || t->kind==KG_TILE_PASTURE) && !kg_is_animal_tile(t));
}

KG_HD static inline int kag_explicit_legal(const Env* env,int p,int macro) {
    const KGState* g=&env->game_storage; const KGPlayer* f=&g->players[p];
    if(macro==KAG_MACRO_DIVERSIFY || macro==KAG_MACRO_MAINTAIN) return 0;
    if(macro==KAG_EXPLICIT_RECLAIM) {
        for(int t=0;t<KG_MAX_TILES;t++) if(kag_explicit_reclaimable(&f->tiles[t])) {
            int reserved=0;
            for(int s=0;s<KG_NUM_ANIMALS;s++) if(f->tiles[t].kind==KG_ANIMAL_DEFS[s].structure)
                reserved+=kag_macro_item_stock(f,KG_ITEM_GOOSE+s);
            if(!reserved)return 1;
        }
        return 0;
    }
    if(macro==KAG_EXPLICIT_FERTILIZE) {
        if(kag_macro_item_stock(f,KG_ITEM_FERTILIZER)<=0) return 0;
        for(int t=0;t<KG_MAX_TILES;t++) if(f->tiles[t].kind==KG_TILE_PLANT
                && f->tiles[t].fertilized_until_day<g->day) return 1;
        return 0;
    }
    /* Feasibility masks, not a scripted investment calendar or herd choice. */
    if(macro>=KAG_MACRO_PLANT_BASE && macro<KAG_MACRO_PLANT_BASE+KG_NUM_CROPS) {
        int c=macro-KAG_MACRO_PLANT_BASE;
        return kag_macro_reclaimable_tiles(f)>0 && (f->seeds[c]>0
            || kag_macro_can_invest_after_feed(g,p,KG_CROP_DEFS[c].seed_cost,0));
    }
    if(macro>=KAG_MACRO_ANIMAL_BASE && macro<KAG_MACRO_ANIMAL_BASE+KG_NUM_ANIMALS) {
        int s=macro-KAG_MACRO_ANIMAL_BASE;
        return (kag_macro_animal_room(f,s)>0 || kag_macro_empty_tiles(f)>0)
            && (kag_macro_item_stock(f,KG_ITEM_GOOSE+s)>0
                || kag_macro_can_invest_after_feed(g,p,KG_ANIMAL_DEFS[s].cost,1));
    }
    if(macro==KAG_MACRO_HIRE) {
        int limit=env->policy_max_hands>0?env->policy_max_hands:KG_MAX_HANDS;
        if(limit>KG_MAX_HANDS)limit=KG_MAX_HANDS;
        return f->hand_count<limit && kag_market_action_legal(g,f,
            (KGPolicyMarketSpec){KG_MARKET_HIRE,KG_ITEM_INVALID,1})
            && kag_macro_can_invest_after_feed(g,p,kg_hire_cost(f->hires_today,g->config.farm_hand_cost_mult),0);
    }
    if(macro==KAG_MACRO_EXPAND) return kag_market_action_legal(g,f,
        (KGPolicyMarketSpec){KG_MARKET_BUY_LAND,KG_ITEM_INVALID,1})
        && kag_macro_can_invest_after_feed(g,p,
            kag_popcount((unsigned)f->unlocked_mask)==1?1000:
            kag_popcount((unsigned)f->unlocked_mask)==2?2000:4000,0);
    if(macro>=KAG_MACRO_BUY_SEED_BASE && macro<KAG_MACRO_BUY_SEED_BASE+KG_NUM_CROPS) {
        int c=macro-KAG_MACRO_BUY_SEED_BASE;
        return kag_macro_seed_purchase_room(f)>0 && kag_market_action_legal(g,f,
            (KGPolicyMarketSpec){KG_MARKET_BUY_SEED,c,1})
            && kag_macro_can_invest_after_feed(g,p,KG_CROP_DEFS[c].seed_cost,0);
    }
    if(macro>=KAG_MACRO_BUY_ANIMAL_BASE && macro<KAG_MACRO_BUY_ANIMAL_BASE+KG_NUM_ANIMALS) {
        int s=macro-KAG_MACRO_BUY_ANIMAL_BASE;
        return kag_macro_animal_purchase_room(f,s)>0 && kag_market_action_legal(g,f,
            (KGPolicyMarketSpec){KG_MARKET_BUY_ANIMAL,KG_ITEM_GOOSE+s,1})
            && kag_macro_can_invest_after_feed(g,p,KG_ANIMAL_DEFS[s].cost,1);
    }
    if(macro==KAG_MACRO_BUY_FERTILIZER) return kag_market_action_legal(g,f,
        (KGPolicyMarketSpec){KG_MARKET_BUY_PRODUCT,KG_ITEM_FERTILIZER,1})
        && kag_macro_can_invest_after_feed(g,p,g->market.prices[KG_ITEM_FERTILIZER],1);
    return kag_macro_candidate_legal_legacy(env,p,macro);
}

KG_HD static inline void kag_explicit_add(KagExplicitJob* j,int* count,
        int x,int y,int priority,int op,int arg,int n) {
    if(*count<KAG_EXPLICIT_MAX_JOBS) j[(*count)++]=(KagExplicitJob){x,y,priority,op,arg,n};
}

/* All feasible targets enter the assignment. Batch limits are charged only
 * after selecting worker-target pairs, never by truncating board-order cells.
 * Global closest-pair greedy matching is bounded and deterministic. This is
 * not a claim of optimal multi-day scheduling or Hungarian matching. */
KG_HD static inline void kag_explicit_action(const KGState* g,int p,
        int macro,int quantity,int quadrant,int hand_limit,KGAction* a) {
    const KGPlayer* f=&g->players[p];
    memset(a,0,sizeof(*a));a->farmer=(KGUnitAction){KG_OP_PASS,-1,1};
    a->hand_count=f->hand_count;
    for(int u=0;u<a->hand_count;u++)a->hands[u]=(KGUnitAction){KG_OP_PASS,-1,1};
    if(quantity<1)quantity=1;
    if(quadrant && !(f->unlocked_mask&quadrant))quadrant=0;
    int crop=macro>=KAG_MACRO_PLANT_BASE && macro<KAG_MACRO_PLANT_BASE+KG_NUM_CROPS
        ? macro-KAG_MACRO_PLANT_BASE:-1;
    int animal=macro>=KAG_MACRO_ANIMAL_BASE && macro<KAG_MACRO_ANIMAL_BASE+KG_NUM_ANIMALS
        ? macro-KAG_MACRO_ANIMAL_BASE:-1;
    int feed=kag_macro_feed_shortfall(g,p);
    int free_cash=f->money-kag_macro_feed_cost(g,p);if(free_cash<0)free_cash=0;
    int stock[KG_NUM_ANIMALS],carried[KG_NUM_ANIMALS]={0};
    int budget[KG_NUM_ITEMS]={0};
    for(int s=0;s<KG_NUM_ANIMALS;s++) {
        stock[s]=kag_macro_item_stock(f,KG_ITEM_GOOSE+s);
        for(int u=0;u<f->unit_count;u++)carried[s]+=f->units[u].inventory[KG_ITEM_GOOSE+s];
    }
    int unfed=0;for(int t=0;t<KG_MAX_TILES;t++)
        unfed+=kg_is_animal_tile(&f->tiles[t]) && !f->tiles[t].fed_today;
    int carried_feed=0;for(int u=0;u<f->unit_count;u++)carried_feed+=f->units[u].inventory[KG_ITEM_WHEAT];
    budget[KG_ITEM_WHEAT]=unfed-carried_feed;
    if(budget[KG_ITEM_WHEAT]<0)budget[KG_ITEM_WHEAT]=0;
    if(budget[KG_ITEM_WHEAT]>f->shed[KG_ITEM_WHEAT])budget[KG_ITEM_WHEAT]=f->shed[KG_ITEM_WHEAT];
    for(int s=0;s<KG_NUM_ANIMALS;s++) {
        int room=kag_macro_animal_room(f,s);
        for(int other=0;other<KG_NUM_ANIMALS;other++)
            if(KG_ANIMAL_DEFS[other].structure==KG_ANIMAL_DEFS[s].structure)room-=carried[other];
        if(room<0)room=0;
        budget[KG_ITEM_GOOSE+s]=room<f->shed[KG_ITEM_GOOSE+s]?room:f->shed[KG_ITEM_GOOSE+s];
    }
    if(macro==KAG_EXPLICIT_FERTILIZE)budget[KG_ITEM_FERTILIZER]=f->shed[KG_ITEM_FERTILIZER]<quantity
        ?f->shed[KG_ITEM_FERTILIZER]:quantity;
    int build_budget=0;
    if(animal>=0) {
        int room=kag_macro_animal_room(f,animal);
        int affordable=free_cash/KG_ANIMAL_DEFS[animal].cost;
        int need=quantity;
        if(need>stock[animal]+affordable)need=stock[animal]+affordable;
        /* Existing compatible livestock reserves its own housing first. */
        for(int s=0;s<KG_NUM_ANIMALS;s++)if(s!=animal
                && KG_ANIMAL_DEFS[s].structure==KG_ANIMAL_DEFS[animal].structure)room-=stock[s];
        if(room<0)room=0;
        build_budget=need-room;if(build_budget<0)build_budget=0;
    }
    int plant_budget=crop>=0?f->seeds[crop]:0;
    if(plant_budget>quantity)plant_budget=quantity;
    int reclaim_budget=macro==KAG_EXPLICIT_RECLAIM?quantity:0;
    int fertilize_budget=macro==KAG_EXPLICIT_FERTILIZE?quantity:0;
    KagExplicitJob jobs[KAG_EXPLICIT_MAX_JOBS];int count=0;
    for(int t=0;t<KG_MAX_TILES;t++) {
        const KGTile* tile=&f->tiles[t];int x=t%KG_MAX_BOARD_SIZE,y=t/KG_MAX_BOARD_SIZE;
        int targeted=!quadrant || kg_quadrant(x,y,g->config.board_size)==quadrant;
        if(kg_is_animal_tile(tile)) {
            if(!tile->fed_today)kag_explicit_add(jobs,&count,x,y,0,KG_OP_FEED,-1,1);
            else if(tile->yield_units>0)kag_explicit_add(jobs,&count,x,y,2,KG_OP_HARVEST,-1,1);
            else if(!tile->cared_today)kag_explicit_add(jobs,&count,x,y,3,KG_OP_CARE,-1,1);
            else if(tile->fertilizer_available)kag_explicit_add(jobs,&count,x,y,4,KG_OP_COLLECT_FERTILIZER,-1,1);
        } else if(tile->kind==KG_TILE_PLANT) {
            const KGCropDef* d=&KG_CROP_DEFS[tile->crop];int age=g->day-tile->planted_day;
            if(kag_macro_plant_needs_water(g,tile))
                kag_explicit_add(jobs,&count,x,y,tile->consecutive_unwatered?0:3,KG_OP_WATER,-1,1);
            else if(tile->yield_units>0 && age>=d->first_yield_day && (d->ongoing
                    || macro==KAG_MACRO_HARVEST || age>=d->max_yield_day
                    || g->config.episode_steps-g->step<=g->config.turns_per_day))
                kag_explicit_add(jobs,&count,x,y,2,KG_OP_HARVEST,-1,1);
            if(fertilize_budget && targeted && tile->fertilized_until_day<g->day)
                kag_explicit_add(jobs,&count,x,y,3,KG_OP_FERTILIZE,KG_ITEM_FERTILIZER,1);
        } else {
            for(int s=0;s<KG_NUM_ANIMALS;s++)if(stock[s]>0
                    && tile->kind==KG_ANIMAL_DEFS[s].structure && tile->animal==KG_ANIMAL_INVALID)
                kag_explicit_add(jobs,&count,x,y,1,KG_OP_PLACE,KG_ITEM_GOOSE+s,1);
        }
        if(crop>=0 && targeted) {
            if(tile->kind==KG_TILE_EMPTY && plant_budget)
                kag_explicit_add(jobs,&count,x,y,4,KG_OP_PLANT,crop,1);
            else if(tile->kind==KG_TILE_WEED)
                kag_explicit_add(jobs,&count,x,y,4,KG_OP_DIG,-1,1);
        }
        if(reclaim_budget && targeted && kag_explicit_reclaimable(tile)) {
            /* Never clear housing reserved for any already-bought livestock. */
            int reserved=0;
            for(int s=0;s<KG_NUM_ANIMALS;s++)if(tile->kind==KG_ANIMAL_DEFS[s].structure)reserved+=stock[s];
            if(!reserved)kag_explicit_add(jobs,&count,x,y,4,KG_OP_DIG,-1,1);
        }
        if(build_budget && tile->kind==KG_TILE_EMPTY)
            kag_explicit_add(jobs,&count,x,y,4,
                KG_ANIMAL_DEFS[animal].structure==KG_TILE_COOP?KG_OP_BUILD_COOP:KG_OP_BUILD_PASTURE,-1,1);
    }
    KGPosition access[4];kg_shed_access_count(g->config.board_size,access);
    for(int item=0;item<KG_NUM_ITEMS;item++)if(budget[item]>0)
        for(int s=0;s<4;s++)kag_explicit_add(jobs,&count,access[s].x,access[s].y,
            item==KG_ITEM_WHEAT?0:1,KG_OP_PICKUP,item,1);
    unsigned char used[KG_MAX_HANDS+1]={0},claimed[KG_MAX_TILES]={0};
    int pickup_claimed[KG_NUM_ITEMS]={0};
    int built=0,picked_animals=0;
    for(int assigned=0;assigned<f->unit_count;assigned++) {
        int best_u=-1,best_j=-1,best_cost=0x7fffffff;
        for(int u=0;u<f->unit_count;u++)if(!used[u]) {
            const KGUnitState* unit=&f->units[u];
            for(int j=0;j<count;j++) {
                const KagExplicitJob* job=&jobs[j];int t=kg_tile_index(job->x,job->y);
                if(job->op!=KG_OP_PICKUP && claimed[t])continue;
                if(job->op==KG_OP_PLANT && plant_budget<=0)continue;
                if((job->op==KG_OP_BUILD_COOP || job->op==KG_OP_BUILD_PASTURE) && build_budget<=0)continue;
                if(job->op==KG_OP_DIG && macro==KAG_EXPLICIT_RECLAIM && reclaim_budget<=0)continue;
                if(job->op==KG_OP_FERTILIZE && (fertilize_budget<=0 || !unit->inventory[KG_ITEM_FERTILIZER]))continue;
                if(job->op==KG_OP_FEED && !unit->inventory[KG_ITEM_WHEAT])continue;
                if(job->op==KG_OP_PLACE && !unit->inventory[job->arg])continue;
                if(job->op==KG_OP_PICKUP) {
                    if(pickup_claimed[job->arg] || budget[job->arg]<=0)continue;
                    if(unit->inventory[job->arg]>0)continue;
                    int carry_animal=0;for(int s=0;s<KG_NUM_ANIMALS;s++)carry_animal+=unit->inventory[KG_ITEM_GOOSE+s];
                    if(carry_animal)continue;
                    if(job->arg>=KG_ITEM_GOOSE && unfed && unit->inventory[KG_ITEM_WHEAT])continue;
                }
                int dist=kag_abs((int)unit->x-job->x)+kag_abs((int)unit->y-job->y);
                /* Local feasible completion gets first refusal; no resource-
                 * unavailable job can capture a worker already at a task. */
                int cost=(dist==0?0:1024)+job->priority*32+dist;
                if(cost<best_cost){best_cost=cost;best_u=u;best_j=j;}
            }
        }
        if(best_u<0)break;
        const KagExplicitJob* job=&jobs[best_j];const KGUnitState* unit=&f->units[best_u];
        KGUnitAction* cmd=best_u==0?&a->farmer:&a->hands[best_u-1];
        used[best_u]=1;int local=unit->x==job->x && unit->y==job->y;
        if(job->op==KG_OP_PICKUP) {
            pickup_claimed[job->arg]=1;
            int n=job->arg==KG_ITEM_WHEAT?budget[job->arg]:1;
            *cmd=local?(KGUnitAction){KG_OP_PICKUP,job->arg,n}
                :(KGUnitAction){kag_bot_route(f,unit,job->x,job->y),-1,1};
            if(local && job->arg>=KG_ITEM_GOOSE)picked_animals+=n;
        } else {
            claimed[kg_tile_index(job->x,job->y)]=1;
            *cmd=local?(KGUnitAction){job->op,job->arg,job->n}
                :(KGUnitAction){kag_bot_route(f,unit,job->x,job->y),-1,1};
            if(job->op==KG_OP_PLANT)plant_budget--;
            if(job->op==KG_OP_BUILD_COOP || job->op==KG_OP_BUILD_PASTURE){build_budget--;built+=local;}
            if(job->op==KG_OP_DIG && macro==KAG_EXPLICIT_RECLAIM)reclaim_budget--;
            if(job->op==KG_OP_FERTILIZE)fertilize_budget--;
        }
    }
    /* Only mandatory feed and the selected strategic order are emitted. */
    if(feed>0)kag_macro_append_order(g,p,a,(KGMarketOrder){KG_MARKET_BUY_PRODUCT,KG_ITEM_WHEAT,feed});
    if(crop>=0) {
        int need=quantity;int room=kag_macro_reclaimable_tiles_in_quadrant(g,f,quadrant);
        if(need>room)need=room;need-=f->seeds[crop];
        int affordable=free_cash/KG_CROP_DEFS[crop].seed_cost;
        if(need>affordable)need=affordable;
        kag_macro_append_quantity(g,p,a,(KGMarketOrder){KG_MARKET_BUY_SEED,crop,1},need);
    } else if(animal>=0) {
        int need=quantity-stock[animal];int capacity=kag_macro_animal_room(f,animal)+built;
        for(int s=0;s<KG_NUM_ANIMALS;s++)if(KG_ANIMAL_DEFS[s].structure==KG_ANIMAL_DEFS[animal].structure)capacity-=stock[s];
        int room=g->config.shed_capacity-kg_shed_total(f)+picked_animals-feed;
        if(need>capacity)need=capacity;if(need>room)need=room;
        int affordable=free_cash/KG_ANIMAL_DEFS[animal].cost;if(need>affordable)need=affordable;
        if(need>0 && a->market_count<g->config.max_market_orders_per_turn)
            a->market[a->market_count++]=(KGMarketOrder){KG_MARKET_BUY_ANIMAL,KG_ITEM_GOOSE+animal,need};
    } else if(macro==KAG_MACRO_EXPAND) {
        kag_macro_append_order(g,p,a,(KGMarketOrder){KG_MARKET_BUY_LAND,-1,1});
    } else if(macro>=KAG_MACRO_SELL_BASE && macro<KAG_MACRO_SELL_BASE+KG_NUM_PRODUCTS) {
        kag_macro_append_quantity(g,p,a,(KGMarketOrder){KG_MARKET_SELL,macro-KAG_MACRO_SELL_BASE,1},quantity);
    } else if(macro==KAG_MACRO_SELL_ALL || macro==KAG_MACRO_CASH_OUT) {
        for(int i=0;i<KG_NUM_PRODUCTS;i++)if(f->shed[i])
            kag_macro_append_order(g,p,a,(KGMarketOrder){KG_MARKET_SELL,i,f->shed[i]});
        if(macro==KAG_MACRO_SELL_ALL){kag_macro_sort_sales_by_price(g,a);kag_macro_cap_market(a,KG_MARKET_SELL,-1,quantity);}
    } else if(macro>=KAG_MACRO_BUY_SEED_BASE && macro<KAG_MACRO_BUY_SEED_BASE+KG_NUM_CROPS) {
        int room=kag_macro_seed_purchase_room(f);if(quantity>room)quantity=room;
        kag_macro_append_quantity(g,p,a,(KGMarketOrder){KG_MARKET_BUY_SEED,macro-KAG_MACRO_BUY_SEED_BASE,1},quantity);
    } else if(macro>=KAG_MACRO_BUY_ANIMAL_BASE && macro<KAG_MACRO_BUY_ANIMAL_BASE+KG_NUM_ANIMALS) {
        int s=macro-KAG_MACRO_BUY_ANIMAL_BASE,room=kag_macro_animal_purchase_room(f,s);
        if(quantity>room)quantity=room;
        kag_macro_append_quantity(g,p,a,(KGMarketOrder){KG_MARKET_BUY_ANIMAL,KG_ITEM_GOOSE+s,1},quantity);
    } else if(macro==KAG_MACRO_HIRE) {
        int room=hand_limit-f->hand_count;
        if(quantity>room)quantity=room;
        kag_macro_append_quantity(g,p,a,(KGMarketOrder){KG_MARKET_HIRE,-1,1},quantity);
    } else if(macro==KAG_MACRO_BUY_WHEAT || macro==KAG_MACRO_BUY_FERTILIZER) {
        int item=macro==KAG_MACRO_BUY_WHEAT?KG_ITEM_WHEAT:KG_ITEM_FERTILIZER;
        if(item==KG_ITEM_WHEAT){kag_macro_remove_market_op_item(a,KG_MARKET_BUY_PRODUCT,item);if(quantity<feed)quantity=feed;}
        kag_macro_append_quantity(g,p,a,(KGMarketOrder){KG_MARKET_BUY_PRODUCT,item,1},quantity);
    }
}
