#include <cuda_runtime.h>
#include "trace.h"
#include "cuda_module.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdio>
#include <stdexcept>
#include <string>

static void checked(cudaError_t r){if(r!=cudaSuccess)throw std::runtime_error(cudaGetErrorString(r));}
template<class T>static void read(std::ifstream& f,T* p,size_t count=1) {
    if(!f.read((char*)p,count*sizeof(T)))throw std::runtime_error("truncated trace");
}
int main(int argc,char** argv) {
    try {
        if(argc!=2&&argc!=4)throw std::runtime_error("usage: verify_cuda QUALIFIED_CLIPS.bin [CASE_BEGIN CASE_END]");
        std::ifstream in(argv[1],std::ios::binary);SmbTraceHeader h={};read(in,&h);
        if(h.magic!=SMB_TRACE_MAGIC||h.version!=SMB_TRACE_VERSION||h.scene_size!=sizeof(SmbScene)
           ||h.case_size!=sizeof(SmbTraceCase)||h.frame_size!=sizeof(SmbTraceFrame)||h.scope!=1
           ||h.rom_fingerprint!=0x6e01246e5d215cb3ull||!h.cases||!h.frames)
            throw std::runtime_error("trace is incomplete, failed, or incompatible");
        std::vector<uint8_t> data(SMB_PRG);read(in,data.data(),data.size());
        const int batch=SMB_CUDA_REPLAY_BATCH,maximum_frames=240;
        unsigned case_begin=argc==4?(unsigned)std::stoul(argv[2]):0;
        unsigned case_end=argc==4?(unsigned)std::stoul(argv[3]):h.cases;
        if(case_begin>=case_end||case_end>h.cases||case_begin%batch
            ||(case_end!=h.cases&&case_end%batch))throw std::runtime_error("range must cover complete replay batches");
        SmbLogic* states=nullptr;uint8_t* device_data=nullptr;SmbTraceCase* dc=nullptr;
        SmbTraceFrame* df=nullptr;int* offsets=nullptr;SmbReplayResult* dr=nullptr;
        checked(cudaMalloc(&states,batch*sizeof(*states)));checked(cudaMalloc(&device_data,SMB_PRG));
        checked(cudaMalloc(&dc,batch*sizeof(*dc)));checked(cudaMalloc(&df,batch*maximum_frames*sizeof(*df)));
        checked(cudaMalloc(&offsets,batch*sizeof(int)));checked(cudaMalloc(&dr,batch*sizeof(*dr)));
        cudaStream_t stream;checked(cudaStreamCreateWithFlags(&stream,cudaStreamNonBlocking));
        auto module_path=std::filesystem::absolute(argv[0]).parent_path()/"cuda_replay.cubin";
        smb_cuda_replay_init(module_path.c_str());
        checked(cudaMemcpyAsync(device_data,data.data(),SMB_PRG,cudaMemcpyHostToDevice,stream));
        unsigned completed=0,verified_cases=0;long total_frames=0,verified_frames=0;int failures=0;
        while(completed<h.cases) {
            int n=(int)(h.cases-completed);if(n>batch)n=batch;
            std::vector<SmbTraceCase> cases(n);std::vector<SmbTraceFrame> frames;
            std::vector<int> starts(n);std::vector<SmbReplayResult> result(n);
            for(int i=0;i<n;i++) {
                read(in,&cases[i]);starts[i]=frames.size();
                if(!cases[i].frames||cases[i].frames>maximum_frames)throw std::runtime_error("invalid clip length");
                frames.resize(frames.size()+cases[i].frames);read(in,frames.data()+starts[i],cases[i].frames);
            }
            total_frames+=frames.size();
            if(completed<case_begin||completed>=case_end){completed+=n;continue;}
            checked(cudaMemcpyAsync(dc,cases.data(),n*sizeof(*dc),cudaMemcpyHostToDevice,stream));
            checked(cudaMemcpyAsync(df,frames.data(),frames.size()*sizeof(*df),cudaMemcpyHostToDevice,stream));
            checked(cudaMemcpyAsync(offsets,starts.data(),n*sizeof(int),cudaMemcpyHostToDevice,stream));
            if((completed/batch)&1) {
                cudaGraph_t graph;cudaGraphExec_t executable;
                checked(cudaStreamBeginCapture(stream,cudaStreamCaptureModeThreadLocal));
                for(int t=0;t<maximum_frames;t+=16)smb_cuda_replay_launch(states,device_data,dc,offsets,df,dr,n,t,t+16,stream);
                checked(cudaStreamEndCapture(stream,&graph));checked(cudaGraphInstantiate(&executable,graph,0,0,0));
                checked(cudaGraphLaunch(executable,stream));checked(cudaGraphExecDestroy(executable));checked(cudaGraphDestroy(graph));
            } else for(int t=0;t<maximum_frames;t+=16)smb_cuda_replay_launch(states,device_data,dc,offsets,df,dr,n,t,t+16,stream);
            checked(cudaGetLastError());checked(cudaMemcpyAsync(result.data(),dr,n*sizeof(*dr),cudaMemcpyDeviceToHost,stream));
            checked(cudaStreamSynchronize(stream));
            for(int i=0;i<n;i++)if(result[i].fault||result[i].field>=0) {
                failures++;if(failures<=12)fprintf(stderr,"CUDA case=%u frame=%d field=%x fault=%x\n",completed+i,result[i].frame,result[i].field,result[i].fault);
            }
            verified_frames+=frames.size();verified_cases+=n;completed+=n;
            fprintf(stderr,"CUDA %u/%u selected cases, %ld frames, %d failures\n",verified_cases,case_end-case_begin,verified_frames,failures);
        }
        if(total_frames!=h.frames||in.peek()!=EOF)throw std::runtime_error("trace frame count/trailer mismatch");
        smb_cuda_replay_shutdown();
        checked(cudaStreamDestroy(stream));cudaFree(states);cudaFree(device_data);cudaFree(dc);cudaFree(df);cudaFree(offsets);cudaFree(dr);
        printf("{\"scope\":\"video_frame_clips\",\"cases\":%u,\"frames\":%ld,\"failures\":%d,\"bitwise_observations\":true,\"source_cases\":%u,\"source_frames\":%ld,\"case_begin\":%u,\"case_end\":%u}\n",
            verified_cases,verified_frames,failures,h.cases,total_frames,case_begin,case_end);
        return failures?1:0;
    }catch(const std::exception& e){fprintf(stderr,"CUDA reconstruction: %s\n",e.what());return 2;}
}
