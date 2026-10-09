// Replay and branch from reachable ROM states. No RAM writes or policy updates.
#include "../retro/retro.h"
#include "fpg_rom.h"
#include "search.h"
#include <fstream>
#include <vector>
#include <map>
#include <stdexcept>

static void advance(Nes_Emu& emu,int buttons) {
    retro_bind_pixels(&emu); retro_check(emu.emulate_frame(fpg_rom_mask(buttons)));
    if(emu.error_count()) throw std::runtime_error("emulator opcode error");
}
static int compare(const FpgBody& a,const FpgBody& b,std::map<std::string,int>& counts,bool detail) {
    int errors=0;
#define FIELD(n) if(a.n!=b.n) {errors++;counts[#n]++;if(detail)printf(" %s:%d/%d",#n,a.n,b.n);}
    FIELD(x);FIELD(y);FIELD(vx);FIELD(vy);FIELD(xsub);FIELD(ysub);FIELD(ax);FIELD(vyfrac);
    FIELD(motion);FIELD(facing);FIELD(moving);FIELD(abs_vx);FIELD(running);FIELD(run_timer);
    FIELD(gravity);FIELD(fall_gravity);FIELD(jump_y);FIELD(previous_ab);FIELD(collision);
    FIELD(side_timer);FIELD(routine);
    if(a.routine!=8||b.routine!=8) {FIELD(flag_y);FIELD(flag_fraction);FIELD(grab_y);}
#undef FIELD
    return errors;
}
int main(int argc,char**argv) {
    try {
        if(argc!=2 && argc!=3 && argc!=4 && argc!=5) throw std::runtime_error("usage: reference ACTION_TAPE [--end-only | SEARCH_FRAME OUTPUT_TAPE | --replay START_FRAME FPG_TAPE]");
        bool replay=argc==5 && std::string(argv[2])=="--replay";
        bool end_only=argc==3 && std::string(argv[2])=="--end-only";
        if((argc==3&&!end_only)||(argc==5&&!replay)) throw std::runtime_error("unknown mode");
        std::ifstream input(argv[1]); if(!input) throw std::runtime_error("cannot read tape");
        std::vector<int> actions;int v;while(input>>v) actions.push_back(fpg_from_ml(v));
        RetroRom& rom=retro_rom();retro_load_rom_locked(rom,RETRO_SMB1_ROM_PATH);retro_prepare_start_locked(rom,1,1);
        Nes_Emu emu,branch;retro_check(emu.set_cart(&rom.cart,&rom.seed));retro_check(branch.set_cart(&rom.cart,&rom.seed));
        retro_bind_pixels(&emu);emu.load_state(rom.starts[0]->state);
        std::map<std::string,int> counts;int checks=0,failed=0,frames=0;
        for(size_t t=0;t<actions.size();t++) {
            const unsigned char* m=emu.low_mem();FpgBody before=fpg_rom_body(m);
            if((argc==4||replay) && t==(size_t)std::stoi(argv[replay?3:2])) {
                FpgWorld w;fpg_rom_world(m,&w);
                printf("search start %zu x=%d.%d y=%d.%d vx=%d pole=%d\n",t,before.x,before.xsub,before.y,before.ysub,before.vx,w.pole_col);
                for(int y=2;y<14;y++) {for(int x=w.origin_col;x<w.origin_col+40;x++) printf("%c",".#|"[fpg_tile(&w,x*16,y*16)]);puts("");}
                std::vector<int> path;
                if(replay) {std::ifstream suffix(argv[4]);int a;while(suffix>>a) {if(a<0||a>=12)throw std::runtime_error("invalid action");path.push_back(a);}}
                else path=fpg_search(before,w,12000,240);
                if(path.empty()) throw std::runtime_error("bounded search found no solution");
                std::ofstream tape;if(!replay) {tape.open(argv[3]);if(!tape) throw std::runtime_error("cannot create search tape");}
                FpgBody predicted=before;
                for(int a:path) {
                    fpg_physics(&predicted,&w,fpg_buttons(a));advance(emu,fpg_buttons(a));
                    auto actual=fpg_rom_body(emu.low_mem());std::map<std::string,int> diff;
                    if(compare(predicted,actual,diff,true)) throw std::runtime_error("searched trajectory diverged on ROM replay");
                    if(!replay)tape<<a<<'\n';
                }
                auto actual=fpg_rom_body(emu.low_mem());
                printf("{\"search_frames\":%zu,\"rom_verified\":true,\"grab_y\":%d,\"flag_y\":%d,\"routine\":%d}\n",path.size(),actual.grab_y,actual.flag_y,actual.routine);
                return actual.routine==5&&actual.grab_y>=162&&actual.flag_y==before.flag_y?0:1;
            }
            if(argc==4||replay) {advance(emu,actions[t]);continue;}
            if(before.x>=(end_only?2800:2688) && m[0x74e]==1 && m[0x754]==1 && m[0x747]==0
                && before.routine==8 && before.y>=0 && before.y<208) {
                FpgWorld w;fpg_rom_world(m,&w);Nes_State snapshot;emu.save_state(&snapshot);frames++;
                if(before.motion==0 && (frames%8==0 || before.x>3000))
                    printf("ground t=%zu x=%d.%d y=%d vx=%d ax=%d flagcol=%d\n",t,before.x,before.xsub,before.y,before.vx,before.ax,w.pole_col);
                for(int a=0;a<12;a++) {
                    branch.load_state(snapshot);FpgBody predicted=before;fpg_physics(&predicted,&w,fpg_buttons(a));
                    advance(branch,fpg_buttons(a));FpgBody actual=fpg_rom_body(branch.low_mem());checks++;
                    auto temp=counts;int n=compare(predicted,actual,counts,false);
                    if(n) {
                        if(failed<20) {
                            printf("mismatch t=%zu a=%d start=(%d.%d,%d.%d) v=(%d,%d) motion=%d",t,a,before.x,before.xsub,before.y,before.ysub,before.vx,before.vy,before.motion);
                            compare(predicted,actual,temp,true);printf("\n");
                        }
                        failed++;
                    }
                }
            }
            advance(emu,actions[t]);
            auto after=fpg_rom_body(emu.low_mem());
            if(before.routine==8 && (after.routine==4||after.routine==5))
                printf("flag t=%zu grab_y=%d flag_y=%d routine=%d\n",t+1,after.grab_y,after.flag_y,after.routine);
        }
        printf("{\"frames\":%d,\"checks\":%d,\"failed\":%d,\"fields\":{",frames,checks,failed);
        bool first=true;for(auto& c:counts) {printf("%s\"%s\":%d",first?"":",",c.first.c_str(),c.second);first=false;}
        printf("}}\n");return failed?1:0;
    } catch(const std::exception& e) {fprintf(stderr,"fpg reference: %s\n",e.what());return 2;}
}
