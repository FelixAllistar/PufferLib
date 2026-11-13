// Held-out evaluation suite for the role task (CONTRACT.md "Held-out
// evaluation suite"; ROADMAP "Train and evaluate the AI" items 1 and 4).
//
// A suite version fixes the cases (mission, occupancy, roster, time limit,
// episode count) and, through roles_split.h, every episode's simulation and
// layout seed. Those seeds come from the evaluation stream, which training
// envs cannot draw from, and the busy motel occupancy is evaluation-only, so
// nothing here is ever seen during training or changed by the curriculum or
// reward coefficients. Any change to a case, to the seed derivation or to
// the metric definitions needs a new SWAT_ROLES_EVAL_SUITE.
//
// Suite v2 changes only the metrics: roe_violations now also counts lethal
// force on a person who was not a perceived lethal threat (roe.h), and
// protected_violations and lethal_violations report the two kinds apart.
// Its cases and seeds are suite v1's: SWAT_ROLES_EVAL_SEEDS keeps the seed
// stream of v1, so v1 and v2 runs play the same episodes.
//
// Suite v3 keeps every v2 case, seed and metric and adds two low-ammunition
// cases, annex_low_ammo and motel_low_ammo, so the suite exercises reload
// handling: each officer starts with SWAT_ROLES_EVAL_LOW_MAGAZINE rounds in
// the seated primary magazine (plus the chambered round) and full spares,
// set through the sim config (officer_magazine) when the episode is set up.
// They are cases 6 and 7 of the same seed stream, so their seeds are their
// own and every v2 case keeps its index and seeds. The new metrics are per
// officer per episode: reloads completed, reloads interrupted, and decisions
// spent with an empty weapon while a spare magazine was available.
//
// Suite v4 keeps every v3 case, seed and metric and adds two contact reload
// cases, annex_contact_reload and motel_contact_reload (cases 8 and 9): each
// episode the officers start with 1 to 3 rounds in the seated primary
// magazine (drawn from the episode seed, officer_magazine_min..
// officer_magazine) and in sight of the nearest suspect (roles_contact), so
// they reload with an armed enemy in front of them. Eight metrics are new,
// per officer and per suspect (every suspect in the sim): reloads started
// while seeing an armed enemy, reloads broken by damage or a sprint
// (SWAT_WEAPON_EVENT_RELOAD_DISTURBED), deaths while reloading, and reloads
// completed. Suite v3 stays reproducible by name (roles_eval --suite 3): it
// runs cases 0-7 and reports the v3 metrics.
//
// The suite drives the ordinary role env (roles_env.h) on the evaluation
// split. Every agent is either a scripted baseline (roles_baselines.h) or
// controlled by a SwatRolesPolicy, which sees exactly what a deployed actor
// sees: the role observation and legal-action mask of the agents it controls.
// Metrics come from the env's ground-truth episode record, which no agent
// observes.
#ifndef SWAT_ROLES_EVAL_H
#define SWAT_ROLES_EVAL_H
#include <time.h>
#include "roles_env.h"

#define SWAT_ROLES_EVAL_SUITE 4
#define SWAT_ROLES_EVAL_SEEDS 1
// Rounds in each officer's seated primary magazine in the low-ammunition
// cases (the carbine holds 30).
#define SWAT_ROLES_EVAL_LOW_MAGAZINE 3
// Rounds in the seated primary magazine in the contact reload cases (v4):
// drawn per episode from 1 to 3.
#define SWAT_ROLES_EVAL_CONTACT_MAGAZINE_MIN 1
#define SWAT_ROLES_EVAL_CONTACT_MAGAZINE_MAX 3

typedef struct SwatRolesEvalCase {
    const char* name;
    const char* mission;     // roles_mission
    int occupancy;           // motel_occupancy (3 is evaluation-only)
    int officers,escape,max_ticks,episodes;
    int officer_magazine;    // officer_magazine (0: full)
    int magazine_min;        // officer_magazine_min (0: officer_magazine fixed)
    int contact;             // roles_contact: officers start in sight of a suspect
} SwatRolesEvalCase;

