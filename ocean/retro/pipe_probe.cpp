// Pipe probe (2026-11-01): from each state of one key point, try simple input scripts (hold Down; walk right N
// frames then hold Down) and report whether Mario warps (x jumps forward 300+ px or the area changes) or dies.
// Usage: pipe_probe ARCHIVE.gz POINT
#define PUFFERLIB_BUILD_MAIN
#include "retro.h"
#undef PUFFERLIB_BUILD_MAIN

int main(int argc,char** argv) {
    try {
        if(argc<3) throw std::runtime_error("usage: pipe_probe ARCHIVE.gz POINT");
        int point=atoi(argv[2]);
        Ini ini={}; puf_ini_load_env(&ini,"retro",0,nullptr);
        puf_ini_set(puf_ini_section(&ini,"env",0),"practice_replay","None");
        puf_ini_put(&ini,"env.archive_enabled","1"); puf_ini_put(&ini,"env.archive_practice","1");
        puf_ini_put(&ini,"env.archive_capacity_mb","1024"); puf_ini_put(&ini,"env.terminate_on_clear","1");
        puf_ini_set(puf_ini_section(&ini,"env",0),"archive_init_path",argv[1]);
        Dict vec={}; dict_set(&vec,"total_agents",1); dict_set(&vec,"num_buffers",1);
        int count,begin[1],sizes[1]; Env* e=my_vec_init(&count,begin,sizes,&vec,puf_ini_section(&ini,"env",0));
        for(auto& c:retro_archive_bank().cells) {
            if((int)((c.key>>52)&255)!=point) continue;
            printf("state x=%d:",c.state->start.x);
            int hits=0,tries=0;
            for(int jump:{0,10,16,24,32}) for(int walk:{0,4,8,12,16,24,32}) for(int run:{0,1}) {
                e->emu->load_state(c.state->start.state); retro_bind_pixels(e->emu);
                if(e->rom_blocks) e->emu->set_rom_blocks(true);
                const auto* m=e->emu->low_mem();
                int x0=robs_x(m), life=robs_life(m); unsigned a0=retro_area_key(m);
                const char* out="-";
                for(int f=0;f<jump+walk+90;f++) {
                    // jump right for `jump` frames, walk right for `walk`, then hold Down
                    unsigned char b=f<jump?(RETRO_BTN_RIGHT|RETRO_BTN_A|(run?RETRO_BTN_B:0))
                        :f<jump+walk?(RETRO_BTN_RIGHT|(run?RETRO_BTN_B:0)):RETRO_BTN_DOWN;
                    retro_frame(e,b,false);
                    if(robs_life(m)<life||robs_dead(m)) { out="died"; break; }
                    if(!retro_playable_area_state(m)) continue;
                    unsigned a=retro_area_key(m); int x=robs_x(m);
                    if(a!=a0) { out="AREA"; break; }
                    if(x>=x0+300) { out="WARP"; break; }
                }
                tries++;
                if(out[0]=='W'||out[0]=='A') { hits++; printf(" j%d w%d%s:%s",jump,walk,run?"b":"",out); }
            }
            printf("  -> %d/%d scripts enter\n",hits,tries);
        }
        return 0;
    } catch(const std::exception& ex){fprintf(stderr,"pipe_probe: %s\n",ex.what());return 1;}
}
