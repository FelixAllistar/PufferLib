#include "swat.h"
#include <assert.h>
#include <string.h>

typedef struct Fixture {
    Env env;
    float obs[OBS_SIZE],actions[NUM_ATNS],reward,terminal;
} Fixture;

static void setup(Fixture* f, unsigned int seed, int max_ticks) {
    memset(f,0,sizeof(*f));
    Dict cfg={0};
    dict_set(&cfg,"max_ticks",max_ticks);
    dict_set(&cfg,"hostile_fire",0);
    f->env.rng=seed;
    puf_init(&f->env,&cfg);
    f->env.agents[0].observations=f->obs;
    f->env.agents[0].actions=f->actions;
    f->env.agents[0].rewards=&f->reward;
    f->env.agents[0].terminals=&f->terminal;
    puf_reset(&f->env);
    dict_clear(&cfg);
}

int main(void) {
    Fixture pair[2];
    for(int i=0;i<2;i++) setup(&pair[i],731,31);
    int sizes[]=ACT_SIZES,ends=0;
    uint32_t rng=123;
    for(int t=0;t<1024;t++) {
        for(int head=0;head<NUM_ATNS;head++)
            pair[0].actions[head]=pair[1].actions[head]=(float)(swat_random(&rng)%sizes[head]);
        for(int i=0;i<2;i++) {
            puf_step(&pair[i].env);
            for(int o=0;o<OBS_SIZE;o++) assert(isfinite(pair[i].obs[o]));
            assert(isfinite(pair[i].reward));
        }
        assert(!memcmp(pair[0].obs,pair[1].obs,sizeof(pair[0].obs)));
        assert(pair[0].reward==pair[1].reward && pair[0].terminal==pair[1].terminal);
        if(pair[0].terminal) {
            ends++;
            assert(pair[0].env.sim->tick==0 && pair[0].env.sim->end==SWAT_RUNNING);
        }
    }
    assert(ends>=32);
    for(int i=0;i<2;i++) puf_close(&pair[i].env);

    Fixture f; setup(&f,42,8);
    float neutral[NUM_ATNS]={2,2,1,1,1,0,1,0,0,0,0,0,0,0};
    memcpy(f.actions,neutral,sizeof(neutral));
    // Moving Env must preserve Box3D callback metadata owned by heap Sim.
    Env moved=f.env;
    memset(&f.env,0,sizeof(f.env));
    assert(b3Body_GetUserData(moved.sim->actors[0].controller.body.body)==&moved.sim->actors[0].tag);
    for(int t=0;t<8;t++) {
        puf_step(&moved);
        assert(f.terminal==(t==7));
        assert(fabsf(f.reward+0.001f)<1e-7f);
    }
    assert(moved.log.episode_length==8 && moved.log.n==1);
    assert(moved.sim->tick==0);
    puf_step(&moved);
    assert(f.terminal==0 && moved.sim->tick==1);
    puf_close(&moved);

    // Civilian harm must retain its penalty on the terminal transition even
    // though the returned observation already belongs to the reset episode.
    setup(&f,42,300);
    memcpy(f.actions,neutral,sizeof(neutral));
    SwatController* c=&f.env.sim->actors[0].controller;
    b3Body_SetTransform(c->body.body,(b3Pos){19,c->body.totalHeight*0.5f+0.02f,-2.5f},b3Quat_identity);
    c->yaw=-SWAT_PI*0.5f; c->pitch=-0.13f; c->ads=1;
    f.actions[7]=1; f.actions[8]=1;
    puf_step(&f.env);
    assert(f.terminal==1 && f.reward < -5);
    assert(f.env.log.civilian_damage>0 && f.env.log.perf==0 && f.env.log.shots==1);
    assert(f.env.sim->tick==0 && f.env.sim->actors[2].health==100);
    puf_close(&f.env);

    // Place a cleared mission at extraction to exercise success accounting.
    setup(&f,42,300);
    memcpy(f.actions,neutral,sizeof(neutral));
    swat_sim_damage_actor(f.env.sim,1,0,1000);
    c=&f.env.sim->actors[0].controller;
    b3Body_SetTransform(c->body.body,b3OffsetPos(f.env.sim->extraction,
        swat_v(0,c->body.totalHeight*0.5f+0.02f,0)),b3Quat_identity);
    puf_step(&f.env);
    assert(f.terminal==1 && fabsf(f.reward-4.999f)<1e-6f);
    assert(f.env.log.perf==1 && f.env.log.n==1 && f.env.log.episode_length==1);
    assert(f.env.sim->tick==0 && swat_sim_hostiles(f.env.sim)==1);
    puf_close(&f.env);
    printf("PASS SWAT adapter: obs=%d, heads=%d, 2048 paired transitions, %d paired resets, relocation, exact timeout, civilian failure and extraction reward\n",OBS_SIZE,NUM_ATNS,ends);
    return 0;
}
