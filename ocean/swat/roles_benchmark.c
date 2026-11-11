// Headless throughput and determinism benchmark for the role task contract
// (roles_env.h). It drives the same puf_init/puf_reset/puf_step entry points
// the trainer uses, with every controlled agent choosing a uniformly random
// legal action from its mask each decision, so episodes include firing,
// interaction, surrender and resets.
//
// Throughput: --mission annex|motel --envs N --threads T --decisions D prints
// env decisions, agent steps and simulation ticks per second. Each decision
// visits every env, like a trainer rollout.
//
// Determinism: --hashes FILE writes one state hash per decision for env 0
// (use --envs 1 --threads 1, and --repeat 1 to hash every 60 Hz tick).
// --check FILE recomputes them and fails at the first differing decision.
// The hash covers the replicated snapshot digest, the physics transform and
// velocities of every actor and dynamic object body, the simulation RNG and
// tick, and the observations, masks, rewards and terminals the trainer sees,
// so an optimisation that perturbs any of them is caught.
#define SWAT_ROLES_TRAINING
#define SWAT_ROLES_NO_RENDER
#include "swat.h"
#include "replay.h"
#include <string.h>
#include <time.h>

typedef struct Bench {
    Env env;
    float obs[SWAT_ROLES_MAX_AGENTS][OBS_SIZE],actions[SWAT_ROLES_MAX_AGENTS][NUM_ATNS];
    float rewards[SWAT_ROLES_MAX_AGENTS],terminals[SWAT_ROLES_MAX_AGENTS];
    unsigned char masks[SWAT_ROLES_MAX_AGENTS][SWAT_ROLES_LOGITS];
    uint32_t random;
    int episodes;
} Bench;

static const int sizes[]=ACT_SIZES;

static double now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return t.tv_sec+t.tv_nsec*1e-9;
}
static uint32_t fnv(uint32_t hash,const void* bytes,size_t count) {
    const unsigned char* p=bytes;
    for(size_t i=0;i<count;i++) { hash^=p[i]; hash*=16777619u; }
    return hash;
}
static uint32_t body_hash(uint32_t hash,b3BodyId body) {
    if(!b3Body_IsValid(body)) return hash;
    b3WorldTransform t=b3Body_GetTransform(body);
    b3Vec3 v=b3Body_GetLinearVelocity(body),w=b3Body_GetAngularVelocity(body);
    hash=fnv(hash,&t,sizeof(t)); hash=fnv(hash,&v,sizeof(v));
    return fnv(hash,&w,sizeof(w));
}
static uint32_t state_hash(const Bench* b) {
    const SwatSim* s=b->env.sim;
    uint32_t hash=swat_replay_digest(s);
    hash=fnv(hash,&s->rng,sizeof(s->rng)); hash=fnv(hash,&s->tick,sizeof(s->tick));
    for(int i=0;i<s->actor_count;i++) if(s->actors[i].present) hash=body_hash(hash,s->actors[i].controller.body.body);
    for(int i=0;i<s->world.count;i++) {
        const SwatObject* o=&s->world.objects[i];
        if(o->active && (o->motion || o->rubble)) hash=body_hash(hash,o->body);
    }
    for(int n=0;n<b->env.num_agents;n++) {
        hash=fnv(hash,b->obs[n],sizeof(b->obs[n])); hash=fnv(hash,b->masks[n],sizeof(b->masks[n]));
        hash=fnv(hash,&b->rewards[n],sizeof(float)); hash=fnv(hash,&b->terminals[n],sizeof(float));
    }
    return hash;
}
static void choose(Bench* b) {
    for(int n=0;n<b->env.num_agents;n++) {
        const unsigned char* mask=b->masks[n]; int offset=0;
        for(int h=0;h<NUM_ATNS;h++) {
            int legal=0; for(int i=0;i<sizes[h];i++) legal+=mask[offset+i]!=0;
            int pick=legal ? (int)(swat_random(&b->random)%(uint32_t)legal) : 0;
            b->actions[n][h]=0;
            for(int i=0;i<sizes[h];i++) if(mask[offset+i] && pick--==0) { b->actions[n][h]=(float)i; break; }
            offset+=sizes[h];
        }
    }
}
static void setup(Bench* b,Dict* cfg,uint32_t seed) {
    memset(b,0,sizeof(*b));
    b->env.rng=seed; b->random=0x9e3779b9u^seed;
    puf_init(&b->env,cfg);
    for(int i=0;i<b->env.num_agents;i++) {
        b->env.agents[i].observations=b->obs[i]; b->env.agents[i].actions=b->actions[i];
        b->env.agents[i].rewards=&b->rewards[i]; b->env.agents[i].terminals=&b->terminals[i];
        b->env.agents[i].action_mask=b->masks[i];
    }
    puf_reset(&b->env);
}
static void step(Bench* b) {
    choose(b);
    puf_step(&b->env);
    b->episodes+=b->terminals[0]!=0;
}

