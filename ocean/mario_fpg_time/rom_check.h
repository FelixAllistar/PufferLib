#pragma once
#include "../mario_sim/rom_import.h"
#include "../mario_sim/trace.h"
#include <fstream>
#include <stdexcept>
#include <vector>

static void fpt_rom_require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
static int fpt_rom_difference(const SmbLogic& s,const SmbLogic& r) {
    for(int k=0;k<SMB_RAM;k++)if(s.ram[k]!=r.ram[k])return k;
    if(s.pc!=r.pc||s.a!=r.a||s.x!=r.x||s.y!=r.y||(s.p&207)!=(r.p&207)||s.sp!=r.sp)return SMB_RAM;
    if(s.timing.timestamp!=r.timing.timestamp||s.timing.video_frame!=r.timing.video_frame
        ||s.timing.control!=r.timing.control||s.timing.mask!=r.timing.mask)return SMB_RAM+1;
    return -1;
}
static SmbTraceFrame fpt_rom_record(const SmbLogic& s,int buttons) {
    SmbTraceFrame f={};f.buttons=(uint8_t)buttons;memcpy(f.ram,s.ram,SMB_RAM);
    f.pc=s.pc;f.a=s.a;f.x=s.x;f.y=s.y;f.p=s.p&207;f.sp=s.sp;
    f.control=s.timing.control;f.mask=s.timing.mask;f.timestamp=s.timing.timestamp;f.video_frame=s.timing.video_frame;return f;
}
static void fpt_save_snapshot(const Nes_State& state,const std::string& path) {
    Mem_Writer writer;const char* error=state.write(writer);if(error)throw std::runtime_error(error);
    std::ofstream out(path,std::ios::binary);out.write(writer.data(),writer.size());
    fpt_rom_require(bool(out),"cannot write ROM pipe snapshot");
}
static void fpt_load_snapshot(Nes_State* state,const std::string& path) {
    std::ifstream in(path,std::ios::binary);fpt_rom_require(bool(in),"missing ROM pipe snapshot");
    std::vector<char> data((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    fpt_rom_require(!data.empty()&&data.size()<=1024*1024,"invalid ROM pipe snapshot size");
    Mem_File_Reader reader(data.data(),(long)data.size());const char* error=state->read(reader);
    if(error)throw std::runtime_error(error);
}
