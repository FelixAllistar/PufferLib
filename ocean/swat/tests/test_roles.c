// Headless role task contract v1 checks: shapes, legal-action masks, gaze rays,
// paired determinism, relocation, role rewards, protected kills, inactive
// padding agents, terminal accounting, and the roles_scripted baselines
// (mask legality, obs-only replay, paired determinism, eval logs).
// `test_roles motel` additionally checks the padded 8-agent motel roster, a
// timeout reset and an all-scripted motel.
#define SWAT_ROLES_TRAINING
#define SWAT_ROLES_NO_RENDER
#include "../swat.h"
#include <assert.h>
#include <string.h>

typedef struct Fixture {
    Env env;
    float obs[SWAT_ROLES_MAX_AGENTS][OBS_SIZE],actions[SWAT_ROLES_MAX_AGENTS][NUM_ATNS];
    float rewards[SWAT_ROLES_MAX_AGENTS],terminals[SWAT_ROLES_MAX_AGENTS];
    unsigned char masks[SWAT_ROLES_MAX_AGENTS][SWAT_ROLES_LOGITS];
} Fixture;

static const int sizes[]=ACT_SIZES;

static Fixture* setup(unsigned int seed,Dict* cfg) {
    Fixture* f=calloc(1,sizeof(Fixture)); assert(f);
    f->env.rng=seed;
    puf_init(&f->env,cfg);
    assert(f->env.roster>0 && f->env.roster<=SWAT_MAX_ACTORS);
    assert(f->env.num_agents>=f->env.roster && f->env.num_agents<=SWAT_ROLES_MAX_AGENTS);
    for(int i=0;i<f->env.num_agents;i++) {
        f->env.agents[i].observations=f->obs[i]; f->env.agents[i].actions=f->actions[i];
        f->env.agents[i].rewards=&f->rewards[i]; f->env.agents[i].terminals=&f->terminals[i];
        f->env.agents[i].action_mask=f->masks[i];
    }
    puf_reset(&f->env);
    return f;
}
static void teardown(Fixture* f) { puf_close(&f->env); free(f); }
static Dict annex_config(int max_ticks,int hostile_fire) {
    Dict cfg={0};
    dict_set(&cfg,"max_ticks",max_ticks); dict_set(&cfg,"hostile_fire",hostile_fire);
    dict_set_str(&cfg,"roles_mission","annex");
    return cfg;
}
static void neutral(Fixture* f) {
    for(int n=0;n<f->env.num_agents;n++) {
        for(int h=0;h<SWAT_ROLES_MOTOR_HEADS;h++) f->actions[n][h]=(float)swat_roles_neutral[h];
        for(int k=0;k<SWAT_GAZE_RAYS;k++) {
            f->actions[n][SWAT_ROLE_HEAD_GAZE+2*k]=(float)(k*SWAT_GAZE_YAW_BINS/SWAT_GAZE_RAYS);
            f->actions[n][SWAT_ROLE_HEAD_GAZE+2*k+1]=SWAT_GAZE_PITCH_BINS/2;
        }
    }
}
static bool legal(const unsigned char* mask,int head,int index) {
    return mask[swat_roles_logit_offset(head)+index]!=0;
}
static int legal_count(const unsigned char* mask,int head) {
    int count=0; for(int i=0;i<sizes[head];i++) count+=legal(mask,head,i);
    return count;
}
static void sample(const unsigned char* mask,uint32_t* rng,float* action) {
    for(int h=0;h<NUM_ATNS;h++) {
        int count=legal_count(mask,h); assert(count>0);
        int pick=(int)(swat_random(rng)%(uint32_t)count);
        for(int i=0;i<sizes[h];i++) if(legal(mask,h,i) && pick--==0) { action[h]=(float)i; break; }
    }
}
static void place(SwatSim* s,int actor,float x,float z,float yaw,float pitch) {
    SwatController* c=&s->actors[actor].controller;
    b3Body_SetTransform(c->body.body,(b3Pos){x,c->body.totalHeight*0.5f+0.02f,z},b3Quat_identity);
    c->yaw=yaw; c->pitch=pitch;
}

