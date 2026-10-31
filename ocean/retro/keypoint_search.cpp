// Route search past a maze wall (2026-10-31): from one key point's states in a keypoint_states archive, random
// exploration on the emulator (no policy) until Mario gets past TARGET_X in the start area without a loop-back or a
// lost life, or leaves the start area through a pipe. A Go-Explore-style cell map (area, x/8, y/16) keeps the first
// state reaching each cell, its parent and the button chunk from the parent; rollouts start from cells picked with
// weight (1 + progress / 256) / sqrt(1 + picks). In 7-4 a state also needs clean maze counters ($06D9 == $06DA).
// The found route becomes two new key points appended to the archive: states along the route (on the ground, safe
// for 60 frames, at least 32 px apart) and states just past the wall (the next section's start).
// Usage: keypoint_search KEYPOINTS_IN.archive.gz POINT TARGET_X OUT.archive.gz [MINUTES=20] [SEED]
#define PUFFERLIB_BUILD_MAIN
#include "retro.h"
#undef PUFFERLIB_BUILD_MAIN
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <omp.h>

struct Cell {
    std::unique_ptr<Nes_State> state;
    int parent=-1, buttons=0, frames=0, x=0, y=0, picks=0;
    unsigned area=0;
};

static const unsigned char CHUNK_BUTTONS[]={
    RETRO_BTN_RIGHT, RETRO_BTN_RIGHT|RETRO_BTN_A, RETRO_BTN_RIGHT|RETRO_BTN_B, RETRO_BTN_RIGHT|RETRO_BTN_A|RETRO_BTN_B,
    RETRO_BTN_LEFT, RETRO_BTN_LEFT|RETRO_BTN_A, RETRO_BTN_A, 0, RETRO_BTN_DOWN, RETRO_BTN_RIGHT|RETRO_BTN_B,
    RETRO_BTN_RIGHT|RETRO_BTN_A|RETRO_BTN_B};

static std::shared_ptr<RetroArchiveSnapshot> snapshot(Env* e) {
    auto s=std::make_shared<RetroArchiveSnapshot>();auto& st=s->start;
    const auto* m=e->emu->low_mem();
    e->emu->save_state(&st.state);const auto& f=e->emu->frame();
    for(int y=0;y<240;y++)memcpy(st.pixels+y*256,f.pixels+y*f.pitch,256);
    memcpy(st.palette,f.palette,sizeof(st.palette));
    st.world=robs_world(m);st.stage=robs_stage(m);st.area=robs_area(m);st.data=m[0xe7]+256*m[0xe8];st.x=robs_x(m);
    st.rta_offset_frames=0;
    s->ledger={};  // empty progress ledger: the first frame after a reset sets the baseline
    return s;
}
static void load(Env* e,const Nes_State& s) {
    e->emu->load_state(s); retro_bind_pixels(e->emu);
    if(e->rom_blocks&&!e->emu->set_rom_blocks(true)) throw std::runtime_error("rom blocks");
}
static bool lost(const unsigned char* m,int life) {
    return robs_dead(m)||robs_dying(m)||robs_life(m)<life||m[0xb5]>1;
}
static bool safe(Env* e) {
    std::unique_ptr<Nes_State> keep(new Nes_State); e->emu->save_state(keep.get());
    int life=robs_life(e->emu->low_mem()); bool ok=true;
    for(int f=0;f<60&&ok;f++) { retro_frame(e,0,false); ok=!lost(e->emu->low_mem(),life); }
    load(e,*keep);
    return ok;
}

