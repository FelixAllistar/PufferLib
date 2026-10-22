// Diagnostic: play one level from its start with a checkpoint and log where
// attempts die and where Mario's x jumps backwards (castle maze loops).
// Same policy inference and sampling as sweep_eval; never changes ROM RAM.
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
#include <filesystem>
#include <omp.h>

int main(int argc,char** argv) {
    try {
        if(argc<4) throw std::runtime_error("usage: trace_level CHECKPOINT LEVEL(e.g. 4-4) ATTEMPTS [SEED] [FRAMES]");
        const char* level_name=argv[2]; int n=atoi(argv[3]);
        unsigned seed=argc>4?strtoul(argv[4],nullptr,10):20261023; int frames=argc>5?atoi(argv[5]):30000;
        std::string config=std::string(argv[1])+".ini";
        Ini ini={}; puf_ini_load_env(&ini,"retro",0,nullptr);
        puf_ini_set(puf_ini_section(&ini,"env",0),"practice_replay","None");
        puf_ini_load_file(&ini,config.c_str());
        puf_ini_put(&ini,"env.archive_enabled","0");
        puf_ini_set(puf_ini_section(&ini,"env",0),"practice_replay","None");
        puf_ini_put(&ini,"env.spawn_levels",level_name);
        puf_ini_put(&ini,"env.backend","quicknes"); puf_ini_put(&ini,"env.full_render","0");
        puf_ini_put(&ini,"env.max_frames",std::to_string(frames).c_str());
        puf_ini_put(&ini,"env.checkpoint_distance","128");
        puf_ini_put(&ini,"env.terminate_on_clear","1");
        int hidden=puf_ini_get(&ini,"policy","hidden_size"),layers=puf_ini_get(&ini,"policy","num_layers");
        RetroPolicy* first=retro_cpu_load(argv[1],hidden,layers);
        if(!first) throw std::runtime_error("checkpoint read failed");
        Dict vec={}; dict_set(&vec,"total_agents",n); dict_set(&vec,"num_buffers",1);
        int count,begin[1],sizes[1]; Env* envs=my_vec_init(&count,begin,sizes,&vec,puf_ini_section(&ini,"env",0));
        std::vector<float> obs(n*OBS_SIZE),actions(n),rewards(n),terminals(n);
        std::vector<std::string> out(n);
        for(int i=0;i<n;i++){Env* e=&envs[i];e->spawn_pin=1;e->cur_spawn=0;
            e->agents[0]={obs.data()+i*OBS_SIZE,&actions[i],&rewards[i],&terminals[i],nullptr,0};puf_reset(e);}
        #pragma omp parallel for num_threads(12) schedule(dynamic,1)
        for(int i=0;i<n;i++) {
            Env* e=&envs[i]; RetroPolicy* net=i?retro_cpu_clone(first,hidden,layers):first;
            unsigned rng=seed^((unsigned)(i+1)*2654435761u); int elapsed=0,furthest=e->x_pos,last_x=e->x_pos;
            bool was_dead=false,respawn=false; int loops=0; char line[256];
            std::string events;
            // SMB1 maze-loop RAM (disassembly names): MultiLoopCorrectCntr $06D9,
            // MultiLoopPassCntr $06DA, LoopCommand $0745. Logged whenever they change.
            const int loop_ram[3]={0x6d9,0x6da,0x745}; int loop_seen[3];
            for(int k=0;k<3;k++) loop_seen[k]=e->emu->low_mem()[loop_ram[k]];
            while(elapsed<frames) {
                const float* logits=retro_cpu_logits(net,obs.data()+i*OBS_SIZE);
                actions[i]=retro_panel_action(logits,retro_random(&rng),false);
                int prev_area=e->area; const auto* m0=e->emu->low_mem(); int prev_y=m0[0xce],prev_yhi=m0[0xb5],prev_state=m0[0x1d];
                puf_step(e); elapsed+=e->last_frames;
                const auto* m=e->emu->low_mem(); int x=e->x_pos;
                bool dead=robs_dead(m);
                if(dead&&!was_dead){snprintf(line,sizeof(line)," DEATH@x=%d,y=%d,f=%d",last_x,prev_y,elapsed);events+=line;respawn=true;}
                else if(!dead&&!respawn&&!terminals[i]&&e->area==prev_area&&x<last_x-200){loops++;snprintf(line,sizeof(line)," LOOP %d->%d(y=%d,yhi=%d,state=%d)",last_x,x,prev_y,prev_yhi,prev_state);events+=line;}
                for(int k=0;k<3;k++) if(m[loop_ram[k]]!=loop_seen[k]) {
                    snprintf(line,sizeof(line)," RAM[%03X]%d->%d@x=%d,y=%d",loop_ram[k],loop_seen[k],m[loop_ram[k]],x,m[0xce]);
                    events+=line; loop_seen[k]=m[loop_ram[k]];
                }
                if(terminals[i]) {snprintf(line,sizeof(line)," END(clear=%d,frames=%d)",(int)(e->log.level_clears[retro_level_id(e->world,e->stage)]>0),elapsed);events+=line;break;}
                was_dead=dead; if(x>furthest)furthest=x; if(!dead){if(respawn&&x>0)respawn=false; last_x=x;}
            }
            snprintf(line,sizeof(line),"attempt %d furthest_x=%d loops=%d:",i,furthest,loops);
            out[i]=line+events;
        }
        for(auto& s:out) puts(s.c_str());
        return 0;
    } catch(const std::exception& ex){fprintf(stderr,"trace_level: %s\n",ex.what());return 1;}
}