// Suite v1 cases, unchanged in v2-v4, the v3 low-ammunition cases, then the
// v4 contact reload cases.
// Every episode has hostile fire, randomized annex placement, the default
// reward coefficients and roles_repeat=4. Motel layout seeds come from the
// held-out stream, so occupant bays differ from any training layout;
// motel_busy also uses the held-out occupancy profile.
static const SwatRolesEvalCase swat_roles_eval_cases[]={
    {"annex",          "annex",0,2,0,1800,64,0,0,0},
    {"annex_escape",   "annex",0,2,1,1800,64,0,0,0},
    {"motel_standard", "motel",0,2,0,5400,32,0,0,0},
    {"motel_dispersed","motel",1,2,0,5400,32,0,0,0},
    {"motel_mixed",    "motel",2,2,0,5400,32,0,0,0},
    {"motel_busy",     "motel",3,2,0,5400,32,0,0,0},
    {"annex_low_ammo", "annex",0,2,0,1800,64,SWAT_ROLES_EVAL_LOW_MAGAZINE,0,0},
    {"motel_low_ammo", "motel",0,2,0,5400,32,SWAT_ROLES_EVAL_LOW_MAGAZINE,0,0},
    {"annex_contact_reload","annex",0,2,0,1800,64,SWAT_ROLES_EVAL_CONTACT_MAGAZINE_MAX,SWAT_ROLES_EVAL_CONTACT_MAGAZINE_MIN,1},
    {"motel_contact_reload","motel",0,2,0,5400,32,SWAT_ROLES_EVAL_CONTACT_MAGAZINE_MAX,SWAT_ROLES_EVAL_CONTACT_MAGAZINE_MIN,1},
};
#define SWAT_ROLES_EVAL_CASES ((int)(sizeof(swat_roles_eval_cases)/sizeof(swat_roles_eval_cases[0])))
// Cases of an earlier suite still reproducible by name: v3 is cases 0-7.
static inline int swat_roles_eval_suite_cases(int suite) {
    return suite==3 ? 8 : suite==SWAT_ROLES_EVAL_SUITE ? SWAT_ROLES_EVAL_CASES : 0;
}

// Who controls which roles. Unlisted roles run the scripted baselines.
typedef enum SwatRolesMatchup {
    SWAT_MATCHUP_SCRIPTED,          // all roles scripted: the reference bar
    SWAT_MATCHUP_POLICY_OFFICERS,   // policy officers vs scripted occupants
    SWAT_MATCHUP_POLICY_OCCUPANTS,  // scripted officers vs policy suspects and civilians
    SWAT_MATCHUPS
} SwatRolesMatchup;
static const char* const swat_roles_matchup_names[SWAT_MATCHUPS]={"scripted","policy_officers","policy_occupants"};
static const char* const swat_roles_matchup_scripted[SWAT_MATCHUPS]={"all","suspects+civilians","officers"};

// Policy hook. begin starts a new episode for agents 0..agents-1 (reset any
// recurrent state; episode_seed allows reproducible sampling). act writes
// actions [agents][SWAT_ROLES_HEADS] from observations [agents][SWAT_ROLES_OBS]
// and masks [agents][SWAT_ROLES_LOGITS]. Scripted and padding agents observe
// zeros and their actions are ignored, as in training. A native checkpoint
// policy is in roles_eval.c (src/puffercpu.c inference, the trainer's format).
typedef struct SwatRolesPolicy {
    const char* name;
    void* context;
    void (*begin)(void* context,int agents,uint32_t episode_seed);
    void (*act)(void* context,int agents,const float* observations,const unsigned char* masks,float* actions);
} SwatRolesPolicy;

// Uniform over legal actions: the floor every trained policy must clear.
typedef struct SwatRolesRandom { uint32_t rng; } SwatRolesRandom;
static void swat_roles_random_begin(void* context,int agents,uint32_t seed) {
    ((SwatRolesRandom*)context)->rng=swat_roles_split_mix(seed^0x2545f491u)|1u;
}
static void swat_roles_random_act(void* context,int agents,const float* observations,const unsigned char* masks,float* actions) {
    SwatRolesRandom* r=context; static const int sizes[]=SWAT_ROLES_ACTION_SIZES;
    for(int n=0;n<agents;n++) {
        const unsigned char* mask=masks+(size_t)n*SWAT_ROLES_LOGITS; int offset=0;
        for(int h=0;h<SWAT_ROLES_HEADS;h++) {
            int legal=0; for(int i=0;i<sizes[h];i++) legal+=mask[offset+i]!=0;
            int pick=legal ? (int)(swat_random(&r->rng)%(uint32_t)legal) : 0,value=0;
            for(int i=0;i<sizes[h];i++) if(mask[offset+i] && pick--==0) { value=i; break; }
            actions[(size_t)n*SWAT_ROLES_HEADS+h]=(float)value; offset+=sizes[h];
        }
    }
}

