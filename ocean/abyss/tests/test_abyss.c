#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../abyss.h"

static void assert_finite_observation(const obs_t* observation) {
    for (int i = 0; i < OBS_SIZE; i++) {
        assert(isfinite(observation[i]));
    }
}

int main(void) {
    Ini ini = {0};
    puf_ini_load_env(&ini, "abyss", 0, NULL);
    Dict* cfg = puf_ini_section(&ini, "env", 0);

    Env env = {0};
    obs_t observations[OBS_SIZE] = {0};
    float actions[NUM_ATNS] = {0};
    float rewards[1] = {0};
    float terminals[1] = {0};
    unsigned char action_mask[ABYSS_ACTION_MASK_SIZE] = {0};
    env.agents[0].observations = observations;
    env.agents[0].actions = actions;
    env.agents[0].rewards = rewards;
    env.agents[0].terminals = terminals;
    env.rng = 1;
    puf_init(&env, cfg);
    env.agents[0].action_mask = action_mask;
    puf_reset(&env);

    assert(env.num_agents == 1);
    assert(env.configured_scenario_episode == -1);
    assert(env.hard_scenario_probability == 0);
    assert(fabsf(env.weather_high_penalty_probability-.10f)<.0001f);
    assert(env.room == 1);
    assert(env.entity_count >= 2);
    assert_finite_observation(observations);
    assert(ab_turret_hit_chance(0, 1, 1, 1000, 2000, 1000) == 1.0f);
    assert(ab_missile_damage_fraction(372, 0, 20, 150, .644f, 36001, 36000) == 0);
    assert(fabsf(ab_missile_damage_fraction(10, 0, 20, 150, .644f, 1000, 36000)
        - .5f) < .0001f);
    float missile_expected=powf(150.0f*40.0f/(125.0f*1000.0f),.682f);
    assert(fabsf(ab_missile_damage_fraction(40, 1000, 125, 150, .682f, 1000, 36000)
        - missile_expected) < .0001f);
    int pacifier=-1;
    for(int i=0;i<GENERATED_NPC_COUNT;i++)
        if(fabsf(GENERATED_NPCS[i].missile_dps-39.6f)<.001f)pacifier=i;
    assert(pacifier>=0);
    assert(GENERATED_NPCS[pacifier].turret_dps == 0);
    assert(GENERATED_NPCS[pacifier].missile_range == 36000);
    assert(GENERATED_NPCS[pacifier].missile_explosion_radius == 20);
    assert(GENERATED_NPCS[pacifier].missile_explosion_velocity == 150);
    assert(fabsf(GENERATED_NPCS[pacifier].missile_drf-.644f)<.0001f);
    assert(ab_lock_time(578.0f, 40.0f) > 0.0f);
    assert(fabsf(env.cap_recharge_time - 227.0f) < 0.001f);
    assert(fabsf(env.shield_recharge_time - 531.25f) < 0.001f);
    assert(fabsf(env.signature - 62.0f) < 0.001f);
    assert(fabsf(env.prop_signature_multiplier - 6.0f) < 0.001f);
    assert(fabsf(env.signature * env.prop_signature_multiplier - 372.0f) < 0.001f);
    assert(env.weapon_count == 8);
    assert(fabsf(env.weapon_cap_cost_each - 2.64f) < 0.001f);
    assert(fabsf(env.prop_cycle - 10.0f) < 0.001f);
    assert(fabsf(env.prop_cap_cost - 40.5f) < 0.001f);
    assert(fabsf(env.base_ship_resist[LAYER_SHIELD][0] - 0.00f) < 0.0001f);
    assert(fabsf(env.base_ship_resist[LAYER_ARMOR][0] - 0.50f) < 0.0001f);
    assert(fabsf(env.base_ship_resist[LAYER_HULL][0] - 0.33f) < 0.0001f);
    assert(env.reward_cap_spent == 0);
    assert(env.reward_low_cap == 0);
    assert(fabsf(env.cap_reserve_fraction - 0.30f) < 0.0001f);
    assert(env.reward_gate_ready_idle < 0);
    assert(env.reward_final_gate_ready_idle < env.reward_gate_ready_idle);
    assert(env.reward_weapon_idle_ready < 0);
    assert(fabsf(env.rep_urgency_fraction - 0.60f) < 0.0001f);
    assert(env.reward_rep_idle_urgent < 0);
    assert(env.reward_failure == -1);
    assert(env.reward_success + env.reward_room_clear +
        env.reward_completion_speed <= 1.0f);

    env.configured_scenario_episode = 7;
    puf_reset(&env);
    assert(env.scenario_episode == 7);
    env.configured_scenario_episode = -1;

    env.hard_scenario_probability = 1;
    for(int i=0;i<100;i++) {
        puf_reset(&env);
        assert(env.scenario_episode==3||env.scenario_episode==9||
            env.scenario_episode==11);
    }
    env.hard_scenario_probability = 0;

    // Randomized giant-rock placement may affect combat movement, but must not
    // fabricate an impossible room by sealing the required cache/gate routes.
    for(int sample=0;sample<500;sample++){
        puf_reset(&env);
        int sample_cache=ab_find_kind(&env,ENTITY_CACHE);
        int sample_gate=ab_find_kind(&env,ENTITY_CONDUIT);
        assert(sample_cache>=0&&sample_gate>=0);
        Vec3 origin={0,0,0};
        assert(ab_obstacles_blocking_segment(
            &env,origin,env.entities[sample_cache].pos,60.0f)==0);
        assert(ab_obstacles_blocking_segment(
            &env,origin,env.entities[sample_gate].pos,60.0f)==0);
        assert(ab_obstacles_blocking_segment(
            &env,env.entities[sample_cache].pos,
            env.entities[sample_gate].pos,60.0f)==0);
    }

    // Capacitor is its own physical opportunity cost. Spending or holding a low
    // reserve has no additional shaped reward.
    Env cap_economy = {0};
    float cap_rewards[1] = {0};
    float cap_metric = 0;
    cap_economy.capacitor = 100;
    cap_economy.cap_capacity = 100;
    cap_economy.reward_cap_spent = 0;
    cap_economy.reward_low_cap = 0;
    cap_economy.cap_reserve_fraction = 0.30f;
    ab_spend_capacitor(&cap_economy, 20, cap_rewards, &cap_metric);
    assert(cap_economy.capacitor == 80);
    assert(cap_metric == 20);
    assert(cap_rewards[0] == 0);
    ab_apply_cap_reserve_reward(&cap_economy, cap_rewards);
    assert(cap_rewards[0] == 0);
    cap_economy.capacitor = 0;
    ab_apply_cap_reserve_reward(&cap_economy, cap_rewards);
    assert(cap_rewards[0] == 0);

    // Exact nonlinear recharge: peak is near 25%, and Electrical's halved
    // recharge time produces more capacitor over the same one-second interval.
    float quarter_cap = 0.25f * env.cap_capacity;
    float normal_recharge = ab_capacitor_after_recharge(
        quarter_cap, env.cap_capacity, env.cap_recharge_time, 1.0f);
    float electrical_recharge = ab_capacitor_after_recharge(
        quarter_cap, env.cap_capacity, 0.5f * env.cap_recharge_time, 1.0f);
    assert(normal_recharge > quarter_cap);
    assert(normal_recharge - quarter_cap > 7.0f);
    assert(normal_recharge - quarter_cap < 8.5f);
    assert(electrical_recharge > normal_recharge);
    float shield_quarter = 0.25f * env.ship_shield_hp;
    float shield_recharge = ab_capacitor_after_recharge(
        shield_quarter, env.ship_shield_hp, env.shield_recharge_time, 1.0f);
    assert(shield_recharge > shield_quarter);
    assert(shield_recharge - shield_quarter > 3.3f);
    assert(shield_recharge - shield_quarter < 3.6f);

    // Armor repair pays at activation but lands at cycle end, including after
    // auto-repeat is disabled. Shield boost pays and lands at cycle start.
    Env repair = {0};
    float repair_rewards[1] = {0};
    repair.ship_shield_hp = 735;
    repair.ship_armor_hp = 1080;
    repair.ship_hull_hp = 920;
    repair.shield = 500;
    repair.armor = 500;
    repair.hull = 500;
    repair.capacitor = 100;
    repair.cap_capacity = 100;
    repair.rep_amount = 79;
    repair.rep_cycle = 4.8f;
    repair.rep_cap_cost = 40;
    repair.rep_layer = LAYER_ARMOR;
    repair.rep_effect_timing = EFFECT_CYCLE_END;
    repair.rep_on = 1;
    ab_step_repair_module(&repair, repair_rewards, 1.0f);
    assert(repair.armor == 500.0f);
    assert(repair.capacitor == 60.0f);
    assert(repair.rep_cycle_active == 1);
    repair.rep_on = 0;
    for(int i=0;i<4;i++)ab_step_repair_module(&repair, repair_rewards, 1.0f);
    assert(repair.armor == 500.0f);
    ab_step_repair_module(&repair, repair_rewards, 1.0f);
    assert(repair.armor == 579.0f);
    assert(repair.rep_cycle_active == 0);

    repair.rep_layer = LAYER_SHIELD;
    repair.rep_effect_timing = EFFECT_CYCLE_START;
    repair.rep_on = 1;
    repair.capacitor = 100;
    repair.rep_cycle_active = 0;
    ab_step_repair_module(&repair, repair_rewards, 1.0f);
    assert(repair.shield == 579.0f);
    assert(repair.capacitor == 60.0f);

    // Live reports an armor repairer as active until its paid cycle finishes,
    // even when auto-repeat has already been disabled.
    env.rep_on = 0;
    env.rep_cycle_active = 1;
    compute_observations(&env);
    assert(observations[11] == 1.0f);
    assert(observations[26] == 0.0f);
    env.rep_cycle_active = 0;
    env.rep_on = 1;
    env.prop_desired_on = 1;
    compute_observations(&env);
    assert(observations[11] == 0.0f);
    assert(observations[26] == 1.0f);
    assert(observations[27] == 1.0f);
    env.rep_on = 0;
    env.prop_desired_on = 0;

    assert(fabsf(env.ship_mass_kg - 1650000.0f) < 1.0f);
    assert(fabsf(env.prop_mass_addition_kg - 500000.0f) < 1.0f);
    assert(fabsf(env.inertia_modifier - 2.2132f) < 0.0001f);
    float align_time_off = ab_ship_time_constant(&env, 1.0f) * logf(4.0f);
    assert(fabsf(align_time_off - 5.0627f) < 0.001f);
    env.prop_on = 1;
    float align_time_on = ab_ship_time_constant(&env, 1.0f) * logf(4.0f);
    assert(fabsf(align_time_on - 6.5964f) < 0.001f);
    env.prop_on = 0;

    env.ship_pos = (Vec3){0, 0, 0};
    env.ship_vel = (Vec3){100, 0, 0};
    env.distance_observation_lag_ticks = 2;
    Vec3 delayed = ab_observed_relative(&env, (Vec3){1000, 0, 0}, (Vec3){0, 0, 0});
    assert(fabsf(delayed.x - 1200.0f) < 0.001f);
    env.distance_observation_lag_ticks = 0;
    env.ship_vel = (Vec3){0, 0, 0};

    // EVE collision response deflects a ship around solid geometry. A persistent
    // approach transaction must not pin the ship forever against a spherical
    // collider when its destination is directly behind that collider.
    Env slide = {0};
    obs_t slide_observations[OBS_SIZE] = {0};
    float slide_actions[NUM_ATNS] = {0};
    float slide_rewards[1] = {0};
    float slide_terminals[1] = {0};
    unsigned char slide_mask[ABYSS_ACTION_MASK_SIZE] = {0};
    slide.agents[0].observations = slide_observations;
    slide.agents[0].actions = slide_actions;
    slide.agents[0].rewards = slide_rewards;
    slide.agents[0].terminals = slide_terminals;
    slide.agents[0].action_mask = slide_mask;
    slide.rng = 42;
    puf_init(&slide, cfg);
    puf_reset(&slide);
    int slide_gate = ab_find_kind(&slide, ENTITY_CONDUIT);
    assert(slide_gate >= 0);
    for(int i=0;i<slide.entity_count;i++)
        if(slide.entities[i].kind==ENTITY_HOSTILE)slide.entities[i].alive=0;
    slide.ship_pos=(Vec3){0,0,0};
    slide.ship_vel=(Vec3){0,0,0};
    slide.entities[slide_gate].pos=(Vec3){20000,0,0};
    slide.entities[slide_gate].radius=1000;
    slide.obstacle_count=5;
    for(int i=0;i<slide.obstacle_count;i++)
        slide.obstacles[i]=(AbyssObstacle){
            .center={10000,(i-2)*3000.0f,0},.radius=4000};
    slide.cache_looted=1;
    slide.caches_looted=1;
    slide.interaction_kind=INTERACTION_ACTIVATE;
    slide.interaction_target_index=slide_gate;
    slide.navigation_target_index=slide_gate;
    compute_observations(&slide);
    for(int i=0;i<400&&slide.rooms_cleared==0;i++)puf_step(&slide);
    assert(slide.rooms_cleared==1);

    env.caches_looted = 2;
    env.navigation_target_index = 0;
    compute_observations(&env);
    assert(fabsf(observations[25] - 2.0f / 3.0f) < 0.0001f);
    env.caches_looted = 0;
    env.navigation_target_index = -1;
    compute_observations(&env);

    int cache = -1;
    int gate = -1;
    int hostile = -1;
    for (int i = 0; i < env.entity_count; i++) {
        if (env.entities[i].kind == ENTITY_CACHE) cache = i;
        if (env.entities[i].kind == ENTITY_CONDUIT) gate = i;
        if (env.entities[i].kind == ENTITY_HOSTILE && hostile < 0) hostile = i;
    }
    assert(cache >= 0 && gate >= 0 && hostile >= 0);
    int cache_slot = ab_slot_for_entity(&env, cache);
    int gate_slot = ab_slot_for_entity(&env, gate);
    int hostile_slot = ab_slot_for_entity(&env, hostile);
    assert(cache_slot >= 0 && gate_slot >= 0 && cache_slot != gate_slot);

    env.navigation_target_index = cache;
    env.interaction_kind = INTERACTION_OPEN;
    env.interaction_target_index = cache;
    compute_observations(&env);
    assert(fabsf(observations[28] -
        (cache_slot + 1.0f) / (ABYSS_MAX_ENTITIES + 1.0f)) < 0.0001f);
    assert(observations[29] == 0.5f);
    assert(fabsf(observations[30] -
        (cache_slot + 1.0f) / (ABYSS_MAX_ENTITIES + 1.0f)) < 0.0001f);
    assert(observations[31] == 0.0f);
    env.navigation_target_index = -1;
    env.interaction_kind = INTERACTION_NONE;
    env.interaction_target_index = -1;

    // Fire(slot) is a persistent transaction: focus first, start on a later tick.
    env.entities[cache].locked = 1;
    env.entities[gate].locked = 1;
    env.entities[hostile].locked = 1;
    compute_observations(&env);
    int weapon_offset = ABYSS_NAV_ACTIONS + ABYSS_TARGET_ACTIONS;
    int prop_offset = weapon_offset + ABYSS_WEAPON_ACTIONS;
    int rep_offset = prop_offset + ABYSS_DESIRED_ACTIONS;
    assert(action_mask[weapon_offset + WEAPON_OFF] == 1);
    assert(action_mask[weapon_offset + WEAPON_FIRE_BASE + cache_slot] == 1);
    assert(action_mask[weapon_offset + WEAPON_FIRE_BASE + hostile_slot] == 1);
    assert(action_mask[weapon_offset + WEAPON_FIRE_BASE + gate_slot] == 0);
    assert(action_mask[prop_offset + DESIRED_OFF] == 1);
    assert(action_mask[prop_offset + DESIRED_ON] == 1);
    assert(action_mask[rep_offset + DESIRED_OFF] == 1);
    assert(action_mask[rep_offset + DESIRED_ON] == 1);
    env.weapon_cooldown = 100;
    actions[2] = WEAPON_FIRE_BASE + cache_slot;
    puf_step(&env);
    assert(env.focus_index == cache);
    assert(env.weapon_on == 0);
    assert(env.weapon_desired_target_index == cache);
    compute_observations(&env);
    assert(action_mask[weapon_offset + WEAPON_FIRE_BASE + cache_slot] == 1);
    memset(actions, 0, sizeof(actions));
    actions[2] = WEAPON_FIRE_BASE + cache_slot;
    puf_step(&env);
    assert(env.weapon_target_index == cache);

    // A later focus click does not silently redirect an active weapon.
    memset(actions, 0, sizeof(actions));
    actions[1] = TARGET_FOCUS_BASE + hostile_slot;
    actions[2] = WEAPON_FIRE_BASE + cache_slot;
    puf_step(&env);
    assert(env.focus_index == hostile);
    assert(env.weapon_target_index == cache);

    // Retargeting stops the old cycle, then starts on the already-focused hostile
    // on the following tick. A conduit is never a legal fire target.
    memset(actions, 0, sizeof(actions));
    actions[2] = WEAPON_FIRE_BASE + hostile_slot;
    puf_step(&env);
    assert(env.weapon_on == 0);
    assert(env.weapon_desired_target_index == hostile);
    memset(actions, 0, sizeof(actions));
    actions[2] = WEAPON_FIRE_BASE + hostile_slot;
    puf_step(&env);
    assert(env.weapon_target_index == hostile);
    memset(actions, 0, sizeof(actions));
    actions[1] = TARGET_FOCUS_BASE + gate_slot;
    actions[2] = WEAPON_FIRE_BASE + hostile_slot;
    puf_step(&env);
    assert(env.focus_index == hostile);
    memset(actions, 0, sizeof(actions));
    actions[2] = WEAPON_FIRE_BASE + gate_slot;
    puf_step(&env);
    assert(env.weapon_desired_target_index == hostile);
    env.weapon_on = 0;
    env.weapon_target_index = env.weapon_desired_target_index = -1;

    for (int i = 0; i < env.entity_count; i++)
        if (env.entities[i].kind == ENTITY_HOSTILE) env.entities[i].alive = 0;
    compute_observations(&env);
    assert(observations[ABYSS_GLOBAL_FEATURES +
        hostile_slot * ABYSS_ENTITY_FEATURES + 5] == 0.0f);
    memset(actions, 0, sizeof(actions));
    puf_step(&env);
    assert(fabsf(rewards[0] -
        (env.reward_step + env.reward_weapon_idle_ready)) < 0.0001f);

    // A cap-affordable repair must not be deferred at low HP while threats remain.
    // Keep every damageable entity unlocked so weapon-idle shaping is not involved.
    env.entities[hostile].alive = 1;
    for (int i = 0; i < env.entity_count; i++) {
        env.entities[i].locked = 0;
        env.entities[i].locking = 0;
    }
    env.armor = 0.5f * ab_layer_max_hp(&env, LAYER_ARMOR);
    env.capacitor = env.cap_capacity;
    env.rep_on = 0;
    env.rep_cycle_active = 0;
    memset(actions, 0, sizeof(actions));
    puf_step(&env);
    float urgency=(env.rep_urgency_fraction-0.5f)/env.rep_urgency_fraction;
    assert(fabsf(rewards[0] -
        (env.reward_step + env.reward_rep_idle_urgent*urgency*urgency)) < 0.0001f);
    env.entities[hostile].alive = 0;

    env.entities[cache].alive = 0;
    env.ship_pos = ab_add(env.entities[cache].pos, (Vec3){10000, 0, 0});
    compute_observations(&env);
    int interaction_offset = ABYSS_NAV_ACTIONS + ABYSS_TARGET_ACTIONS +
        ABYSS_WEAPON_ACTIONS + 2*ABYSS_DESIRED_ACTIONS;
    assert(action_mask[interaction_offset + INTERACT_OPEN_BASE + cache_slot] == 1);
    memset(actions, 0, sizeof(actions));
    actions[5] = INTERACT_OPEN_BASE + cache_slot;
    puf_step(&env);
    assert(env.cargo_open == 0);
    assert(env.interaction_kind == INTERACTION_OPEN);
    assert(env.navigation_target_index == cache);
    assert(action_mask[NAV_HOLD] == 1);
    assert(action_mask[NAV_STOP] == 0);
    assert(action_mask[interaction_offset + INTERACT_OPEN_BASE + cache_slot] == 0);
    for (int i = 0; i < 100 && !env.cargo_open; i++) {
        memset(actions, 0, sizeof(actions));
        puf_step(&env);
    }
    assert(env.cargo_open == 1);
    assert(env.interaction_kind == INTERACTION_NONE);
    assert(env.navigation_target_index == cache);

    // Cargo-open is a persistent UI state, not proof that Loot All can still
    // succeed. Reproduce the live overshoot: the pane opens near the wreck, but
    // one second of outward momentum carries the ship beyond 2,500 m before Enter
    // lands. The failed key keeps cargo open and forces navigation back to the
    // wreck; conduit navigation stays unavailable until the looted postcondition.
    env.ship_pos=ab_add(env.entities[cache].pos,
        (Vec3){env.entities[cache].radius+1000,0,0});
    env.ship_vel=(Vec3){4000,0,0};
    env.navigation_target_index=-1;
    compute_observations(&env);
    assert(action_mask[NAV_HOLD] == 0);
    assert(action_mask[NAV_APPROACH_BASE + cache_slot] == 1);
    assert(action_mask[NAV_APPROACH_BASE + gate_slot] == 0);
    assert(action_mask[interaction_offset + INTERACT_LOOT] == 1);
    memset(actions, 0, sizeof(actions));
    actions[0] = NAV_APPROACH_BASE + cache_slot;
    actions[5] = INTERACT_LOOT;
    puf_step(&env);
    assert(env.cache_looted == 0);
    assert(env.cargo_open == 1);
    assert(env.navigation_target_index == cache);
    assert(action_mask[NAV_HOLD] == 1);
    assert(action_mask[NAV_APPROACH_BASE + gate_slot] == 0);
    assert(action_mask[interaction_offset + INTERACT_LOOT] == 0);
    for(int i=0;i<200&&!env.cache_looted;i++){
        memset(actions,0,sizeof(actions));
        if(action_mask[interaction_offset + INTERACT_LOOT])
            actions[5]=INTERACT_LOOT;
        puf_step(&env);
    }
    assert(env.cache_looted == 1);
    assert(env.caches_looted == 1);
    assert(env.cargo_open == 0);
    assert(env.navigation_target_index == cache);

    env.room = 3;
    env.rooms_cleared = 2;
    env.ship_pos = ab_add(env.entities[gate].pos, (Vec3){30000, 0, 0});
    compute_observations(&env);
    assert(action_mask[interaction_offset + INTERACT_ACTIVATE_BASE + gate_slot] == 0);
    env.caches_looted = 3;
    compute_observations(&env);
    assert(action_mask[interaction_offset + INTERACT_ACTIVATE_BASE + gate_slot] == 1);
    memset(actions, 0, sizeof(actions));
    puf_step(&env);
    assert(env.gate_ready_ticks == 1);
    assert(env.gate_ready_idle_ticks == 1);
    assert(fabsf(rewards[0] -
        (env.reward_step + env.reward_final_gate_ready_idle)) < 0.0001f);
    memset(actions, 0, sizeof(actions));
    actions[5] = INTERACT_ACTIVATE_BASE + gate_slot;
    puf_step(&env);
    assert(terminals[0] == 0.0f);
    assert(env.interaction_kind == INTERACTION_ACTIVATE);
    assert(env.navigation_target_index == gate);
    assert(action_mask[NAV_STOP] == 0);
    assert(action_mask[interaction_offset + INTERACT_ACTIVATE_BASE + gate_slot] == 0);
    for (int i = 0; i < 200 && terminals[0] == 0.0f; i++) {
        memset(actions, 0, sizeof(actions));
        puf_step(&env);
    }
    assert(terminals[0] == 1.0f);
    assert(env.log.n == 1.0f);
    assert(env.log.completion_rate == 1.0f);
    assert(env.log.rooms_cleared == 3.0f);
    assert(env.log.caches_looted == 3.0f);
    assert(env.room == 1);
    assert(env.tick == 0);
    assert_finite_observation(observations);
    memset(actions, 0, sizeof(actions));
    actions[3] = DESIRED_ON;
    puf_step(&env);
    assert(terminals[0] == 0.0f);
    assert(env.tick == 1);
    assert(env.prop_desired_on == 1);
    memset(actions, 0, sizeof(actions));
    puf_step(&env);
    assert(env.prop_desired_on == 0);
    assert(env.rep_on == 0);

    // Completion requires all three caches, and completion dominates speed.
    Env incomplete = {0};
    float incomplete_rewards[1] = {0};
    float incomplete_terminals[1] = {0};
    incomplete.agents[0].rewards = incomplete_rewards;
    incomplete.agents[0].terminals = incomplete_terminals;
    incomplete.max_steps = 1200;
    incomplete.tick = 1;
    incomplete.rooms_cleared = 3;
    incomplete.caches_looted = 2;
    incomplete.hull = 1;
    ab_finish(&incomplete, 1);
    assert(incomplete.log.completion_rate == 0.0f);
    assert(incomplete.log.survived_incomplete_rate == 1.0f);
    assert(incomplete.log.death_rate == 0.0f);

    Env slow_complete = {0};
    float slow_rewards[1] = {0};
    float slow_terminals[1] = {0};
    slow_complete.agents[0].rewards = slow_rewards;
    slow_complete.agents[0].terminals = slow_terminals;
    slow_complete.max_steps = 1200;
    slow_complete.tick = 1199;
    slow_complete.rooms_cleared = 3;
    slow_complete.caches_looted = 3;
    slow_complete.hull = 1;
    ab_finish(&slow_complete, 1);
    assert(slow_complete.log.completion_rate == 1.0f);
    assert(slow_complete.log.perf > incomplete.log.perf);

    Env timeout = {0};
    float timeout_rewards[1] = {0};
    float timeout_terminals[1] = {0};
    timeout.agents[0].rewards = timeout_rewards;
    timeout.agents[0].terminals = timeout_terminals;
    timeout.max_steps = timeout.tick = 1200;
    timeout.room = 1;
    timeout.hull = 1;
    ab_finish(&timeout, 0);
    assert(timeout.log.timeout_rate == 1.0f);
    assert(timeout.log.timeout_cache_rate == 1.0f);
    assert(timeout.log.survived_incomplete_rate == 1.0f);

    Env boundary_death = {0};
    float boundary_rewards[1] = {0};
    float boundary_terminals[1] = {0};
    boundary_death.agents[0].rewards = boundary_rewards;
    boundary_death.agents[0].terminals = boundary_terminals;
    boundary_death.max_steps = 1200;
    boundary_death.tick = 50;
    boundary_death.hull = 0;
    boundary_death.boundary_kill = 1;
    ab_finish(&boundary_death, 0);
    assert(boundary_death.log.death_rate == 1.0f);
    assert(boundary_death.log.boundary_death_rate == 1.0f);
    assert(boundary_death.log.combat_death_rate == 0.0f);

    Env diagnostics = {0};
    float diagnostic_rewards[1] = {0};
    float diagnostic_terminals[1] = {0};
    diagnostics.agents[0].rewards = diagnostic_rewards;
    diagnostics.agents[0].terminals = diagnostic_terminals;
    diagnostics.max_steps = 1200;
    diagnostics.tick = 100;
    diagnostics.hull = 1;
    diagnostics.min_cap_fraction = 0.1f;
    diagnostics.cap_dry_ticks = 10;
    diagnostics.prop_ticks = 20;
    diagnostics.rep_ticks = 30;
    diagnostics.weapon_ticks = 40;
    diagnostics.threat_ticks = 80;
    diagnostics.weapon_idle_threat_ticks = 50;
    diagnostics.rep_starved_ticks = 1;
    diagnostics.wasted_rep_amount = 79;
    diagnostics.total_rep_amount = 158;
    diagnostics.prop_cap_spent = 200;
    diagnostics.rep_cap_spent = 400;
    diagnostics.neut_cap_drained = 50;
    ab_finish(&diagnostics, 0);
    assert(fabsf(diagnostics.log.min_cap_fraction - 0.1f) < 0.0001f);
    assert(fabsf(diagnostics.log.cap_dry_fraction - 0.1f) < 0.0001f);
    assert(fabsf(diagnostics.log.prop_uptime - 0.2f) < 0.0001f);
    assert(fabsf(diagnostics.log.rep_uptime - 0.3f) < 0.0001f);
    assert(fabsf(diagnostics.log.weapon_uptime - 0.4f) < 0.0001f);
    assert(fabsf(diagnostics.log.weapon_idle_threat_fraction - 0.625f) < 0.0001f);
    assert(diagnostics.log.rep_starved_rate == 1.0f);
    assert(fabsf(diagnostics.log.wasted_rep_fraction - 0.5f) < 0.0001f);
    assert(diagnostics.log.prop_cap_spent == 200.0f);
    assert(diagnostics.log.rep_cap_spent == 400.0f);
    assert(diagnostics.log.neut_cap_drained == 50.0f);

    puf_close(&env);
    puf_ini_free(&ini);
    puts("native Abyss smoke ok");
    return 0;
}
