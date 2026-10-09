#pragma once
// ROM snapshots and encounter input routes shared by reconstruction and bank generation.
#include "../retro/retro.h"
#include "trace.h"
#include "parameters.h"
#include "world.h"
#include "rom_import.h"
#include "logic_cpu.h"
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <array>

struct Source {
    std::unique_ptr<Nes_State> snapshot;
    SmbScene scene;
    int stage,frame;
    std::string name;
    std::vector<int> teacher;
};
static SmbLogic import_logic(Nes_Emu& e) {
    return smb_import_logic(e);
}
static void configure_reference(Nes_Emu& e,const SmbLogic& s) {
    memcpy(e.low_mem(),s.ram,SMB_RAM);auto& r=e.cpu_debug().r;
    r.pc=s.pc;r.a=s.a;r.x=s.x;r.y=s.y;r.sp=s.sp;r.status=s.p;
}
static uint32_t random_u32(uint32_t& x) {x^=x<<13;x^=x>>17;x^=x<<5;return x;}
static bool gameplay(const unsigned char* m) {return m[0x770]==1&&m[0x772]==3;}
static bool boundary(Nes_Emu& e) {return e.cpu_debug().r.pc==0x8057;}
static int explorer(int t,int variant) {
    if(t<40)return 0;
    int period=variant?56:40,held=variant?35:24;
    return 130|(((t+variant*13)%period<held)?1:0);
}
static std::vector<Source> collect(RetroRom& rom,Nes_Emu& e,bool boundaries) {
    std::vector<Source> result,items,phases;
    for(int stage=0;stage<32;stage++) {
        retro_prepare_start_locked(rom,stage/4+1,stage%4+1);
        for(int variant=0;variant<2;variant++) {
            e.load_state(rom.starts[stage]->state);
            bool captured_item=false;
            bool captured_lag=false,captured_transition=false;
            for(int t=0;t<=960;t++) {
                const auto* m=e.low_mem();
                bool lag=!boundary(e),transition=!gameplay(m);
                if(boundaries&&((lag&&!captured_lag)||(transition&&!captured_transition))) {
                    Source source;source.snapshot=std::make_unique<Nes_State>();e.save_state(source.snapshot.get());
                    source.scene={};source.scene.initial=import_logic(e);source.stage=stage;source.frame=t;
                    source.name=lag?"boundary_nonidle":"boundary_transition";phases.push_back(std::move(source));
                    captured_lag|=lag;captured_transition|=transition;
                }
                if(!captured_item&&m[0x14]&&m[0x1b]==46&&m[0x23]>=6&&gameplay(m)&&boundary(e)) {
                    Source source;source.snapshot=std::make_unique<Nes_State>();e.save_state(source.snapshot.get());
                    source.scene={};source.scene.initial=import_logic(e);source.stage=stage;source.frame=t;
                    source.name="powerup_jump_cycle_"+std::to_string(variant);items.push_back(std::move(source));captured_item=true;
                }
                if((t==0||t==96||t==192||t==320||t==512||t==768||t==960)
                    &&gameplay(e.low_mem())&&boundary(e)&&e.low_mem()[0xe]==8) {
                    Source source;source.snapshot=std::make_unique<Nes_State>();e.save_state(source.snapshot.get());
                    source.scene={};source.scene.initial=import_logic(e);source.stage=stage;source.frame=t;
                    source.name="jump_cycle_"+std::to_string(variant);
                    result.push_back(std::move(source));
                }
                retro_check(e.emulate_skip_frame_fast(explorer(t,variant),0));
            }
        }
    }
    struct Route {int stage;const char* path;const char* name;bool ml;};
    const Route routes[]={{3,"ocean/mario_sim/reference/bowser_1_4.inputs","bowser_1_4",false},
        {4,"ocean/mario_sim/reference/vine_2_1.inputs","vine_2_1",false},
        {0,"ocean/mario_fpg/reference/learned_pipe_route.actions","learned_1_1_pipe_route",true},
        {0,"ocean/mario_sim/reference/powerup_1_1.inputs","powerup_1_1",false}};
    for(const auto& route:routes) {
        std::ifstream input(route.path);if(!input)throw std::runtime_error("missing encounter input route");
        std::vector<int> tape;int b;while(input>>b)tape.push_back(route.ml?((b&3)|((b&60)<<2)):b);
        int original=tape.size();
        for(int t=0;t<240;t++)tape.push_back(route.stage==4?(t<90?0:t<122?17:16):130);
        e.load_state(rom.starts[route.stage]->state);
        for(int t=0;t<(int)tape.size();t++) {
            if((t%64==0||t==original)&&gameplay(e.low_mem())&&boundary(e)) {
                Source source;source.snapshot=std::make_unique<Nes_State>();e.save_state(source.snapshot.get());
                source.scene={};source.scene.initial=import_logic(e);source.stage=route.stage;source.frame=t;source.name=route.name;
                source.teacher.assign(tape.begin()+t,tape.end());result.push_back(std::move(source));
            }
            retro_check(e.emulate_skip_frame_fast(tape[t],0));
        }
    }
    for(auto& source:items)result.push_back(std::move(source));
    for(auto& source:phases)result.push_back(std::move(source));
    return result;
}

