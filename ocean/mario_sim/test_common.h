#pragma once
#include "trace.h"
#include <cmath>
#include <fstream>
#include <vector>

static void fpt_check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
static bool fpt_near(float a,float b) {return std::isfinite(a)&&std::isfinite(b)&&fabsf(a-b)<=1e-5f*fmaxf(1.0f,fabsf(a));}
static void fpt_same_state(const SmbLogic& a,const SmbLogic& b) {
    fpt_check(!memcmp(a.ram,b.ram,SMB_RAM)&&a.pc==b.pc&&a.a==b.a&&a.x==b.x&&a.y==b.y&&a.p==b.p&&a.sp==b.sp
        &&!memcmp(a.joy,b.joy,2)&&!memcmp(a.joy_shift,b.joy_shift,2)&&a.strobe==b.strobe
        &&a.instructions==b.instructions&&a.fault==b.fault
        &&!memcmp(&a.timing,&b.timing,offsetof(SmbClock,sprites)+256),"native state mismatch");
}
struct FptTestTeacher {SmbTraceCase item;std::vector<SmbTraceFrame> frames;};
static std::vector<FptTestTeacher> fpt_test_teachers() {
    std::ifstream in("build/mario_sim/fpg/pipe/teacher/clips.bin",std::ios::binary);SmbTraceHeader h={};
    fpt_check(bool(in.read((char*)&h,sizeof(h)))&&h.magic==SMB_TRACE_MAGIC&&h.version==SMB_TRACE_VERSION
        &&h.scene_size==sizeof(SmbScene)&&h.case_size==sizeof(SmbTraceCase)&&h.frame_size==sizeof(SmbTraceFrame)
        &&h.cases>0&&h.cases<=64,"invalid test teacher");in.seekg(SMB_PRG,std::ios::cur);
    std::vector<FptTestTeacher> teachers;uint64_t frames=0;
    for(unsigned i=0;i<h.cases;i++) {
        FptTestTeacher t;fpt_check(bool(in.read((char*)&t.item,sizeof(t.item)))&&t.item.frames>0&&t.item.frames<=1800,"invalid teacher size");
        t.frames.resize(t.item.frames);fpt_check(bool(in.read((char*)t.frames.data(),t.frames.size()*sizeof(SmbTraceFrame))),"truncated teacher");
        frames+=t.frames.size();teachers.push_back(std::move(t));
    }
    fpt_check(frames==h.frames&&in.peek()==EOF,"invalid teacher frame count");return teachers;
}

// A prior FPG-only sidecar: the format predates mixed actor pools. Use a local
// fixture so these migration checks do not require someone's trained weights.
static void fpt_test_old_progress(const char* path,uint64_t bank_hash,const FptCurriculumConfig& c) {
    {std::ofstream out(path,std::ios::binary);out<<"FPG-only format fixture";}
    std::vector<FptCurriculumProgress> progress(4096);
    for(auto& p:progress) {
        fpt_curriculum_init(&p,&c);
        for(int i=0;i<FPT_CURRICULUM_LEVELS;i++)if(c.depths[i]==96)p.level=p.sampled_level=i;
    }
    fpt_write_progress(path,bank_hash,c,progress);
}
