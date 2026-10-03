// SWAT: Gold Element player and checkpoint viewer. The game and native RL
// adapter call the same fixed-step simulation; rendering never changes it.
#include "swat.h"
#include "frontend.h"
#include "../../src/puffercpu.c"

static void usage(const char* path) {
    printf("SWAT: Gold Element\n"
        "  %s [play] [--env.hostile_fire=0] [--base.seed=42]\n"
        "  %s watch CHECKPOINT [--deterministic] [--policy.hidden_size=64]\n"
        "  %s --eval CHECKPOINT [EPISODES] [--deterministic] [--env.max_ticks=1800]\n"
        "  %s --capture FILE.png [--env.randomize=0]\n"
        "  --settings FILE.ini          Override the saved player preferences\n"
        "  --capture-screen SCREEN      game, main, pause, or settings\n"
        "Run from the repository root so config/default.ini and config/swat.ini are available.\n",
        path,path,path,path);
}

static PufferNet* load_policy(const char* path, int hidden, int layers, Weights** storage) {
    if (hidden <= 0 || hidden%8 || layers <= 0) {
        fprintf(stderr,"swat: policy width must be a positive multiple of 8 and layers positive\n");
        return NULL;
    }
    Weights* weights=load_weights(path);
    if (!weights) { fprintf(stderr,"swat: cannot read checkpoint %s\n",path); return NULL; }
    int sizes[]=ACT_SIZES,logits=0;
    for (int i=0;i<NUM_ATNS;i++) logits+=sizes[i];
    long long expected=(long long)OBS_SIZE*hidden+(long long)(logits+1)*hidden+(long long)layers*3*hidden*hidden;
    if (weights->size-7!=expected) {
        fprintf(stderr,"swat: incompatible checkpoint: expected %lld FP32 weights, got %d; contract v%d, hidden=%d, layers=%d\n",
            expected,weights->size-7,SWAT_CONTRACT_VERSION,hidden,layers);
        free(weights); return NULL;
    }
    *storage=weights;
    return make_puffernet(weights,1,OBS_SIZE,hidden,layers,sizes,NUM_ATNS);
}

static void policy_action(PufferNet* policy, float* obs, float* actions,
                           float terminal, bool deterministic) {
    mingru_zero_term(policy->mingru,&terminal);
    linear(policy->encoder,obs);
    mingru(policy->mingru,policy->encoder->output);
    linear(policy->decoder,policy->mingru->output);
    multidiscrete(policy->multidiscrete,policy->decoder->output,actions,deterministic,NULL);
}