static void shapes(void) {
    int logits=0; for(int h=0;h<NUM_ATNS;h++) logits+=sizes[h];
    assert((int)(sizeof(sizes)/sizeof(sizes[0]))==NUM_ATNS && logits==SWAT_ROLES_LOGITS);
    assert(OBS_SIZE==144 && NUM_ATNS==32 && SWAT_ROLES_RAYS==9);
    Dict cfg=annex_config(1800,0); Fixture* f=setup(42,&cfg);
    Env* e=&f->env;
    // Default roster: two officers (actors 0,3), the suspect, the civilian.
    // Four is already a power of two, so roles_agents=0 adds no padding.
    assert(e->num_agents==4 && e->roster==4);
    assert(e->actor[0]==0 && e->actor[1]==3 && e->actor[2]==1 && e->actor[3]==2);
    assert(e->role[0]==SWAT_OFFICER && e->role[1]==SWAT_OFFICER && e->role[2]==SWAT_SUSPECT && e->role[3]==SWAT_CIVILIAN);
    for(int n=0;n<e->num_agents;n++) {
        for(int o=0;o<OBS_SIZE;o++) assert(isfinite(f->obs[n][o]));
        assert(f->obs[n][14]==1);
        assert(f->obs[n][15]==(e->role[n]==SWAT_OFFICER) && f->obs[n][16]==(e->role[n]==SWAT_SUSPECT) &&
               f->obs[n][17]==(e->role[n]==SWAT_CIVILIAN));
        for(int k=0;k<SWAT_GAZE_RAYS;k++) for(int h=0;h<2;h++) {
            float v=f->obs[n][SWAT_ROLES_SELF+(1+k)*SWAT_ROLES_RAY_CHANNELS+h]; assert(v>=-1 && v<=1);
        }
    }
    // Hidden mission telemetry is officers only; suspects get no objective by default.
    assert(f->obs[0][42]==.25f && f->obs[2][42]==0 && f->obs[3][42]==0);
    assert(f->obs[2][38]==0 && f->obs[2][39]==0 && f->obs[3][38]!=0);
    // Role legal actions.
    assert(legal_count(f->masks[0],SWAT_ROLE_HEAD_COMPLY)==1 && legal_count(f->masks[0],SWAT_ROLE_HEAD_COMMAND)==2);
    assert(legal_count(f->masks[2],SWAT_ROLE_HEAD_COMMAND)==1 && legal_count(f->masks[2],SWAT_ROLE_HEAD_TRIGGER)==2);
    assert(legal_count(f->masks[2],SWAT_ROLE_HEAD_COMPLY)==2);
    static const int unarmed[]={SWAT_ROLE_HEAD_AIM,SWAT_ROLE_HEAD_TRIGGER,SWAT_ROLE_HEAD_RELOAD,
        SWAT_ROLE_HEAD_WEAPON,SWAT_ROLE_HEAD_SELECTOR,SWAT_ROLE_HEAD_COMMAND};
    for(int i=0;i<6;i++) assert(legal_count(f->masks[3],unarmed[i])==1 && legal(f->masks[3],unarmed[i],0));
    for(int h=SWAT_ROLE_HEAD_GAZE;h<NUM_ATNS;h++) for(int n=0;n<e->num_agents;n++) assert(legal_count(f->masks[n],h)==sizes[h]);
    // The decoder enforces the same rules without a mask.
    float action[NUM_ATNS]={0}; for(int h=0;h<NUM_ATNS;h++) action[h]=(float)(sizes[h]-1);
    bool comply=false;
    SwatInput in=swat_roles_decode(SWAT_CIVILIAN,action,&comply);
    assert(!in.fire && !in.aim && !in.reload && !in.selector && in.weapon==0 && !in.command && comply);
    in=swat_roles_decode(SWAT_SUSPECT,action,&comply); assert(in.fire && !in.command && comply);
    in=swat_roles_decode(SWAT_OFFICER,action,&comply); assert(in.fire && in.command && !comply);
    action[0]=NAN; action[SWAT_ROLE_HEAD_GAZE]=1e9f; assert(swat_roles_bin(action,0,5)==0 && swat_roles_bin(action,SWAT_ROLE_HEAD_GAZE,9)==8);
    teardown(f); dict_clear(&cfg);
    puts("PASS roles shapes: obs=144, heads=32, logits=155, roster, role masks and decoder");
}

