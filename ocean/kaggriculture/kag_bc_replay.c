/* CPU-only offline bridge over the canonical controller and stateful rewards. */
#include "kaggriculture.h"
#ifndef KAG_BC_SOURCE_HASH
#define KAG_BC_SOURCE_HASH 0ULL
#endif

typedef struct {
    Env env;
    float obs[2][OBS_SIZE], rewards[2], terminals[2];
    unsigned char mask[2][KAG_ALL_LOGITS];
    uint64_t semantics_hash;
    double gamma;
} KagBCReplay;

uint64_t kag_bc_source_hash(void) { return KAG_BC_SOURCE_HASH; }
int kag_bc_abi(int field) {
    const int values[] = {OBS_SIZE, NUM_ATNS, KAG_ALL_LOGITS, 5};
    return (unsigned)field < 4 ? values[field] : -1;
}
int kag_bc_executor(KagBCReplay* r) { return 2; }

KagBCReplay* kag_bc_create(const KGConfig* config, const char* profile) {
    Ini ini = {0};
    puf_ini_load_env(&ini, "kaggriculture", 0, NULL);
    puf_ini_load_file(&ini, profile);
    assert(puf_ini_get(&ini, "train", "reward_clip") == 0);
    Dict* d = puf_ini_section(&ini, "env", 0);
    dict_set(d, "num_agents", 2);
    dict_set(d, "reset_state_prob", 0);
    KagBCReplay* r = (KagBCReplay*)calloc(1, sizeof(*r));
    puf_init(&r->env, d);
    KGConfig expected;
    kg_config_default(&expected);
    assert(!memcmp(config, &expected, offsetof(KGConfig, seed)));
    kg_init(&r->env.game, config);
    kag_policy_reset(&r->env.policy, &r->env.game, 0);
    r->gamma = puf_ini_get(&ini, "train", "gamma");
    /* Hash explicit fields, not pointers or struct padding. Game seeds vary. */
    uint64_t h = 14695981039346656037ULL;
    const unsigned char* bytes = (const unsigned char*)config;
    for (size_t i = 0; i < offsetof(KGConfig, seed); i++) {
        h = (h ^ bytes[i]) * 1099511628211ULL;
    }
    const char* keys[] = {"market_slots", "max_hands", "land_buy_min_days",
        "reward_money", "reward_growth_land", "reward_growth_crop", "reward_growth_animal",
        "reward_alive_daily", "reward_quality_scale", "reward_quality_idle_cost",
        "reward_target_plots", "reward_target_crops", "reward_target_animals"};
    for (int i = 0; i < 13; i++) {
        double value = dict_get(d, keys[i]);
        bytes = (const unsigned char*)&value;
        for (size_t j = 0; j < sizeof(value); j++) {
            h = (h ^ bytes[j]) * 1099511628211ULL;
        }
    }
    r->semantics_hash = h;
    for (int p = 0; p < 2; p++) {
        r->env.agents[p].rewards = r->rewards + p;
        r->env.agents[p].terminals = r->terminals + p;
        r->env.agents[p].observations = r->obs[p];
        kag_write_observation(&r->env.policy, &r->env.game, p, r->obs[p]);
        kag_write_mask(&r->env.policy, &r->env.game, p, r->mask[p]);
    }
    puf_ini_free(&ini);
    return r;
}
void kag_bc_destroy(KagBCReplay* r) { free(r); }
uint64_t kag_bc_semantics_hash(KagBCReplay* r) { return r->semantics_hash; }
double kag_bc_gamma(KagBCReplay* r) { return r->gamma; }
const KGState* kag_bc_state(KagBCReplay* r) { return &r->env.game; }

int kag_bc_positions(KagBCReplay* r, int p, int* output, int capacity) {
    if (!r || p < 0 || p >= 2 || !output) return -1;
    const KGPlayer* player = &r->env.game.players[p];
    if (capacity < 2 * player->unit_count) return -1;
    for (int u = 0; u < player->unit_count; u++) {
        output[2 * u] = player->units[u].x;
        output[2 * u + 1] = player->units[u].y;
    }
    return player->unit_count;
}

/* Current-state, own-action facts for supervision. Four integers per worker:
 * standing crop (-1 otherwise), tile changed, goods deposited, action changed
 * anything. Worker ordering and atomic seed rejection match the production
 * core. No future state, hidden intent or opponent action is consulted. */
int kag_bc_work_facts(KagBCReplay* r, int p, const KGAction* action,
        int* facts, int capacity) {
    if (!r || (unsigned)p >= 2 || !action || !facts || capacity < 4*(KG_MAX_HANDS+1)) return 0;
    KGState copy = r->env.game;
    KGPlayer* f = &copy.players[p];
    int blocked[KG_NUM_CROPS]; kg_validate_plant_atomic(action,f,blocked);
    memset(facts,0,4*(KG_MAX_HANDS+1)*sizeof(int));
    for (int u = 0; u < f->unit_count; u++) {
        KGUnitState before = f->units[u];
        int tile = kg_tile_index(before.x,before.y);
        KGTile prior = f->tiles[tile];
        int shed = kg_shed_total(f);
        facts[4*u] = prior.kind == KG_TILE_PLANT ? prior.crop : -1;
        if (u > action->hand_count) continue;
        const KGUnitAction* a = u ? &action->hands[u-1] : &action->farmer;
        if (a->op == KG_OP_PLANT && (unsigned)a->arg < KG_NUM_CROPS && blocked[a->arg]) continue;
        kg_apply_unit_action(&copy,f,u,a);
        facts[4*u+1] = memcmp(&prior,&f->tiles[tile],sizeof(prior)) != 0;
        int deposited = kg_shed_total(f)-shed;
        facts[4*u+2] = deposited > 0 ? deposited : 0;
        facts[4*u+3] = facts[4*u+1] || memcmp(&before,&f->units[u],sizeof(before)) != 0;
    }
    return f->unit_count;
}

