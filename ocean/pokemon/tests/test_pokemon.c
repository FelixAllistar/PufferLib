#define _POSIX_C_SOURCE 200809L
#include "../pokemon.h"
#include <assert.h>
#include <stdio.h>
// First sourced variant of each fixture species. Not a training restriction.
static const int fixtures[] = {445,392,511,365,421,231,389,526,470,328,336,429};
static void test_catalog_coverage(void) {
    int counted = 0;
    for (int species = 1; species <= 149; species++) {
        assert(pk_species_count(species) > 0 && pk_species_count(species) <= PK_ACTIONS);
        for (int variant = 0; variant < pk_species_count(species); variant++) {
            int set = pk_species_set(species, variant);
            assert(pk_species(set) == species);
            counted++;
            for (int m = 0; m < 4; m++) {
                int move = pk_set_move(set, m);
                assert(move >= 0 && move <= 164);
                assert(move != 12 && move != 32 && move != 90); // OHKO
                assert(move != 104 && move != 107); // Double Team, Minimize
                assert(move != 19 && move != 91); // Fly, Dig
                for (int n = 0; move && n < m; n++) assert(move != pk_set_move(set,n));
            }
            uint16_t teams[12];
            teams[0] = (uint16_t)set;
            int slot = 1;
            for (int other = 1; slot < 6; other++) if (other != species)
                teams[slot++] = (uint16_t)pk_species_set(other,0);
            memcpy(teams+6,teams,6*sizeof(uint16_t));
            PKBattle battle;
            assert(pk_start(&battle,42,teams) == 0);
            uint8_t obs[PK_OBS];
            pk_observe(&battle,0,obs);
            assert(obs[16] == species);
            for (int m=0;m<4;m++) assert(obs[24+m] == pk_set_move(set,m));
            teams[1] = teams[0];
            assert(pk_start(&battle,42,teams) == 4); // Boundary Species Clause
        }
    }
    assert(counted == PK_SETS);
    assert(pk_species_count(150) == 0 && pk_species_count(151) == 0);
    PKGame g = {0}; g.draft=1; g.rng=1; g.max_updates=512;
    pk_game_reset(&g);
    for(int i=0;i<PK_ACTIONS;i++) assert(g.masks[0][i] == (i<149));
}

