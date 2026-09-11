// Fixed, balanced post-training panel. This is a policy evaluation, not a
// hardware/ROM parity certificate. Never enters the viewer or changes ROM RAM.
#define PUFFERLIB_BUILD_MAIN
#include "retro.h"
#undef PUFFERLIB_BUILD_MAIN
#include "puffercpu.h"
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
        if(argc<2) throw std::runtime_error("usage: build/retro/sweep_eval CHECKPOINT [--config INI] [--frames 3600] [--repeats 2] [--seed 20260907] [--workers 4] [--output TSV] [--deterministic]");
        std::string config=std::string(argv[1])+".ini",output;
        int frames=3600,repeats=2,workers=4; unsigned seed=20260907; bool deterministic=false;
        for(int i=2;i<argc;i++) {
            std::string arg=argv[i];
            if(arg=="--deterministic") { deterministic=true; continue; }
            if(i+1==argc) throw std::runtime_error("missing panel option value");
            const char* value=argv[++i];
            if(arg=="--config") config=value;
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
        // This contract is fixed for every trial, independent of rollout shape
        // and training gamma/reward scale. No retuning eval to the candidate.
        puf_ini_put(&ini,"env.backend","quicknes"); puf_ini_put(&ini,"env.spawn_levels","all");
        puf_ini_put(&ini,"env.frameskip","1"); puf_ini_put(&ini,"env.full_render","0");
        puf_ini_put(&ini,"env.max_frames",std::to_string(frames).c_str());
        int hidden=puf_ini_get_int(&ini,"policy","hidden_size"),layers=puf_ini_get_int(&ini,"policy","num_layers");
        if(hidden<8||hidden>1024||hidden%8||layers<1||layers>8)
            throw std::runtime_error("invalid checkpoint architecture");
        auto aligned=[](size_t n) { return (n+7)&~size_t(7); };
        size_t expected=aligned(OBS_SIZE*hidden)+aligned(65*hidden)+layers*aligned(3*hidden*hidden);
        if(std::filesystem::file_size(argv[1])!=expected*sizeof(float))
            throw std::runtime_error("checkpoint shape does not match ROM policy metadata");
        Weights* weights=load_weights(argv[1]); if(!weights) throw std::runtime_error("checkpoint read failed");
        int n=32*repeats,actions_sizes[]=ACT_SIZES;
        // Independent recurrent state per attempt; immutable weights shared.
        // Run each case through its terminal in one worker instead of barriers
        // every frame or forwarding dead rows until the slowest case finishes.
        std::vector<PufferNet*> nets(n);
        for(int i=0;i<n;i++) {
            Weights cursor=*weights;
            nets[i]=make_puffernet(&cursor,1,OBS_SIZE,hidden,layers,actions_sizes,1);
        }
        Dict vec={}; dict_set(&vec,"total_agents",n); dict_set(&vec,"num_buffers",1);
        int count,begin[1],sizes[1]; Env* envs=my_vec_init(&count,begin,sizes,&vec,puf_ini_section(&ini,"env",0));
        std::vector<float> obs(n*OBS_SIZE),actions(n),rewards(n),terminals(n);
        std::vector<unsigned> rng(n);
        std::vector<int> cleared(n),elapsed(n),furthest(n);
        std::vector<double> progress(n);
        for(int i=0;i<n;i++) {
            Env* e=&envs[i]; e->spawn_pin=1; e->cur_spawn=i%32;
            e->agents[0]={obs.data()+i*OBS_SIZE,&actions[i],&rewards[i],&terminals[i],nullptr,0};
            puf_reset(e); furthest[i]=e->x_pos;
            rng[i]=seed^((unsigned)(i+1)*2654435761u); if(!rng[i]) rng[i]=73;
        }
        omp_set_dynamic(0);
        std::exception_ptr failure;
        #pragma omp parallel for num_threads(workers) schedule(dynamic,1)
        for(int i=0;i<n;i++) {
            try {
                Env* e=&envs[i]; int level=i%32; PufferNet* net=nets[i];
                for(int f=0;f<frames;f++) {
                    linear(net->encoder,obs.data()+i*OBS_SIZE);
                    mingru(net->mingru,net->encoder->output);
                    linear(net->decoder,net->mingru->output);
                    actions[i]=retro_panel_action(net->decoder->output,retro_random(&rng[i]),deterministic);
                    float before=e->log.level_clears[level];
                    puf_step(e); elapsed[i]+=e->last_frames;
                    cleared[i]=e->log.level_clears[level]>before;
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
        int clears=0; long executed=0; double mean_progress=0;
        for(int i=0;i<n;i++) {
            clears+=cleared[i]; executed+=elapsed[i];
            int start=retro_rom().starts[i%32]->x;
            progress[i]=retro_panel_progress(start,furthest[i]); mean_progress+=progress[i]/n;
        }
        mean_progress=std::max(0.0,std::min(1.0,mean_progress));
        double score=retro_panel_score(clears,n,mean_progress);
        if(!output.empty()) {
            std::string temporary=output+".tmp"; FILE* file=fopen(temporary.c_str(),"w");
            if(!file) throw std::runtime_error("cannot write panel report");
            fprintf(file,"# retro_panel_v1 frames=%d repeats=%d seed=%u deterministic=%d\n",frames,repeats,seed,deterministic);
            fprintf(file,"level\treplicate\tclear\tprogress\tframes\n");
            for(int i=0;i<n;i++) fprintf(file,"%d-%d\t%d\t%d\t%.9g\t%d\n",i%32/4+1,i%4+1,i/32,cleared[i],progress[i],elapsed[i]);
            fprintf(file,"# score=%.9g clears=%d attempts=%d progress=%.9g frames=%ld\n",score,clears,n,mean_progress,executed);
            bool failed=ferror(file)!=0; if(fclose(file)) failed=true;
            if(failed||rename(temporary.c_str(),output.c_str())) throw std::runtime_error("cannot publish panel report");
        }
        printf("retro_panel version=1 score=%.9g clears=%d attempts=%d progress=%.9g frames=%ld seed=%u deterministic=%d\n",
            score,clears,n,mean_progress,executed,seed,deterministic);
        my_vec_close(envs); dict_clear(&vec);
        for(PufferNet* net:nets) free_puffernet(net);
        free(weights); puf_ini_free(&ini);
        return 0;
    } catch(const std::exception& e) { fprintf(stderr,"retro panel: %s\n",e.what()); return 1; }
}
