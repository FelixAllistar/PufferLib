#include "bank_io.h"
#include <filesystem>
#include <cstdio>

int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::runtime_error("usage: test_bank BANK FIXTURE_DIRECTORY");
        SmbBank valid(argv[1]);
        std::ifstream input(argv[1],std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
        std::filesystem::create_directories(argv[2]);
        unsigned rejected=0;
        for(int kind=0;kind<6;kind++) {
            auto fixture=bytes;
            if(kind==0)fixture[0]^=1;
            if(kind==1)fixture.resize(sizeof(SmbBankHeader)-1);
            if(kind==2)fixture.pop_back();
            if(kind==3)fixture.push_back(0);
            if(kind==4)fixture.back()^=1;
            if(kind==5) {
                SmbBankHeader h;memcpy(&h,fixture.data(),sizeof(h));h.worlds=257;
                memcpy(fixture.data(),&h,sizeof(h));
            }
            auto path=std::filesystem::path(argv[2])/(std::to_string(kind)+".bin");
            {std::ofstream out(path,std::ios::binary);out.write(fixture.data(),fixture.size());}
            bool caught=false;try {SmbBank bad(path.c_str());}catch(const std::runtime_error&){caught=true;}
            if(!caught)throw std::runtime_error("corrupt bank accepted");rejected++;
        }
        SmbTaskConfig cfg={};cfg.task=SMB_TASK_FPG;cfg.fixed_stage=-1;
        auto selected=valid.select(cfg);
        for(unsigned i:selected)if(!(valid.entries[i].flags&SMB_BANK_FLAGPOLE))throw std::runtime_error("wrong task selection");
        cfg.fixed_stage=32;
        bool caught=false;try {valid.select(cfg);}catch(const std::runtime_error&){caught=true;}
        if(!caught)throw std::runtime_error("empty bank selection accepted");
        printf("{\"invalid_banks_rejected\":%u,\"empty_selection_rejected\":true,\"flagpole_templates\":%zu,\"failures\":0}\n",rejected,selected.size());
        return 0;
    } catch(const std::exception& e){fprintf(stderr,"bank test: %s\n",e.what());return 1;}
}