typedef struct SwatRolesEvalEpisode {
    SwatRolesEpisode outcome;
    // Wall time inside puf_step (not deterministic): all decisions but the
    // last, and the last one, which also resets the env for the next episode.
    double seconds,reset_seconds;
} SwatRolesEvalEpisode;

static double swat_roles_eval_now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (double)t.tv_sec+1e-9*(double)t.tv_nsec;
}

// Runs `episodes` held-out episodes of case `index` under one matchup.
// policy may be NULL only for SWAT_MATCHUP_SCRIPTED. max_ticks>0 overrides
// the case limit (for smoke tests; a full suite run never overrides).
static void swat_roles_eval_run(int index,SwatRolesMatchup matchup,const SwatRolesPolicy* policy,
                                int episodes,int max_ticks,SwatRolesEvalEpisode* out) {
    const SwatRolesEvalCase* c=&swat_roles_eval_cases[index];
    if(matchup!=SWAT_MATCHUP_SCRIPTED && !policy) { fprintf(stderr,"swat roles eval: %s needs a policy\n",swat_roles_matchup_names[matchup]); exit(1); }
    Dict cfg={0};
    dict_set_str(&cfg,"roles_mission",c->mission); dict_set(&cfg,"motel_occupancy",c->occupancy);
    dict_set(&cfg,"roles_officers",c->officers); dict_set(&cfg,"suspect_escape",c->escape);
    dict_set(&cfg,"max_ticks",max_ticks>0 ? max_ticks : c->max_ticks);
    dict_set(&cfg,"hostile_fire",1); dict_set(&cfg,"randomize",1); dict_set(&cfg,"officer_magazine",c->officer_magazine);
    dict_set(&cfg,"officer_magazine_min",c->magazine_min); dict_set(&cfg,"roles_contact",c->contact);
    dict_set_str(&cfg,"roles_scripted",swat_roles_matchup_scripted[matchup]); dict_set(&cfg,"roles_scripted_rate",1);
    Env* env=calloc(1,sizeof(Env));
    float* obs=calloc((size_t)SWAT_ROLES_MAX_AGENTS*SWAT_ROLES_OBS,sizeof(float));
    float* actions=calloc((size_t)SWAT_ROLES_MAX_AGENTS*SWAT_ROLES_HEADS,sizeof(float));
    unsigned char* masks=calloc((size_t)SWAT_ROLES_MAX_AGENTS*SWAT_ROLES_LOGITS,1);
    float rewards[SWAT_ROLES_MAX_AGENTS],terminals[SWAT_ROLES_MAX_AGENTS];
    if(!env || !obs || !actions || !masks) { fprintf(stderr,"swat roles eval: allocation failed\n"); exit(1); }
    swat_roles_init(env,&cfg,SWAT_ROLES_EVAL);
    for(int n=0;n<env->num_agents;n++) {
        env->agents[n].observations=obs+(size_t)n*SWAT_ROLES_OBS; env->agents[n].actions=actions+(size_t)n*SWAT_ROLES_HEADS;
        env->agents[n].action_mask=masks+(size_t)n*SWAT_ROLES_LOGITS;
        env->agents[n].rewards=&rewards[n]; env->agents[n].terminals=&terminals[n];
    }
    env->eval_suite=SWAT_ROLES_EVAL_SEEDS; env->eval_case=(uint32_t)index; env->episodes=0;
    puf_reset(env);
    for(int e=0;e<episodes;e++) {
        if(policy) policy->begin(policy->context,env->num_agents,env->current.seed);
        double seconds=0,last=0; int finished=env->finished;
        while(env->finished==finished) {
            if(policy) policy->act(policy->context,env->num_agents,obs,masks,actions);
            seconds+=last;
            double start=swat_roles_eval_now();
            puf_step(env);
            last=swat_roles_eval_now()-start;
        }
        out[e].outcome=env->last; out[e].seconds=seconds; out[e].reset_seconds=last;
    }
    puf_close(env); free(env); free(obs); free(actions); free(masks); dict_clear(&cfg);
}

