#define _POSIX_C_SOURCE 200809L
#include "../pokemon.h"
#include "fixtures.h"
#include <omp.h>

typedef struct { uint8_t obs[PK_OBS], mask[PK_ACTIONS]; float action, reward, terminal; } Buffer;

static void test_deck(void) {
    PKCoreDeck a={0},b={0},other={0};
    assert(pk_core_init(&a,"all",762));
    assert(pk_core_init(&b,"all",762));
    assert(pk_core_init(&other,"all",763));
    assert(a.species_count==149 && a.count==540274);
    assert(memcmp(a.cards,other.cards,a.count*sizeof(uint32_t)));
    uint32_t first=a.cards[0];
    uint8_t* seen=(uint8_t*)calloc(150*150*150,1); assert(seen);
    for(int cycle=0;cycle<2;cycle++) {
        memset(seen,0,150*150*150);
        for(uint32_t i=0;i<a.count;i++) {
            uint32_t x=pk_core_deal(&a); assert(x==pk_core_deal(&b));
            int s0=x&255,s1=(x>>8)&255,s2=(x>>16)&255;
            assert(1<=s0 && s0<s1 && s1<s2 && s2<=149);
            unsigned index=(s0*150+s1)*150+s2;
            assert(!seen[index]); seen[index]=1;
            assert(a.cycle==(uint64_t)cycle);
        }
        for(int x=1;x<148;x++) for(int y=x+1;y<149;y++) for(int z=y+1;z<=149;z++)
            assert(seen[(x*150+y)*150+z]);
    }
    assert(a.cards[0]!=first);
    assert(a.assigned==1080548);
    free(seen); pk_core_free(&a); pk_core_free(&b); pk_core_free(&other);
    assert(pk_core_init(&a,"143,128,113",1));
    assert(a.count==1 && pk_core_deal(&a)==(113u|(128u<<8)|(143u<<16)));
    assert(pk_core_deal(&a)==(113u|(128u<<8)|(143u<<16)) && a.cycle==1);
    assert(!pk_core_init(&a,"1,1,2",0));
    assert(!pk_core_init(&a,"1,2",0));
    assert(!pk_core_init(&a,"1,2,150",0));
    assert(!pk_core_init(&a,"1,2,3,",0));
    assert(!pk_core_init(&a,"1,2,wat",0));
    assert(pk_core_init(&a,"128, 113,143,103",9));
    assert(pk_core_init(&b,"103,113,128,143",9));
    assert(!memcmp(a.cards,b.cards,4*sizeof(uint32_t)));
    for(int i=0;i<17;i++)pk_core_deal(&a);
    assert(pk_core_seek(&b,17,12));
    assert(b.assigned==17 && b.drafted==12);
    for(int i=0;i<20;i++)assert(pk_core_deal(&a)==pk_core_deal(&b));
    assert(!pk_core_seek(&b,3,4));
    pk_core_free(&a); pk_core_free(&b);
    puts("Deck: every one of 540274 triples exactly once in each of two deterministic shuffles PASS");
}

