#pragma once

KG_HD static inline void kag_decode_multi_action(KGAction* action,
        const Agent* agent, const KGState* game, int p, Env* env) {
    kag_multi_work(action, agent, game, p);
    env->macro_intent[p] = kag_discrete_index(agent->actions[0], KAG_MULTI_INTENTS);
    env->macro_quantity[p] = env->macro_intent[p] ? 1 + kag_discrete_index(agent->actions[1],44) : 0;
    env->macro_target[p] = env->macro_intent[p]
        ? kag_macro_target_from_bin(kag_discrete_index(agent->actions[2],5)) : 0;
    env->macro_ticks[p] = 0;
    KagActionMaskState prefix;
    kag_action_mask_begin(&prefix, env, p);
    memcpy(prefix.choices, agent->actions, sizeof(prefix.choices));
    kag_mask_prepare_market_from_work(&prefix, action);
    int limit = kag_policy_market_slot_limit(env);
    if (limit > game->config.max_market_orders_per_turn) limit = game->config.max_market_orders_per_turn;
    for (int slot = 0; slot < limit; slot++) {
        int h = KG_POLICY_MARKET_HEAD_OFFSET + 3*slot;
        if (kag_discrete_index(agent->actions[h],2) != 1) break;
        int id = kag_discrete_index(agent->actions[h+1],KG_POLICY_MARKET_COMMANDS);
        KGPolicyMarketSpec spec = kag_market_spec(id);
        int n = id < KG_POLICY_MARKET_QUANTITY_COMMANDS
            ? kag_market_quantity_spec(kag_discrete_index(agent->actions[h+2],KG_POLICY_MARKET_QUANTITIES)) : 1;
        /* Preserve the requested order, including quantity. Market fills may
         * differ under simultaneous opponent trades. Masks use only our own
         * observed prefix; the core remains the authority on actual fills. */
        action->market[action->market_count++] = (KGMarketOrder){spec.op,spec.item,n};
        if (kag_mask_market_capacity(&prefix,id) > 0) kag_mask_market_commit(&prefix,id,n);
    }
    if (kag_discrete_index(agent->actions[KAG_MULTI_FEED_HEAD],3) == 0
            && action->market_count < limit) {
        const KGPlayer* f = &game->players[p];
        int need = 0, available = prefix.shed[KG_ITEM_WHEAT] + prefix.wheat_out_of_shed;
        for (int t = 0; t < KG_MAX_TILES; t++)
            need += kg_is_animal_tile(&f->tiles[t]) && !f->tiles[t].fed_today;
        for (int u = 0; u < f->unit_count; u++) available += f->units[u].inventory[KG_ITEM_WHEAT];
        /* FEED lowers need and carried stock equally. Stock harvested by this
         * turn's workers is not assumed available for feeding other workers. */
        need -= available;
        int command = kag_market_action_id(KG_MARKET_BUY_PRODUCT,KG_ITEM_WHEAT,1);
        int cap = kag_mask_market_capacity(&prefix,command);
        if (need > cap) need = cap;
        if (need > 0) action->market[action->market_count++] =
            (KGMarketOrder){KG_MARKET_BUY_PRODUCT,KG_ITEM_WHEAT,need};
    }
    kag_apply_policy_limits(env, &game->players[p], action);
}
