#include "cuda_module.h"
#include <cuda.h>
#include <stdexcept>

static CUmodule module;
static CUfunction kernel;
static void driver_checked(CUresult result) {
    if(result==CUDA_SUCCESS)return;
    const char* message=nullptr;cuGetErrorString(result,&message);
    throw std::runtime_error(message?message:"CUDA driver error");
}
void smb_cuda_replay_init(const char* path) {
    driver_checked(cuInit(0));driver_checked(cuModuleLoad(&module,path));
    driver_checked(cuModuleGetFunction(&kernel,module,
        "_Z6replayP8SmbLogicPKhPK12SmbTraceCasePKiPK13SmbTraceFrameP15SmbReplayResultiii"));
}
void smb_cuda_replay_launch(SmbLogic* states,const uint8_t* data,const SmbTraceCase* cases,
        const int* offsets,const SmbTraceFrame* frames,SmbReplayResult* results,
        int count,int begin,int end,cudaStream_t stream) {
    void* args[]={&states,&data,&cases,&offsets,&frames,&results,&count,&begin,&end};
    // Each independent scene gets a block. This keeps the verification jobs
    // distributed across the device despite unrelated per-scene control flow.
    driver_checked(cuLaunchKernel(kernel,count,1,1,1,1,1,0,stream,args,nullptr));
}
void smb_cuda_replay_shutdown() {
    driver_checked(cuModuleUnload(module));module=nullptr;kernel=nullptr;
}