int main(int argc,char** argv) {
    try {
        if(argc<5) throw std::runtime_error("usage: keypoint_search IN.archive.gz POINT TARGET_X OUT.archive.gz [MINUTES] [SEED]");
        int point=atoi(argv[2]), target=atoi(argv[3]); double minutes=argc>5?atof(argv[5]):20;
        unsigned seed=argc>6?strtoul(argv[6],nullptr,10):20261031;
        Ini ini={}; puf_ini_load_env(&ini,"retro",0,nullptr);
        puf_ini_set(puf_ini_section(&ini,"env",0),"practice_replay","None");
        puf_ini_put(&ini,"env.archive_enabled","1"); puf_ini_put(&ini,"env.archive_practice","1");
        puf_ini_put(&ini,"env.archive_capacity_mb","1024"); puf_ini_put(&ini,"env.terminate_on_clear","1");
        puf_ini_set(puf_ini_section(&ini,"env",0),"archive_init_path",argv[1]);
        puf_ini_put(&ini,"env.spawn_levels","4-4,7-4,8-4,5-3,8-1,8-3"); puf_ini_put(&ini,"env.full_render","0");
        int threads=omp_get_max_threads();
        Dict vec={}; dict_set(&vec,"total_agents",threads); dict_set(&vec,"num_buffers",1);
        int count,begin[1],sizes[1]; Env* envs=my_vec_init(&count,begin,sizes,&vec,puf_ini_section(&ini,"env",0));
        auto& b=retro_archive_bank();
        int next_point=0; std::vector<size_t> roots;
        for(size_t i=0;i<b.cells.size();i++) {
            int p=(int)((b.cells[i].key>>52)&255); next_point=std::max(next_point,p+1);
            if(p==point) roots.push_back(i);
        }
        if(roots.empty()) throw std::runtime_error("no states for that key point");
        std::vector<Cell> cells; std::map<uint64_t,int> index; std::mutex mutex;
        unsigned start_area=0; int level=-1, world=0;
        for(size_t r:roots) {
            Env* e=&envs[0]; load(e,b.cells[r].state->start.state); const auto* m=e->emu->low_mem();
            Cell c; c.state.reset(new Nes_State); e->emu->save_state(c.state.get());
            c.x=robs_x(m); c.y=m[0xce]; c.area=retro_area_key(m);
            start_area=c.area; level=b.cells[r].level; world=robs_world(m);
            index[((uint64_t)c.area<<32)|((c.x/8)<<8)|(c.y/16)]=(int)cells.size(); cells.push_back(std::move(c));
        }
        int x0=cells[0].x; std::atomic<int> found{-1}; std::atomic<long> rollouts{0};
        auto deadline=std::chrono::steady_clock::now()+std::chrono::duration<double>(minutes*60);
        #pragma omp parallel
        {
            Env* e=&envs[omp_get_thread_num()];
            unsigned rng=seed^((unsigned)(omp_get_thread_num()+1)*2654435761u);
            while(found<0&&std::chrono::steady_clock::now()<deadline) {
                int from;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    double total=0; std::vector<double> w(cells.size());
                    for(size_t i=0;i<cells.size();i++) total+=w[i]=(1+std::max(0,cells[i].x-x0)/256.0)/std::sqrt(1.0+cells[i].picks);
                    double pick=(retro_random(&rng)+.5)/4294967296.0*total; from=0;
                    for(size_t i=0;i<cells.size();i++) { from=(int)i; if((pick-=w[i])<=0) break; }
                    cells[from].picks++;
                    load(e,*cells[from].state);
                }
                rollouts++;
                int parent=from, life=robs_life(e->emu->low_mem());
                for(int chunk=0;chunk<48&&found<0;chunk++) {
                    const auto* m=e->emu->low_mem(); int before_x=robs_x(m); unsigned before_area=retro_area_key(m);
                    int buttons=CHUNK_BUTTONS[retro_random(&rng)%sizeof(CHUNK_BUTTONS)];
                    int frames=4+retro_random(&rng)%17;
                    for(int f=0;f<frames;f++) retro_frame(e,(unsigned char)buttons,f+1==frames);
                    m=e->emu->low_mem();
                    if(lost(m,life)) break;
                    unsigned area=retro_area_key(m); int x=robs_x(m);
                    if(area==before_area&&before_x-x>=900) break;  // maze loop-back
                    if(!retro_playable_area_state(m)) continue;
                    if(world==7&&m[0x6d9]!=m[0x6da]) break;  // a failed check in the group: doomed
                    uint64_t key=((uint64_t)area<<32)|((x/8)<<8)|(m[0xce]/16);
                    std::lock_guard<std::mutex> lock(mutex);
                    auto it=index.find(key);
                    if(it!=index.end()) { parent=it->second; continue; }
                    Cell c; c.state.reset(new Nes_State); e->emu->save_state(c.state.get());
                    c.parent=parent; c.buttons=buttons; c.frames=frames; c.x=x; c.y=m[0xce]; c.area=area;
                    parent=(int)cells.size(); index[key]=parent; cells.push_back(std::move(c));
                    bool past=(area==start_area&&x>=target)||(area!=start_area&&area!=0xffffffffu);
                    if(past&&found<0) found=parent;
                }
            }
        }
        printf("%zu cells, %ld rollouts, furthest x %d\n",cells.size(),rollouts.load(),
            std::max_element(cells.begin(),cells.end(),[](const Cell& a,const Cell& c){return a.x<c.x;})->x);
        if(found<0) { printf("no route past x %d found\n",target); return 2; }
        // Route = chain from the found cell back to a root; keep safe on-ground states 32+ px apart.
        std::vector<int> chain; for(int c=found;c>=0;c=cells[c].parent) chain.push_back(c);
        std::reverse(chain.begin(),chain.end());
        Env* e=&envs[0]; int last_x=-1000, route_n=0;
        std::vector<RetroArchiveCell> add;
        // One drawn no-input frame first, so the saved first image matches the state.
        auto keep=[&](int p){
            retro_frame(e,0,true);
            const auto* m=e->emu->low_mem();
            if(m[0x1d]!=0||!retro_playable_area_state(m)||!safe(e)) return false;
            RetroArchiveCell c; c.state=snapshot(e); c.level=level;
            c.key=(uint64_t(p)<<52)|(uint64_t(robs_x(m))<<16)|(uint64_t)(b.cells.size()+add.size());
            add.push_back(c); return true;
        };
        for(int c:chain) {
            load(e,*cells[c].state);
            if(cells[c].area==start_area&&cells[c].x-last_x>=32&&cells[c].x<target&&keep(next_point)) { last_x=cells[c].x; route_n++; }
        }
        printf("route: %zu cells, %d states as key point %d\n",chain.size(),route_n,next_point);
        // Past the wall: from the found state, keep exploring forward briefly and keep the first safe states.
        int past_n=0;
        load(e,*cells[found].state);
        int past_life=robs_life(e->emu->low_mem());
        for(int f=0;f<600&&past_n<8;f++) {
            retro_frame(e,f%40<30?RETRO_BTN_RIGHT:0,true);
            if(lost(e->emu->low_mem(),past_life)) break;
            if(f%20==0&&keep(next_point+1)) past_n++;
        }
        printf("past the wall: %d states as key point %d\n",past_n,next_point+1);
        b.capacity=b.cells.size()+add.size()+1;
        for(auto& c:add) { b.index[c.key]=b.cells.size(); b.cells.push_back(c); }
        retro_archive_file(argv[4],true,0);
        printf("wrote %zu states -> %s\n",b.cells.size(),argv[4]);
        return 0;
    } catch(const std::exception& ex){fprintf(stderr,"keypoint_search: %s\n",ex.what());return 1;}
}