int main(int argc,char** argv) {
    int envs=16,threads=1,decisions=2000,repeat=4,occupancy=0;
    const char *mission="annex",*hashes=NULL,*check=NULL;
    for(int i=1;i<argc;i++) {
        if(i+1<argc && !strcmp(argv[i],"--mission")) mission=argv[++i];
        else if(i+1<argc && !strcmp(argv[i],"--envs")) envs=atoi(argv[++i]);
        else if(i+1<argc && !strcmp(argv[i],"--threads")) threads=atoi(argv[++i]);
        else if(i+1<argc && !strcmp(argv[i],"--decisions")) decisions=atoi(argv[++i]);
        else if(i+1<argc && !strcmp(argv[i],"--repeat")) repeat=atoi(argv[++i]);
        else if(i+1<argc && !strcmp(argv[i],"--occupancy")) occupancy=atoi(argv[++i]);
        else if(i+1<argc && !strcmp(argv[i],"--hashes")) hashes=argv[++i];
        else if(i+1<argc && !strcmp(argv[i],"--check")) check=argv[++i];
        else {
            fprintf(stderr,"usage: %s [--mission annex|motel] [--envs N] [--threads N] [--decisions N] [--repeat N] [--occupancy N] [--hashes FILE | --check FILE]\n",argv[0]);
            return 2;
        }
    }
    if(envs<1 || envs>4096 || threads<1 || threads>128 || decisions<1 || (hashes && check)) return 2;
    if((hashes || check) && (envs!=1 || threads!=1)) { fprintf(stderr,"hash traces need --envs 1 --threads 1\n"); return 2; }
    Dict cfg={0};
    dict_set_str(&cfg,"roles_mission",mission); dict_set(&cfg,"roles_repeat",repeat);
    dict_set(&cfg,"motel_occupancy",occupancy);
    Bench* benches=calloc((size_t)envs,sizeof(*benches)); if(!benches) return 2;
    double setup_start=now();
    for(int i=0;i<envs;i++) setup(&benches[i],&cfg,2718u+(uint32_t)i);
    double setup_seconds=now()-setup_start;

    if(hashes || check) {
        FILE* file=fopen(hashes ? hashes : check,hashes ? "wb" : "rb");
        if(!file) { perror(hashes ? hashes : check); return 2; }
        Bench* b=&benches[0]; int status=0;
        for(int d=0;d<decisions;d++) {
            step(b);
            uint32_t hash=state_hash(b),expected=0;
            if(hashes) { if(fwrite(&hash,sizeof(hash),1,file)!=1) { status=2; break; } continue; }
            if(fread(&expected,sizeof(expected),1,file)!=1) { fprintf(stderr,"hash trace ends at decision %d\n",d); status=2; break; }
            if(hash!=expected) { fprintf(stderr,"FAIL determinism: decision %d tick %d hash %08x expected %08x\n",d,b->env.sim->tick,hash,expected); status=1; break; }
        }
        if(fclose(file)) status=2;
        if(!status) printf("%s %s: mission=%s repeat=%d decisions=%d episodes=%d\n",hashes ? "WROTE" : "PASS determinism",
            hashes ? hashes : check,mission,repeat,decisions,b->episodes);
        puf_close(&b->env); free(benches); dict_clear(&cfg);
        return status;
    }

    double start=now();
#pragma omp parallel num_threads(threads)
    for(int d=0;d<decisions;d++) {
#pragma omp for schedule(static)
        for(int i=0;i<envs;i++) step(&benches[i]);
    }
    double seconds=now()-start;
    // The checksum combines every env's final state hash, so runs at
    // different thread counts and builds can be compared for identical work.
    int episodes=0,agents=benches[0].env.num_agents,roster=benches[0].env.roster; uint32_t checksum=0;
    for(int i=0;i<envs;i++) { episodes+=benches[i].episodes; checksum^=state_hash(&benches[i])*(uint32_t)(2*i+1); }
    double steps=(double)decisions*envs;
    printf("mission=%s envs=%d threads=%d repeat=%d agents=%d roster=%d decisions=%.0f episodes=%d setup_seconds=%.3f seconds=%.3f "
           "env_steps_per_second=%.1f agent_steps_per_second=%.1f ticks_per_second=%.1f checksum=%08x\n",
           mission,envs,threads,repeat,agents,roster,steps,episodes,setup_seconds,seconds,
           steps/seconds,steps*agents/seconds,steps*repeat/seconds,checksum);
    for(int i=0;i<envs;i++) puf_close(&benches[i].env);
    free(benches); dict_clear(&cfg);
    return 0;
}