static void gaze(void) {
    Dict cfg=annex_config(1800,0); Fixture* f=setup(42,&cfg);
    neutral(f);
    // Ray 3 straight ahead must match the reticle ray exactly.
    f->actions[0][SWAT_ROLE_HEAD_GAZE+6]=4; f->actions[0][SWAT_ROLE_HEAD_GAZE+7]=2;
    place(f->env.sim,0,19,-2.5f,-SWAT_PI*0.5f,-0.13f);
    puf_step(&f->env); assert(f->terminals[0]==0);
    const float* reticle=f->obs[0]+SWAT_ROLES_SELF;
    const float* ahead=reticle+4*SWAT_ROLES_RAY_CHANNELS;
    assert(ahead[SWAT_RAY_YAW]==0 && ahead[SWAT_RAY_PITCH]==0);
    assert(!memcmp(reticle+SWAT_RAY_DEPTH,ahead+SWAT_RAY_DEPTH,(SWAT_ROLES_RAY_CHANNELS-SWAT_RAY_DEPTH)*sizeof(float)));
    // The officer faces the civilian at close range.
    assert(reticle[SWAT_RAY_CIVILIAN]==1 && reticle[SWAT_RAY_SUSPECT]==0 && reticle[SWAT_RAY_DEPTH]<.15f);
    assert(reticle[SWAT_RAY_COMPLIANT]==0 && reticle[SWAT_RAY_INTEGRITY]==1);
    // Directions are the chosen bins, reported back as normalized offsets.
    f->actions[0][SWAT_ROLE_HEAD_GAZE]=8; f->actions[0][SWAT_ROLE_HEAD_GAZE+1]=0;
    puf_step(&f->env);
    const float* first=f->obs[0]+SWAT_ROLES_SELF+SWAT_ROLES_RAY_CHANNELS;
    assert(fabsf(first[SWAT_RAY_YAW]-80.0f/90)<1e-6f && fabsf(first[SWAT_RAY_PITCH]+40.0f/45)<1e-6f);
    // One-hot semantics: at most one class per ray.
    for(int n=0;n<f->env.num_agents;n++) for(int r=0;r<SWAT_ROLES_RAYS;r++) {
        const float* ray=f->obs[n]+SWAT_ROLES_SELF+r*SWAT_ROLES_RAY_CHANNELS; float classes=0;
        for(int c=SWAT_RAY_SOLID;c<=SWAT_RAY_CIVILIAN;c++) { assert(ray[c]==0 || ray[c]==1); classes+=ray[c]; }
        assert(classes<=1 && ray[SWAT_RAY_DEPTH]>=0 && ray[SWAT_RAY_DEPTH]<=1);
    }
    teardown(f); dict_clear(&cfg);
    puts("PASS roles gaze: chosen directions, reticle equivalence, civilian identity, one-hot classes");
}

static void determinism(void) {
    Dict cfg=annex_config(64,1);
    Fixture* pair[2]={setup(731,&cfg),setup(731,&cfg)};
    uint32_t rng[2]={123,123}; int ends=0,agents=pair[0]->env.num_agents;
    for(int t=0;t<640;t++) {
        for(int i=0;i<2;i++) for(int n=0;n<agents;n++) sample(pair[i]->masks[n],&rng[i],pair[i]->actions[n]);
        for(int i=0;i<2;i++) {
            puf_step(&pair[i]->env);
            for(int n=0;n<agents;n++) {
                for(int o=0;o<OBS_SIZE;o++) assert(isfinite(pair[i]->obs[n][o]));
                assert(isfinite(pair[i]->rewards[n]));
            }
        }
        assert(!memcmp(pair[0]->obs,pair[1]->obs,sizeof(pair[0]->obs)));
        assert(!memcmp(pair[0]->masks,pair[1]->masks,sizeof(pair[0]->masks)));
        assert(!memcmp(pair[0]->rewards,pair[1]->rewards,sizeof(pair[0]->rewards)));
        assert(!memcmp(pair[0]->terminals,pair[1]->terminals,sizeof(pair[0]->terminals)));
        for(int n=1;n<agents;n++) assert(pair[0]->terminals[n]==pair[0]->terminals[0]);
        if(pair[0]->terminals[0]) { ends++; assert(pair[0]->env.sim->tick==0 && pair[0]->env.sim->end==SWAT_RUNNING); }
    }
    assert(ends>=40 && pair[0]->env.log.n==ends);
    for(int i=0;i<2;i++) teardown(pair[i]);
    dict_clear(&cfg);
    // Relocating Env (native vector realloc) must keep the heap Sim valid.
    cfg=annex_config(8,0); Fixture* f=setup(42,&cfg); neutral(f);
    Env moved=f->env; memset(&f->env,0,sizeof(f->env));
    assert(b3Body_GetUserData(moved.sim->actors[0].controller.body.body)==&moved.sim->actors[0].tag);
    puf_step(&moved); assert(f->terminals[0]==0 && moved.sim->tick==4);
    puf_step(&moved); assert(f->terminals[0]==1 && moved.log.n==1 && moved.log.episode_length==2 && moved.sim->tick==0);
    f->env=moved; teardown(f); dict_clear(&cfg);
    printf("PASS roles determinism: %d agents, 640 paired decisions, %d paired resets, relocation, exact timeout\n",agents,ends);
}