static void setup(Env* env, Buffer b[2], unsigned id) {
    memset(env,0,sizeof(*env)); memset(b,0,2*sizeof(*b)); env->rng=id;
    Dict kwargs={0};
    dict_set(&kwargs,"generation",1); dict_set(&kwargs,"format",0);
    dict_set(&kwargs,"team_selection",1); dict_set(&kwargs,"max_updates",8);
    dict_set(&kwargs,"seed",71);
    puf_init(env,&kwargs); dict_clear(&kwargs);
    env->audit=1;
    for(int p=0;p<2;p++) {
        env->agents[p].observations=b[p].obs; env->agents[p].action_mask=b[p].mask;
        env->agents[p].actions=&b[p].action; env->agents[p].rewards=&b[p].reward;
        env->agents[p].terminals=&b[p].terminal;
    }
    puf_reset(env);
}
static void verify_draft(PKGame* g) {
    for(int p=0;p<2;p++) if(g->fixed_enabled[p]==3) {
        for(int j=0;j<3;j++) {
            int found=0;
            for(int k=0;k<6;k++) found+=g->teams[p][k].species==g->fixed_team[p][j];
            assert(found==1);
        }
    }
}
static void test_masks(void) {
    assert(pk_core_init(&pk_core_deck,"113,128,143",77));
    Env env; Buffer b[2]; setup(&env,b,0); pk_core_flush(&env,1);
    assert(pk_core_deck.assigned==2);
    for(int p=0;p<2;p++) for(int i=0;i<3;i++) assert(b[p].obs[477+i]==env.game.fixed_team[p][i]);
    // Changing the opposing assignment cannot leak into our observation/mask.
    PKGame copy=env.game; copy.fixed_team[1][0]=1; pk_game_observe(&copy);
    assert(!memcmp(copy.obs[0],env.game.obs[0],PK_OBS));
    // Repeated startup resets (including expert binding) must not skip cards.
    puf_reset(&env); pk_core_flush(&env,1); assert(pk_core_deck.assigned==2);
    while(env.game.phase!=PK_PHASE_BATTLE) {
        for(int p=0;p<2;p++) {
            int action=-1;
            if(env.game.phase==PK_PHASE_MOVES) {
                for(int a=0;a<164;a++) {
                    assert(env.game.masks[p][a]==(env.game.move_stopped[p]?0:pk_move_allowed(&env.game.teams[p][env.game.move_index[p]],a+1)));
                    if(env.game.masks[p][a])action=a;
                }
                if(action<0)action=PK_MOVE_DONE;
            } else {
                for(int a=0;a<149;a++) if(env.game.masks[p][a]) {
                    action=a;
                    if(!pk_required_species(&env.game,p,a+1)) break;
                }
                if(env.game.picks>=3) assert(pk_required_species(&env.game,p,action+1));
            }
            assert(action>=0); b[p].action=(float)action;
        }
        puf_step(&env); pk_core_flush(&env,1);
    }
    verify_draft(&env.game); assert(pk_core_deck.drafted==2);
    for(int p=0;p<2;p++) for(int j=476;j<480;j++) assert(b[p].obs[j]==0);
    pk_record_teams(&env);
    float forced=0,free_picks=0;
    for(int i=0;i<150;i++) { forced+=env.log.core_forced_picks[i]; free_picks+=env.log.core_free_picks[i]; }
    assert(forced==6 && free_picks==6 && env.log.core_team_samples==2);
    pk_core_free(&pk_core_deck);
    puts("Core masks: all required species, free variants/order/remaining slots, private observations, reusable startup cards PASS");
}
static uint64_t trajectory(int threads) {
    enum { N=8 };
    Env* envs=(Env*)calloc(N,sizeof(Env)); Buffer b[N][2]; uint64_t rng[N];
    assert(envs && pk_core_init(&pk_core_deck,"all",91));
    for(int i=0;i<N;i++) { setup(&envs[i],b[i],(unsigned)i); rng[i]=100+i; }
    pk_core_flush(envs,N);
    uint64_t hash=0; int completed=0;
    for(int t=0;t<600;t++) {
        for(int i=0;i<N;i++) for(int p=0;p<2;p++)
            b[i][p].action=(float)pk_random_action(envs[i].game.masks[p],&rng[i]);
        #pragma omp parallel for schedule(dynamic,1) num_threads(threads)
        for(int i=0;i<N;i++) puf_step(&envs[i]);
        pk_core_flush(envs,N);
        for(int i=0;i<N;i++) {
            hash=(hash*UINT64_C(0x9e3779b97f4a7c15))^pk_state_hash(&envs[i].game,sizeof(PKGame));
            if(envs[i].game.phase==PK_PHASE_BATTLE) verify_draft(&envs[i].game);
            for(int p=0;p<2;p++) {
                assert(!memcmp(b[i][p].obs,envs[i].game.obs[p],PK_OBS));
                assert(b[i][p].reward==-b[i][1-p].reward);
            }
            if(b[i][0].terminal) {
                completed++; assert(envs[i].game.picks==0 && !envs[i].core_pending);
                assert(b[i][1].terminal==1); // Post-reset hook preserves terminal/reward.
            }
        }
    }
    assert(completed>100 && pk_core_deck.drafted>200);
    assert(pk_core_deck.assigned>=pk_core_deck.drafted);
    free(envs); pk_core_free(&pk_core_deck);
    return hash;
}
static void test_experts(void) {
    Ini ini={0}; puf_ini_load_env(&ini,"pokemon",0,NULL);
    puf_ini_put(&ini,"env.force_core_combos","1");
    puf_ini_put(&ini,"env.force_core_prob","1");
    puf_ini_put(&ini,"env.expert_fraction","1");
    puf_ini_put(&ini,"env.reset_state_prob","0");
    puf_ini_put(&ini,"env.native_league",pk_test_native_manifest(2));
    pk_configure(&ini,"train"); assert(pk_native_count==2 && pk_core_deck.count==540274);
    Env env; Buffer b[2]; setup(&env,b,0); env.tag=1;
    pk_core_flush(&env,1); assert(pk_core_deck.assigned==1);
    uint32_t core=env.core_card[0];
    pk_native_bank_loaded(&env,1,0,pk_native_bank_path(0,"unused"));
    assert(pk_core_deck.assigned==1 && env.core_card[0]==core);
    assert(env.game.fixed_enabled[0]==3 && env.game.fixed_enabled[1]==4);
    for(int i=477;i<480;i++) assert(b[1].obs[i]==0);
    uint64_t rng=71;
    while(env.game.phase!=PK_PHASE_BATTLE) {
        for(int p=0;p<2;p++) b[p].action=(float)pk_random_action(env.game.masks[p],&rng);
        puf_step(&env); pk_core_flush(&env,1);
    }
    verify_draft(&env.game);
    for(int j=0;j<6;j++) assert(pk_required_species(&env.game,1,env.game.teams[1][j].species));
    pk_configure(&ini,"eval"); assert(!pk_core_deck.cards);
    assert(puf_ini_get(&ini,"env","force_core_combos")==0);
    puf_ini_put(&ini,"env.force_core_combos","1");
    puf_ini_put(&ini,"env.native_league",pk_test_native_manifest(3));
    pk_configure(&ini,"train"); assert(pk_native_count==3);
    for(int bank=0;bank<3;bank++) {
        setup(&env,b,(unsigned)bank); env.tag=bank+1;
        pk_core_flush(&env,1);
        uint64_t assigned=pk_core_deck.assigned;
        pk_native_bank_loaded(&env,1,bank,pk_native_bank_path(bank,"unused"));
        assert(pk_core_deck.assigned==assigned);
        while(env.game.phase!=PK_PHASE_BATTLE) {
            for(int p=0;p<2;p++) b[p].action=(float)pk_random_action(env.game.masks[p],&rng);
            puf_step(&env); pk_core_flush(&env,1);
        }
        verify_draft(&env.game);
        assert(env.game.fixed_enabled[1]==4);
        for(int j=0;j<env.game.required_count[1];j++) {
            int found=0; for(int k=0;k<6;k++) found+=env.game.teams[1][k].species==env.game.fixed_team[1][j];
            assert(found==1);
        }
    }
    pk_core_free(&pk_core_deck);
    puf_ini_free(&ini);
    puts("Config: two/three experts retain bound teams; only learner consumes cores; eval disables core forcing PASS");
}
static uint64_t mixture_trajectory(int threads, double probability) {
    enum { N=8 };
    Ini ini={0}; puf_ini_load_env(&ini,"pokemon",0,NULL);
    puf_ini_put(&ini,"env.force_core_combos","1");
    puf_ini_put(&ini,"env.reset_state_prob","0");
    puf_ini_put(&ini,"env.native_league",pk_test_native_manifest(3));
    puf_ini_put(&ini,"env.expert_fraction","0.5");
    puf_ini_put(&ini,"vec.total_agents","16");
    char value[32]; snprintf(value,sizeof(value),"%.9g",probability);
    puf_ini_put(&ini,"env.force_core_prob",value);
    pk_configure(&ini,"train");
    assert(pk_native_count==3 && puf_ini_get(&ini,"vec","frozen_bank_pct")==.5);
    assert(puf_ini_get(&ini,"selfplay","enabled")==0);
    Env* envs=(Env*)calloc(N,sizeof(Env)); Buffer b[N][2]; uint64_t rng[N];
    assert(envs);
    for(int i=0;i<N;i++) {
        setup(&envs[i],b[i],(unsigned)i); rng[i]=100+i;
        // Mirror the trainer's static 50% current-policy / 50% expert layout.
        envs[i].tag=i<N/2?0:1+(i-N/2)%3;
    }
    pk_core_flush(envs,N);
    for(int bank=0;bank<3;bank++) {
        uint64_t assigned=pk_core_deck.assigned;
        uint64_t streams[N]; int selected[N];
        for(int i=0;i<N;i++) { streams[i]=envs[i].reset_rng; selected[i]=envs[i].core_episode_selected; }
        pk_native_bank_loaded(envs,N,bank,pk_native_bank_path(bank,"unused"));
        assert(pk_core_deck.assigned==assigned);
        for(int i=0;i<N;i++) {
            assert(streams[i]==envs[i].reset_rng && selected[i]==envs[i].core_episode_selected);
        }
    }
    uint64_t hash=0,started_cards=pk_core_deck.assigned;
    int normal_starts=0,core_starts=0;
    for(int t=0;t<2400;t++) {
        for(int i=0;i<N;i++) {
            PKGame* g=&envs[i].game;
            if(g->picks==0 && g->phase==PK_PHASE_SPECIES) {
                if(envs[i].core_episode_selected) core_starts++; else normal_starts++;
                if(!envs[i].core_episode_selected) {
                    assert(!g->fixed_enabled[0]);
                    for(int a=0;a<149;a++) assert(g->masks[0][a]);
                    for(int a=477;a<480;a++) assert(g->obs[0][a]==0);
                    if(!envs[i].tag) assert(!g->fixed_enabled[1]);
                }
            }
            if(g->phase==PK_PHASE_BATTLE) {
                verify_draft(g);
                if(envs[i].tag) {
                    assert(g->fixed_enabled[1]==4);
                    for(int j=0;j<g->required_count[1];j++) {
                        int found=0; for(int k=0;k<6;k++) found+=g->teams[1][k].species==g->fixed_team[1][j];
                        assert(found==1);
                    }
                }
            }
            for(int p=0;p<2;p++) b[i][p].action=(float)pk_random_action(g->masks[p],&rng[i]);
        }
        #pragma omp parallel for schedule(dynamic,1) num_threads(threads)
        for(int i=0;i<N;i++) puf_step(&envs[i]);
        uint64_t expected_cards=0;
        pk_core_flush(envs,N);
        for(int i=0;i<N;i++) {
            if(b[i][0].terminal && envs[i].core_episode_selected) expected_cards+=envs[i].tag?1:2;
            hash=(hash*UINT64_C(0x9e3779b97f4a7c15))^pk_state_hash(&envs[i].game,sizeof(PKGame));
            assert(!memcmp(b[i][0].obs,envs[i].game.obs[0],PK_OBS));
            assert(b[i][0].terminal==b[i][1].terminal && b[i][0].reward==-b[i][1].reward);
        }
        assert(pk_core_deck.assigned-started_cards==expected_cards);
        started_cards=pk_core_deck.assigned;
    }
    Log total={0};
    for(int i=0;i<N;i++) {
        const float* src=(const float*)&envs[i].log; float* dst=(float*)&total;
        for(size_t j=0;j<sizeof(Log)/sizeof(float);j++) dst[j]+=src[j];
    }
    Dict metrics={0}; puf_log(&total,&metrics);
    double games=0,steps=0;
    const char* names[4]={"normal_self","normal_expert","core_self","core_expert"};
    for(int group=0;group<4;group++) {
        char key[80];
        snprintf(key,sizeof(key),"%s_game_fraction",names[group]); games+=dict_get(&metrics,key);
        snprintf(key,sizeof(key),"%s_step_fraction",names[group]); steps+=dict_get(&metrics,key);
        if(probability==.5) assert(total.mix_games[group]>80);
    }
    assert(fabs(games-1)<1e-6 && fabs(steps-1)<1e-6);
    assert(total.mix_steps[0]+total.mix_steps[2]==N/2*2400);
    assert(total.mix_steps[1]+total.mix_steps[3]==N/2*2400);
    if(probability==0) {
        assert(!pk_core_deck.assigned && !core_starts && !total.core_team_samples);
        assert(total.mix_games[2]==0 && total.mix_games[3]==0);
    } else if(probability==1) {
        assert(!normal_starts && !total.normal_team_samples);
        assert(total.mix_games[0]==0 && total.mix_games[1]==0);
    } else {
        double fraction=(double)core_starts/(core_starts+normal_starts);
        assert(fraction>.4 && fraction<.6);
        assert(total.core_team_samples>0 && total.normal_team_samples>0);
    }
    double normal_picks=0,free_picks=0,forced_picks=0;
    for(int i=0;i<150;i++) {
        normal_picks+=total.normal_team_picks[i];
        free_picks+=total.core_free_picks[i]; forced_picks+=total.core_forced_picks[i];
    }
    assert(normal_picks==6*total.normal_team_samples);
    assert(free_picks==3*total.core_team_samples && forced_picks==free_picks);
    printf("Mixture probability=%.1f workers=%d completed games normal/self=%.0f normal/expert=%.0f core/self=%.0f core/expert=%.0f PASS\n",
        probability,threads,total.mix_games[0],total.mix_games[1],total.mix_games[2],total.mix_games[3]);
    dict_clear(&metrics); free(envs); pk_core_free(&pk_core_deck);
    // Fraction zero disables expert loading and leaves pure current-policy play.
    puf_ini_put(&ini,"env.expert_fraction","0");
    pk_configure(&ini,"train"); assert(!pk_native_count && !pk_native_fraction);
    assert(puf_ini_get(&ini,"vec","num_frozen_banks")==0);
    pk_core_free(&pk_core_deck); puf_ini_free(&ini);
    return hash;
}
int main(void) {
    test_deck(); test_masks();
    uint64_t a=trajectory(1),b=trajectory(4); assert(a==b);
    printf("Deterministic full trajectories: 1 vs 4 workers hash=%llu PASS\n",(unsigned long long)a);
    test_experts();
    (void)mixture_trajectory(1,0);
    (void)mixture_trajectory(1,1);
    a=mixture_trajectory(1,.5); b=mixture_trajectory(4,.5); assert(a==b);
    printf("Mixed-core/expert trajectories: 1 vs 4 workers hash=%llu PASS\n",(unsigned long long)a);
    puts("Core-deck tests PASS");
}
