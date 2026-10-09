#include "trace.h"
#include <cuda.h>
#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

static void check(cudaError_t e) {if(e!=cudaSuccess)throw std::runtime_error(cudaGetErrorString(e));}
static void check(CUresult e) {
    if(e!=CUDA_SUCCESS){const char* message=nullptr;cuGetErrorString(e,&message);throw std::runtime_error(message?message:"CUDA driver error");}
}
template<class T>static void read_exact(std::ifstream& in,T* out,size_t count=1) {
    if(!in.read((char*)out,count*sizeof(T)))throw std::runtime_error("truncated trace");
}
int main(int argc,char** argv) {
    try {
        if(argc<2||argc>7)throw std::runtime_error("usage: bench_runtime_gpu TRACE [AGENTS=256] [THREADS=32] [LOCAL=0] [FRAMES_PER_LAUNCH=16] [FRAMES=64]");
        int count=argc>2?std::stoi(argv[2]):256,threads=argc>3?std::stoi(argv[3]):32;
        int local=argc>4?std::stoi(argv[4]):0,chunk=argc>5?std::stoi(argv[5]):16;
        int horizon=argc>6?std::stoi(argv[6]):64;
        if(count<1||count>65536||threads<1||threads>256||local<0||local>2||chunk<1||chunk>120||horizon<1||horizon>240||horizon%chunk)
            throw std::runtime_error("invalid benchmark parameters");
        unsigned shared=local==2?(unsigned)(threads*sizeof(SmbLogic)):0;
        if(shared>48*1024)throw std::runtime_error("shared state exceeds per-block limit");
        std::ifstream in(argv[1],std::ios::binary);SmbTraceHeader h;read_exact(in,&h);
        if(h.magic!=SMB_TRACE_MAGIC||h.version!=SMB_TRACE_VERSION||h.scene_size!=sizeof(SmbScene)
            ||h.case_size!=sizeof(SmbTraceCase)||h.frame_size!=sizeof(SmbTraceFrame)
            ||h.rom_fingerprint!=0x6e01246e5d215cb3ull||!h.cases||!h.frames||h.scope!=1)
            throw std::runtime_error("unqualified/incompatible trace");
        std::vector<uint8_t> world(SMB_PRG);read_exact(in,world.data(),world.size());
        std::vector<SmbScene> scenes;uint64_t source_frames=0;
        for(unsigned i=0;i<h.cases;i++) {
            SmbTraceCase c;read_exact(in,&c);
            if(!c.frames||c.frames>240)throw std::runtime_error("invalid clip");
            if(c.kind==16)scenes.push_back(c.scene);
            in.seekg((size_t)c.frames*sizeof(SmbTraceFrame),std::ios::cur);source_frames+=c.frames;
        }
        if(source_frames!=h.frames||in.peek()!=EOF||scenes.empty())throw std::runtime_error("invalid source counts");
        std::vector<SmbLogic> initial(count);std::vector<uint8_t> buttons(count);
        for(int i=0;i<count;i++) {
            if(smb_scene_reset(&initial[i],&scenes[(size_t)i*scenes.size()/count]))throw std::runtime_error("reset failed");
            static const uint8_t masks[]={0,130,131,64,129,3,128,1};buttons[i]=masks[i%8];
        }
        SmbLogic* states;uint8_t *worlds,*actions;unsigned *world_ids,*active;
        check(cudaMalloc(&states,(size_t)count*sizeof(SmbLogic)));check(cudaMalloc(&worlds,SMB_PRG));
        check(cudaMalloc(&actions,count));check(cudaMalloc(&world_ids,count*sizeof(unsigned)));
        check(cudaMalloc(&active,count*sizeof(unsigned)));
        check(cudaMemcpy(worlds,world.data(),SMB_PRG,cudaMemcpyHostToDevice));
        check(cudaMemcpy(actions,buttons.data(),count,cudaMemcpyHostToDevice));
        check(cudaMemset(world_ids,0,count*sizeof(unsigned)));check(cudaMemset(active,0,count*sizeof(unsigned)));
        cudaStream_t stream;check(cudaStreamCreateWithFlags(&stream,cudaStreamNonBlocking));
        CUmodule module;CUfunction kernel;check(cuInit(0));
        auto path=std::filesystem::absolute(argv[0]).parent_path()/"cuda_replay.cubin";
        check(cuModuleLoad(&module,path.c_str()));
        check(cuModuleGetFunction(&kernel,module,local==2?"smb_runtime_step_shared":local?"smb_runtime_step_local":"smb_runtime_step"));
        size_t offset=0,size=0;
        check(cuFuncGetParamInfo(kernel,4,&offset,&size));
        if(size!=sizeof(void*))throw std::runtime_error("stale runtime module: rebuild gpu target");
        check(cuFuncGetParamInfo(kernel,6,&offset,&size));
        if(size!=sizeof(int))throw std::runtime_error("incompatible runtime kernel arguments");
        auto launch=[&]() {
            void* args[]={&states,&worlds,&world_ids,&actions,&active,&count,&chunk};
            check(cuLaunchKernel(kernel,(count+threads-1)/threads,1,1,threads,1,1,shared,stream,args,nullptr));
        };
        check(cudaMemcpyAsync(states,initial.data(),(size_t)count*sizeof(SmbLogic),cudaMemcpyHostToDevice,stream));
        launch();check(cudaStreamSynchronize(stream));
        check(cudaMemcpyAsync(states,initial.data(),(size_t)count*sizeof(SmbLogic),cudaMemcpyHostToDevice,stream));
        check(cudaMemsetAsync(active,0,count*sizeof(unsigned),stream));
        cudaGraph_t graph;cudaGraphExec_t executable;
        check(cudaStreamBeginCapture(stream,cudaStreamCaptureModeThreadLocal));
        for(int t=0;t<horizon;t+=chunk)launch();
        check(cudaStreamEndCapture(stream,&graph));check(cudaGraphInstantiate(&executable,graph,0,0,0));
        cudaEvent_t begin,end;check(cudaEventCreate(&begin));check(cudaEventCreate(&end));
        check(cudaEventRecord(begin,stream));check(cudaGraphLaunch(executable,stream));
        check(cudaEventRecord(end,stream));check(cudaEventSynchronize(end));
        float ms;check(cudaEventElapsedTime(&ms,begin,end));
        std::vector<unsigned> counts(count);check(cudaMemcpy(counts.data(),active,count*sizeof(unsigned),cudaMemcpyDeviceToHost));
        std::vector<SmbLogic> result(count);check(cudaMemcpy(result.data(),states,(size_t)count*sizeof(SmbLogic),cudaMemcpyDeviceToHost));
        uint64_t running=0;int faults=0;for(int i=0;i<count;i++){running+=counts[i];faults+=result[i].fault!=0;}
        int registers=0,local_bytes=0;check(cuFuncGetAttribute(&registers,CU_FUNC_ATTRIBUTE_NUM_REGS,kernel));
        check(cuFuncGetAttribute(&local_bytes,CU_FUNC_ATTRIBUTE_LOCAL_SIZE_BYTES,kernel));
        printf("{\"agents\":%d,\"threads\":%d,\"local_state\":%d,\"frames_per_launch\":%d,\"frames\":%d,"
               "\"active_frames\":%llu,\"seconds\":%.6f,\"frames_per_second\":%.1f,\"faults\":%d,"
               "\"registers\":%d,\"local_bytes\":%d,\"scene_templates\":%zu,\"observations_included\":false,"
               "\"ppo_included\":false,\"resets_included\":false,\"action_source\":\"fixed varied controller masks\"}\n",
               count,threads,local,chunk,count*horizon,(unsigned long long)running,ms/1000.0,count*horizon*1000.0/ms,
               faults,registers,local_bytes,scenes.size());
        check(cudaGraphExecDestroy(executable));check(cudaGraphDestroy(graph));check(cudaEventDestroy(begin));
        check(cudaEventDestroy(end));check(cuModuleUnload(module));check(cudaStreamDestroy(stream));
        check(cudaFree(states));check(cudaFree(worlds));check(cudaFree(actions));check(cudaFree(world_ids));check(cudaFree(active));
        return faults?1:0;
    }catch(const std::exception& e){fprintf(stderr,"runtime benchmark: %s\n",e.what());return 2;}
}
