#pragma once
#include "practice.h"
#include "table.h"
#include <filesystem>
#include <fstream>
#include <vector>

struct SmbPracticeSavedHeader {
    uint32_t magic,version,count,progress_size;
    uint64_t bank_hash,checkpoint_hash,payload_hash;
};
static void smb_write_practice(const char* checkpoint,uint64_t bank_hash,const std::vector<SmbPracticeProgress>& progress) {
    if(progress.empty())return;
    SmbPracticeSavedHeader h={0x50524331,1,(uint32_t)progress.size(),sizeof(SmbPracticeProgress),bank_hash,
        fpg_time_file_hash(checkpoint),smb_bank_hash(1469598103934665603ull,progress.data(),progress.size()*sizeof(progress[0]))};
    std::string path=std::string(checkpoint)+".practice";
    std::ofstream out(path+".tmp",std::ios::binary);out.write((const char*)&h,sizeof(h));
    out.write((const char*)progress.data(),progress.size()*sizeof(progress[0]));out.close();
    if(!out)throw std::runtime_error("cannot save practice curriculum");std::filesystem::rename(path+".tmp",path);
}
static std::vector<SmbPracticeProgress> smb_read_practice(const char* checkpoint,uint64_t bank_hash) {
    std::string path=std::string(checkpoint)+".practice";if(!std::filesystem::exists(path))return {};
    std::ifstream in(path,std::ios::binary);SmbPracticeSavedHeader h={};
    if(!in.read((char*)&h,sizeof(h))||h.magic!=0x50524331||h.version!=1||!h.count||h.count>1000000
        ||h.progress_size!=sizeof(SmbPracticeProgress)||h.bank_hash!=bank_hash||h.checkpoint_hash!=fpg_time_file_hash(checkpoint))
        throw std::runtime_error("invalid or mismatched practice checkpoint");
    std::vector<SmbPracticeProgress> progress(h.count);
    if(!in.read((char*)progress.data(),progress.size()*sizeof(progress[0]))||in.peek()!=EOF
        ||h.payload_hash!=smb_bank_hash(1469598103934665603ull,progress.data(),progress.size()*sizeof(progress[0])))
        throw std::runtime_error("corrupt practice checkpoint");
    for(const auto& p:progress) {
        if(p.stage<0||p.stage>=32||p.sampled<0||p.sampled>=SMB_PRACTICE_BANDS)
            throw std::runtime_error("invalid practice stage");
        for(int s=0;s<32;s++)if(p.level[s]>=SMB_PRACTICE_BANDS||p.attempts[s]>=8||p.wins[s]>p.attempts[s]||p.bad_windows[s]>=2)
            throw std::runtime_error("invalid practice mastery counters");
    }
    return progress;
}
// Curriculum teacher sidecar (practice_teacher = 1), written instead of `.practice`: header plus the raw
// group array. Like `.practice`, loading requires the same practice bank and checkpoint; groups match by key.
struct SmbTeacherSavedHeader {
    uint32_t magic,version,count,group_size;
    uint64_t bank_hash,checkpoint_hash,payload_hash;
};
static void smb_write_teacher(const char* checkpoint,uint64_t bank_hash,const std::vector<TeacherGroup>& groups) {
    if(groups.empty())return;
    SmbTeacherSavedHeader h={0x54434831,1,(uint32_t)groups.size(),sizeof(TeacherGroup),bank_hash,
        fpg_time_file_hash(checkpoint),smb_bank_hash(1469598103934665603ull,groups.data(),groups.size()*sizeof(groups[0]))};
    std::string path=std::string(checkpoint)+".teacher";
    std::ofstream out(path+".tmp",std::ios::binary);out.write((const char*)&h,sizeof(h));
    out.write((const char*)groups.data(),groups.size()*sizeof(groups[0]));out.close();
    if(!out)throw std::runtime_error("cannot save practice teacher");std::filesystem::rename(path+".tmp",path);
}
static std::vector<TeacherGroup> smb_read_teacher(const char* checkpoint,uint64_t bank_hash) {
    std::string path=std::string(checkpoint)+".teacher";if(!std::filesystem::exists(path))return {};
    std::ifstream in(path,std::ios::binary);SmbTeacherSavedHeader h={};
    if(!in.read((char*)&h,sizeof(h))||h.magic!=0x54434831||h.version!=1||!h.count||h.count>4096
        ||h.group_size!=sizeof(TeacherGroup)||h.bank_hash!=bank_hash||h.checkpoint_hash!=fpg_time_file_hash(checkpoint))
        throw std::runtime_error("invalid or mismatched practice teacher checkpoint");
    std::vector<TeacherGroup> groups(h.count);
    if(!in.read((char*)groups.data(),groups.size()*sizeof(groups[0]))||in.peek()!=EOF
        ||h.payload_hash!=smb_bank_hash(1469598103934665603ull,groups.data(),groups.size()*sizeof(groups[0])))
        throw std::runtime_error("corrupt practice teacher checkpoint");
    for(const auto& g:groups)
        if(!std::isfinite(g.fast)||!std::isfinite(g.slow)||!std::isfinite(g.deaths)||g.deaths<0||g.wins>g.attempts)
            throw std::runtime_error("invalid practice teacher statistics");
    return groups;
}
