#pragma once

KG_HD static inline int kag_market_quantity_spec(int id);

KG_HD static inline int kag_bulk_quote_quantity(int bin) {
    switch (bin) {
        case 0: return 1; case 1: return 5; case 2: return 10; case 3: return 20;
        case 4: return 30; case 5: return 50; case 6: return 75; default: return 100;
    }
}

KG_HD static inline void kag_update_quote_cache(Env* env, int item) {
    KagQuoteCache* c = &env->quote_cache[item];
    int inventory = env->game_storage.market.inventory[item];
    if (c->valid && c->inventory == inventory) return;
    float proceeds = 0.0f;
    int order_bin = 0, bulk_bin = 0;
    for (int n = 1; n <= 100; n++) {
        proceeds += kg_market_price(item, inventory + n - 1);
        if (order_bin < 8 && n == kag_market_quantity_spec(order_bin)) {
            c->quotes[order_bin] = proceeds / 10000.0f;
            c->quotes[8 + order_bin] = kg_market_price(item, inventory + n) / 1000.0f;
            order_bin++;
        }
        if (bulk_bin < 8 && n == kag_bulk_quote_quantity(bulk_bin)) {
            c->quotes[16 + bulk_bin] = proceeds / 10000.0f;
            c->quotes[24 + bulk_bin] = kg_market_price(item, inventory + n) / 1000.0f;
            bulk_bin++;
        }
    }
    c->inventory = inventory;
    c->valid = 1;
}

KG_HD static inline int kag_ready_units(const KGState* game, const KGTile* t) {
    if (t->kind == KG_TILE_PLANT && (unsigned)t->crop < KG_NUM_CROPS) {
        return game->day - t->planted_day >= KG_CROP_DEFS[t->crop].first_yield_day ? t->yield_units : 0;
    }
    return kg_is_animal_tile(t) ? t->yield_units : 0;
}