// A statistic with its 95% confidence interval [low, high] over n episodes.
typedef struct SwatRolesStat { double mean,low,high; int n; } SwatRolesStat;

// Wilson score interval for k successes in n trials.
static SwatRolesStat swat_roles_rate(int k,int n) {
    SwatRolesStat s={0,0,0,n};
    if(n<=0) return s;
    const double z=1.959963984540054; double p=(double)k/n,z2=z*z/n;
    double centre=(p+z2/2)/(1+z2),half=z*sqrt(p*(1-p)/n+z*z/(4.0*n*n))/(1+z2);
    s.mean=p; s.low=fmax(0,centre-half); s.high=fmin(1,centre+half);
    return s;
}
// Student t interval for a mean. The t quantile uses the Cornish-Fisher
// expansion: within 2% of the exact value for n >= 5 and 0.1% for n >= 30.
// A single value has no interval.
static SwatRolesStat swat_roles_mean(const double* x,int n) {
    SwatRolesStat s={0,0,0,n};
    if(n<=0) return s;
    double sum=0; for(int i=0;i<n;i++) sum+=x[i];
    s.mean=s.low=s.high=sum/n;
    if(n<2) return s;
    double var=0; for(int i=0;i<n;i++) var+=(x[i]-s.mean)*(x[i]-s.mean);
    var/=n-1;
    const double z=1.959963984540054; double v=n-1,z3=z*z*z,z5=z3*z*z;
    double t=z+(z3+z)/(4*v)+(5*z5+16*z3+3*z)/(96*v*v);
    double half=t*sqrt(var/n);
    s.low=s.mean-half; s.high=s.mean+half;
    return s;
}

