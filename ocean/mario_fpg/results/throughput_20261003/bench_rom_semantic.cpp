#include "../../ocean/retro/retro.h"
#include "../../ocean/mario_fpg/fpg_task.h"
#include "../../ocean/mario_fpg/fpg_rom.h"
#include <chrono>
#include <fstream>
#include <vector>
#include <stdexcept>
int main() {
    try {
        RetroRom& rom=retro_rom();retro_load_rom_locked(rom,RETRO_SMB1_ROM_PATH);retro_prepare_start_locked(rom,1,1);
        Nes_Emu emu;retro_check(emu.set_cart(&rom.cart,&rom.seed));retro_bind_pixels(&emu);emu.load_state(rom.starts[0]->state);
        std::ifstream prefix("ocean/mario_fpg/reference/learned_pipe_route.actions");
        for(int i=0;i<1510;i++){int a;if(!(prefix>>a))throw std::runtime_error("short prefix");retro_check(emu.emulate_frame(fpg_rom_mask(fpg_from_ml(a))));}
        Nes_State root;emu.save_state(&root);
        std::ifstream input("ocean/mario_fpg/reference/fpg_suffix.actions");std::vector<int> actions;int a;
        while(input>>a)actions.push_back(a);if(actions.size()!=100)throw std::runtime_error("invalid suffix");
        FpgConfig cfg={};cfg.contract_version=2;cfg.max_frames=240;
        for(int draw=0;draw<=1;draw++) {
            const int repeats=128;double checksum=0;int success=0;
            auto run=[&]() {
                emu.load_state(root);emu.set_idle_skip(true);
                if(!emu.set_rom_blocks(true))throw std::runtime_error("optimized ROM engine unavailable");
                for(int t=0;t<(int)actions.size();t++) {
                    int buttons=fpg_rom_mask(fpg_buttons(actions[t]));
                    retro_check(draw?emu.emulate_frame(buttons):emu.emulate_skip_frame_fast(buttons));
                    FpgState s={};const auto* m=emu.low_mem();s.body=fpg_rom_body(m);
                    fpg_rom_world(m,&s.world);s.camera=fpg_rom_camera(m);s.actors=fpg_rom_actors(m);s.tick=t+1;
                    float sum=0;for(int i=0;i<FPG_OBS;i++)sum+=fpg_observation(&s,&cfg,i);checksum+=sum;
                    if(t==(int)actions.size()-1)success+=fpg_outcome(&s.body)==FPG_SUCCESS;
                }
            };
            run();checksum=0;success=0;
            auto start=std::chrono::steady_clock::now();for(int r=0;r<repeats;r++)run();
            double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            if(success!=repeats||emu.error_count())throw std::runtime_error("ROM benchmark replay failed");
            printf("{\"backend\":\"optimized QuickNES\",\"cpu_threads\":1,\"render\":%s,\"idle_loop_skip\":true,\"rom_blocks\":true,\"game_frames\":%zu,\"seconds\":%.6f,\"frames_per_second\":%.1f,\"observations\":448,\"resets_included\":true,\"ppo_included\":false,\"successful_replays\":%d,\"checksum\":%.9g}\n",draw?"true":"false",actions.size()*repeats,seconds,actions.size()*repeats/seconds,success,checksum);
            fflush(stdout);
        }
    } catch(const std::exception& e){fprintf(stderr,"ROM benchmark: %s\n",e.what());return 2;}
}
