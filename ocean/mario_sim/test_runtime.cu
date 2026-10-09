#define SMB_HEADLESS
#include "mario_sim.cu"
#include "test_common.h"

// Real reset banks, both execution paths, auto-resets, semantic history and
// mixed checkpoint persistence. The reference runs the translated CPU engine.
int main(int argc,char** argv) {
    try {
        const int count=128,steps=384;unsigned decisions=0,resets=0;
        for(const char* mode:{"game","fpg","mixed"}) {
            Ini ini={};puf_ini_load_file(&ini,"config/mario_sim.ini");
            if(argc>1)dict_set_str(puf_ini_section(&ini,"env",0),"engine_module",argv[1]);
            if(argc>2)dict_set_str(puf_ini_section(&ini,"game",0),"reset_bank",(std::string(argv[2])+"/bank.bin").c_str());
            dict_set_str(puf_ini_section(&ini,"env",0),"mode",mode);
            dict_set(puf_ini_section(&ini,"vec",0),"total_agents",count);
            dict_set(puf_ini_section(&ini,"game",0),"max_frames",97);
            dict_set(puf_ini_section(&ini,"fpg",0),"max_frames",41);
            if(!strcmp(mode,"game")) {
                // Full games must not depend on any FPG assets.
                dict_set_str(puf_ini_section(&ini,"fpg",0),"reset_bank","missing-fpg-bank");
                dict_set_str(puf_ini_section(&ini,"fpg",0),"time_table","missing-fpg-table");
            }
            fpt_configure(&ini,"train",nullptr);
            float *obs,*actions,*rewards,*terminals;
            smb_cuda_check(cudaMalloc(&obs,(size_t)count*FPT_OBS*sizeof(float)));
            smb_cuda_check(cudaMalloc(&actions,count*sizeof(float)));smb_cuda_check(cudaMalloc(&rewards,count*sizeof(float)));
            smb_cuda_check(cudaMalloc(&terminals,count*sizeof(float)));
            Env* envs=puf_vec_create(count,puf_ini_section(&ini,"env",0),obs,actions,rewards,terminals);
            cudaStream_t stream;smb_cuda_check(cudaStreamCreateWithFlags(&stream,cudaStreamNonBlocking));puf_bind_stream(stream);puf_reset(envs);
            smb_cuda_check(cudaStreamSynchronize(stream));auto& h=*fpt_host;
            std::vector<Env> cpu(count),gpu(count);std::vector<SmbLogic> states(count),actual(count);
            std::vector<float> input(count),expected_reward(count),expected_terminal(count),got_reward(count),got_terminal(count),observations(count*FPT_OBS),expected_obs(count*FPT_OBS);
            uint32_t seen_stages=0;int fpg_actors=0;
            for(int i=0;i<count;i++) {
                auto& e=cpu[i];e.num_agents=1;e.is_fpg=smb_fpg_actor(i,count,&h.game);fpg_actors+=e.is_fpg;
                e.episode.rng=smb_seed(h.task.seed,i);
                fpt_check(!fpt_reset_state(&e,&states[i],&h.task,&h.time,h.runtime_bank.data(),h.eligible.data(),h.eligible.size(),h.table.data(),&h.curriculum,&h.game),"CPU reset fault");
                if(!e.is_fpg) {
                    // Cover every original water, castle, underground and ground
                    // stage on both backends, regardless of the random sample.
                    auto game=h.game;game.fixed_stage=i%32;
                    fpt_check(!smb_game_reset(&states[i],&e.episode,&e.game,&game,h.runtime_bank.data()),"stage reset fault");
                    seen_stages|=1u<<e.game.stage;
                }
                fpt_observe(&states[i],h.bank->worlds.data(),&e.observation_history,expected_obs.data()+i*FPT_OBS);
            }
            fpt_check(fpg_actors==(!strcmp(mode,"game")?0:!strcmp(mode,"fpg")?128:32),"incorrect frame allocation");
            if(strcmp(mode,"fpg"))fpt_check(seen_stages==0xffffffffu,"did not cover 32 stages");
            smb_cuda_check(cudaMemcpy(envs,cpu.data(),count*sizeof(Env),cudaMemcpyHostToDevice));
            smb_cuda_check(cudaMemcpy(fpt_states,states.data(),count*sizeof(SmbLogic),cudaMemcpyHostToDevice));
            cudaGraph_t graph;cudaGraphExec_t executable;
            smb_cuda_check(cudaStreamBeginCapture(stream,cudaStreamCaptureModeThreadLocal));puf_step(envs);
            smb_cuda_check(cudaStreamEndCapture(stream,&graph));smb_cuda_check(cudaGraphInstantiate(&executable,graph,0,0,0));
            uint32_t rng=137;
            for(int frame=0;frame<steps;frame++) {
                for(int i=0;i<count;i++) {
                    auto& e=cpu[i];input[i]=(float)(smb_random(&rng)%64);
                    if(!e.is_fpg&&i%4==0)input[i]=34; // Right+B: exercise positive progress rewards.
                    fpt_check(!smb_native_frame(&states[i],h.bank->worlds.data(),smb_action_buttons((int)input[i])),"CPU frame fault");
                    expected_reward[i]=smb_after_frame(&e,&states[i],&h.task,&h.time,&h.curriculum,h.table.data(),&h.game);
                    expected_terminal[i]=e.episode.status!=SMB_EPISODE_ACTIVE;
                    if(expected_terminal[i]) {
                        resets++;fpt_check(!fpt_reset_state(&e,&states[i],&h.task,&h.time,h.runtime_bank.data(),h.eligible.data(),h.eligible.size(),h.table.data(),&h.curriculum,&h.game),"CPU auto-reset fault");
                    }
                    fpt_observe(&states[i],h.bank->worlds.data(),&e.observation_history,expected_obs.data()+i*FPT_OBS);
                }
                smb_cuda_check(cudaMemcpyAsync(actions,input.data(),count*sizeof(float),cudaMemcpyHostToDevice,stream));
                if(frame&1)smb_cuda_check(cudaGraphLaunch(executable,stream));else puf_step(envs);
                smb_cuda_check(cudaStreamSynchronize(stream));
                smb_cuda_check(cudaMemcpy(gpu.data(),envs,count*sizeof(Env),cudaMemcpyDeviceToHost));
                smb_cuda_check(cudaMemcpy(actual.data(),fpt_states,count*sizeof(SmbLogic),cudaMemcpyDeviceToHost));
                smb_cuda_check(cudaMemcpy(got_reward.data(),rewards,count*sizeof(float),cudaMemcpyDeviceToHost));
                smb_cuda_check(cudaMemcpy(got_terminal.data(),terminals,count*sizeof(float),cudaMemcpyDeviceToHost));
                smb_cuda_check(cudaMemcpy(observations.data(),obs,observations.size()*sizeof(float),cudaMemcpyDeviceToHost));
                for(int i=0;i<count;i++) {
                    fpt_same_state(states[i],actual[i]);const auto& a=cpu[i];const auto& b=gpu[i];
                    fpt_check(a.is_fpg==b.is_fpg&&!memcmp(&a.curriculum,&b.curriculum,sizeof(a.curriculum))&&!memcmp(&a.game,&b.game,sizeof(a.game)),"mode/controller state mismatch");
                    fpt_check(!memcmp(&a.episode,&b.episode,sizeof(a.episode)),"episode state mismatch");
                    fpt_check(fpt_near(expected_reward[i],got_reward[i])&&expected_terminal[i]==got_terminal[i],"reward/terminal mismatch");
                    for(size_t j=0;j<sizeof(Log)/sizeof(float);j++)fpt_check(fpt_near(((const float*)&a.log)[j],((const float*)&b.log)[j]),"log mismatch");
                    for(int k=0;k<FPT_OBS;k++)fpt_check(std::isfinite(observations[i*FPT_OBS+k])&&observations[i*FPT_OBS+k]==expected_obs[i*FPT_OBS+k],"semantic observation mismatch");
                    decisions++;
                }
            }
            if(strcmp(mode,"fpg")) {
                float checkpoints=0;for(const auto& e:cpu)checkpoints+=e.log.game_checkpoints;
                fpt_check(checkpoints>0,"game actors did not exercise checkpoint rewards");
            }
            if(!strcmp(mode,"mixed")) {
                const char* path="build/mario_sim/mixed_test_checkpoint.bin";
                {std::ofstream out(path);out<<"mixed test";}
                fpt_gpu_save(path,nullptr);auto saved=fpt_read_progress(path);
                fpt_check(saved.progress.size()==32,"checkpoint included full-game actors");
                fpt_gpu_load(path,&ini);smb_cuda_check(cudaMemcpy(gpu.data(),envs,count*sizeof(Env),cudaMemcpyDeviceToHost));
                for(int i=0;i<96;i++)fpt_check(!memcmp(&gpu[i].episode,&cpu[i].episode,sizeof(SmbEpisode))&&!memcmp(&gpu[i].observation_history,&cpu[i].observation_history,sizeof(FptObservationHistory)),"curriculum restore changed a game actor");
                for(int i=96;i<count;i++)fpt_check(gpu[i].curriculum.level==cpu[i].curriculum.level&&gpu[i].curriculum.attempts==cpu[i].curriculum.attempts,"mixed progress did not restore");
                // The previous FPG-only checkpoint remains a valid warm start.
                fpt_test_old_progress("build/mario_sim/fpg_only_test_runtime.bin",fpt_host->bank->header.payload_hash,fpt_host->curriculum);
        fpt_gpu_load("build/mario_sim/fpg_only_test_runtime.bin",&ini);
                smb_cuda_check(cudaMemcpy(gpu.data(),envs,count*sizeof(Env),cudaMemcpyDeviceToHost));
                for(int i=96;i<count;i++)fpt_check(h.curriculum.depths[gpu[i].curriculum.level]==96,"legacy semantic frontier did not restore");
            }
            fprintf(stderr,"mode=%s actors=%d FPG=%d parity=passed\n",mode,count,fpg_actors);
            smb_cuda_check(cudaGraphExecDestroy(executable));smb_cuda_check(cudaGraphDestroy(graph));puf_close(envs);
            cudaStreamDestroy(stream);cudaFree(obs);cudaFree(actions);cudaFree(rewards);cudaFree(terminals);puf_ini_free(&ini);
        }
        printf("{\"decisions\":%u,\"resets\":%u,\"stages\":32,\"modes\":3,\"failures\":0}\n",decisions,resets);return 0;
    }catch(const std::exception& e){fprintf(stderr,"unified CUDA test: %s\n",e.what());return 1;}
}
