// Optional learned-policy integration check. Same actions/ROM/observations
// under old and new rewards; audit clear time independently of episode length.
#define PUFFERLIB_BUILD_MAIN
#include "retro.h"
#undef PUFFERLIB_BUILD_MAIN
#include "retro_policy_cpu.h"
#include "nes_emu/abstract_file.h"
#include <filesystem>

static void require(bool ok,const char* message) {
    if(!ok) throw std::runtime_error(message);
}
static std::vector<char> saved(Nes_Emu& emu) {
    Nes_State state; emu.save_state(&state); Mem_Writer writer;
    retro_check(state.write(writer));
    return std::vector<char>(writer.data(),writer.data()+writer.size());
}
int main(int argc,char** argv) {
    try {
        require(argc==3||(argc==4&&!strcmp(argv[3],"--finish-fixture")),
            "usage: test_speed_policy CHECKPOINT CONFIG [--finish-fixture]");
        // Engine/reward regression only: PAL-trained policies may fall into
        // the post-pipe gap on NTSC. An explicit scripted continuation lets
        // us exercise a real finish without claiming that the policy cleared.
        bool finish_fixture=argc==4; int fixture_frames=0;
        Ini ini={}; puf_ini_load_env(&ini,"retro",0,nullptr); puf_ini_load_file(&ini,argv[2]);
        int hidden=puf_ini_get_int(&ini,"policy","hidden_size"),layers=puf_ini_get_int(&ini,"policy","num_layers");
        require(std::filesystem::file_size(argv[1])==retro_policy_weights(hidden,layers)*sizeof(float),"checkpoint shape mismatch");
        Weights* weights=load_weights(argv[1]); require(weights,"checkpoint load failed");
        RetroPolicy* policy=make_retro_policy(weights,hidden,layers);
        Env old={},next={}; old.rng=next.rng=73;
        puf_init(&old,puf_ini_section(&ini,"env",0)); puf_init(&next,puf_ini_section(&ini,"env",0));
        old.terminate_on_clear=next.terminate_on_clear=false;
        Env terminal={}; terminal.rng=73; puf_init(&terminal,puf_ini_section(&ini,"env",0));
        terminal.terminate_on_clear=true;
        terminal.death_penalty=terminal.idle_penalty=0;
        terminal.display=new RetroDisplay{};
        std::vector<float> terminal_obs(OBS_SIZE); float terminal_reward=0,terminal_done=0;
        old.completion_time_bonus=0; // Absent in pre-change checkpoint metadata.
        next.score_scale=0; next.checkpoint_reward=0.125f; next.completion_reward=10;
        next.completion_time_bonus=5; next.death_penalty=1; next.reward_scale=0.0625f;
        next.coin_reward=0; next.idle_penalty=0;
        std::vector<float> a(OBS_SIZE),b(OBS_SIZE); float action=0,ra=0,rb=0,da=0,db=0;
        terminal.agents[0]={terminal_obs.data(),&action,&terminal_reward,&terminal_done,nullptr,0};
        puf_reset(&terminal);
        old.agents[0]={a.data(),&action,&ra,&da,nullptr,0};
        next.agents[0]={b.data(),&action,&rb,&db,nullptr,0}; puf_reset(&old); puf_reset(&next);
        // Independent full-frame playback, without compiled CPU blocks,
        // idle skipping, or the environment's frame/terminal scheduler.
        Nes_Emu reference;
        retro_check(reference.set_cart(&retro_rom().cart));
        std::vector<unsigned char> pixels(Nes_Emu::buffer_width*256);
        reference.set_pixels(pixels.data()+8*Nes_Emu::buffer_width,Nes_Emu::buffer_width);
        reference.set_idle_skip(false);
        reference.load_state(next.start->state);
        unsigned long core_start=reference.video_frame_count();
        int observed_clears=0,elapsed=0,clear_frame_sum=0,observed_rta_frames=0;
        double old_return=0,new_return=0;
        while(!da) {
            require(!memcmp(a.data(),b.data(),OBS_SIZE*sizeof(float)),"reward change altered policy input");
            retro_policy_act(policy,a.data(),&action,true);
            if(finish_fixture&&next.pipe_phase==3&&next.world==1&&next.stage==1) {
                int phase=fixture_frames++%48;
                action=retro_mask_action(RETRO_BTN_RIGHT|RETRO_BTN_B|
                    ((phase>=10&&phase<42)?RETRO_BTN_A:0));
            }
            if(!terminal_done) {
                require(!memcmp(a.data(),terminal_obs.data(),OBS_SIZE*sizeof(float)),"terminal env diverged before flag");
                puf_step(&terminal);
                require(terminal_reward>=0,"negative reward with death/idle penalties disabled");
                if(terminal_done) require(terminal.log.episode_return>=0,
                    "negative completed return with penalties disabled");
            }
            int prior_clears=next.episode_clears,prior_frames=next.episode_clear_frames;
            int prior_progress=next.progress_pixels;
            int prior_phase=next.pipe_phase;
            int prior_screen_timer=next.emu->low_mem()[0x7a0];
            puf_step(&old); puf_step(&next); elapsed+=next.last_frames;
            for(int f=0;f<next.last_frames;f++) retro_check(reference.emulate_frame(retro_action_mask((int)action)));
            require(reference.video_frame_count()-core_start==(unsigned long)elapsed,
                "independent NES video count disagrees with wrapper elapsed frames");
            if(!db) {
                require(next.emu->video_frame_count()==reference.video_frame_count(),"optimized core skipped NES frames");
                require(saved(*next.emu)==saved(reference),"optimized trajectory differs from unoptimized reference core");
            }
            require(da==db&&old.last_frames==next.last_frames,"reward change altered trajectory boundaries");
            old_return+=ra; new_return+=rb;
            if(!db&&next.rta.first_split_frames&&!observed_rta_frames) {
                observed_rta_frames=next.rta.first_split_frames;
                require(next.world==1&&next.stage==2&&observed_rta_frames<=elapsed,
                    "RTA split fired on wrong level or after current frame");
                if(next.frameskip==1) require(next.emu->low_mem()[0x7a0]>=6&&prior_screen_timer==0,
                    "RTA split does not match LiveSplit black-screen edge");
                printf("rta_split frames=%d seconds=%.6f\n",observed_rta_frames,retro_frame_seconds(observed_rta_frames));
            }
            if(!db&&((prior_phase==1&&next.pipe_phase==2)||(prior_phase==2&&next.pipe_phase==3))) {
                int segment_frames=next.pipe_phase==2?next.tick:next.tick-next.underground_start_tick;
                int checkpoints=next.progress_pixels/next.checkpoint_distance-prior_progress/next.checkpoint_distance;
                float base=retro_rom_reward(&next,0,false,0,
                    checkpoints,0,0,0);
                float extra=next.pipe_segment_bonus*retro_clear_speed(segment_frames,next.pipe_segment_frames)*next.reward_scale;
                require(fabs(rb-base-extra)<1e-6,"pipe segment reward missing or paid at wrong time");
                printf("segment=%d frames=%d extra=%g\n",next.pipe_phase-1,segment_frames,extra);
            }
            int after_clears=db?(int)next.log.clears:next.episode_clears;
            int after_frames=db?(int)next.log.clear_frame_sum:next.episode_clear_frames;
            if(after_clears>prior_clears) {
                int clear_frames=after_frames-prior_frames;
                observed_clears++; clear_frame_sum+=clear_frames;
                require(!db,"test policy must clear before the timeout to test post-clear accounting");
                require(next.completion_on_rta_split ? observed_rta_frames==clear_frames :
                    (next.completion_on_next_playable
                        ? (next.world==1&&next.stage==2&&retro_playable_area_state(next.emu->low_mem()))
                        : retro_flag_contact(next.emu->low_mem())),"clear reward has the wrong timing endpoint");
                if(next.completion_on_next_playable||next.completion_on_rta_split) {
                    require(next.pipe_phase==3,"both pipe segments were not confirmed");
                    require(observed_rta_frames>0&&observed_rta_frames<=clear_frames
                        &&terminal.log.rta_count==1&&terminal.log.rta_frame_sum==observed_rta_frames,
                        "RTA completion was lost or conflated with next-playable finish");
                    require(terminal.display->rta_counts[0]==1
                        &&terminal.display->rta_frame_sums[0]==(unsigned)observed_rta_frames,
                        "viewer completed mean lost during autoreset");
                }
                require(terminal_done&&terminal.log.clears==1&&terminal.log.clear_frame_sum==clear_frames
                    &&terminal.log.episode_length==clear_frames&&clear_frames<=elapsed
                    &&elapsed-clear_frames<next.frameskip,
                    "terminal clear did not stop on exact configured finish frame");
                if(next.completion_on_rta_split) {
                    require(terminal.log.clear_frame_sum==terminal.log.rta_frame_sum
                        &&terminal.display->last_clear_frames==terminal.display->last_rta_frames,
                        "reward, dashboard and viewer disagree on RTA finish");
                }
                require(terminal.display->last_clear_frames==clear_frames
                    &&terminal.display->last_clear_time==next.time,
                    "viewer loses exact last-clear result across automatic reset");
                int checkpoints=next.progress_pixels/next.checkpoint_distance-prior_progress/next.checkpoint_distance;
                float expected=retro_rom_reward(&next,1,false,0,checkpoints,
                    retro_clear_speed(clear_frames,next.max_frames,next.completion_time_min_frames,next.completion_time_max_frames));
                require(fabs(expected-rb)<1e-6,"clear transition has wrong speed bonus");
                printf("clear=%d frames=%d episode_tick=%d old_reward=%.7g new_reward=%.7g\n",
                    observed_clears,clear_frames,elapsed,ra,rb);
            }
            require(elapsed<=old.max_frames,"episode overran frame limit");
        }
        require(observed_clears==1,"flag animation/next-level load paid a duplicate clear");
        require(next.log.clear_frame_sum==clear_frame_sum,"clear time includes post-clear waiting");
        require(next.log.episode_length==elapsed,"episode length does not count all frames");
        require(next.level_start_tick==0&&next.episode_clear_frames==0,"reset did not clear time accounting");
        require(next.log.clears==observed_clears,"duplicate clear reward");
        if(next.completion_on_next_playable||next.completion_on_rta_split) require(next.log.rta_count==1&&next.log.rta_frame_sum==observed_rta_frames,
            "failed continuation diluted or duplicated a completed RTA split");
        printf("PASS: every frame matched unoptimized reference CPU/PPU/APU state and NES video counter\n");
        printf("PASS: %s; same trajectory; %d clears, clear_frames=%g, episode_frames=%d, return old=%g new=%g, deaths=%g\n",
            finish_fixture?"scripted finish fixture (NOT a policy clear)":"learned policy",
            observed_clears,next.log.clear_frame_sum/next.log.clears,elapsed,old_return,new_return,next.log.deaths);
        puf_close(&old); puf_close(&next); puf_close(&terminal); free_retro_policy(policy); free(weights); puf_ini_free(&ini);
        return 0;
    } catch(const std::exception& e) { fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
