// Independent ROM replay verifies the continuing lifecycle across stage starts,
// deaths, flagpole finishes, castle victory mode and transitions into the next level.
#include "reference_sources.h"
#include "bank_io.h"
#include "game.h"
#include "observation.h"
#include "rom_check.h"
#include <cmath>

static void controller_controls() {
    SmbGameConfig c;SmbLogic s={};SmbEpisode e={};SmbGameProgress p={};
    s.ram[0x770]=1;s.ram[0x75a]=2;s.ram[0xe]=8;p.mode=1;p.lives=2;p.routine=8;
    s.ram[0xe]=11;smb_game_after_frame(&s,&e,&p,&c);
    fpt_rom_require(!e.status,"death animation terminated a full game");
    s.ram[0x75a]=1;smb_game_after_frame(&s,&e,&p,&c);
    fpt_rom_require(!e.status&&p.deaths==1,"natural life loss did not continue");
    s.ram[0xe]=4;float reward=smb_game_after_frame(&s,&e,&p,&c);
    fpt_rom_require(reward>1&&!e.status&&p.clears==1,"flag did not reward and continue");
    for(int i=0;i<10;i++)fpt_rom_require(!smb_game_after_frame(&s,&e,&p,&c),"flag paid twice");
    s.ram[0x75c]=1;smb_game_after_frame(&s,&e,&p,&c);
    fpt_rom_require(!smb_game_after_frame(&s,&e,&p,&c)&&p.clears==1,"finish routine leaked reward into next level");
    s.ram[0x75a]=255;smb_game_after_frame(&s,&e,&p,&c);
    fpt_rom_require(e.status==SMB_EPISODE_DEAD&&p.deaths==2,"game over did not terminate");
    for(int stage:{3,31}) {
        e={};p={};p.stage=stage;p.mode=1;p.lives=2;p.routine=8;
        s={};s.ram[0x75f]=stage/4;s.ram[0x75c]=3;s.ram[0x75a]=2;s.ram[0x770]=2;
        smb_game_after_frame(&s,&e,&p,&c);
        fpt_rom_require(e.status==(stage==31?SMB_EPISODE_SUCCESS:SMB_EPISODE_ACTIVE),"castle win classification wrong");
    }
    c.max_frames=1;e={};p={};s={};s.ram[0x75a]=2;s.ram[0x770]=1;
    smb_game_after_frame(&s,&e,&p,&c);fpt_rom_require(e.status==SMB_EPISODE_TIMEOUT,"frame limit did not terminate");
}
int main() {
    try {
        controller_controls();RetroRom& rom=retro_rom();retro_load_rom_locked(rom,RETRO_SMB1_ROM_PATH);
        Nes_Emu emu;retro_check(emu.set_cart(&rom.cart,&rom.seed));emu.set_rom_blocks(false);emu.set_idle_skip(true);
        SmbBank bank("build/mario_sim/runtime/generated/bank.bin");
        SmbGameConfig config;config.max_frames=10000;unsigned frames=0,clears=0,deaths=0,transitions=0,gameovers=0,checkpoints=0;
        auto replay=[&](int stage,const std::vector<int>& actions,int limit,bool use_bank) {
            retro_prepare_start_locked(rom,stage/4+1,stage%4+1);emu.load_state(rom.starts[stage]->state);
            SmbLogic real=smb_import_logic(emu),native=real;
            if(use_bank) {
                bool found=false;
                for(const auto& entry:bank.entries)if((int)entry.stage==stage&&(entry.flags&SMB_BANK_STAGE_START)&&!(entry.flags&SMB_BANK_CONSTRUCTED)) {
                    fpt_rom_require(!smb_scene_reset(&native,&entry.scene),"stage bank reset failed");found=true;break;
                }
                fpt_rom_require(found&&fpt_rom_difference(native,real)<0,"stage bank is not the original natural boot");
            }
            SmbEpisode a={},b={};SmbGameProgress pa={},pb={};
            pa.stage=stage;pa.mode=real.ram[0x770];pa.routine=real.ram[0xe];pa.lives=real.ram[0x75a];smb_game_track_progress(&real,&pa,&config);pb=pa;
            FptObservationHistory ha={},hb={};
            for(int t=0;t<limit;t++) {
                int buttons=actions.empty()?explorer(t,stage%2):t<(int)actions.size()?actions[t]:130;
                fpt_rom_require(!smb_native_frame(&native,bank.worlds.data(),buttons),"native frame fault");
                retro_check(emu.emulate_skip_frame_fast(buttons,0));real=smb_import_logic(emu);
                if(fpt_rom_difference(native,real)>=0)throw std::runtime_error("ROM/native frame mismatch stage="+std::to_string(stage)+" frame="+std::to_string(t));
                float x[FPT_OBS],y[FPT_OBS];fpt_observe(&native,bank.worlds.data(),&ha,x);fpt_observe(&real,bank.worlds.data(),&hb,y);
                for(int k=0;k<FPT_OBS;k++)fpt_rom_require(std::isfinite(x[k])&&x[k]==y[k],"ROM/native semantic mismatch");
                int old_stage=pa.stage;
                float ra=smb_game_after_frame(&native,&a,&pa,&config),rb=smb_game_after_frame(&real,&b,&pb,&config);
                fpt_rom_require(ra==rb&&!memcmp(&a,&b,sizeof(a))&&!memcmp(&pa,&pb,sizeof(pa)),"ROM/native lifecycle mismatch");
                transitions+=pa.stage!=old_stage;frames++;
                if(a.status)break;
            }
            checkpoints+=pa.checkpoints;clears+=pa.clears;deaths+=pa.deaths;gameovers+=a.status==SMB_EPISODE_DEAD;
        };
        for(int stage=0;stage<32;stage++)replay(stage,{},4096,true);
        auto tape=[](const char* path,bool actions) {
            std::ifstream in(path);fpt_rom_require(bool(in),"missing reference tape");std::vector<int> result;int n;
            while(in>>n)result.push_back(actions?smb_action_buttons(n):n);return result;
        };
        auto route=tape("ocean/mario_sim/reference/learned_pipe_route.actions",true);
        route.resize(1510);auto suffix=tape("ocean/mario_sim/reference/fpg_suffix.actions",false);
        for(auto& a:suffix)a=(a%3==1?128:a%3==2?64:0)|(((a/3)&1)?1:0)|(a>=6?2:0);
        route.insert(route.end(),suffix.begin(),suffix.end());replay(0,route,route.size()+2000,false);
        auto castle=tape("ocean/mario_sim/reference/bowser_1_4.inputs",false);replay(3,castle,castle.size()+2000,false);
        fpt_rom_require(clears>0&&deaths>0&&transitions>0&&gameovers>0&&checkpoints>0,"missing lifecycle coverage");
        printf("{\"stages\":32,\"ROM_frames\":%u,\"clears\":%u,\"life_losses\":%u,\"level_transitions\":%u,\"gameovers\":%u,\"checkpoints\":%u,\"semantic_and_lifecycle_parity\":true,\"failures\":0}\n",frames,clears,deaths,transitions,gameovers,checkpoints);return 0;
    }catch(const std::exception& e){fprintf(stderr,"full-game ROM test: %s\n",e.what());return 1;}
}
