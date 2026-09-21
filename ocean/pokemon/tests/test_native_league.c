#define _POSIX_C_SOURCE 200809L
#include "../pokemon.h"
#include "fixtures.h"

int main(void) {
    Ini ini={0};
    puf_ini_load_env(&ini,"pokemon",0,NULL);
    puf_ini_put(&ini,"env.force_core_combos","0");
    puf_ini_put(&ini,"env.reset_state_prob","0");
    puf_ini_put(&ini,"env.native_league",pk_test_native_manifest(2));
    puf_ini_put(&ini,"env.expert_fraction","1");
    // League-only training must work without enabling learner/history selfplay.
    puf_ini_put(&ini,"selfplay.enabled","0");
    pk_configure(&ini,"train");
    assert(pk_native_count > 0);
    assert(PUF_FROZEN_LEAGUE_ACTIVE());
    assert(puf_ini_get(&ini,"selfplay","enabled")==0);
    assert(puf_ini_get(&ini,"selfplay","opp_timeout_steps")==0);
    assert(puf_ini_get(&ini,"vec","frozen_bank_pct")==1);
    Env env={0};
    uint8_t obs[2][PK_OBS],mask[2][PK_ACTIONS];
    float rewards[2],terminals[2];
    env.rng=123;
    puf_init(&env,puf_ini_section(&ini,"env",0));
    for(int p=0;p<2;p++) {
        env.agents[p].observations=obs[p]; env.agents[p].action_mask=mask[p];
        env.agents[p].rewards=&rewards[p]; env.agents[p].terminals=&terminals[p];
    }
    for(int b=0;b<pk_native_count;b++) {
        env.tag=b+1;
        pk_native_bank_loaded(&env,1,b,pk_native_bank_path(b,"unused"));
        assert(!env.game.fixed_enabled[0]);
        PKGame expected={0}; pk_parse_fixed_team(&expected,1,pk_native_banks[b].team);
        assert(env.game.fixed_enabled[1]==expected.fixed_enabled[1]);
        assert(env.game.required_count[1]==expected.required_count[1]);
        assert(!memcmp(env.game.obs[1],obs[1],PK_OBS));
        while(env.game.phase!=PK_PHASE_BATTLE) {
            int a=pk_random_action(env.game.masks[0],&env.game.rng);
            int foe=pk_random_action(env.game.masks[1],&env.game.rng);
            pk_game_step(&env.game,a,foe);
        }
        if(expected.fixed_enabled[1]) for(int i=0;i<expected.required_count[1];i++) {
            int found=0;
            for(int j=0;j<6;j++) found+=env.game.teams[1][j].species==expected.fixed_team[1][i];
            assert(found==1);
        }
    }
    // A specialist learner keeps its own composition in either seat. The
    // frozen opponent keeps its distinct composition and prescribed lead.
    snprintf(pk_learner_team,sizeof(pk_learner_team),"species:132,129,10");
    snprintf(pk_learner_lead,sizeof(pk_learner_lead),"132");
    for(int seat=0;seat<2;seat++) {
        env.agents[seat].policy=0;env.agents[1-seat].policy=1;env.tag=1;
        pk_native_bank_loaded(&env,1,0,pk_native_banks[0].path);
        assert(env.game.fixed_lead[seat]==132 && env.game.fixed_lead[1-seat]==65);
        assert(env.game.masks[seat][131] && env.game.masks[1-seat][64]);
        while(env.game.phase!=PK_PHASE_BATTLE)pk_game_step(&env.game,
            pk_random_action(env.game.masks[0],&env.game.rng),pk_random_action(env.game.masks[1],&env.game.rng));
        assert(env.game.teams[seat][0].species==132 && env.game.teams[1-seat][0].species==65);
        for(int p=0;p<2;p++)for(int j=0;j<env.game.required_count[p];j++) {
            int found=0;for(int i=0;i<6;i++)found+=env.game.teams[p][i].species==env.game.fixed_team[p][j];assert(found==1);
        }
    }
    puf_ini_free(&ini);
    // The league has its own off switch, independent of selfplay.
    puf_ini_load_env(&ini,"pokemon",0,NULL);
    puf_ini_put(&ini,"env.force_core_combos","0");
    puf_ini_put(&ini,"env.reset_state_prob","0");
    puf_ini_put(&ini,"selfplay.enabled","0");
    puf_ini_put(&ini,"env.native_league","None");
    puf_ini_put(&ini,"vec.num_frozen_banks","2");
    puf_ini_put(&ini,"vec.frozen_bank_pct","0.5");
    puf_ini_put(&ini,"selfplay.opp_timeout_steps","123");
    puf_ini_put(&ini,"selfplay.opponent_pool","None");
    pk_configure(&ini,"train");
    assert(pk_native_count==0);
    assert(puf_ini_get(&ini,"selfplay","enabled")==0);
    assert(puf_ini_get(&ini,"vec","num_frozen_banks")==2);
    assert(puf_ini_get(&ini,"vec","frozen_bank_pct")==0.5);
    assert(puf_ini_get(&ini,"selfplay","opp_timeout_steps")==123);
    assert(!strcmp(puf_ini_get_str(&ini,"selfplay","opponent_pool"),"None"));
    assert(!strcmp(pk_native_bank_path(0,"fallback"),"fallback"));
    puf_ini_free(&ini);
    puts("Native league: all bank paths, teams, reset observations and drafts passed");
}
