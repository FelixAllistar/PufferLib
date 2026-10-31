// Maze practice starts (2026-10-30): keep only the archive cells in the maze castles (4-4, 7-4, 8-4) and write them
// as an archive for env.archive_init_path. A policy's own archive concentrates there in front of the loop checks it
// fails, so these cells are starts just before the choice. Optional: also merge the 4-4 bottom-corridor seed.
// Usage: maze_cells CHECKPOINT ARCHIVE_IN ARCHIVE_OUT [EXTRA_ARCHIVE]
// Build like seed_maze (CPU only): clang++ -O2 -std=c++17 -I../../src -Ines_emu ... maze_cells.cpp (see Makefile tools)
#define PUFFERLIB_BUILD_MAIN
#include "retro.h"
#undef PUFFERLIB_BUILD_MAIN

int main(int argc,char** argv) {
    try {
        if(argc<4) throw std::runtime_error("usage: maze_cells CHECKPOINT ARCHIVE_IN ARCHIVE_OUT [EXTRA_ARCHIVE]");
        std::string config=std::string(argv[1])+".ini";
        Ini ini={}; puf_ini_load_env(&ini,"retro",0,nullptr);
        puf_ini_set(puf_ini_section(&ini,"env",0),"practice_replay","None");
        puf_ini_load_file(&ini,config.c_str());
        puf_ini_set(puf_ini_section(&ini,"env",0),"practice_replay","None");
        puf_ini_put(&ini,"env.archive_enabled","1"); puf_ini_put(&ini,"env.archive_capacity_mb","1024");
        Dict vec={}; dict_set(&vec,"total_agents",1); dict_set(&vec,"num_buffers",1);
        int count,begin[1],sizes[1]; Env* e=my_vec_init(&count,begin,sizes,&vec,puf_ini_section(&ini,"env",0));
        (void)e;
        auto& b=retro_archive_bank();
        const int maze[]={retro_level_id(4,4),retro_level_id(7,4),retro_level_id(8,4)};
        auto keep=[&](const RetroArchiveCell& c){ for(int l:maze) if(c.level==l) return true; return false; };
        retro_archive_file(argv[2],false);
        std::vector<RetroArchiveCell> kept;
        for(auto& c:b.cells) if(keep(c)) kept.push_back(c);
        if(argc>4) { retro_archive_file(argv[4],false); for(auto& c:b.cells) if(keep(c)) kept.push_back(c); }
        b.cells=kept; b.index.clear();
        for(size_t i=0;i<b.cells.size();i++) b.index[b.cells[i].key]=i;
        int per[3]={0,0,0};
        for(auto& c:b.cells) for(int k=0;k<3;k++) per[k]+=c.level==maze[k];
        printf("kept %zu maze cells: 4-4 %d, 7-4 %d, 8-4 %d\n",b.cells.size(),per[0],per[1],per[2]);
        retro_archive_file(argv[3],true,0);
        return 0;
    } catch(const std::exception& ex){fprintf(stderr,"maze_cells: %s\n",ex.what());return 1;}
}