// Aggregated metrics of one (case, matchup) run.
enum {
    SWAT_METRIC_COMPLETION, SWAT_METRIC_CIVILIAN_HARM_RATE, SWAT_METRIC_CIVILIANS_HARMED, SWAT_METRIC_CIVILIANS_KILLED,
    SWAT_METRIC_SUSPECTS_KILLED, SWAT_METRIC_SUSPECTS_ARRESTED, SWAT_METRIC_SUSPECTS_FREE, SWAT_METRIC_OFFICERS_DOWN,
    SWAT_METRIC_ROE_VIOLATIONS, SWAT_METRIC_PROTECTED_VIOLATIONS, SWAT_METRIC_LETHAL_VIOLATIONS,
    SWAT_METRIC_UNLAWFUL_DAMAGE, SWAT_METRIC_TIME_TO_CLEAR, SWAT_METRIC_OFFICER_ROUNDS,
    SWAT_METRIC_SUSPECT_ROUNDS, SWAT_METRIC_RELOADS, SWAT_METRIC_RELOAD_INTERRUPTS, SWAT_METRIC_DRY_DECISIONS,
    SWAT_METRIC_EPISODE_SECONDS, SWAT_METRIC_STEP_MS, SWAT_METRIC_TICK_US, SWAT_METRIC_RESET_MS,
    // Suite v4: reloads under pressure, per officer and per suspect.
    SWAT_METRIC_OFFICER_RELOADS_UNDER_FIRE, SWAT_METRIC_OFFICER_RELOADS_DISTURBED, SWAT_METRIC_OFFICER_RELOAD_DEATHS,
    SWAT_METRIC_OFFICER_RELOADS_DONE, SWAT_METRIC_SUSPECT_RELOADS_UNDER_FIRE, SWAT_METRIC_SUSPECT_RELOADS_DISTURBED,
    SWAT_METRIC_SUSPECT_RELOAD_DEATHS, SWAT_METRIC_SUSPECT_RELOADS_DONE, SWAT_METRICS
};
// Suite v3 reports the metrics before the v4 reload pressure ones.
#define SWAT_METRICS_V3 SWAT_METRIC_OFFICER_RELOADS_UNDER_FIRE
static inline int swat_roles_eval_metrics(int suite) { return suite>=4 ? SWAT_METRICS : SWAT_METRICS_V3; }
static const char* const swat_roles_metric_names[SWAT_METRICS]={
    "completion","civilian_harm_rate","civilians_harmed","civilians_killed",
    "suspects_killed","suspects_arrested","suspects_free","officer_casualties",
    "roe_violations","protected_violations","lethal_violations","unlawful_damage","time_to_clear_s","officer_rounds",
    "suspect_rounds","reloads_completed","reloads_interrupted","dry_decisions","episode_s","sim_ms_per_decision","sim_us_per_tick","reset_ms",
    "officer_reloads_under_fire","officer_reloads_disturbed","officer_reload_deaths","officer_reloads_done",
    "suspect_reloads_under_fire","suspect_reloads_disturbed","suspect_reload_deaths","suspect_reloads_done",
};
// Episode-level value of a metric; false when the episode has none (time to
// clear exists only for completed missions).
static bool swat_roles_metric_value(const SwatRolesEvalEpisode* r,int metric,double* value) {
    const SwatRolesEpisode* e=&r->outcome;
    switch(metric) {
    case SWAT_METRIC_COMPLETION: *value=e->success; return true;
    case SWAT_METRIC_CIVILIAN_HARM_RATE: *value=e->civilians_harmed>0; return true;
    case SWAT_METRIC_CIVILIANS_HARMED: *value=e->civilians_harmed; return true;
    case SWAT_METRIC_CIVILIANS_KILLED: *value=e->civilians_killed; return true;
    case SWAT_METRIC_SUSPECTS_KILLED: *value=e->suspects_killed; return true;
    case SWAT_METRIC_SUSPECTS_ARRESTED: *value=e->suspects_arrested; return true;
    case SWAT_METRIC_SUSPECTS_FREE: *value=e->suspects_free; return true;
    case SWAT_METRIC_OFFICERS_DOWN: *value=e->officers_down; return true;
    case SWAT_METRIC_ROE_VIOLATIONS: *value=e->roe_violations; return true;
    case SWAT_METRIC_PROTECTED_VIOLATIONS: *value=e->protected_violations; return true;
    case SWAT_METRIC_LETHAL_VIOLATIONS: *value=e->lethal_violations; return true;
    case SWAT_METRIC_UNLAWFUL_DAMAGE: *value=e->unlawful_damage; return true;
    case SWAT_METRIC_TIME_TO_CLEAR: *value=e->ticks/60.0; return e->success;
    case SWAT_METRIC_OFFICER_ROUNDS: *value=e->officer_rounds; return true;
    case SWAT_METRIC_SUSPECT_ROUNDS: *value=e->suspect_rounds; return true;
    // Reload handling, per officer.
    case SWAT_METRIC_RELOADS: *value=e->officers ? (double)e->officer_reloads/e->officers : 0; return e->officers>0;
    case SWAT_METRIC_RELOAD_INTERRUPTS: *value=e->officers ? (double)e->officer_reload_interrupts/e->officers : 0; return e->officers>0;
    case SWAT_METRIC_DRY_DECISIONS: *value=e->officers ? (double)e->officer_dry_decisions/e->officers : 0; return e->officers>0;
    case SWAT_METRIC_EPISODE_SECONDS: *value=e->ticks/60.0; return true;
    // Sim cost excludes the final decision, whose time is mostly the reset.
    case SWAT_METRIC_STEP_MS: *value=e->decisions>1 ? 1e3*r->seconds/(e->decisions-1) : 0; return e->decisions>1;
    case SWAT_METRIC_TICK_US: *value=e->decisions>1 ? 1e6*r->seconds/(e->ticks*(e->decisions-1.0)/e->decisions) : 0; return e->decisions>1;
    case SWAT_METRIC_RESET_MS: *value=1e3*r->reset_seconds; return true;
    // Reloads under pressure, per officer and per suspect.
    case SWAT_METRIC_OFFICER_RELOADS_UNDER_FIRE: *value=e->officers ? (double)e->officer_reloads_under_fire/e->officers : 0; return e->officers>0;
    case SWAT_METRIC_OFFICER_RELOADS_DISTURBED: *value=e->officers ? (double)e->officer_reloads_disturbed/e->officers : 0; return e->officers>0;
    case SWAT_METRIC_OFFICER_RELOAD_DEATHS: *value=e->officers ? (double)e->officer_reload_deaths/e->officers : 0; return e->officers>0;
    case SWAT_METRIC_OFFICER_RELOADS_DONE: *value=e->officers ? (double)e->officer_reloads_done/e->officers : 0; return e->officers>0;
    case SWAT_METRIC_SUSPECT_RELOADS_UNDER_FIRE: *value=e->suspects ? (double)e->suspect_reloads_under_fire/e->suspects : 0; return e->suspects>0;
    case SWAT_METRIC_SUSPECT_RELOADS_DISTURBED: *value=e->suspects ? (double)e->suspect_reloads_disturbed/e->suspects : 0; return e->suspects>0;
    case SWAT_METRIC_SUSPECT_RELOAD_DEATHS: *value=e->suspects ? (double)e->suspect_reload_deaths/e->suspects : 0; return e->suspects>0;
    case SWAT_METRIC_SUSPECT_RELOADS_DONE: *value=e->suspects ? (double)e->suspect_reloads_done/e->suspects : 0; return e->suspects>0;
    }
    return false;
}
static bool swat_roles_metric_rate(int metric) {
    return metric==SWAT_METRIC_COMPLETION || metric==SWAT_METRIC_CIVILIAN_HARM_RATE;
}
static SwatRolesStat swat_roles_aggregate(const SwatRolesEvalEpisode* runs,int episodes,int metric) {
    double* x=calloc((size_t)(episodes>0 ? episodes : 1),sizeof(double)); int n=0,k=0;
    for(int e=0;e<episodes;e++) { double v; if(swat_roles_metric_value(&runs[e],metric,&v)) { x[n++]=v; k+=v>0; } }
    SwatRolesStat s=swat_roles_metric_rate(metric) ? swat_roles_rate(k,n) : swat_roles_mean(x,n);
    free(x);
    return s;
}