int main(int argc, char** argv) {
    const char* model=NULL; const char* capture=NULL; const char* settings_path=NULL;
    SwatScreen capture_screen=SWAT_SCREEN_GAME;
    bool eval=false,deterministic=false;
    int episodes=8,override_count=0;
    char** overrides=calloc((size_t)argc,sizeof(char*));
    if (!overrides) return 1;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"--help") || !strcmp(argv[i],"help")) {
            usage(argv[0]); free(overrides); return 0;
        } else if (!strcmp(argv[i],"play")) {
            continue;
        } else if (!strcmp(argv[i],"watch") || !strcmp(argv[i],"--eval")) {
            eval=!strcmp(argv[i],"--eval");
            if (++i>=argc) { usage(argv[0]); free(overrides); return 1; }
            model=argv[i];
            if (eval && i+1<argc && argv[i+1][0]!='-') {
                char* end=NULL;
                long n=strtol(argv[++i],&end,10);
                if (!end || *end || n<1 || n>100000) {
                    fprintf(stderr,"swat: episodes must be 1..100000\n"); free(overrides); return 1;
                }
                episodes=(int)n;
            }
        } else if (!strcmp(argv[i],"--capture")) {
            if (++i>=argc) { usage(argv[0]); free(overrides); return 1; }
            capture=argv[i];
        } else if (!strcmp(argv[i],"--settings")) {
            if (++i>=argc) { usage(argv[0]); free(overrides); return 1; }
            settings_path=argv[i];
        } else if (!strcmp(argv[i],"--capture-screen")) {
            if (++i>=argc) { usage(argv[0]); free(overrides); return 1; }
            if(!strcmp(argv[i],"game")) capture_screen=SWAT_SCREEN_GAME;
            else if(!strcmp(argv[i],"main")) capture_screen=SWAT_SCREEN_MAIN;
            else if(!strcmp(argv[i],"pause")) capture_screen=SWAT_SCREEN_PAUSE;
            else if(!strcmp(argv[i],"settings")) capture_screen=SWAT_SCREEN_SETTINGS;
            else { fprintf(stderr,"swat: unknown capture screen %s\n",argv[i]); free(overrides); return 1; }
        } else if (!strcmp(argv[i],"--deterministic")) deterministic=true;
        else if (!strncmp(argv[i],"--",2) && strchr(argv[i],'.') && strchr(argv[i],'='))
            overrides[override_count++]=argv[i];
        else { fprintf(stderr,"swat: unknown argument %s\n",argv[i]); usage(argv[0]); free(overrides); return 1; }
    }
    Ini ini={0};
    puf_ini_load_env(&ini,"swat",override_count,overrides);
    free(overrides);
    int hidden=(int)puf_ini_get(&ini,"policy","hidden_size");
    int layers=(int)puf_ini_get(&ini,"policy","num_layers");
    unsigned int seed=(unsigned int)puf_ini_get(&ini,"base","seed");
    Env env={0}; env.rng=seed;
    puf_init(&env,puf_ini_section(&ini,"env",0));
    puf_ini_free(&ini);
    float obs[OBS_SIZE]={0},actions[NUM_ATNS]={0},reward=0,terminal=0;
    env.agents[0].observations=obs; env.agents[0].actions=actions;
    env.agents[0].rewards=&reward; env.agents[0].terminals=&terminal;
    puf_reset(&env);
    PufferNet* policy=NULL; Weights* weights=NULL;
    if (model) {
        policy=load_policy(model,hidden,layers,&weights);
        if (!policy) { puf_close(&env); return 1; }
    }
    srand(seed);
    if (eval) {
        while (env.log.n<episodes) {
            policy_action(policy,obs,actions,terminal,deterministic);
            puf_step(&env);
            for (int i=0;i<OBS_SIZE;i++) if (!isfinite(obs[i])) {
                fprintf(stderr,"swat: nonfinite observation\n"); puf_close(&env);
                free_puffernet(policy); free(weights); return 1;
            }
        }
        printf("SWAT eval episodes=%.0f success=%.4f return=%.6f length=%.2f shots=%.2f civilian_damage=%.2f\n",
            env.log.n,env.log.perf/env.log.n,env.log.episode_return/env.log.n,
            env.log.episode_length/env.log.n,env.log.shots/env.log.n,env.log.civilian_damage/env.log.n);
        puf_close(&env); free_puffernet(policy); free(weights);
        return 0; // Inference health. Mission success is reported explicitly.
    }

    SwatView view={0}; swat_view_init(&view,capture!=NULL);
    if (!view.initialized) { puf_close(&env); if(policy) free_puffernet(policy); free(weights); return 1; }
    SwatFrontend app;
    swat_frontend_init(&app,settings_path);
    if(capture) app.screen=capture_screen;
    float accumulator=0,look_x=0,look_y=0;
    int frames=0;
    while (!WindowShouldClose() && !app.quit) {
        if(!capture) swat_frontend_update(&app,env.sim,policy!=NULL);
        if(app.restart_requested) {
            puf_reset(&env); terminal=1;
            app.restart_requested=false;
            app.reset_input=true;
        }
        if(app.reset_input || capture || !swat_frontend_playing(&app)) {
            accumulator=0;
            look_x=look_y=0;
            app.reset_input=false;
        }
        if(!capture && swat_frontend_playing(&app)) {
            SwatInput input=policy ? swat_neutral_input() : swat_frontend_input(&app,env.sim);
            look_x+=input.yaw_delta; look_y+=input.pitch_delta;
            accumulator+=fminf(GetFrameTime(),0.1f);
            int ticks=(int)(accumulator/SWAT_DT);
            if(ticks>0) {
                input.yaw_delta=look_x/ticks; input.pitch_delta=look_y/ticks;
                look_x=look_y=0;
            }
            for(int t=0;t<ticks;t++) {
                if(policy) {
                    policy_action(policy,obs,actions,terminal,deterministic);
                    puf_step(&env);
                } else swat_sim_step(env.sim,&input);
                accumulator-=SWAT_DT;
            }
        }
        BeginDrawing();
        swat_view_draw(&view,env.sim,policy!=NULL,app.settings.vertical_fov);
        swat_frontend_draw(&app,env.sim,policy!=NULL);
        EndDrawing();
        if(capture && ++frames==12) {
            Image frame=LoadImageFromScreen();
            bool saved=ExportImage(frame,capture);
            UnloadImage(frame);
            swat_frontend_close(&app); swat_view_close(&view); puf_close(&env);
            if(policy) free_puffernet(policy);
            free(weights);
            return saved ? 0 : 1;
        }
    }
    swat_frontend_close(&app); swat_view_close(&view); puf_close(&env);
    if(policy) free_puffernet(policy);
    free(weights);
    return 0;
}
