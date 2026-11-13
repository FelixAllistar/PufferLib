// Held-out evaluation suite checks (roles_eval.h, roles_split.h,
// roles_curriculum.h): disjoint training/evaluation seed streams in the
// streams and in the env, training configs that cannot reach held-out
// layouts, an information-leak audit of the deployed actor observation,
// curriculum validation, episode metrics, confidence intervals and suite
// determinism. argv[1] is the prefix of the smoke JSON/Markdown report.
#define SWAT_ROLES_TRAINING
#define SWAT_ROLES_NO_RENDER
#include "../swat.h"
#include "../roles_eval.h"
#include <assert.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct Fixture {
    Env env;
    float obs[SWAT_ROLES_MAX_AGENTS][OBS_SIZE],actions[SWAT_ROLES_MAX_AGENTS][NUM_ATNS];
    float rewards[SWAT_ROLES_MAX_AGENTS],terminals[SWAT_ROLES_MAX_AGENTS];
    unsigned char masks[SWAT_ROLES_MAX_AGENTS][SWAT_ROLES_LOGITS];
} Fixture;
static const int sizes[]=ACT_SIZES;

static Fixture* setup(unsigned int seed,Dict* cfg,SwatRolesSplit split,uint32_t eval_case) {
    Fixture* f=calloc(1,sizeof(Fixture)); assert(f);
    f->env.rng=seed;
    swat_roles_init(&f->env,cfg,split);
    for(int i=0;i<f->env.num_agents;i++) {
        f->env.agents[i].observations=f->obs[i]; f->env.agents[i].actions=f->actions[i];
        f->env.agents[i].rewards=&f->rewards[i]; f->env.agents[i].terminals=&f->terminals[i];
        f->env.agents[i].action_mask=f->masks[i];
    }
    f->env.eval_suite=SWAT_ROLES_EVAL_SEEDS; f->env.eval_case=eval_case;
    puf_reset(&f->env);
    return f;
}
static void teardown(Fixture* f) { puf_close(&f->env); free(f); }
static void neutral(Fixture* f) {
    for(int n=0;n<f->env.num_agents;n++) {
        for(int h=0;h<SWAT_ROLES_MOTOR_HEADS;h++) f->actions[n][h]=(float)swat_roles_neutral[h];
        for(int k=0;k<SWAT_GAZE_RAYS;k++) {
            f->actions[n][SWAT_ROLE_HEAD_GAZE+2*k]=(float)(k*SWAT_GAZE_YAW_BINS/SWAT_GAZE_RAYS);
            f->actions[n][SWAT_ROLE_HEAD_GAZE+2*k+1]=SWAT_GAZE_PITCH_BINS/2;
        }
    }
}
static void sample(const unsigned char* mask,uint32_t* rng,float* action) {
    int offset=0;
    for(int h=0;h<NUM_ATNS;h++) {
        int legal=0; for(int i=0;i<sizes[h];i++) legal+=mask[offset+i]!=0;
        assert(legal>0);
        int pick=(int)(swat_random(rng)%(uint32_t)legal);
        for(int i=0;i<sizes[h];i++) if(mask[offset+i] && pick--==0) { action[h]=(float)i; break; }
        offset+=sizes[h];
    }
}
static void place(SwatSim* s,int actor,float x,float z,float yaw,float pitch) {
    SwatController* c=&s->actors[actor].controller;
    b3Body_SetTransform(c->body.body,(b3Pos){x,c->body.totalHeight*0.5f+0.02f,z},b3Quat_identity);
    c->yaw=yaw; c->pitch=pitch;
}
// Runs `body` in a child process and returns its exit status, so config
// rejections (which exit like the trainer's own config errors) can be tested.
static int exit_status(void (*body)(void)) {
    fflush(stdout); fflush(stderr);
    pid_t pid=fork(); assert(pid>=0);
    if(!pid) { if(!freopen("/dev/null","w",stderr)) _exit(3); body(); _exit(0); }
    int status=0; waitpid(pid,&status,0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// --- Seed and layout split ------------------------------------------------

static void train_busy(void) {
    Dict cfg={0}; dict_set_str(&cfg,"roles_mission","motel"); dict_set(&cfg,"motel_occupancy",SWAT_ROLES_EVAL_OCCUPANCY);
    Env* env=calloc(1,sizeof(Env)); puf_init(env,&cfg);
}
static void train_eval_layout(void) {
    Dict cfg={0}; dict_set(&cfg,"layout_seed",(double)SWAT_ROLES_EVAL_BIT);
    Env* env=calloc(1,sizeof(Env)); puf_init(env,&cfg);
}
static void eval_stage(void) {
    Dict cfg={0}; dict_set(&cfg,"roles_stage",0); dict_set_str(&cfg,"roles_curriculum","roles_curriculum.ini");
    Env* env=calloc(1,sizeof(Env)); swat_roles_init(env,&cfg,SWAT_ROLES_EVAL);
}
static void train_busy_stage(void) {
    // A curriculum file that tries to train on the held-out occupancy.
    FILE* f=fopen("/tmp/swat_roles_bad_curriculum.ini","w"); assert(f);
    fputs("[busy]\nroles_mission = motel\nmotel_occupancy = 3\npromote_metric = perf\npromote_at = 0.5\n",f); fclose(f);
    Dict cfg={0}; dict_set(&cfg,"roles_stage",0); dict_set_str(&cfg,"roles_curriculum","/tmp/swat_roles_bad_curriculum.ini");
    Env* env=calloc(1,sizeof(Env)); puf_init(env,&cfg);
}

static void split(void) {
    // Streams: training values never carry bit 31; every held-out value does.
    for(uint32_t env_seed=0;env_seed<2000;env_seed++) for(uint32_t episode=0;episode<500;episode++) {
        uint32_t seed=swat_roles_train_seed(env_seed*2654435761u,episode);
        assert(!swat_roles_eval_value(seed) && (seed&1u));
        assert(!swat_roles_eval_value(swat_roles_train_layout(seed,1,SWAT_ROLES_MAX_LAYOUTS)));
    }
    assert(!swat_roles_eval_value(swat_roles_train_layout(5,SWAT_ROLES_EVAL_BIT-1,0)));
    // The whole suite: held-out values only, and every episode a distinct situation.
    int total=0; for(int c=0;c<SWAT_ROLES_EVAL_CASES;c++) total+=swat_roles_eval_cases[c].episodes;
    uint32_t* seeds=calloc((size_t)total,sizeof(uint32_t)); int k=0;
    for(int c=0;c<SWAT_ROLES_EVAL_CASES;c++) for(int e=0;e<swat_roles_eval_cases[c].episodes;e++) {
        uint32_t seed=swat_roles_eval_seed(SWAT_ROLES_EVAL_SEEDS,(uint32_t)c,(uint32_t)e);
        assert(swat_roles_eval_value(seed) && (seed&1u));
        assert(swat_roles_eval_value(swat_roles_eval_layout(SWAT_ROLES_EVAL_SEEDS,(uint32_t)c,(uint32_t)e)));
        for(int i=0;i<k;i++) assert(seeds[i]!=seed);
        seeds[k++]=seed;
    }
    free(seeds);
    // Suite v2 changes metrics only: it plays suite v1's episodes (the first
    // annex episode of the v1 report in benchmarks/roles-eval-v1-baselines.json).
    assert(SWAT_ROLES_EVAL_SUITE==2 && SWAT_ROLES_EVAL_SEEDS==1);
    assert(swat_roles_eval_seed(SWAT_ROLES_EVAL_SEEDS,0,0)==3408825361u && swat_roles_eval_layout(SWAT_ROLES_EVAL_SEEDS,0,0)==2343128055u);
    // The training env really runs the training stream, with its fixed
    // layout or its per-episode pool.
    Dict cfg={0}; dict_set(&cfg,"max_ticks",8); dict_set(&cfg,"seed",77);
    Fixture* f=setup(9,&cfg,SWAT_ROLES_TRAIN,0);
    for(int i=1;i<=40;i++) {
        if(i>1) puf_reset(&f->env);
        assert(f->env.sim->reset_seed==swat_roles_train_seed(f->env.rng,(uint32_t)i));
        assert(!swat_roles_eval_value(f->env.sim->reset_seed) && f->env.sim->config.layout_seed==1);
        assert(f->env.current.seed==f->env.sim->reset_seed);
    }
    teardown(f);
    dict_set(&cfg,"roles_layouts",8); f=setup(9,&cfg,SWAT_ROLES_TRAIN,0);
    unsigned seen=0;
    for(int i=0;i<200;i++) {
        puf_reset(&f->env); uint32_t layout=f->env.sim->config.layout_seed;
        assert(layout>=1 && layout<=8); seen|=1u<<layout;
    }
    assert(seen==0x1feu);
    teardown(f); dict_clear(&cfg);
    // The evaluation env runs the held-out stream of its case.
    cfg=(Dict){0}; dict_set(&cfg,"max_ticks",8);
    f=setup(9,&cfg,SWAT_ROLES_EVAL,1); f->env.episodes=0; puf_reset(&f->env);
    for(int i=0;i<20;i++) {
        if(i) puf_reset(&f->env);
        assert(f->env.sim->reset_seed==swat_roles_eval_seed(SWAT_ROLES_EVAL_SEEDS,1,(uint32_t)i));
        assert(f->env.sim->config.layout_seed==swat_roles_eval_layout(SWAT_ROLES_EVAL_SEEDS,1,(uint32_t)i));
    }
    teardown(f); dict_clear(&cfg);
    // Training configs cannot select held-out layouts or run eval stages.
    assert(exit_status(train_busy)==1);
    assert(exit_status(train_eval_layout)==1);
    assert(exit_status(eval_stage)==1);
    assert(exit_status(train_busy_stage)==1);
    printf("PASS roles eval split: 1,000,000 training seeds clear of the held-out stream, %d distinct held-out episodes, env streams, held-out layouts rejected in training\n",total);
}

// --- Information-leak audit -----------------------------------------------

// Whether any of agent n's 9 observation rays (physical or glass sight ray)
// reaches actor `target`: a target outside every ray is unperceived.
static bool perceives(const Env* env,int n,int target) {
    const SwatSim* s=env->sim; const SwatActor* a=&s->actors[env->actor[n]];
    if(!a->alive) return false;
    const SwatController* c=&a->controller;
    b3Vec3 forward,right,up; swat_controller_view(c,&forward,&right,&up);
    b3Pos eye=swat_controller_eye(c);
    for(int r=0;r<SWAT_ROLES_RAYS;r++) {
        b3Vec3 dir=forward;
        if(r) {
            float yaw=swat_gaze_yaw_degrees[env->gaze[n][r-1][0]],pitch=swat_gaze_pitch_degrees[env->gaze[n][r-1][1]];
            b3CosSin y=swat_sincos(yaw*SWAT_RAD),p=swat_sincos(pitch*SWAT_RAD);
            dir=swat_normalize(swat_add(swat_mul(forward,p.cosine*y.cosine),swat_add(swat_mul(right,p.cosine*y.sine),swat_mul(up,p.sine))));
        }
        SwatHit hit=swat_world_ray(&s->world,eye,dir,SWAT_ROLES_RANGE,c->body.body);
        SwatHit seen=swat_world_sight_ray(&s->world,eye,dir,SWAT_ROLES_RANGE,c->body.body);
        if((hit.kind==SWAT_HIT_ACTOR && hit.index==target) || (seen.kind==SWAT_HIT_ACTOR && seen.index==target)) return true;
    }
    return false;
}
typedef struct Snapshot { float obs[SWAT_ROLES_MAX_AGENTS][OBS_SIZE]; unsigned char mask[SWAT_ROLES_MAX_AGENTS][SWAT_ROLES_LOGITS]; } Snapshot;
static void snapshot(const Env* env,Snapshot* out) {
    for(int n=0;n<env->roster;n++) { swat_roles_observe(env,n,out->obs[n]); swat_roles_mask(env,n,out->mask[n]); }
}
// Every agent that does not perceive `target` (all agents when target<0)
// observes and may act exactly as before. Returns how many were compared.
static int unchanged(const Env* env,const Snapshot* before,int target,const char* what) {
    Snapshot after; snapshot(env,&after); int compared=0;
    for(int n=0;n<env->roster;n++) {
        if(target>=0 && (env->actor[n]==target || perceives(env,n,target))) continue;
        if(memcmp(before->obs[n],after.obs[n],sizeof(after.obs[n])) || memcmp(before->mask[n],after.mask[n],sizeof(after.mask[n]))) {
            for(int o=0;o<OBS_SIZE;o++) if(before->obs[n][o]!=after.obs[n][o])
                fprintf(stderr,"leak: %s changes agent %d obs[%d] %g -> %g\n",what,n,o,before->obs[n][o],after.obs[n][o]);
            assert(!"hidden state reached an actor observation");
        }
        compared++;
    }
    return compared;
}

// Hidden bookkeeping no agent may observe: the force ledger, ROE records
// (including the perceived ROE state and lethal engagements), the annex team
// sightings, debrief, event totals, reward accounting and the evaluation
// record.
static void scramble_bookkeeping(Env* env,uint32_t* rng) {
    SwatSim* s=env->sim;
    for(int i=0;i<SWAT_MAX_ACTORS;i++) {
        SwatForce* f=&s->force[i];
        f->taken=(float)(swat_random(rng)%500); f->threat_damage=(float)(swat_random(rng)%500);
        f->officer_damage=(float)(swat_random(rng)%500); f->protected_damage=(float)(swat_random(rng)%500);
        f->downed=(int)(swat_random(rng)%3); f->downed_protected=swat_random(rng)&1;
        for(int j=0;j<SWAT_MAX_ACTORS;j++) {
            s->roe_hit_tick[i][j]=(int)(swat_random(rng)%1000);
            s->roe_lethal_tick[i][j]=(int)(swat_random(rng)%1000);
        }
        s->roe[i].state=(SwatRoeState)(swat_random(rng)%SWAT_ROE_STATES);
        s->roe[i].since=(int)(swat_random(rng)%1000); s->roe[i].seen_tick=(int)(swat_random(rng)%1000);
        s->roe[i].challenged_tick=(int)(swat_random(rng)%1000);
        env->roe_last[i]=(int)(swat_random(rng)%1000); env->protected_last[i]=(int)(swat_random(rng)%1000);
        s->sighted[i]=swat_random(rng)&1;
    }
    s->debrief.roe_violations=(int)(swat_random(rng)%50); s->debrief.unlawful_damage=(float)(swat_random(rng)%900);
    s->debrief.lethal_violations=(int)(swat_random(rng)%50); s->debrief.feints=(int)(swat_random(rng)%50);
    s->totals.civilian_damage=(float)(swat_random(rng)%900); s->totals.hostile_down=(int)(swat_random(rng)%9);
    s->events=s->totals;
    env->protected_damage=(float)(swat_random(rng)%900);
    env->current.roe_violations=(int)(swat_random(rng)%50); env->current.officer_rounds=(int)(swat_random(rng)%50);
    env->current.protected_violations=(int)(swat_random(rng)%50); env->current.lethal_violations=(int)(swat_random(rng)%50);
    for(int n=0;n<env->roster;n++) env->returns[n]=(float)(swat_random(rng)%100);
}
// Hidden properties of one unperceived person: identity, life, health,
// weapon state and mind. Its body (what rays hit) is left in place.
static void scramble_person(SwatSim* s,int i,uint32_t* rng,int variant) {
    SwatActor* a=&s->actors[i];
    switch(variant) {
    case 0: a->role=a->role==SWAT_SUSPECT ? SWAT_CIVILIAN : SWAT_SUSPECT; break;  // identity
    case 1: a->alive=false; a->health=0; break;                                   // killed out of sight
    case 2: a->health=(float)(1+swat_random(rng)%99); break;
    case 3: a->arsenal.slots[a->arsenal.active].magazine=0; a->arsenal.shots+=7; a->last_shot_tick=s->tick; break;
    case 4: a->mind.resolve=1-a->mind.resolve; a->target_actor=(int)(swat_random(rng)%4); a->visible_ticks+=90; break;
    case 5: a->gear.surrendered=!a->gear.surrendered; break;                       // hands up out of sight
    }
}
static int audit_world(Dict* cfg,const char* name,int decisions) {
    int compared=0;
    for(int variant=-1;variant<6;variant++) {
        // A fresh episode per perturbation: some are irreversible.
        Fixture* f=setup(31,cfg,SWAT_ROLES_TRAIN,0); Env* e=&f->env; neutral(f);
        for(int t=0;t<decisions;t++) puf_step(e);
        uint32_t rng=1234u+(uint32_t)variant; Snapshot before; snapshot(e,&before);
        if(variant<0) { scramble_bookkeeping(e,&rng); compared+=unchanged(e,&before,-1,"bookkeeping"); teardown(f); continue; }
        for(int i=0;i<e->sim->actor_count;i++) {
            const SwatActor* a=&e->sim->actors[i];
            if(!a->present || !a->alive || a->role==SWAT_OFFICER) continue;
            bool unseen=false; for(int n=0;n<e->roster;n++) unseen|=e->actor[n]!=i && !perceives(e,n,i);
            if(!unseen) continue;
            snapshot(e,&before);
            scramble_person(e->sim,i,&rng,variant);
            char what[64]; snprintf(what,sizeof(what),"%s actor %d variant %d",name,i,variant);
            compared+=unchanged(e,&before,i,what);
        }
        teardown(f);
    }
    return compared;
}
// The weapon ray channel (contract v4) of a person agent 0 sees on
// its reticle. Hidden state (mind, health, ammunition, ROE state, ledger,
// and a weapon concealed under a feigned surrender) must leave every other
// agent's observation unchanged; dropping the weapon in hand changes only
// weapon channels, and at least the reticle's. Returns the people audited.
static int same_except(const Env* env,const Snapshot* before,int person,const char* what) {
    Snapshot after; snapshot(env,&after); int compared=0;
    for(int n=0;n<env->roster;n++) {
        if(env->actor[n]==person) continue;
        if(memcmp(before->obs[n],after.obs[n],sizeof(after.obs[n])) || memcmp(before->mask[n],after.mask[n],sizeof(after.mask[n]))) {
            for(int o=0;o<OBS_SIZE;o++) if(before->obs[n][o]!=after.obs[n][o])
                fprintf(stderr,"leak: %s changes agent %d obs[%d] %g -> %g\n",what,n,o,before->obs[n][o],after.obs[n][o]);
            assert(!"hidden state reached the observation of a perceiving agent");
        }
        compared++;
    }
    return compared;
}
static int weapon_audit(Dict* cfg,const char* name,int* compared) {
    int people=0;
    Fixture* f=setup(31,cfg,SWAT_ROLES_TRAIN,0); Env* e=&f->env; neutral(f);
    for(int t=0;t<3;t++) puf_step(e);
    SwatSim* s=e->sim; const SwatController* c=&s->actors[e->actor[0]].controller;
    b3Vec3 forward,right,up; swat_controller_view(c,&forward,&right,&up);
    b3Pos eye=swat_controller_eye(c);
    const float* reticle=f->obs[0]+SWAT_ROLES_SELF;
    uint32_t rng=99;
    for(int i=0;i<s->actor_count;i++) {
        SwatActor* a=&s->actors[i];
        if(!a->present || !a->alive || a->role!=SWAT_SUSPECT) continue;
        // Stand the suspect 3 m ahead of agent 0, facing it.
        float x=(float)eye.x+forward.x*3,z=(float)eye.z+forward.z*3;
        place(s,i,x,z,c->yaw+SWAT_PI,0);
        Snapshot before; snapshot(e,&before);
        memcpy(f->obs[0],before.obs[0],sizeof(f->obs[0]));
        if(!perceives(e,0,i) || reticle[SWAT_RAY_SUSPECT]!=1) continue;
        assert(reticle[SWAT_RAY_WEAPON]==1 && reticle[SWAT_RAY_COMPLIANT]==0);
        char what[80]; snprintf(what,sizeof(what),"%s seen actor %d hidden state",name,i);
        a->mind.resolve=1-a->mind.resolve; a->target_actor=(int)(swat_random(&rng)%4); a->visible_ticks+=90;
        a->health=(float)(1+swat_random(&rng)%99);
        a->arsenal.slots[a->arsenal.active].magazine=0; a->arsenal.shots+=7; a->last_shot_tick=s->tick;
        scramble_bookkeeping(e,&rng);
        *compared+=same_except(e,&before,i,what);
        // A feigned surrender hides the weapon: hands up shows no weapon,
        // and whether one is still carried never shows.
        a->gear.surrendered=true; snapshot(e,&before);
        assert(before.obs[0][SWAT_ROLES_SELF+SWAT_RAY_WEAPON]==0 && before.obs[0][SWAT_ROLES_SELF+SWAT_RAY_COMPLIANT]==1);
        a->gear.disarmed=!a->gear.disarmed;
        snprintf(what,sizeof(what),"%s seen actor %d concealed weapon",name,i);
        *compared+=same_except(e,&before,i,what);
        // Dropping the weapon in hand is visible, in the weapon channel only.
        a->gear.surrendered=false; a->gear.disarmed=false; snapshot(e,&before);
        assert(before.obs[0][SWAT_ROLES_SELF+SWAT_RAY_WEAPON]==1);
        a->gear.disarmed=true;
        Snapshot after; snapshot(e,&after);
        for(int n=0;n<e->roster;n++) {
            if(e->actor[n]==i) continue;
            for(int o=0;o<OBS_SIZE;o++) if(before.obs[n][o]!=after.obs[n][o])
                assert(o>=SWAT_ROLES_SELF && (o-SWAT_ROLES_SELF)%SWAT_ROLES_RAY_CHANNELS==SWAT_RAY_WEAPON && after.obs[n][o]==0);
            assert(!memcmp(before.mask[n],after.mask[n],sizeof(after.mask[n])));
        }
        assert(after.obs[0][SWAT_ROLES_SELF+SWAT_RAY_WEAPON]==0);
        a->gear.disarmed=false; place(s,i,x+40,z+40,0,0); people++;
    }
    teardown(f);
    return people;
}

// Annex contract v4: obs 29 counts only suspects the team has seen at large
// and not secured. Changes to an unseen suspect leave the whole annex
// observation unchanged, although the v3 count (swat_sim_hostiles) would
// reveal the kill.
static void annex_obs_step(SwatSim* s) { SwatInput in=swat_neutral_input(); swat_sim_step(s,&in); }
static void annex_face(SwatSim* s,float yaw_offset) {
    b3Pos target=swat_body_feet_position(&s->actors[1].controller.body);
    place(s,0,(float)target.x-3,(float)target.z,yaw_offset,0);
}
static int annex_audit(void) {
    SwatConfig config=swat_default_config(); config.randomize=false; config.hostile_fire=false;
    SwatSim* s=calloc(1,sizeof(SwatSim)); assert(s);
    swat_sim_init(s,config,55);
    float before[SWAT_OBS_SIZE],after[SWAT_OBS_SIZE]; int checks=0; uint32_t rng=5;
    // Unseen: the officer 3 m from the suspect, facing away.
    annex_face(s,SWAT_PI); annex_obs_step(s);
    assert(!swat_sim_actor_visible(s,0,1,24,60*SWAT_RAD) && swat_sim_hostiles(s)==1);
    swat_sim_observe(s,0,before); assert(before[29]==0);
    SwatActor* a=&s->actors[1];
    for(int variant=0;variant<7;variant++) {
        if(variant==0) a->role=SWAT_CIVILIAN;
        if(variant==1) { a->role=SWAT_SUSPECT; a->health=(float)(1+swat_random(&rng)%99); }
        if(variant==2) { a->arsenal.slots[a->arsenal.active].magazine=0; a->arsenal.shots+=7; }
        if(variant==3) { a->mind.resolve=1-a->mind.resolve; a->target_actor=2; }
        if(variant==4) { a->gear.surrendered=true; a->gear.disarmed=true; }
        if(variant==5) {
            s->roe[1].state=SWAT_ROE_HOSTILE_FIRING; s->debrief.lethal_violations=9; s->force[1].downed=3;
            s->totals.hostile_down=4;
        }
        if(variant==6) { a->alive=false; a->health=0; assert(swat_sim_hostiles(s)==0); }
        swat_sim_observe(s,0,after);
        if(memcmp(before,after,sizeof(after))) {
            for(int o=0;o<SWAT_OBS_SIZE;o++) if(before[o]!=after[o]) fprintf(stderr,"leak: annex variant %d obs[%d] %g -> %g\n",variant,o,before[o],after[o]);
            assert(!"hidden state reached the annex observation");
        }
        checks++;
    }
    // Seen: the count rises when an officer sees the suspect, and stays
    // when they leave sight and die unseen.
    swat_sim_reset(s); annex_face(s,0); annex_obs_step(s);
    assert(swat_sim_actor_visible(s,0,1,24,60*SWAT_RAD));
    swat_sim_observe(s,0,after); assert(after[29]==1);
    annex_face(s,SWAT_PI); annex_obs_step(s); s->actors[1].alive=false; s->actors[1].health=0; annex_obs_step(s);
    swat_sim_observe(s,0,after); assert(after[29]==1);
    // Secured: killed by police fire, or cuffed.
    swat_sim_reset(s); annex_face(s,0); annex_obs_step(s);
    swat_sim_damage_actor(s,1,0,1000); annex_obs_step(s);
    swat_sim_observe(s,0,after); assert(after[29]==0);
    swat_sim_reset(s); annex_face(s,0); annex_obs_step(s);
    s->actors[1].gear.restrained=true; annex_obs_step(s);
    swat_sim_observe(s,0,after); assert(after[29]==0);
    swat_sim_close(s); free(s);
    return checks;
}

// Role ray weapon channel (contract v4) in real geometry, the placements of
// the ROE tests (test_roe.c) seen by motel officer 0: in the open the weapon
// in hand reads 1; a beam hiding the head leaves torso and rifle in view
// (1); a low wall leaves only the head (0.5); dropped in the open, empty
// hands (0). Each person is judged once per observation, so every ray that
// sees them carries the same reading. The scripted motel officer, fed the
// observation for 60 decisions, fires only at the reading 1.
enum { HANDS_OPEN, HANDS_BEAM, HANDS_WALL, HANDS_DROPPED, HANDS_PLACEMENTS };
static float hands_reading(int placement,int* fired,int* audited) {
    Dict cfg={0}; dict_set_str(&cfg,"roles_mission","motel"); dict_set(&cfg,"hostile_fire",0); dict_set_str(&cfg,"roles_scripted","officers");
    Fixture* f=setup(31,&cfg,SWAT_ROLES_TRAIN,0); Env* e=&f->env; neutral(f);
    for(int t=0;t<3;t++) puf_step(e);
    SwatSim* s=e->sim; int officer=e->actor[0],suspect=-1;
    for(int i=0;i<s->actor_count && suspect<0;i++) if(s->actors[i].present && s->actors[i].alive && s->actors[i].role==SWAT_SUSPECT) suspect=i;
    assert(suspect>=0 && e->motel_map);
    // The officer faces +x where it stands; the suspect stands 3 m ahead, facing it.
    b3Pos at=swat_body_feet_position(&s->actors[officer].controller.body);
    float x=(float)at.x,z=(float)at.z,floor=(float)at.y;
    place(s,officer,x,z,0,0); place(s,suspect,x+3,z,SWAT_PI,0);
    // One gaze ray straight ahead 15 deg down, onto the torso.
    e->gaze[0][0][0]=SWAT_GAZE_YAW_BINS/2; e->gaze[0][0][1]=1;
    // The beam's lower edge is 6.5 cm under the suspect's eye, 60 cm in
    // front of it: above the rifle held at the ready, across the eye line.
    float under=(float)swat_controller_eye(&s->actors[suspect].controller).y-.065f;
    if(placement==HANDS_BEAM) swat_world_box(&s->world,(b3Pos){x+2.4f,under+.5f,z},swat_v(.05f,.5f,.3f),SWAT_CONCRETE,0);
    // The wall's top is 10 cm under the eye, 1 m in front of the suspect:
    // above the rifle's line from there, below the eye line.
    float top=under-.035f;
    if(placement==HANDS_WALL) swat_world_box(&s->world,(b3Pos){x+2,(floor+top)*.5f,z},swat_v(.05f,(top-floor)*.5f,1),SWAT_CONCRETE,0);
    if(placement==HANDS_DROPPED) s->actors[suspect].gear.disarmed=true;
    float obs[OBS_SIZE]; swat_roles_observe(e,0,obs);
    float reading=-1; int rays=0;
    for(int r=0;r<SWAT_ROLES_RAYS;r++) {
        const float* ray=obs+SWAT_ROLES_SELF+r*SWAT_ROLES_RAY_CHANNELS;
        if(ray[SWAT_RAY_SUSPECT]!=1) continue;
        assert(reading<0 || ray[SWAT_RAY_WEAPON]==reading);
        reading=ray[SWAT_RAY_WEAPON]; rays++;
    }
    assert(rays>0);
    // The reticle sees the suspect except where the beam hides the head.
    assert((obs[SWAT_ROLES_SELF+SWAT_RAY_SUSPECT]==1)==(placement!=HANDS_BEAM));
    // Hidden state never moves the reading: mind, health, ammunition and
    // the ROE records and ledgers.
    Snapshot before; snapshot(e,&before);
    SwatActor* a=&s->actors[suspect]; uint32_t rng=7u+(uint32_t)placement;
    a->mind.resolve=1-a->mind.resolve; a->health=(float)(1+swat_random(&rng)%99);
    a->arsenal.slots[a->arsenal.active].magazine=0; a->arsenal.shots+=7;
    s->roe[suspect].armed_seen=!s->roe[suspect].armed_seen;
    scramble_bookkeeping(e,&rng);
    char what[64]; snprintf(what,sizeof(what),"hands placement %d hidden state",placement);
    *audited+=same_except(e,&before,suspect,what);
    // Over the low wall, dropping the weapon is seen: the empty hands go up
    // beside the head, above the wall. Only weapon channels change.
    if(placement==HANDS_WALL) {
        a->gear.disarmed=true; Snapshot after; snapshot(e,&after);
        for(int n=0;n<e->roster;n++) {
            if(e->actor[n]==suspect) continue;
            for(int o=0;o<OBS_SIZE;o++) if(before.obs[n][o]!=after.obs[n][o])
                assert(o>=SWAT_ROLES_SELF && (o-SWAT_ROLES_SELF)%SWAT_ROLES_RAY_CHANNELS==SWAT_RAY_WEAPON && after.obs[n][o]==0);
        }
        assert(after.obs[0][SWAT_ROLES_SELF+SWAT_RAY_WEAPON]==0);
        a->gear.disarmed=false;
    }
    // The scripted officer on this observation: challenge, wait, and fire
    // only at a weapon seen in hand.
    SwatMotelOfficer o; swat_motel_officer_begin(&o,e->motel_map,0,e->officers,e->repeat);
    *fired=0;
    for(int t=0;t<60;t++) {
        float action[SWAT_ROLES_HEADS]; swat_roles_observe(e,0,obs);
        if(t%10==0) obs[SWAT_SELF_TAKEN]=.2f;
        swat_motel_officer_act(&o,e->motel_map,obs,(uint32_t)t,action);
        *fired+=action[SWAT_ROLE_HEAD_TRIGGER]>.5f;
    }
    teardown(f); dict_clear(&cfg);
    return reading;
}
static void hands_channel(void) {
    static const float expected[HANDS_PLACEMENTS]={1,1,.5f,0};
    int fired[HANDS_PLACEMENTS],audited=0;
    for(int p=0;p<HANDS_PLACEMENTS;p++) {
        float reading=hands_reading(p,&fired[p],&audited);
        if(reading!=expected[p]) fprintf(stderr,"hands placement %d reads %g, expected %g\n",p,reading,expected[p]);
        assert(reading==expected[p]);
    }
    assert(fired[HANDS_OPEN]>0 && !fired[HANDS_WALL] && !fired[HANDS_DROPPED] && audited>0);
    printf("PASS roles hands channel: weapon reads 1 in the open (%d officer shots) and with the head behind a beam, 0.5 with only the head over a wall, 0 dropped; "
           "%d perceiving observations unchanged by hidden state; dropping over the wall changes weapon channels only; the motel officer never fires at 0.5 or 0, hurt or not\n",fired[HANDS_OPEN],audited);
}

static void leak_audit(void) {
    Dict annex={0}; dict_set_str(&annex,"roles_mission","annex"); dict_set(&annex,"hostile_fire",0);
    int a=audit_world(&annex,"annex",3);
    Dict motel={0}; dict_set_str(&motel,"roles_mission","motel"); dict_set(&motel,"hostile_fire",0);
    int m=audit_world(&motel,"motel",3);
    assert(a>=20 && m>=60);
    // The weapon in a seen person's hand (contract v4).
    int seen=0,people=weapon_audit(&annex,"annex",&seen)+weapon_audit(&motel,"motel",&seen);
    assert(people>=2 && seen>=4);
    // The hands as the ROE observers see them (contract v4).
    hands_channel();
    // Annex obs 29 (annex contract v4).
    int annex_checks=annex_audit();
    // Custody (obs 42-43) is the team's own record: it counts only people
    // the officers have cuffed, and the ray shows hands up apart from cuffs.
    Fixture* f=setup(31,&annex,SWAT_ROLES_TRAIN,0); Env* e=&f->env; neutral(f);
    float obs[OBS_SIZE]; swat_roles_observe(e,0,obs); assert(obs[42]==0 && obs[43]==0);
    e->sim->actors[1].gear.surrendered=true; swat_roles_observe(e,0,obs); assert(obs[42]==0);
    e->sim->actors[1].gear.restrained=true; swat_roles_observe(e,0,obs); assert(obs[42]==.25f && obs[43]==0);
    swat_roles_observe(e,2,obs); assert(obs[42]==0); // suspects and civilians get no custody report
    teardown(f);
    // Own damage taken (obs 44) is exactly the agent's own health drop.
    Dict fight={0}; dict_set_str(&fight,"roles_mission","annex"); dict_set_str(&fight,"roles_scripted","all"); dict_set(&fight,"max_ticks",900);
    f=setup(5,&fight,SWAT_ROLES_TRAIN,0); e=&f->env; int hits=0;
    for(int t=0;t<600;t++) {
        float health[SWAT_ROLES_MAX_AGENTS]; for(int n=0;n<e->roster;n++) health[n]=e->sim->actors[e->actor[n]].health;
        int episodes=e->finished; puf_step(e);
        if(e->finished!=episodes) continue;
        for(int n=0;n<e->roster;n++) {
            float own[OBS_SIZE]; swat_roles_observe(e,n,own);
            const SwatActor* x=&e->sim->actors[e->actor[n]];
            if(!x->alive) continue;
            assert(own[44]==swat_clamp(fmaxf(0,health[n]-x->health)/100,0,1));
            hits+=own[44]>0;
        }
    }
    teardown(f);
    dict_clear(&annex); dict_clear(&motel); dict_clear(&fight);
    printf("PASS roles eval leak audit: %d annex and %d motel agent observations unchanged by hidden identities, deaths, health, ammo, minds, surrender, ledger and ROE state; custody and own damage are own knowledge (%d hit decisions); "
           "weapon channel: %d seen suspects, %d perceiving observations unchanged by hidden state and a concealed weapon, only weapon channels change when the weapon is dropped; "
           "annex obs 29: %d unseen-suspect changes hidden, seen, unseen death kept, police kill and cuffs secured\n",a,m,hits,people,seen,annex_checks);
}

// --- Curriculum -----------------------------------------------------------

static void curriculum(void) {
    Ini ini; swat_roles_curriculum_load(&ini,"roles_curriculum.ini");
    static const char* names[]={"locomotion_doors","weapon_handling","target_discrimination","cover","extraction_selfplay"};
    assert(ini.num_sections==5);
    for(int i=0;i<5;i++) assert(!strcmp(ini.sections[i].name,names[i]));
    Dict plain={0}; Env* reference=calloc(1,sizeof(Env)); swat_roles_init(reference,&plain,SWAT_ROLES_TRAIN);
    for(int stage=0;stage<ini.num_sections;stage++) {
        Dict cfg={0}; dict_set(&cfg,"roles_stage",stage); dict_set_str(&cfg,"roles_curriculum","roles_curriculum.ini");
        dict_set(&cfg,"roles_agents",8);
        Fixture* f=setup(3,&cfg,SWAT_ROLES_TRAIN,0); Env* e=&f->env;
        // Stages change difficulty, never the reward or the split.
        assert(!memcmp(&e->reward,&reference->reward,sizeof(SwatRoleRewards)));
        assert(e->split==SWAT_ROLES_TRAIN && e->num_agents==8);
        const char* mission=dict_get_str(&ini.sections[stage],"roles_mission");
        assert(e->sim->config.mission==(!strcmp(mission,"motel") ? SWAT_MOTEL : SWAT_ANNEX));
        assert(e->sim->config.max_ticks==(int)dict_get(&ini.sections[stage],"max_ticks"));
        DictItem* hostile=dict_find(&ini.sections[stage],"hostile_fire");
        assert(hostile && e->sim->config.hostile_fire==(hostile->value!=0));
        uint32_t rng=7;
        for(int t=0;t<4;t++) { for(int n=0;n<e->num_agents;n++) sample(f->masks[n],&rng,f->actions[n]); puf_step(e); }
        teardown(f); dict_clear(&cfg);
    }
    puf_close(reference); free(reference); puf_ini_free(&ini);
    // Rejected stage keys: rewards, seeds, layouts, the held-out occupancy,
    // unknown metrics and stages without a promotion rule.
    static const char* rejected[]={"officer_success","suspect_down","roles_time_cost","seed","layout_seed","roles_agents","roles_repeat"};
    char reason[160];
    for(int i=0;i<(int)(sizeof(rejected)/sizeof(rejected[0]));i++) {
        Dict stage={0}; dict_set_str(&stage,"promote_metric","perf"); dict_set(&stage,"promote_at",.5); dict_set(&stage,rejected[i],1);
        assert(swat_roles_stage_check(&stage,reason,sizeof(reason))); dict_clear(&stage);
    }
    Dict stage={0}; dict_set_str(&stage,"promote_metric","perf"); dict_set(&stage,"promote_at",.5);
    assert(!swat_roles_stage_check(&stage,reason,sizeof(reason)));
    dict_set(&stage,"motel_occupancy",SWAT_ROLES_EVAL_OCCUPANCY); assert(swat_roles_stage_check(&stage,reason,sizeof(reason)));
    dict_set(&stage,"motel_occupancy",1); assert(!swat_roles_stage_check(&stage,reason,sizeof(reason)));
    dict_set_str(&stage,"promote_metric","episode_return"); assert(swat_roles_stage_check(&stage,reason,sizeof(reason)));
    dict_clear(&stage);
    stage=(Dict){0}; dict_set(&stage,"hostile_fire",0); assert(swat_roles_stage_check(&stage,reason,sizeof(reason)));
    dict_clear(&stage); dict_clear(&plain);
    puts("PASS roles eval curriculum: 5 stages load and run as training envs with unchanged rewards; reward, seed, layout and held-out keys rejected");
}

// --- Metrics --------------------------------------------------------------

static Dict eval_annex(void) {
    Dict cfg={0}; dict_set_str(&cfg,"roles_mission","annex"); dict_set(&cfg,"hostile_fire",0);
    dict_set_str(&cfg,"roles_scripted","suspects+civilians");
    return cfg;
}
static void metrics(void) {
    Dict cfg=eval_annex();
    Fixture* f=setup(42,&cfg,SWAT_ROLES_EVAL,0); Env* e=&f->env; neutral(f);
    // Neutralised: the suspect is down, both officers at extraction.
    SwatSim* s=e->sim; uint32_t seed=e->current.seed;
    swat_sim_damage_actor(s,1,0,1000);
    place(s,0,(float)s->extraction.x,(float)s->extraction.z-.5f,0,0); place(s,3,(float)s->extraction.x,(float)s->extraction.z+.5f,0,0);
    puf_step(e);
    SwatRolesEpisode r=e->last;
    assert(e->finished==1 && r.success && r.seed==seed && swat_roles_eval_value(r.seed) && swat_roles_eval_value(r.layout_seed));
    assert(r.end==SWAT_SUCCESS && r.ticks>=1 && r.ticks<=4 && r.decisions==1 && r.suspects==1 && r.suspects_killed==1 && !r.suspects_arrested);
    assert(r.civilians==1 && !r.civilians_harmed && r.officers==2 && !r.officers_down && !r.roe_violations && !r.officer_rounds);
    // Arrested: the suspect is cuffed instead.
    neutral(f); s=e->sim; s->actors[1].gear.restrained=true;
    place(s,0,(float)s->extraction.x,(float)s->extraction.z-.5f,0,0); place(s,3,(float)s->extraction.x,(float)s->extraction.z+.5f,0,0);
    puf_step(e); r=e->last;
    assert(e->finished==2 && r.success && r.suspects_arrested==1 && !r.suspects_killed && !r.suspects_free);
    // ROE: an officer shoots the civilian; the annex fails on civilian harm.
    neutral(f); s=e->sim;
    place(s,0,19,-2.5f,-SWAT_PI*0.5f,-0.13f); s->actors[0].controller.ads=1;
    f->actions[0][SWAT_ROLE_HEAD_AIM]=1; f->actions[0][SWAT_ROLE_HEAD_TRIGGER]=1;
    puf_step(e); r=e->last;
    assert(e->finished==3 && !r.success && r.end==SWAT_CIVILIAN_HARMED && r.civilians_harmed==1 && r.roe_violations==1);
    // The annex has no ROE perception (no tactical rules): no lethal count.
    assert(r.protected_violations==1 && r.lethal_violations==0);
    assert(r.unlawful_damage>0 && r.officer_rounds>=1 && r.suspects_free==1);
    teardown(f); dict_clear(&cfg);
    // Suite v2 ROE: a lethal hit by an officer on a motel suspect who is
    // still UNKNOWN (unchallenged, no weapon raised) is a lethal violation
    // in the sim's debrief and on the shooter's force ledger. The env merges
    // it with protected force per officer engagement: a lethal hit and a
    // protected hit in the same engagement are one ROE violation.
    cfg=(Dict){0}; dict_set_str(&cfg,"roles_mission","motel"); dict_set(&cfg,"hostile_fire",0);
    f=setup(42,&cfg,SWAT_ROLES_EVAL,2); e=&f->env; s=e->sim;
    int suspect=-1; for(int i=0;i<s->actor_count;i++) if(s->actors[i].present && s->actors[i].role==SWAT_SUSPECT) { suspect=i; break; }
    assert(suspect>=0 && s->roe[suspect].state==SWAT_ROE_UNKNOWN);
    swat_sim_damage_region(s,suspect,0,10,SWAT_TORSO,false);
    assert(s->debrief.lethal_violations==1 && s->force[0].lethal==1 && s->force[0].protected_damage==0);
    SwatRolesBefore before; swat_roles_snapshot(s,&before); float rewards[SWAT_ROLES_MAX_AGENTS]={0};
    swat_roles_tick(e,&before,rewards);
    assert(e->current.roe_violations==1 && e->current.protected_violations==0);
    s->force[0].lethal=0; s->force[0].protected_damage=5; swat_roles_tick(e,&before,rewards);
    assert(e->current.roe_violations==1 && e->current.protected_violations==1);
    s->tick+=SWAT_ROE_ENGAGEMENT_TICKS; s->force[0].protected_damage=0; s->force[0].lethal=1; swat_roles_tick(e,&before,rewards);
    assert(e->current.roe_violations==2 && e->current.protected_violations==1);
    s->force[0].lethal=0;
    e->sim->config.max_ticks=s->tick+1; neutral(f); puf_step(e); r=e->last;
    assert(e->finished==1 && r.roe_violations==2 && r.protected_violations==1 && r.lethal_violations==1);
    teardown(f); dict_clear(&cfg);

    // Statistics against known values.
    SwatRolesStat w=swat_roles_rate(5,10);
    assert(fabs(w.mean-.5)<1e-12 && fabs(w.low-.2366)<1e-3 && fabs(w.high-.7634)<1e-3);
    w=swat_roles_rate(0,10); assert(w.low==0 && fabs(w.high-.2775)<1e-3);
    w=swat_roles_rate(10,10); assert(fabs(w.high-1)<1e-12 && fabs(w.low-.7225)<1e-3);
    double x[30]; for(int i=0;i<30;i++) x[i]=i%2 ? 3 : 1; // mean 2, sd 1.01710
    SwatRolesStat m=swat_roles_mean(x,30);
    double exact=2.045229642*1.017095556/sqrt(30.0); // t(0.975, 29)
    assert(fabs(m.mean-2)<1e-12 && fabs((m.high-m.mean)-exact)<1e-3*exact && fabs((m.mean-m.low)-exact)<1e-3*exact);
    double y[5]={1,2,3,4,5}; m=swat_roles_mean(y,5);
    assert(fabs((m.high-3)-2.776445*sqrt(2.5/5))<.02*2.776445*sqrt(2.5/5));
    m=swat_roles_mean(y,1); assert(m.low==1 && m.high==1 && m.n==1);
    // Time to clear exists only for completed episodes.
    SwatRolesEvalEpisode runs[3]={{{.success=true,.ticks=600,.decisions=150},1.5,.1},{{.success=false,.ticks=1800,.decisions=450},4.5,.1},
        {{.success=true,.ticks=1200,.decisions=300},3,.1}};
    SwatRolesStat t=swat_roles_aggregate(runs,3,SWAT_METRIC_TIME_TO_CLEAR); assert(t.n==2 && fabs(t.mean-15)<1e-9);
    SwatRolesStat c=swat_roles_aggregate(runs,3,SWAT_METRIC_COMPLETION); assert(c.n==3 && fabs(c.mean-2.0/3)<1e-9);
    puts("PASS roles eval metrics: neutralised, arrested and ROE-failure episodes recorded; suite v2 lethal and protected ROE counts merged per engagement; Wilson and t intervals; time to clear over completions");
}

// --- Suite determinism and report -----------------------------------------

static void suite(const char* prefix) {
    enum { EPISODES=3 };
    SwatRolesEvalEpisode a[EPISODES],b[EPISODES];
    // Annex with scripted roles, twice: identical outcomes (timing aside).
    swat_roles_eval_run(0,SWAT_MATCHUP_SCRIPTED,NULL,EPISODES,600,a);
    swat_roles_eval_run(0,SWAT_MATCHUP_SCRIPTED,NULL,EPISODES,600,b);
    for(int i=0;i<EPISODES;i++) {
        assert(!memcmp(&a[i].outcome,&b[i].outcome,sizeof(SwatRolesEpisode)));
        assert(a[i].outcome.seed==swat_roles_eval_seed(SWAT_ROLES_EVAL_SEEDS,0,(uint32_t)i));
        assert(a[i].outcome.ticks>0 && a[i].outcome.ticks<=600);
    }
    // The random-officer floor is reproducible from the episode seed.
    SwatRolesRandom ra={0},rb={0};
    SwatRolesPolicy pa={"random",&ra,swat_roles_random_begin,swat_roles_random_act},pb={"random",&rb,swat_roles_random_begin,swat_roles_random_act};
    SwatRolesEvalEpisode c[2],d[2];
    swat_roles_eval_run(1,SWAT_MATCHUP_POLICY_OFFICERS,&pa,2,240,c);
    swat_roles_eval_run(1,SWAT_MATCHUP_POLICY_OFFICERS,&pb,2,240,d);
    for(int i=0;i<2;i++) assert(!memcmp(&c[i].outcome,&d[i].outcome,sizeof(SwatRolesEpisode)));
    // Policy-controlled occupants run through the same hook.
    SwatRolesEvalEpisode o[1];
    swat_roles_eval_run(0,SWAT_MATCHUP_POLICY_OCCUPANTS,&pa,1,120,o);
    assert(o[0].outcome.ticks>0);
    // The held-out busy motel: six occupants on a held-out layout.
    SwatRolesEvalEpisode busy[1];
    swat_roles_eval_run(5,SWAT_MATCHUP_SCRIPTED,NULL,1,120,busy);
    assert(busy[0].outcome.suspects==2 && busy[0].outcome.civilians==4 && busy[0].outcome.officers==2);
    assert(swat_roles_eval_value(busy[0].outcome.layout_seed) && busy[0].outcome.ticks==120 && !busy[0].outcome.success);
    // Reports.
    SwatRolesEvalResult results[4]={{0,SWAT_MATCHUP_SCRIPTED,NULL,EPISODES,a},{1,SWAT_MATCHUP_POLICY_OFFICERS,"random",2,c},
        {0,SWAT_MATCHUP_POLICY_OCCUPANTS,"random",1,o},{5,SWAT_MATCHUP_SCRIPTED,NULL,1,busy}};
    char path[512]; snprintf(path,sizeof(path),"%s.json",prefix);
    FILE* f=fopen(path,"w"); assert(f); swat_roles_eval_json(f,results,4,"smoke test"); fclose(f);
    snprintf(path,sizeof(path),"%s.md",prefix);
    f=fopen(path,"w"); assert(f); swat_roles_eval_markdown(f,results,4,"smoke test"); fclose(f);
    f=fopen(path,"r"); assert(f); char line[4096]; int rows=0;
    while(fgets(line,sizeof(line),f)) rows+=!strncmp(line,"| annex",7) || !strncmp(line,"| motel_busy",11);
    fclose(f); assert(rows==4);
    puts("PASS roles eval suite: paired scripted and random runs identical, policy hook for officers and occupants, held-out busy motel, JSON and Markdown written");
}

int main(int argc,char** argv) {
    split();
    leak_audit();
    curriculum();
    metrics();
    suite(argc>1 ? argv[1] : "roles-eval-test");
    return 0;
}