static std::vector<Source> collect_flagpole_sources(RetroRom& rom,Nes_Emu& e) {
    std::vector<Source> result;
    auto load=[](const std::string& path,bool suffix) {
        std::ifstream input(path);if(!input)throw std::runtime_error("missing flagpole route");
        std::vector<int> tape;int action;
        while(input>>action) {
            if(action<0||action>=(suffix?12:64))throw std::runtime_error("invalid flagpole action");
            if(suffix)tape.push_back((action%3==1?128:action%3==2?64:0)|(((action/3)&1)?1:0)|(action>=6?2:0));
            else tape.push_back((action&3)|((action&60)<<2));
        }
        return tape;
    };
    auto prefix=load("ocean/mario_fpg/reference/learned_pipe_route.actions",false);
    if(prefix.size()<1510)throw std::runtime_error("short flagpole prefix");
    auto suffix=load("ocean/mario_fpg/reference/fpg_suffix.actions",true);
    std::vector<int> searched(prefix.begin(),prefix.begin()+1510);searched.insert(searched.end(),suffix.begin(),suffix.end());
    struct Route {std::string name;std::vector<int> tape;int first;};
    std::vector<Route> routes;
    routes.push_back({"flagpole_learned_20",prefix,0});
    for(int episode:{7,26,41})routes.push_back({"flagpole_learned_"+std::to_string(episode),
        load("ocean/mario_fpg/reference/routes/episode_"+std::to_string(episode)+".actions",false),0});
    routes.push_back({"flagpole_searched",searched,1510});
    for(const auto& route:routes) {
        e.load_state(rom.starts[0]->state);
        for(int t=0;t<(int)route.tape.size();t++) {
            const auto* m=e.low_mem();int x=m[0x6d]*256+m[0x86],y=((int)m[0xb5]-1)*256+m[0xce];
            if(t>=route.first&&(t%8==0||t==route.first)&&gameplay(m)&&m[0xe]==8
                &&m[0x74e]==1&&m[0x754]==1&&x>=2799&&x<3180&&y>=0&&y<208) {
                Source source;source.snapshot=std::make_unique<Nes_State>();e.save_state(source.snapshot.get());
                source.scene={};source.scene.initial=smb_import_logic(e);source.stage=0;source.frame=t;source.name=route.name;
                source.teacher.assign(route.tape.begin()+t,route.tape.end());result.push_back(std::move(source));
            }
            retro_check(e.emulate_skip_frame_fast(route.tape[t],0));
        }
    }
    return result;
}
