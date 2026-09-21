#define _POSIX_C_SOURCE 200809L
#include "../pokemon.h"

int main(void) {
    Env env={0}; Dict kwargs={0};
    dict_set(&kwargs,"generation",1); dict_set(&kwargs,"format",0);
    dict_set(&kwargs,"team_selection",1); dict_set(&kwargs,"max_updates",512);
    dict_set(&kwargs,"seed",123); dict_set(&kwargs,"behavior_sleep",.5);
    dict_set(&kwargs,"behavior_paralysis",.5);
    puf_init(&env,&kwargs);
    pk_behavior_enabled=1;pk_behavior_weights[0]=pk_behavior_weights[1]=.5;
    uint8_t obs[2][PK_OBS],mask[2][PK_ACTIONS];float a[2],r[2],t[2];
    for(int p=0;p<2;p++) {
        env.agents[p].observations=obs[p];env.agents[p].action_mask=mask[p];
        env.agents[p].actions=&a[p];env.agents[p].rewards=&r[p];env.agents[p].terminals=&t[p];
    }
    puf_reset(&env);
    uint64_t rng=918273;int episodes=0,events=0;double total=0,bonus=0;
    float cumulative[2][2]={{0}};
    for(int step=0;step<100000;step++) {
        for(int p=0;p<2;p++)a[p]=pk_random_action(mask[p],&rng);
        float score=env.log.slot_0_score;double prior_log=env.log.episode_return;
        puf_step(&env);
        assert(r[0]==-r[1] && isfinite(r[0]));
        double extra=pk_behavior_extra(env.behavior_delta,pk_behavior_weights);
        total+=r[0];bonus+=extra;
        for(int p=0;p<2;p++)for(int k=0;k<2;k++) {
            assert(env.behavior_delta[p][k]>=0);
            events+=env.behavior_delta[p][k]>0;
            cumulative[p][k]+=env.behavior_delta[p][k];
            assert(cumulative[p][k]<=1.000001);
            if(!t[0] && env.game.phase==PK_PHASE_BATTLE) {
                float raw[2];pk_behavior(&env.game.battle,p,raw);
                assert(fabs(cumulative[p][k]-pk_behavior_feature(raw[k],k))<1e-6);
            }
        }
        if(t[0]) {
            double outcome=2*(env.log.slot_0_score-score)-1;
            assert(fabs(total-outcome-bonus)<1e-5);
            assert(fabs(env.log.episode_return-prior_log-total)<.01);
            assert(env.episode_return==0 && env.game.picks==0);
            memset(cumulative,0,sizeof(cumulative));total=bonus=0;episodes++;
        } else assert(fabs(r[0]-extra)<1e-6);
    }
    assert(events>0 && episodes>100);
    dict_clear(&kwargs);
    printf("Behavior adapter audit passed: %d events, %d episodes; zero-sum, capped, non-cancelling, reset-safe\n",events,episodes);
}
