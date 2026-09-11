// Historical standalone and approximate-port diagnostics (RETRO_LEGACY=1).
// ./retro               -> human play (arrows + Z/X)
// ./retro play          -> human play
// ./retro watch [latest|PATH.bin] [--deterministic]

#include "retro.h"
#include "puffercpu.h"
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#ifdef __cplusplus
#include <omp.h>
#endif

static const char* RETRO_ENV_NAME = "retro";

static int retro_has_suffix(const char* s, const char* suffix){
    size_t n=strlen(s), m=strlen(suffix);
    return n>=m && strcmp(s+n-m,suffix)==0;
}
static void retro_find_latest(const char* dir, char* out, size_t out_size, time_t* best){
    DIR* dp=opendir(dir);
    if(!dp) return;
    struct dirent* ent;
    while((ent=readdir(dp))){
        if(strcmp(ent->d_name,".")==0 || strcmp(ent->d_name,"..")==0) continue;
        char path[4096];
        snprintf(path,sizeof(path),"%s/%s",dir,ent->d_name);
        struct stat st;
        if(stat(path,&st)!=0) continue;
        if(S_ISDIR(st.st_mode)){
            retro_find_latest(path,out,out_size,best);
        } else if(S_ISREG(st.st_mode) && retro_has_suffix(path,".bin") && st.st_ctime >= *best){
            *best=st.st_ctime;
            snprintf(out,out_size,"%s",path);
        }
    }
    closedir(dp);
}
static int retro_resolve_model(const char* arg, char* out, size_t out_size){
    if(!arg || !*arg || strcmp(arg,"latest")==0){
        const char* root="checkpoints/retro";
        out[0]=0; time_t best=0;
        retro_find_latest(root,out,out_size,&best);
        if(!out[0]){ fprintf(stderr,"no .bin checkpoints found in %s\n",root); return -1; }
        return 0;
    }
    snprintf(out,out_size,"%s",arg);
    return 0;
}
static void retro_print_usage(const char* a0){
    fprintf(stderr,"usage:\n  %s                 human play\n  %s play            human play\n  %s watch [latest|PATH.bin] [--deterministic] [--random|--continue]\n\nRun from repo root. Watch reads policy from config/retro.ini\n  --random     spawn a random level each episode\n  --continue   SMB rules: clear a level -> next one, die -> retry it\n",a0,a0,a0);
}

