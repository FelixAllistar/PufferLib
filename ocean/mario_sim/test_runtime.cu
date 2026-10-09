#define SMB_HEADLESS
#include "mario_sim.cu"
#include "trace.h"
#include <cstdio>
#include <vector>

static void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
static void same_state(const SmbLogic& a,const SmbLogic& b,int env,int frame) {
    // Structure padding is outside the state contract.
    if(memcmp(a.ram,b.ram,SMB_RAM)||a.pc!=b.pc||a.a!=b.a||a.x!=b.x||a.y!=b.y||a.p!=b.p||a.sp!=b.sp
        ||memcmp(a.joy,b.joy,2)||memcmp(a.joy_shift,b.joy_shift,2)||a.strobe!=b.strobe
        ||a.instructions!=b.instructions||a.fault!=b.fault
        ||memcmp(&a.timing,&b.timing,offsetof(SmbClock,sprites)+256)) {
        fprintf(stderr,"runtime mismatch env=%d frame=%d pc=%04x/%04x instructions=%u/%u\n",
                env,frame,a.pc,b.pc,a.instructions,b.instructions);
        for(int k=0;k<SMB_RAM;k++)if(a.ram[k]!=b.ram[k]){fprintf(stderr,"ram[%03x]=%02x/%02x\n",k,a.ram[k],b.ram[k]);break;}
        throw std::runtime_error("CPU/CUDA full native state mismatch");
    }
}
int main(int argc,char** argv) {
    try {
        if(argc>3)throw std::runtime_error("usage: test_runtime [MODULE] [BANK_DIRECTORY]");
        std::string bank_root=argc>2?argv[2]:"build/mario_sim/runtime/generated";
        const int count=33,steps=256;unsigned decisions=0,resets=0,successes=0;
        for(int task=SMB_TASK_FREE;task<=SMB_TASK_CLEAR;task++) {
            float *obs,*actions,*rewards,*terminals;
            smb_cuda_check(cudaMalloc(&obs,(size_t)count*SMB_DEBUG_OBS*sizeof(float)));
            smb_cuda_check(cudaMalloc(&actions,count*sizeof(float)));smb_cuda_check(cudaMalloc(&rewards,count*sizeof(float)));
            smb_cuda_check(cudaMalloc(&terminals,count*sizeof(float)));
            Dict options={};dict_set(&options,"task",task);dict_set(&options,"generation_knobs",SMB_GEN_ALL);
            if(argc>1)dict_set_str(&options,"engine_module",argv[1]);
            dict_set_str(&options,"reset_bank",(bank_root+"/bank.bin").c_str());
            dict_set(&options,"max_frames",task==SMB_TASK_FPG?240:37);
            Env* envs=puf_vec_create(count,&options,obs,actions,rewards,terminals);
            cudaStream_t stream;smb_cuda_check(cudaStreamCreateWithFlags(&stream,cudaStreamNonBlocking));puf_bind_stream(stream);puf_reset(envs);
            smb_cuda_check(cudaStreamSynchronize(stream));
            std::vector<SmbLogic> cpu(count),gpu(count);std::vector<SmbEpisode> episodes(count);
            std::vector<Log> logs(count);std::vector<Env> shells(count);
            auto cfg=smb_host->cfg;
            for(int i=0;i<count;i++) {
                episodes[i].rng=smb_seed(cfg.seed,(unsigned)i);
                require(!smb_task_reset(&cpu[i],&episodes[i],&cfg,smb_host->bank->entries.data(),smb_host->eligible.data(),(int)smb_host->eligible.size()),"CPU reset failed");
            }
            smb_cuda_check(cudaMemcpy(gpu.data(),smb_states,count*sizeof(SmbLogic),cudaMemcpyDeviceToHost));
            for(int i=0;i<count;i++)same_state(cpu[i],gpu[i],i,-1);
            std::vector<SmbTraceFrame> teacher;
            if(task==SMB_TASK_FPG) {
                std::ifstream in(bank_root+"/teacher/clips.bin",std::ios::binary);
                SmbTraceHeader h;SmbTraceCase c;
                require((bool)in.read((char*)&h,sizeof(h))&&h.magic==SMB_TRACE_MAGIC,"missing qualified teacher");
                in.seekg(SMB_PRG,std::ios::cur);require((bool)in.read((char*)&c,sizeof(c)),"truncated teacher");
                require(c.frames>0&&c.frames<=240,"invalid teacher length");teacher.resize(c.frames);
                require((bool)in.read((char*)teacher.data(),teacher.size()*sizeof(SmbTraceFrame)),"truncated teacher frames");
                require(!smb_scene_reset(&cpu[0],&c.scene),"teacher reset failed");
                episodes[0].frames=0;episodes[0].status=0;episodes[0].world=0;episodes[0].scene=c.seed;
                smb_cuda_check(cudaMemcpyAsync(smb_states,cpu.data(),sizeof(SmbLogic),cudaMemcpyHostToDevice,stream));
                unsigned zero=0;smb_cuda_check(cudaMemcpyAsync(smb_world_ids,&zero,sizeof(zero),cudaMemcpyHostToDevice,stream));
                smb_cuda_check(cudaMemcpyAsync(&envs[0].episode,episodes.data(),sizeof(SmbEpisode),cudaMemcpyHostToDevice,stream));
            }
            cudaGraph_t graph;cudaGraphExec_t executable;
            smb_cuda_check(cudaStreamBeginCapture(stream,cudaStreamCaptureModeThreadLocal));puf_step(envs);
            smb_cuda_check(cudaStreamEndCapture(stream,&graph));smb_cuda_check(cudaGraphInstantiate(&executable,graph,0,0,0));
            uint32_t action_seed=0x882931;std::vector<float> input(count),expected_reward(count),expected_terminal(count),actual_reward(count),actual_terminal(count),actual_obs(count*SMB_DEBUG_OBS);
            for(int t=0;t<steps;t++) {
                for(int i=0;i<count;i++) {
                    if(t%8==0)input[i]=(float)(smb_random(&action_seed)%64);
                    if(i==0&&t<(int)teacher.size())input[i]=(float)((teacher[t].buttons&3)|((teacher[t].buttons>>2)&60));
                    const auto* data=smb_host->bank->worlds.data()+(size_t)episodes[i].world*SMB_PRG;
                    require(!smb_native_frame(&cpu[i],data,smb_action_buttons((int)input[i])),"CPU frame fault");
                    expected_reward[i]=smb_task_after_frame(&cpu[i],&episodes[i],&cfg);
                    expected_terminal[i]=episodes[i].status!=SMB_EPISODE_ACTIVE;decisions++;
                    if(expected_terminal[i]) {
                        resets++;successes+=episodes[i].status==SMB_EPISODE_SUCCESS;
                        smb_log_episode(&logs[i],&episodes[i],&cfg);
                        require(!smb_task_reset(&cpu[i],&episodes[i],&cfg,smb_host->bank->entries.data(),smb_host->eligible.data(),(int)smb_host->eligible.size()),"CPU auto-reset fault");
                    }
                }
                smb_cuda_check(cudaMemcpyAsync(actions,input.data(),count*sizeof(float),cudaMemcpyHostToDevice,stream));
                if(t&1)smb_cuda_check(cudaGraphLaunch(executable,stream));else puf_step(envs);
                smb_cuda_check(cudaStreamSynchronize(stream));
                smb_cuda_check(cudaMemcpy(gpu.data(),smb_states,count*sizeof(SmbLogic),cudaMemcpyDeviceToHost));
                smb_cuda_check(cudaMemcpy(shells.data(),envs,count*sizeof(Env),cudaMemcpyDeviceToHost));
                smb_cuda_check(cudaMemcpy(actual_reward.data(),rewards,count*sizeof(float),cudaMemcpyDeviceToHost));
                smb_cuda_check(cudaMemcpy(actual_terminal.data(),terminals,count*sizeof(float),cudaMemcpyDeviceToHost));
                smb_cuda_check(cudaMemcpy(actual_obs.data(),obs,actual_obs.size()*sizeof(float),cudaMemcpyDeviceToHost));
                for(int i=0;i<count;i++) {
                    same_state(cpu[i],gpu[i],i,t);
                    require(!memcmp(&episodes[i],&shells[i].episode,sizeof(SmbEpisode)),"episode/RNG metadata mismatch");
                    require(!memcmp(&logs[i],&shells[i].log,sizeof(Log)),"episode log mismatch");
                    require(expected_reward[i]==actual_reward[i]&&expected_terminal[i]==actual_terminal[i],"reward/terminal mismatch");
                    for(int k=0;k<SMB_DEBUG_OBS;k++) {
                        float expected=(float)cpu[i].ram[k]/256.0f;
                        require(!memcmp(&expected,&actual_obs[(size_t)i*SMB_DEBUG_OBS+k],sizeof(float)),"bitwise observation mismatch");
                    }
                }
            }
            smb_cuda_check(cudaGraphExecDestroy(executable));smb_cuda_check(cudaGraphDestroy(graph));puf_close(envs);
            smb_cuda_check(cudaStreamDestroy(stream));cudaFree(obs);cudaFree(actions);cudaFree(rewards);cudaFree(terminals);dict_clear(&options);
        }
        require(resets>100&&successes>0,"runtime test missed terminal/success controls");
        printf("{\"decisions\":%u,\"resets\":%u,\"successes\":%u,\"failures\":0,\"task_modes\":3,\"bitwise_observations\":true,\"direct_and_graph\":true}\n",decisions,resets,successes);
        return 0;
    }catch(const std::exception& e){fprintf(stderr,"runtime integration: %s\n",e.what());return 1;}
}
