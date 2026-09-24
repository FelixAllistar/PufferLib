#ifdef PUFFER_RETRO_LEGACY
#error "Legacy retro backends are retired; use full-screen ROM observations"
#else
#define PUFFERLIB_BUILD_MAIN
#include "retro.h"
#undef PUFFERLIB_BUILD_MAIN
#include "puffercpu.h"
#include "retro_policy_cpu.h"
#include <omp.h>
#include <chrono>
#include <filesystem>
#include <ctime>
#include <climits>
#include "retro_playback.h"
#include "retro_inspect.h"

static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static void usage() {
    fprintf(stderr,"retro play [level] [--inspect] [--single-life]\nretro watch [PATH.bin|latest] [level|--level LEVEL] [--random] [--single-life] [--deterministic] [--inspect|--inspect-check]\n"
        "  LEVEL: 1-1 through 8-4 (default 1-1); lives/respawns continue naturally unless --single-life\n"
        "  --timing: headless deterministic split CSV through the reward/RTA finish (frameskip=1)\n"
        "  --full-run: ignore practice spawn and start the ordinary level; practice otherwise resets each attempt\n"
        "retro levels\nretro bench [envs=512] [steps=512] [workers=4] [frameskip=1] [levels=all] [random|right|mixed] [idle_loop_skip=1] [reference|blocks] [reference|wide]\n"
        "retro replay INPUT.txt [level=1-1]  # one integer NES button mask (0..255) per frame\n");
}
static int bench(int argc,char** argv,Ini* ini) {
    int n=argc>2?atoi(argv[2]):512,steps=argc>3?atoi(argv[3]):512,workers=argc>4?atoi(argv[4]):4;
    int skip=argc>5?atoi(argv[5]):1;
    if(n<1||steps<1||workers<1) throw std::runtime_error("invalid benchmark size");
    puf_ini_put(ini,"env.spawn_levels",argc>6?argv[6]:"all");
    puf_ini_put(ini,"env.frameskip",std::to_string(skip).c_str());
    puf_ini_put(ini,"env.idle_loop_skip",argc>8?argv[8]:"1");
    puf_ini_put(ini,"env.cpu_backend",argc>9?argv[9]:"reference");
    puf_ini_put(ini,"env.render_backend",argc>10?argv[10]:"reference");
    Dict vec={0}; dict_set(&vec,"total_agents",n); dict_set(&vec,"num_buffers",1);
    int count,begin[1],size[1]; double init=now();
    Env* envs=my_vec_init(&count,begin,size,&vec,puf_ini_section(ini,"env",0));
    std::vector<float> obs(n*OBS_SIZE),actions(n),rewards(n),terminals(n);
    std::vector<unsigned int> rng(n);
    for(int i=0;i<n;i++) {
        envs[i].agents[0].observations=obs.data()+i*OBS_SIZE; envs[i].agents[0].actions=&actions[i];
        envs[i].agents[0].rewards=&rewards[i]; envs[i].agents[0].terminals=&terminals[i];
        rng[i]=12345u+i*2654435761u; puf_reset(&envs[i]);
    }
    init=now()-init; omp_set_dynamic(0);
    bool right=argc>7&&!strcmp(argv[7],"right");
    bool mixed=argc>7&&!strcmp(argv[7],"mixed");
    unsigned long long frames=0,resets=0; double start=now();
    std::clock_t cpu_start=std::clock();
    for(int s=0;s<steps;s++) {
        #pragma omp parallel for schedule(static) num_threads(workers) reduction(+:frames,resets)
        for(int i=0;i<n;i++) {
            actions[i]=right?retro_mask_action(RETRO_BTN_RIGHT):retro_random(&rng[i])%64;
            if(mixed&&(s+i)%512<384) actions[i]=retro_mask_action(RETRO_BTN_RIGHT|RETRO_BTN_B|(((s+i)%48<28)?RETRO_BTN_A:0));
            puf_step(&envs[i]); frames+=envs[i].last_frames; resets+=terminals[i]!=0;
        }
    }
    double elapsed=now()-start; unsigned long long hash=0;
    double cpu_seconds=(double)(std::clock()-cpu_start)/CLOCKS_PER_SEC;
    for(int i=0;i<n;i++) hash+=envs[i].x_pos+envs[i].tick+envs[i].score;
    printf("bench ROM cpu=%s render=%s envs=%d steps=%d workers=%d frameskip=%d init=%.3fs elapsed=%.3fs cpu_seconds=%.3f decisions/s=%.0f frames/s=%.0f frames/cpu_second=%.0f resets=%llu checksum=%llu\n",
        argc>9?argv[9]:"reference",argc>10?argv[10]:"reference",n,steps,workers,skip,init,elapsed,cpu_seconds,(double)n*steps/elapsed,frames/elapsed,frames/cpu_seconds,resets,hash);
    my_vec_close(envs); dict_clear(&vec); puf_ini_free(ini); return 0;
}
static std::string newest_checkpoint(Ini* ini) {
    namespace fs=std::filesystem;
    fs::path best; fs::file_time_type stamp=fs::file_time_type::min();
    const fs::path root=fs::path(puf_ini_get_str(ini,"base","checkpoint_dir")) /
        puf_ini_get_str(ini,"base","env_name");
    if(fs::exists(root)) for(const auto& entry:fs::recursive_directory_iterator(root)) {
        if(entry.is_regular_file()&&entry.path().extension()==".bin"&&entry.last_write_time()>stamp) {
            stamp=entry.last_write_time(); best=entry.path();
        }
    }
    if(best.empty()) throw std::runtime_error("no checkpoints under "+root.string());
    return best.string();
}
static unsigned char human_buttons() {
    return ((IsKeyDown(KEY_X)||IsKeyDown(KEY_SPACE))?1:0)
        | ((IsKeyDown(KEY_Z)||IsKeyDown(KEY_LEFT_SHIFT))?2:0)
        | (IsKeyDown(KEY_UP)?16:0) | (IsKeyDown(KEY_DOWN)?32:0)
        | (IsKeyDown(KEY_LEFT)?64:0) | (IsKeyDown(KEY_RIGHT)?128:0);
}
static void retro_timing_audit(Env* e,RetroPolicy* net,float* obs,float* action) {
    if(e->frameskip!=1) throw std::runtime_error("--timing requires env.frameskip=1 for exact per-frame events");
    fprintf(stderr,"Timing uses current viewer config: area rewards REMOVED; completion base=%g frame bonus=%g range=%d..%d; HUD extra=%g range=%d..%g\n",
        e->completion_reward,e->completion_time_bonus,
        e->completion_time_min_frames,e->completion_time_max_frames,e->completion_time_target_bonus,
        e->completion_time_target,e->completion_time_target_max);
    if(e->completion_time_target_bonus>0&&e->completion_time_target_max<=e->completion_time_target)
        fprintf(stderr,"NOTE: inverted/equal HUD completion range disables its extra bonus.\n");
    fprintf(stderr,"NTSC frame-derived RTA: %.8f fps, autosplitter black-screen convention, reset offset=%d frames\n",
        RETRO_NTSC_FPS,e->rta.start_offset);
    if(e->practice) fprintf(stderr,"PRACTICE: elapsed times are segment-only, NOT full-run RTA.\n");
    if(!RETRO_RTA_COMPARABLE)
        fprintf(stderr,"NOT SPEEDRUN RTA: the supported ROM is PAL, but this core schedules NTSC frames.\n");
    fprintf(stderr,"Reward endpoint: %s\n",e->completion_on_rta_split?"autosplitter-style split":
        e->completion_on_next_playable?"next playable":"flag/level advance (legacy)");
    unsigned long core_start=e->emu->video_frame_count();
    printf("event,frames,seconds,hud_time,routine,x,area_events,novel_destinations,hud_divider,bus_phase,reward,rta_frames,rta_seconds,nes_frames\n");
    auto row=[&](const char* event) {
        const auto* m=e->emu->low_mem();
        int rta_frames=retro_rta_elapsed(e->rta,e->tick);
        printf("%s,%d,%.6f,%d,%d,%d,%d,%d,%d,%d,%.9g,%d,%.6f,%lu\n",event,e->tick,retro_frame_seconds(e->tick),
            e->time,m[0xe],e->x_pos,e->episode_area_transitions,e->episode_novel_areas,
            m[0x787],m[0x77f],e->agents[0].rewards[0],rta_frames,retro_frame_seconds(rta_frames),
            e->emu->video_frame_count()-core_start);
    };
    row("start");
    for(int frame=0;frame<6000;frame++) {
        const auto* m=e->emu->low_mem();
        int old_state=m[0xe],old_areas=e->episode_area_transitions,old_clears=e->episode_clears;
        int old_world=e->world,old_stage=e->stage;
        int old_split=e->rta.previous_split_tick;
        retro_policy_act(net,obs,action,true);
        puf_step(e);
        if(e->agents[0].terminals[0]) { row("episode_reset"); break; }
        if(e->emu->video_frame_count()-core_start!=(unsigned long)e->tick)
            throw std::runtime_error("RTA audit: wrapper frames disagree with NES video scheduler");
        if(m[0xe]!=old_state) row(m[0xe]==3?"down_pipe_entry":m[0xe]==2?"side_pipe_entry":"routine_change");
        if(e->episode_area_transitions!=old_areas) row("area_playable");
        if(e->rta.previous_split_tick!=old_split) {
            row("rta_split");
            if(!e->completion_on_rta_split&&!e->completion_on_next_playable) break;
        }
        if(e->episode_clears!=old_clears) { row("clear"); if(e->completion_on_next_playable||e->completion_on_rta_split) break; }
        if(e->world!=old_world||e->stage!=old_stage) {
            row("next_level_load");
        }
        if(e->is_dead) { row("death"); break; }
        if(frame==5999) row("audit_limit");
    }
}
int main(int argc,char** argv) {
    try {
        Ini ini={0}; puf_ini_load_env(&ini,RETRO_ENV_NAME,0,nullptr);
        const char* mode=argc>1?argv[1]:"play";
        if(!strcmp(mode,"--help")||!strcmp(mode,"-h")) { usage(); puf_ini_free(&ini); return 0; }
        if(!strcmp(mode,"bench")) return bench(argc,argv,&ini);
        bool watch=!strcmp(mode,"watch"),replay=!strcmp(mode,"replay"),levels=!strcmp(mode,"levels");
        if(!watch&&!replay&&!levels&&strcmp(mode,"play")) { usage(); return 1; }
        bool random=false,deterministic=false,inspect=false,inspect_check=false,single_life=false,timing=false;
        bool full_run=false;
        const char* inspect_snapshot=nullptr;
        const char* selected_level=nullptr;
        const char* checkpoint="latest";
        int first_arg=2;
        if(watch&&argc>2&&argv[2][0]!='-') { checkpoint=argv[2]; first_arg=3; }
        else if(replay) first_arg=3;
        for(int i=first_arg;i<argc;i++) {
            if(!strcmp(argv[i],"--help")||!strcmp(argv[i],"-h")) { usage(); puf_ini_free(&ini); return 0; }
            else if(!strcmp(argv[i],"--random")) random=true;
            else if(!strcmp(argv[i],"--level")) {
                if(++i>=argc||argv[i][0]=='-') throw std::runtime_error("--level needs a level such as 4-2");
                if(selected_level) throw std::runtime_error("specify the starting level only once");
                selected_level=argv[i];
            }
            else if(!strcmp(argv[i],"--single-life")) single_life=true;
            else if(!strcmp(argv[i],"--full-run")) full_run=true;
            else if(!strcmp(argv[i],"--deterministic")) deterministic=true;
            else if(!strcmp(argv[i],"--timing")) timing=deterministic=true;
            else if(!strcmp(argv[i],"--inspect")) inspect=true;
            else if(!strcmp(argv[i],"--inspect-check")) inspect=inspect_check=true;
            else if(!strcmp(argv[i],"--inspect-snapshot")) {
                if(++i>=argc) throw std::runtime_error("--inspect-snapshot needs an output PNG path");
                inspect=true; inspect_snapshot=argv[i];
            }
            else if(!strcmp(argv[i],"--continue")) {} // Natural lives/level continuation is now the default.
            else if(argv[i][0]!='-'&&!selected_level&&!levels) selected_level=argv[i];
            else throw std::runtime_error(std::string("unknown argument: ")+argv[i]);
        }
        if(random&&selected_level) throw std::runtime_error("--random cannot be combined with an explicit starting level");
        if(inspect&&!watch&&strcmp(mode,"play")) throw std::runtime_error("--inspect requires watch or play mode");
        if(inspect_check&&inspect_snapshot) throw std::runtime_error("--inspect-check and --inspect-snapshot are mutually exclusive");
        if(timing&&(!watch||single_life||inspect||random))
            throw std::runtime_error("--timing requires watch with one level and natural continuation");
        if(full_run||levels) puf_ini_set(puf_ini_section(&ini,"env",0),"practice_replay","None");
        if(full_run) {
            puf_ini_put(&ini,"env.max_frames","3000");
            puf_ini_put(&ini,"env.completion_time_max_frames","3000");
        }
        DictItem* practice_option=dict_find(puf_ini_section(&ini,"env",0),"practice_replay");
        bool practice=practice_option&&practice_option->str&&*practice_option->str&&strcmp(practice_option->str,"None");
        if(practice&&!timing) single_life=true;
        // Bound episodes only in the explicit regression mode, to exercise
        // reset images and recurrent resets even with a competent checkpoint.
        if(inspect_check) puf_ini_put(&ini,"env.max_frames","128");
        else if(!single_life&&!practice) {
            puf_ini_put(&ini,"env.max_frames",std::to_string(INT_MAX).c_str());
            // Training may stop at the first flag via terminate_on_clear.
            // The viewer keeps the ROM natural-life continuation by default;
            // use --single-life for training-style clear/death boundaries.
            puf_ini_put(&ini,"env.terminate_on_clear","0");
        }
        puf_ini_put(&ini,"env.spawn_levels",levels||random?"all":selected_level?selected_level:"1-1");
        Env env={}; float obs[OBS_SIZE]={0},action=0,reward=0,terminal=0;
        env.rng=73; puf_init(&env,puf_ini_section(&ini,"env",0));
        if(timing&&practice) env.terminate_on_clear=false;
        env.agents[0].observations=obs; env.agents[0].actions=&action;
        env.agents[0].rewards=&reward; env.agents[0].terminals=&terminal; puf_reset(&env);
        if(levels) {
            for(int i=0;i<32;i++) {
                const auto& s=*retro_rom().starts[i];
                printf("%d-%d area=%d data=$%04x x=%d\n",s.world,s.stage,s.area,s.data,s.x);
            }
            puf_close(&env); puf_ini_free(&ini); return 0;
        }
        if(replay) {
            if(argc<3) throw std::runtime_error("replay needs an input tape");
            FILE* f=fopen(argv[2],"r"); if(!f) throw std::runtime_error("cannot open replay tape");
            unsigned int mask; int frame=0,status;
            while((status=fscanf(f,"%u",&mask))==1) {
                if(mask>255) throw std::runtime_error("replay masks must be 0..255");
                retro_frame(&env,(unsigned char)mask); retro_sync_from_emu(&env);
                unsigned hash=2166136261u; for(int i=0;i<2048;i++) hash=(hash^env.emu->low_mem()[i])*16777619u;
                printf("%d,%u,%08x,%d,%d,%d\n",frame++,mask,hash,env.world,env.stage,env.x_pos);
            }
            if(status!=EOF||ferror(f)) { fclose(f); throw std::runtime_error("malformed replay tape"); }
            fclose(f); puf_close(&env); puf_ini_free(&ini); return 0;
        }
        RetroPolicy* net=nullptr; Weights* weights=nullptr;
        if(watch) {
            std::string path=strcmp(checkpoint,"latest")?checkpoint:newest_checkpoint(&ini);
            size_t hidden=puf_ini_get_int(&ini,"policy","hidden_size");
            size_t layers=puf_ini_get_int(&ini,"policy","num_layers");
            size_t expected=retro_policy_weights(hidden,layers);
            if(std::filesystem::file_size(path)!=expected*sizeof(float))
                throw std::runtime_error("checkpoint is not the current CNN policy; start fresh training with load_model_path=None");
            weights=load_weights(path.c_str()); if(!weights) throw std::runtime_error("checkpoint load failed");
            fprintf(stderr,"Watching checkpoint: %s (%s actions)\n",path.c_str(),deterministic?"argmax":"sampled");
            net=make_retro_policy(weights,hidden,layers);
        }
        fprintf(stderr,"Playback: start %d-%d; %s\n",env.world,env.stage,
            single_life?"single-life training episodes":"natural lives (new game on game over; R restarts)");
        RetroPlayback playback={single_life,false};
        if(timing) {
            retro_timing_audit(&env,net,obs,&action);
            puf_close(&env); free_retro_policy(net); free(weights); puf_ini_free(&ini);
            return 0;
        }
        bool display=(getenv("DISPLAY")&&*getenv("DISPLAY"))||(getenv("WAYLAND_DISPLAY")&&*getenv("WAYLAND_DISPLAY"));
        if(inspect) {
            if(!display&&!inspect_check) throw std::runtime_error("--inspect needs a display; use --inspect-check for headless validation");
            int result=retro_inspect(env,net,obs,&action,&reward,&terminal,playback,deterministic,inspect_check,inspect_snapshot);
            puf_close(&env); free_retro_policy(net); free(weights); puf_ini_free(&ini);
            return result;
        }
        if(display) { SetTraceLogLevel(LOG_ERROR); puf_render(&env); }
        for(int i=0;display?!WindowShouldClose():i<600;i++) {
            if(watch) {
                retro_policy_act(net,obs,&action,deterministic);
            } else action=retro_mask_action(display?human_buttons():RETRO_BTN_RIGHT);
            if(retro_playback_step(&env,&playback)) retro_inspect_clear_rnn(net);
            if(display) {
                if(IsKeyPressed(KEY_R)) { puf_reset(&env); playback.waiting_respawn=false; retro_inspect_clear_rnn(net); }
                puf_render(&env);
            }
        }
        fprintf(stderr,"ROM world=%d-%d x=%d episodes=%.0f clears=%.0f\n",env.world,env.stage,env.x_pos,env.log.n,env.log.clears);
        puf_close(&env); free_retro_policy(net); if(weights) free(weights); puf_ini_free(&ini);
        if(display) CloseWindow();
        return 0;
    } catch(const std::exception& e) { fprintf(stderr,"%s\n",e.what()); return 1; }
}
#endif