// One finished (case, matchup) run of the report.
typedef struct SwatRolesEvalResult {
    int index; SwatRolesMatchup matchup; const char* policy;
    int episodes; SwatRolesEvalEpisode* runs;
} SwatRolesEvalResult;

static void swat_roles_json_stat(FILE* f,SwatRolesStat s) {
    if(!s.n) { fprintf(f,"{\"mean\":null,\"ci95\":null,\"n\":0}"); return; }
    fprintf(f,"{\"mean\":%.6g,\"ci95\":[%.6g,%.6g],\"n\":%d}",s.mean,s.low,s.high,s.n);
}
static void swat_roles_eval_json(FILE* f,int suite,const SwatRolesEvalResult* results,int count,const char* note) {
    fprintf(f,"{\n  \"suite\": %d,\n  \"seed_stream\": %d,\n  \"roles_contract\": %d,\n  \"note\": \"%s\",\n",
        suite,SWAT_ROLES_EVAL_SEEDS,SWAT_ROLES_VERSION,note ? note : "");
    fprintf(f,"  \"roe\": \"roe_violations merges protected_violations (force on protected people or officers) and lethal force on a person who was not a perceived lethal threat, per officer engagement\",\n");
    fprintf(f,"  \"reloads\": \"reloads_completed, reloads_interrupted and dry_decisions are per officer per episode; dry_decisions counts decisions begun alive with an empty weapon, not reloading, while it had a spare magazine\",\n");
    if(suite>=4) fprintf(f,"  \"reload_pressure\": \"officer_* and suspect_* reload metrics are per officer and per suspect per episode, over every officer and suspect in the sim: reloads started while seeing an armed enemy, reloads broken by damage or a sprint (reload-disturbed event), deaths with a reload in progress, reloads completed\",\n");
    fprintf(f,"  \"ci\": \"95%%: Wilson for completion and civilian_harm_rate, Student t for means\",\n");
    fprintf(f,"  \"timing\": \"sim_* and per-episode seconds are wall clock and not deterministic\",\n  \"runs\": [\n");
    for(int r=0;r<count;r++) {
        const SwatRolesEvalResult* x=&results[r]; const SwatRolesEvalCase* c=&swat_roles_eval_cases[x->index];
        fprintf(f,"    {\"case\": \"%s\", \"mission\": \"%s\", \"occupancy\": %d, \"officers\": %d, \"suspect_escape\": %d, \"max_ticks\": %d, \"officer_magazine\": %d,\n",
            c->name,c->mission,c->occupancy,c->officers,c->escape,c->max_ticks,c->officer_magazine);
        if(suite>=4) fprintf(f,"     \"officer_magazine_min\": %d, \"contact\": %d,\n",c->magazine_min,c->contact);
        fprintf(f,"     \"matchup\": \"%s\", \"policy\": \"%s\", \"episodes\": %d,\n     \"metrics\": {",
            swat_roles_matchup_names[x->matchup],x->policy ? x->policy : "scripted",x->episodes);
        for(int m=0;m<swat_roles_eval_metrics(suite);m++) {
            fprintf(f,"%s\n       \"%s\": ",m ? "," : "",swat_roles_metric_names[m]);
            swat_roles_json_stat(f,swat_roles_aggregate(x->runs,x->episodes,m));
        }
        fprintf(f,"\n     },\n     \"per_episode\": [");
        for(int e=0;e<x->episodes;e++) {
            const SwatRolesEpisode* o=&x->runs[e].outcome;
            fprintf(f,"%s\n       {\"seed\":%u,\"layout_seed\":%u,\"end\":%d,\"success\":%d,\"escaped\":%d,\"ticks\":%d,\"decisions\":%d,"
                "\"civilians\":%d,\"civilians_harmed\":%d,\"civilians_killed\":%d,\"suspects\":%d,\"suspects_killed\":%d,"
                "\"suspects_arrested\":%d,\"suspects_free\":%d,\"officers\":%d,\"officers_down\":%d,\"roe_violations\":%d,"
                "\"protected_violations\":%d,\"lethal_violations\":%d,\"unlawful_damage\":%.3f,\"officer_rounds\":%d,\"suspect_rounds\":%d,"
                "\"officer_reloads\":%d,\"officer_reload_interrupts\":%d,\"officer_dry_decisions\":%d,",
                e ? "," : "",o->seed,o->layout_seed,o->end,o->success,o->escaped,o->ticks,o->decisions,
                o->civilians,o->civilians_harmed,o->civilians_killed,o->suspects,o->suspects_killed,
                o->suspects_arrested,o->suspects_free,o->officers,o->officers_down,o->roe_violations,
                o->protected_violations,o->lethal_violations,o->unlawful_damage,o->officer_rounds,o->suspect_rounds,
                o->officer_reloads,o->officer_reload_interrupts,o->officer_dry_decisions);
            if(suite>=4) fprintf(f,"\"officer_reloads_under_fire\":%d,\"officer_reloads_disturbed\":%d,\"officer_reload_deaths\":%d,\"officer_reloads_done\":%d,"
                "\"suspect_reloads_under_fire\":%d,\"suspect_reloads_disturbed\":%d,\"suspect_reload_deaths\":%d,\"suspect_reloads_done\":%d,",
                o->officer_reloads_under_fire,o->officer_reloads_disturbed,o->officer_reload_deaths,o->officer_reloads_done,
                o->suspect_reloads_under_fire,o->suspect_reloads_disturbed,o->suspect_reload_deaths,o->suspect_reloads_done);
            fprintf(f,"\"seconds\":%.4f,\"reset_seconds\":%.4f}",x->runs[e].seconds,x->runs[e].reset_seconds);
        }
        fprintf(f,"\n     ]}%s\n",r+1<count ? "," : "");
    }
    fprintf(f,"  ]\n}\n");
}

