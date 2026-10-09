#pragma once
#include "logic.h"
#include <cuda.h>
#include <cuda_runtime.h>
#include <stdexcept>

static inline void smb_cuda_check(cudaError_t status) {
    if(status!=cudaSuccess)throw std::runtime_error(cudaGetErrorString(status));
}
static inline void smb_driver_check(CUresult status) {
    if(status==CUDA_SUCCESS)return;
    const char* message=nullptr;cuGetErrorString(status,&message);
    throw std::runtime_error(message?message:"Mario CUDA driver error");
}
class SmbCudaRuntime {
    CUmodule module=nullptr;
    CUfunction step_kernel=nullptr;
public:
    int threads=32;
    void open(const char* path) {
        if(module)throw std::runtime_error("Mario runtime already open");
        smb_driver_check(cuInit(0));smb_driver_check(cuModuleLoad(&module,path));
        CUdeviceptr ptr;size_t size;unsigned contract[4];
        smb_driver_check(cuModuleGetGlobal(&ptr,&size,module,"smb_runtime_contract"));
        if(size!=sizeof(contract))throw std::runtime_error("incompatible Mario runtime contract size");
        smb_driver_check(cuMemcpyDtoH(contract,ptr,sizeof(contract)));
        if(contract[0]!=0x534d5231||contract[1]!=1||contract[2]!=sizeof(SmbLogic)||contract[3]!=SMB_DEBUG_OBS)
            throw std::runtime_error("incompatible Mario runtime module; rebuild runtime target");
        smb_driver_check(cuModuleGetFunction(&step_kernel,module,"smb_runtime_actions"));
    }
    void step(SmbLogic* states,const uint8_t* worlds,const uint32_t* world_ids,const float* actions,
            int count,cudaStream_t stream) {
        void* args[]={&states,&worlds,&world_ids,&actions,&count};
        smb_driver_check(cuLaunchKernel(step_kernel,(count+threads-1)/threads,1,1,threads,1,1,0,stream,args,nullptr));
    }
    void close() {
        if(module){smb_driver_check(cuModuleUnload(module));module=nullptr;step_kernel=nullptr;}
    }
};
