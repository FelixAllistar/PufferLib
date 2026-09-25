/* Public observations and our private inventory only. No trainer dependency. */
#include "../policy.h"

typedef struct {
    KGState game;
    KagPolicy policy;
    unsigned int rng;
    float actions[KAG_ACTION_HEADS];
    unsigned char mask[KAG_ALL_LOGITS];
    int last_step;
} Export;

Export* export_create(void) {
    Export* x = calloc(1, sizeof(*x));
    assert(x);
    x->policy.market_slots = 10;
    x->policy.max_hands = 16;
    kg_config_default(&x->game.config);
    x->rng = 97;
    x->last_step = -1;
    return x;
}

void export_free(Export* x) { free(x); }
uint32_t export_rng(Export* x) { return x->rng; }

void export_begin(Export* x, int step, int day, int hour) {
    KGConfig config = x->game.config;
    memset(&x->game, 0, sizeof(x->game));
    x->game.config = config;
    x->game.step = step;
    x->game.day = day;
    x->game.hour = hour;
}

/* Fixed int32 interface, independent of compiler struct padding. */
void export_farm(Export* x, int p, const int* v) {
    KGPlayer* f = &x->game.players[p];
    f->money = v[0];
    f->unlocked_mask = v[1];
    f->hires_today = v[2];
    f->hand_count = v[3];
    f->unit_count = v[3] + 1;
    for (int u = 0; u < f->unit_count; u++) {
        kg_set_unit_position(f, u, v[4 + 2*u], v[5 + 2*u]);
    }
}

void export_tile(Export* x, int p, int i, const int* v) {
    KGPlayer* f = &x->game.players[p];
    KGTile* t = &f->tiles[i];
    kg_set_player_tile(f, i, v[0]);
    t->crop = v[1];
    t->animal = v[2];
    t->planted_day = v[3];
    t->placed_day = v[4];
    t->fertilized_until_day = v[5];
    t->watered_today = v[6];
    t->consecutive_unwatered = v[7];
    t->yield_units = v[8];
    t->max_lifespan_step = v[9];
    t->consecutive_unfed = v[10];
    t->fed_today = v[11];
    t->cared_today = v[12];
    t->fertilizer_available = v[13];
    t->pending_care_bonus = v[14];
}

void export_private(Export* x, int p, const int* shed, const int* seeds) {
    memcpy(x->game.players[p].shed, shed, 12 * sizeof(int));
    memcpy(x->game.players[p].seeds, seeds, 5 * sizeof(int));
}

void export_inventory(Export* x, int p, int u, int item, int n) {
    kg_inventory_add(&x->game.players[p].units[u], item, n);
}

void export_market(Export* x, const int* inventory, const int* prices,
    const int* shops, int count) {
    memcpy(x->game.market.inventory, inventory, 9 * sizeof(int));
    memcpy(x->game.market.prices, prices, 9 * sizeof(int));
    x->game.shop_count = count;
    memcpy(x->game.unlocked_shops, shops, count * sizeof(int));
}

int export_view(Export* x, int p, float* out) {
    int step = x->game.step;
    if (x->last_step < 0) {
        if (step != 0) {
            return 0; // Midgame history cannot be reconstructed from one frame.
        }
        kag_policy_reset(&x->policy, &x->game, 0);
    } else if (step == x->last_step + 1) {
        kag_policy_step(&x->policy, &x->game);
    } else if (step != x->last_step) {
        return 0;
    }
    x->last_step = step;
    kag_write_observation(&x->policy, &x->game, p, out);
    return 1;
}

void export_action(Export* x, int p, const float* logits, int deterministic, KGAction* out) {
    kag_sample_cpu_logits(&x->policy, &x->game, p, logits, deterministic,
        &x->rng, x->actions, x->mask);
    kag_decode_multi_action(out, x->actions, &x->game, p, &x->policy);
}

void export_debug(Export* x, float* actions, unsigned char* mask) {
    memcpy(actions, x->actions, sizeof(x->actions));
    memcpy(mask, x->mask, sizeof(x->mask));
}
