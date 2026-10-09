#include "trace.h"
#include "logic_cpu.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

template<class T>static void read_exact(std::ifstream& in,T* out,size_t count=1) {
    if(!in.read((char*)out,sizeof(T)*count))throw std::runtime_error("truncated trace");
}
struct Trial {SmbScene scene;std::vector<SmbTraceFrame> frames;};
int main(int argc,char** argv) {
    try {
        if(argc<2||argc>3||(argc==3&&std::string(argv[2])!="--bench"))
            throw std::runtime_error("usage: verify_runtime_cpu QUALIFIED_TRACE [--bench]");
        bool bench=argc==3;
        std::ifstream in(argv[1],std::ios::binary);SmbTraceHeader header={};read_exact(in,&header);
        if(header.magic!=SMB_TRACE_MAGIC||header.version!=SMB_TRACE_VERSION
            ||header.scene_size!=sizeof(SmbScene)||header.case_size!=sizeof(SmbTraceCase)
            ||header.frame_size!=sizeof(SmbTraceFrame)||header.scope!=1
            ||header.rom_fingerprint!=0x6e01246e5d215cb3ull||!header.cases||!header.frames)
            throw std::runtime_error("unqualified/incompatible trace");
        std::vector<uint8_t> data(SMB_PRG);read_exact(in,data.data(),data.size());
        unsigned failures=0;uint64_t frames=0,measured_frames=0,active_frames=0,instructions=0;
        double seconds=0;
        for(unsigned index=0;index<header.cases;index++) {
            SmbTraceCase c;read_exact(in,&c);
            if(!c.frames||c.frames>240)throw std::runtime_error("invalid clip length");
            std::vector<SmbTraceFrame> expected(c.frames);read_exact(in,expected.data(),expected.size());
            frames+=c.frames;
            if(bench&&c.kind!=16)continue;
            SmbLogic state;int fault=smb_scene_reset(&state,&c.scene),field=-1,bad_frame=-1;
            auto begin=std::chrono::steady_clock::now();
            for(unsigned t=0;t<c.frames&&!fault&&field<0;t++) {
                const auto& f=expected[t];
                fault=smb_native_frame(&state,data.data(),f.buttons);
                measured_frames++;instructions+=state.instructions;
                active_frames+=state.ram[0x770]==1&&state.ram[0x772]==3&&state.ram[0xe]==8;
                if(bench)continue;
                if(memcmp(state.ram,f.ram,SMB_RAM)) {
                    for(int k=0;k<SMB_RAM;k++)if(state.ram[k]!=f.ram[k]){field=k;break;}
                } else if(state.pc!=f.pc||state.a!=f.a||state.x!=f.x||state.y!=f.y
                    ||(state.p&207)!=f.p||state.sp!=f.sp||state.timing.timestamp!=f.timestamp
                    ||state.timing.video_frame!=f.video_frame||state.timing.control!=f.control
                    ||state.timing.mask!=f.mask)field=SMB_RAM;
                if(fault||field>=0)bad_frame=(int)t;
            }
            seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
            if(fault||field>=0) {
                failures++;
                if(failures<=8)fprintf(stderr,"case=%u frame=%d field=%x fault=%x\n",index,bad_frame,field,fault);
            }
        }
        if(frames!=header.frames||in.peek()!=EOF)throw std::runtime_error("frame count/trailer mismatch");
        printf("{\"mode\":\"%s\",\"source_cases\":%u,\"source_frames\":%llu,\"frames\":%llu,"
               "\"active_frames\":%llu,\"failures\":%u,\"seconds\":%.6f,\"frames_per_second\":%.1f,"
               "\"average_virtual_instructions\":%.1f,\"ppo_included\":false,\"observations_included\":false}\n",
               bench?"engine_benchmark":"full_trace_verification",header.cases,(unsigned long long)frames,
               (unsigned long long)measured_frames,(unsigned long long)active_frames,failures,seconds,
               measured_frames/seconds,(double)instructions/measured_frames);
        return failures?1:0;
    }catch(const std::exception& e){fprintf(stderr,"CPU runtime: %s\n",e.what());return 2;}
}
