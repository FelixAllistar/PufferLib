#pragma once
#include "bank.h"
#include "task.h"
#include <fstream>
#include <stdexcept>
#include <vector>

struct SmbBank {
    SmbBankHeader header={};
    std::vector<uint8_t> worlds;
    std::vector<SmbBankEntry> entries;
    explicit SmbBank(const char* path) {
        std::ifstream in(path,std::ios::binary);
        if(!in.read((char*)&header,sizeof(header))||header.magic!=SMB_BANK_MAGIC||header.version!=SMB_BANK_VERSION
            ||header.scene_size!=sizeof(SmbScene)||header.entry_size!=sizeof(SmbBankEntry)
            ||header.rom_fingerprint!=0x6e01246e5d215cb3ull||header.scenes<1||header.scenes>65536
            ||header.worlds<1||header.worlds>256)throw std::runtime_error("invalid or missing Mario reset bank");
        worlds.resize((size_t)header.worlds*SMB_PRG);entries.resize(header.scenes);
        if(!in.read((char*)worlds.data(),worlds.size())||!in.read((char*)entries.data(),entries.size()*sizeof(SmbBankEntry))
            ||in.peek()!=EOF)throw std::runtime_error("truncated or trailing Mario reset bank");
        uint64_t hash=smb_bank_hash(1469598103934665603ull,worlds.data(),worlds.size());
        hash=smb_bank_hash(hash,entries.data(),entries.size()*sizeof(SmbBankEntry));
        if(hash!=header.payload_hash)throw std::runtime_error("Mario reset bank checksum mismatch");
        for(const auto& entry:entries) {
            if(entry.stage>=32||entry.scene.initial.fault||entry.scene.initial.timing.enabled!=1
                ||entry.scene.initial.ram[0x770]!=1||entry.scene.initial.ram[0x772]!=3)
                throw std::runtime_error("invalid Mario reset template");
        }
    }
    std::vector<uint32_t> select(const SmbTaskConfig& cfg) const {
        std::vector<uint32_t> result;
        for(unsigned i=0;i<entries.size();i++) {
            const auto& entry=entries[i];
            if(cfg.fixed_stage>=0&&(int)entry.stage!=cfg.fixed_stage)continue;
            if(cfg.task==SMB_TASK_FPG&&!(entry.flags&SMB_BANK_FLAGPOLE))continue;
            result.push_back(i);
        }
        if(result.empty())throw std::runtime_error("no reset templates match the requested task/stage");
        return result;
    }
};
