// SWAT: Gold Element player and checkpoint viewer. The game and native RL
// adapter call the same fixed-step simulation; rendering never changes it.
#include "swat.h"
#include "frontend.h"
#include "net.h"
#include "replay.h"
#include "sound_view.h"
#include "../../src/puffercpu.c"

static void usage(const char* path) {
    printf("SWAT: Gold Element\n"
        "  %s [play] [--env.hostile_fire=0] [--base.seed=42]\n"
        "  %s watch CHECKPOINT [--deterministic] [--policy.hidden_size=64]\n"
        "  %s --eval CHECKPOINT [EPISODES] [--deterministic] [--env.max_ticks=1800]\n"
        "  %s --capture FILE.png [--env.randomize=0]\n"
        "  %s host [--port 27474]\n"
        "  %s join ADDRESS [--port 27474]\n"
        "  --record FILE.sgrp          Record a solo round for exact input replay\n"
        "  --settings FILE.ini          Override the saved player preferences\n"
        "  --mission house|annex|generated|range  Human default: house; policy default: annex\n"
        "  --layout-seed N --difficulty 0|1|2 --generator neural|uniform\n"
        "  --layout-model FILE          Optional trained house policy\n"
        "  --capture-screen SCREEN      game, main, pause, settings, plan, or overwatch\n"
        "Run from the repository root so config/default.ini and config/swat.ini are available.\n",
        path,path,path,path,path,path);
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
    const char* model=NULL; const char* capture=NULL; const char* settings_path=NULL; const char* layout_model=NULL;
    const char* record_path=NULL;
    SwatReplay recording={0}; bool record_started=false;
    const char* join_address=NULL; bool start_host=false;
    int port=SWAT_DEFAULT_PORT;
    int mission=-1,preview=0,difficulty=1,generator=SWAT_LAYOUT_NEURAL;
    uint32_t layout_seed=1;
    SwatScreen capture_screen=SWAT_SCREEN_GAME;
    bool eval=false,deterministic=false,max_ticks_override=false;
    int episodes=8,override_count=0;
    char** overrides=calloc((size_t)argc,sizeof(char*));
    if (!overrides) return 1;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"--help") || !strcmp(argv[i],"help")) {
            usage(argv[0]); free(overrides); return 0;
        } else if (!strcmp(argv[i],"play")) {
            continue;
        } else if(!strcmp(argv[i],"host")) start_host=true;
        else if(!strcmp(argv[i],"join")) {
            if(++i>=argc) { usage(argv[0]); free(overrides); return 1; }
            join_address=argv[i];
        } else if(!strcmp(argv[i],"--mission")) {
            if(++i>=argc || (strcmp(argv[i],"house") && strcmp(argv[i],"annex") && strcmp(argv[i],"generated") && strcmp(argv[i],"range"))) { usage(argv[0]); free(overrides); return 1; }
            mission=!strcmp(argv[i],"house") ? SWAT_HOUSE : (!strcmp(argv[i],"generated") ? SWAT_GENERATED : (!strcmp(argv[i],"range") ? SWAT_RANGE : SWAT_ANNEX));
        } else if(!strcmp(argv[i],"--layout-model")) {
            if(++i>=argc) { usage(argv[0]); free(overrides); return 1; } layout_model=argv[i];
        } else if(!strcmp(argv[i],"--generator")) {
            if(++i>=argc || (strcmp(argv[i],"neural") && strcmp(argv[i],"uniform"))) { usage(argv[0]); free(overrides); return 1; }
            generator=!strcmp(argv[i],"neural") ? SWAT_LAYOUT_NEURAL : SWAT_LAYOUT_UNIFORM;
        } else if(!strcmp(argv[i],"--layout-seed") || !strcmp(argv[i],"--difficulty")) {
            bool seed_option=!strcmp(argv[i],"--layout-seed");
            if(++i>=argc) { usage(argv[0]); free(overrides); return 1; }
            char* end; unsigned long long n=strtoull(argv[i],&end,10);
            if(end==argv[i] || *end || argv[i][0]=='-' || n>(seed_option ? UINT32_MAX : 2u)) { usage(argv[0]); free(overrides); return 1; }
            if(seed_option) layout_seed=(uint32_t)n; else difficulty=(int)n;
        } else if(!strcmp(argv[i],"--port")) {
            if(++i>=argc) { usage(argv[0]); free(overrides); return 1; }
            char* end; long parsed=strtol(argv[i],&end,10);
            if(*end || parsed<1 || parsed>65535) { fprintf(stderr,"swat: invalid port\n"); free(overrides); return 1; }
            port=(int)parsed;
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
        } else if(!strcmp(argv[i],"--record")) {
            if(++i>=argc) { usage(argv[0]); free(overrides); return 1; }
            record_path=argv[i];
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
            else if(!strcmp(argv[i],"plan")) capture_screen=SWAT_SCREEN_PLAN;
            else if(!strcmp(argv[i],"overwatch")) { capture_screen=SWAT_SCREEN_PLAN; preview=1; }
            else { fprintf(stderr,"swat: unknown capture screen %s\n",argv[i]); free(overrides); return 1; }
        } else if (!strcmp(argv[i],"--deterministic")) deterministic=true;
        else if (!strncmp(argv[i],"--",2) && strchr(argv[i],'.') && strchr(argv[i],'=')) {
            overrides[override_count++]=argv[i];
            if(!strncmp(argv[i],"--env.max_ticks=",16)) max_ticks_override=true;
        }
        else { fprintf(stderr,"swat: unknown argument %s\n",argv[i]); usage(argv[0]); free(overrides); return 1; }
    }
    if(record_path && (model || start_host || join_address)) { fprintf(stderr,"Recording currently requires solo human play\n"); free(overrides); return 1; }
    if((model || capture) && (start_host || join_address)) {
        fprintf(stderr,"swat: host/join requires human play\n"); free(overrides); return 1;
    }
    if(layout_model && !swat_layout_load_policy(layout_model)) {
        fprintf(stderr,"swat: cannot load layout model %s\n",layout_model); free(overrides); return 1;
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
    env.sim->config.mission=mission<0 ? (model ? SWAT_ANNEX : SWAT_HOUSE) : mission;
    env.sim->config.tactical_rules=!model && env.sim->config.mission!=SWAT_ANNEX;
    env.sim->config.squad_bots=!model && (env.sim->config.mission==SWAT_HOUSE || env.sim->config.mission==SWAT_GENERATED) ? 3 : 0;
    env.sim->config.layout_seed=layout_seed; env.sim->config.generator=generator; env.sim->config.difficulty=difficulty;
    if(!model && !max_ticks_override && env.sim->config.max_ticks==1800) env.sim->config.max_ticks=18000;
    puf_reset(&env);
    SwatConfig solo_config=env.sim->config;
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
    SwatNetServer server={0}; SwatNetClient client={0};
    SwatSoundView sound={0}; if(!capture) swat_sound_view_init(&sound);
    snprintf(app.port,sizeof(app.port),"%d",port);
    if(start_host || join_address) {
        app.hosting=start_host; app.host_requested=start_host; app.join_requested=join_address!=NULL;
        app.connect_pending=true;
        if(join_address) snprintf(app.address,sizeof(app.address),"%s",join_address);
        swat_frontend_set_screen(&app,SWAT_SCREEN_CONNECT);
    }
    if(capture) { app.screen=capture_screen; app.plan_preview=preview; }
    float accumulator=0,look_x=0,look_y=0;
    int frames=0;
    while (!WindowShouldClose() && !app.quit) {
        if(app.disconnect_requested) {
            swat_client_close(&client); swat_server_close(&server);
            ClearWindowState(FLAG_WINDOW_ALWAYS_RUN);
            env.sim->config=solo_config; env.sim->rng=seed; puf_reset(&env); terminal=1;
            app.networked=false; app.leader=true; app.actor=0; app.connect_pending=false;
            app.disconnect_requested=false; app.reset_input=true;
            view.session_status[0]='\0';
        }
        if(app.host_requested || app.join_requested) {
            if(recording.file) swat_replay_close(&recording);
            char* end; long parsed=strtol(app.port,&end,10);
            bool request_host=app.host_requested;
            app.host_requested=app.join_requested=false;
            if(end==app.port || *end || parsed<1 || parsed>65535) {
                snprintf(app.notice,sizeof(app.notice),"Choose a UDP port from 1 to 65535."); app.connect_pending=false;
            } else if(request_host) {
                env.sim->config=solo_config;
                if(swat_server_open(&server,env.sim,(int)parsed,true)) {
                    SetWindowState(FLAG_WINDOW_ALWAYS_RUN);
                    app.networked=true; app.leader=true; app.actor=0; app.connect_pending=false;
                    swat_frontend_set_screen(&app,SWAT_SCREEN_GAME);
                } else {
                    env.sim->config=solo_config;
                    snprintf(app.notice,sizeof(app.notice),"%s",server.error); app.connect_pending=false;
                }
            } else if(!swat_client_open(&client,env.sim,app.address,(int)parsed)) {
                snprintf(app.notice,sizeof(app.notice),"%s",client.error); app.connect_pending=false;
            } else SetWindowState(FLAG_WINDOW_ALWAYS_RUN);
        }
        if(server.transport) swat_server_poll(&server);
        if(client.transport) {
            swat_client_poll(&client);
            if(client.status==SWAT_NET_ACTIVE) {
                app.actor=client.actor; app.networked=true; app.leader=client.slot==client.leader_slot;
                if(app.connect_pending) { app.connect_pending=false; app.last_episode=-1; swat_frontend_set_screen(&app,SWAT_SCREEN_GAME); }
            } else if(client.status==SWAT_NET_FAILED) {
                snprintf(app.notice,sizeof(app.notice),"%s",client.error);
                swat_client_close(&client); env.sim->config=solo_config; env.sim->rng=seed; puf_reset(&env);
                ClearWindowState(FLAG_WINDOW_ALWAYS_RUN);
                app.networked=false; app.leader=true; app.actor=0; app.connect_pending=false;
                swat_frontend_set_screen(&app,SWAT_SCREEN_MAIN); app.disconnect_requested=false;
            }
        }
        if(!capture) swat_frontend_update(&app,env.sim,policy!=NULL);
        if(recording.file && (app.scenario_requested || app.restart_requested || app.host_requested || app.join_requested)) swat_replay_close(&recording);
        if(app.scenario_requested) {
            if(app.leader) {
                if(server.transport) swat_server_scenario(&server,&app.scenario);
                else if(client.transport) swat_client_scenario(&client,&app.scenario);
                else { env.sim->config=solo_config=app.scenario; puf_reset(&env); terminal=1; }
            }
            app.scenario_requested=false; app.reset_input=true;
        }
        if(app.restart_requested) {
            if(server.transport) swat_server_restart(&server);
            else if(client.transport) swat_client_restart(&client);
            else { env.sim->config=solo_config; puf_reset(&env); terminal=1; }
            app.restart_requested=false;
            app.reset_input=true;
        }
        bool online=server.transport || client.status==SWAT_NET_ACTIVE;
        if(app.reset_input || capture || (!online && !swat_frontend_playing(&app))) {
            if(!online) accumulator=0;
            look_x=look_y=0;
            app.reset_input=false;
        }
        if(!capture && (online || swat_frontend_playing(&app))) {
            SwatInput input=policy || !swat_frontend_playing(&app) ? swat_neutral_input() : swat_frontend_input(&app,env.sim);
            look_x+=input.yaw_delta; look_y+=input.pitch_delta;
            accumulator+=fminf(GetFrameTime(),0.1f);
            int ticks=(int)(accumulator/SWAT_DT);
            if(ticks>0) {
                input.yaw_delta=look_x/ticks; input.pitch_delta=look_y/ticks;
                look_x=look_y=0;
            }
            for(int t=0;t<ticks;t++) {
                if(server.transport) swat_server_tick(&server,&input);
                else if(client.status==SWAT_NET_ACTIVE) swat_client_input(&client,&input);
                else if(policy) {
                    policy_action(policy,obs,actions,terminal,deterministic);
                    puf_step(&env);
                } else {
                    if(record_path && !record_started && env.sim->tick==0) {
                        record_started=true;
                        if(!swat_replay_record(&recording,record_path,env.sim)) fprintf(stderr,"Cannot start replay recording: %s\n",record_path);
                    }
                    int before_tick=env.sim->tick; swat_sim_step(env.sim,&input);
                    if(recording.file && env.sim->tick!=before_tick && !swat_replay_append(&recording,&input,env.sim)) {
                        fprintf(stderr,"Replay recording stopped after %u frames\n",recording.count); swat_replay_close(&recording);
                    }
                }
                accumulator-=SWAT_DT;
            }
        }
        view.actor=app.actor; view.debug=app.debug;
        view.planning=app.screen==SWAT_SCREEN_PLAN; view.plan_preview=app.plan_preview; view.plan_yaw=app.plan_yaw;
        view.scope=app.screen==SWAT_SCREEN_SCOPE; view.sniper_unit=app.selected_sniper;
        view.sniper_camera=swat_frontend_playing(&app) && app.camera_open;
        view.camera_expansion=app.camera_expansion;
        view.yaw_offset=look_x; view.pitch_offset=look_y;
        if(client.status==SWAT_NET_ACTIVE) for(int i=0;i<client.pending_count;i++) {
            const SwatInput* pending=&client.pending[(client.pending_head+i)%128].input;
            // Unacknowledged aim belongs to the body or selected sniper that
            // received it. Switching feeds must not rotate another camera.
            bool pending_scope=pending->sniper_control || pending->device_control;
            int pending_unit=pending->device_control ? SWAT_SNIPERS+pending->device_unit : pending->sniper_unit;
            if(pending_scope!=view.scope || (view.scope && pending_unit!=view.sniper_unit)) continue;
            view.yaw_offset+=pending->yaw_delta; view.pitch_offset+=pending->pitch_delta;
        }
        if(server.transport) {
            int players=0; for(int i=0;i<SWAT_MAX_PLAYERS;i++) players+=(server.player_mask>>i)&1;
            snprintf(view.session_status,sizeof(view.session_status),"HOST :%d / %d OF 4",server.port,players);
        } else if(client.status==SWAT_NET_ACTIVE)
            snprintf(view.session_status,sizeof(view.session_status),"CO-OP / GOLD %02d / %d ms",client.slot+1,client.ping_ms);
        else view.session_status[0]='\0';
        int listener=app.actor;
        if(view.scope && app.selected_sniper<SWAT_SNIPERS && env.sim->snipers[app.selected_sniper].deployed) listener=swat_sniper_actor(app.selected_sniper);
        if(view.scope && app.selected_sniper>=SWAT_SNIPERS) listener=SWAT_MAX_ACTORS+app.selected_sniper-SWAT_SNIPERS;
        swat_sound_view_update(&sound,env.sim,listener,app.settings.master_volume,
            client.status==SWAT_NET_ACTIVE ? client.sound_floor : 0,view.yaw_offset);
        BeginDrawing();
        swat_view_draw(&view,env.sim,policy!=NULL,app.settings.vertical_fov);
        swat_frontend_draw(&app,&view,env.sim,policy!=NULL);
        EndDrawing();
        if(capture && ++frames==12) {
            Image frame=LoadImageFromScreen();
            bool saved=ExportImage(frame,capture);
            UnloadImage(frame);
            if(recording.file && !swat_replay_close(&recording)) fprintf(stderr,"Replay could not be finalized\n");
            swat_sound_view_close(&sound); swat_frontend_close(&app); swat_view_close(&view); puf_close(&env);
            if(policy) free_puffernet(policy);
            free(weights);
            return saved ? 0 : 1;
        }
    }
    if(recording.file && !swat_replay_close(&recording)) fprintf(stderr,"Replay could not be finalized\n");
    swat_client_close(&client); swat_server_close(&server); swat_sound_view_close(&sound);
    swat_frontend_close(&app); swat_view_close(&view); puf_close(&env);
    if(policy) free_puffernet(policy);
    free(weights);
    return 0;
}
