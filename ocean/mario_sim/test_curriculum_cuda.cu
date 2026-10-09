#define SMB_HEADLESS
#include "mario_sim.cu"
#include "test_common.h"
int main() {
    try {
        const int count=5,steps=8192;float *obs,*actions,*rewards,*terminals;
        smb_cuda_check(cudaMalloc(&obs,(size_t)count*FPT_OBS*sizeof(float)));
        smb_cuda_check(cudaMalloc(&actions,count*sizeof(float)));smb_cuda_check(cudaMalloc(&rewards,count*sizeof(float)));smb_cuda_check(cudaMalloc(&terminals,count*sizeof(float)));
        Dict options={};dict_set(&options,"curriculum",1);dict_set(&options,"curriculum_window",2);dict_set(&options,"curriculum_confirmations",2);
        dict_set(&options,"curriculum_threshold",1);dict_set(&options,"time_power",1.7);
        Env* envs=puf_vec_create(count,&options,obs,actions,rewards,terminals);cudaStream_t stream;
        smb_cuda_check(cudaStreamCreateWithFlags(&stream,cudaStreamNonBlocking));puf_bind_stream(stream);puf_reset(envs);smb_cuda_check(cudaStreamSynchronize(stream));
        auto task=fpt_host->task;auto time=fpt_host->time;auto curriculum=fpt_host->curriculum;auto teacher=fpt_test_teachers()[0];
        std::vector<Env> cpu(count),gpu(count);std::vector<SmbLogic> states(count),actual(count);
        for(int i=0;i<count;i++) {
            cpu[i].num_agents=1;cpu[i].episode.rng=smb_seed(task.seed,i);
            fpt_check(!fpt_reset_state(&cpu[i],&states[i],&task,&time,fpt_host->bank->entries.data(),fpt_host->eligible.data(),fpt_host->eligible.size(),fpt_host->table.data(),&curriculum),"CPU initial curriculum reset fault");
            float initial[FPT_OBS];fpt_observe(&states[i],fpt_host->bank->worlds.data(),&cpu[i].observation_history,initial);
        }
        cudaGraph_t graph;cudaGraphExec_t executable;smb_cuda_check(cudaStreamBeginCapture(stream,cudaStreamCaptureModeThreadLocal));puf_step(envs);
        smb_cuda_check(cudaStreamEndCapture(stream,&graph));smb_cuda_check(cudaGraphInstantiate(&executable,graph,0,0,0));
        std::vector<float> input(count),expected_reward(count),expected_terminal(count),got_reward(count),got_terminal(count),observations(count*FPT_OBS),expected_obs(count*FPT_OBS);
        uint32_t rng=137;unsigned successes=0,resets=0;
        for(int frame=0;frame<steps;frame++) {
            for(int i=0;i<count;i++) {
                auto& e=cpu[i];input[i]=(float)(smb_random(&rng)%64);
                if(i<2) {
                    int remaining=fpt_host->table[e.episode.scene].best_frames,offset=425-remaining+e.episode.frames;
                    fpt_check(offset>=0&&offset<425,"teacher control outside successful suffix");int b=teacher.frames[offset].buttons;input[i]=(float)((b&3)|((b>>2)&60));
                }
                fpt_check(!smb_native_frame(&states[i],fpt_host->bank->worlds.data(),smb_action_buttons((int)input[i])),"CPU curriculum simulation fault");
                expected_reward[i]=fpg_time_after_frame(&states[i],&e.episode,&task,e.target,&time);expected_terminal[i]=e.episode.status!=SMB_EPISODE_ACTIVE;
                if(expected_terminal[i]) {
                    successes+=e.episode.status==SMB_EPISODE_SUCCESS;resets++;fpt_log_episode(&e,&task,&time,&curriculum,fpt_host->table.data());
                    fpt_check(!fpt_reset_state(&e,&states[i],&task,&time,fpt_host->bank->entries.data(),fpt_host->eligible.data(),fpt_host->eligible.size(),fpt_host->table.data(),&curriculum),"CPU curriculum auto-reset fault");
                }
                fpt_observe(&states[i],fpt_host->bank->worlds.data(),&e.observation_history,expected_obs.data()+i*FPT_OBS);
            }
            smb_cuda_check(cudaMemcpyAsync(actions,input.data(),count*sizeof(float),cudaMemcpyHostToDevice,stream));
            if(frame&1)smb_cuda_check(cudaGraphLaunch(executable,stream));else puf_step(envs);smb_cuda_check(cudaStreamSynchronize(stream));
            smb_cuda_check(cudaMemcpy(gpu.data(),envs,count*sizeof(Env),cudaMemcpyDeviceToHost));
            smb_cuda_check(cudaMemcpy(actual.data(),fpt_states,count*sizeof(SmbLogic),cudaMemcpyDeviceToHost));
            smb_cuda_check(cudaMemcpy(got_reward.data(),rewards,count*sizeof(float),cudaMemcpyDeviceToHost));smb_cuda_check(cudaMemcpy(got_terminal.data(),terminals,count*sizeof(float),cudaMemcpyDeviceToHost));
            smb_cuda_check(cudaMemcpy(observations.data(),obs,observations.size()*sizeof(float),cudaMemcpyDeviceToHost));
            for(int i=0;i<count;i++) {
                fpt_same_state(states[i],actual[i]);const auto& a=cpu[i];const auto& b=gpu[i];
                fpt_check(!memcmp(&a.curriculum,&b.curriculum,sizeof(a.curriculum)),"CPU/CUDA mastery state differs");
                fpt_check(a.episode.scene==b.episode.scene&&a.episode.rng==b.episode.rng&&a.episode.frames==b.episode.frames
                    &&a.episode.episodes==b.episode.episodes&&fpt_near(a.target,b.target),"CPU/CUDA curriculum reset differs");
                fpt_check(fpt_near(expected_reward[i],got_reward[i])&&expected_terminal[i]==got_terminal[i],"curriculum reward or terminal mismatch");
                for(size_t j=0;j<sizeof(Log)/sizeof(float);j++)fpt_check(fpt_near(((const float*)&a.log)[j],((const float*)&b.log)[j]),"curriculum log mismatch");
                for(int k=0;k<FPT_OBS;k++)fpt_check(observations[i*FPT_OBS+k]==expected_obs[i*FPT_OBS+k],"curriculum semantic observation mismatch");
            }
        }
        fpt_check(cpu[0].curriculum.level==FPT_CURRICULUM_LEVELS-1&&cpu[1].curriculum.level==FPT_CURRICULUM_LEVELS-1,"teacher controls did not traverse curriculum");
        const char* checkpoint="build/mario_sim/fpg/curriculum_test_gpu_checkpoint.bin";
        {std::ofstream out(checkpoint,std::ios::binary);out<<"GPU curriculum checkpoint";}
        fpt_gpu_save(checkpoint,nullptr);puf_reset(envs);Ini ini={};dict_set(puf_ini_section(&ini,"env",1),"curriculum_resume",1);fpt_gpu_load(checkpoint,&ini);
        smb_cuda_check(cudaMemcpy(gpu.data(),envs,count*sizeof(Env),cudaMemcpyDeviceToHost));
        for(int i=0;i<count;i++)fpt_check(gpu[i].curriculum.level==cpu[i].curriculum.level
            &&gpu[i].curriculum.attempts==cpu[i].curriculum.attempts&&gpu[i].curriculum.wins==cpu[i].curriculum.wins
            &&gpu[i].curriculum.streak==cpu[i].curriculum.streak,"GPU curriculum checkpoint resume failed");
        puf_ini_free(&ini);smb_cuda_check(cudaGraphExecDestroy(executable));smb_cuda_check(cudaGraphDestroy(graph));puf_close(envs);
        smb_cuda_check(cudaStreamDestroy(stream));cudaFree(obs);cudaFree(actions);cudaFree(rewards);cudaFree(terminals);dict_clear(&options);
        printf("{\"decisions\":%d,\"successes\":%u,\"resets\":%u,\"teacher_agents_at_final_level\":2,\"bitwise_native_states\":true,\"bitwise_curriculum\":true,\"bitwise_observations\":true,\"direct_and_graph\":true,\"checkpoint_resume\":true,\"failures\":0}\n",count*steps,successes,resets);return 0;
    }catch(const std::exception& e){fprintf(stderr,"curriculum CUDA test: %s\n",e.what());return 1;}
}
