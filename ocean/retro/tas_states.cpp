// Practice starts along a TAS route (2026-11-01). Replays an FCEUX movie from power-on (tas_replay.cpp checks sync),
// and for each requested level hands the TAS state at the level's first playable frame to an env, then plays the TAS
// inputs frame by frame through the env's own tracking (progress frontiers, area keys), so every saved start has the
// same ledger a real run along that route would have. Every EVERY frames, when Mario stands on the ground alive, the
// moment becomes a start; key point id = BASE + running section number (one section per SECTION frames of a level).
// Writes an archive for env.archive_init_path and keypoint_eval. TAS_SMALL=1 saves each start as small Mario
// (PlayerSize $0754=1, PlayerStatus $0756=0; the TAS itself keeps playing as it was), since real attempts and every
// respawn start small while the warpless TAS is Fire Mario from 1-1. TAS_KEEP=4,7,11 keeps only those key points
// (ids are numbered as without the filter), for practice aimed at the spots a policy still fails.
// Usage: tas_states CHECKPOINT.ini MOVIE.fm2 LEVELS OUT.archive.gz [EVERY=8] [SECTION=240] [BASE=0] [OFFSET=-2]
#define PUFFERLIB_BUILD_MAIN
#include "retro.h"
#undef PUFFERLIB_BUILD_MAIN
#include <fstream>
#include <sstream>

static std::shared_ptr<RetroArchiveSnapshot> snapshot(Env* e) {
    auto s=std::make_shared<RetroArchiveSnapshot>();auto& st=s->start;auto& l=s->ledger;
    const auto* m=e->emu->low_mem();
    e->emu->save_state(&st.state);const auto& f=e->emu->frame();
    for(int y=0;y<240;y++)memcpy(st.pixels+y*256,f.pixels+y*f.pitch,256);
    memcpy(st.palette,f.palette,sizeof(st.palette));
    st.world=e->world;st.stage=e->stage;st.area=e->area;st.data=m[0xe7]+256*m[0xe8];st.x=e->x_pos;st.rta_offset_frames=0;
    memcpy(l.frontiers,e->frontiers,sizeof(l.frontiers));l.frontier_count=e->frontier_count;
    memcpy(l.areas,e->area_transition_keys,sizeof(l.areas));l.area_count=e->area_transition_key_count;
    l.rewarded=e->rewarded_levels;l.last_area=e->last_area_key;l.pipe_phase=e->pipe_phase;
    return s;
}

