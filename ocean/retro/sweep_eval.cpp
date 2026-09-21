// Balanced post-training panel over the configured starting levels. This is a policy evaluation, not a
// hardware/ROM parity certificate. Never enters the viewer or changes ROM RAM.
#define PUFFERLIB_BUILD_MAIN
#include "retro.h"
#undef PUFFERLIB_BUILD_MAIN
#include "puffercpu.h"
#include "retro_policy_cpu.h"
#include "retro_sweep_config.h"
#include <exception>
#include <filesystem>
#include <omp.h>

static long number(const char* text) {
    char* end=nullptr; long n=strtol(text,&end,10);
    if(!*text||*end||n<0||n>1000000000) throw std::runtime_error("invalid panel argument");
    return n;
}

int main(int argc,char** argv) {
    try {
        if(argc<2) throw std::runtime_error("usage: build/retro/sweep_eval CHECKPOINT [--config INI] [--levels CSV|all] [--frameskip N] [--metric speed|distance|perf] [--frames 3600] [--repeats 2] [--seed 20260907] [--workers 4] [--output TSV] [--deterministic]");
        std::string config=std::string(argv[1])+".ini",output;
        std::string levels_override,metric_override,skip_override;
        int frames=3600,repeats=2,workers=4; unsigned seed=20260907; bool deterministic=false;
        for(int i=2;i<argc;i++) {
            std::string arg=argv[i];
            if(arg=="--deterministic") { deterministic=true; continue; }
            if(i+1==argc) throw std::runtime_error("missing panel option value");
            const char* value=argv[++i];
            if(arg=="--config") config=value;
            else if(arg=="--levels") levels_override=value;
            else if(arg=="--metric") metric_override=value;
            else if(arg=="--frameskip") skip_override=value;
            else if(arg=="--output") output=value;
            else if(arg=="--frames") frames=number(value);
            else if(arg=="--repeats") repeats=number(value);
            else if(arg=="--seed") seed=number(value);
            else if(arg=="--workers") workers=number(value);
            else throw std::runtime_error("unknown panel option");
        }
        if(frames<1||frames>30000||repeats<1||repeats>32||workers<1||workers>64)
            throw std::runtime_error("panel limits: frames 1..30000, repeats 1..32, workers 1..64");
        if(!std::filesystem::exists(config))
            throw std::runtime_error("checkpoint config missing; pass --config logs/retro/RUN.ini for an older checkpoint");
        Ini ini={}; puf_ini_load_env(&ini,"retro",0,nullptr); puf_ini_load_file(&ini,config.c_str());
        // Preserve the task/control timing recorded with the checkpoint.
        // Explicit overrides support separate all-level transfer diagnostics.
        if(!levels_override.empty()) puf_ini_put(&ini,"env.spawn_levels",levels_override.c_str());
        if(!skip_override.empty()) puf_ini_put(&ini,"env.frameskip",skip_override.c_str());
        if(!metric_override.empty()) puf_ini_put(&ini,"sweep.metric",metric_override.c_str());
        double skip=puf_ini_get(&ini,"env","frameskip");
        if(!std::isfinite(skip)||skip<1||skip>16||skip!=floor(skip))
            throw std::runtime_error("panel frameskip must be an integer in 1..16");
        const char* metric=puf_ini_get_str(&ini,"sweep","metric");
        retro_panel_objective(metric,0,1,0,0,0,frames); // Validate before inference.
        Env selection={}; retro_parse_spawns(&selection,puf_ini_get_str(&ini,"env","spawn_levels"));
        int level_count=selection.spawn_n;
        std::vector<int> level_ids(level_count);
        for(int i=0;i<level_count;i++)
            level_ids[i]=retro_level_id(selection.spawn_w[i],selection.spawn_l[i]);
        puf_ini_put(&ini,"env.backend","quicknes"); puf_ini_put(&ini,"env.full_render","0");
        puf_ini_put(&ini,"env.max_frames",std::to_string(frames).c_str());
        int hidden=puf_ini_get_int(&ini,"policy","hidden_size"),layers=puf_ini_get_int(&ini,"policy","num_layers");
        if(hidden<8||hidden>1024||hidden%8||layers<1||layers>8)
            throw std::runtime_error("invalid checkpoint architecture");
        size_t expected=retro_policy_weights(hidden,layers);
        if(std::filesystem::file_size(argv[1])!=expected*sizeof(float))
            throw std::runtime_error("checkpoint shape does not match ROM policy metadata");
        Weights* weights=load_weights(argv[1]); if(!weights) throw std::runtime_error("checkpoint read failed");
        int n=level_count*repeats;
        // Independent recurrent state per attempt; immutable weights shared.
        // Run each case through its terminal in one worker instead of barriers
        // every frame or forwarding dead rows until the slowest case finishes.
        std::vector<RetroPolicy*> nets(n);
        for(int i=0;i<n;i++) {
            Weights cursor=*weights;
            nets[i]=make_retro_policy(&cursor,hidden,layers);
        }
        Dict vec={}; dict_set(&vec,"total_agents",n); dict_set(&vec,"num_buffers",1);
        int count,begin[1],sizes[1]; Env* envs=my_vec_init(&count,begin,sizes,&vec,puf_ini_section(&ini,"env",0));
        std::vector<float> obs(n*OBS_SIZE),actions(n),rewards(n),terminals(n);
        std::vector<unsigned> rng(n);
        std::vector<int> cleared(n),elapsed(n),furthest(n),clear_frames(n);
        std::vector<double> progress(n);
        for(int i=0;i<n;i++) {
            Env* e=&envs[i]; e->spawn_pin=1; e->cur_spawn=i%level_count;
            e->agents[0]={obs.data()+i*OBS_SIZE,&actions[i],&rewards[i],&terminals[i],nullptr,0};
            puf_reset(e); furthest[i]=e->x_pos;
            unsigned case_id=(i/level_count)*32+level_ids[i%level_count]+1;
            rng[i]=seed^(case_id*2654435761u); if(!rng[i]) rng[i]=73;
        }
        omp_set_dynamic(0);
        std::exception_ptr failure;
        #pragma omp parallel for num_threads(workers) schedule(dynamic,1)
        for(int i=0;i<n;i++) {
            try {
                Env* e=&envs[i]; int level=level_ids[i%level_count]; RetroPolicy* net=nets[i];
                while(elapsed[i]<frames) {
                    // The budget is in NES frames, even when decisions span
                    // several frames. puf_step enforces the exact last frame.
                    retro_policy_logits(net,obs.data()+i*OBS_SIZE);
                    actions[i]=retro_panel_action(net->decoder->output,retro_random(&rng[i]),deterministic);
                    float before=e->log.level_clears[level];
                    puf_step(e); elapsed[i]+=e->last_frames;
                    cleared[i]=e->log.level_clears[level]>before;
                    if(cleared[i]) clear_frames[i]=terminals[i]
                        ?(int)e->log.clear_frame_sum:e->episode_clear_frames;
                    // Stop at first source-level clear or terminal. Auto-reset
                    // observations do NOT become a second attempt in this panel.
                    if(cleared[i]||terminals[i]) break;
                    if(e->world==level/4+1&&e->stage==level%4+1&&e->area==e->start->area
                        &&e->emu->low_mem()[0xe7]+256*e->emu->low_mem()[0xe8]==e->start->data)
                        furthest[i]=std::max(furthest[i],e->x_pos);
                }
            } catch(...) {
                #pragma omp critical(retro_panel_failure)
                { if(!failure) failure=std::current_exception(); }
            }
        }
        if(failure) std::rethrow_exception(failure);
        int clears=0; long executed=0; double mean_progress=0,mean_distance=0,clear_frame_sum=0;
        for(int i=0;i<n;i++) {
            clears+=cleared[i]; executed+=elapsed[i];
            clear_frame_sum+=clear_frames[i];
            int start=retro_rom().starts[level_ids[i%level_count]]->x;
            mean_distance+=retro_panel_distance(start,furthest[i])/n;
            progress[i]=retro_panel_progress(start,furthest[i]); mean_progress+=progress[i]/n;
        }
        mean_progress=std::max(0.0,std::min(1.0,mean_progress));
        double mean_clear_frames=clears?clear_frame_sum/clears:0;
        double score=retro_panel_objective(metric,clears,n,mean_progress,mean_distance,mean_clear_frames,frames);
        if(!output.empty()) {
            std::string temporary=output+".tmp"; FILE* file=fopen(temporary.c_str(),"w");
            if(!file) throw std::runtime_error("cannot write panel report");
            fprintf(file,"# retro_panel_v2 frames=%d repeats=%d seed=%u deterministic=%d levels=%s frameskip=%d\n",
                frames,repeats,seed,deterministic,puf_ini_get_str(&ini,"env","spawn_levels"),(int)skip);
            fprintf(file,"level\treplicate\tclear\tprogress\tframes\tclear_frames\n");
            for(int i=0;i<n;i++) {
                int level=level_ids[i%level_count];
                fprintf(file,"%d-%d\t%d\t%d\t%.9g\t%d\t%d\n",level/4+1,level%4+1,
                    i/level_count,cleared[i],progress[i],elapsed[i],clear_frames[i]);
            }
            fprintf(file,"# score=%.9g clears=%d attempts=%d progress=%.9g frames=%ld\n",score,clears,n,mean_progress,executed);
            fprintf(file,"# metric=%s distance=%.9g units=forward_pixels\n",metric,mean_distance);
            fprintf(file,"# clear_frames=%.9g clear_rate=%.9g\n",mean_clear_frames,(double)clears/n);
            bool failed=ferror(file)!=0; if(fclose(file)) failed=true;
            if(failed||rename(temporary.c_str(),output.c_str())) throw std::runtime_error("cannot publish panel report");
        }
        printf("retro_panel version=1 score=%.9g clears=%d attempts=%d progress=%.9g frames=%ld seed=%u deterministic=%d metric=%s distance=%.9g clear_frames=%.9g levels=%s frameskip=%d\n",
            score,clears,n,mean_progress,executed,seed,deterministic,metric,mean_distance,
            mean_clear_frames,puf_ini_get_str(&ini,"env","spawn_levels"),(int)skip);
        my_vec_close(envs); dict_clear(&vec);
        for(RetroPolicy* net:nets) free_retro_policy(net);
        free(weights); puf_ini_free(&ini);
        return 0;
    } catch(const std::exception& e) { fprintf(stderr,"retro panel: %s\n",e.what()); return 1; }
}