#ifdef __cplusplus
static double retro_now_seconds(){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static int retro_benchmark(const char* a0, int argc, char** argv){
    int env_count = argc > 2 ? atoi(argv[2]) : 512;
    int steps = argc > 3 ? atoi(argv[3]) : 100;
    int workers = argc > 4 ? atoi(argv[4]) : 1;
    const char* backend_ov = argc > 5 ? argv[5] : NULL;
    if(env_count < 1 || steps < 1 || workers < 1){
        fprintf(stderr,"usage: %s bench [envs] [steps] [workers] [quicknes|fast]\n",a0);
        return 1;
    }

    Ini ini={0};
    puf_ini_load_env(&ini, RETRO_ENV_NAME, 0, NULL);
    if(backend_ov) puf_ini_put(&ini, "env.backend", backend_ov);
    Dict* env_kwargs=puf_ini_section(&ini,"env",0);
    DictItem* be_it=dict_find(env_kwargs,"backend");
    int use_fast = (be_it && be_it->str && strcmp(be_it->str,"fast")==0) ? 1 : 0;
    Env* envs=(Env*)calloc((size_t)env_count,sizeof(Env));
    Nes_Emu* emus=NULL;
    RetroFastArena farena={0};
    if(use_fast){
        if(!fast_arena_alloc(&farena, env_count)){
            fprintf(stderr,"benchmark fast arena alloc failed\n");
            free(envs); puf_ini_free(&ini);
            return 1;
        }
        fast_hb_init(env_count);
    } else {
        emus=new Nes_Emu[env_count];
    }
    float* observations=(float*)calloc((size_t)env_count*OBS_SIZE,sizeof(float));
    float* actions=(float*)calloc((size_t)env_count*NUM_ATNS,sizeof(float));
    float* rewards=(float*)calloc((size_t)env_count,sizeof(float));
    float* terminals=(float*)calloc((size_t)env_count,sizeof(float));
    if(!envs || !observations || !actions || !rewards || !terminals){
        fprintf(stderr,"benchmark allocation failed\n");
        free(envs); delete[] emus; free(observations); free(actions);
        free(rewards); free(terminals); puf_ini_free(&ini);
        return 1;
    }

    for(int i=0;i<env_count;i++){
        envs[i].rng=(unsigned int)i;
        if(use_fast){
            envs[i].fast=&farena.st[i];
            envs[i].fast_owned=false;
            envs[i].fast_arena=&farena;
            envs[i].fast_idx=i;
            envs[i].fast_clone_src=(i>0)?0:-1;
        } else {
            envs[i].emu=&emus[i];
            envs[i].emu_owned=false;
        }
        puf_init(&envs[i],env_kwargs);
        envs[i].agents[0].observations=observations+(size_t)i*OBS_SIZE;
        envs[i].agents[0].actions=actions+(size_t)i*NUM_ATNS;
        envs[i].agents[0].rewards=rewards+i;
        envs[i].agents[0].terminals=terminals+i;
        envs[i].agents[0].action_mask=nullptr;
        envs[i].agents[0].policy=0;
        actions[i]=1;
        puf_reset(&envs[i]);
    }

    omp_set_dynamic(0);
    double start=retro_now_seconds();
    for(int step=0;step<steps;step++){
        #pragma omp parallel for schedule(static) num_threads(workers)
        for(int i=0;i<env_count;i++) puf_step(&envs[i]);
    }
    double elapsed=retro_now_seconds()-start;
    long long checksum=0;
    for(int i=0;i<env_count;i++) checksum+=envs[i].x_pos+envs[i].score+envs[i].tick;
    fprintf(stderr,"bench backend=%s envs=%d steps=%d workers=%d elapsed=%.3f env_steps/s=%.0f frames/s=%.0f checksum=%lld\n",
        use_fast?"fast":"quicknes",
        env_count,steps,workers,elapsed,
        (double)env_count*steps/elapsed,
        (double)env_count*steps*envs[0].frameskip/elapsed,checksum);

    for(int i=0;i<env_count;i++) puf_close(&envs[i]);
    if(emus) delete[] emus;
    fast_arena_free(&farena);
    free(envs); free(observations); free(actions);
    free(rewards); free(terminals); puf_ini_free(&ini);
    return 0;
}

// Chaos monkey: random actions every step (xorshift per env, deterministic).
// Hunts hang/freeze triggers that benign action=1 scripts never reach.
// Usage: chaos [envs] [steps] [workers] [quicknes|fast] [seed]
static unsigned int retro_xorshift(unsigned int* s){
    unsigned int x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x;
    return x;
}

static int retro_chaos(const char* a0, int argc, char** argv){
    int env_count = argc > 2 ? atoi(argv[2]) : 512;
    int steps = argc > 3 ? atoi(argv[3]) : 500;
    int workers = argc > 4 ? atoi(argv[4]) : 1;
    const char* backend_ov = argc > 5 ? argv[5] : NULL;
    unsigned int seed = argc > 6 ? (unsigned int)atoi(argv[6]) : 12345u;
    if(env_count < 1 || steps < 1 || workers < 1){
        fprintf(stderr,"usage: %s chaos [envs] [steps] [workers] [quicknes|fast] [seed]\n",a0);
        return 1;
    }

    Ini ini={0};
    puf_ini_load_env(&ini, RETRO_ENV_NAME, 0, NULL);
    if(backend_ov) puf_ini_put(&ini, "env.backend", backend_ov);
    Dict* env_kwargs=puf_ini_section(&ini,"env",0);
    DictItem* be_it=dict_find(env_kwargs,"backend");
    int use_fast = (be_it && be_it->str && strcmp(be_it->str,"fast")==0) ? 1 : 0;
    Env* envs=(Env*)calloc((size_t)env_count,sizeof(Env));
    Nes_Emu* emus=NULL;
    RetroFastArena farena={0};
    if(use_fast){
        if(!fast_arena_alloc(&farena, env_count)){
            fprintf(stderr,"chaos fast arena alloc failed\n");
            free(envs); puf_ini_free(&ini);
            return 1;
        }
        fast_hb_init(env_count);
    } else {
        emus=new Nes_Emu[env_count];
    }
    float* observations=(float*)calloc((size_t)env_count*OBS_SIZE,sizeof(float));
    float* actions=(float*)calloc((size_t)env_count*NUM_ATNS,sizeof(float));
    float* rewards=(float*)calloc((size_t)env_count,sizeof(float));
    float* terminals=(float*)calloc((size_t)env_count,sizeof(float));
    if(!envs || !observations || !actions || !rewards || !terminals){
        fprintf(stderr,"chaos allocation failed\n");
        free(envs); if(emus) delete[] emus; fast_arena_free(&farena);
        free(observations); free(actions);
        free(rewards); free(terminals); puf_ini_free(&ini);
        return 1;
    }

    for(int i=0;i<env_count;i++){
        envs[i].rng=(unsigned int)(i*2654435761u + seed);
        if(use_fast){
            envs[i].fast=&farena.st[i];
            envs[i].fast_owned=false;
            envs[i].fast_arena=&farena;
            envs[i].fast_idx=i;
            envs[i].fast_clone_src=(i>0)?0:-1;
        } else {
            envs[i].emu=&emus[i];
            envs[i].emu_owned=false;
        }
        puf_init(&envs[i],env_kwargs);
        envs[i].agents[0].observations=observations+(size_t)i*OBS_SIZE;
        envs[i].agents[0].actions=actions+(size_t)i*NUM_ATNS;
        envs[i].agents[0].rewards=rewards+i;
        envs[i].agents[0].terminals=terminals+i;
        envs[i].agents[0].action_mask=nullptr;
        envs[i].agents[0].policy=0;
        puf_reset(&envs[i]);
        // Reseed AFTER init: puf_init memsets Env, wiping pre-set rng (which
        // silently collapsed all envs onto one identical action stream).
        envs[i].rng=(unsigned int)(i*2654435761u + seed);
    }

    omp_set_dynamic(0);
    int report_every = steps/10 > 0 ? steps/10 : 1;
    double start=retro_now_seconds();
    double rsum=0;
    for(int step=0;step<steps;step++){
        for(int i=0;i<env_count;i++)
            actions[i]=(float)(retro_xorshift(&envs[i].rng)%RETRO_NUM_ACTIONS);
        #pragma omp parallel for schedule(static) num_threads(workers)
        for(int i=0;i<env_count;i++) puf_step(&envs[i]);
        for(int i=0;i<env_count;i++) rsum+=rewards[i];
        if((step+1)%report_every==0 || step+1==steps){
            double deaths=0, flags=0;
            for(int i=0;i<env_count;i++){ deaths+=envs[i].log.deaths; flags+=envs[i].log.flag; }
            fprintf(stderr,"chaos backend=%s step %d/%d elapsed=%.1fs deaths=%.0f flags=%.0f\n",
                use_fast?"fast":"quicknes", step+1, steps, retro_now_seconds()-start, deaths, flags);
        }
    }
    double elapsed=retro_now_seconds()-start;
    long long checksum=0;
    double deaths=0, flags=0, dist=0, wsum=0;
    for(int i=0;i<env_count;i++){
        checksum+=envs[i].x_pos+envs[i].score+envs[i].tick;
        deaths+=envs[i].log.deaths; flags+=envs[i].log.flag; dist+=envs[i].log.distance;
        wsum+=envs[i].world;
    }
    fprintf(stderr,"chaos backend=%s envs=%d steps=%d workers=%d seed=%u elapsed=%.3f env_steps/s=%.0f checksum=%lld deaths=%.0f flags=%.0f avgdist=%.0f meanrew=%.5f avgworld=%.2f\n",
        use_fast?"fast":"quicknes",
        env_count,steps,workers,seed,elapsed,
        (double)env_count*steps/elapsed,checksum,deaths,flags,dist/env_count,
        rsum/((double)env_count*steps), wsum/env_count);

    for(int i=0;i<env_count;i++) puf_close(&envs[i]);
    if(emus) delete[] emus;
    fast_arena_free(&farena);
    free(envs); free(observations); free(actions);
    free(rewards); free(terminals); puf_ini_free(&ini);
    return 0;
}

// ---- Differential parity tool: fast vs QuickNES + fast self-determinism ----
// Drives ONE precomputed action tape through: quicknes env0, fast env1,
// fast env2 (same tape as env1 -> must be bit-identical), and compares
// landmark traces. Cross-backend: the backends differ by a documented
// ~1-frame input/physics phase offset, so we compare with tolerance and
// report worst deltas instead of demanding bit equality.
// Also dumps the death-jingle window (engine/music state per step) for
// both backends at the first dying transition -- the calibration data for
// the fast backend's music clock.
typedef struct { int step,x,eng,lives,coins,score,ws,evmus,evq,lencnt; } ParSample;

static uint8_t* parity_ram(Env* e, int fast){
    if(fast) return SMB_ram(e->fast);
    return e->emu ? e->emu->low_mem() : NULL;
}

static int retro_parity(const char* a0, int argc, char** argv){
    int steps = argc > 2 ? atoi(argv[2]) : 800;
    unsigned int seed = argc > 3 ? (unsigned int)atoi(argv[3]) : 7u;
    if(steps < 10){ fprintf(stderr,"usage: %s parity [steps] [seed]\n",a0); return 1; }

    Ini ini={0};
    puf_ini_load_env(&ini, RETRO_ENV_NAME, 0, NULL);
    puf_ini_put(&ini, "env.backend", "fast"); // puf_init picks via kwargs per env? no: single kwarg dict
    Dict* env_kwargs=puf_ini_section(&ini,"env",0);
    DictItem* be_it=dict_find(env_kwargs,"backend");
    int use_fast = (be_it && be_it->str && strcmp(be_it->str,"fast")==0) ? 1 : 0;
    (void)use_fast;
    // Force default quicknes kwarg off: we override backend per env below by
    // direct field setup, like bench does for a single backend. Here we need
    // BOTH: create 3 envs; env0 quicknes, env1/2 fast.
    puf_ini_put(&ini, "env.backend", "quicknes");
    env_kwargs=puf_ini_section(&ini,"env",0);

    const int NENV=3;
    Env* envs=(Env*)calloc(NENV,sizeof(Env));
    Nes_Emu* emus=new Nes_Emu[1];
    RetroFastArena farena={0};
    if(!fast_arena_alloc(&farena, 2)){
        fprintf(stderr,"parity fast arena alloc failed\n");
        free(envs); delete[] emus; puf_ini_free(&ini);
        return 1;
    }
    fast_hb_init(2);
    float* observations=(float*)calloc(NENV*OBS_SIZE,sizeof(float));
    float* actions=(float*)calloc(NENV*NUM_ATNS,sizeof(float));
    float* rewards=(float*)calloc(NENV,sizeof(float));
    float* terminals=(float*)calloc(NENV,sizeof(float));

    // one shared deterministic action tape. Optional 4th arg "scripted":
    // constant run-right for 400 steps (guaranteed first-goomba death in
    // both backends -> aligned death-jingle calibration windows).
    int scripted = (argc > 4 && !strcmp(argv[4], "scripted"));
    unsigned int rng=seed*2654435761u+1u;
    unsigned char* tape=(unsigned char*)malloc((size_t)steps);
    for(int s=0;s<steps;s++){
        if(scripted && s<400){ tape[s]=2; continue; } // run right
        rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; tape[s]=(unsigned char)(rng%RETRO_NUM_ACTIONS);
    }

    for(int i=0;i<NENV;i++){
        // puf_init reads env.backend from kwargs: quicknes for env0, fast
        // for env1/2 (env2 clones env1's boot for the determinism check).
        puf_ini_put(&ini, "env.backend", i==0 ? "quicknes" : "fast");
        env_kwargs=puf_ini_section(&ini,"env",0);
        envs[i].rng=12345u+i;
        if(i==0){
            envs[i].emu=&emus[0]; envs[i].emu_owned=false;
        } else {
            envs[i].fast=&farena.st[i-1];
            envs[i].fast_owned=false;
            envs[i].fast_arena=&farena;
            envs[i].fast_idx=i-1;
            envs[i].fast_clone_src=-1; // both fast envs boot fresh: clone vs
            // boot has a documented ~1px skew that would false-fail the
            // determinism check (determinism = same procedure twice).
        }
        puf_init(&envs[i],env_kwargs);
        envs[i].agents[0].observations=observations+(size_t)i*OBS_SIZE;
        envs[i].agents[0].actions=actions+(size_t)i*NUM_ATNS;
        envs[i].agents[0].rewards=rewards+i;
        envs[i].agents[0].terminals=terminals+i;
        envs[i].agents[0].action_mask=nullptr;
        envs[i].agents[0].policy=0;
        puf_reset(&envs[i]);
        envs[i].rng=12345u+i;
    }

    ParSample* tr[3];
    for(int i=0;i<3;i++) tr[i]=(ParSample*)calloc((size_t)steps,sizeof(ParSample));

    double start=retro_now_seconds();
    for(int s=0;s<steps;s++){
        for(int i=0;i<NENV;i++){
            envs[i].agents[0].actions[0]=(float)tape[s];
            puf_step(&envs[i]);
            uint8_t* m=parity_ram(&envs[i], envs[i].use_fast);
            ParSample* t=&tr[i][s];
            t->step=s;
            t->x = m ? m[0x6D]*256+m[0x86] : -1;
            t->eng = m ? m[0x000E] : -1;
            t->lives = m ? m[0x075A] : -1;
            t->coins = m ? ((m[0x07ED]%10)*10+(m[0x07EE]%10)) : -1;
            t->score = m ? robs_score(m) : -1;
            t->ws = m ? (m[0x075F]*16+m[0x075C]) : -1;
            t->evmus = m ? m[0x07B1] : -1;
            t->evq = m ? m[0x00FC] : -1;
            t->lencnt = m ? m[0x07B4] : -1;
        }
    }
    double elapsed=retro_now_seconds()-start;

    // 1) fast self-determinism: env1 vs env2 must be bit-identical
    int det_mism=0; int first_det=-1;
    for(int s=0;s<steps;s++){
        ParSample*a=&tr[1][s]; ParSample*b=&tr[2][s];
        if(a->x!=b->x||a->eng!=b->eng||a->lives!=b->lives||a->coins!=b->coins||
           a->score!=b->score||a->ws!=b->ws||a->evmus!=b->evmus||a->evq!=b->evq||a->lencnt!=b->lencnt){
            det_mism++; if(first_det<0) first_det=s;
        }
    }
    // 2) cross-backend: tolerance comparison fast(env1) vs quicknes(env0)
    int n_cmp=0,x_bad=0,eng_bad=0,stat_bad=0;
    int max_dx=0; int first_x_bad=-1;
    for(int s=0;s<steps;s++){
        ParSample*a=&tr[0][s]; ParSample*b=&tr[1][s];
        n_cmp++;
        int dx=b->x-a->x; if(dx<0)dx=-dx; if(dx>max_dx)max_dx=dx;
        if(dx>32){ x_bad++; if(first_x_bad<0) first_x_bad=s; }
        if(b->eng!=a->eng) eng_bad++;
        if(b->lives!=a->lives||b->coins!=a->coins||b->score!=a->score||b->ws!=a->ws) stat_bad++;
    }
    fprintf(stderr,"parity steps=%d seed=%u elapsed=%.2fs\n",steps,seed,elapsed);
    fprintf(stderr,"parity fast-determinism: %s (mismatches=%d first=%d)\n",
        det_mism==0?"PASS":"FAIL",det_mism,first_det);
    fprintf(stderr,"parity cross-backend: n=%d x-drift>32px: %d (max_dx=%d first=%d) eng-mismatch: %d (%.1f%%) stat-mismatch: %d (%.1f%%)\n",
        n_cmp,x_bad,max_dx,first_x_bad,eng_bad,100.0*eng_bad/n_cmp,stat_bad,100.0*stat_bad/n_cmp);
    int cross_pass = det_mism==0 && (double)eng_bad/n_cmp<0.05 && (double)stat_bad/n_cmp<0.05;
    fprintf(stderr,"parity verdict: %s\n", cross_pass?"PASS":"FAIL");

    // 3) death-jingle calibration window: first transition into eng=0x0B
    for(int be=0; be<2; be++){
        int dj=-1;
        for(int s=1;s<steps;s++) if(tr[be][s].eng==0x0B && tr[be][s-1].eng!=0x0B){ dj=s; break; }
        if(dj<0){ fprintf(stderr,"parity: backend %d saw no death\n", be); continue; }
        int lo=dj>2?dj-2:0; int hi=dj+80<steps?dj+80:steps;
        fprintf(stderr,"parity backend=%s death window (step eng evmus evq lencnt):\n",
            be==0?"quicknes":"fast");
        for(int s=lo;s<hi;s++)
            fprintf(stderr,"  s=%4d %02x %02x %02x %02x\n",
                s, tr[be][s].eng,tr[be][s].evmus,tr[be][s].evq,tr[be][s].lencnt);
    }

    for(int i=0;i<NENV;i++) puf_close(&envs[i]);
    fast_arena_free(&farena);
    delete[] emus;
    free(envs); free(observations); free(actions);
    free(rewards); free(terminals); free(tr[0]); free(tr[1]); free(tr[2]); free(tape);
    puf_ini_free(&ini);
    return cross_pass?0:2;
}
#endif

static Weights* g_weights=nullptr;
static void retro_policy_arch(int* hidden,int* layers){
    Ini ini={0};
    puf_ini_load_env(&ini, RETRO_ENV_NAME, 0, NULL);
    *hidden=puf_ini_get_int(&ini,"policy","hidden_size");
    *layers=puf_ini_get_int(&ini,"policy","num_layers");
    puf_ini_free(&ini);
}
static PufferNet* retro_load_policy(const char* path){
    Weights* w=load_weights(path);
    if(!w){ fprintf(stderr,"failed to load %s\n",path); return nullptr; }
    int act_sizes[] = ACT_SIZES;
    int num_actions = (int)(sizeof(act_sizes)/sizeof(act_sizes[0]));
    int hidden=128, layers=4;
    retro_policy_arch(&hidden,&layers);
    PufferNet* net=make_puffernet(w,1,OBS_SIZE,hidden,layers,act_sizes,num_actions);
    fprintf(stderr,"watch: %s (hidden=%d layers=%d)\n",path,hidden,layers);
    g_weights=w;
    return net;
}
static int retro_human_action(){
    int right = IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D);
    int left  = IsKeyDown(KEY_LEFT)  || IsKeyDown(KEY_A);
    int up    = IsKeyDown(KEY_UP)    || IsKeyDown(KEY_W);
    int down  = IsKeyDown(KEY_DOWN)  || IsKeyDown(KEY_S);
    int a_btn = IsKeyDown(KEY_X) || IsKeyDown(KEY_SPACE);
    int b_btn = IsKeyDown(KEY_C) || IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_Z);
    unsigned char mask=0;
    if(right) mask|=RETRO_BTN_RIGHT;
    if(left)  mask|=RETRO_BTN_LEFT;
    if(up)    mask|=RETRO_BTN_UP;
    if(down)  mask|=RETRO_BTN_DOWN;
    if(a_btn) mask|=RETRO_BTN_A;
    if(b_btn) mask|=RETRO_BTN_B;
    for(int i=0;i<RETRO_NUM_ACTIONS;i++) if(RETRO_ACTION_MASKS[i]==mask) return i;
    if((mask & RETRO_BTN_RIGHT) && (mask & RETRO_BTN_A) && (mask & RETRO_BTN_B)) return 4;
    if((mask & RETRO_BTN_RIGHT) && (mask & RETRO_BTN_A)) return 2;
    if((mask & RETRO_BTN_RIGHT) && (mask & RETRO_BTN_B)) return 3;
    if(mask & RETRO_BTN_RIGHT) return 1;
    if(mask & RETRO_BTN_LEFT) return 6;
    if(mask & RETRO_BTN_A) return 5;
    if(mask & RETRO_BTN_B) return 9;
    if(mask & RETRO_BTN_DOWN) return 8;
    return 0;
}
int main(int argc, char** argv){
    int watch_mode=0;
    int watch_deterministic=0;
    int watch_random=0;
    int watch_continue=0;
    const char* model_arg=nullptr;
    if(argc>=2){
        if(strcmp(argv[1],"bench")==0){
#ifdef __cplusplus
            return retro_benchmark(argv[0],argc,argv);
#else
            fprintf(stderr,"benchmark requires the C++ standalone build\n");
            return 1;
#endif
        } else if(strcmp(argv[1],"chaos")==0){
#ifdef __cplusplus
            return retro_chaos(argv[0],argc,argv);
#else
            fprintf(stderr,"chaos requires the C++ standalone build\n");
            return 1;
#endif
        } else if(strcmp(argv[1],"parity")==0){
#ifdef __cplusplus
            return retro_parity(argv[0],argc,argv);
#else
            fprintf(stderr,"parity requires the C++ standalone build\n");
            return 1;
#endif
        } else if(strcmp(argv[1],"watch")==0){
            watch_mode=1; model_arg="latest";
            int set=0;
            for(int i=2;i<argc;i++){
                if(strcmp(argv[i],"--deterministic")==0) watch_deterministic=1;
                else if(strcmp(argv[i],"--random")==0) watch_random=1;
                else if(strcmp(argv[i],"--continue")==0) watch_continue=1;
                else if(!set && argv[i][0]!='-'){ model_arg=argv[i]; set=1; }
                else { fprintf(stderr,"unknown watch arg %s\n",argv[i]); retro_print_usage(argv[0]); return 1; }
            }
        } else if(strcmp(argv[1],"play")==0){
        } else if(strcmp(argv[1],"help")==0 || strcmp(argv[1],"-h")==0 || strcmp(argv[1],"--help")==0){
            retro_print_usage(argv[0]); return 0;
        } else {
            fprintf(stderr,"unknown mode %s\n",argv[1]); retro_print_usage(argv[0]); return 1;
        }
    }
    PufferNet* net=nullptr;
    if(watch_mode){
        char path[4096];
        if(retro_resolve_model(model_arg,path,sizeof(path))!=0) return 1;
        net=retro_load_policy(path);
        if(!net) return 1;
    }
    Ini ini={0};
    puf_ini_load_env(&ini, RETRO_ENV_NAME, 0, NULL);
    const char* be_ov = getenv("RETRO_BACKEND");
    if(be_ov && *be_ov) puf_ini_put(&ini, "env.backend", be_ov);
    if(watch_random || watch_continue) puf_ini_put(&ini, "env.spawn_levels", "all");
    Dict* env_kwargs=puf_ini_section(&ini,"env",0);
    Env env={0};
    float obs[OBS_SIZE]={0};
    float act[NUM_ATNS]={0};
    float rew[1]={0};
    float term[1]={0};
    puf_init(&env, env_kwargs);
    // puf_init memsets env, so set agents after
    env.agents[0].observations=obs;
    env.agents[0].actions=act;
    env.agents[0].rewards=rew;
    env.agents[0].terminals=term;
    env.agents[0].action_mask=nullptr;
    env.agents[0].policy=0;
    // puf_init memsets Env: seed the spawn picker from wall time so
    // watch --random varies across sessions.
    env.rng = (unsigned int)time(NULL) ^ 0x9E3779B9u;
    if(watch_continue) env.spawn_pin=1;
    puf_reset(&env);
    const char* _hd=getenv("DISPLAY"); const char* _hw=getenv("WAYLAND_DISPLAY");
    if((!_hd || !*_hd) && (!_hw || !*_hw)){
        // Long-horizon policy audit (hang/trigger hunting): AUDIT_STEPS=N
        // runs the policy (deterministic argmax) for N steps with progress
        // telemetry. Last line before a hang = trigger vicinity.
        int audit_n = 100;
        const char* audit_ev = getenv("AUDIT_STEPS");
        if(audit_ev && *audit_ev) audit_n = atoi(audit_ev);
        if(audit_n < 1) audit_n = 1;
        int audit_det = (audit_ev && *audit_ev) ? 1 : watch_deterministic;
        const char* audit_det_ev = getenv("AUDIT_DET");
        if(audit_det_ev && *audit_det_ev == '0') audit_det = 0;
        fprintf(stderr,"no display - headless demo %d steps%s\n", audit_n,
            audit_n > 100 ? " (AUDIT mode, deterministic policy)" : "");
        long audit_eps = 0;
        for(int i=0;i<audit_n;i++){
            if(!watch_mode) act[0]=1;
            else {
                if(audit_det){ linear(net->encoder, obs); mingru(net->mingru, net->encoder->output); linear(net->decoder, net->mingru->output); argmax_multidiscrete(net->multidiscrete, net->decoder->output, act); }
                else forward_puffernet(net, obs, act);
            }
            int pre_w=env.world, pre_s=env.stage; long pre_f=env.log.flag;
            puf_step(&env);
            if(i%100==0 || term[0]>0.5f) fprintf(stderr,"audit step %d ep=%ld x=%d score=%d world=%d-%d area=%d tick=%d act=%.0f term=%.0f deaths=%.0f flags=%.0f\n",
                i, audit_eps, env.x_pos, env.score, env.world, env.stage, env.area, env.tick, act[0], term[0], env.log.deaths, env.log.flag);
            if(term[0]>0.5f){
                audit_eps++;
                if(watch_continue){
                    int w=pre_w, l=pre_s;
                    if(env.log.flag>pre_f){ l++; if(l>4){l=1;w++;} if(w>8){w=1;l=1;} }
                    env.cur_spawn=retro_spawn_index(&env,w,l);
                }
                puf_reset(&env);
            }
        }
        puf_close(&env);
        if(net) free_puffernet(net);
        if(g_weights) free(g_weights);
        puf_ini_free(&ini);
        return 0;
    }
    SetTraceLogLevel(LOG_ERROR);
    puf_render(&env);
    if(!IsWindowReady()){
        fprintf(stderr,"no window - headless demo 100 steps\n");
        for(int i=0;i<100;i++){
            if(!watch_mode) act[0]=1;
            else {
                if(watch_deterministic){ linear(net->encoder, obs); mingru(net->mingru, net->encoder->output); linear(net->decoder, net->mingru->output); argmax_multidiscrete(net->multidiscrete, net->decoder->output, act); }
                else forward_puffernet(net, obs, act);
            }
            puf_step(&env);
            if(i%20==0) fprintf(stderr,"headless step %d x=%d score=%d world=%d-%d term=%.0f\n", i, env.x_pos, env.score, env.world, env.stage, term[0]);
            if(term[0]>0.5f) puf_reset(&env);
        }
        puf_close(&env);
        if(net) free_puffernet(net);
        if(g_weights) free(g_weights);
        puf_ini_free(&ini);
        return 0;
    }
    while(!WindowShouldClose()){
        if(IsKeyPressed(KEY_R)){
            puf_reset(&env);
        }
        if(!watch_mode){
            int a = retro_human_action();
            act[0]=(float)a;
        } else {
            if(watch_deterministic){
                linear(net->encoder, obs);
                mingru(net->mingru, net->encoder->output);
                linear(net->decoder, net->mingru->output);
                argmax_multidiscrete(net->multidiscrete, net->decoder->output, act);
            } else {
                forward_puffernet(net, obs, act);
            }
        }
        int pre_w=env.world, pre_s=env.stage; long pre_f=env.log.flag;
        puf_step(&env);
        if(env.agents[0].terminals[0]>0.5f){
            if(watch_continue && env.log.flag>pre_f){
                // cleared the level: the internal reset replayed the same
                // level (pin); advance to the next and load it.
                int w=pre_w, l=pre_s;
                l++; if(l>4){l=1;w++;} if(w>8){w=1;l=1;}
                env.cur_spawn=retro_spawn_index(&env,w,l);
                puf_reset(&env);
            }
            if(net && net->mingru && net->mingru->state){
                int cnt=net->mingru->num_layers*net->mingru->batch_size*net->mingru->hidden_size;
                memset(net->mingru->state,0,cnt*sizeof(float));
            }
        }
        puf_render(&env);
    }
    puf_close(&env);
    if(net) free_puffernet(net);
    if(g_weights) free(g_weights);
    puf_ini_free(&ini);
    return 0;
}
