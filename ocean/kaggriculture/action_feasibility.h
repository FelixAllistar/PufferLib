#pragma once

/* A sampled prefix has its own mask. This is shared by GPU training and CPU
 * inference; PPO archives these exact masks AFTER sampling, not the initial
 * independent-head approximation. No hidden opponent actions are consulted. */
#define PUFFER_PREFIX_DEPENDENT_MASK 1

typedef struct {
    const Env* env;
    int player;
    int mode;
    float choices[NUM_ATNS];
    int task_used[KAG_TASK_COUNT];
    int task_capacity[KAG_TASK_COUNT];
    int empty_used[4];
    int empty_capacity[4];
    int seeds_used[KG_NUM_CROPS];
    int cash;
    int shed[KG_NUM_ITEMS];
    int inventory[KG_NUM_PRODUCTS];
    int hands;
    int hires;
    int plots;
    int land_used;
    int land_ready;
    int prepared;
    unsigned char opening_quantity[8];
} KagActionMaskState;

KG_HD static inline int kag_mask_shed_total(const KagActionMaskState* s) {
    int n = 0;
    for (int i = 0; i < KG_NUM_ITEMS; i++) n += s->shed[i];
    return n;
}

KG_HD static inline void kag_action_mask_begin(KagActionMaskState* s,
        const Env* env, int player) {
    memset(s, 0, sizeof(*s));
    s->env = env;
    s->player = player;
    if (!env) return;
    s->mode = kag_agent_macro_mode(env, player);
    if (s->mode == 3) {
        const KGState* g = &env->game_storage;
        for (int task = 1; task < KAG_TASK_COUNT; task++)
            s->task_capacity[task] = kag_task_available_count(g, player, task);
        for (int y = 0; y < g->config.board_size; y++) for (int x = 0; x < g->config.board_size; x++) {
            int bit = kg_quadrant(x, y, g->config.board_size);
            int q = bit == 1 ? 0 : bit == 2 ? 1 : bit == 4 ? 2 : 3;
            s->empty_capacity[q] += g->players[player].tiles[kg_tile_index(x, y)].kind == KG_TILE_EMPTY;
        }
    }
}

/* Only PICKUP, DROP, and product PLACE can change the shed during worker
 * execution. Replay those effects in worker order, including inventory-order
 * overflow, without copying or mutating a game. Other workers' newly harvested
 * goods cannot appear in the shed until a later action/day boundary. */
KG_HD static inline void kag_mask_prepare_market(KagActionMaskState* s) {
    const Env* env = s->env;
    const KGState* g = &env->game_storage;
    const KGPlayer* p = &g->players[s->player];
    s->cash = p->money;
    s->hands = p->hand_count;
    s->hires = p->hires_today;
    s->plots = kag_popcount(p->unlocked_mask);
    s->land_ready = kag_land_buy_delay_ready(env, s->player);
    memcpy(s->shed, p->shed, sizeof(s->shed));
    memcpy(s->inventory, g->market.inventory, sizeof(s->inventory));
    Agent preview = env->agents[s->player];
    preview.actions = s->choices;
    KGAction work;
    if (s->mode == 3) kag_decode_task_action(&work, &preview, g, s->player);
    else kag_decode_action(&work, &preview, g, s->player);
    int count = work.hand_count + 1;
    if (count > p->unit_count) count = p->unit_count;
    for (int u = 0; u < count; u++) {
        const KGUnitState* unit = &p->units[u];
        const KGUnitAction* a = u ? &work.hands[u - 1] : &work.farmer;
        KGPosition pos = {unit->x, unit->y};
        if (!kg_is_shed_adjacent(&pos, g->config.board_size)) continue;
        int item = a->arg;
        if (a->op == KG_OP_PICKUP && (unsigned)item < KG_NUM_ITEMS) {
            int n = a->n > 0 ? a->n : 1;
            if (n > s->shed[item]) n = s->shed[item];
            s->shed[item] -= n;
        } else if (a->op == KG_OP_DROP) {
            for (int i = 0; i < unit->inventory_order_count; i++) {
                item = unit->inventory_order[i];
                int n = unit->inventory[item];
                int room = g->config.shed_capacity - kag_mask_shed_total(s);
                if (n > room) n = room;
                if (n > 0) s->shed[item] += n;
            }
        } else if (a->op == KG_OP_PLACE && (unsigned)item < KG_NUM_ITEMS
                && p->tiles[kg_tile_index(unit->x, unit->y)].kind != KG_TILE_LOCKED) {
            const KGTile* tile = &p->tiles[kg_tile_index(unit->x, unit->y)];
            if (item >= KG_ITEM_GOOSE && tile->kind == KG_ANIMAL_DEFS[item - KG_ITEM_GOOSE].structure
                    && tile->animal == KG_ANIMAL_INVALID) continue; /* places livestock */
            int n = a->n > 0 ? a->n : 1;
            int room = g->config.shed_capacity - kag_mask_shed_total(s);
            if (n > room) n = room;
            if (n > unit->inventory[item]) n = unit->inventory[item];
            if (n > 0) s->shed[item] += n;
        }
    }
    s->prepared = 1;
}

