// Bounded, deliberately approximate rollout search. No optimality claim.
#include "table.h"
#include "curriculum.h"
#include "logic_cpu.h"
#include "trace.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>

static void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
struct Teacher {SmbTraceCase item;std::vector<SmbTraceFrame> frames;};
static std::vector<Teacher> read_teachers(const char* path,const SmbBank& bank) {
    std::ifstream in(path,std::ios::binary);SmbTraceHeader h={};
    require(bool(in.read((char*)&h,sizeof(h)))&&h.magic==SMB_TRACE_MAGIC&&h.version==SMB_TRACE_VERSION
        &&h.scene_size==sizeof(SmbScene)&&h.case_size==sizeof(SmbTraceCase)&&h.frame_size==sizeof(SmbTraceFrame)
        &&h.cases>0&&h.cases<=65536&&h.rom_fingerprint==bank.header.rom_fingerprint,"invalid teacher trace header");
    std::vector<uint8_t> world(SMB_PRG);require(bool(in.read((char*)world.data(),SMB_PRG))
        &&!memcmp(world.data(),bank.worlds.data(),SMB_PRG),"teacher world differs from reset bank");
    uint64_t frames=0;std::vector<Teacher> result;
    for(unsigned i=0;i<h.cases;i++) {
        Teacher t;require(bool(in.read((char*)&t.item,sizeof(t.item)))&&t.item.frames>0&&t.item.frames<=1000000
            &&t.item.seed<bank.entries.size(),"invalid teacher case");
        require(!memcmp(&t.item.scene,&bank.entries[t.item.seed].scene,sizeof(SmbScene)),"teacher reset differs from bank");
        t.frames.resize(t.item.frames);require(bool(in.read((char*)t.frames.data(),t.frames.size()*sizeof(SmbTraceFrame))),"truncated teacher trace");
        frames+=t.frames.size();result.push_back(std::move(t));
    }
    require(frames==h.frames&&in.peek()==EOF,"invalid teacher frame count or trailing data");return result;
}

