#define _POSIX_C_SOURCE 200809L
#include "../pokemon.h"
#include "../policy_view.h"
#include <time.h>
typedef struct { PKGame game; PKPolicy policy; int learner, deterministic, fixed_learner; PKMon fixed[6]; double policy_seconds; } Benchmark;
void* bench_create(const char* checkpoint, int deterministic) {
    Benchmark* b=calloc(1,sizeof(*b));
    pk_load_policy(&b->policy,checkpoint,0,0,NULL);
    b->deterministic=deterministic;
    b->game.draft=1; b->game.max_updates=512;
    return b;
}
void bench_fixed_learner(void* handle,int enabled) { ((Benchmark*)handle)->fixed_learner=enabled; }
int bench_reset(void* handle, uint64_t seed, int learner, const uint8_t* team) {
    Benchmark* b=handle;b->learner=learner;
    memcpy(b->fixed,team,sizeof(b->fixed));
    memset(&b->game,0,sizeof(b->game));b->game.draft=1;b->game.max_updates=512;b->game.rng=seed;
    pk_parse_fixed_team(&b->game,learner,b->policy.team);
    pk_parse_lead(&b->game,learner,b->policy.lead);
    pk_reset_policy(&b->policy,seed);
    pk_game_reset(&b->game);
    while(b->game.phase!=PK_PHASE_BATTLE && !b->game.result) {
        int other=1-learner,action;
        if(b->game.phase==PK_PHASE_SPECIES) action=b->fixed[b->game.picks].species-1;
        else {
            int i=b->game.move_index[other];
            int n=i<6?pk_move_count(&b->game.teams[other][i]):4;
            int move=i<6 && n<4?b->fixed[i].moves[n]:0;
            action=move?move-1:PK_MOVE_DONE;
        }
        if(!b->game.masks[other][action])return -1;
        if(b->fixed_learner) {
            // A controlled battle-only test: force the same sourced species,
            // lead and moves on both players, maintaining recurrent draft history.
            int forced;
            if(b->game.phase==PK_PHASE_SPECIES)forced=b->fixed[b->game.picks].species-1;
            else {
                int i=b->game.move_index[learner];
                int n=i<6?pk_move_count(&b->game.teams[learner][i]):4;
                int move=i<6 && n<4?b->fixed[i].moves[n]:0;
                forced=move?move-1:PK_MOVE_DONE;
            }
            if(!b->game.masks[learner][forced])return -3;
            memset(b->game.masks[learner],0,PK_ACTIONS);b->game.masks[learner][forced]=1;
            memcpy(b->game.obs[learner]+480,b->game.masks[learner],PK_ACTIONS);
        }
        int chosen=pk_policy_action(&b->policy,b->game.obs[learner],b->game.masks[learner],b->deterministic);
        if(pk_game_step(&b->game,learner==0?chosen:action,learner==0?action:chosen)==4)return -2;
    }
    return b->game.result;
}
int bench_state(void* handle, uint8_t* out) { return pk_benchmark_state(&((Benchmark*)handle)->game.battle,out); }
void bench_fields(void* handle, uint16_t* out) { pk_benchmark_fields(&((Benchmark*)handle)->game.battle,out); }
int bench_map(void* handle, int choice) {
    Benchmark* b=handle;return pk_benchmark_choice(&b->game.battle,1-b->learner,choice);
}
// Return all legal engine choice bytes, ordered, for bridge parity checks.
int bench_legal(void* handle, int side, uint8_t* out) {
    Benchmark* b=handle;int n=0;
    for(int c=0;c<256;c++)if(pk_benchmark_choice(&b->game.battle,side,c)>=0)out[n++]=c;
    return n;
}
int bench_step(void* handle,int opponent_action,int* selected) {
    Benchmark* b=handle;int p=b->learner;
    if(b->game.result)return b->game.result;
    if(opponent_action<0 || opponent_action>=PK_ACTIONS || !b->game.masks[1-p][opponent_action])return -1;
    struct timespec start,end;
    clock_gettime(CLOCK_MONOTONIC,&start);
    int a=pk_policy_action(&b->policy,b->game.obs[p],b->game.masks[p],b->deterministic);
    clock_gettime(CLOCK_MONOTONIC,&end);
    b->policy_seconds=(end.tv_sec-start.tv_sec)+(end.tv_nsec-start.tv_nsec)*1e-9;
    *selected=-1;
    for(int c=0;c<256;c++)if(pk_benchmark_choice(&b->game.battle,p,c)==a){*selected=c;break;}
    if(*selected<0)return -2;
    return pk_game_step(&b->game,p==0?a:opponent_action,p==0?opponent_action:a);
}
int bench_native_choice(void* handle,int side,int action) {
    Benchmark* b=handle;
    for(int c=0;c<256;c++)if(pk_benchmark_choice(&b->game.battle,side,c)==action)return c;
    return -1;
}
void bench_team(void* handle,uint8_t* out) { Benchmark* b=handle;memcpy(out,b->game.teams[b->learner],30); }
double bench_policy_seconds(void* handle) { return ((Benchmark*)handle)->policy_seconds; }
void bench_logits(void* handle,float* out) { Benchmark* b=handle;memcpy(out,b->policy.net->output,169*sizeof(float)); }
void bench_destroy(void* handle) { Benchmark* b=handle;pk_free_policy(&b->policy);free(b); }
