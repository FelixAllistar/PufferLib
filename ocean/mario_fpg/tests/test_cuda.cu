#define FPG_HEADLESS
#include "../mario_fpg.cu"
#include <assert.h>
#include <vector>
int main() {
    const int n=17;float *obs,*actions,*rewards,*terminals;
    FPG_CUDA(cudaMalloc(&obs,n*FPG_OBS*sizeof(float)));FPG_CUDA(cudaMalloc(&actions,n*sizeof(float)));
    FPG_CUDA(cudaMalloc(&rewards,n*sizeof(float)));FPG_CUDA(cudaMalloc(&terminals,n*sizeof(float)));
    Dict d={0};dict_set(&d,"max_frames",32);dict_set(&d,"contract_version",2);Env* envs=puf_vec_create(n,&d,obs,actions,rewards,terminals);
    cudaStream_t stream;FPG_CUDA(cudaStreamCreateWithFlags(&stream,cudaStreamNonBlocking));puf_bind_stream(stream);puf_reset(envs);FPG_CUDA(cudaStreamSynchronize(stream));
    std::vector<FpgState> cpu(n),gpu(n);std::vector<Env> shells(n);
    FpgCurriculum curricula[n]={};uint32_t rng[n];
    for(int i=0;i<n;i++){rng[i]=fpg_hash((uint32_t)fpg_host_config.seed^(unsigned)i)|1u;fpg_reset_task(&cpu[i],&fpg_host_config,fpg_bank,fpg_bank_count,&curricula[i],&rng[i]);}
    cudaGraph_t graph;cudaGraphExec_t executable;
    FPG_CUDA(cudaStreamBeginCapture(stream,cudaStreamCaptureModeGlobal));puf_step(envs);FPG_CUDA(cudaStreamEndCapture(stream,&graph));FPG_CUDA(cudaGraphInstantiate(&executable,graph,NULL,NULL,0));
    uint32_t action_rng=127;std::vector<float> actual_obs(n*FPG_OBS);
    int successes=0,ended=0;
    for(int t=0;t<512;t++) {
        float a[n],expected_r[n],expected_t[n],actual_r[n],actual_t[n];
        for(int i=0;i<n;i++) {
            const FpgCase* sample=&fpg_bank[cpu[i].case_index];int at=sample->length-cpu[i].reset_remaining+cpu[i].tick;
            a[i]=i%2||at>=sample->length?(float)(fpg_rand(&action_rng)%12):(float)sample->frames[at].action;
            expected_r[i]=fpg_step_task(&cpu[i],&fpg_host_config,(int)a[i]);expected_t[i]=cpu[i].status!=FPG_ACTIVE;
            if(expected_t[i]){successes+=cpu[i].status==FPG_SUCCESS;ended++;fpg_record(&curricula[i],&cpu[i]);fpg_reset_task(&cpu[i],&fpg_host_config,fpg_bank,fpg_bank_count,&curricula[i],&rng[i]);}
        }
        FPG_CUDA(cudaMemcpyAsync(actions,a,sizeof(a),cudaMemcpyHostToDevice,stream));
        if(t%2)FPG_CUDA(cudaGraphLaunch(executable,stream));else puf_step(envs);
        FPG_CUDA(cudaStreamSynchronize(stream));
        FPG_CUDA(cudaMemcpy(gpu.data(),fpg_states,n*sizeof(FpgState),cudaMemcpyDeviceToHost));FPG_CUDA(cudaMemcpy(shells.data(),envs,n*sizeof(Env),cudaMemcpyDeviceToHost));
        FPG_CUDA(cudaMemcpy(actual_r,rewards,sizeof(actual_r),cudaMemcpyDeviceToHost));FPG_CUDA(cudaMemcpy(actual_t,terminals,sizeof(actual_t),cudaMemcpyDeviceToHost));
        FPG_CUDA(cudaMemcpy(actual_obs.data(),obs,actual_obs.size()*sizeof(float),cudaMemcpyDeviceToHost));
        for(int i=0;i<n;i++) {
            assert(!memcmp(&gpu[i],&cpu[i],sizeof(FpgState)));assert(shells[i].rng==rng[i]);
            for(int k=0;k<4;k++){assert(shells[i].curriculum.tries[k]==curricula[i].tries[k]);assert(shells[i].curriculum.wins[k]==curricula[i].wins[k]);assert(fabsf(shells[i].curriculum.success[k]-curricula[i].success[k])<1e-6f);}
            assert(actual_r[i]==expected_r[i]&&actual_t[i]==expected_t[i]);
            for(int j=0;j<FPG_OBS;j++)assert(fabsf(actual_obs[i*FPG_OBS+j]-fpg_observation(&cpu[i],&fpg_host_config,j))<1e-6f);
        }
    }
    assert(successes>20&&ended>100);printf("CUDA parity: %d decisions, %d resets, %d FPG successes; direct and graph replay agree\n",n*512,ended,successes);
    FPG_CUDA(cudaGraphExecDestroy(executable));FPG_CUDA(cudaGraphDestroy(graph));puf_close(envs);FPG_CUDA(cudaStreamDestroy(stream));
    cudaFree(obs);cudaFree(actions);cudaFree(rewards);cudaFree(terminals);return 0;
}