/* Offline audit only: what actually changed during worker execution, plus
 * each order's realized fill under the original simultaneous action pair.
 * This does NOT run inside the controller/masks or supply opponent actions to
 * a policy. The copy never advances the live replay or its reward history. */
int kag_bc_effects(KagBCReplay* r, int p, const KGAction* pair,
        int* unit_changed, int* fills, int capacity) {
    if (!r || (unsigned)p >= 2 || !pair || !unit_changed || !fills
            || capacity < KG_MAX_HANDS+1) return 0;
    KGState copy = r->env.game;
    memset(unit_changed,0,(KG_MAX_HANDS+1)*sizeof(int));
    memset(fills,0,KG_MAX_MARKET_ORDERS*sizeof(int));
    for (int player = 0; player < 2; player++) {
        KGPlayer* f = &copy.players[player];
        int blocked[KG_NUM_CROPS]; kg_validate_plant_atomic(&pair[player],f,blocked);
        int count = pair[player].hand_count+1;
        if (count > f->unit_count) count = f->unit_count;
        for (int u = 0; u < count; u++) {
            const KGUnitAction* a = u ? &pair[player].hands[u-1] : &pair[player].farmer;
            if (a->op == KG_OP_PLANT && (unsigned)a->arg < KG_NUM_CROPS && blocked[a->arg]) continue;
            KGUnitState before = f->units[u];
            int tile = kg_tile_index(before.x,before.y);
            KGTile prior = f->tiles[tile];
            kg_apply_unit_action(&copy,f,u,a);
            if (player == p) unit_changed[u] = memcmp(&before,&f->units[u],sizeof(before)) != 0
                || memcmp(&prior,&f->tiles[tile],sizeof(prior)) != 0;
        }
    }
    for (int slot = 0; slot < copy.config.max_market_orders_per_turn && slot < KG_MAX_MARKET_ORDERS; slot++) {
        KGAction one[2] = {{0},{0}};
        for (int player = 0; player < 2; player++) if (slot < pair[player].market_count) {
            one[player].market_count = 1; one[player].market[0] = pair[player].market[slot];
        }
        KGPlayer* f = &copy.players[p];
        KGMarketOrder order = one[p].market[0];
        int before = order.op == KG_MARKET_HIRE ? f->hand_count
            : order.op == KG_MARKET_BUY_LAND ? kag_popcount(f->unlocked_mask)
            : order.op == KG_MARKET_BUY_SEED && (unsigned)order.item < KG_NUM_CROPS ? f->seeds[order.item]
            : (unsigned)order.item < KG_NUM_ITEMS ? f->shed[order.item] : 0;
        kg_process_market(&copy,one);
        int after = order.op == KG_MARKET_HIRE ? f->hand_count
            : order.op == KG_MARKET_BUY_LAND ? kag_popcount(f->unlocked_mask)
            : order.op == KG_MARKET_BUY_SEED && (unsigned)order.item < KG_NUM_CROPS ? f->seeds[order.item]
            : (unsigned)order.item < KG_NUM_ITEMS ? f->shed[order.item] : 0;
        fills[slot] = slot < pair[p].market_count ? kag_abs(after-before) : 0;
    }
    return 1;
}

int kag_bc_view(KagBCReplay* r, int p, float* obs, unsigned char* mask) {
    if (!r || p < 0 || p >= 2 || !obs || !mask) return 0;
    memcpy(obs, r->obs[p], sizeof(r->obs[p]));
    memcpy(mask, r->mask[p], sizeof(r->mask[p]));
    return 1;
}

int kag_bc_teacher_mask(KagBCReplay* r, int p, const float* heads, unsigned char* mask) {
    if (!r || p < 0 || p >= 2 || !heads || !mask) return 0;
    memcpy(mask, r->mask[p], sizeof(r->mask[p]));
    KagActionMaskState prefix;
    kag_action_mask_begin(&prefix, &r->env.game, &r->env.policy, p);
    for (int head = 0; head < NUM_ATNS; head++) {
        kag_action_mask_before(&prefix, head, mask);
        kag_action_mask_commit(&prefix, head, (int)heads[head]);
    }
    return 1;
}

/* Preview the exact executor without changing history or land-fill timers. */
int kag_bc_decode(KagBCReplay* r, int p, float* heads, KGAction* action) {
    if (!r || p < 0 || p >= 2 || !heads || !action) return 0;
    Env copy = r->env;
    kag_decode_multi_action(action, heads, &copy.game, p, &copy.policy);
    return 1;
}

int kag_bc_step(KagBCReplay* r, const KGAction* pair, int teacher,
        float* history_heads, float* rewards) {
    if (!r || !pair || teacher < 0 || teacher >= 2 || !history_heads || !rewards
            || kg_done(&r->env.game)) return 0;
    /* The original replay has no latent macro labels. Record the documented
     * teacher-forced projection as history, but execute the ORIGINAL actions. */
    KGAction ignored;
    kag_decode_multi_action(&ignored, history_heads, &r->env.game, teacher, &r->env.policy);
    kag_apply_actions(&r->env, pair);
    memcpy(rewards, r->rewards, sizeof(r->rewards));
    for (int p = 0; p < 2; p++) {
        kag_write_observation(&r->env.policy, &r->env.game, p, r->obs[p]);
        kag_write_mask(&r->env.policy, &r->env.game, p, r->mask[p]);
    }
    return 1;
}
