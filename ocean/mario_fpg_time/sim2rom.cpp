// Frozen policy, independent recurrent states, matched naturally reached resets.
#include "../mario_sim/reference_sources.h"
#include "curriculum.h"
#include "rom_check.h"
#include "policy.h"
#include "observation.h"
#include "table.h"
#include <algorithm>
#include <cmath>

static int choose(const float* logits,uint32_t* rng,int deterministic) {
    int best=0;for(int a=0;a<64;a++){fpt_rom_require(std::isfinite(logits[a]),"nonfinite policy output");if(logits[a]>logits[best])best=a;}
    if(deterministic)return best;
    float probabilities[64],sum=0;for(int a=0;a<64;a++){probabilities[a]=expf(logits[a]-logits[best]);sum+=probabilities[a];}
    double draw=((double)smb_random(rng)+0.5)/4294967296.0*sum;
    for(int a=0;a<64;a++){draw-=probabilities[a];if(draw<=0)return a;}return 63;
}
int main(int argc,char** argv) {
    try {
        if(argc<4||argc>10)throw std::runtime_error("usage: sim2rom MODEL CONFIG OUTPUT [EPISODES=64] [SEED=137] [DETERMINISTIC=0] [BANK_DIRECTORY|new-game] [REFERENCE_FRAMES=0] [OBJECTIVE=fpg|clear]");
        int episodes=argc>4?std::stoi(argv[4]):64;uint32_t seed=argc>5?(uint32_t)std::stoul(argv[5]):137;
        int deterministic=argc>6?std::stoi(argv[6]):0;std::filesystem::path root=argc>7?argv[7]:"build/mario_fpg_time/pipe",out=argv[3];
        int reference_frames=argc>8?std::stoi(argv[8]):0;
        bool new_game=root=="new-game",clear=argc>9&&!strcmp(argv[9],"clear");
        fpt_rom_require(argc<=9||clear||!strcmp(argv[9],"fpg"),"objective must be fpg or clear");
        fpt_rom_require(!new_game||reference_frames==0,"new-game has no reference depth");
        fpt_rom_require(episodes>0&&episodes<=100000&&seed&&(deterministic==0||deterministic==1),"invalid transfer parameters");
        Ini ini={};puf_ini_load_file(&ini,argv[2]);int hidden=(int)puf_ini_get(&ini,"policy","hidden_size"),layers=(int)puf_ini_get(&ini,"policy","num_layers");
        int horizon=(int)puf_ini_get(&ini,"env","max_frames");fpt_rom_require(horizon>0&&horizon<=1000000,"invalid episode limit");
        void* native_policy=fpt_policy_load(argv[1],hidden,layers);void* rom_policy=fpt_policy_load(argv[1],hidden,layers);
        fpt_rom_require(native_policy&&rom_policy,"checkpoint is incompatible with semantic FPG observations and 64 actions; old RAM policies require the archived binary");
        std::unique_ptr<SmbBank> bank;
        if(!new_game){bank=std::make_unique<SmbBank>((root/"bank.bin").c_str());fpt_require_natural_bank(*bank);}
        fpt_rom_require(reference_frames==0||(reference_frames>=3&&reference_frames<=425),"invalid fixed reference depth");
        std::vector<uint32_t> eligible;
        if(reference_frames) {
            FpgTimeTable table(puf_ini_get_str(&ini,"env","time_table"),*bank,
                puf_ini_get_str(&ini,"env","engine_cpu_archive"),puf_ini_get_str(&ini,"env","engine_module"));
            for(unsigned i=0;i<bank->entries.size();i++)if((int)table.entries[i].best_frames==reference_frames)eligible.push_back(i);
        }else if(new_game)eligible.push_back(0);
        else for(unsigned i=0;i<bank->entries.size();i++)eligible.push_back(i);
        fpt_rom_require(!eligible.empty(),"no reset matches the requested reference depth");
        RetroRom& rom=retro_rom();retro_load_rom_locked(rom,RETRO_SMB1_ROM_PATH);
        if(bank)fpt_rom_require(!memcmp(rom.cart.prg(),bank->worlds.data(),SMB_PRG),"bank does not contain the original ROM world");
        if(new_game)retro_prepare_start_locked(rom,1,1);
        const uint8_t* world=rom.cart.prg();
        Nes_Emu emulator;retro_check(emulator.set_cart(&rom.cart,&rom.seed));emulator.set_rom_blocks(false);emulator.set_idle_skip(true);
        std::filesystem::create_directories(out);std::ofstream cases(out/"episodes.jsonl");
        std::ofstream traces(out/"trajectories.csv");
        traces<<"episode,frame,x,y,vx,vy,world,level,area,routine,action\n";
        unsigned wins_native=0,wins_rom=0,normal=0,deaths=0,timeouts=0,mismatches=0,episodes_run=0,completed=0;
        unsigned flag_contacts=0,fpg_contacts=0,cleared_levels=0;int furthest_x=0;
        uint64_t total_frames=0,successful_frames=0;
        SmbTaskConfig task={seed,0,horizon,SMB_TASK_FPG,0,1};uint32_t pick=smb_seed(seed,0);
        for(int index=0;index<episodes;index++) {
            unsigned scene=eligible[smb_random(&pick)%eligible.size()];Nes_State snapshot;
            if(new_game)emulator.load_state(rom.starts[0]->state);
            else {
                fpt_load_snapshot(&snapshot,(root/"states"/(std::to_string(scene)+".state")).string());
                emulator.load_state(snapshot);
            }
            SmbLogic real=smb_import_logic(emulator),native=real;
            if(bank)smb_scene_reset(&native,&bank->entries[scene].scene);
            fpt_rom_require(fpt_rom_difference(native,real)<0,"ROM/native initial snapshots disagree");
            SmbEpisode a={},b={};fpt_policy_reset(native_policy);fpt_policy_reset(rom_policy);
            FptObservationHistory nh={},rh={};
            uint32_t ar=smb_seed(seed,index+1),br=ar;std::string failure;int bad_field=-1;
            int start_x=fpt_x(real.ram,0),max_x=start_x;
            bool touched_flag=false,touched_fpg=false,level_clear=false;
            auto after=[&](const SmbLogic& s,SmbEpisode* ep) {
                if(!clear)return smb_task_after_frame(&s,ep,&task);
                // A flag touch is not yet a completed level. Continue through
                // the ROM's finish sequence until its stage/world advances.
                ep->frames++;const auto* m=s.ram;
                if(m[0x75f]!=0||m[0x75c]!=0||m[0x770]==2)ep->status=SMB_EPISODE_SUCCESS;
                else if(m[0xe]==11||m[0x770]==3)ep->status=SMB_EPISODE_DEAD;
                else if(ep->frames>=horizon)ep->status=SMB_EPISODE_TIMEOUT;
                return ep->status==SMB_EPISODE_SUCCESS?1.0f:0.0f;
            };
            for(int t=0;t<horizon;t++) {
                float no[FPT_OBS],ro[FPT_OBS];fpt_observe(&native,world,&nh,no);fpt_observe(&real,world,&rh,ro);
                if(memcmp(no,ro,sizeof(no))){failure="observation";break;}
                const float* nl=fpt_policy_logits(native_policy,no);const float* rl=fpt_policy_logits(rom_policy,ro);
                if(memcmp(nl,rl,65*sizeof(float))){failure="policy_logits";break;}
                int na=choose(nl,&ar,deterministic),ra=choose(rl,&br,deterministic);
                if(na!=ra){failure="policy_action";break;}
                if(!(t%16))traces<<index<<','<<t<<','<<fpt_x(real.ram,0)<<','<<fpt_y(real.ram,0)<<','
                    <<fpt_signed(real.ram[0x57])<<','<<fpt_signed(real.ram[0x9f])<<','<<(int)real.ram[0x75f]+1<<','
                    <<(int)real.ram[0x75c]+1<<','<<(int)real.ram[0x74e]<<','<<(int)real.ram[0xe]<<','<<ra<<'\n';
                fpt_rom_require(!smb_native_frame(&native,world,smb_action_buttons(na)),"native simulation fault");
                retro_check(emulator.emulate_skip_frame_fast(smb_action_buttons(ra),0));real=smb_import_logic(emulator);total_frames++;
                bad_field=fpt_rom_difference(native,real);if(bad_field>=0){failure="native_state";break;}
                const auto* m=real.ram;
                touched_flag|=m[0xe]==4||m[0xe]==5;
                touched_fpg|=m[0xe]==5&&m[0x70f]>=162&&m[0xd4]==48;
                level_clear|=m[0x75f]!=0||m[0x75c]!=0||m[0x770]==2;
                if(m[0x75f]==0&&m[0x75c]==0&&m[0x74e]==1)max_x=std::max(max_x,fpt_x(m,0));
                float nr=after(native,&a),rr=after(real,&b);
                if(nr!=rr||a.status!=b.status){failure="outcome";break;}
                if(a.status)break;
            }
            episodes_run++;completed+=failure.empty()&&a.status!=SMB_EPISODE_ACTIVE&&b.status!=SMB_EPISODE_ACTIVE;
            mismatches+=!failure.empty();wins_native+=a.status==SMB_EPISODE_SUCCESS;wins_rom+=b.status==SMB_EPISODE_SUCCESS;
            normal+=b.status==SMB_EPISODE_NORMAL_FLAG;deaths+=b.status==SMB_EPISODE_DEAD;timeouts+=b.status==SMB_EPISODE_TIMEOUT;
            if(b.status==SMB_EPISODE_SUCCESS)successful_frames+=b.frames;
            flag_contacts+=touched_flag;fpg_contacts+=touched_fpg;cleared_levels+=level_clear;furthest_x=std::max(furthest_x,max_x);
            cases<<"{\"episode\":"<<index<<",\"scene\":"<<scene<<",\"frames\":"<<b.frames<<",\"native_status\":"<<a.status
                <<",\"rom_status\":"<<b.status<<",\"failure\":\""<<failure<<"\",\"field\":"<<bad_field
                <<",\"start_x\":"<<start_x<<",\"max_overworld_x\":"<<max_x<<",\"final_x\":"<<fpt_x(real.ram,0)
                <<",\"touched_flag\":"<<(touched_flag?"true":"false")<<",\"fpg\":"<<(touched_fpg?"true":"false")
                <<",\"level_clear\":"<<(level_clear?"true":"false")<<"}\n";
            if(!failure.empty())break;
        }
        cases.close();fpt_rom_require(bool(cases),"cannot write transfer episodes");
        traces.close();fpt_rom_require(bool(traces),"cannot write transfer trajectories");
        bool complete=completed==(unsigned)episodes,passed=!mismatches&&complete;
        std::ofstream report(out/"summary.json");
        report<<"{\"schema\":2,\"scope\":\""<<(new_game?"natural_1_1_new_game":"natural_1_1_starts")
            <<"\",\"objective\":\""<<(clear?"level_transition":"fpg")<<"\",\"reference_frames\":"<<reference_frames
            <<",\"eligible_starts\":"<<eligible.size()<<",\"episodes_requested\":"<<episodes
            <<",\"episodes_run\":"<<episodes_run<<",\"episodes_completed\":"<<completed<<",\"seed\":"<<seed
            <<",\"max_frames\":"<<horizon<<",\"checkpoint_fnv64\":\""<<fpg_time_file_hash(argv[1])
            <<"\",\"config_fnv64\":\""<<fpg_time_file_hash(argv[2])<<"\",\"bank_payload_fnv64\":\""<<(bank?bank->header.payload_hash:0)<<"\""
            <<",\"deterministic\":"<<deterministic<<",\"policy_inference\":\"shared FP32 CPU implementation with independent recurrent states\""
            <<",\"frames\":"<<total_frames<<",\"native_successes\":"<<wins_native<<",\"rom_successes\":"<<wins_rom
            <<",\"normal_flags\":"<<normal<<",\"deaths\":"<<deaths<<",\"timeouts\":"<<timeouts
            <<",\"flag_contacts\":"<<flag_contacts<<",\"fpg_contacts\":"<<fpg_contacts<<",\"level_clears\":"<<cleared_levels
            <<",\"furthest_overworld_x\":"<<furthest_x
            <<",\"mean_success_frames\":"<<(wins_rom?(double)successful_frames/wins_rom:0)<<",\"mismatches\":"<<mismatches
            <<",\"ram_writes_after_reset\":0,\"all_episodes_run_to_terminal\":"<<(complete?"true":"false")
            <<",\"passed\":"<<(passed?"true":"false")<<"}\n";
        report.close();fpt_rom_require(bool(report),"cannot write transfer summary");
        printf("episodes=%d frames=%llu sim_successes=%u rom_successes=%u normal_flags=%u deaths=%u timeouts=%u mismatches=%u\n",
            episodes,(unsigned long long)total_frames,wins_native,wins_rom,normal,deaths,timeouts,mismatches);
        fpt_policy_free(native_policy);fpt_policy_free(rom_policy);puf_ini_free(&ini);return passed?0:1;
    }catch(const std::exception& e){fprintf(stderr,"sim2rom: %s\n",e.what());return 1;}
}
