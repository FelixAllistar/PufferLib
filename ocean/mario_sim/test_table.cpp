#include "table.h"
#include <iterator>
#include <cstdio>
#include <unistd.h>

int main() {
    const auto path=std::filesystem::temp_directory_path()/
        ("mario_sim_table_"+std::to_string(getpid())+".bin");
    try {
        SmbBank bank("build/mario_sim/fpg/curriculum/bank.bin");
        const char* original="build/mario_sim/fpg/curriculum_targets.bin";
        // A portable cache must load without either compiled simulator artifact.
        FpgTimeTable table(original,bank,"/nonexistent/cpu.a","/nonexistent/cuda.cubin");
        if(table.header.version!=2)throw std::runtime_error("fixture is not portable");
        std::ifstream in(original,std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)),{});
        auto reject=[&](size_t offset) {
            auto changed=bytes;changed.at(offset)^=1;
            {std::ofstream out(path,std::ios::binary);out.write(changed.data(),changed.size());}
            bool rejected=false;
            try {FpgTimeTable invalid(path.c_str(),bank,"/nonexistent/cpu.a","/nonexistent/cuda.cubin");}
            catch(const std::runtime_error&){rejected=true;}
            if(!rejected)throw std::runtime_error("corrupt or stale table was accepted");
        };
        reject(offsetof(FpgTimeHeader,bank_hash));
        reject(offsetof(FpgTimeHeader,cpu_hash));
        reject(offsetof(FpgTimeHeader,cuda_hash));
        reject(offsetof(FpgTimeHeader,version));
        reject(sizeof(FpgTimeHeader));
        std::filesystem::remove(path);
        printf("{\"portable_table\":true,\"stale_and_corrupt_rejections\":5,\"failures\":0}\n");
        return 0;
    }catch(const std::exception& e) {
        std::filesystem::remove(path);
        fprintf(stderr,"timing table test: %s\n",e.what());return 1;
    }
}
