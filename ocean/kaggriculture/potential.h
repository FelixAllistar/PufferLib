#pragma once
#include "core.h"

// Public economics plus own inventory; no opponent-private state or future data.
#define KAG_POTENTIAL_VERSION 1
#define KAG_POTENTIAL_BASE 95
#define KAG_POTENTIAL_FEATURES 205

typedef struct {
    char magic[8];
    uint32_t version, features, state_version, reserved;
    double gamma;
    float intercept;
    float mean[KAG_POTENTIAL_FEATURES];
    float inverse_scale[KAG_POTENTIAL_FEATURES];
    float weights[KAG_POTENTIAL_FEATURES];
} KagPotential;

KG_HD void kag_potential_features(const KGState* g, int player, float* x) {
    const KGPlayer* p = &g->players[player];
    const KGPlayer* opponent = &g->players[1 - player];
    float crops[5][KG_NUM_CROPS] = {0}, animals[5][KG_NUM_ANIMALS] = {0};
    float empty = 0, weeds = 0, coop = 0, pasture = 0, plants = 0, livestock = 0, due = 0;
    float stock[KG_NUM_ITEMS] = {0}, opponent_plants = 0, opponent_animals = 0;
    int tiles = g->config.board_size * g->config.board_size;
    for (int i = 0; i < tiles; i++) {
        const KGTile* t = &p->tiles[i];
        empty += t->kind == KG_TILE_EMPTY;
        weeds += t->kind == KG_TILE_WEED;
        coop += t->kind == KG_TILE_COOP && !kg_is_animal_tile(t);
        pasture += t->kind == KG_TILE_PASTURE && !kg_is_animal_tile(t);
        if (t->kind == KG_TILE_PLANT) {
            int c = t->crop;
            crops[0][c]++;
            crops[1][c] += t->yield_units > 0;
            crops[2][c] += g->day - t->planted_day;
            crops[3][c] += !t->watered_today;
            crops[4][c] += t->consecutive_unwatered;
            plants++;
            due += !t->watered_today;
        } else if (kg_is_animal_tile(t)) {
            int a = t->animal;
            animals[0][a]++;
            animals[1][a] += t->yield_units;
            animals[2][a] += g->day - t->placed_day;
            animals[3][a] += !t->fed_today;
            animals[4][a] += t->consecutive_unfed;
            livestock++;
            due += !t->fed_today;
            due += t->kind == KG_TILE_PASTURE && !t->cared_today;
        }
        opponent_plants += opponent->tiles[i].kind == KG_TILE_PLANT;
        opponent_animals += kg_is_animal_tile(&opponent->tiles[i]);
    }
    for (int i = 0; i < KG_NUM_ITEMS; i++) {
        stock[i] = p->shed[i];
        for (int u = 0; u < p->unit_count; u++) {
            stock[i] += p->units[u].inventory[i];
        }
    }
    int plots = 0, opponent_plots = 0;
    for (int i = 0; i < 4; i++) {
        plots += (p->unlocked_mask >> i) & 1;
        opponent_plots += (opponent->unlocked_mask >> i) & 1;
    }
    float remaining = (float)(g->config.episode_steps - 1 - g->step) /
                      (g->config.episode_steps - 1);
    int n = 0;
    x[n++] = remaining;
    x[n++] = (float)g->hour / g->config.turns_per_day;
    x[n++] = remaining * remaining;
    x[n++] = (float)p->money / g->config.starting_money;
    x[n++] = (float)opponent->money / g->config.starting_money;
    x[n++] = plots;
    x[n++] = p->unit_count;
    x[n++] = empty;
    x[n++] = weeds;
    x[n++] = coop;
    x[n++] = pasture;
    x[n++] = plants;
    x[n++] = livestock;
    x[n++] = due;
    x[n++] = stock[KG_ITEM_WHEAT] - p->shed[KG_ITEM_WHEAT];
    x[n++] = p->shed[KG_ITEM_WHEAT];
    for (int k = 0; k < 5; k++) {
        for (int c = 0; c < KG_NUM_CROPS; c++) {
            x[n++] = crops[k][c];
        }
    }
    for (int k = 0; k < 5; k++) {
        for (int a = 0; a < KG_NUM_ANIMALS; a++) {
            x[n++] = animals[k][a];
        }
    }
    for (int c = 0; c < KG_NUM_CROPS; c++) {
        x[n++] = p->seeds[c];
    }
    for (int i = 0; i < KG_NUM_ITEMS; i++) {
        x[n++] = stock[i];
    }
    for (int i = 0; i < KG_NUM_PRODUCTS; i++) {
        x[n++] = g->market.prices[i];
    }
    for (int i = 0; i < KG_NUM_PRODUCTS; i++) {
        x[n++] = g->market.inventory[i];
    }
    x[n++] = opponent_plots;
    x[n++] = opponent->unit_count;
    x[n++] = opponent_plants;
    x[n++] = opponent_animals;
    assert(n == KAG_POTENTIAL_BASE);
    for (int i = 3; i < KAG_POTENTIAL_BASE; i++) {
        x[n++] = x[i] * remaining;
    }
    for (int i = 1; i <= 4; i++) {
        x[n++] = plots == i;
        x[n++] = (plots == i) * remaining;
    }
    for (int a = 0; a < KG_NUM_ANIMALS; a++) {
        float supported = animals[0][a] * fminf(4, stock[KG_ITEM_WHEAT] / (livestock + 1));
        x[n++] = supported;
        x[n++] = supported * remaining;
    }
    x[n++] = due / fmaxf(1, p->unit_count);
    x[n++] = due * p->unit_count;
    x[n++] = plots * x[3];
    x[n++] = empty * p->unit_count;
    assert(n == KAG_POTENTIAL_FEATURES);
}

KG_HD float kag_potential_predict(const KagPotential* model, const KGState* game,
        int player, float start_cash) {
    if (game->done) {
        return 0;
    }
    float x[KAG_POTENTIAL_FEATURES];
    kag_potential_features(game, player, x);
    float value = model->intercept;
    for (int i = 0; i < KAG_POTENTIAL_FEATURES; i++) {
        value += model->weights[i] * (x[i] - model->mean[i]) * model->inverse_scale[i];
    }
    // Fit root-start gain, then exactly adjust its known cash baseline for resets.
    // At the last nonterminal state the terminal reward has discount exponent 0.
    int exponent = game->config.episode_steps - 2 - game->step;
    float discount = powf((float)model->gamma, exponent);
    return value - discount * (start_cash / game->config.starting_money - 1);
}

KagPotential kag_potential_load(const char* path) {
    KagPotential model = {0};
    FILE* file = fopen(path, "rb");
    assert(file && fread(&model, sizeof(model), 1, file) == 1);
    assert(fgetc(file) == EOF);
    fclose(file);
    assert(!memcmp(model.magic, "KGRIDGE1", 8));
    assert(model.version == KAG_POTENTIAL_VERSION && model.features == KAG_POTENTIAL_FEATURES);
    assert(model.state_version == KG_STATE_SERIALIZATION_VERSION && model.reserved == 0);
    assert(isfinite(model.gamma) && model.gamma > 0 && model.gamma <= 1);
    assert(isfinite(model.intercept));
    for (int i = 0; i < KAG_POTENTIAL_FEATURES; i++) {
        assert(isfinite(model.weights[i]) && isfinite(model.mean[i]));
        assert(isfinite(model.inverse_scale[i]) && model.inverse_scale[i] > 0);
    }
    return model;
}

int kag_potential_size(void) { return sizeof(KagPotential); }
int kag_potential_count(void) { return KAG_POTENTIAL_FEATURES; }
