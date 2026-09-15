#pragma once

#define KAG_COUNTER_FIELDS(X) \
    X(plant_days) X(watered_plant_days) X(neglect_deaths) X(planting_day_deaths) \
    X(production_units) X(planted_crops) X(placed_animals) X(sold_units) X(bought_units) \
    X(production_value) X(sales_revenue) X(purchase_spend)
#define KAG_PRODUCT_COUNTER_FIELDS(X) \
    X(production_product_units) X(production_product_value) \
    X(sold_product_units) X(sold_product_revenue)

KG_HD static inline KagEpisodeCounters kag_metrics_capture(const KGState* g, int pid) {
    KagEpisodeCounters c;
    c.step = g->step; c.money = g->players[pid].money;
    c.plots = kag_popcount(g->players[pid].unlocked_mask);
#define KAG_CAPTURE(name) c.name = g->name[pid];
    KAG_COUNTER_FIELDS(KAG_CAPTURE)
#undef KAG_CAPTURE
    for (int item = 0; item < KG_NUM_PRODUCTS; item++) {
#define KAG_CAPTURE_PRODUCT(name) c.name[item] = g->name[pid][item];
        KAG_PRODUCT_COUNTER_FIELDS(KAG_CAPTURE_PRODUCT)
#undef KAG_CAPTURE_PRODUCT
    }
    return c;
}

KG_HD static inline void kag_episode_metrics_reset(Env* env, int pid) {
    env->metrics_start[pid] = kag_metrics_capture(&env->game_storage, pid);
    memset(&env->action_counts[pid], 0, sizeof(env->action_counts[pid]));
}

KG_HD static inline KagEpisodeCounters kag_metrics_delta(const Env* env, int pid) {
    KagEpisodeCounters c = kag_metrics_capture(&env->game_storage, pid);
    const KagEpisodeCounters* b = &env->metrics_start[pid];
    c.step -= b->step; c.money -= b->money; c.plots -= b->plots;
#define KAG_DELTA(name) c.name -= b->name;
    KAG_COUNTER_FIELDS(KAG_DELTA)
#undef KAG_DELTA
    for (int item = 0; item < KG_NUM_PRODUCTS; item++) {
#define KAG_DELTA_PRODUCT(name) c.name[item] -= b->name[item];
        KAG_PRODUCT_COUNTER_FIELDS(KAG_DELTA_PRODUCT)
#undef KAG_DELTA_PRODUCT
    }
    return c;
}

KG_HD static inline void kag_metrics_finish(Env* env, int pid, float win,
        const KagEpisodeCounters* delta) {
    const KGPlayer* p = &env->game_storage.players[pid];
    const KagEpisodeCounters* b = &env->metrics_start[pid];
    const KagActionCounters* a = &env->action_counts[pid];
    /* Commit actions with their completed episode, not in a different log window. */
#define KAG_ADD_ACTION(name) env->log.name += a->name;
    KAG_ACTION_COUNTERS(KAG_ADD_ACTION)
#undef KAG_ADD_ACTION
    env->log.start_money += b->money;
    env->log.start_plots += b->plots;
    env->log.ending_plots += kag_popcount(p->unlocked_mask);
    KagStartSummary* s = &env->log.starts[env->reset_source ? 1 : 0];
    s->money += p->money;
    s->opponent_money += env->game_storage.players[1 - pid].money;
    s->start_money += b->money; s->start_plots += b->plots;
    s->ending_plots += kag_popcount(p->unlocked_mask);
    s->land_purchases += delta->plots;
    s->plants += delta->planted_crops;
    s->animal_places += delta->placed_animals;
    s->neglect_deaths += delta->neglect_deaths;
    s->steps += delta->step; s->win_rate += win;
    for (int item = 0; item < KG_NUM_PRODUCTS; item++) {
        if (item <= KG_ITEM_MELON) s->crop_units += delta->production_product_units[item];
        else if (item >= KG_ITEM_EGG && item <= KG_ITEM_WOOL)
            s->animal_units += delta->production_product_units[item];
    }
}
#undef KAG_COUNTER_FIELDS
#undef KAG_PRODUCT_COUNTER_FIELDS
