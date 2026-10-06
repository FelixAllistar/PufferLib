#include "pokemon_gen9.h"
#include <time.h>
int main(void){
    Env env={0};Dict kwargs={0};dict_set(&kwargs,"games_per_worker",4);dict_set(&kwargs,"seed",42);
    puf_init(&env,&kwargs);
    for(int a=0;a<env.num_agents;a++){
        Agent *p=env.agents+a;p->observations=(float*)calloc(PG9_OBS,sizeof(float));
        p->actions=(float*)calloc(1,sizeof(float));p->rewards=(float*)calloc(1,sizeof(float));
        p->terminals=(float*)calloc(1,sizeof(float));p->action_mask=(unsigned char*)calloc(PG9_ACTIONS,1);
    }
    puf_reset(&env);unsigned rng=917;int decisions=0,steps=0;
    struct timespec start,end;clock_gettime(CLOCK_MONOTONIC,&start);
    while(env.log.n<32){
        assert(++steps<10000);
        for(int a=0;a<env.num_agents;a++){
            Agent *p=env.agents+a;int legal[PG9_ACTIONS],n=0;
            rng=rng*1664525u+1013904223u;int moves=(rng%100)<90;
            for(int i=0;i<PG9_ACTIONS;i++)if(p->action_mask[i]&&(!moves||i<9))legal[n++]=i;
            if(!n)for(int i=0;i<PG9_ACTIONS;i++)if(p->action_mask[i])legal[n++]=i;
            assert(n);rng=rng*1664525u+1013904223u;p->actions[0]=(float)legal[rng%(unsigned)n];
            decisions+=p->actions[0]!=0;
        }
        puf_step(&env);
        for(int a=0;a<env.num_agents;a++){
            Agent *p=env.agents+a;assert(p->terminals[0]==0||p->terminals[0]==1);
            assert(p->rewards[0]>=-1&&p->rewards[0]<=1);
            int legal=0;for(int i=0;i<PG9_ACTIONS;i++){assert(p->action_mask[i]<=1);legal+=p->action_mask[i];}assert(legal);
            for(int i=0;i<PG9_OBS;i++)assert(isfinite(p->observations[i]));
        }
        for(int g=0;g<env.games;g++){
            assert(env.agents[2*g].terminals[0]==env.agents[2*g+1].terminals[0]);
            assert(env.agents[2*g].rewards[0]==-env.agents[2*g+1].rewards[0]);
        }
    }
    clock_gettime(CLOCK_MONOTONIC,&end);
    double seconds=(end.tv_sec-start.tv_sec)+(end.tv_nsec-start.tv_nsec)/1e9;
    printf("PASS: C/PufferLib worker transport; games=%.0f decisions=%d steps=%d observations=%d seconds=%.3f decisions/sec=%.2f; masks, finite actor observations, auto-reset, zero-sum terminals\n",
        env.log.n,decisions,steps,PG9_OBS,seconds,decisions/seconds);
    puf_close(&env);
    for(int a=0;a<env.num_agents;a++){free(env.agents[a].observations);free(env.agents[a].actions);free(env.agents[a].rewards);
        free(env.agents[a].terminals);free(env.agents[a].action_mask);}
    dict_clear(&kwargs);return 0;
}