static void rewards(void) {
    // Officer shoots the civilian: annex civilian-harm failure, team ROE and
    // civilian-harm costs for both officers, damage cost for the civilian.
    Dict cfg=annex_config(1800,0); Fixture* f=setup(42,&cfg); neutral(f);
    SwatSim* s=f->env.sim;
    place(s,0,19,-2.5f,-SWAT_PI*0.5f,-0.13f); s->actors[0].controller.ads=1;
    f->actions[0][SWAT_ROLE_HEAD_AIM]=1; f->actions[0][SWAT_ROLE_HEAD_TRIGGER]=1;
    puf_step(&f->env);
    for(int n=0;n<4;n++) assert(f->terminals[n]==1);
    assert(f->rewards[0]<-5 && f->rewards[1]<-5 && f->rewards[0]<f->rewards[1]); // shooter also pays its shot
    assert(f->rewards[3]<f->env.reward.civilian_survive);
    assert(f->env.log.perf==0 && f->env.log.civilian_damage>0 && f->env.log.protected_damage>0);
    assert(s->tick==0 && s->actors[2].health==100);
    teardown(f); dict_clear(&cfg);

    // Cleared annex with both officers at extraction: success on the first tick.
    cfg=annex_config(1800,0); f=setup(42,&cfg); neutral(f); s=f->env.sim;
    swat_sim_damage_actor(s,1,0,1000);
    place(s,0,(float)s->extraction.x,(float)s->extraction.z-.5f,0,0);
    place(s,3,(float)s->extraction.x,(float)s->extraction.z+.5f,0,0);
    puf_step(&f->env);
    const SwatRoleRewards* w=&f->env.reward;
    assert(f->terminals[0]==1 && f->env.log.perf==1 && f->env.log.episode_length==1);
    assert(fabsf(f->rewards[0]-(w->officer_success-w->time_cost))<1e-5f);
    assert(fabsf(f->rewards[1]-(w->officer_success-w->time_cost))<1e-5f);
    assert(fabsf(f->rewards[2]+w->time_cost)<1e-6f);            // dead suspect: no terminal term
    assert(fabsf(f->rewards[3]-(w->civilian_survive-w->time_cost))<1e-5f);
    teardown(f); dict_clear(&cfg);

    // Voluntary surrender: irreversible, motor heads then fixed to neutral.
    cfg=annex_config(1800,0); f=setup(42,&cfg); neutral(f);
    f->actions[2][SWAT_ROLE_HEAD_COMPLY]=1; f->actions[3][SWAT_ROLE_HEAD_COMPLY]=1;
    puf_step(&f->env);
    assert(f->env.sim->actors[1].gear.surrendered && f->env.sim->actors[2].gear.surrendered);
    assert(f->obs[2][18]==1 && f->obs[3][18]==1);
    for(int h=0;h<SWAT_ROLES_MOTOR_HEADS;h++) assert(legal_count(f->masks[2],h)==1 && legal(f->masks[2],h,swat_roles_neutral[h]));
    assert(fabsf(f->rewards[3]-(f->env.reward.civilian_comply-4*f->env.reward.time_cost))<1e-5f);
    teardown(f); dict_clear(&cfg);

    // Optional escape: a free suspect reaching staging ends the episode.
    cfg=annex_config(1800,0); dict_set(&cfg,"suspect_escape",1); f=setup(42,&cfg); neutral(f); s=f->env.sim;
    assert(f->obs[2][41]>0); // suspects now observe the escape objective
    place(s,0,5,0,0,0);
    place(s,1,(float)s->mission.staging.x,(float)s->mission.staging.z,SWAT_PI,0);
    puf_step(&f->env);
    w=&f->env.reward;
    assert(f->terminals[0]==1 && f->env.log.escapes==1 && f->env.log.perf==0);
    assert(f->rewards[0]<-w->officer_failure+.01f && fabsf(f->rewards[2]-(w->suspect_escape-w->time_cost))<1e-5f);
    teardown(f); dict_clear(&cfg);
    puts("PASS roles rewards: civilian harm and ROE, success, voluntary surrender, escape");
}

