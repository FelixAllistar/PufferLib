// Diagnostic: search random button sequences from archived 4-4 cells near the
// maze split for a state standing in the bottom corridor (y=176, on the ground,
// x >= 1650), and write a copy of the archive with up to three such cells added.
// 4-4's second loop check requires that corridor (ROM LoopCmdYPosition 0xB0).
#define PUFFERLIB_BUILD_MAIN
#include "retro.h"
#undef PUFFERLIB_BUILD_MAIN
#include <map>

int main(int argc,char** argv) {
    try {
        if(argc<4) throw std::runtime_error("usage: seed_maze CHECKPOINT ARCHIVE_IN ARCHIVE_OUT [TRIES] [SEED]");
        int tries=argc>4?atoi(argv[4]):20000; unsigned rng=argc>5?strtoul(argv[5],nullptr,10):20261021;
        std::string config=std::string(argv[1])+".ini";
        Ini ini={}; puf_ini_load_env(&ini,"retro",0,nullptr);
        puf_ini_set(puf_ini_section(&ini,"env",0),"practice_replay","None");
        puf_ini_load_file(&ini,config.c_str());
        puf_ini_set(puf_ini_section(&ini,"env",0),"practice_replay","None");
        puf_ini_put(&ini,"env.archive_enabled","1"); puf_ini_put(&ini,"env.spawn_levels","4-4");
        puf_ini_put(&ini,"env.backend","quicknes"); puf_ini_put(&ini,"env.full_render","1");
        Dict vec={}; dict_set(&vec,"total_agents",1); dict_set(&vec,"num_buffers",1);
        int count,begin[1],sizes[1]; Env* e=my_vec_init(&count,begin,sizes,&vec,puf_ini_section(&ini,"env",0));
        std::vector<float> obs(OBS_SIZE); float action=0,reward=0,terminal=0;
        e->agents[0]={obs.data(),&action,&reward,&terminal,nullptr,0}; puf_reset(e);
        retro_archive_file(argv[2],false);
        auto& b=retro_archive_bank();
        if(argc>4&&!strcmp(argv[4],"verify")) {
            // From each bottom-corridor 4-4 cell, run right (jump when stuck) and
            // report whether Mario passes loop 2 (x > 2100) without a loop-back.
            for(auto& c:b.cells) if(c.level==15&&((c.key>>42)&15)==5&&c.state->start.x>=1650) {
                e->emu->load_state(c.state->start.state); int furthest=c.state->start.x,last=furthest,stuck=0; bool looped=false;
                for(int d=0;d<400&&furthest<=2100;d++) {
                    int a=2|32|(stuck>4?1:0);
                    for(int f=0;f<4;f++) retro_frame(e,retro_action_mask(a),false);
                    const unsigned char* m=e->emu->low_mem(); int x=robs_x(m);
                    if(robs_dying(m)||robs_dead(m)) break;
                    if(last-x>=900) looped=true;
                    stuck=x<=last?stuck+1:0; last=x; furthest=std::max(furthest,x);
                }
                printf("cell x=%d: furthest=%d looped=%d passed=%d\n",c.state->start.x,furthest,looped,furthest>2100&&!looped);
            }
            return 0;
        }
        std::vector<std::shared_ptr<RetroArchiveSnapshot>> sources;
        for(auto& c:b.cells) if(c.level==15&&c.state->start.x>=1450&&c.state->start.x<=1650) sources.push_back(c.state);
        if(sources.empty()) throw std::runtime_error("no 4-4 cells between x 1450 and 1650");
        int found=0,furthest_low=0; std::map<int,long> standing; // y -> decisions standing at x >= 1600
        for(int t=0;t<tries&&found<3;t++) {
            auto source=sources[retro_random(&rng)%sources.size()];
            e->archive->state=source; retro_archive_restore(e);
            e->emu->load_state(source->start.state); retro_sync_from_emu(e);
            int held=0,a=0; bool low=false;
            for(int d=0;d<300;d++) {
                // Random buttons until Mario stands on the bottom floor past the
                // lava, then run right (B+Right, no jump) along that corridor.
                if(low) a=2|32; else if(!held){a=retro_random(&rng)%64;held=1+retro_random(&rng)%8;} if(held) held--;
                for(int f=0;f<4;f++) retro_frame(e,retro_action_mask(a),f==3);
                const unsigned char* m=e->emu->low_mem(); int x=robs_x(m);
                if(robs_dying(m)||robs_dead(m)||robs_world(m)!=4||robs_stage(m)!=4) break;
                if(m[0xb5]==1&&m[0x1d]==0&&x>=1600) standing[m[0xce]]++;
                if(m[0xb5]==1&&m[0xce]==176&&m[0x1d]==0&&x>=1530) low=true;
                if(m[0xb5]==1&&m[0xce]==176&&m[0x1d]==0&&x>furthest_low) {
                    furthest_low=x; const auto& fr=e->emu->frame(); FILE* out=fopen("seed_maze_furthest.ppm","wb");
                    fprintf(out,"P6 256 240 255\n");
                    for(int y=0;y<240;y++) for(int c=0;c<256;c++) {
                        const auto& rgb=Nes_Emu::nes_colors[fr.palette[fr.pixels[y*fr.pitch+c]]&(Nes_Emu::color_table_size-1)];
                        fputc(rgb.red,out); fputc(rgb.green,out); fputc(rgb.blue,out);
                    }
                    fclose(out);
                }
                if(m[0xb5]==1&&m[0xce]==176&&m[0x1d]==0&&x>=1650&&x<=2000) {
                    // The bank is usually full, so a new cell replaces another: check the key.
                    retro_sync_from_emu(e); uint64_t key=retro_archive_key(e);
                    bool fresh=!b.index.count(key); retro_archive_capture(e);
                    if(fresh&&b.index.count(key)) { found++; printf("try %d: bottom corridor x=%d after %d decisions\n",t,x,d+1); }
                    break;
                }
            }
        }
        e->archive->state.reset();
        for(auto& [y,n]:standing) printf("standing at x>=1600: y=%d decisions=%ld\n",y,n);
        printf("furthest bottom-floor x=%d (screen in seed_maze_furthest.ppm)\n",furthest_low);
        printf("%d bottom-corridor cells added from %zu source cells\n",found,sources.size());
        if(!found) return 2;
        retro_archive_file(argv[3],true,0);
        return 0;
    } catch(const std::exception& ex){fprintf(stderr,"seed_maze: %s\n",ex.what());return 1;}
}
