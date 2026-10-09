// Frozen-policy evaluation only. All resets are exact, naturally reached states.
#include "../retro/retro.h"
#include "../retro/nes_emu/abstract_file.h"
#include "fpg_rom.h"
#include "fpg_task.h"
#include "fpg_config.h"
#include "policy.h"
#include <fstream>
#include <vector>
#include <stdexcept>
static void advance(Nes_Emu& emu,int buttons) {
    retro_bind_pixels(&emu);retro_check(emu.emulate_frame(fpg_rom_mask(buttons)));
    if(emu.error_count())throw std::runtime_error("emulator opcode error");
}
static std::vector<char> snapshot(Nes_Emu& emu) {
    Nes_State state;emu.save_state(&state);Mem_Writer writer;retro_check(state.write(writer));
    return {writer.data(),writer.data()+writer.size()};
}
int main(int argc,char**argv) {
    try {
        if(argc!=7)throw std::runtime_error("usage: transfer MODEL CONFIG PREFIX START_FRAME SUFFIX EPISODES_PER_TIER");
        Ini ini={};puf_ini_load_file(&ini,argv[2]);FpgConfig cfg=fpg_config(puf_ini_section(&ini,"env",0));
        bool random_policy=std::string(argv[1])=="random",right_policy=std::string(argv[1])=="right",idle_policy=std::string(argv[1])=="idle";
        bool baseline=random_policy||right_policy||idle_policy;
        void* policy=baseline?nullptr:fpg_policy_load(argv[1],(int)puf_ini_get(&ini,"policy","hidden_size"),(int)puf_ini_get(&ini,"policy","num_layers"));
        if(!baseline&&!policy)throw std::runtime_error("invalid model/architecture");
        int start=std::stoi(argv[4]),episodes=std::stoi(argv[6]);if(start<0||episodes<1||episodes>10000)throw std::runtime_error("invalid budget");
        std::ifstream prefix(argv[3]),suffix_file(argv[5]);if(!prefix||!suffix_file)throw std::runtime_error("missing tape");
        RetroRom& rom=retro_rom();retro_load_rom_locked(rom,RETRO_SMB1_ROM_PATH);retro_prepare_start_locked(rom,1,1);
        Nes_Emu emu;retro_check(emu.set_cart(&rom.cart,&rom.seed));retro_bind_pixels(&emu);emu.load_state(rom.starts[0]->state);
        for(int t=0;t<start;t++){int a;if(!(prefix>>a))throw std::runtime_error("short prefix");advance(emu,fpg_from_ml(a));}
        // Nes_State keeps internal pointers to its own sections; copying it
        // into a growing vector invalidates those pointers. Keep stable owners.
        std::vector<std::unique_ptr<Nes_State>> starts;int a;
        while(suffix_file>>a) {
            if(a<0||a>=12)throw std::runtime_error("invalid suffix action");
            auto snapshot=std::make_unique<Nes_State>();emu.save_state(snapshot.get());starts.push_back(std::move(snapshot));advance(emu,fpg_buttons(a));
        }
        FpgBody reference_end=fpg_rom_body(emu.low_mem());
        if(starts.empty()||fpg_outcome(&reference_end)!=FPG_SUCCESS)throw std::runtime_error("reference suffix did not produce FPG");
        for(int tier=0;tier<4;tier++)for(int deterministic=0;deterministic<=1;deterministic++) {
            int successes=0,normal=0,timeout=0,drift=0,frames=0;
            for(int e=0;e<episodes;e++) {
                int lo=tier==0?1:tier==1?9:33,hi=tier==0?8:tier==1?32:96;
                if(hi>(int)starts.size())hi=(int)starts.size();if(lo>hi)lo=hi;
                int span=hi-lo+1;
                int remaining=tier==3?(int)starts.size():lo+(episodes<span?e*span/episodes:e%span),offset=(int)starts.size()-remaining;
                emu.load_state(*starts[offset]);retro_bind_pixels(&emu);if(policy)fpg_policy_reset(policy);srand(fpg_hash(7001u+(unsigned)e));
                FpgState state={};int mismatches=0,t=0;std::vector<int> actions;
                for(;t<cfg.max_frames;t++) {
                    const unsigned char* m=emu.low_mem();state.body=fpg_rom_body(m);
                    fpg_rom_world_version(m,&state.world,cfg.contract_version);state.tick=t;
                    if(cfg.contract_version>=2){state.camera=fpg_rom_camera(m);state.actors=fpg_rom_actors(m);}
                    int screen_left=m[0x71a]*256+m[0x71c];
                    float obs[FPG_OBS];for(int i=0;i<FPG_OBS;i++)obs[i]=fpg_observation(&state,&cfg,i);
                    int action=random_policy?rand()%12:right_policy?7:idle_policy?0:fpg_policy_action(policy,obs,deterministic);actions.push_back(action);
                    FpgState predicted=state;fpg_step_task(&predicted,&cfg,action);FpgBody expected=predicted.body;
                    advance(emu,fpg_buttons(action));
                    FpgBody actual=fpg_rom_body(emu.low_mem());
                    // Scope departures (e.g. scrolling limits) are counted as
                    // transfer discrepancies, not silently repaired in RAM.
                    if(memcmp(&expected,&actual,sizeof(actual))) {
                        mismatches++;
                        if(mismatches<=2) {
                            fprintf(stderr,"mismatch tier=%d deterministic=%d episode=%d frame=%d pre=(%d,%d) screen_left=%d action=%d",tier,deterministic,e,t,state.body.x,state.body.y,screen_left,action);
#define DIFF(n) if(expected.n!=actual.n)fprintf(stderr," %s:%d/%d",#n,expected.n,actual.n)
                            DIFF(x);DIFF(y);DIFF(vx);DIFF(vy);DIFF(xsub);DIFF(ysub);DIFF(ax);DIFF(vyfrac);DIFF(motion);DIFF(facing);DIFF(moving);DIFF(abs_vx);DIFF(running);DIFF(run_timer);DIFF(gravity);DIFF(fall_gravity);DIFF(jump_y);DIFF(previous_ab);DIFF(collision);DIFF(side_timer);DIFF(routine);DIFF(flag_y);DIFF(flag_fraction);DIFF(grab_y);
#undef DIFF
                            fputc('\n',stderr);
                        }
                    }
                    state.status=fpg_outcome(&actual);
                    if(state.status){t++;break;}
                }
                if(!state.status)state.status=FPG_TIMEOUT;
                successes+=state.status==FPG_SUCCESS;normal+=state.status==FPG_NORMAL_FLAG;timeout+=state.status==FPG_TIMEOUT;
                drift+=mismatches;frames+=t;
                FpgBody last=fpg_rom_body(emu.low_mem());
                auto final=snapshot(emu);emu.load_state(*starts[offset]);
                for(int action:actions)advance(emu,fpg_buttons(action));
                if(final!=snapshot(emu))throw std::runtime_error("action tape failed complete NES-state replay");
                printf("{\"type\":\"episode\",\"tier\":%d,\"deterministic\":%d,\"episode\":%d,\"reference_remaining\":%d,\"status\":%d,\"frames\":%d,\"controller_mismatches\":%d,\"grab_y\":%d,\"flag_y\":%d,\"replay_verified\":true,\"actions\":[",tier,deterministic,e,remaining,state.status,t,mismatches,last.grab_y,last.flag_y);
                for(size_t i=0;i<actions.size();i++)printf("%s%d",i?",":"",actions[i]);puts("]}");
            }
            printf("{\"type\":\"summary\",\"tier\":%d,\"deterministic\":%d,\"episodes\":%d,\"fpg\":%d,\"ordinary_flag\":%d,\"timeouts\":%d,\"controller_mismatches\":%d,\"frames\":%d}\n",tier,deterministic,episodes,successes,normal,timeout,drift,frames);fflush(stdout);
        }
        fpg_policy_free(policy);puf_ini_free(&ini);return 0;
    } catch(const std::exception& e){fprintf(stderr,"fpg transfer: %s\n",e.what());return 2;}
}