// Inactive padding: zero observations, one legal value per head, zero reward,
// and the shared terminal.
static void check_padding(const Fixture* f) {
    const Env* e=&f->env;
    for(int n=e->roster;n<e->num_agents;n++) {
        for(int o=0;o<OBS_SIZE;o++) assert(f->obs[n][o]==0);
        for(int h=0;h<NUM_ATNS;h++) assert(legal_count(f->masks[n],h)==1);
        for(int h=0;h<SWAT_ROLES_MOTOR_HEADS;h++) assert(legal(f->masks[n],h,swat_roles_neutral[h]));
        assert(f->rewards[n]==0 && f->terminals[n]==f->terminals[0]);
    }
}

// Killing a surrendered or restrained suspect is never a neutralisation: no
// officer_neutralize, and the killing damage is charged as protected force.
static void protected_kills(void) {
    for(int state=0;state<3;state++) {
        Dict cfg=annex_config(1800,0); Fixture* f=setup(42,&cfg); neutral(f);
        Env* e=&f->env; SwatSim* s=e->sim; const SwatRoleRewards* w=&e->reward;
        if(state==1) s->actors[1].gear.surrendered=true;
        if(state==2) s->actors[1].gear.restrained=true;
        float health=s->actors[1].health;
        SwatRolesBefore before; swat_roles_snapshot(s,&before);
        memset(s->force,0,sizeof(s->force));
        swat_sim_damage_actor(s,1,0,1000);
        assert(!s->actors[1].alive && s->force[0].downed==1);
        assert(s->force[1].downed_protected==(state!=0));
        assert(state ? s->force[0].protected_damage==health && s->force[0].threat_damage==0 :
                       s->force[0].threat_damage==health && s->force[0].protected_damage==0);
        float rewards[SWAT_ROLES_MAX_AGENTS]={0};
        swat_roles_tick(e,&before,rewards);
        float officer=state ? -w->time_cost-w->officer_roe*health : -w->time_cost+w->officer_neutralize;
        assert(fabsf(rewards[0]-officer)<1e-4f && fabsf(rewards[1]-officer)<1e-4f);
        assert(fabsf(rewards[2]-(-w->time_cost-w->suspect_damage*health-w->suspect_down))<1e-4f);
        assert(e->protected_damage==(state ? health : 0));
        teardown(f); dict_clear(&cfg);
    }
    // The ledger also marks civilians, who never count as suspects down.
    Dict cfg=annex_config(1800,0); Fixture* f=setup(42,&cfg); neutral(f);
    SwatSim* s=f->env.sim; memset(s->force,0,sizeof(s->force));
    swat_sim_damage_actor(s,2,1,1000);
    assert(s->force[2].downed_protected && s->force[1].protected_damage>0);
    teardown(f); dict_clear(&cfg);
    puts("PASS roles protected kills: surrendered/restrained deaths pay ROE, never officer_neutralize");
}

// roles_agents pads the env with inactive agents without changing anything
// the real agents see: a padded env pairs exactly with an unpadded one.
static void padding(void) {
    Dict plain=annex_config(24,1),padded=annex_config(24,1);
    dict_set(&padded,"roles_agents",8);
    Fixture* a=setup(731,&plain); Fixture* b=setup(731,&padded);
    assert(a->env.num_agents==4 && b->env.num_agents==8 && b->env.roster==4);
    for(int n=4;n<8;n++) assert(b->env.actor[n]==-1);
    check_padding(b);
    uint32_t rng[2]={77,77}; int ends=0;
    for(int t=0;t<40;t++) {
        for(int n=0;n<4;n++) { sample(a->masks[n],&rng[0],a->actions[n]); sample(b->masks[n],&rng[1],b->actions[n]); }
        // Whatever the trainer writes into padding actions must be ignored.
        for(int n=4;n<8;n++) for(int h=0;h<NUM_ATNS;h++) b->actions[n][h]=(float)(sizes[h]-1);
        puf_step(&a->env); puf_step(&b->env);
        assert(!memcmp(a->obs,b->obs,4*sizeof(a->obs[0])) && !memcmp(a->masks,b->masks,4*sizeof(a->masks[0])));
        assert(!memcmp(a->rewards,b->rewards,4*sizeof(float)) && !memcmp(a->terminals,b->terminals,4*sizeof(float)));
        check_padding(b);
        ends+=b->terminals[0]!=0;
    }
    assert(ends>=1 && a->env.log.n==b->env.log.n && a->env.log.episode_return==b->env.log.episode_return);
    teardown(a); teardown(b); dict_clear(&plain); dict_clear(&padded);
    // Auto padding rounds a 3-agent roster up to 4.
    Dict three=annex_config(24,0); dict_set(&three,"roles_officers",1);
    Fixture* f=setup(5,&three);
    assert(f->env.roster==3 && f->env.num_agents==4); check_padding(f);
    teardown(f); dict_clear(&three);
    puts("PASS roles padding: inactive agents masked, zero obs/reward, paired exactly with unpadded env");
}