/* Quantity feasibility at the observed prices plus this player's own prefix.
 * Simultaneous opponent trading can still change prices/partial fills; using
 * its hidden order to mask ours would leak information. */
KG_HD static inline int kag_mask_market_capacity(const KagActionMaskState* s, int command) {
    const KGState* g = &s->env->game_storage;
    KGPolicyMarketSpec spec = kag_market_spec(command);
    if (spec.op == KG_MARKET_SELL) return s->shed[spec.item];
    int remaining = g->config.episode_steps - 2 - g->step;
    if (spec.op == KG_MARKET_HIRE) {
        if (remaining < 1 || g->hour == g->config.turns_per_day - 1
                || s->hands >= kag_policy_hand_limit(s->env)) return 0;
        return s->cash >= kg_hire_cost(s->hires, g->config.farm_hand_cost_mult);
    }
    if (spec.op == KG_MARKET_BUY_LAND) {
        if (remaining < 1 || s->plots >= 4 || !s->land_ready
                || (s->land_used && s->env->land_buy_min_days > 0)) return 0;
        return s->cash >= (1000 << (s->plots - 1));
    }
    if (spec.op == KG_MARKET_BUY_SEED) {
        if (remaining < 1) return 0;
        return s->cash / KG_CROP_DEFS[spec.item].seed_cost;
    }
    int room = g->config.shed_capacity - kag_mask_shed_total(s);
    if (room <= 0) return 0;
    if (spec.op == KG_MARKET_BUY_ANIMAL) {
        if (remaining < 1) return 0;
        int n = s->cash / KG_ANIMAL_DEFS[spec.item - KG_ITEM_GOOSE].cost;
        return n < room ? n : room;
    }
    if (spec.op == KG_MARKET_BUY_PRODUCT) {
        int cost = 0, n = 0;
        while (n < room && n < 10) {
            int price = kg_market_price(spec.item, s->inventory[spec.item] - n - 1);
            if (price > s->cash - cost) break;
            cost += price; n++;
        }
        return n;
    }
    return 0;
}

KG_HD static inline void kag_mask_market_commit(KagActionMaskState* s, int command, int n) {
    KGPolicyMarketSpec spec = kag_market_spec(command);
    const KGState* g = &s->env->game_storage;
    if (spec.op == KG_MARKET_HIRE) {
        s->cash -= kg_hire_cost(s->hires++, g->config.farm_hand_cost_mult);
        s->hands++; return;
    }
    if (spec.op == KG_MARKET_BUY_LAND) {
        s->cash -= 1000 << (s->plots - 1);
        s->plots++; s->land_used++; return;
    }
    int cap = kag_mask_market_capacity(s, command);
    if (n > cap) n = cap;
    for (int j = 0; j < n; j++) {
        if (spec.op == KG_MARKET_SELL) {
            int price = kg_market_price(spec.item, s->inventory[spec.item]);
            s->shed[spec.item]--; s->cash += price;
            if (price > 1) s->inventory[spec.item]++;
        } else if (spec.op == KG_MARKET_BUY_PRODUCT) {
            s->cash -= kg_market_price(spec.item, s->inventory[spec.item] - 1);
            s->inventory[spec.item]--; s->shed[spec.item]++;
        } else if (spec.op == KG_MARKET_BUY_SEED) {
            s->cash -= KG_CROP_DEFS[spec.item].seed_cost;
        } else if (spec.op == KG_MARKET_BUY_ANIMAL) {
            s->cash -= KG_ANIMAL_DEFS[spec.item - KG_ITEM_GOOSE].cost;
            s->shed[spec.item]++;
        }
    }
}

