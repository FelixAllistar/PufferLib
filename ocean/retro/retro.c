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
#include "retro_inspect.h"

static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static void usage() {
    fprintf(stderr,"retro play [level]\nretro watch PATH.bin|latest [--random|--continue] [--deterministic] [--inspect|--inspect-check]\n"
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
int main(int argc,char** argv) {
    try {
        Ini ini={0}; puf_ini_load_env(&ini,RETRO_ENV_NAME,0,nullptr);
        const char* mode=argc>1?argv[1]:"play";
        if(!strcmp(mode,"bench")) return bench(argc,argv,&ini);
        bool watch=!strcmp(mode,"watch"),replay=!strcmp(mode,"replay"),levels=!strcmp(mode,"levels");
        if(!watch&&!replay&&!levels&&strcmp(mode,"play")) { usage(); return 1; }
        bool random=false,deterministic=false,inspect=false,inspect_check=false;
        const char* inspect_snapshot=nullptr;
        for(int i=3;i<argc;i++) {
            if(!strcmp(argv[i],"--random")) random=true;
            else if(!strcmp(argv[i],"--deterministic")) deterministic=true;
            else if(!strcmp(argv[i],"--inspect")) inspect=true;
            else if(!strcmp(argv[i],"--inspect-check")) inspect=inspect_check=true;
            else if(!strcmp(argv[i],"--inspect-snapshot")) {
                if(++i>=argc) throw std::runtime_error("--inspect-snapshot needs an output PNG path");
                inspect=true; inspect_snapshot=argv[i];
            }
            else if(!strcmp(argv[i],"--continue")) {} // ROM naturally advances levels.
        }
        if(inspect&&!watch&&strcmp(mode,"play")) throw std::runtime_error("--inspect requires watch or play mode");
        if(inspect_check&&inspect_snapshot) throw std::runtime_error("--inspect-check and --inspect-snapshot are mutually exclusive");
        // Bound episodes only in the explicit regression mode, to exercise
        // reset images and recurrent resets even with a competent checkpoint.
        if(inspect_check) puf_ini_put(&ini,"env.max_frames","128");
        puf_ini_put(&ini,"env.spawn_levels",levels||random?"all":replay?(argc>3?argv[3]:"1-1"):!watch&&argc>2?argv[2]:"1-1");
        Env env={}; float obs[OBS_SIZE]={0},action=0,reward=0,terminal=0;
        env.rng=73; puf_init(&env,puf_ini_section(&ini,"env",0));
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
            std::string path=argc>2&&strcmp(argv[2],"latest")?argv[2]:newest_checkpoint(&ini);
            size_t hidden=puf_ini_get_int(&ini,"policy","hidden_size");
            size_t layers=puf_ini_get_int(&ini,"policy","num_layers");
            size_t expected=retro_policy_weights(hidden,layers);
            if(std::filesystem::file_size(path)!=expected*sizeof(float))
                throw std::runtime_error("checkpoint is not the current CNN policy; start fresh training with load_model_path=None");
            weights=load_weights(path.c_str()); if(!weights) throw std::runtime_error("checkpoint load failed");
            fprintf(stderr,"Watching checkpoint: %s (%s actions)\n",path.c_str(),deterministic?"argmax":"sampled");
            net=make_retro_policy(weights,hidden,layers);
        }
        bool display=(getenv("DISPLAY")&&*getenv("DISPLAY"))||(getenv("WAYLAND_DISPLAY")&&*getenv("WAYLAND_DISPLAY"));
        if(inspect) {
            if(!display&&!inspect_check) throw std::runtime_error("--inspect needs a display; use --inspect-check for headless validation");
            int result=retro_inspect(env,net,obs,&action,&reward,&terminal,deterministic,inspect_check,inspect_snapshot);
            puf_close(&env); free_retro_policy(net); free(weights); puf_ini_free(&ini);
            return result;
        }
        if(display) { SetTraceLogLevel(LOG_ERROR); puf_render(&env); }
        for(int i=0;display?!WindowShouldClose():i<600;i++) {
            if(watch) {
                retro_policy_act(net,obs,&action,deterministic);
            } else action=retro_mask_action(display?human_buttons():RETRO_BTN_RIGHT);
            puf_step(&env);
            if(terminal&&net) memset(net->mingru->state,0,net->mingru->num_layers*net->mingru->batch_size*net->mingru->hidden_size*sizeof(float));
            if(display) { if(IsKeyPressed(KEY_R)) { puf_reset(&env); if(net) memset(net->mingru->state,0,net->mingru->num_layers*net->mingru->hidden_size*sizeof(float)); } puf_render(&env); }
        }
        fprintf(stderr,"ROM world=%d-%d x=%d episodes=%.0f clears=%.0f\n",env.world,env.stage,env.x_pos,env.log.n,env.log.clears);
        puf_close(&env); free_retro_policy(net); if(weights) free(weights); puf_ini_free(&ini);
        if(display) CloseWindow();
        return 0;
    } catch(const std::exception& e) { fprintf(stderr,"%s\n",e.what()); return 1; }
}
#endif