static void motel(void) {
    Dict cfg={0}; dict_set(&cfg,"max_ticks",40); dict_set_str(&cfg,"roles_mission","motel");
    Fixture* f=setup(5,&cfg);
    // Standard occupancy: two officers, suspects 1 and 8, civilians 2, 6 and 7,
    // padded with one inactive agent so 8 divides vec.total_agents=8192.
    assert(f->env.roster==7 && f->env.num_agents==8);
    assert(f->env.sim->config.tactical_rules && !f->env.sim->config.squad_bots);
    check_padding(f);
    static const int actors[]={0,3,1,8,2,6,7};
    for(int n=0;n<7;n++) assert(f->env.actor[n]==actors[n]);
    uint32_t rng=9; int ends=0;
    for(int t=0;t<12;t++) {
        for(int n=0;n<8;n++) sample(f->masks[n],&rng,f->actions[n]);
        puf_step(&f->env); ends+=f->terminals[0]!=0;
        for(int n=0;n<8;n++) { for(int o=0;o<OBS_SIZE;o++) assert(isfinite(f->obs[n][o])); assert(isfinite(f->rewards[n])); }
        check_padding(f);
    }
    assert(ends>=1);
    teardown(f); dict_clear(&cfg);
    puts("PASS roles motel: 7-agent roster padded to 8, tactical rules, no squad bots, timeout reset");
}

// A scripted agent's slot looks exactly like padding to the trainer.
static void check_scripted(const Fixture* f) {
    const Env* e=&f->env;
    for(int n=0;n<e->roster;n++) {
        if(!e->scripted[n]) continue;
        for(int o=0;o<OBS_SIZE;o++) assert(f->obs[n][o]==0);
        for(int h=0;h<NUM_ATNS;h++) assert(legal_count(f->masks[n],h)==1);
        for(int h=0;h<SWAT_ROLES_MOTOR_HEADS;h++) assert(legal(f->masks[n],h,swat_roles_neutral[h]));
        assert(f->rewards[n]==0 && f->terminals[n]==f->terminals[0]);
    }
}
static Dict scripted_config(int max_ticks,int hostile_fire,const char* roles) {
    Dict cfg=annex_config(max_ticks,hostile_fire);
    dict_set_str(&cfg,"roles_scripted",roles);
    return cfg;
}

// roles_scripted selects roles; the roster and agent count do not change.
static void scripted_roster(void) {
    static const struct { const char* roles; bool scripted[4]; } cases[]={
        {"none",{0,0,0,0}},{"suspects",{0,0,1,0}},{"civilians",{0,0,0,1}},
        {"suspects+civilians",{0,0,1,1}},{"officers, civilians",{1,1,0,1}},{"all",{1,1,1,1}},
    };
    for(int c=0;c<6;c++) {
        Dict cfg=scripted_config(24,1,cases[c].roles); Fixture* f=setup(731,&cfg);
        Env* e=&f->env; int learned=0;
        assert(e->roster==4 && e->num_agents==4 && e->scripted_episode==(c!=0));
        for(int n=0;n<4;n++) { assert(e->scripted[n]==cases[c].scripted[n]); learned+=!cases[c].scripted[n]; }
        assert(e->learned==learned);
        check_scripted(f);
        uint32_t rng=11; int ends=0;
        for(int t=0;t<30;t++) {
            for(int n=0;n<4;n++) sample(f->masks[n],&rng,f->actions[n]);
            // Whatever the trainer writes for a scripted agent is ignored.
            for(int n=0;n<4;n++) if(e->scripted[n]) for(int h=0;h<NUM_ATNS;h++) f->actions[n][h]=(float)(sizes[h]-1);
            puf_step(e); ends+=f->terminals[0]!=0;
            check_scripted(f);
            for(int n=0;n<4;n++) assert(isfinite(f->rewards[n]));
        }
        assert(ends>=1 && e->log.scripted_episodes==(c ? e->log.n : 0));
        teardown(f); dict_clear(&cfg);
    }
    // roles_scripted=none is exactly the unscripted env.
    Dict plain=annex_config(24,1),none=scripted_config(24,1,"none");
    Fixture* a=setup(731,&plain); Fixture* b=setup(731,&none);
    uint32_t rng[2]={5,5};
    for(int t=0;t<40;t++) {
        for(int n=0;n<4;n++) { sample(a->masks[n],&rng[0],a->actions[n]); sample(b->masks[n],&rng[1],b->actions[n]); }
        puf_step(&a->env); puf_step(&b->env);
        assert(!memcmp(a->obs,b->obs,sizeof(a->obs)) && !memcmp(a->masks,b->masks,sizeof(a->masks)));
        assert(!memcmp(a->rewards,b->rewards,sizeof(a->rewards)) && !memcmp(a->terminals,b->terminals,sizeof(a->terminals)));
    }
    assert(!memcmp(&a->env.log,&b->env.log,sizeof(Log)));
    teardown(a); teardown(b); dict_clear(&plain); dict_clear(&none);
    // roles_scripted_rate picks scripted episodes from the env seed alone.
    for(int k=0;k<3;k++) {
        static const float rates[]={0,.5f,1};
        Dict cfg=scripted_config(24,0,"suspects"); dict_set(&cfg,"roles_scripted_rate",rates[k]);
        Fixture* f=setup(731,&cfg); int count=0;
        for(int i=0;i<100;i++) {
            puf_reset(&f->env); count+=f->env.scripted_episode;
            assert(f->env.scripted[2]==f->env.scripted_episode && !f->env.scripted[0] && !f->env.scripted[3]);
            check_scripted(f);
        }
        assert(k==0 ? count==0 : k==2 ? count==100 : count>25 && count<75);
        teardown(f); dict_clear(&cfg);
    }
    puts("PASS roles scripted roster: role selection, trainer sees padding, none unchanged, per-episode rate");
}

