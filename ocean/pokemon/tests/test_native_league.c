#define _POSIX_C_SOURCE 200809L
#include "../pokemon.h"

int main(void) {
    Ini ini={0};
    puf_ini_load_env(&ini,"pokemon",0,NULL);
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
        while(env.game.picks<6) {
            int a=pk_random_action(env.game.masks[0],&env.game.rng);
            int foe=pk_random_action(env.game.masks[1],&env.game.rng);
            pk_game_step(&env.game,a,foe);
        }
        if(expected.fixed_enabled[1]) for(int i=0;i<expected.required_count[1];i++) {
            int found=0;
            for(int j=0;j<6;j++) found+=env.game.teams[1][j]==expected.fixed_team[1][i];
            assert(found==1);
        }
    }
    puf_ini_free(&ini);
    // The league has its own off switch, independent of selfplay.
    puf_ini_load_env(&ini,"pokemon",0,NULL);
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