KG_HD static inline void kag_write_observation_with_summaries(Env* env, int pid,
        const KagFarmSummary summaries[KG_NUM_PLAYERS]) {
    (void)summaries;
    float* out = (float*)env->agents[pid].observations;
    const KGState* game = &env->game_storage;
    const KGPlayer* me = &game->players[pid];
    const KGPlayer* opponent = &game->players[1 - pid];
    const KagRewardState* rs = &env->reward_state[pid];
    memset(out, 0, OBS_SIZE * sizeof(*out));
    float episode = (float)game->config.episode_steps;
    float board = (float)(game->config.board_size - 1);
    if (board < 1.0f) board = 1.0f;
    out[0] = me->money / 100000.0f;
    out[1] = opponent->money / 100000.0f;
    out[2] = ((float)me->money - opponent->money) / 100000.0f;
    out[3] = game->step / episode; /* EMAG progress contract */
    out[4] = game->day * game->config.turns_per_day / episode;
    out[5] = game->hour / (float)game->config.turns_per_day;
    out[6] = (episode - game->step) / episode;
    for (int q = 0; q < 4; q++) {
        out[7 + q] = (me->unlocked_mask & (1 << q)) != 0;
        out[11 + q] = (opponent->unlocked_mask & (1 << q)) != 0;
    }
    out[15] = me->hires_today / 16.0f;
    out[16] = opponent->hires_today / 16.0f;
    out[17] = me->unit_count / 17.0f;
    out[18] = opponent->unit_count / 17.0f;
    for (int shop = 0; shop < game->shop_count; shop++) out[19 + game->unlocked_shops[shop]] = 1.0f;
    out[27] = (me->money % 1000) / 1000.0f;
    out[28] = (opponent->money % 1000) / 1000.0f;
    out[29] = kg_hire_cost(me->hires_today, game->config.farm_hand_cost_mult) / 10000.0f;
    int lands = kag_popcount(me->unlocked_mask);
    out[30] = lands < 4 ? (1000 << (lands - 1)) / 10000.0f : 0.0f;
    out[KAG_OBS_RESET_SOURCE_INDEX] = env->reset_source != 0;
    out[32] = (game->step - rs->start_step) / episode;
    out[33] = rs->start_cash / 100000.0f;
    out[34] = rs->coverage_sum / episode;
    out[35] = rs->idle_sum / episode;
    out[36] = kag_land_buy_delay_ready(env, pid);
    out[37] = env->land_buy_min_days / 30.0f;
    out[38] = env->land_fill_step[pid] >= 0;
    out[39] = -1.0f;
    if (env->land_fill_step[pid] >= 0) {
        int left = env->land_buy_min_days * game->config.turns_per_day - (game->step - env->land_fill_step[pid]);
        out[39] = (left > 0 ? left : 0) / episode;
    }
    for (int c = 0; c < KG_NUM_CROPS; c++) out[40 + c] = me->seeds[c] / 100.0f;
    for (int item = 0; item < KG_NUM_ITEMS; item++) out[45 + item] = me->shed[item] / 100.0f;
    out[57] = rs->start_step / episode;
    out[58] = game->config.starting_money / 100000.0f;
    out[59] = game->config.shed_capacity / 100.0f;
    out[60] = (env->policy_max_hands > 0 && env->policy_max_hands < 16 ? env->policy_max_hands : 16) / 16.0f;
    out[61] = env->policy_market_slots / 10.0f;
    out[62] = game->config.turns_per_day / 24.0f;
    out[63] = game->config.episode_steps / 720.0f;
    for (int unit = 0; unit < KG_POLICY_UNITS && unit < opponent->unit_count; unit++) {
        out[64 + 3 * unit] = 1.0f;
        out[65 + 3 * unit] = opponent->units[unit].x / board;
        out[66 + 3 * unit] = opponent->units[unit].y / board;
    }
    out[115] = game->config.town_shop_sell_interval / 24.0f;
    out[116] = game->config.town_center_sell_interval / 24.0f;
    out[117] = game->config.town_shop_unlock_interval / 30.0f;
    if (game->config.town_shop_sell_interval > 0)
        out[118] = (game->step % game->config.town_shop_sell_interval) / (float)game->config.town_shop_sell_interval;
    if (game->config.town_center_sell_interval > 0)
        out[119] = (game->step % game->config.town_center_sell_interval) / (float)game->config.town_center_sell_interval;
    /* Each 8-bit limb survives the trainer's bf16 cast exactly. Keep the
     * uncapped magnitude above as well, so scale and small cash changes are
     * both available even late in a high-money game. */
    for (int limb = 0; limb < 4; limb++) {
        out[120 + limb] = (((uint32_t)me->money >> (8 * limb)) & 255u) / 256.0f;
        out[124 + limb] = (((uint32_t)opponent->money >> (8 * limb)) & 255u) / 256.0f;
    }

    int count[2][KG_NUM_PRODUCTS] = {{0}}, healthy[2][KG_NUM_PRODUCTS] = {{0}};
    int ready[2][KG_NUM_PRODUCTS] = {{0}}, ready_tiles[2][KG_NUM_PRODUCTS] = {{0}};
    int ages[KG_NUM_PRODUCTS] = {0}, held[KG_NUM_PRODUCTS] = {0};
    for (int view = 0; view < 2; view++) {
        const KGPlayer* p = &game->players[view == 0 ? pid : 1 - pid];
        int capacity[4] = {0};
        for (int q = 0; q < 4; q++) {
            float* row = out + KAG_PLOT_OFFSET + (view * 4 + q) * KAG_PLOT_FEATURES;
            row[0] = view == 0;
            row[1 + q] = 1.0f;
            row[5] = (p->unlocked_mask & (1 << q)) != 0;
        }
        for (int y = 0; y < game->config.board_size; y++) {
            for (int x = 0; x < game->config.board_size; x++) {
                int bit = kg_quadrant(x, y, game->config.board_size);
                int q = bit == 1 ? 0 : bit == 2 ? 1 : bit == 4 ? 2 : 3;
                capacity[q]++;
                const KGTile* t = &p->tiles[kg_tile_index(x, y)];
                float* row = out + KAG_PLOT_OFFSET + (view * 4 + q) * KAG_PLOT_FEATURES;
                row[6] += t->kind == KG_TILE_EMPTY;
                row[7] += t->kind == KG_TILE_WEED;
                int crop = t->kind == KG_TILE_PLANT && (unsigned)t->crop < KG_NUM_CROPS;
                int animal = kg_is_animal_tile(t);
                if (!crop && !animal) continue;
                int item = crop ? t->crop : KG_ANIMAL_DEFS[t->animal].product;
                int good = kag_quality_tile(game, t), yield = kag_ready_units(game, t);
                row[8] += crop; row[9] += animal; row[10] += good;
                row[11] += crop && t->watered_today;
                row[12] += animal && t->fed_today;
                row[13] += animal ? t->cared_today : t->fertilized_until_day >= game->day;
                row[crop ? 14 : 15] += yield / 100.0f;
                row[crop ? 16 + t->crop : 21 + t->animal] += 1.0f;
                count[view][item]++; healthy[view][item] += good;
                ready[view][item] += yield; ready_tiles[view][item] += yield > 0;
                if (!view) ages[item] += game->day - (crop ? t->planted_day : t->placed_day);
                if (animal) {
                    count[view][KG_ITEM_FERTILIZER]++;
                    healthy[view][KG_ITEM_FERTILIZER] += good;
                    ready[view][KG_ITEM_FERTILIZER] += t->fertilizer_available;
                    ready_tiles[view][KG_ITEM_FERTILIZER] += t->fertilizer_available != 0;
                }
            }
        }
        for (int q = 0; q < 4; q++) {
            float* row = out + KAG_PLOT_OFFSET + (view * 4 + q) * KAG_PLOT_FEATURES;
            float cap = capacity[q] ? (float)capacity[q] : 1.0f;
            for (int f = 6; f <= 13; f++) row[f] /= cap;
            for (int f = 16; f < 24; f++) row[f] /= cap;
        }
    }
    KagRouteTable routes[KAG_ROUTE_COUNT];
    for (int r = 0; r < KAG_ROUTE_SHED; r++) kag_build_route_table(&routes[r], game, me, r);
    for (int unit = 0; unit < KG_POLICY_UNITS && unit < me->unit_count; unit++) {
        const KGUnitState* u = &me->units[unit];
        float* row = out + KAG_WORKER_OFFSET + unit * KAG_WORKER_FEATURES;
        int tile = kg_tile_index(u->x, u->y);
        const KGTile* t = &me->tiles[tile];
        row[0] = 1.0f; row[1] = u->x / board; row[2] = u->y / board; row[3] = unit == 0;
        for (int item = 0; item < KG_NUM_ITEMS; item++) {
            row[4 + item] = u->inventory[item] / 100.0f;
            row[16] += row[4 + item];
            if (item < KG_NUM_PRODUCTS) held[item] += u->inventory[item];
        }
        row[17] = kag_tile_entity(t) / 12.0f;
        int crop = t->kind == KG_TILE_PLANT, animal = kg_is_animal_tile(t);
        row[18] = crop ? (game->day - t->planted_day) / 30.0f : animal ? (game->day - t->placed_day) / 30.0f : 0.0f;
        row[19] = kag_ready_units(game, t) / 100.0f;
        row[20] = crop ? !t->watered_today : animal ? !t->fed_today : 0.0f;
        for (int r = 0; r < KAG_ROUTE_SHED; r++) {
            row[21 + 2 * r] = routes[r].dist[tile] == 255 ? -1.0f : routes[r].dx[tile] / board;
            row[22 + 2 * r] = routes[r].dist[tile] == 255 ? -1.0f : routes[r].dy[tile] / board;
        }
        KGPosition access[4]; kg_shed_access_count(game->config.board_size, access);
        int nearest = 0, best = INT_MAX;
        for (int s = 0; s < 4; s++) {
            int distance = kag_abs((int)u->x - access[s].x) + kag_abs((int)u->y - access[s].y);
            if (distance < best) { best = distance; nearest = s; }
        }
        row[29] = access[nearest].x / board;
        row[30] = access[nearest].y / board;
        KGPosition pos = {u->x, u->y};
        row[31] = kg_is_shed_adjacent(&pos, game->config.board_size);
    }
    for (int item = 0; item < KG_NUM_PRODUCTS; item++) {
        float* row = out + KAG_PRODUCT_OFFSET + item * KAG_PRODUCT_FEATURES;
        row[item] = 1.0f;
        row[9] = game->market.prices[item] / 1000.0f;
        row[10] = (game->market.inventory[item] - KG_MARKET_DEFS[item].i0) / (float)KG_MARKET_DEFS[item].throughput;
        row[11] = me->shed[item] / 100.0f;
        row[12] = held[item] / 100.0f;
        row[13] = ready[0][item] / 100.0f;
        row[14] = count[0][item] / 25.0f;
        row[15] = healthy[0][item] / 25.0f;
        row[16] = ready_tiles[0][item] / 25.0f;
        if (item < KG_NUM_CROPS) {
            row[17] = me->seeds[item] / 100.0f;
            row[18] = KG_CROP_DEFS[item].seed_cost / 1000.0f;
        } else if (item < KG_ITEM_FERTILIZER) {
            int animal = item - KG_ITEM_EGG;
            row[17] = me->shed[KG_ITEM_GOOSE + animal] / 100.0f;
            row[18] = KG_ANIMAL_DEFS[animal].cost / 1000.0f;
        }
        row[19] = count[0][item] ? ages[item] / (30.0f * count[0][item]) : 0.0f;
        kag_update_quote_cache(env, item);
        for (int f = 0; f < 32; f++) row[20 + f] = env->quote_cache[item].quotes[f];
        row[52] = count[1][item] / 25.0f;
        row[53] = healthy[1][item] / 25.0f;
        row[54] = ready[1][item] / 100.0f;
        row[55] = KG_MARKET_DEFS[item].throughput / 500.0f;
    }
    float* task = out + KAG_TASK_OFFSET;
    int mode = kag_agent_macro_mode(env, pid);
    for (int t = 0; t < KAG_TASK_COUNT; t++) {
        if (mode == 1 || mode == 2) {
            task[t] = kag_macro_candidate_legal(env, pid, t)
                ? kag_agent_macro_scores(env, pid) ? kag_macro_candidate_score(env, pid, t) / 10000.0f : 1.0f
                : 0.0f;
        } else task[t] = kag_task_available_count(game, pid, t) / 100.0f;
    }
    /* v3 replaces duplicated tail features with controller identity and raw
     * episode peaks. These make sticky actions and milestone history visible. */
    task[44] = mode / 3.0f;
    task[45] = kag_agent_executor_version(env, pid);
    task[46] = env->macro_intent[pid] / 44.0f;
    task[47] = env->macro_ticks[pid] / episode;
    task[48] = env->macro_quantity[pid] / 100.0f;
    task[49] = env->macro_target[pid] / 15.0f;
    task[50] = rs->peak_plots / 4.0f;
    task[51] = rs->peak_crops / 100.0f;
    task[52] = rs->peak_animals / 100.0f;
    task[53] = 1.0f; /* state-aware market feasibility contract */
    task[54] = kag_agent_macro_scores(env, pid);
    task[55] = kag_agent_macro_interval(env, pid) / episode;
}