static void swat_roles_md_stat(FILE* f,SwatRolesStat s,double scale,const char* format) {
    if(!s.n) { fprintf(f," n/a |"); return; }
    fputc(' ',f); fprintf(f,format,s.mean*scale);
    fprintf(f," ["); fprintf(f,format,s.low*scale); fprintf(f,", "); fprintf(f,format,s.high*scale); fprintf(f,"] |");
}
static void swat_roles_eval_markdown(FILE* f,int suite,const SwatRolesEvalResult* results,int count,const char* note) {
    fprintf(f,"# SWAT role task held-out evaluation, suite v%d (role contract v%d)\n\n",suite,SWAT_ROLES_VERSION);
    fprintf(f,"Suite v%d plays the cases and seeds of suite v%d, plus two low-ammunition cases in which each officer starts "
              "with %d rounds in the seated primary magazine and full spares. ROE violations count officer engagements with force on "
              "protected people or officers or lethal force on a person who was not a perceived lethal threat (roe.h); "
              "the lethal column counts the second kind alone. Reloads completed, reloads interrupted and dry decisions "
              "(begun alive with an empty weapon, not reloading, while it had a spare magazine) are per officer.\n\n",
              suite,SWAT_ROLES_EVAL_SEEDS,SWAT_ROLES_EVAL_LOW_MAGAZINE);
    if(suite>=4) fprintf(f,"Suite v4 adds two contact reload cases: the officers start with %d to %d rounds in the seated primary magazine "
              "(drawn per episode) and in sight of the nearest suspect. The reload pressure table counts, per officer and per "
              "suspect, reloads started while seeing an armed enemy, reloads broken by damage or a sprint, deaths with a reload "
              "in progress, and reloads completed.\n\n",SWAT_ROLES_EVAL_CONTACT_MAGAZINE_MIN,SWAT_ROLES_EVAL_CONTACT_MAGAZINE_MAX);
    if(note && *note) fprintf(f,"%s\n\n",note);
    fprintf(f,"Means per episode with 95%% confidence intervals (Wilson for rates, Student t for means). "
              "Time to clear averages completed episodes only. Sim cost is wall clock.\n\n");
    fprintf(f,"| Case | Matchup | Policy | N | Completion %% | Civ. harmed (any) %% | Civ. harmed | Suspects killed | Suspects arrested | Officer casualties | ROE violations | Lethal violations | Time to clear s | Officer rounds | Reloads | Reloads interrupted | Dry decisions | Sim ms/decision | Reset ms |\n");
    fprintf(f,"| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |\n");
    for(int r=0;r<count;r++) {
        const SwatRolesEvalResult* x=&results[r];
        fprintf(f,"| %s | %s | %s | %d |",swat_roles_eval_cases[x->index].name,swat_roles_matchup_names[x->matchup],
            x->policy ? x->policy : "scripted",x->episodes);
        static const int metrics[]={SWAT_METRIC_COMPLETION,SWAT_METRIC_CIVILIAN_HARM_RATE,SWAT_METRIC_CIVILIANS_HARMED,
            SWAT_METRIC_SUSPECTS_KILLED,SWAT_METRIC_SUSPECTS_ARRESTED,SWAT_METRIC_OFFICERS_DOWN,SWAT_METRIC_ROE_VIOLATIONS,
            SWAT_METRIC_LETHAL_VIOLATIONS,SWAT_METRIC_TIME_TO_CLEAR,SWAT_METRIC_OFFICER_ROUNDS,SWAT_METRIC_RELOADS,
            SWAT_METRIC_RELOAD_INTERRUPTS,SWAT_METRIC_DRY_DECISIONS,SWAT_METRIC_STEP_MS,SWAT_METRIC_RESET_MS};
        for(int i=0;i<(int)(sizeof(metrics)/sizeof(metrics[0]));i++) {
            int m=metrics[i];
            swat_roles_md_stat(f,swat_roles_aggregate(x->runs,x->episodes,m),swat_roles_metric_rate(m) ? 100 : 1,
                swat_roles_metric_rate(m) ? "%.0f" : m==SWAT_METRIC_TIME_TO_CLEAR || m==SWAT_METRIC_OFFICER_ROUNDS || m==SWAT_METRIC_RESET_MS ? "%.1f" : "%.2f");
        }
        fputc('\n',f);
    }
    if(suite<4) return;
    fprintf(f,"\n## Reloads under pressure\n\n| Case | Matchup | Policy | N | Officer reloads under fire | Officer reloads disturbed | Officer deaths reloading | Officer reloads done |"
              " Suspect reloads under fire | Suspect reloads disturbed | Suspect deaths reloading | Suspect reloads done |\n");
    fprintf(f,"| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |\n");
    for(int r=0;r<count;r++) {
        const SwatRolesEvalResult* x=&results[r];
        fprintf(f,"| %s | %s | %s | %d |",swat_roles_eval_cases[x->index].name,swat_roles_matchup_names[x->matchup],
            x->policy ? x->policy : "scripted",x->episodes);
        for(int m=SWAT_METRIC_OFFICER_RELOADS_UNDER_FIRE;m<SWAT_METRICS;m++) swat_roles_md_stat(f,swat_roles_aggregate(x->runs,x->episodes,m),1,"%.2f");
        fputc('\n',f);
    }
}
#endif
