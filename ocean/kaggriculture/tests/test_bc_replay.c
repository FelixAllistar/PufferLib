#include "../kag_bc_replay.c"

int main(void) {
    const char* profiles[] = {"ocean/kaggriculture/profiles/terminal.ini",
        "ocean/kaggriculture/profiles/shaped.ini"};
    uint64_t previous_hash = 0;
    for (int profile = 0; profile < 2; profile++) {
        KGConfig cfg;
        kg_config_default(&cfg);
        cfg.seed = 123;
        KagBCReplay* r = kag_bc_create(&cfg, profiles[profile]);
        assert(r && kag_bc_executor(r) == 2 && kag_bc_gamma(r) > 0.99);
        assert(r->semantics_hash && r->semantics_hash != previous_hash);
        previous_hash = r->semantics_hash;
        KGState reference;
        kg_init(&reference, &cfg);
        float history[NUM_ATNS] = {0}, rewards[2], obs[OBS_SIZE];
        unsigned char masks[KAG_ALL_LOGITS];
        int positions[(KG_MAX_HANDS + 1) * 2];
        int facts[(KG_MAX_HANDS + 1) * 4];
        float total[2] = {0};
        for (int step = 0; step < cfg.episode_steps - 1; step++) {
            KGAction pair[2] = {0}, preview;
            for (int p = 0; p < 2; p++) kg_rule_action(&reference, p, pair + p);
            assert(kag_bc_positions(r, 0, positions, (KG_MAX_HANDS + 1) * 2) > 0);
            assert(kag_bc_work_facts(r, 0, pair, facts, (KG_MAX_HANDS + 1) * 4) > 0);
            assert(kag_bc_view(r, 0, obs, masks));
            assert(kag_bc_teacher_mask(r, 0, history, masks));
            assert(kag_bc_decode(r, 0, history, &preview));
            assert(!memcmp(&r->env.game, &reference, sizeof(reference)));
            assert(kag_bc_step(r, pair, 0, history, rewards));
            kg_step(&reference, pair);
            assert(!memcmp(kag_bc_state(r), &reference, sizeof(reference)));
            for (int p = 0; p < 2; p++) {
                assert(isfinite(rewards[p]));
                total[p] += rewards[p];
                if (profile == 0 && !reference.done) assert(rewards[p] == 0);
            }
            for (int i = 0; i < OBS_SIZE; i++) assert(isfinite(obs[i]));
            for (int i = 0; i < KAG_ALL_LOGITS; i++) assert(masks[i] <= 1);
        }
        assert(kg_done(kag_bc_state(r)));
        if (profile == 0) {
            for (int p = 0; p < 2; p++) {
                float expected = r->env.reward_money *
                    (reference.players[p].money - cfg.starting_money) / cfg.starting_money;
                assert(total[p] == expected);
            }
        }
        kag_bc_destroy(r);
    }
    puts("BC replay bridge: 1438 primitive transitions, terminal/shaped rewards PASS");
}