// Every baseline action the env runs is legal under that agent's role mask,
// and is the action computed from that agent's observation alone. Recorded
// (observation, seed, action) triples replay exactly with no simulation.
typedef struct ScriptedRecord { float obs[OBS_SIZE]; uint32_t seed; float action[NUM_ATNS]; } ScriptedRecord;
static int scripted_run(Dict* cfg,unsigned int seed,int decisions,ScriptedRecord* records,int capacity) {
    Fixture* f=setup(seed,cfg); Env* e=&f->env; int count=0,ends=0;
    for(int t=0;t<decisions;t++) {
        float action[SWAT_ROLES_MAX_AGENTS][NUM_ATNS];
        for(int n=0;n<e->roster;n++) {
            assert(e->scripted[n]);
            float obs[OBS_SIZE]; unsigned char mask[SWAT_ROLES_LOGITS];
            swat_roles_observe(e,n,obs); swat_roles_mask(e,n,mask);
            swat_baseline_act(obs,e->script_seed[n],action[n]);
            for(int h=0;h<NUM_ATNS;h++) {
                int i=(int)action[n][h];
                assert((float)i==action[n][h] && i>=0 && i<sizes[h] && legal(mask,h,i));
            }
            if(count<capacity) {
                memcpy(records[count].obs,obs,sizeof(obs)); records[count].seed=e->script_seed[n];
                memcpy(records[count].action,action[n],sizeof(action[n])); count++;
            }
        }
        puf_step(e); ends+=f->terminals[0]!=0;
        check_scripted(f);
        // The env applied the same decision: its gaze state is the baseline's.
        if(!f->terminals[0]) for(int n=0;n<e->roster;n++) for(int k=0;k<SWAT_GAZE_RAYS;k++) {
            assert(e->gaze[n][k][0]==(int)action[n][SWAT_ROLE_HEAD_GAZE+2*k]);
            assert(e->gaze[n][k][1]==(int)action[n][SWAT_ROLE_HEAD_GAZE+2*k+1]);
        }
    }
    assert(e->log.n==ends && e->log.scripted_episodes==ends);
    teardown(f);
    return count;
}
static void scripted_baselines(void) {
    enum { CAPACITY=2400 };
    ScriptedRecord* records=calloc(CAPACITY,sizeof(ScriptedRecord)); assert(records);
    Dict cfg=scripted_config(240,1,"all");
    int count=scripted_run(&cfg,731,600,records,CAPACITY);
    // The simulation is gone; the baselines need nothing but the record.
    for(int i=0;i<count;i++) {
        float action[NUM_ATNS]; swat_baseline_act(records[i].obs,records[i].seed,action);
        assert(!memcmp(action,records[i].action,sizeof(action)));
    }
    // Paired determinism: two all-scripted envs on one seed stay identical.
    Fixture* pair[2]={setup(99,&cfg),setup(99,&cfg)};
    for(int t=0;t<400;t++) {
        for(int i=0;i<2;i++) puf_step(&pair[i]->env);
        for(int n=0;n<pair[0]->env.roster;n++) {
            float a[OBS_SIZE],b[OBS_SIZE];
            swat_roles_observe(&pair[0]->env,n,a); swat_roles_observe(&pair[1]->env,n,b);
            assert(!memcmp(a,b,sizeof(a)));
        }
        assert(!memcmp(pair[0]->terminals,pair[1]->terminals,sizeof(pair[0]->terminals)));
        assert(!memcmp(&pair[0]->env.log,&pair[1]->env.log,sizeof(Log)));
    }
    assert(pair[0]->env.log.n>=1);
    for(int i=0;i<2;i++) teardown(pair[i]);
    dict_clear(&cfg);
    free(records);
    printf("PASS roles scripted baselines: %d legal mask-checked decisions replayed without the sim, paired determinism\n",count);
}

