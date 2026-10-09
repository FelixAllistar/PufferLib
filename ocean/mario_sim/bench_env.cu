#define SMB_HEADLESS
#include "mario_sim.cu"
#include <vector>

__global__ void smb_benchmark_actions(float* actions,uint32_t* rng,unsigned* ticks,int count) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;if(i>=count)return;
    if(!(ticks[i]++%8))actions[i]=(float)(smb_random(&rng[i])%64);
}
__global__ void smb_benchmark_clear_logs(Env* envs,int count) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;if(i<count)envs[i].log={};
}
int main(int argc,char** argv) {
    try {
        if(argc>6)throw std::runtime_error("usage: bench_env [AGENTS=4096] [TASK=1] [STEPS=256] [MODULE] [THREADS=32]");
        int count=argc>1?std::stoi(argv[1]):4096,task=argc>2?std::stoi(argv[2]):1,steps=argc>3?std::stoi(argv[3]):256;
        if(count<1||count>65536||task<0||task>2||steps<16||steps%16)throw std::runtime_error("invalid benchmark parameters");
        float *obs,*actions,*rewards,*terminals;uint32_t* rng;unsigned* ticks;
        smb_cuda_check(cudaMalloc(&obs,(size_t)count*FPT_OBS*sizeof(float)));
        smb_cuda_check(cudaMalloc(&actions,count*sizeof(float)));smb_cuda_check(cudaMalloc(&rewards,count*sizeof(float)));
        smb_cuda_check(cudaMalloc(&terminals,count*sizeof(float)));smb_cuda_check(cudaMalloc(&rng,count*sizeof(uint32_t)));
        smb_cuda_check(cudaMalloc(&ticks,count*sizeof(unsigned)));smb_cuda_check(cudaMemset(ticks,0,count*sizeof(unsigned)));
        std::vector<uint32_t> seeds(count);for(int i=0;i<count;i++)seeds[i]=smb_seed(91,(unsigned)i);
        smb_cuda_check(cudaMemcpy(rng,seeds.data(),count*sizeof(uint32_t),cudaMemcpyHostToDevice));
        Ini ini={};puf_ini_load_file(&ini,"config/mario_sim.ini");
        Dict* configured=puf_ini_section(&ini,"env",0);dict_set_str(configured,"mode",task==0?"game":task==1?"fpg":"mixed");
        fpt_configure(&ini,"train",nullptr);Dict options={};dict_copy(&options,configured);
        if(argc>4)dict_set_str(&options,"engine_module",argv[4]);
        Env* envs=puf_vec_create(count,&options,obs,actions,rewards,terminals);
        fpt_engine.threads=argc>5?std::stoi(argv[5]):32;
        if(fpt_engine.threads<32||fpt_engine.threads>256||fpt_engine.threads%32)throw std::runtime_error("invalid thread count");
        cudaStream_t stream;smb_cuda_check(cudaStreamCreateWithFlags(&stream,cudaStreamNonBlocking));puf_bind_stream(stream);puf_reset(envs);
        for(int i=0;i<16;i++) {
            smb_benchmark_actions<<<(count+127)/128,128,0,stream>>>(actions,rng,ticks,count);puf_step(envs);
        }
        smb_cuda_check(cudaStreamSynchronize(stream));puf_reset(envs);
        smb_benchmark_clear_logs<<<(count+127)/128,128,0,stream>>>(envs,count);
        cudaGraph_t graph;cudaGraphExec_t executable;
        smb_cuda_check(cudaStreamBeginCapture(stream,cudaStreamCaptureModeThreadLocal));
        for(int i=0;i<16;i++) {
            smb_benchmark_actions<<<(count+127)/128,128,0,stream>>>(actions,rng,ticks,count);puf_step(envs);
        }
        smb_cuda_check(cudaStreamEndCapture(stream,&graph));smb_cuda_check(cudaGraphInstantiate(&executable,graph,0,0,0));
        cudaEvent_t start,end;smb_cuda_check(cudaEventCreate(&start));smb_cuda_check(cudaEventCreate(&end));
        smb_cuda_check(cudaEventRecord(start,stream));
        for(int i=0;i<steps;i+=16)smb_cuda_check(cudaGraphLaunch(executable,stream));
        smb_cuda_check(cudaEventRecord(end,stream));smb_cuda_check(cudaEventSynchronize(end));
        float ms;smb_cuda_check(cudaEventElapsedTime(&ms,start,end));
        std::vector<Env> results(count);smb_cuda_check(cudaMemcpy(results.data(),envs,count*sizeof(Env),cudaMemcpyDeviceToHost));
        unsigned long long resets=0,successes=0;for(const auto& e:results){resets+=(unsigned)e.log.n;successes+=(unsigned)e.log.successes;}
        printf("{\"agents\":%d,\"task\":%d,\"frames\":%llu,\"seconds\":%.6f,\"frames_per_second\":%.1f,"
               "\"resets\":%llu,\"successes\":%llu,\"observations\":%d,\"templates\":%zu,\"worlds\":%d,"
               "\"generation_knobs\":%u,\"threads\":%d,\"resets_and_generation_included\":true,\"observations_included\":true,"
               "\"ppo_included\":false,\"action_source\":\"seeded uniform actions held for eight frames\"}\n",
               count,task,(unsigned long long)count*steps,ms/1000.0,(double)count*steps*1000/ms,resets,successes,
               FPT_OBS,fpt_host->runtime_bank.size(),fpt_host->task.world_count,fpt_host->task.knobs,fpt_engine.threads);
        smb_cuda_check(cudaGraphExecDestroy(executable));smb_cuda_check(cudaGraphDestroy(graph));puf_close(envs);
        cudaEventDestroy(start);cudaEventDestroy(end);cudaStreamDestroy(stream);
        cudaFree(obs);cudaFree(actions);cudaFree(rewards);cudaFree(terminals);cudaFree(rng);cudaFree(ticks);dict_clear(&options);
        return 0;
    }catch(const std::exception& e){fprintf(stderr,"environment benchmark: %s\n",e.what());return 1;}
}
