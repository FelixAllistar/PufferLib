#define SMB_HEADLESS
#include "mario_sim.cu"
#include "test_common.h"

int main() {
    try {
        const int count=33,steps=512;unsigned decisions=0,successes=0,resets=0;auto teachers=fpt_test_teachers();
        fpt_check(teachers.size()<(size_t)count,"too many teacher controls");
        for(float bonus:{0.0f,0.5f}) {
            float *obs,*actions,*rewards,*terminals;
            smb_cuda_check(cudaMalloc(&obs,(size_t)count*FPT_OBS*sizeof(float)));
            smb_cuda_check(cudaMalloc(&actions,count*sizeof(float)));smb_cuda_check(cudaMalloc(&rewards,count*sizeof(float)));
            smb_cuda_check(cudaMalloc(&terminals,count*sizeof(float)));
            Dict options={};dict_set(&options,"time_bonus",bonus);dict_set(&options,"time_power",bonus?1.7:1.0);
            Env* envs=puf_vec_create(count,&options,obs,actions,rewards,terminals);
            cudaStream_t stream;smb_cuda_check(cudaStreamCreateWithFlags(&stream,cudaStreamNonBlocking));puf_bind_stream(stream);puf_reset(envs);
            smb_cuda_check(cudaStreamSynchronize(stream));
            std::vector<SmbLogic> cpu(count),gpu(count);std::vector<Env> reference(count),shells(count);
            std::vector<uint32_t> worlds(count);auto task=fpt_host->task;auto time=fpt_host->time;
            for(int i=0;i<count;i++) {
                auto& e=reference[i];e.num_agents=1;e.episode.rng=smb_seed(task.seed,(unsigned)i);
                fpt_check(!fpt_reset_state(&e,&cpu[i],&task,&time,fpt_host->bank->entries.data(),fpt_host->eligible.data(),(int)fpt_host->eligible.size(),fpt_host->table.data()),"CPU generated reset fault");
                if((size_t)i<teachers.size()) {
                    auto& t=teachers[i];fpt_check(!smb_scene_reset(&cpu[i],&t.item.scene),"teacher reset fault");e.episode.frames=0;e.episode.status=0;
                    e.episode.world=0;e.episode.scene=t.item.seed;e.table_hit=1;e.target=fpg_time_target(&cpu[i],t.item.seed,fpt_host->table.data(),&time);
                }
                worlds[i]=e.episode.world;
            }
            smb_cuda_check(cudaMemcpyAsync(envs,reference.data(),count*sizeof(Env),cudaMemcpyHostToDevice,stream));
            smb_cuda_check(cudaMemcpyAsync(fpt_states,cpu.data(),count*sizeof(SmbLogic),cudaMemcpyHostToDevice,stream));
            smb_cuda_check(cudaMemcpyAsync(fpt_world_ids,worlds.data(),count*sizeof(uint32_t),cudaMemcpyHostToDevice,stream));
            cudaGraph_t graph;cudaGraphExec_t executable;
            smb_cuda_check(cudaStreamBeginCapture(stream,cudaStreamCaptureModeThreadLocal));puf_step(envs);
            smb_cuda_check(cudaStreamEndCapture(stream,&graph));smb_cuda_check(cudaGraphInstantiate(&executable,graph,0,0,0));
            uint32_t rng=91;std::vector<float> input(count),er(count),et(count),ar(count),at(count),actual_obs(count*FPT_OBS),expected_obs(count*FPT_OBS);
            for(int t=0;t<steps;t++) {
                for(int i=0;i<count;i++) {
                    if(!(t%8))input[i]=(float)(smb_random(&rng)%64);
                    if((size_t)i<teachers.size()&&(size_t)t<teachers[i].frames.size()) {
                        int buttons=teachers[i].frames[t].buttons;input[i]=(float)((buttons&3)|((buttons>>2)&60));
                    }
                    auto& e=reference[i];auto* data=fpt_host->bank->worlds.data()+(size_t)e.episode.world*SMB_PRG;
                    fpt_check(!smb_native_frame(&cpu[i],data,smb_action_buttons((int)input[i])),"CPU reference simulation fault");
                    er[i]=fpg_time_after_frame(&cpu[i],&e.episode,&task,e.target,&time);et[i]=(float)(e.episode.status!=0);decisions++;
                    if(et[i]) {
                        resets++;successes+=e.episode.status==SMB_EPISODE_SUCCESS;fpt_log_episode(&e,&task,&time);
                        fpt_check(!fpt_reset_state(&e,&cpu[i],&task,&time,fpt_host->bank->entries.data(),fpt_host->eligible.data(),(int)fpt_host->eligible.size(),fpt_host->table.data()),"CPU auto-reset fault");
                    }
                    fpt_observe(&cpu[i],data,&e.observation_history,expected_obs.data()+i*FPT_OBS);
                }
                smb_cuda_check(cudaMemcpyAsync(actions,input.data(),count*sizeof(float),cudaMemcpyHostToDevice,stream));
                if(t&1)smb_cuda_check(cudaGraphLaunch(executable,stream));else puf_step(envs);
                smb_cuda_check(cudaStreamSynchronize(stream));
                smb_cuda_check(cudaMemcpy(gpu.data(),fpt_states,count*sizeof(SmbLogic),cudaMemcpyDeviceToHost));
                smb_cuda_check(cudaMemcpy(shells.data(),envs,count*sizeof(Env),cudaMemcpyDeviceToHost));
                smb_cuda_check(cudaMemcpy(ar.data(),rewards,count*sizeof(float),cudaMemcpyDeviceToHost));
                smb_cuda_check(cudaMemcpy(at.data(),terminals,count*sizeof(float),cudaMemcpyDeviceToHost));
                smb_cuda_check(cudaMemcpy(actual_obs.data(),obs,actual_obs.size()*sizeof(float),cudaMemcpyDeviceToHost));
                for(int i=0;i<count;i++) {
                    fpt_same_state(cpu[i],gpu[i]);auto& a=reference[i];auto& b=shells[i];
                    fpt_check(a.episode.rng==b.episode.rng&&a.episode.episodes==b.episode.episodes&&a.episode.scene==b.episode.scene
                        &&a.episode.world==b.episode.world&&a.episode.frames==b.episode.frames&&a.episode.status==b.episode.status
                        &&fpt_near(a.episode.episode_return,b.episode.episode_return)&&a.table_hit==b.table_hit&&fpt_near(a.target,b.target),"GPU episode/target mismatch");
                    fpt_check(fpt_near(er[i],ar[i])&&et[i]==at[i],"GPU reward or terminal mismatch");
                    for(size_t j=0;j<sizeof(Log)/sizeof(float);j++)fpt_check(fpt_near(((float*)&a.log)[j],((float*)&b.log)[j]),"GPU episode log mismatch");
                    for(int k=0;k<FPT_OBS;k++)fpt_check(actual_obs[i*FPT_OBS+k]==expected_obs[i*FPT_OBS+k],"GPU semantic observation mismatch");
                }
            }
            smb_cuda_check(cudaGraphExecDestroy(executable));smb_cuda_check(cudaGraphDestroy(graph));puf_close(envs);
            smb_cuda_check(cudaStreamDestroy(stream));cudaFree(obs);cudaFree(actions);cudaFree(rewards);cudaFree(terminals);dict_clear(&options);
        }
        fpt_check(successes>=teachers.size()*2&&resets>successes,"missing terminal controls");
        printf("{\"decisions\":%u,\"successes\":%u,\"resets\":%u,\"reward_tolerance\":0.00001,\"bitwise_native_states\":true,\"bitwise_observations\":true,\"direct_and_graph\":true,\"failures\":0}\n",decisions,successes,resets);
        return 0;
    }catch(const std::exception& e){fprintf(stderr,"FPG timing CUDA test: %s\n",e.what());return 1;}
}
