/* Fixed new recovery Puffer-hook smoke through a minimal test-only ABI shim.
 * No training, match data, throughput benchmark, or learned checkpoint. */
#ifndef PC_POKER_HEADER
#define PC_POKER_HEADER "../poker.h"
#endif
#include PC_POKER_HEADER
static uint64_t digest=UINT64_C(1469598103934665603);
static void hash_bytes(const void* data,size_t n){const unsigned char* p=data;while(n--){digest^=*p++;digest*=UINT64_C(1099511628211);}}
static void require(int ok){if(!ok){fprintf(stderr,"Puffer bridge proof assertion failed\n");abort();}}
#if POKER_RIVER_CONTINUATION && POKER_PLAYERS == 2
static void inspect_context(Env* e) {
    PcAudit a;require(pc_audit(e->river_context[1],&a));
    require(a.generation==e->river_generation && a.phase!=PC_ERROR && a.phase!=PC_ENDED);
    require(a.events==e->game->decisions[e->lane] && !a.pending);
    PcPublic state;require(pc_public_from_game(e->game,e->lane,&state));
    unsigned char expected[32];pc_public_hash(&state,expected);require(!memcmp(expected,a.public_hash,32));
}
#endif
int main(void) {
    DictItem vi[]={{"total_agents",2,NULL},{"num_buffers",1,NULL},{"num_policies",1,NULL}};
    DictItem ei[]={{"native_league",0,"None"},{"seed",1937,NULL},{"starting_stack",20000,NULL},
        {"num_players",POKER_PLAYERS,NULL},{"bot_policy",7,NULL},{"rules_fraction",1,NULL},
        {"equity_samples",32,NULL},{"native_baseline_mask",0,NULL},{"native_history",0,NULL},
        {"river_context_namespace",88172026,NULL}};
    Dict vk={vi,3},ek={ei,10};int size=0,starts[1],counts[1];Env* env=my_vec_init(&size,starts,counts,&vk,&ek);
    require(size==2 && counts[0]==2 && starts[0]==0 && env[0].game==env[1].game && env[0].lane!=env[1].lane);
#if POKER_RIVER_CONTINUATION && POKER_PLAYERS == 2
    // A second vector may reuse local indexes and seeds, but its explicit
    // coordinator namespace makes ownership distinguishable across vectors.
    ei[9].value=88172027;Env* other=my_vec_init(&size,starts,counts,&vk,&ek);ei[9].value=88172026;
    for(int i=0;i<2;i++)require(memcmp(env[i].river_owner[1],other[i].river_owner[1],32)!=0);
    puf_close(other+1);puf_close(other);free(other);
#endif
    float obs[2][POKER_OBS]={{0}},actions[2]={0},masks[2][8]={{0}},rewards[2]={0},terminals[2]={0};
    for(int i=0;i<2;i++) {
        env[i].agents[0].observations=obs[i];env[i].agents[0].actions=actions+i;env[i].agents[0].action_mask=masks[i];
        env[i].agents[0].rewards=rewards+i;env[i].agents[0].terminals=terminals+i;puf_reset(env+i);
    }
    for(int step=0;step<32;step++)for(int i=0;i<2;i++) {
        require(env[i].game->actor[env[i].lane]==0 && masks[i][1]==1);
#if POKER_RIVER_CONTINUATION && POKER_PLAYERS == 2
        inspect_context(env+i);PcAudit untouched;require(pc_audit(env[1-i].river_context[1],&untouched));
        uint64_t other_rng=env[1-i].policy_rng;
        // A byte-for-byte full engine rebuild is not a new real hand and may
        // not discard the current context or advance its identity.
        if(step==7){Poker* copy=malloc(sizeof(Poker));require(copy!=NULL);memcpy(copy,env[i].game,sizeof(Poker));
            memcpy(env[i].game,copy,sizeof(Poker));free(copy);inspect_context(env+i);}
#endif
        actions[i]=1;puf_step(env+i);hash_bytes(obs[i],sizeof(obs[i]));hash_bytes(masks[i],sizeof(masks[i]));
        hash_bytes(rewards+i,sizeof(float));hash_bytes(terminals+i,sizeof(float));hash_bytes(&env[i].policy_rng,sizeof(uint64_t));
#if POKER_RIVER_CONTINUATION && POKER_PLAYERS == 2
        PcAudit check;require(pc_audit(env[1-i].river_context[1],&check));require(!memcmp(&untouched,&check,sizeof(check)) && other_rng==env[1-i].policy_rng);
        inspect_context(env+i);
#endif
    }
#if POKER_RIVER_CONTINUATION && POKER_PLAYERS == 2
    for(int i=0;i<2;i++) {
        uint64_t old=env[i].river_generation;puf_reset(env+i);require(env[i].river_generation>old);inspect_context(env+i);
        old=env[i].river_generation;puf_set_bot_policy(env+i,1);require(env[i].river_generation>old);inspect_context(env+i);
        require(env[i].baseline_styles[1]==1);
    }
#endif
    printf("{\"players\":%d,\"steps\":64,\"digest\":\"%016llx\",\"hands\":[%.0f,%.0f]}\n",POKER_PLAYERS,
        (unsigned long long)digest,env[0].log.n,env[1].log.n);
    // All lanes release their context before the shared game owner frees it.
    puf_close(env+1);puf_close(env);free(env);return 0;
}
