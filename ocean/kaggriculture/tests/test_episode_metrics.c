#include <assert.h>
#include "../kaggriculture.h"

static void run_case(int replay, int seat, int add_progress) {
    Env* e = calloc(1, sizeof(*e));
    obs_t obs[2 * OBS_SIZE]; float acts[2 * NUM_ATNS] = {0}, rewards[2], done[2];
    unsigned char mask[2 * KG_POLICY_ACTION_MASK_SIZE];
    KGConfig cfg; kg_config_default(&cfg); kg_init(&e->game_storage, &cfg);
    e->macro_mode = 0; e->frozen_macro_mode = e->frozen_macro_executor_version = -1;
    e->observation_version = 3; e->frozen_observation_version = -1;
    e->macro_decision_interval = 1; e->frozen_macro_decision_interval = -1;
    e->policy_max_hands = 16; e->policy_market_slots = 10;
    e->curriculum_stage = -1; e->num_agents = 2; e->tag = 1;
    Dict kwargs = {0}; kag_reward_configure(e, &kwargs);
    for (int p = 0; p < 2; p++) {
        e->agents[p].observations = obs + p * OBS_SIZE;
        e->agents[p].actions = acts + p * NUM_ATNS;
        e->agents[p].rewards = rewards + p; e->agents[p].terminals = done + p;
        e->agents[p].action_mask = mask + p * KG_POLICY_ACTION_MASK_SIZE;
        e->agents[p].policy = p == seat ? 0 : 1;
    }
    KGState* g = &e->game_storage;
    g->step = 718; g->day = 29; g->hour = 22; e->reset_source = replay;
    for (int p = 0; p < 2; p++) {
        g->players[p].money = 100000;
        kg_do_buy_land(g, &g->players[p]); kg_do_buy_land(g, &g->players[p]);
        g->planted_crops[p] = 190; g->placed_animals[p] = 13;
        g->production_units[p] = 963; g->production_value[p] = 50000;
        g->sold_units[p] = 1027; g->sales_revenue[p] = 70000;
        g->bought_units[p] = 54; g->purchase_spend[p] = 9000;
        g->production_product_units[p][KG_ITEM_MILK] = 963;
        g->production_product_value[p][KG_ITEM_MILK] = 50000;
        g->sold_product_units[p][KG_ITEM_MILK] = 1027;
        g->sold_product_revenue[p][KG_ITEM_MILK] = 70000;
        g->plant_days[p] = 100; g->watered_plant_days[p] = 60;
        g->neglect_deaths[p] = 20;
        KGState before = *g;
        kag_reward_reset(e, p);
        assert(!memcmp(&before, g, sizeof(before)));
    }
    KGAction actions[2] = {0};
    actions[seat].market_count = 1;
    actions[seat].market[0] = (KGMarketOrder){KG_MARKET_SELL, KG_ITEM_MILK, 1};
    actions[1 - seat].market_count = 1;
    actions[1 - seat].market[0] = (KGMarketOrder){KG_MARKET_BUY_ANIMAL, KG_ITEM_COW, 1};
    kag_log_actions(e, g, actions);
    assert(e->log.sell_orders == 0 && e->log.animal_buy_orders == 0);
    if (add_progress) {
        kg_do_buy_land(g, &g->players[seat]);
        g->production_units[seat] += 7; g->production_value[seat] += 70;
        g->production_product_units[seat][KG_ITEM_MILK] += 7;
        g->production_product_value[seat][KG_ITEM_MILK] += 70;
        g->sold_units[seat] += 4; g->sales_revenue[seat] += 40;
        g->sold_product_units[seat][KG_ITEM_MILK] += 4;
        g->sold_product_revenue[seat][KG_ITEM_MILK] += 40;
        g->planted_crops[seat] += 2; g->placed_animals[seat] += 1;
        g->neglect_deaths[seat] += 3;
        g->plant_days[seat] += 4; g->watered_plant_days[seat] += 3;
    }
    puf_step(e);
    assert(done[0] && e->log.n == 1);
    assert(e->log.episode_length == 1 && e->log.start_money == 97000);
    assert(e->log.start_plots == 3 && e->log.ending_plots == 3 + add_progress);
    assert(e->log.land_purchases == add_progress);
    assert(e->log.growth_land_reward == 0); /* Fourth plot exceeds reward target. */
    assert(e->log.successful_plants == 2 * add_progress);
    assert(e->log.successful_animal_places == add_progress);
    assert(e->log.production_units == 7 * add_progress);
    assert(e->log.milk_units == 7 * add_progress && e->log.milk_value == 70 * add_progress);
    assert(e->log.sold_units == 4 * add_progress && e->log.milk_sold_units == 4 * add_progress);
    assert(e->log.sales_revenue == 40 * add_progress && e->log.milk_sales_revenue == 40 * add_progress);
    assert(e->log.bought_units == 0 && e->log.purchase_spend == 0);
    assert(e->log.neglect_deaths == 3 * add_progress);
    assert(e->log.water_coverage == (add_progress ? .75f : 1.0f));
    assert(e->log.sell_orders == 1 && e->log.animal_buy_orders == 0);
    assert(e->log.starts[replay].animal_units == 7 * add_progress);
    assert(e->log.starts[1 - replay].money == 0);
    Dict out = {0}; puf_log(&e->log, &out);
    assert(dict_get(&out, replay ? "reset_steps" : "root_steps") == 1);
    assert(dict_get(&out, replay ? "reset_animal_units" : "root_animal_units") == 7 * add_progress);
    dict_clear(&out); free(e);
}
int main(void) {
    for (int source = 0; source < 2; source++) for (int seat = 0; seat < 2; seat++)
        for (int progress = 0; progress < 2; progress++) run_case(source, seat, progress);
    puts("episode metrics: inherited counters excluded, completed action windows, both learner seats, source splits PASS");
}
