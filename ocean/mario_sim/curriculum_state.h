#pragma once
#include "curriculum.h"
#include <filesystem>
#include <fstream>
#include <cmath>

struct FptCurriculumSavedHeader {
    uint32_t magic,version,count,progress_size;
    uint64_t bank_hash,checkpoint_hash,payload_hash;
    FptCurriculumConfig config;
};
struct FptCurriculumSaved {
    FptCurriculumSavedHeader header={};
    std::vector<FptCurriculumProgress> progress;
};
static uint64_t fpt_progress_hash(const FptCurriculumSaved& saved) {
    uint64_t hash=smb_bank_hash(1469598103934665603ull,&saved.header.config,sizeof(saved.header.config));
    return smb_bank_hash(hash,saved.progress.data(),saved.progress.size()*sizeof(FptCurriculumProgress));
}
static FptCurriculumSaved fpt_read_progress(const char* checkpoint) {
    FptCurriculumSaved saved;std::string path=std::string(checkpoint)+".curriculum";
    if(!std::filesystem::exists(path))return saved;
    std::ifstream in(path,std::ios::binary);auto& h=saved.header;
    if(!in.read((char*)&h,sizeof(h))||h.magic!=0x46504331||h.version!=1||h.progress_size!=sizeof(FptCurriculumProgress)
        ||!h.count||h.count>1000000||h.checkpoint_hash!=fpg_time_file_hash(checkpoint))
        throw std::runtime_error("invalid or mismatched FPG curriculum checkpoint state");
    saved.progress.resize(h.count);
    if(!in.read((char*)saved.progress.data(),saved.progress.size()*sizeof(FptCurriculumProgress))||in.peek()!=EOF
        ||fpt_progress_hash(saved)!=h.payload_hash)throw std::runtime_error("corrupt FPG curriculum checkpoint state");
    if(!h.config.enabled||h.config.window<1||h.config.window>4096||h.config.confirmations<1||h.config.confirmations>128
        ||!std::isfinite(h.config.threshold)||h.config.threshold<=0||h.config.threshold>1)
        throw std::runtime_error("invalid saved curriculum controls");
    for(int i=0;i<FPT_CURRICULUM_LEVELS;i++)if(h.config.depths[i]!=fpt_curriculum_depths[i])
        throw std::runtime_error("checkpoint curriculum schedule changed");
    for(const auto& p:saved.progress)if(p.initialized!=1||p.level<0||p.level>=FPT_CURRICULUM_LEVELS
        ||p.sampled_level<0||p.sampled_level>p.level||p.attempts<0||p.attempts>=h.config.window
        ||p.wins<0||p.wins>p.attempts||p.streak<0||p.streak>h.config.confirmations
        ||!std::isfinite(p.last_rate)||p.last_rate<0||p.last_rate>1)
        throw std::runtime_error("invalid saved curriculum progress");
    return saved;
}
static int fpt_median_level(const std::vector<FptCurriculumProgress>& progress) {
    int counts[FPT_CURRICULUM_LEVELS]={};for(const auto& p:progress)counts[p.level]++;
    size_t cumulative=0;for(int level=0;level<FPT_CURRICULUM_LEVELS;level++) {
        cumulative+=counts[level];if(cumulative>=(progress.size()+1)/2)return level;
    }
    return 0;
}
static void fpt_write_progress(const char* checkpoint,uint64_t bank_hash,const FptCurriculumConfig& config,
        const std::vector<FptCurriculumProgress>& progress) {
    if(!config.enabled||progress.empty())return;
    FptCurriculumSaved saved;saved.progress=progress;auto& h=saved.header;
    h.magic=0x46504331;h.version=1;h.count=progress.size();h.progress_size=sizeof(FptCurriculumProgress);
    h.bank_hash=bank_hash;h.checkpoint_hash=fpg_time_file_hash(checkpoint);h.config=config;h.payload_hash=fpt_progress_hash(saved);
    std::string path=std::string(checkpoint)+".curriculum",temporary=path+".tmp";
    std::ofstream out(temporary,std::ios::binary);out.write((char*)&h,sizeof(h));out.write((char*)progress.data(),progress.size()*sizeof(FptCurriculumProgress));
    out.close();if(!out)throw std::runtime_error("cannot write curriculum checkpoint state");std::filesystem::rename(temporary,path);
    int counts[FPT_CURRICULUM_LEVELS]={},lo=FPT_CURRICULUM_LEVELS-1,hi=0;double total=0;uint64_t promotions=0;
    for(const auto& p:progress){counts[p.level]++;lo=std::min(lo,p.level);hi=std::max(hi,p.level);total+=config.depths[p.level];promotions+=p.promotions;}
    std::ofstream report(path+".json");
    report<<"{\"schema\":1,\"agents\":"<<progress.size()<<",\"mean_reference_frames\":"<<total/progress.size()
        <<",\"min_reference_frames\":"<<config.depths[lo]<<",\"max_reference_frames\":"<<config.depths[hi]
        <<",\"median_reference_frames\":"<<config.depths[fpt_median_level(progress)]<<",\"promotions\":"<<promotions
        <<",\"window\":"<<config.window<<",\"confirmations\":"<<config.confirmations<<",\"threshold\":"<<config.threshold
        <<",\"replay_fraction\":"<<config.replay_fraction<<",\"levels\":[";
    for(int i=0;i<FPT_CURRICULUM_LEVELS;i++){if(i)report<<',';report<<"{\"reference_frames\":"<<config.depths[i]<<",\"agents\":"<<counts[i]<<"}";}
    report<<"]}\n";report.close();if(!report)throw std::runtime_error("cannot write curriculum checkpoint report");
}
static void fpt_check_resume(const FptCurriculumSaved& saved,uint64_t bank_hash) {
    if(!saved.progress.empty()&&saved.header.bank_hash!=bank_hash)
        throw std::runtime_error("saved curriculum belongs to a different reset bank; use curriculum_resume=0 for a new curriculum");
}
static FptCurriculumProgress fpt_restore_progress(const FptCurriculumSaved& saved,size_t index,const FptCurriculumConfig& config) {
    auto p=saved.progress[index%saved.progress.size()];const auto& previous=saved.header.config;
    if(previous.window!=config.window||previous.confirmations!=config.confirmations||previous.threshold!=config.threshold) {
        p.attempts=p.wins=p.streak=0;p.last_rate=0;
    }
    return p;
}
