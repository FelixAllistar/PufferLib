// Bounded input search for reference encounters. This is not policy training.
#include "../retro/retro.h"
#include "rom_import.h"
#include "logic_cpu.h"
#include <algorithm>
#include <fstream>
#include <unordered_set>
#include <vector>
#include <filesystem>
struct Node {SmbLogic state;std::vector<uint8_t> inputs;double score;};
static bool contains(const unsigned char* m,int actor) {
    for(int k=0;k<6;k++)if(m[15+k]&&m[15+k]<128&&m[22+k]==actor)return true;
    return false;
}
static int px(const unsigned char* m){return m[0x6d]*256+m[0x86];}
static int py(const unsigned char* m){return ((int)m[0xb5]-1)*256+m[0xce];}
int main(int argc,char**argv) {
    try {
        if(argc!=6)throw std::runtime_error("usage: find_clip WORLD STAGE TARGET_X ACTOR_ID OUTPUT.inputs");
        int w=atoi(argv[1]),l=atoi(argv[2]),target=atoi(argv[3]),actor=atoi(argv[4]);
        RetroRom& rom=retro_rom();retro_load_rom_locked(rom,RETRO_SMB1_ROM_PATH);retro_prepare_start_locked(rom,w,l);
        Nes_Emu e;retro_check(e.set_cart(&rom.cart,&rom.seed));e.load_state(rom.starts[(w-1)*4+l-1]->state);
        Node root={smb_import_logic(e),{},0};root.state.timing.enabled=0;
        std::vector<Node> beam;beam.push_back(root);std::vector<uint8_t> solution;
        const int actions[]={130,131,128,129,0,1,66,67};
        const int width=96,hold=8,depth_limit=320;
        long frames=0;
        for(int depth=0;depth<depth_limit&&solution.empty();depth++) {
            std::vector<Node> candidates;
            for(const auto& old:beam)for(int action:actions) {
                Node node=old;bool alive=true,found=false;
                for(int f=0;f<hold;f++) {
                    if(smb_native_frame(&node.state,rom.cart.prg(),action)){alive=false;break;}
                    frames++;node.inputs.push_back(action);const auto* m=node.state.ram;
                    if((actor<0&&px(m)>=target)||(actor>=0&&contains(m,actor))){found=true;break;}
                    if(m[0xe]==11||py(m)>224||m[0x770]!=1){alive=false;break;}
                }
                if(found){solution=std::move(node.inputs);break;}
                if(!alive)continue;
                const auto* m=node.state.ram;int x=px(m),y=py(m);
                node.score=actor==47?-abs(x-target)+(abs(x-target)<40?(176-y)*0.35:0):x;
                node.score+=((int8_t)m[0x57])*0.015;
                candidates.push_back(std::move(node));
            }
            if(!solution.empty())break;
            std::sort(candidates.begin(),candidates.end(),[](const Node& a,const Node& b){return a.score>b.score;});
            beam.clear();std::unordered_set<uint64_t> occupied;
            for(auto& n:candidates) {
                const auto* m=n.state.ram;
                uint64_t key=(uint64_t)(px(m)/4)&65535;
                key|=(uint64_t)((py(m)+256)/4&255)<<16;
                key|=(uint64_t)((int8_t)m[0x57]/4+16)<<24;
                key|=(uint64_t)m[0x9f]<<30;key|=(uint64_t)m[0x1d]<<38;key|=(uint64_t)(m[0xd]&128)<<42;
                if(occupied.insert(key).second)beam.push_back(std::move(n));
                if((int)beam.size()==width)break;
            }
            if(beam.empty())break;
            if(depth%25==0){printf("depth=%d x=%d candidates=%zu frames=%ld\n",depth,px(beam[0].state.ram),candidates.size(),frames);fflush(stdout);}
        }
        if(solution.empty()){fprintf(stderr,"No solution within bounded search (%ld frames).\n",frames);return 1;}
        // A candidate tape becomes a fixture only after unmodified-ROM replay.
        e.load_state(rom.starts[(w-1)*4+l-1]->state);e.set_rom_blocks(false);e.set_idle_skip(true);
        bool observed=false;for(int action:solution){retro_check(e.emulate_skip_frame_fast(action,0));observed|=actor<0?px(e.low_mem())>=target:contains(e.low_mem(),actor);}
        if(!observed)throw std::runtime_error("search candidate failed independent ROM encounter validation");
        std::filesystem::create_directories(std::filesystem::path(argv[5]).parent_path());std::ofstream out(argv[5]);
        for(int action:solution)out<<action<<'\n';
        printf("Verified ROM encounter: stage=%d-%d actor=%d frames=%zu x=%d searched=%ld\n",w,l,actor,solution.size(),px(e.low_mem()),frames);
        return 0;
    }catch(const std::exception& ex){fprintf(stderr,"find_clip: %s\n",ex.what());return 2;}
}
