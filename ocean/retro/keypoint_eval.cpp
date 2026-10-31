// Key-point pass rates (2026-10-31): from the states of a keypoint_states archive, how often a policy gets PASS px
// past its start (or clears the level) before the attempt ends. Same rules as env.archive_practice: the first lost
// life or a maze loop-back ends it. Prints one line per key point: attempts, passes, rate.
// Usage: keypoint_eval CHECKPOINT KEYPOINTS.archive.gz [ATTEMPTS_PER_STATE=8] [PASS=300] [SEED] [FRAMES=3600]
#define PUFFERLIB_BUILD_MAIN
#include "retro.h"
#undef PUFFERLIB_BUILD_MAIN
#include "retro_policy_shape.h"
typedef void RetroPolicy;
extern "C" {
RetroPolicy* retro_cpu_load(const char* path, int hidden, int layers);
RetroPolicy* retro_cpu_clone(RetroPolicy* source, int hidden, int layers);
const float* retro_cpu_logits(RetroPolicy* policy, const float* obs);
void retro_cpu_free(RetroPolicy* policy);
}
#include "retro_sweep_config.h"
#include <map>
#include <omp.h>

int main(int argc,char** argv) {
    try {
        if(argc<3) throw std::runtime_error("usage: keypoint_eval CHECKPOINT KEYPOINTS.archive.gz [ATTEMPTS] [PASS] [SEED] [FRAMES]");
        int per_state=argc>3?atoi(argv[3]):8, pass_px=argc>4?atoi(argv[4]):300;
        unsigned seed=argc>5?strtoul(argv[5],nullptr,10):20261031; int frames=argc>6?atoi(argv[6]):3600;
        std::string config=std::string(argv[1])+".ini";
        Ini ini={}; puf_ini_load_env(&ini,"retro",0,nullptr);
        puf_ini_set(puf_ini_section(&ini,"env",0),"practice_replay","None");
        puf_ini_load_file(&ini,config.c_str());
        puf_ini_set(puf_ini_section(&ini,"env",0),"practice_replay","None");
        puf_ini_put(&ini,"env.archive_enabled","1"); puf_ini_put(&ini,"env.archive_practice","1");
        puf_ini_put(&ini,"env.archive_start","1"); puf_ini_put(&ini,"env.archive_capacity_mb","1024");
        puf_ini_set(puf_ini_section(&ini,"env",0),"archive_init_path",argv[2]);
        puf_ini_put(&ini,"env.spawn_levels","4-4,7-4,8-4,5-3,8-1,8-3");
        puf_ini_put(&ini,"env.full_render","0"); puf_ini_put(&ini,"env.terminate_on_clear","1");
        puf_ini_put(&ini,"env.max_frames",std::to_string(frames).c_str());
        int hidden=puf_ini_get(&ini,"policy","hidden_size"),layers=puf_ini_get(&ini,"policy","num_layers");
        RetroPolicy* first=retro_cpu_load(argv[1],hidden,layers);
        if(!first) throw std::runtime_error("checkpoint read failed");
        Dict vec={}; dict_set(&vec,"total_agents",1); dict_set(&vec,"num_buffers",1);
        int count,begin[1],sizes[1]; Env* probe=my_vec_init(&count,begin,sizes,&vec,puf_ini_section(&ini,"env",0));
        auto& b=retro_archive_bank(); size_t cells=b.cells.size(); int n=(int)cells*per_state;
        if(!cells) throw std::runtime_error("empty key-point archive");
        (void)probe;
        dict_set(&vec,"total_agents",n);
        Env* envs=my_vec_init(&count,begin,sizes,&vec,puf_ini_section(&ini,"env",0));
        std::vector<float> obs(n*OBS_SIZE),actions(n),rewards(n),terminals(n);
        std::vector<int> point(n),passed(n),furthest(n),endx(n),ended(n);
        // KP_TRACE=<point>: also print each attempt of that point (start x, furthest x, last x, how it ended)
        const char* trace=getenv("KP_TRACE");
        #pragma omp parallel for schedule(dynamic,1)
        for(int i=0;i<n;i++) {
            Env* e=&envs[i]; RetroPolicy* net=i?retro_cpu_clone(first,hidden,layers):first;
            e->agents[0]={obs.data()+i*OBS_SIZE,&actions[i],&rewards[i],&terminals[i],nullptr,0};
            // Pin this attempt to cell i / per_state: the bank's choice is replaced by that cell's state.
            puf_reset(e);
            auto& c=b.cells[i/per_state]; point[i]=(int)((c.key>>52)&255);
            e->archive->state=c.state; e->start=&c.state->start;
            e->emu->load_state(e->start->state); e->reset_image=true; retro_sync_from_emu(e);
            if(e->rom_blocks&&!e->emu->set_rom_blocks(true)) throw std::runtime_error("rom blocks");
            e->x_pos_max=e->x_pos; e->tick=0; e->frontier_count=0; e->progress_pixels=0;
            retro_track_progress(e); retro_archive_restore(e);
            retro_compute_obs_real(e,(obs_t*)e->agents[0].observations);
            int start_x=e->x_pos, level=retro_level_id(e->world,e->stage), elapsed=0;
            unsigned rng=seed^((unsigned)(i+1)*2654435761u);
            float before=e->log.level_clears[level];
            while(elapsed<frames) {
                const float* logits=retro_cpu_logits(net,obs.data()+i*OBS_SIZE);
                actions[i]=retro_panel_action(logits,retro_random(&rng),false);
                puf_step(e); elapsed+=e->last_frames;
                furthest[i]=std::max(furthest[i],e->x_pos);
                if(e->log.level_clears[level]>before) { passed[i]=1; break; }
                if(terminals[i]) { ended[i]=1; break; }
                endx[i]=e->x_pos;
                if(retro_level_id(e->world,e->stage)==level&&e->x_pos>=start_x+pass_px&&e->x_pos<start_x+1000) { passed[i]=1; break; }
            }
            if(i) retro_cpu_free(net);
        }
        if(trace) for(int i=0;i<n;i++) if(point[i]==atoi(trace))
            printf("point %d attempt %d: furthest %d last %d %s %s\n",point[i],i,furthest[i],endx[i],
                passed[i]?"PASS":"fail",ended[i]?"(episode ended: death/loop)":"(time)");
        std::map<int,std::pair<int,int>> rate;
        for(int i=0;i<n;i++) { rate[point[i]].first++; rate[point[i]].second+=passed[i]; }
        int total=0,good=0;
        for(auto& [p,r]:rate) { printf("key point %d: %d/%d = %.2f\n",p,r.second,r.first,(double)r.second/r.first); total+=r.first; good+=r.second; }
        printf("mean over key points %.3f (%d/%d attempts)\n",[&]{double s=0;for(auto& [p,r]:rate)s+=(double)r.second/r.first;return s/rate.size();}(),good,total);
        return 0;
    } catch(const std::exception& ex){fprintf(stderr,"keypoint_eval: %s\n",ex.what());return 1;}
}
