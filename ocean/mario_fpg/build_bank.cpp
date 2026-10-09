#include "fpg_task.h"
#include "search.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <filesystem>

int main(int argc,char** argv) {
    try {
        if(argc!=3) throw std::runtime_error("usage: build_bank OUTPUT CASES_PER_SPLIT");
        int target=std::stoi(argv[2]);if(target<1||target>256) throw std::runtime_error("cases per split must be 1..256");
        if(std::filesystem::exists(argv[1])) throw std::runtime_error("refusing to overwrite bank");
        std::vector<FpgCase> cases;int attempts=0,counts[3]={};
        while((int)cases.size()<target*3 && attempts<target*30) {
            int split=(int)cases.size()%3;uint32_t seed=fpg_hash(79031u+(uint32_t)attempts++);
            uint32_t rng=seed;FpgCase sample={};
            sample.course={seed,split,32+(int)(fpg_rand(&rng)%9),6+(int)(fpg_rand(&rng)%3),
                6+(int)(fpg_rand(&rng)%5),2+(int)(fpg_rand(&rng)%3)};
            FpgWorld world;fpg_generate(&sample.course,&world);FpgBody b=fpg_initial(&sample.course);
            auto path=fpg_search(b,world,12000,FPG_PATH_MAX);
            if(path.empty()) {fprintf(stderr,"seed %u unsolved within budget; excluded\n",seed);continue;}
            sample.length=(int)path.size();
            for(int t=0;t<sample.length;t++) {sample.frames[t]={b,path[t]};fpg_physics(&b,&world,fpg_buttons(path[t]));}
            if(fpg_outcome(&b)!=FPG_SUCCESS) throw std::runtime_error("search replay failure");
            cases.push_back(sample);counts[split]++;
            printf("case=%zu split=%d seed=%u frames=%d height=%d gap=%d\n",cases.size(),split,seed,sample.length,sample.course.height,sample.course.gap);fflush(stdout);
        }
        if((int)cases.size()!=target*3) throw std::runtime_error("insufficient solvable cases within search budget");
        FILE* f=fopen(argv[1],"wb");if(!f) throw std::runtime_error("cannot create bank");
        uint32_t header[]={0x46504731,FPG_VERSION,(uint32_t)cases.size(),(uint32_t)sizeof(FpgCase)};
        if(fwrite(header,sizeof(header),1,f)!=1 || fwrite(cases.data(),sizeof(FpgCase),cases.size(),f)!=cases.size() || fclose(f)) throw std::runtime_error("bank write failed");
        printf("{\"format\":\"fpg_bank_v1\",\"original_synthetic_geometry\":true,\"rom_used\":false,\"attempted\":%d,\"train\":%d,\"validation\":%d,\"test\":%d}\n",attempts,counts[0],counts[1],counts[2]);
    } catch(const std::exception& e) {fprintf(stderr,"build_bank: %s\n",e.what());return 1;}
}
