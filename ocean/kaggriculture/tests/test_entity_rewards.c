#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../kaggriculture.h"

static void near(float a, float b) {
    if (fabsf(a - b) > 2e-5f * (1.0f + fabsf(b))) {
        fprintf(stderr, "not close: %.9g versus %.9g\n", a, b); abort();
    }
}
static Env* fixture(void) {
    Env* e = calloc(1, sizeof(*e));
    KGConfig c; kg_config_default(&c); c.weed_spawn_chance = 0;
    kg_init(&e->game_storage, &c);
    e->reward = (KagRewardConfig){.money_scale=1, .quality_scale=1,
        .quality_idle_cost=0.25f, .gamma=0.997f, .cash_weight=1,
        .stock_weight=1, .crop_weight=0.25f, .animal_weight=0.25f};
    e->macro_mode = KAG_MACRO_MODE_TASKS;
    e->frozen_macro_mode = e->frozen_observation_version = e->frozen_macro_executor_version = -1;
    e->observation_version = KAG_OBSERVATION_ENTITIES;
    for (int p = 0; p < 2; p++) kag_reward_reset(e, p);
    return e;
}
static void fill_plot(Env* e, int pid, int q) {
    KGPlayer* p = &e->game_storage.players[pid];
    p->unlocked_mask |= 1 << q;
    for (int y = 0; y < 10; y++) for (int x = 0; x < 10; x++) {
        if (kg_quadrant(x, y, 10) != (1 << q)) continue;
        int i = kg_tile_index(x, y);
        kg_new_animal(p, i, KG_COW, 0);
        p->tiles[i].fed_today = 1;
    }
}
static void quality(void) {
    Env* e = fixture();
    float c, idle; int crops, animals;
    fill_plot(e, 0, 0);
    kag_quality_components(&e->game_storage, 0, &c, &idle, &crops, &animals);
    near(c, 0.25f); near(idle, 0); assert(animals == 25 && crops == 0);
    e->game_storage.players[0].unlocked_mask = 15;
    kag_quality_components(&e->game_storage, 0, &c, &idle, &crops, &animals);
    near(c, 0.25f); near(idle, 1);
    for (int q = 1; q < 4; q++) fill_plot(e, 0, q);
    kag_quality_components(&e->game_storage, 0, &c, &idle, &crops, &animals);
    near(c, 1); near(idle, 0);
    for (int y = 0; y < 5; y++) for (int x = 0; x < 5; x++)
        kg_set_player_tile(&e->game_storage.players[0], kg_tile_index(x, y), KG_TILE_EMPTY);
    kag_quality_components(&e->game_storage, 0, &c, &idle, &crops, &animals);
    near(c, 0); /* Extra farms cannot conceal an abandoned first plot. */
    free(e);
}
static void viable(void) {
    Env* e = fixture(); KGState* g = &e->game_storage; KGPlayer* p = &g->players[0];
    g->day = 29; g->step = 700;
    kg_new_plant(p, 0, KG_WHEAT, 29, 24); p->tiles[0].watered_today = 1;
    assert(p->tiles[0].yield_units == 1 && !kag_quality_tile(g, &p->tiles[0]));
    kg_new_plant(p, 0, KG_WHEAT, 27, 24); p->tiles[0].watered_today = 1;
    assert(kag_quality_tile(g, &p->tiles[0]));
    kg_new_animal(p, 0, KG_COW, 25); p->tiles[0].fed_today = 1;
    assert(!kag_quality_tile(g, &p->tiles[0]));
    p->tiles[0].yield_units = 1; assert(kag_quality_tile(g, &p->tiles[0]));
    p->tiles[0].fed_today = 0; p->tiles[0].consecutive_unfed = 1;
    assert(!kag_quality_tile(g, &p->tiles[0]));
    g->day = 12; g->step = 288;
    kg_new_plant(p, 0, KG_TOMATO, 0, 24); p->tiles[0].watered_today = 1;
    assert(!kag_quality_tile(g, &p->tiles[0])); /* Four events already elapsed. */
    g->config.episode_steps = 100; g->config.turns_per_day = 10; g->day = 7;
    kg_new_plant(p, 0, KG_WHEAT, 7, 10); p->tiles[0].watered_today = 1;
    assert(kag_quality_tile(g, &p->tiles[0]));
    p->tiles[0].planted_day = 8; assert(!kag_quality_tile(g, &p->tiles[0]));
    g->config.episode_steps = 101;
    assert(!kag_quality_tile(g, &p->tiles[0])); /* Day 10 begins only after the final action. */
    free(e);
}
static void terminal_and_pbrs(void) {
    Env* e = fixture();
    e->reward.pbrs_scale = 0.7f;
    e->game_storage.players[0].money = 50000;
    e->game_storage.players[0].shed[KG_ITEM_MILK] = 8;
    fill_plot(e, 0, 0);
    e->game_storage.step = 400; e->reset_source = 1;
    kag_reward_reset(e, 0);
    float initial_phi = e->reward_state[0].phi;
    assert(initial_phi > 0 && e->reward_state[0].coverage_sum == 0);
    float total_discounted = 0, discount = 1;
    for (int step = 0; step < 20; step++) {
        e->game_storage.step++;
        e->game_storage.players[0].money += 100;
        e->game_storage.players[0].shed[KG_ITEM_MILK] = step % 8;
        int done = step == 19;
        float reward = kag_reward_step(e, 0, done);
        float terminal = done ? e->reward_state[0].money_reward + e->reward_state[0].quality_reward : 0;
        total_discounted += discount * (reward - terminal);
        discount *= e->reward.gamma;
    }
    near(e->reward_state[0].money_reward, 2000.0f / 3000.0f);
    near(e->reward_state[0].quality_reward, 20 * 0.25f / 720);
    near(total_discounted, -e->reward.pbrs_scale * initial_phi);
    near(e->reward_state[0].discounted_pbrs, total_discounted);
    near(e->reward_state[0].phi, 0);
    kag_reward_reset(e, 0);
    near(e->reward_state[0].coverage_sum, 0);
    near(e->reward_state[0].money_reward, 0);
    assert(e->reward_state[0].start_cash == 52000);
    free(e);
}
static void sustained(void) {
    Env* a = fixture(); Env* b = fixture();
    fill_plot(a, 0, 0);
    for (int step = 0; step < 100; step++) {
        if (step == 99) fill_plot(b, 0, 0);
        kag_reward_step(a, 0, step == 99); kag_reward_step(b, 0, step == 99);
    }
    near(a->reward_state[0].quality_reward, 100 * b->reward_state[0].quality_reward);
    assert(a->reward_state[0].quality_reward > 0);
    free(a); free(b);
}
static void quotes(void) {
    Env* e = fixture(); KGState* g = &e->game_storage;
    const int stocks[] = {0, 9000, 9999, 10000, 10001, 10500, 15000, 100000};
    for (int item = 0; item < 9; item++) for (unsigned i = 0; i < sizeof(stocks)/sizeof(stocks[0]); i++) {
        g->market.inventory[item] = stocks[i]; kg_refresh_prices(g); kag_update_quote_cache(e, item);
        for (int bin = 0; bin < 8; bin++) for (int bulk = 0; bulk < 2; bulk++) {
            int n = bulk ? kag_bulk_quote_quantity(bin) : kag_market_quantity_spec(bin);
            KGState copy = *g; copy.players[0].shed[item] = n;
            int before = copy.players[0].money;
            for (int j = 0; j < n; j++) {
                int quote = kg_market_price(item, copy.market.inventory[item]);
                assert(kg_commit_unit(&copy, 0, KG_MARKET_SELL, item, quote));
            }
            near(e->quote_cache[item].quotes[16 * bulk + bin] * 10000, (float)copy.players[0].money - before);
            near(e->quote_cache[item].quotes[16 * bulk + 8 + bin] * 1000, kg_market_price(item, copy.market.inventory[item]));
        }
    }
    free(e);
}
static void rejects_bad_config(void) {
    for (int kind = 0; kind < 4; kind++) {
        pid_t child = fork(); assert(child >= 0);
        if (!child) {
            Dict d = {0}; Env* e = fixture();
            if (kind == 0) { dict_set(&d, "reward_pbrs_scale", 1); kag_reward_bind_train_config(&d, 0.99f, 1); }
            if (kind == 1) dict_set(&d, "reward_phase_scale", 1);
            if (kind == 2) dict_set(&d, "reward_quality_scale", NAN);
            if (kind == 3) dict_set(&d, "reward_pbrs_scale", 1);
            kag_reward_configure(e, &d); _exit(0);
        }
        int status; waitpid(child, &status, 0); assert(WIFEXITED(status) && WEXITSTATUS(status) == 1);
    }
}
static void dense_growth(void) {
    Env* e = fixture(); Dict d = {0}; kag_reward_configure(e, &d);
    assert(e->reward.money_timing == 1 && e->reward.target_plots == 3
        && e->reward.target_animals == 15 && kag_reward_crop_target(e) == 60);
    KGPlayer* p = &e->game_storage.players[0];
    kag_reward_reset(e, 0);
    p->money -= 300; near(kag_reward_step(e, 0, 0), -0.1f);
    p->money += 600; near(kag_reward_step(e, 0, 0), 0.2f);
    near(kag_reward_step(e, 0, 0), 0);
    p->unlocked_mask = 3; near(kag_reward_step(e, 0, 0), 1);
    p->unlocked_mask = 7; near(kag_reward_step(e, 0, 0), 1);
    p->unlocked_mask = 15; near(kag_reward_step(e, 0, 0), 0); /* Three TOTAL plots. */
    e->reward.alive_daily = 0;
    kg_new_plant(p, 0, KG_WHEAT, 0, 24); p->tiles[0].watered_today = 1;
    near(kag_reward_step(e, 0, 0), 0.05f);
    kg_set_player_tile(p, 0, KG_TILE_EMPTY); near(kag_reward_step(e, 0, 0), 0);
    kg_new_plant(p, 0, KG_WHEAT, 0, 24); p->tiles[0].watered_today = 1;
    near(kag_reward_step(e, 0, 0), 0); /* Kill/replant below the peak pays nothing. */
    kg_new_animal(p, 1, KG_COW, 0); p->tiles[1].fed_today = 1;
    near(kag_reward_step(e, 0, 0), 0.25f);
    kag_reward_reset(e, 0); near(kag_reward_step(e, 0, 0), 0); /* No inherited bonus. */
    near(e->reward_state[0].growth_crop_reward, 0);
    e->reward.alive_daily = 0.05f;
    near(kag_reward_step(e, 0, 0), 0.05f * (1.0f / 60 + 1.0f / 15) / 48);
    p->tiles[0].watered_today = 0; p->tiles[0].consecutive_unwatered = 1;
    p->tiles[1].fed_today = 0; p->tiles[1].consecutive_unfed = 1;
    near(kag_reward_step(e, 0, 0), 0);
    /* Full caps, then an extra producer beyond each cap. */
    for (int t = 0; t < 100; t++) {
        if (t < 15) { kg_new_animal(p, t, KG_COW, 0); p->tiles[t].fed_today = 1; }
        else { kg_new_plant(p, t, KG_WHEAT, 0, 24); p->tiles[t].watered_today = 1; }
    }
    e->reward.alive_daily = 0;
    near(kag_reward_step(e, 0, 0), 59 * 0.05f + 14 * 0.25f);
    kg_new_animal(p, 99, KG_COW, 0); p->tiles[99].fed_today = 1;
    near(kag_reward_step(e, 0, 1), 0);
    free(e); free(d.items);
}
int main(void) {
    quality(); viable(); terminal_and_pbrs(); sustained(); quotes(); rejects_bad_config(); dense_growth();
    puts("entity rewards: dense/terminal cash, capped growth, alive upkeep, reset accounting, optional quality/PBRS PASS");
}