// Four useful controls, with a seeded jump window. Search proposals constrain
// only this offline estimate; the learner always retains all 64 actions.
static int plan_buttons(int frame,int plan,int delay,int hold) {
    if(!plan)return 130; // right + run
    if(plan==1)return 131; // right + run + jump
    if(plan==2)return 130|((frame%48<24)?1:0);
    return 130|((frame>=delay&&frame<delay+hold)?1:0);
}
int main(int argc,char** argv) {
    try {
        std::map<std::string,std::string> args;
        for(int i=1;i<argc;i++) {
            std::string key=argv[i];if(key=="--refresh"){args[key]="1";continue;}
            require(i+1<argc,"missing option value");args[key]=argv[++i];
        }
        auto str=[&](const char* key,const char* value){auto it=args.find(key);return it==args.end()?std::string(value):it->second;};
        auto integer=[&](const char* key,int value,int low,int high){std::string raw=str(key,std::to_string(value).c_str());size_t used=0;
            int n=std::stoi(raw,&used);require(used==raw.size()&&n>=low&&n<=high,"invalid search option");return n;};
        for(const auto& kv:args)require(kv.first=="--bank"||kv.first=="--teacher"||kv.first=="--output"
            ||kv.first=="--cpu"||kv.first=="--module"||kv.first=="--seed"||kv.first=="--variants"
            ||kv.first=="--plans"||kv.first=="--horizon"||kv.first=="--refresh","unknown search option");
        std::string bank_path=str("--bank","build/mario_sim/fpg/curriculum/bank.bin");
        std::string teacher_path=str("--teacher","build/mario_sim/fpg/curriculum/teacher/clips.bin");
        std::string cpu=str("--cpu","build/mario_sim/runtime/logic_cpu.a"),cuda=str("--module","build/mario_sim/runtime/cuda_replay.cubin");
        std::string output=str("--output","build/mario_sim/fpg/curriculum_targets.bin");
        SmbBank bank(bank_path.c_str());fpt_require_natural_bank(bank);
        unsigned seed=integer("--seed",73,1,2147483647),variants=integer("--variants",0,0,256);
        unsigned plans=integer("--plans",0,0,256),horizon=integer("--horizon",1800,1,1000000);
        bool all_teachers=std::all_of(bank.entries.begin(),bank.entries.end(),[](const SmbBankEntry& e){return bool(e.flags&SMB_BANK_FPG_TEACHER);});
        require(!all_teachers||(!variants&&!plans),"the backward curriculum uses recorded suffix lengths; keep variants=0 and plans=0");
        auto begin=std::chrono::steady_clock::now();
        FpgTimeHeader h={FPG_TIME_MAGIC,FPG_TIME_VERSION,sizeof(FpgTimeEntry),(uint32_t)bank.entries.size(),
            seed,variants,plans,horizon,bank.header.payload_hash,fpg_time_file_hash(cpu.c_str()),fpg_time_file_hash(cuda.c_str()),0};
        if(!args.count("--refresh")&&std::filesystem::exists(output)) {
            try {FpgTimeTable old(output.c_str(),bank,cpu.c_str(),cuda.c_str());auto& o=old.header;
                if(o.seed==seed&&o.variants==variants&&o.plans==plans&&o.horizon==horizon) {
                    printf("{\"cache_hit\":true,\"entries\":%u,\"optimality_proven\":false}\n",h.entries);return 0;
                }
            }catch(const std::exception& e){fprintf(stderr,"rebuilding timing table: %s\n",e.what());}
        }
        auto teachers=read_teachers(teacher_path.c_str(),bank);
        std::vector<FpgTimeEntry> estimates(bank.entries.size());
        std::vector<std::vector<int>> best_tapes(bank.entries.size());
        struct Witness {uint32_t world=0,seed=0,knobs=0;};std::vector<Witness> witnesses(bank.entries.size());
        uint64_t simulated=0;unsigned teacher_successes=0;
        auto record=[&](unsigned scene,int frames,const std::vector<int>& tape,Witness witness) {
            auto& e=estimates[scene];e.trials++;
            if(frames) {e.successes++;if(!e.best_frames||(unsigned)frames<e.best_frames) {
                e.best_frames=frames;best_tapes[scene]=tape;best_tapes[scene].resize(frames);witnesses[scene]=witness;
            }}
        };
        SmbTaskConfig task={seed,0,(int)horizon,SMB_TASK_FPG,0,1};
        for(const auto& t:teachers) {
            SmbLogic state;require(!smb_scene_reset(&state,&t.item.scene),"teacher reset fault");SmbEpisode e={};
            SmbTaskConfig control=task;control.max_frames=(int)t.frames.size();std::vector<int> tape;
            for(const auto& f:t.frames) {
                require(!smb_native_frame(&state,bank.worlds.data(),f.buttons),"teacher simulation fault");simulated++;tape.push_back(f.buttons);
                require(!memcmp(state.ram,f.ram,SMB_RAM)&&state.pc==f.pc&&state.a==f.a&&state.x==f.x&&state.y==f.y
                    &&state.p==f.p&&state.sp==f.sp&&state.timing.timestamp==f.timestamp&&state.timing.video_frame==f.video_frame,
                    "teacher reference mismatch");
                smb_task_after_frame(&state,&e,&control);if(e.status)break;
            }
            require(e.status==SMB_EPISODE_SUCCESS&&e.frames==(int)t.frames.size(),"teacher no longer reaches FPG");
            record(t.item.seed,e.frames,tape,{});teacher_successes++;
        }
        auto eligible=bank.select(task);
        for(unsigned scene:eligible)for(unsigned v=0;v<variants;v++) {
            uint32_t reset_seed=smb_seed(seed,scene*257+v),rng=reset_seed;
            unsigned world=smb_random(&rng)%bank.header.worlds;uint32_t generate_seed=rng;
            // Additional search rounds vary controller plans, never the saved
            // ROM state. The bank itself supplies natural gameplay variation.
            SmbLogic initial;require(!smb_generate(&initial,&bank.entries[scene].scene,&rng,0),"sample reset fault");
            for(unsigned p=0;p<plans;p++) {
                SmbLogic state=initial;SmbEpisode e={};std::vector<int> tape;
                int delay=(int)(smb_random(&rng)%97),hold=1+(int)(smb_random(&rng)%40);
                for(unsigned f=0;f<horizon;f++) {
                    int buttons=plan_buttons((int)f,(int)p,delay,hold);tape.push_back(buttons);
                    require(!smb_native_frame(&state,bank.worlds.data()+(size_t)world*SMB_PRG,buttons),"search simulation fault");simulated++;
                    smb_task_after_frame(&state,&e,&task);if(e.status)break;
                }
                record(scene,e.status==SMB_EPISODE_SUCCESS?e.frames:0,tape,{world,generate_seed,0});
            }
        }
        unsigned covered=0,trials=0,successes=0;for(unsigned i:eligible) {
            covered+=estimates[i].best_frames!=0;trials+=estimates[i].trials;successes+=estimates[i].successes;
        }
        h.payload_hash=smb_bank_hash(1469598103934665603ull,estimates.data(),estimates.size()*sizeof(FpgTimeEntry));
        std::filesystem::path path(output);if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
        std::string temporary=output+".tmp";std::ofstream out(temporary,std::ios::binary);
        out.write((char*)&h,sizeof(h));out.write((char*)estimates.data(),estimates.size()*sizeof(FpgTimeEntry));out.close();
        require(bool(out),"cannot write timing table");std::filesystem::rename(temporary,output);
        double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
        std::ofstream report(output+".json");
        report<<"{\"schema\":1,\"optimality_proven\":false,\"cache_hit\":false,\"templates\":"<<eligible.size()
            <<",\"covered\":"<<covered<<",\"fallback_templates\":"<<eligible.size()-covered
            <<",\"teacher_successes\":"<<teacher_successes<<",\"trials\":"<<trials<<",\"successful_trials\":"<<successes
            <<",\"simulated_frames\":"<<simulated<<",\"seconds\":"<<seconds<<",\"seed\":"<<seed
            <<",\"variants\":"<<variants<<",\"plans\":"<<plans<<",\"search_horizon\":"<<horizon
            <<",\"generation_knobs\":0,\"bank_hash\":"<<h.bank_hash<<",\"cpu_hash\":"<<h.cpu_hash
            <<",\"cuda_hash\":"<<h.cuda_hash<<",\"payload_hash\":"<<h.payload_hash<<",\"witnesses\":[";
        bool comma=false;for(unsigned i:eligible)if(estimates[i].best_frames) {
            if(comma)report<<',';comma=true;auto w=witnesses[i];
            report<<"{\"scene\":"<<i<<",\"frames\":"<<estimates[i].best_frames<<",\"world\":"<<w.world
                <<",\"generation_seed\":"<<w.seed<<",\"knobs\":"<<w.knobs<<",\"buttons\":[";
            for(size_t j=0;j<best_tapes[i].size();j++){if(j)report<<',';report<<best_tapes[i][j];}report<<"]}";
        }
        report<<"]}\n";report.close();require(bool(report),"cannot write timing table report");
        printf("{\"cache_hit\":false,\"templates\":%zu,\"covered\":%u,\"fallback_templates\":%zu,\"teacher_successes\":%u,"
            "\"trials\":%u,\"successful_trials\":%u,\"simulated_frames\":%llu,\"seconds\":%.3f,\"optimality_proven\":false}\n",
            eligible.size(),covered,eligible.size()-covered,teacher_successes,trials,successes,(unsigned long long)simulated,seconds);
        return 0;
    }catch(const std::exception& e){fprintf(stderr,"FPG timing table: %s\n",e.what());return 1;}
}
