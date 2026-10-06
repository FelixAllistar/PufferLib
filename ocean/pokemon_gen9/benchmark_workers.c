#include "pokemon_gen9.h"
#include <pthread.h>
#include <time.h>

typedef struct {
    int id,games,target;
    long decisions,rows,steps;
    float completed;
    pthread_barrier_t *ready,*done;
} Trial;
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void *work(void *arg){
    Trial *trial=(Trial*)arg;Env env={0};Dict kwargs={0};env.rng=trial->id;
    dict_set(&kwargs,"games_per_worker",trial->games);dict_set(&kwargs,"seed",42);
    puf_init(&env,&kwargs);
    for(int a=0;a<env.num_agents;a++){
        Agent *p=env.agents+a;p->observations=(float*)calloc(PG9_OBS,sizeof(float));
        p->actions=(float*)calloc(1,sizeof(float));p->rewards=(float*)calloc(1,sizeof(float));
        p->terminals=(float*)calloc(1,sizeof(float));p->action_mask=(unsigned char*)calloc(PG9_ACTIONS,1);
    }
    puf_reset(&env);unsigned rng=917+trial->id*2654435761u;
    pthread_barrier_wait(trial->ready);
    while(env.log.n<trial->target){
        assert(++trial->steps<10000);
        for(int a=0;a<env.num_agents;a++){
            Agent *p=env.agents+a;int legal[PG9_ACTIONS],n=0;
            rng=rng*1664525u+1013904223u;int moves=(rng%100)<90;
            for(int i=0;i<PG9_ACTIONS;i++)if(p->action_mask[i]&&(!moves||i<9))legal[n++]=i;
            if(!n)for(int i=0;i<PG9_ACTIONS;i++)if(p->action_mask[i])legal[n++]=i;
            assert(n);rng=rng*1664525u+1013904223u;p->actions[0]=(float)legal[rng%(unsigned)n];
            trial->decisions+=p->actions[0]!=0;
        }
        puf_step(&env);trial->rows+=env.num_agents;
        // Every game's masks and terminal accounting, plus all observations
        // at initialization and periodically during the timed run.
        for(int a=0;a<env.num_agents;a++){
            Agent *p=env.agents+a;int legal=0;
            for(int i=0;i<PG9_ACTIONS;i++){assert(p->action_mask[i]<=1);legal+=p->action_mask[i];}assert(legal);
            assert(p->terminals[0]==0||p->terminals[0]==1);
            assert(p->rewards[0]>=-1&&p->rewards[0]<=1);
            if(trial->steps==1||trial->steps%16==0)
                for(int j=0;j<PG9_OBS;j++)assert(isfinite(p->observations[j]));
        }
        for(int g=0;g<env.games;g++){
            assert(env.agents[2*g].terminals[0]==env.agents[2*g+1].terminals[0]);
            assert(env.agents[2*g].rewards[0]==-env.agents[2*g+1].rewards[0]);
        }
    }
    trial->completed=env.log.n;pthread_barrier_wait(trial->done);
    puf_close(&env);
    for(int a=0;a<env.num_agents;a++){
        Agent *p=env.agents+a;free(p->observations);free(p->actions);free(p->rewards);free(p->terminals);free(p->action_mask);
    }
    dict_clear(&kwargs);return NULL;
}
int main(int argc,char **argv){
    assert(argc==4);int workers=atoi(argv[1]),games=atoi(argv[2]),target=atoi(argv[3]);
    assert(workers>=1&&workers<=8&&games>=1&&games<=PG9_MAX_GAMES&&target>=games);
    pthread_barrier_t ready,done;pthread_barrier_init(&ready,NULL,workers+1);pthread_barrier_init(&done,NULL,workers+1);
    pthread_t threads[8];Trial trials[8]={0};double start=now();
    for(int i=0;i<workers;i++){
        trials[i].id=i;trials[i].games=games;trials[i].target=target;trials[i].ready=&ready;trials[i].done=&done;
        assert(!pthread_create(threads+i,NULL,work,trials+i));
    }
    pthread_barrier_wait(&ready);double readyAt=now();pthread_barrier_wait(&done);double end=now();
    long decisions=0,rows=0;float completed=0;
    for(int i=0;i<workers;i++){pthread_join(threads[i],NULL);decisions+=trials[i].decisions;rows+=trials[i].rows;completed+=trials[i].completed;}
    printf("{\"workers\":%d,\"games_per_worker\":%d,\"agents\":%d,\"completed_games\":%.0f,\"decisions\":%ld,\"agent_rows\":%ld,\"startup_seconds\":%.6f,\"seconds\":%.6f,\"decisions_per_second\":%.3f,\"agent_rows_per_second\":%.3f}\n",
        workers,games,2*workers*games,completed,decisions,rows,readyAt-start,end-readyAt,decisions/(end-readyAt),rows/(end-readyAt));
    pthread_barrier_destroy(&ready);pthread_barrier_destroy(&done);return 0;
}