int main(int argc,char** argv) {
    try {
        if(argc<5) throw std::runtime_error("usage: tas_states CHECKPOINT.ini MOVIE.fm2 LEVELS OUT.archive.gz [EVERY] [SECTION] [BASE] [OFFSET]");
        int every=argc>5?atoi(argv[5]):8, section=argc>6?atoi(argv[6]):240, base=argc>7?atoi(argv[7]):0;
        int offset=argc>8?atoi(argv[8]):-2;
        std::vector<std::string> levels; { std::stringstream ss(argv[3]); std::string t; while(std::getline(ss,t,',')) levels.push_back(t); }
        // movie inputs (FM2 pad columns RLDUTSBA -> our mask)
        std::ifstream in(argv[2]); std::string line; std::vector<int> masks;
        const int bit[8]={128,64,32,16,8,4,2,1};
        while(std::getline(in,line)) {
            if(line.size()<12||line[0]!='|') continue;
            size_t p=line.find('|',1); if(p==std::string::npos) continue;
            int mask=0; for(int i=0;i<8;i++) if(line[p+1+i]!='.') mask|=bit[i];
            masks.push_back(mask);
        }
        if(offset<0) masks.erase(masks.begin(),masks.begin()+std::min((size_t)-offset,masks.size()));
        std::vector<std::shared_ptr<RetroArchiveSnapshot>> kept; std::vector<int> kept_id;
        int next_id=base;
        for(auto& level:levels) {
            int lw,ll; if(sscanf(level.c_str(),"%d-%d",&lw,&ll)!=2) throw std::runtime_error("bad level "+level);
            Ini ini={}; puf_ini_load_env(&ini,"retro",0,nullptr);
            puf_ini_set(puf_ini_section(&ini,"env",0),"practice_replay","None");
            puf_ini_load_file(&ini,argv[1]);
            puf_ini_put(&ini,"env.archive_enabled","0");
            puf_ini_set(puf_ini_section(&ini,"env",0),"practice_replay","None");
            puf_ini_put(&ini,"env.spawn_levels",level.c_str());
            puf_ini_put(&ini,"env.terminate_on_clear","1");
            Dict vec={}; dict_set(&vec,"total_agents",1); dict_set(&vec,"num_buffers",1);
            int count,begin[1],sizes[1]; Env* e=my_vec_init(&count,begin,sizes,&vec,puf_ini_section(&ini,"env",0));
            std::vector<float> obs(OBS_SIZE); float act=0,rew=0,term=0;
            e->spawn_pin=1; e->cur_spawn=0; e->agents[0]={obs.data(),&act,&rew,&term,nullptr,0}; puf_reset(e);
            // run the movie on a standalone core up to the level's first playable frame
            RetroRom& rom=retro_rom();
            Nes_Emu tas; retro_check(tas.set_cart(&rom.cart)); retro_bind_pixels(&tas);
            for(int i=0;i<offset;i++) retro_check(tas.emulate_frame(0,0));
            size_t f=0;
            for(;f<masks.size();f++) {
                retro_check(tas.emulate_frame(masks[f],0));
                const auto* m=tas.low_mem();
                if(robs_world(m)==lw&&robs_stage(m)==ll&&retro_playable_area_state(m)) { f++; break; }
            }
            if(f>=masks.size()) throw std::runtime_error("movie never reaches "+level);
            Nes_State* st=new Nes_State(); tas.save_state(st);
            e->emu->load_state(*st); delete st; retro_bind_pixels(e->emu);
            if(e->rom_blocks&&!e->emu->set_rom_blocks(true)) throw std::runtime_error("rom blocks");
            // fresh per-level ledger, as at a reset
            const auto* m=e->emu->low_mem();
            e->rewarded_levels=0; e->pipe_phase=0; e->frontier_count=0; e->area_transition_key_count=0;
            e->last_area_key=retro_area_key(m);
            if(e->last_area_key!=0xffffffffu) e->area_transition_keys[e->area_transition_key_count++]=e->last_area_key;
            retro_track_progress(e); retro_sync_from_emu(e);
            int frames=0,saved=0,first_id=next_id,last_section=-1; int life=robs_life(m);
            for(;f<masks.size();f++,frames++) {
                retro_frame(e,(unsigned char)masks[f],true);
                retro_track_progress(e); retro_record_area_transition(e,e->emu->low_mem()); retro_sync_from_emu(e);
                m=e->emu->low_mem();
                if(robs_world(m)!=lw||robs_stage(m)!=ll||m[0x770]!=1) break;
                if(robs_life(m)<life||robs_dead(m)) throw std::runtime_error("TAS dies in "+level+" (desync?)");
                if(frames%every||!retro_playable_area_state(m)||m[0x1d]!=0||m[0xb5]!=1) continue;
                int sec=frames/section;
                if(sec!=last_section) { if(last_section>=0) next_id++; last_section=sec; }
                if(next_id>255) throw std::runtime_error("more than 256 key points");
                if(getenv("TAS_KEEP")) {
                    std::string keep=std::string(",")+getenv("TAS_KEEP")+",";
                    if(keep.find(","+std::to_string(next_id)+",")==std::string::npos) continue;
                }
                if(getenv("TAS_SMALL")&&atoi(getenv("TAS_SMALL"))) {
                    Nes_State* keep=new Nes_State(); e->emu->save_state(keep);
                    unsigned char* w=e->emu->low_mem(); w[0x754]=1; w[0x756]=0;
                    kept.push_back(snapshot(e));
                    e->emu->load_state(*keep); delete keep; retro_bind_pixels(e->emu);
                    if(e->rom_blocks&&!e->emu->set_rom_blocks(true)) throw std::runtime_error("rom blocks");
                } else kept.push_back(snapshot(e));
                kept_id.push_back(next_id); saved++;
            }
            printf("%s: %d TAS frames, %d starts, key points %d-%d\n",level.c_str(),frames,saved,first_id,next_id);
            next_id++;
        }
        auto& b=retro_archive_bank();
        b.capacity=kept.size()+1; b.cells.clear(); b.index.clear();
        for(size_t i=0;i<kept.size();i++) {
            auto& c=kept[i]; RetroArchiveCell cell; cell.state=c; cell.level=retro_level_id(c->start.world,c->start.stage);
            cell.key=(uint64_t(kept_id[i])<<52)|(uint64_t(c->start.x)<<16)|(uint64_t)i;
            cell.depth=retro_archive_depth(*c,cell.level);
            b.index[cell.key]=b.cells.size(); b.cells.push_back(cell);
        }
        retro_archive_file(argv[4],true,0);
        printf("wrote %zu starts -> %s\n",kept.size(),argv[4]);
        return 0;
    } catch(const std::exception& ex){fprintf(stderr,"tas_states: %s\n",ex.what());return 1;}
}
