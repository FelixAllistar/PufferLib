// Runs the held-out role task evaluation suite (roles_eval.h) on CPU and
// writes a JSON report and a Markdown table. Without a checkpoint it measures
// the scripted baselines (all roles scripted) and a uniform legal-random
// officer floor; with --checkpoint it also runs a native role checkpoint as
// officers against scripted occupants and as occupants against scripted
// officers. The checkpoint is the trainer's flat FP32 export, evaluated with
// the shared CPU inference in src/puffercpu.c (argmax actions under the
// role mask), so no trainer code is needed.
#define SWAT_ROLES_TRAINING
#define SWAT_ROLES_NO_RENDER
#include "swat.h"
#include "roles_eval.h"
#include "../../src/puffercpu.c"
// OpenMP runs jobs in parallel when available; without it the tool is serial.
#ifdef _OPENMP
#include <omp.h>
#else
static void omp_set_num_threads(int threads) { (void)threads; }
static int omp_get_max_threads(void) { return 1; }
#endif

// Native role checkpoint: one PufferNet whose batch is the env's agents,
// rebuilt when a case has a different agent count.
typedef struct SwatRolesNative {
    Weights* weights; PufferNet* net;
    int hidden,layers;
} SwatRolesNative;

static bool swat_roles_native_load(SwatRolesNative* p,const char* path,int hidden,int layers) {
    if(hidden<8 || hidden%8 || layers<1) { fprintf(stderr,"roles_eval: --hidden must be a positive multiple of 8 and --layers positive\n"); return false; }
    long long expected=(long long)SWAT_ROLES_OBS*hidden+(long long)(SWAT_ROLES_LOGITS+1)*hidden+(long long)layers*3*hidden*hidden;
    FILE* file=fopen(path,"rb");
    if(!file) { fprintf(stderr,"roles_eval: cannot read checkpoint %s\n",path); return false; }
    fclose(file);
    Weights* weights=load_weights(path);
    if(!weights || weights->size-7!=expected) {
        fprintf(stderr,"roles_eval: incompatible checkpoint %s: expected %lld FP32 weights for role contract v%d, hidden=%d, layers=%d\n",
            path,expected,SWAT_ROLES_VERSION,hidden,layers);
        free(weights); return false;
    }
    for(long long i=0;i<expected;i++) if(!isfinite(weights->data[i])) { fprintf(stderr,"roles_eval: non-finite weight in %s\n",path); free(weights); return false; }
    p->weights=weights; p->net=NULL; p->hidden=hidden; p->layers=layers;
    return true;
}
static void swat_roles_native_begin(void* context,int agents,uint32_t seed) {
    SwatRolesNative* p=context;
    if(!p->net || p->net->num_agents!=agents) {
        if(p->net) free_puffernet(p->net);
        int sizes[]=SWAT_ROLES_ACTION_SIZES;
        p->weights->idx=0;
        p->net=make_puffernet(p->weights,agents,SWAT_ROLES_OBS,p->hidden,p->layers,sizes,SWAT_ROLES_HEADS);
    }
    float terminals[SWAT_ROLES_MAX_AGENTS]; for(int i=0;i<SWAT_ROLES_MAX_AGENTS;i++) terminals[i]=1;
    mingru_zero_term(p->net->mingru,terminals);
}
static void swat_roles_native_act(void* context,int agents,const float* observations,const unsigned char* masks,float* actions) {
    SwatRolesNative* p=context; PufferNet* net=p->net;
    linear(net->encoder,(float*)observations);
    mingru(net->mingru,net->encoder->output);
    linear(net->decoder,net->mingru->output);
    multidiscrete(net->multidiscrete,net->decoder->output,actions,1,masks);
}

static void usage(const char* path) {
    fprintf(stderr,
        "usage: %s [--json FILE] [--markdown FILE] [--suite N] [--episodes N] [--case NAME] [--max-ticks N]\n"
        "          [--checkpoint FILE.bin --hidden 1024 --layers 2] [--no-random] [--threads N] [--note TEXT]\n"
        "Runs held-out role suite v%d, or with --suite 3 the cases and metrics of suite v3. --episodes and\n"
        "--max-ticks shorten it for smoke runs only; reported baselines use the suite's own counts and\n"
        "limits. --case may repeat.\n",path,SWAT_ROLES_EVAL_SUITE);
}