KG_HD static inline void kag_action_mask_before(KagActionMaskState* s,
        int head, unsigned char* mask) {
    if (!s->env) return;
    const Env* env = s->env;
    const KGState* g = &env->game_storage;
    const KGPlayer* p = &g->players[s->player];
    if (head < KG_POLICY_UNIT_HEADS) {
        if (s->mode == 2 && (head == KAG_MACRO_QUANTITY_HEAD || head == KAG_MACRO_TARGET_HEAD)) {
            int macro = (int)s->choices[0];
            int explicit_executor = kag_agent_executor_version(env, s->player);
            int planting = macro >= KAG_MACRO_PLANT_BASE && macro < KAG_MACRO_PLANT_BASE + KG_NUM_CROPS;
            int reclaim = explicit_executor && macro == KAG_EXPLICIT_RECLAIM;
            int fertilize = explicit_executor && macro == KAG_EXPLICIT_FERTILIZE;
            int used = head == KAG_MACRO_TARGET_HEAD ? planting || reclaim || fertilize
                : planting || reclaim || fertilize
                    || (macro >= KAG_MACRO_ANIMAL_BASE && macro < KAG_MACRO_ANIMAL_BASE + KG_NUM_ANIMALS)
                    || (macro >= KAG_MACRO_SELL_BASE && macro < KAG_MACRO_SELL_BASE + KG_NUM_PRODUCTS)
                    || (macro >= KAG_MACRO_BUY_SEED_BASE && macro < KAG_MACRO_BUY_SEED_BASE + KG_NUM_CROPS)
                    || (macro >= KAG_MACRO_BUY_ANIMAL_BASE && macro < KAG_MACRO_BUY_ANIMAL_BASE + KG_NUM_ANIMALS)
                    || macro == KAG_MACRO_SELL_ALL || macro == KAG_MACRO_HIRE
                    || macro == KAG_MACRO_BUY_WHEAT || macro == KAG_MACRO_BUY_FERTILIZER;
            unsigned char* out = mask + head * KG_POLICY_UNIT_COMMANDS;
            if (!used || env->macro_ticks[s->player] > 0) {
                memset(out, 0, KG_POLICY_UNIT_COMMANDS); out[0] = 1;
            } else if (head == KAG_MACRO_TARGET_HEAD) {
                for (int bin = 1; bin < KAG_MACRO_TARGET_BINS; bin++) {
                    int bit = kag_macro_target_from_bin(bin), any = 0;
                    for (int y = 0; y < g->config.board_size; y++) for (int x = 0; x < g->config.board_size; x++) {
                        if (kg_quadrant(x, y, g->config.board_size) != bit) continue;
                        const KGTile* t = &p->tiles[kg_tile_index(x, y)];
                        any |= planting ? t->kind == KG_TILE_EMPTY || kag_explicit_reclaimable(t)
                            : reclaim ? kag_explicit_reclaimable(t)
                            : t->kind == KG_TILE_PLANT && t->fertilized_until_day < g->day;
                    }
                    out[bin] = (p->unlocked_mask & bit) && any;
                }
            }
        } else if (s->mode == 3) {
            unsigned char* out = mask + head * KG_POLICY_UNIT_COMMANDS;
            for (int task = 1; task < KAG_TASK_COUNT; task++) if (out[task]) {
                if (s->task_used[task] >= s->task_capacity[task]) out[task] = 0;
                int crop = kag_task_crop(task);
                if (crop >= 0 && s->seeds_used[crop] >= p->seeds[crop]) out[task] = 0;
                if (crop >= 0 || (task >= KAG_TASK_BUILD_COOP_BASE && task < KAG_TASK_ADD_GOOSE)) {
                    int q = kag_task_quadrant_index(task);
                    if (s->empty_used[q] >= s->empty_capacity[q]) out[task] = 0;
                }
            }
        } else if (s->mode == 0 && head < p->unit_count) {
            unsigned char* out = mask + head * KG_POLICY_UNIT_COMMANDS;
            for (int id = 0; id < KG_POLICY_UNIT_COMMANDS; id++) {
                KGPolicyUnitSpec spec = kag_unit_spec(id);
                if (spec.op == KG_OP_PLANT && s->seeds_used[spec.arg] >= p->seeds[spec.arg]) out[id] = 0;
            }
        }
        return;
    }
    if (s->mode == 1 || s->mode == 2) return;
    if (!s->prepared) kag_mask_prepare_market(s);
    int relative = head - KG_POLICY_UNIT_HEADS;
    int slot = relative / 3, node = relative % 3;
    unsigned char* out = kag_market_slot_mask(mask, slot);
    int stopped = slot >= kag_policy_market_slot_limit(env);
    for (int previous = 0; previous < slot; previous++)
        stopped |= s->choices[KG_POLICY_UNIT_HEADS + 3 * previous] != PUFFER_CONDITIONAL_CONTINUE;
    if (node == 0) {
        int any = 0;
        unsigned char forced[KG_POLICY_MARKET_SLOT_MASK_SIZE];
        int opening = !s->mode && env->opening_turns > g->step && env->agents[s->player].policy == 0;
        if (opening) {
            memcpy(forced, out, sizeof(forced));
            memcpy(s->opening_quantity, out + 23, 8);
        }
        for (int command = 0; command < KG_POLICY_MARKET_COMMANDS; command++) {
            out[2 + command] = !stopped && kag_mask_market_capacity(s, command) > 0
                && (!opening || forced[2 + command]);
            any |= out[2 + command];
        }
        out[0] = 1;
        out[1] = any && (!opening || forced[1]);
        if (opening && out[1] && !forced[0]) out[0] = 0;
        if (!any) out[2] = 1; /* inert command fallback after forced STOP */
        memset(out + 23, 0, 8); out[23] = 1;
    } else if (node == 2) {
        int command = (int)s->choices[head - 1];
        int cap = stopped || s->choices[head - 2] == 0 ? 0 : kag_mask_market_capacity(s, command);
        int opening = !s->mode && env->opening_turns > g->step && env->agents[s->player].policy == 0;
        /* Opening quantities were captured by the base mask. Ordinary masks
         * are recomputed from the selected command, not a union over items. */
        int any = 0;
        for (int q = 0; q < 8; q++) {
            out[23 + q] = kag_market_quantity_spec(q) <= cap && (!opening || s->opening_quantity[q]);
            any |= out[23 + q];
        }
        if (!any) out[23] = 1;
    }
}