static void bind_env(Env* e, uint8_t obs[2][PK_OBS], uint8_t masks[2][PK_ACTIONS],
        float actions[2], float rewards[2], float terminals[2]) {
    for (int p = 0; p < 2; p++) {
        e->agents[p].observations = obs[p]; e->agents[p].action_mask = masks[p];
        e->agents[p].actions = actions + p; e->agents[p].rewards = rewards + p;
        e->agents[p].terminals = terminals + p;
    }
}
static void test_draft(void) {
    PKGame a = {0}, b = {0};
    a.rng = b.rng = 42; a.draft = b.draft = 1;
    a.max_updates = b.max_updates = 512;
    pk_game_reset(&a); pk_game_reset(&b);
    assert(a.obs[0][0] == 0);
    for (int pick = 0; pick < 6; pick++) {
        int p0 = fixtures[pick];
        int p1 = fixtures[6 + pick];
        assert(pk_game_step(&a, pk_species(p0)-1, pk_species(p1)-1) == 0);
        assert(pk_game_step(&b, pk_species(p0)-1, pk_species(fixtures[6 + (pick + 1) % 6])-1) == 0);
        assert(memcmp(a.obs[0], b.obs[0], PK_OBS) == 0);
        assert(a.obs[0][12] == 1 && a.obs[0][13] == pk_species(p0));
        assert(pk_game_step(&a, 0, 0) == 0);
        assert(pk_game_step(&b, 0, 0) == 0);
        if (pick < 5) {
            assert(memcmp(a.obs[0], b.obs[0], PK_OBS) == 0);
            assert(memcmp(a.masks[0], b.masks[0], PK_ACTIONS) == 0);
        }
        assert(a.teams[0][pick] == p0);
        if (pick < 5) assert(a.masks[0][pk_species(p0)-1] == 0);
        unsigned encoded = a.obs[0][464+2*pick] + 256u*a.obs[0][465+2*pick];
        assert(encoded == (unsigned)p0+1);
    }
    assert(a.obs[0][0] == 1);
    assert(a.obs[0][208] == pk_species(fixtures[6])); // Only opposing lead is revealed.
    for (int i = 240; i < 400; i++) assert(a.obs[0][i] == 0);
    for (int i = 216; i < 220; i++) assert(a.obs[0][i] == 0); // No foe moves yet.
}
static void test_stable_switch_and_move_reveal(void) {
    PKBattle battle;
    const uint16_t teams[12] = {445,392,511,365,421,231, 445,392,511,365,421,231};
    assert(pk_start(&battle, 1234, teams) == 0);
    uint8_t mask[PK_ACTIONS], obs[PK_OBS];
    pk_mask(&battle, 0, mask);
    assert(!mask[4] && mask[5]); // Original lead active, slot 2 can switch.
    assert(pk_update(&battle, 5, 9) == 0); // Chansey vs Alakazam.
    pk_mask(&battle, 0, mask);
    assert(mask[4] && !mask[5]);
    pk_observe(&battle, 0, obs);
    assert(obs[4] == 2 && obs[5] == 2); // Foe is second REVEALED, not party slot 6.
    assert(obs[240] == pk_species(fixtures[5]));
    assert(pk_update(&battle, 2, 0) == 0); // Ice Beam vs Psychic.
    pk_observe(&battle, 0, obs);
    assert(obs[248] != 0); // Psychic revealed on the correct opposing mon.
    assert(obs[216] == 0); // Tauros still has no revealed moves.
    PKBattle before = battle;
    assert(pk_update(&battle, 31, 31) == -1);
    assert(memcmp(&before, &battle, sizeof(battle)) == 0);

    // Swapping two unrevealed opponent party positions must not change our view.
    PKBattle reordered;
    const uint16_t alternate[12] = {445,392,511,365,421,231, 445,231,511,365,421,392};
    assert(pk_start(&battle, 4321, teams) == 0);
    assert(pk_start(&reordered, 4321, alternate) == 0);
    assert(pk_update(&battle, 5, 9) == 0);
    assert(pk_update(&reordered, 5, 5) == 0);
    uint8_t other[PK_OBS];
    pk_observe(&battle, 0, obs);
    pk_observe(&reordered, 0, other);
    assert(memcmp(obs, other, PK_OBS) == 0);
}
static void test_terminal_reset(void) {
    Env env = {0};
    env.num_agents = 2; env.tag = 1; env.game.rng = 9;
    env.game.max_updates = 1;
    uint8_t obs[2][PK_OBS], masks[2][PK_ACTIONS];
    float actions[2] = {NAN, INFINITY}, rewards[2], terminals[2];
    bind_env(&env, obs, masks, actions, rewards, terminals);
    puf_reset(&env);
    puf_step(&env);
    assert(terminals[0] == 1 && terminals[1] == 1);
    assert(rewards[0] == 0 && rewards[1] == 0);
    assert(env.log.n == 1 && env.log.timeout_rate == 1);
    assert(env.log.invalid_actions == 2 && env.boundary_reached == 1);
    assert(env.game.updates == 0 && env.game.result == 0);
    assert(memcmp(obs[0] + 480, masks[0], PK_ACTIONS) == 0);
    env.game.max_updates = 512;
    actions[0] = actions[1] = 0;
    puf_step(&env);
    assert(terminals[0] == 0 && terminals[1] == 0);
}
static void test_seeded_rollouts(void) {
    uint64_t policy_rng = 77;
    int outcomes[6] = {0};
    for (int mode = 0; mode < 2; mode++) {
        for (int seed = 1; seed <= 100; seed++) {
            PKGame a = {0}, b = {0};
            a.rng = b.rng = (uint64_t)seed;
            a.draft = b.draft = mode;
            a.max_updates = b.max_updates = 512;
            pk_game_reset(&a); pk_game_reset(&b);
            for (int t = 0; !a.result && t < 530; t++) {
                int a0 = pk_random_action(a.masks[0], &policy_rng);
                int a1 = pk_random_action(a.masks[1], &policy_rng);
                assert(pk_game_step(&a, a0, a1) == pk_game_step(&b, a0, a1));
                assert(memcmp(a.obs, b.obs, sizeof(a.obs)) == 0);
                assert(memcmp(a.masks, b.masks, sizeof(a.masks)) == 0);
                assert(a.invalid_actions == 0 && a.result != 4);
            }
            assert(a.result > 0 && a.result != 4);
            outcomes[a.result]++;
        }
    }
    assert(outcomes[1] + outcomes[2] > 0);
    printf("200 deterministic rollouts: P1=%d P2=%d ties=%d timeouts=%d\n",
        outcomes[1], outcomes[2], outcomes[3], outcomes[5]);
}
static void test_adapter_full_episodes(void) {
    Dict kwargs = {0};
    dict_set(&kwargs, "generation", 1);
    dict_set(&kwargs, "format", 0);
    dict_set(&kwargs, "team_selection", 1);
    dict_set(&kwargs, "max_updates", 512);
    dict_set(&kwargs, "seed", 42);
    Env env = {0}, other = {0};
    other.rng = 1;
    puf_init(&env, &kwargs);
    puf_init(&other, &kwargs);
    assert(env.game.rng != other.game.rng);
    assert(env.num_agents == 2 && env.agents[1].policy == 1);
    uint8_t obs[2][PK_OBS], masks[2][PK_ACTIONS];
    float actions[2], rewards[2], terminals[2];
    bind_env(&env, obs, masks, actions, rewards, terminals);
    puf_reset(&env);
    uint64_t rng = 321;
    int finished = 0, decisive = 0;
    for (int t = 0; finished < 20 && t < 11000; t++) {
        for (int p = 0; p < 2; p++) actions[p] = (float)pk_random_action(masks[p], &rng);
        puf_step(&env);
        assert(terminals[0] == terminals[1]);
        assert(rewards[0] == -rewards[1]);
        if (terminals[0]) {
            finished++;
            decisive += rewards[0] != 0;
            assert(env.game.picks == 0 && obs[0][0] == 0 && obs[1][0] == 0);
        } else assert(rewards[0] == 0);
    }
    assert(finished == 20 && decisive > 0 && env.log.n == 20);
    assert(env.log.invalid_actions == 0);
    float teams = 0, leads = 0;
    for (int i = 0; i < PK_SETS; i++) {
        teams += env.log.team_picks[i];
        leads += env.log.lead_picks[i];
    }
    assert(env.log.team_samples == 20 && teams == 120 && leads == 20);
    // The simple numeric dictionary contains no allocated strings or arrays.
    free(kwargs.items);
}
static void test_potential_rewards_and_team_log(void) {
    Env env = {0};
    env.num_agents = 2;
    env.game.rng = 321;
    env.game.max_updates = 64;
    env.reward_win = 2;
    env.reward_hp_scale = 0.7f;
    env.reward_ko_scale = 0.3f;
    env.reward_gamma = 0.95f;
    env.agents[0].policy = 0;
    env.agents[1].policy = 1;
    char path[] = "/tmp/pokemon-teams-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    snprintf(env.team_log_path, sizeof(env.team_log_path), "%s", path);
    env.team_log_interval = 1;
    uint8_t obs[2][PK_OBS], masks[2][PK_ACTIONS];
    float actions[2], rewards[2], terminals[2];
    bind_env(&env, obs, masks, actions, rewards, terminals);
    puf_reset(&env);
    uint64_t rng = 456;
    for (int episode = 0; episode < 10; episode++) {
        double total = 0, discount = 1;
        float old_score = env.log.slot_0_score;
        for (int t = 0; t < 70; t++) {
            for (int p = 0; p < 2; p++) actions[p] = pk_random_action(masks[p], &rng);
            puf_step(&env);
            assert(rewards[0] == -rewards[1]);
            total += discount * rewards[0];
            if (terminals[0]) {
                float outcome = 2 * (env.log.slot_0_score - old_score) - 1;
                assert(fabs(total - discount * env.reward_win * outcome) < 1e-5);
                break;
            }
            discount *= env.reward_gamma;
        }
        assert(terminals[0]);
    }
    FILE* file = fopen(path, "r");
    assert(file);
    char line[4096];
    int rows = 0;
    while (fgets(line, sizeof(line), file)) {
        assert(strstr(line, "\"leads\":[") && strstr(line, "\"teams\":["));
        assert(strstr(line, "\"banks\":[0,1]"));
        assert(strstr(line, "\"moves\":[") && strstr(line, "\"abi\":2"));
        rows++;
    }
    fclose(file);
    assert(rows == 10);
    unlink(path);
}
static void test_top_species_summary(void) {
    Log log = {0};
    log.team_samples=100;
    int tauros=pk_species_set(128,0), snorlax=pk_species_set(143,0), meowth=pk_species_set(52,0);
    assert(pk_species_count(143)>1);
    log.team_picks[tauros]=80;
    log.team_picks[snorlax]=40;
    log.team_picks[snorlax+1]=50;
    log.team_picks[meowth]=5;
    log.lead_picks[tauros]=10;
    log.lead_picks[meowth]=60;
    Dict metrics={0};
    puf_log(&log,&metrics);
    int ids[6]; double rates[6];
    assert(pk_top_species(&metrics,"",0,ids,rates)==3);
    assert(ids[0]==143 && fabs(rates[0]-.9)<1e-6);
    assert(ids[1]==128 && fabs(rates[1]-.8)<1e-6);
    assert(ids[2]==52 && fabs(rates[2]-.05)<1e-6);
    assert(pk_top_species(&metrics,"",1,ids,rates)==2);
    assert(ids[0]==52 && fabs(rates[0]-.6)<1e-6);
    char label[64]; pk_species_label(52,label); assert(!strcmp(label,"Meowth_LC"));
    pk_species_label(122,label); assert(!strcmp(label,"MrMime_NU"));
    pk_species_label(137,label); assert(!strcmp(label,"Porygon_PU-SS"));
    pk_species_label(121,label); assert(!strcmp(label,"Starmie_OU"));
    for(int i=1;i<=149;i++) assert(strlen(pk_species_labels[i])<=18);
    dict_clear(&metrics);
    assert(pk_top_species(&metrics,"",0,ids,rates)==0);
}
int main(void) {
    test_top_species_summary();
    test_catalog_coverage();
    test_draft();
    test_stable_switch_and_move_reveal();
    test_terminal_reset();
    test_seeded_rollouts();
    test_adapter_full_episodes();
    test_potential_rewards_and_team_log();
    puts("Pokemon adapter tests passed");
    return 0;
}
