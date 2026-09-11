#define _POSIX_C_SOURCE 200809L
#include "../pokemon.h"

typedef struct {
    unsigned char pre[64], obs[PK_OBS], mid[64], mask[PK_ACTIONS], post[64];
    float action, reward, terminal;
} Buffers;

static void verify(Env* env, Buffers* buffers, const int mapping[2]) {
    for (int p=0;p<2;p++) {
        Buffers* b=&buffers[mapping[p]];
        for(int i=0;i<64;i++) assert(b->pre[i]==0xa5 && b->mid[i]==0xa5 && b->post[i]==0xa5);
        assert(!memcmp(b->obs,env->game.obs[p],PK_OBS));
        assert(!memcmp(b->mask,env->game.masks[p],PK_ACTIONS));
        assert(!memcmp(b->obs+480,b->mask,PK_ACTIONS));
        int legal=0;
        for(int a=0;a<PK_ACTIONS;a++) { assert(b->mask[a]<=1); legal+=b->mask[a]; }
        assert(legal>0);
        assert(isfinite(b->reward) && fabsf(b->reward)<=1);
        assert(b->terminal==0 || b->terminal==1);
        if(!b->terminal) assert(b->reward==0);
    }
    assert(buffers[mapping[0]].reward == -buffers[mapping[1]].reward);
    assert(buffers[mapping[0]].terminal == buffers[mapping[1]].terminal);
}

int main(int argc, char** argv) {
    uint64_t rng=918273;
    long steps=0,episodes=0;
    for(int mode=0;mode<2;mode++) for(int layout=0;layout<3;layout++) {
        Env env={0}; Buffers buffers[4];
        memset(buffers,0xa5,sizeof(buffers));
        Dict kwargs={0};
        dict_set(&kwargs,"generation",1); dict_set(&kwargs,"format",0);
        dict_set(&kwargs,"team_selection",mode); dict_set(&kwargs,"max_updates",512);
        dict_set(&kwargs,"seed",123+layout);
        puf_init(&env,&kwargs);
        // Non-contiguous physical buffers, swapped seats, learner/learner and
        // learner/frozen layouts. Check that publication follows agent pointers.
        int mapping[2]={layout==1?3:0,layout==1?0:3};
        env.tag=layout==2?0:2;
        env.agents[0].policy=layout==1?1:0;
        env.agents[1].policy=layout==1?0:1;
        for(int p=0;p<2;p++) {
            Buffers* b=&buffers[mapping[p]];
            env.agents[p].observations=b->obs; env.agents[p].action_mask=b->mask;
            env.agents[p].actions=&b->action; env.agents[p].rewards=&b->reward; env.agents[p].terminals=&b->terminal;
        }
        puf_reset(&env); verify(&env,buffers,mapping);
        if(argc>1 && !strcmp(argv[1],"--invalid")) {
            env.audit=1;
            buffers[mapping[0]].action=159;
            puf_step(&env);
            assert(!"audit must reject the illegal action before fallback");
        }
        for(int t=0;t<200000;t++) {
            for(int p=0;p<2;p++) {
                Buffers* b=&buffers[mapping[p]];
                b->action=(float)pk_random_action(b->mask,&rng);
                b->reward=NAN; b->terminal=NAN; // Catch outputs not written.
            }
            puf_step(&env); verify(&env,buffers,mapping);
            assert(env.game.invalid_actions==0 && env.log.invalid_actions==0);
            if(buffers[mapping[0]].terminal) {
                episodes++;
                assert(env.game.updates==0 && env.game.result==0);
                assert(!env.tag || env.boundary_reached);
                env.boundary_reached=0; // Trainer consumes boundary flag.
            }
            if(t%127==0) { puf_reset(&env); verify(&env,buffers,mapping); }
            if(t%1024==0) memset(&env.log,0,sizeof(env.log));
            steps++;
        }
        for(int slot=1;slot<=2;slot++) {
            const unsigned char* bytes=(const unsigned char*)&buffers[slot];
            for(size_t i=0;i<sizeof(Buffers);i++) assert(bytes[i]==0xa5);
        }
        dict_clear(&kwargs);
    }
    printf("Contract audit passed: %ld joint steps, %ld completed episodes; guarded buffers, both seats/banks, explicit/autoresets, finite rewards, exact nonempty masks\n",steps,episodes);
}
