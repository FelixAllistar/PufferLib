// Behavior-cloning data from a TAS (2026-11-01). Replays the movie from power-on (see tas_replay.cpp), and for each
// requested level plays the TAS inputs frame by frame through an env (as tas_states.cpp does), writing the policy
// observation the env would show at every playable frame together with the TAS buttons as a policy action (A, B and
// the d-pad; Start/Select frames are skipped). Frameskip 1: one record per NES frame.
// Output: int32 header {records, OBS_SIZE}, then per record int32 {level id, frame in level, action} + float obs.
// Usage: tas_bc_data CHECKPOINT.ini MOVIE.fm2 LEVELS|all OUT.bin [OFFSET=-2]
#define PUFFERLIB_BUILD_MAIN
#include "retro.h"
#undef PUFFERLIB_BUILD_MAIN
#include <fstream>
#include <sstream>

int main(int argc,char** argv) {
    try {
        if(argc<5) throw std::runtime_error("usage: tas_bc_data CHECKPOINT.ini MOVIE.fm2 LEVELS|all OUT.bin [OFFSET]");
        int offset=argc>5?atoi(argv[5]):-2;
        std::vector<std::string> levels;
        if(!strcmp(argv[3],"all")) { for(int w=1;w<=8;w++) for(int l=1;l<=4;l++) levels.push_back(std::to_string(w)+"-"+std::to_string(l)); }
        else { std::stringstream ss(argv[3]); std::string t; while(std::getline(ss,t,',')) levels.push_back(t); }
        std::ifstream in(argv[2]); std::string line; std::vector<int> masks;
        const int bit[8]={128,64,32,16,8,4,2,1};
        while(std::getline(in,line)) {
            if(line.size()<12||line[0]!='|') continue;
            size_t p=line.find('|',1); if(p==std::string::npos) continue;
            int mask=0; for(int i=0;i<8;i++) if(line[p+1+i]!='.') mask|=bit[i];
            masks.push_back(mask);
        }
        if(offset<0) masks.erase(masks.begin(),masks.begin()+std::min((size_t)-offset,masks.size()));
        FILE* out=fopen(argv[4],"wb"); if(!out) throw std::runtime_error("cannot write output");
        int header[2]={0,OBS_SIZE}; fwrite(header,sizeof(int),2,out);
        std::vector<float> obs(OBS_SIZE); long records=0;
        for(auto& level:levels) {
            int lw,ll; if(sscanf(level.c_str(),"%d-%d",&lw,&ll)!=2) throw std::runtime_error("bad level "+level);
            Ini ini={}; puf_ini_load_env(&ini,"retro",0,nullptr);
            puf_ini_set(puf_ini_section(&ini,"env",0),"practice_replay","None");
            puf_ini_load_file(&ini,argv[1]);
            puf_ini_put(&ini,"env.archive_enabled","0");
            puf_ini_set(puf_ini_section(&ini,"env",0),"practice_replay","None");
            puf_ini_put(&ini,"env.spawn_levels",level.c_str());
            puf_ini_put(&ini,"env.frameskip","1");
            Dict vec={}; dict_set(&vec,"total_agents",1); dict_set(&vec,"num_buffers",1);
            int count,begin[1],sizes[1]; Env* e=my_vec_init(&count,begin,sizes,&vec,puf_ini_section(&ini,"env",0));
            float act=0,rew=0,term=0;
            e->spawn_pin=1; e->cur_spawn=0; e->agents[0]={obs.data(),&act,&rew,&term,nullptr,0}; puf_reset(e);
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
            const auto* m=e->emu->low_mem();
            e->rewarded_levels=0; e->pipe_phase=0; e->frontier_count=0; e->area_transition_key_count=0;
            e->last_area_key=retro_area_key(m);
            if(e->last_area_key!=0xffffffffu) e->area_transition_keys[e->area_transition_key_count++]=e->last_area_key;
            retro_track_progress(e); retro_sync_from_emu(e); e->x_pos_max=e->x_pos; e->tick=0;
            int frames=0,kept=0; int lid=retro_level_id(lw,ll);
            for(;f<masks.size();f++,frames++) {
                m=e->emu->low_mem();
                if(robs_world(m)!=lw||robs_stage(m)!=ll||m[0x770]!=1) break;
                bool playable=retro_playable_area_state(m);
                int mask=masks[f];
                if(playable&&!(mask&12)) {
                    retro_compute_obs_real(e,(obs_t*)obs.data());
                    int rec[3]={lid,frames,retro_mask_action((unsigned char)mask)};
                    fwrite(rec,sizeof(int),3,out); fwrite(obs.data(),sizeof(float),OBS_SIZE,out);
                    records++; kept++;
                }
                retro_frame(e,(unsigned char)mask,true); e->tick++;
                retro_track_progress(e); retro_record_area_transition(e,e->emu->low_mem()); retro_sync_from_emu(e);
                if(e->x_pos>e->x_pos_max) e->x_pos_max=e->x_pos;
            }
            printf("%s: %d frames, %d records\n",level.c_str(),frames,kept);
        }
        fseek(out,0,SEEK_SET); header[0]=(int)records; fwrite(header,sizeof(int),2,out); fclose(out);
        printf("wrote %ld records -> %s\n",records,argv[4]);
        return 0;
    } catch(const std::exception& ex){fprintf(stderr,"tas_bc_data: %s\n",ex.what());return 1;}
}
