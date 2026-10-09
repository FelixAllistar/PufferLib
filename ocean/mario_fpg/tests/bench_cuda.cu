#define FPG_HEADLESS
#include "../mario_fpg.cu"
#include <vector>
int main(int argc,char** argv) {
    int version=argc>1?atoi(argv[1]):2;
    if(version<1||version>2){fprintf(stderr,"usage: bench_cuda [1|2]\n");return 2;}
    const int n=4096,steps=2048,chunk=64;float *obs,*actions,*rewards,*terminals;
    FPG_CUDA(cudaMalloc(&obs,n*FPG_OBS*sizeof(float)));FPG_CUDA(cudaMalloc(&actions,n*sizeof(float)));
    FPG_CUDA(cudaMalloc(&rewards,n*sizeof(float)));FPG_CUDA(cudaMalloc(&terminals,n*sizeof(float)));
    Dict d={0};dict_set(&d,"contract_version",version);Env* envs=puf_vec_create(n,&d,obs,actions,rewards,terminals);
    std::vector<float> a(n);for(int i=0;i<n;i++)a[i]=(float)(fpg_hash((unsigned)i+1)%12);
    FPG_CUDA(cudaMemcpy(actions,a.data(),n*sizeof(float),cudaMemcpyHostToDevice));
    cudaStream_t stream;FPG_CUDA(cudaStreamCreateWithFlags(&stream,cudaStreamNonBlocking));puf_bind_stream(stream);puf_reset(envs);
    for(int i=0;i<64;i++)puf_step(envs);FPG_CUDA(cudaStreamSynchronize(stream));
    cudaGraph_t graph;cudaGraphExec_t executable;
    FPG_CUDA(cudaStreamBeginCapture(stream,cudaStreamCaptureModeGlobal));for(int i=0;i<chunk;i++)puf_step(envs);
    FPG_CUDA(cudaStreamEndCapture(stream,&graph));FPG_CUDA(cudaGraphInstantiate(&executable,graph,NULL,NULL,0));
    cudaEvent_t start,end;FPG_CUDA(cudaEventCreate(&start));FPG_CUDA(cudaEventCreate(&end));
    FPG_CUDA(cudaEventRecord(start,stream));for(int i=0;i<steps/chunk;i++)FPG_CUDA(cudaGraphLaunch(executable,stream));
    FPG_CUDA(cudaEventRecord(end,stream));FPG_CUDA(cudaEventSynchronize(end));float ms;FPG_CUDA(cudaEventElapsedTime(&ms,start,end));
    printf("{\"contract_version\":%d,\"agents\":%d,\"game_frames\":%d,\"seconds\":%.6f,\"frames_per_second\":%.1f,\"observations\":%d,\"resets_and_augmentation_included\":true,\"ppo_included\":false,\"action_source\":\"fixed per-env uniform action IDs\"}\n",version,n,n*steps,ms/1000.0,(double)n*steps*1000/ms,FPG_OBS);
    FPG_CUDA(cudaGraphExecDestroy(executable));FPG_CUDA(cudaGraphDestroy(graph));cudaEventDestroy(start);cudaEventDestroy(end);
    puf_close(envs);cudaStreamDestroy(stream);cudaFree(obs);cudaFree(actions);cudaFree(rewards);cudaFree(terminals);return 0;
}
