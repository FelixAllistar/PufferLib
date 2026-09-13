#define _POSIX_C_SOURCE 200809L
#include "../pokemon.h"
#include <time.h>

typedef struct { uint8_t obs[640],mask[160]; float action,reward,terminal; } Buffer;
static void setup(Env* env,Buffer buffers[2],float probability) {
    memset(env,0,sizeof(*env)); memset(buffers,0,2*sizeof(*buffers));
    Dict kwargs={0};
    dict_set(&kwargs,"generation",1); dict_set(&kwargs,"format",0);
    dict_set(&kwargs,"team_selection",1); dict_set(&kwargs,"max_updates",512);
    dict_set(&kwargs,"seed",871); dict_set(&kwargs,"reset_state_prob",probability);
    puf_init(env,&kwargs); dict_clear(&kwargs);
    for(int p=0;p<2;p++) {
        env->agents[p].observations=buffers[p].obs; env->agents[p].action_mask=buffers[p].mask;
        env->agents[p].actions=&buffers[p].action; env->agents[p].rewards=&buffers[p].reward;
        env->agents[p].terminals=&buffers[p].terminal;
    }
    puf_reset(env);
}
static void verify(Env* env,Buffer b[2]) {
    assert(b[0].reward==-b[1].reward && b[0].terminal==b[1].terminal);
    assert(isfinite(b[0].reward) && fabsf(b[0].reward)<=1);
    for(int p=0;p<2;p++) {
        assert(b[p].obs[476]==env->game.reset_source);
        assert(!memcmp(b[p].obs,env->game.obs[p],640));
        assert(!memcmp(b[p].mask,env->game.masks[p],160));
        assert(!memcmp(b[p].obs+480,b[p].mask,160));
        int count=0; for(int a=0;a<160;a++) { assert(b[p].mask[a]<=1); count+=b[p].mask[a]; }
        assert(count>0);
    }
}
static void make_states(PKGame samples[3]) {
    PKGame g={0}; g.draft=1; g.max_updates=512; g.rng=888;
    uint64_t rng=9234; int found[3]={0};
    for(int game=0;game<1000 && (!found[0] || !found[1] || !found[2]);game++) {
        pk_game_reset(&g);
        while(!g.result) {
            if(g.picks==6) {
                int alive=0; for(int i=0;i<6;i++) alive+=g.obs[0][16+32*i+1]>0;
                int phase=g.updates==0?0:alive<=3?2:1;
                if(!found[phase]) { samples[phase]=g; found[phase]=1; }
            }
            pk_game_step(&g,pk_random_action(g.masks[0],&rng),pk_random_action(g.masks[1],&rng));
        }
    }
    for(int k=0;k<3;k++) assert(found[k] && pk_state_valid(&samples[k]));
}
static void write_fixture(const char* path,PKStateHeader* header,PKGame samples[3],int short_file) {
    FILE* f=fopen(path,"wb"); assert(f);
    assert(fwrite(header,sizeof(*header),1,f)==1);
    assert(fwrite(samples,sizeof(PKGame),short_file?2:3,f)==(size_t)(short_file?2:3));
    assert(!fclose(f));
}
static void test_file(PKGame samples[3]) {
    char path[]="/tmp/pokemon-states-test-XXXXXX";
    int fd=mkstemp(path); assert(fd>=0); close(fd);
    PKStateHeader h={0}; memcpy(h.magic,"PKSTATE1",8); h.version=1; h.record_bytes=sizeof(PKGame);
    for(int k=0;k<3;k++) h.counts[k]=1;
    strcpy(h.catalog,PK_CATALOG_SHA); strcpy(h.schema,PK_STATE_SCHEMA);
    h.checksum=pk_state_hash(samples,3*sizeof(PKGame));
    PKStateBank b={0};
    write_fixture(path,&h,samples,0); assert(pk_state_load(&b,path)); assert(b.count==3);
    assert(!memcmp(b.states,samples,3*sizeof(PKGame))); free(b.states); b.states=NULL;
    h.checksum^=1; write_fixture(path,&h,samples,0); assert(!pk_state_load(&b,path)); h.checksum^=1;
    h.version++; write_fixture(path,&h,samples,0); assert(!pk_state_load(&b,path)); h.version--;
    h.catalog[0]^=1; write_fixture(path,&h,samples,0); assert(!pk_state_load(&b,path)); h.catalog[0]^=1;
    h.schema[0]^=1; write_fixture(path,&h,samples,0); assert(!pk_state_load(&b,path)); h.schema[0]^=1;
    write_fixture(path,&h,samples,1); assert(!pk_state_load(&b,path));
    samples[0].obs[0][216]^=1; h.checksum=pk_state_hash(samples,3*sizeof(PKGame));
    write_fixture(path,&h,samples,0); assert(!pk_state_load(&b,path)); samples[0].obs[0][216]^=1;
    unlink(path);
}
static void test_restore(PKGame samples[3]) {
    uint64_t rng=771;
    for(int k=0;k<3;k++) for(int repeat=0;repeat<100;repeat++) {
        PKGame a=samples[k],b={0}; b.rng=9876;
        uint64_t seed=pk_random(&rng);
        pk_state_restore(&b,&samples[k],seed);
        assert(b.rng==9876 && b.updates==samples[k].updates && b.max_updates==512);
        assert(b.reset_source==1);
        for(int p=0;p<2;p++) for(int i=0;i<640;i++) if(i!=476) assert(a.obs[p][i]==b.obs[p][i]);
        pk_reseed(&a.battle,seed); a.reset_source=1; pk_game_observe(&a);
        for(int step=0;step<16 && !a.result;step++) {
            int a0=pk_random_action(a.masks[0],&rng),a1=pk_random_action(a.masks[1],&rng);
            assert(pk_game_step(&a,a0,a1)==pk_game_step(&b,a0,a1));
            assert(!memcmp(a.obs,b.obs,sizeof(a.obs)) && !memcmp(&a.battle,&b.battle,sizeof(a.battle)));
            assert(a.obs[0][476]==1 && a.obs[1][476]==1);
        }
    }
}
static void test_adapter(void) {
    Env env; Buffer b[2]; setup(&env,b,.5f); env.audit=1;
    uint64_t rng=83453;
    for(int step=0;step<120000;step++) {
        int old_source=env.game.reset_source;
        for(int p=0;p<2;p++) b[p].action=pk_random_action(b[p].mask,&rng);
        puf_step(&env); verify(&env,b);
        if(!b[0].terminal) assert(env.game.reset_source==old_source);
        else assert(env.episode_steps==0 && env.game.result==0);
    }
    float n=env.log.root_games+env.log.reset_games;
    assert(n>500 && env.log.reset_games/n>.44 && env.log.reset_games/n<.56);
    assert(env.log.root_steps+env.log.reset_steps==120000);
    assert(env.log.opening_starts+env.log.midgame_starts+env.log.endgame_starts==env.log.reset_games);
    assert(env.log.opening_starts/env.log.reset_games>.3 && env.log.opening_starts/env.log.reset_games<.5);
    // Both explicit reset and automatic reset clear the auxiliary marker for root games.
    env.reset_state_prob=0; puf_reset(&env); assert(env.game.picks==0 && !env.game.reset_source);
    for(int step=0;step<1000;step++) {
        for(int p=0;p<2;p++) b[p].action=pk_random_action(b[p].mask,&rng);
        puf_step(&env); verify(&env,b); assert(!env.game.reset_source);
    }
    // Restoring a late state does not buy another 512 decisions.
    env.reset_state_prob=1; puf_reset(&env); env.game.updates=511;
    for(int p=0;p<2;p++) b[p].action=pk_random_action(b[p].mask,&rng);
    puf_step(&env); assert(b[0].terminal==1 && env.episode_steps==0); verify(&env,b);
    printf("Adapter checked %.0f completed games; reset fraction %.4f\n",n,env.log.reset_games/n);
}
static double seconds(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec*1e-9; }
int main(int argc,char** argv) {
    if(argc==3 && !strcmp(argv[1],"--benchmark")) {
        assert(pk_state_load(&pk_state_bank,argv[2]));
        for(int trial=0;trial<6;trial++) {
            float prob=trial%2?.5f:0;
            Env env; Buffer b[2]; setup(&env,b,prob); uint64_t rng=91735;
            double start=seconds();
            for(int step=0;step<300000;step++) {
                for(int p=0;p<2;p++) b[p].action=pk_random_action(b[p].mask,&rng);
                puf_step(&env);
            }
            double elapsed=seconds()-start;
            printf("{\"trial\":%d,\"reset_probability\":%.1f,\"agent_sps\":%.0f,\"seconds\":%.6f,\"reset_step_fraction\":%.6f}\n",
                trial,prob,600000/elapsed,elapsed,env.log.reset_steps/(env.log.root_steps+env.log.reset_steps));
        }
        free(pk_state_bank.states); return 0;
    }
    PKGame samples[3]; make_states(samples); test_file(samples); test_restore(samples);
    pk_state_bank.states=samples; pk_state_bank.count=3;
    for(int k=0;k<3;k++) { pk_state_bank.offsets[k]=k; pk_state_bank.header.counts[k]=1; }
    test_adapter(); pk_state_bank.states=NULL;
    puts("State-bank tests passed: file integrity, deterministic continuation, private observations, persistent flag, masks, both resets, mixture, rewards and clocks.");
}