int main(int argc,char** argv) {
    const char* json="roles_eval.json"; const char* markdown="roles_eval.md"; const char* checkpoint=NULL; const char* note="";
    int episodes=0,max_ticks=0,hidden=1024,layers=2,threads=0,suite=SWAT_ROLES_EVAL_SUITE; bool random_floor=true;
    bool selected[SWAT_ROLES_EVAL_CASES]={0}; bool any=false;
    for(int i=1;i<argc;i++) {
        const char* a=argv[i]; const char* v=i+1<argc ? argv[i+1] : NULL;
        if(!strcmp(a,"--no-random")) { random_floor=false; continue; }
        if(!v) { usage(argv[0]); return 2; }
        if(!strcmp(a,"--json")) json=v;
        else if(!strcmp(a,"--markdown")) markdown=v;
        else if(!strcmp(a,"--checkpoint")) checkpoint=v;
        else if(!strcmp(a,"--note")) note=v;
        else if(!strcmp(a,"--episodes")) episodes=atoi(v);
        else if(!strcmp(a,"--max-ticks")) max_ticks=atoi(v);
        else if(!strcmp(a,"--hidden")) hidden=atoi(v);
        else if(!strcmp(a,"--layers")) layers=atoi(v);
        else if(!strcmp(a,"--threads")) threads=atoi(v);
        else if(!strcmp(a,"--suite")) suite=atoi(v);
        else if(!strcmp(a,"--case")) {
            int found=-1; for(int c=0;c<SWAT_ROLES_EVAL_CASES;c++) if(!strcmp(v,swat_roles_eval_cases[c].name)) found=c;
            if(found>=swat_roles_eval_suite_cases(suite)) { fprintf(stderr,"roles_eval: case %s is not in suite v%d (give --suite first)\n",v,suite); return 2; }
            if(found<0) { fprintf(stderr,"roles_eval: unknown case %s\n",v); return 2; }
            selected[found]=any=true;
        } else { usage(argv[0]); return 2; }
        i++;
    }
    int cases=swat_roles_eval_suite_cases(suite);
    if(episodes<0 || max_ticks<0 || threads<0 || !cases) { usage(argv[0]); return 2; }
    if(threads) omp_set_num_threads(threads);
    if(!any) for(int c=0;c<cases;c++) selected[c]=true;

    SwatRolesNative native={0};
    if(checkpoint && !swat_roles_native_load(&native,checkpoint,hidden,layers)) return 1;

    // Jobs: (case, matchup, policy). Each job owns its env and policy state,
    // so jobs run in parallel and results do not depend on scheduling.
    enum { MAX_JOBS=SWAT_ROLES_EVAL_CASES*4 };
    SwatRolesEvalResult results[MAX_JOBS]; int kinds[MAX_JOBS]; int count=0;
    for(int c=0;c<SWAT_ROLES_EVAL_CASES;c++) if(selected[c]) {
        int n=episodes>0 ? episodes : swat_roles_eval_cases[c].episodes;
        results[count]=(SwatRolesEvalResult){c,SWAT_MATCHUP_SCRIPTED,NULL,n,NULL}; kinds[count++]=0;
        if(random_floor) { results[count]=(SwatRolesEvalResult){c,SWAT_MATCHUP_POLICY_OFFICERS,"random",n,NULL}; kinds[count++]=1; }
        if(checkpoint) {
            results[count]=(SwatRolesEvalResult){c,SWAT_MATCHUP_POLICY_OFFICERS,checkpoint,n,NULL}; kinds[count++]=2;
            results[count]=(SwatRolesEvalResult){c,SWAT_MATCHUP_POLICY_OCCUPANTS,checkpoint,n,NULL}; kinds[count++]=2;
        }
    }
    double start=swat_roles_eval_now();
    #pragma omp parallel for schedule(dynamic,1)
    for(int j=0;j<count;j++) {
        SwatRolesEvalResult* r=&results[j];
        r->runs=calloc((size_t)r->episodes,sizeof(SwatRolesEvalEpisode));
        SwatRolesRandom random={0}; SwatRolesNative own=native; own.net=NULL;
        // Each job builds its own network over a private copy of the weights.
        if(kinds[j]==2) {
            own.weights=malloc(sizeof(Weights)); *own.weights=*native.weights;
        }
        SwatRolesPolicy policy=kinds[j]==1 ? (SwatRolesPolicy){"random",&random,swat_roles_random_begin,swat_roles_random_act} :
            (SwatRolesPolicy){r->policy,&own,swat_roles_native_begin,swat_roles_native_act};
        swat_roles_eval_run(r->index,r->matchup,kinds[j] ? &policy : NULL,r->episodes,max_ticks,r->runs);
        if(kinds[j]==2) { if(own.net) free_puffernet(own.net); free(own.weights); }
        #pragma omp critical(swat_roles_eval_log)
        fprintf(stderr,"roles_eval: %s %s %s: %d episodes, completion %.2f\n",swat_roles_eval_cases[r->index].name,
            swat_roles_matchup_names[r->matchup],r->policy ? r->policy : "scripted",r->episodes,
            swat_roles_aggregate(r->runs,r->episodes,SWAT_METRIC_COMPLETION).mean);
    }
    double elapsed=swat_roles_eval_now()-start;
    char full[512];
    snprintf(full,sizeof(full),"%s%s%d jobs in %.0f s wall on %d threads%s%s",note,*note ? " " : "",count,elapsed,
        threads ? threads : omp_get_max_threads(),max_ticks ? "; max_ticks overridden (smoke run)" : "",
        episodes ? "; episode count overridden (smoke run)" : "");
    FILE* f=fopen(json,"w"); if(!f) { fprintf(stderr,"roles_eval: cannot write %s\n",json); return 1; }
    swat_roles_eval_json(f,suite,results,count,full); fclose(f);
    f=fopen(markdown,"w"); if(!f) { fprintf(stderr,"roles_eval: cannot write %s\n",markdown); return 1; }
    swat_roles_eval_markdown(f,suite,results,count,full); fclose(f);
    fprintf(stderr,"roles_eval: wrote %s and %s (%.0f s)\n",json,markdown,elapsed);
    for(int j=0;j<count;j++) free(results[j].runs);
    if(native.weights) free(native.weights);
    return 0;
}
