// Optional learned-policy integration check. Same actions/ROM/observations
// under old and new rewards; audit clear time independently of episode length.
#define PUFFERLIB_BUILD_MAIN
#include "retro.h"
#undef PUFFERLIB_BUILD_MAIN
#include "retro_policy_cpu.h"
#include <filesystem>

static void require(bool ok,const char* message) {
    if(!ok) throw std::runtime_error(message);
}
int main(int argc,char** argv) {
    try {
        require(argc==3,"usage: test_speed_policy CHECKPOINT CONFIG");
        Ini ini={}; puf_ini_load_env(&ini,"retro",0,nullptr); puf_ini_load_file(&ini,argv[2]);
        int hidden=puf_ini_get_int(&ini,"policy","hidden_size"),layers=puf_ini_get_int(&ini,"policy","num_layers");
        require(std::filesystem::file_size(argv[1])==retro_policy_weights(hidden,layers)*sizeof(float),"checkpoint shape mismatch");
        Weights* weights=load_weights(argv[1]); require(weights,"checkpoint load failed");
        RetroPolicy* policy=make_retro_policy(weights,hidden,layers);
        Env old={},next={}; old.rng=next.rng=73;
        puf_init(&old,puf_ini_section(&ini,"env",0)); puf_init(&next,puf_ini_section(&ini,"env",0));
        old.completion_time_bonus=0; // Absent in pre-change checkpoint metadata.
        next.score_scale=0; next.checkpoint_reward=0.125f; next.completion_reward=10;
        next.completion_time_bonus=5; next.death_penalty=1; next.reward_scale=0.0625f;
        std::vector<float> a(OBS_SIZE),b(OBS_SIZE); float action=0,ra=0,rb=0,da=0,db=0;
        old.agents[0]={a.data(),&action,&ra,&da,nullptr,0};
        next.agents[0]={b.data(),&action,&rb,&db,nullptr,0}; puf_reset(&old); puf_reset(&next);
        int observed_clears=0,elapsed=0,clear_frame_sum=0;
        double old_return=0,new_return=0;
        while(!da) {
            require(!memcmp(a.data(),b.data(),OBS_SIZE*sizeof(float)),"reward change altered policy input");
            retro_policy_act(policy,a.data(),&action,true);
            int prior_clears=next.episode_clears,prior_frames=next.episode_clear_frames;
            float old_p=retro_potential(next.x_pos); int prior_progress=next.progress_pixels;
            puf_step(&old); puf_step(&next); elapsed+=next.last_frames;
            require(da==db&&old.last_frames==next.last_frames,"reward change altered trajectory boundaries");
            old_return+=ra; new_return+=rb;
            int after_clears=db?(int)next.log.clears:next.episode_clears;
            int after_frames=db?(int)next.log.clear_frame_sum:next.episode_clear_frames;
            if(after_clears>prior_clears) {
                int clear_frames=after_frames-prior_frames;
                observed_clears++; clear_frame_sum+=clear_frames;
                require(!db,"test policy must clear before the timeout to test post-clear accounting");
                int checkpoints=next.progress_pixels/next.checkpoint_distance-prior_progress/next.checkpoint_distance;
                float expected=retro_rom_reward(&next,old_p,retro_potential(next.x_pos),1,false,false,0,checkpoints,
                    retro_clear_speed(clear_frames,next.max_frames));
                require(fabs(expected-rb)<1e-6,"clear transition has wrong speed bonus");
                printf("clear=%d frames=%d episode_tick=%d old_reward=%.7g new_reward=%.7g\n",
                    observed_clears,clear_frames,elapsed,ra,rb);
            }
            require(elapsed<=old.max_frames,"episode overran frame limit");
        }
        require(observed_clears>0,"policy failed to clear; integration check needs a learned clear-capable checkpoint");
        require(next.log.clear_frame_sum==clear_frame_sum,"clear time includes post-clear waiting");
        require(next.log.episode_length==elapsed,"episode length does not count all frames");
        require(next.level_start_tick==0&&next.episode_clear_frames==0,"reset did not clear time accounting");
        require(next.log.clears==observed_clears,"duplicate clear reward");
        printf("PASS: same trajectory; %d clears, clear_frames=%g, episode_frames=%d, return old=%g new=%g, deaths=%g\n",
            observed_clears,next.log.clear_frame_sum/next.log.clears,elapsed,old_return,new_return,next.log.deaths);
        puf_close(&old); puf_close(&next); free_retro_policy(policy); free(weights); puf_ini_free(&ini);
        return 0;
    } catch(const std::exception& e) { fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
