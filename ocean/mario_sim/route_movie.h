#pragma once
// FCEUX movie (.fm2) input parser for the practice bank's reference route (build_practice_bank --route) and its
// test. Same parsing as ocean/retro/tas_replay.cpp: pad columns RLDUTSBA -> our mask (A=1 B=2 Select=4 Start=8
// Up=16 Down=32 Left=64 Right=128, the native engine's button layout). A negative offset drops that many movie
// frames for power-on alignment (the warpless TAS uses -2).
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

static std::vector<int> smb_movie_masks(const std::string& path,int offset=-2) {
    std::ifstream in(path);if(!in)throw std::runtime_error("cannot read movie "+path);
    std::string line;std::vector<int> masks;const int bit[8]={128,64,32,16,8,4,2,1};
    while(std::getline(in,line)) {
        if(line.size()<12||line[0]!='|')continue;
        size_t p=line.find('|',1);if(p==std::string::npos)continue;
        int mask=0;for(int i=0;i<8;i++)if(line[p+1+i]!='.')mask|=bit[i];
        masks.push_back(mask);
    }
    if(offset>0)throw std::runtime_error("route movie offset must be <= 0");
    masks.erase(masks.begin(),masks.begin()+std::min((size_t)-offset,masks.size()));
    return masks;
}
