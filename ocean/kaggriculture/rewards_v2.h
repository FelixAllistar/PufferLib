#pragma once
#define KAG_REWARD_V2_AVAILABLE 1

/* Eligibility is a rule-based yes/no question, not a future-value estimate.
 * New one-shot crops already contain yield_units=1, even before maturity. */
KG_HD static inline int kag_producer_viable(const KGState* game, const KGTile* t) {
    /* kg_step's final action is episode_steps-2; the resulting state is not
     * another opportunity to harvest newly matured produce. */
    int last_day = (game->config.episode_steps - 2) / game->config.turns_per_day;
    if (t->kind == KG_TILE_PLANT && (unsigned)t->crop < KG_NUM_CROPS) {
        const KGCropDef* d = &KG_CROP_DEFS[t->crop];
        int first = t->planted_day + d->first_yield_day;
        if (game->day >= first && t->yield_units > 0) return 1;
        if (first > last_day) return 0;
        if (!d->ongoing) {
            return game->day <= t->planted_day + d->max_yield_day
                && (t->yield_units > 0 || game->day <= last_day);
        }
        int next = first;
        if (next <= game->day) next += ((game->day - first) / d->interval + 1) * d->interval;
        return next <= last_day && (next - first) / d->interval < d->max_yield;
    }
    if (kg_is_animal_tile(t)) {
        if (t->yield_units > 0) return 1;
        const KGAnimalDef* d = &KG_ANIMAL_DEFS[t->animal];
        int next = t->placed_day + d->first_yield_day;
        if (next <= game->day) next += ((game->day - next) / d->interval + 1) * d->interval;
        return next <= last_day;
    }
    return 0;
}

KG_HD static inline int kag_producer_healthy(const KGTile* t) {
    if (t->kind == KG_TILE_PLANT) return t->watered_today || t->consecutive_unwatered == 0;
    if (kg_is_animal_tile(t)) return t->fed_today || t->consecutive_unfed == 0;
    return 0;
}

KG_HD static inline int kag_quality_tile(const KGState* game, const KGTile* t) {
    return kag_producer_healthy(t) && kag_producer_viable(game, t);
}

KG_HD static inline void kag_quality_components(const KGState* game, int pid,
        float* coverage, float* idle, int* crops, int* animals) {
    const KGPlayer* p = &game->players[pid];
    int capacity[4] = {0}, used[4] = {0};
    *crops = *animals = 0;
    for (int y = 0; y < game->config.board_size; y++) {
        for (int x = 0; x < game->config.board_size; x++) {
            int bit = kg_quadrant(x, y, game->config.board_size);
            int q = bit == 1 ? 0 : bit == 2 ? 1 : bit == 4 ? 2 : 3;
            capacity[q]++;
            const KGTile* t = &p->tiles[kg_tile_index(x, y)];
            if (!(p->unlocked_mask & bit) || !kag_quality_tile(game, t)) continue;
            used[q]++;
            *crops += t->kind == KG_TILE_PLANT;
            *animals += kg_is_animal_tile(t);
        }
    }
    float prefix = 1.0f;
    *coverage = *idle = 0.0f;
    for (int q = 0; q < 4; q++) {
        float u = capacity[q] ? (float)used[q] / capacity[q] : 0.0f;
        if (u < prefix) prefix = u;
        *coverage += prefix / 4.0f;
        if (q && (p->unlocked_mask & (1 << q))) *idle += (1.0f - u) / 3.0f;
    }
}

/* Exact isolated sale quote. No opponent trade or later town demand assumed. */
KG_HD static inline float kag_exact_sale_quote(const KGState* game, int item, int n) {
    float proceeds = 0.0f;
    for (int j = 0; j < n; j++) proceeds += kg_market_price(item, game->market.inventory[item] + j);
    return proceeds;
}

KG_HD static inline float kag_reward_phi(const Env* env, int pid) {
    const KagRewardConfig* r = &env->reward;
    if (r->pbrs_scale == 0.0f) return 0.0f;
    const KGState* game = &env->game_storage;
    const KGPlayer* p = &game->players[pid];
    float norm = (float)game->config.starting_money;
    float value = r->cash_weight * ((float)p->money - env->reward_state[pid].start_cash) / norm;
    if (r->stock_weight != 0.0f) {
        for (int item = 0; item < KG_NUM_PRODUCTS; item++) {
            value += r->stock_weight * kag_exact_sale_quote(game, item, p->shed[item]) / norm;
        }
    }
    if (r->crop_weight != 0.0f || r->animal_weight != 0.0f) {
        float coverage, idle;
        int crops, animals;
        kag_quality_components(game, pid, &coverage, &idle, &crops, &animals);
        value += (r->crop_weight * crops + r->animal_weight * animals) / 25.0f;
    }
    return value;
}

KG_HD static inline void kag_reward_reset(Env* env, int pid) {
    KagRewardState* s = &env->reward_state[pid];
    memset(s, 0, sizeof(*s));
    s->start_cash = env->game_storage.players[pid].money;
    s->start_step = env->game_storage.step;
    s->discount = 1.0f;
    s->phi = kag_reward_phi(env, pid);
}