// The eval statistic: officer success rate in episodes against the baselines.
static void scripted_eval(void) {
    Dict cfg=scripted_config(1800,0,"suspects"); Fixture* f=setup(42,&cfg); neutral(f);
    Env* e=&f->env; SwatSim* s=e->sim;
    // Success: the scripted suspect is down, both officers at extraction.
    swat_sim_damage_actor(s,1,0,1000);
    place(s,0,(float)s->extraction.x,(float)s->extraction.z-.5f,0,0);
    place(s,3,(float)s->extraction.x,(float)s->extraction.z+.5f,0,0);
    puf_step(e);
    assert(f->terminals[0]==1 && e->log.perf==1 && e->log.scripted_episodes==1 && e->log.scripted_wins==1);
    assert(f->rewards[2]==0 && e->log.suspect_return==0); // scripted: not trained, not logged
    // Failure: a learned officer shoots the civilian.
    neutral(f); s=e->sim; assert(e->scripted_episode && e->scripted[2]);
    place(s,0,19,-2.5f,-SWAT_PI*0.5f,-0.13f); s->actors[0].controller.ads=1;
    f->actions[0][SWAT_ROLE_HEAD_AIM]=1; f->actions[0][SWAT_ROLE_HEAD_TRIGGER]=1;
    puf_step(e);
    assert(f->terminals[0]==1 && e->log.n==2 && e->log.perf==1 && e->log.scripted_episodes==2 && e->log.scripted_wins==1);
    Dict out={0}; puf_log(&e->log,&out);
    DictItem* rate=dict_find(&out,"scripted_perf"); DictItem* share=dict_find(&out,"scripted_episodes");
    assert(rate && fabs(rate->value-.5)<1e-6 && share && share->value==2);
    dict_clear(&out); teardown(f); dict_clear(&cfg);
    // No scripted episodes: the rate reads 0, not NaN.
    cfg=annex_config(8,0); f=setup(42,&cfg); neutral(f);
    puf_step(&f->env); puf_step(&f->env); assert(f->env.log.n==1);
    out=(Dict){0}; puf_log(&f->env.log,&out);
    rate=dict_find(&out,"scripted_perf"); assert(rate && rate->value==0);
    dict_clear(&out); teardown(f); dict_clear(&cfg);
    puts("PASS roles scripted eval: scripted_episodes and scripted_perf count only baseline episodes");
}

static void motel_scripted(void) {
    Dict cfg={0}; dict_set(&cfg,"max_ticks",400); dict_set_str(&cfg,"roles_mission","motel");
    dict_set_str(&cfg,"roles_scripted","all");
    enum { CAPACITY=8*40 };
    ScriptedRecord* records=calloc(CAPACITY,sizeof(ScriptedRecord)); assert(records);
    int count=scripted_run(&cfg,5,40,records,CAPACITY);
    for(int i=0;i<count;i++) {
        float action[NUM_ATNS]; swat_baseline_act(records[i].obs,records[i].seed,action);
        assert(!memcmp(action,records[i].action,sizeof(action)));
    }
    free(records); dict_clear(&cfg);
    puts("PASS roles motel scripted: 7 scripted actors, legal actions, replay without the sim");
}

int main(int argc,char** argv) {
    shapes();
    gaze();
    determinism();
    rewards();
    protected_kills();
    padding();
    scripted_roster();
    scripted_baselines();
    scripted_eval();
    if(argc>1 && !strcmp(argv[1],"motel")) { motel(); motel_scripted(); }
    return 0;
}
