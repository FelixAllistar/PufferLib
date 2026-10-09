#include "../parity_format.h"
#include <cuda_runtime.h>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <vector>

#define CUDA_CHECK(x) do {auto err=(x);if(err!=cudaSuccess)throw std::runtime_error(cudaGetErrorString(err));} while(0)
struct Case {FpgState state;int offset,frames;};
struct Difference {int kind,frame,index;float actual,expected;};
__global__ void compare_rom(Case* cases,const FpgParityFrame* frames,Difference* errors,int n,FpgConfig cfg) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;if(i>=n||errors[i].kind)return;
    Case* c=&cases[i];int t=c->state.tick;if(t>=c->frames)return;
    const FpgParityFrame* expected=&frames[c->offset+t];
    fpg_step_task(&c->state,&cfg,expected->action);
    const int* actual_body=(const int*)&c->state.body;
    const int* expected_body=(const int*)&expected->body;
    for(int j=0;j<(int)(sizeof(FpgBody)/sizeof(int));j++)if(actual_body[j]!=expected_body[j]) {
        errors[i]={1,t+1,j,(float)actual_body[j],(float)expected_body[j]};return;
    }
    const int* actual_camera=(const int*)&c->state.camera;
    const int* expected_camera=(const int*)&expected->camera;
    for(int j=0;j<(int)(sizeof(FpgCamera)/sizeof(int));j++)if(actual_camera[j]!=expected_camera[j]) {
        errors[i]={2,t+1,j,(float)actual_camera[j],(float)expected_camera[j]};return;
    }
    const int* actual_actors=(const int*)&c->state.actors;
    const int* expected_actors=(const int*)&expected->actors;
    for(int j=0;j<(int)(sizeof(FpgActors)/sizeof(int));j++)if(actual_actors[j]!=expected_actors[j]) {
        errors[i]={5,t+1,j,(float)actual_actors[j],(float)expected_actors[j]};return;
    }
    for(int j=0;j<FPG_OBS;j++) {
        float actual=fpg_observation(&c->state,&cfg,j),wanted=expected->observations[j];
        if(!isfinite(actual)||fabsf(actual-wanted)>1e-6f) {errors[i]={3,t+1,j,actual,wanted};return;}
    }
    if(c->state.status!=expected->status)errors[i]={4,t+1,0,(float)c->state.status,(float)expected->status};
}
int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("usage: test_rom_cuda ROM_TRACES.bin");
        std::ifstream input(argv[1],std::ios::binary);FpgParityHeader header;
        if(!input.read((char*)&header,sizeof(header))||header.magic!=FPG_PARITY_MAGIC||header.version!=1
            ||header.case_size!=sizeof(FpgParityCase)||header.frame_size!=sizeof(FpgParityFrame)
            ||header.config.contract_version!=2||!header.cases||header.cases>100000)
            throw std::runtime_error("invalid ROM trace header/contract");
        std::vector<Case> cases;std::vector<FpgParityFrame> frames;int max_frames=0;
        for(unsigned int i=0;i<header.cases;i++) {
            FpgParityCase c;
            if(!input.read((char*)&c,sizeof(c))||c.frames<1||c.frames>2000||c.initial.tick!=0)
                throw std::runtime_error("invalid ROM trace case");
            int offset=(int)frames.size();cases.push_back({c.initial,offset,c.frames});
            frames.resize(offset+c.frames);
            if(!input.read((char*)&frames[offset],(size_t)c.frames*sizeof(FpgParityFrame)))
                throw std::runtime_error("short ROM trace");
            if(c.frames>max_frames)max_frames=c.frames;
        }
        if(input.peek()!=EOF)throw std::runtime_error("extra ROM trace data");
        // Keep device memory bounded even for hundreds of MB of ROM traces.
        // This also lets the regression run on the 3GB card while it drives a desktop.
        const int batch=32;
        Case* device_cases;FpgParityFrame* device_frames;Difference* device_errors;
        CUDA_CHECK(cudaMalloc(&device_cases,batch*sizeof(Case)));
        CUDA_CHECK(cudaMalloc(&device_frames,(size_t)batch*max_frames*sizeof(FpgParityFrame)));
        CUDA_CHECK(cudaMalloc(&device_errors,batch*sizeof(Difference)));
        cudaStream_t stream;CUDA_CHECK(cudaStreamCreateWithFlags(&stream,cudaStreamNonBlocking));
        cudaGraph_t graph;cudaGraphExec_t executable;int n=(int)cases.size();
        CUDA_CHECK(cudaStreamBeginCapture(stream,cudaStreamCaptureModeGlobal));
        compare_rom<<<1,32,0,stream>>>(device_cases,device_frames,device_errors,batch,header.config);
        CUDA_CHECK(cudaGetLastError());CUDA_CHECK(cudaStreamEndCapture(stream,&graph));
        CUDA_CHECK(cudaGraphInstantiate(&executable,graph,NULL,NULL,0));
        int failed=0;
        for(int first=0;first<n;first+=batch) {
            int count=n-first<batch?n-first:batch,steps=0,offset=cases[first].offset;
            Case local[batch]={};Difference errors[batch];
            for(int i=0;i<count;i++) {
                local[i]=cases[first+i];local[i].offset-=offset;
                if(local[i].frames>steps)steps=local[i].frames;
            }
            int frame_count=local[count-1].offset+local[count-1].frames;
            CUDA_CHECK(cudaMemcpyAsync(device_cases,local,sizeof(local),cudaMemcpyHostToDevice,stream));
            CUDA_CHECK(cudaMemcpyAsync(device_frames,&frames[offset],(size_t)frame_count*sizeof(FpgParityFrame),cudaMemcpyHostToDevice,stream));
            CUDA_CHECK(cudaMemsetAsync(device_errors,0,sizeof(errors),stream));
            for(int t=0;t<steps;t++) {
                if(t%2)CUDA_CHECK(cudaGraphLaunch(executable,stream));
                else compare_rom<<<1,32,0,stream>>>(device_cases,device_frames,device_errors,batch,header.config);
                CUDA_CHECK(cudaGetLastError());
            }
            CUDA_CHECK(cudaStreamSynchronize(stream));
            CUDA_CHECK(cudaMemcpy(errors,device_errors,sizeof(errors),cudaMemcpyDeviceToHost));
            CUDA_CHECK(cudaMemcpy(local,device_cases,sizeof(local),cudaMemcpyDeviceToHost));
            for(int i=0;i<count;i++) {
                if(errors[i].kind) {
                    if(failed++<10)fprintf(stderr,"case=%d kind=%d frame=%d field=%d native=%g ROM=%g\n",first+i,errors[i].kind,errors[i].frame,errors[i].index,errors[i].actual,errors[i].expected);
                } else if(local[i].state.tick!=local[i].frames)throw std::runtime_error("incomplete CUDA trajectory");
            }
        }
        printf("{\"cases\":%d,\"rom_frames\":%zu,\"failed_cases\":%d,\"integer_tolerance\":0,\"observation_tolerance\":1e-6,\"direct_and_cuda_graph\":true}\n",n,frames.size(),failed);
        CUDA_CHECK(cudaGraphExecDestroy(executable));CUDA_CHECK(cudaGraphDestroy(graph));CUDA_CHECK(cudaStreamDestroy(stream));
        CUDA_CHECK(cudaFree(device_cases));CUDA_CHECK(cudaFree(device_frames));CUDA_CHECK(cudaFree(device_errors));
        return failed?1:0;
    } catch(const std::exception& e){fprintf(stderr,"ROM CUDA parity: %s\n",e.what());return 2;}
}