KG_HD static inline float kag_reward_step(Env* env, int pid, int done) {
    KagRewardState* s = &env->reward_state[pid];
    const KagRewardConfig* r = &env->reward;
    float coverage, idle;
    int crops, animals;
    kag_quality_components(&env->game_storage, pid, &coverage, &idle, &crops, &animals);
    s->coverage_sum += coverage;
    s->idle_sum += idle;
    /* Zero the ENTIRE terminal potential, including cash and stock. */
    float next_phi = done ? 0.0f : kag_reward_phi(env, pid);
    float shaping = r->pbrs_scale * (r->gamma * next_phi - s->phi);
    s->discounted_pbrs += s->discount * shaping;
    s->discount *= r->gamma;
    s->phi = next_phi;
    if (!done) return shaping;
    s->money_reward = r->money_scale
        * ((float)env->game_storage.players[pid].money - s->start_cash)
        / env->game_storage.config.starting_money;
    s->quality_reward = r->quality_scale * (s->coverage_sum - r->quality_idle_cost * s->idle_sum)
        / env->game_storage.config.episode_steps;
    return shaping + s->money_reward + s->quality_reward;
}

KG_HD static inline void kag_reward_log(Env* env, int pid) {
    const KagRewardState* s = &env->reward_state[pid];
    float gain = (float)env->game_storage.players[pid].money - s->start_cash;
    env->log.cash_gain += gain;
    env->log.terminal_cash_reward += s->money_reward;
    env->log.terminal_quality_reward += s->quality_reward;
    env->log.discounted_pbrs += s->discounted_pbrs;
    env->log.quality_coverage += s->coverage_sum / env->game_storage.config.episode_steps;
    env->log.quality_idle += s->idle_sum / env->game_storage.config.episode_steps;
    if (env->reset_source) env->log.reset_cash_gain += gain;
    else env->log.root_cash_gain += gain;
}

static inline float kag_reward_setting(Dict* d, const char* key, float default_value) {
    float value = dict_find(d, key) ? (float)dict_get(d, key) : default_value;
    if (!isfinite(value) || value < 0.0f) {
        fprintf(stderr, "%s must be finite and nonnegative\n", key);
        exit(1);
    }
    return value;
}

/* Trainer owns gamma. Inference needs no shaping; training calls this before
 * constructing any environments. Reward clipping would change the objective. */
static inline void kag_reward_bind_train_config(Dict* d, float gamma, float clip) {
    if (!isfinite(gamma) || gamma <= 0.0f || gamma > 1.0f) {
        fprintf(stderr, "Kaggriculture requires train.gamma in (0,1]\n"); exit(1);
    }
    if (kag_reward_setting(d, "reward_pbrs_scale", 0.0f) != 0.0f && clip != 0.0f) {
        fprintf(stderr, "PBRS requires train.reward_clip=0; clipping breaks telescoping\n"); exit(1);
    }
    dict_set(d, "_reward_train_gamma", gamma);
}

static inline void kag_reward_configure(Env* env, Dict* d) {
    const char* retired[] = {"reward_potential_scale", "reward_cash_scale",
        "reward_progress_scale", "reward_progress_terminal_money_scale", "reward_progress_win_scale",
        "reward_progress_maintenance_scale", "reward_expansion_scale", "reward_phase_scale"};
    for (unsigned i = 0; i < sizeof(retired) / sizeof(retired[0]); i++) {
        if (dict_find(d, retired[i]) && dict_get(d, retired[i]) != 0.0) {
            fprintf(stderr, "%s is retired; use terminal cash, quality, and optional PBRS\n", retired[i]);
            exit(1);
        }
    }
    if (env->curriculum_enabled) {
        fprintf(stderr, "Tagged curriculum rewards are not part of reward v2; set curriculum_enabled=0\n");
        exit(1);
    }
    KagRewardConfig* r = &env->reward;
    r->money_scale = kag_reward_setting(d, "reward_money_scale", 1.0f);
    r->quality_scale = kag_reward_setting(d, "reward_quality_scale", 1.0f);
    r->quality_idle_cost = kag_reward_setting(d, "reward_quality_idle_cost", 0.25f);
    r->pbrs_scale = kag_reward_setting(d, "reward_pbrs_scale", 0.0f);
    r->cash_weight = kag_reward_setting(d, "pbrs_cash_weight", 1.0f);
    r->stock_weight = kag_reward_setting(d, "pbrs_stock_weight", 1.0f);
    r->crop_weight = kag_reward_setting(d, "pbrs_crop_weight", 0.25f);
    r->animal_weight = kag_reward_setting(d, "pbrs_animal_weight", 0.25f);
    if (r->pbrs_scale && !dict_find(d, "_reward_train_gamma")) {
        fprintf(stderr, "PBRS requires gamma bound from the trainer before puf_init\n"); exit(1);
    }
    r->gamma = dict_find(d, "_reward_train_gamma") ? (float)dict_get(d, "_reward_train_gamma") : 1.0f;
    if (r->quality_idle_cost > 1.0f) {
        fprintf(stderr, "reward_quality_idle_cost must be in [0,1]\n"); exit(1);
    }
}
