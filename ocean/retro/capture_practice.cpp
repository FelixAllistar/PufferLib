// Capture a naturally reached return-pipe black screen as reproducible inputs.
#define PUFFERLIB_BUILD_MAIN
#include "retro.h"
#undef PUFFERLIB_BUILD_MAIN
#include "retro_policy_cpu.h"
#include "retro_sweep_config.h"
#include <filesystem>

int main(int argc,char** argv) {
    try {
        if(argc!=3) throw std::runtime_error("usage: capture_practice CHECKPOINT NEW_REPLAY_PATH");
        if(std::filesystem::exists(argv[2])) throw std::runtime_error("refusing to overwrite replay");
        Ini ini={}; puf_ini_load_env(&ini,"retro",0,nullptr);
        puf_ini_set(puf_ini_section(&ini,"env",0),"practice_replay","None");
        puf_ini_put(&ini,"env.spawn_levels","1-1");
        puf_ini_put(&ini,"env.frameskip","1");
        puf_ini_put(&ini,"env.full_render","1");
        puf_ini_put(&ini,"env.terminate_on_clear","1");
        puf_ini_put(&ini,"env.max_frames","3000");
        int hidden=puf_ini_get_int(&ini,"policy","hidden_size"),layers=puf_ini_get_int(&ini,"policy","num_layers");
        if(std::filesystem::file_size(argv[1])!=retro_policy_weights(hidden,layers)*sizeof(float))
            throw std::runtime_error("checkpoint shape mismatch");
        Weights* weights=load_weights(argv[1]);
        RetroPolicy* policy=make_retro_policy(weights,hidden,layers);
        Env env={}; puf_init(&env,puf_ini_section(&ini,"env",0));
        std::vector<float> obs(OBS_SIZE); float action=0,reward=0,done=0;
        env.agents[0]={obs.data(),&action,&reward,&done,nullptr,0}; puf_reset(&env);
        // Same sampled action stream as round-1 panel replicate 0, a PB clear
        // for the run-12 parent. No edits to Mario or the game timer.
        unsigned rng=20260907u^2654435761u;
        std::vector<int> inputs; bool side=false;
        for(int f=0;f<3000;f++) {
            retro_policy_logits(policy,obs.data());
            action=retro_panel_action(policy->decoder->output,retro_random(&rng),false);
            inputs.push_back(retro_action_mask((int)action));
            puf_step(&env);
            if(done) throw std::runtime_error("policy terminated before return-pipe black screen");
            const auto* m=env.emu->low_mem(); side|=env.pipe_phase==2&&m[0xe]==2;
            const auto& frame=env.emu->frame();
            bool black=true;
            for(int y=0;y<240;y++) for(int x=0;x<256;x++)
                black &= frame.palette[frame.pixels[y*frame.pitch+x]]==0x0f;
            if(side&&black) {
                FILE* file=fopen(argv[2],"wx"); if(!file) throw std::runtime_error("cannot create replay");
                fprintf(file,"RETRO_PRACTICE_V1 %016llx %zu\n",retro_rom().fingerprint,inputs.size());
                for(int b:inputs) fprintf(file,"%d\n",b);
                bool error=ferror(file); if(fclose(file)) error=true;
                if(error) throw std::runtime_error("replay write failed");
                printf("Captured first return-pipe black frame: prefix=%d TIME=%d routine=%d screen=%d bus=%d x=%d\n",
                    env.tick,env.time,m[0xe],m[0x7a0],m[0x77f],env.x_pos);
                puf_close(&env); free_retro_policy(policy); free(weights); puf_ini_free(&ini);
                return 0;
            }
        }
        throw std::runtime_error("no return-pipe black screen reached");
    } catch(const std::exception& e) { fprintf(stderr,"capture: %s\n",e.what()); return 1; }
}