KG_HD static inline void kag_action_mask_commit(KagActionMaskState* s, int head, int action) {
    s->choices[head] = (float)action;
    if (!s->env) return;
    if (head < KG_POLICY_UNIT_HEADS) {
        if (s->mode == 3 && action > 0 && action < KAG_TASK_COUNT) {
            s->task_used[action]++;
            int crop = kag_task_crop(action);
            if (crop >= 0) s->seeds_used[crop]++;
            if (crop >= 0 || (action >= KAG_TASK_BUILD_COOP_BASE && action < KAG_TASK_ADD_GOOSE))
                s->empty_used[kag_task_quadrant_index(action)]++;
        } else if (s->mode == 0) {
            KGPolicyUnitSpec spec = kag_unit_spec(action);
            if (spec.op == KG_OP_PLANT) s->seeds_used[spec.arg]++;
        }
        return;
    }
    if (s->mode == 1 || s->mode == 2) return;
    int node = (head - KG_POLICY_UNIT_HEADS) % 3;
    if (node == 1 && s->choices[head - 1] == 1 && action >= KG_POLICY_MARKET_QUANTITY_COMMANDS)
        kag_mask_market_commit(s, action, 1);
    if (node == 2 && s->choices[head - 2] == 1)
        kag_mask_market_commit(s, (int)s->choices[head - 1], kag_market_quantity_spec(action));
}

static inline void kag_sample_cpu_logits(Env* env, int player, const float* logits,
        int deterministic) {
    Agent* agent = &env->agents[player];
    unsigned char temporary[KG_POLICY_ACTION_MASK_SIZE];
    unsigned char* original = agent->action_mask;
    if (!original) agent->action_mask = temporary;
    kag_write_mask(env, player);
    unsigned char* mask = agent->action_mask;
    KagActionMaskState state;
    kag_action_mask_begin(&state, env, player);
    int offset = 0;
    for (int h = 0; h < NUM_ATNS; h++) {
        int size = KG_ACTION_SIZES[h];
        kag_action_mask_before(&state, h, mask);
        int active = 1;
        if (h >= KG_POLICY_UNIT_HEADS) {
            int slot = (h - KG_POLICY_UNIT_HEADS) / 3;
            int node = (h - KG_POLICY_UNIT_HEADS) % 3;
            for (int prev = 0; prev < slot; prev++)
                active &= state.choices[KG_POLICY_UNIT_HEADS + 3 * prev] == 1;
            if (node) active &= state.choices[KG_POLICY_UNIT_HEADS + 3 * slot] == 1;
            if (node == 2) active &= state.choices[h - 1] < KG_POLICY_MARKET_QUANTITY_COMMANDS;
        }
        int selected = 0;
        if (active) {
            float maximum = -INFINITY, sum = 0;
            for (int a = 0; a < size; a++) if (mask[offset + a]) {
                if (logits[offset + a] > maximum) {
                    maximum = logits[offset + a]; selected = a;
                }
            }
            if (!deterministic) {
                for (int a = 0; a < size; a++) if (mask[offset + a]) sum += expf(logits[offset + a] - maximum);
                env->rng = 1664525u * env->rng + 1013904223u;
                float target = (env->rng >> 8) * (1.0f / 16777216.0f) * sum;
                for (int a = 0; a < size; a++) if (mask[offset + a]) {
                    selected = a; target -= expf(logits[offset + a] - maximum);
                    if (target < 0) break;
                }
            }
            kag_action_mask_commit(&state, h, selected);
        }
        agent->actions[h] = (float)selected;
        offset += size;
    }
    agent->action_mask = original;
}
