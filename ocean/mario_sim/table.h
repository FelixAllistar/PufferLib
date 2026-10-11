#pragma once
#include "time_reward.h"
#include "bank_io.h"
#include <filesystem>

enum {FPG_TIME_MAGIC=0x46505431,FPG_TIME_VERSION=2};
struct FpgTimeHeader {
    uint32_t magic,version,entry_size,entries;
    uint32_t seed,variants,plans,horizon;
    uint64_t bank_hash,cpu_hash,cuda_hash,payload_hash;
};
static uint64_t fpg_time_file_hash(const char* path) {
    std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error(std::string("missing timing-cache dependency: ")+path);
    char bytes[65536];uint64_t hash=1469598103934665603ull;
    while(in.read(bytes,sizeof(bytes))||in.gcount())hash=smb_bank_hash(hash,bytes,(size_t)in.gcount());
    if(!in.eof())throw std::runtime_error("cannot fingerprint timing-cache dependency");
    return hash;
}
// Cache simulated frame counts against engine sources, not machine-specific
// archives/cubins. CPU builds do not need an optional CUDA simulator binary.
static uint64_t fpg_time_engine_hash(bool cuda) {
    const char* shared[]={"ocean/mario_sim/logic.h","ocean/mario_sim/runtime_registers.h",
        "ocean/mario_sim/runtime_poll.h","ocean/mario_sim/generate_logic.py",
        "ocean/mario_sim/generate_runtime.py","ocean/retro/batch/generate_blocks.py"};
    uint64_t hash=1469598103934665603ull;
    auto add=[&](const char* path){uint64_t value=fpg_time_file_hash(path);hash=smb_bank_hash(hash,&value,sizeof(value));};
    for(const char* path:shared)add(path);
    if(cuda) {
        add("ocean/mario_sim/runtime_kernel.cu");add("ocean/mario_sim/cuda_replay.cu");
        add("ocean/mario_sim/cuda_replay.h");
    }else add("ocean/mario_sim/logic_cpu.cpp");
    return hash;
}
struct FpgTimeTable {
    FpgTimeHeader header={};
    std::vector<FpgTimeEntry> entries;
    FpgTimeTable(const char* path,const SmbBank& bank,const char* cpu,const char* cuda) {
        std::ifstream in(path,std::ios::binary);
        if(!in.read((char*)&header,sizeof(header))||header.magic!=FPG_TIME_MAGIC
            ||(header.version!=1&&header.version!=FPG_TIME_VERSION)
            ||header.entry_size!=sizeof(FpgTimeEntry)||header.entries!=bank.entries.size()
            ||header.bank_hash!=bank.header.payload_hash||header.horizon<1||header.horizon>1000000
            ||header.variants>256||header.plans>256)
            throw std::runtime_error("missing, stale or invalid FPG timing table; rebuild it or set fpg.time_table=None outside the backward curriculum");
        entries.resize(header.entries);
        if(!in.read((char*)entries.data(),entries.size()*sizeof(FpgTimeEntry))||in.peek()!=EOF)
            throw std::runtime_error("truncated or trailing FPG timing table");
        if(smb_bank_hash(1469598103934665603ull,entries.data(),entries.size()*sizeof(FpgTimeEntry))!=header.payload_hash)
            throw std::runtime_error("FPG timing table checksum mismatch");
        uint64_t cpu_hash=header.version==1?fpg_time_file_hash(cpu):fpg_time_engine_hash(false);
        uint64_t cuda_hash=header.version==1?fpg_time_file_hash(cuda):fpg_time_engine_hash(true);
        if(header.cpu_hash!=cpu_hash||header.cuda_hash!=cuda_hash)
            throw std::runtime_error("FPG timing table engine changed; rebuild the table");
        for(const auto& e:entries)if(e.successes>e.trials||bool(e.best_frames)!=bool(e.successes)
                ||e.best_frames>1000000)throw std::runtime_error("invalid FPG timing estimate");
    }
};
