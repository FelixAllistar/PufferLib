// Reach the pipe exit by actual ROM inputs, then retain complete natural states.
#include "reference_sources.h"
#include "pipe_start.h"
#include "rom_check.h"
#include <algorithm>
#include <set>

struct PipeStart {
    std::unique_ptr<Nes_State> snapshot;
    SmbBankEntry entry={};
    std::vector<int> branch,teacher;
    int route,exit_frame;
};
static std::vector<int> pipe_tape(const char* path,bool suffix=false) {
    std::ifstream in(path);fpt_rom_require(bool(in),"missing pipe-exit action tape");
    std::vector<int> result;int a;
    while(in>>a) {
        fpt_rom_require(a>=0&&a<(suffix?12:64),"invalid pipe action");
        result.push_back(suffix?((a%3==1?128:a%3==2?64:0)|(((a/3)&1)?1:0)|(a>=6?2:0))
                               :((a&3)|((a&60)<<2)));
    }
    fpt_rom_require(in.eof()&&!result.empty(),"invalid pipe tape text");return result;
}
static void finish_trace(std::ofstream& out,SmbTraceHeader* h) {
    fpt_rom_require(h->cases&&h->frames,"empty pipe qualification trace");h->magic=SMB_TRACE_MAGIC;
    out.seekp(0);out.write((char*)h,sizeof(*h));out.close();fpt_rom_require(bool(out),"cannot finish pipe trace");
}
int main(int argc,char** argv) {
    try {
        if(argc<2||argc>4)throw std::runtime_error("usage: build_pipe_bank OUTPUT [BRANCHES_PER_ROUTE=24] [SEED=73]");
        std::filesystem::path out=argv[1];int branches=argc>2?std::stoi(argv[2]):24;
        uint32_t seed=argc>3?(uint32_t)std::stoul(argv[3]):73;
        fpt_rom_require(branches>=0&&branches<=256&&seed,"invalid pipe-bank parameters");
        std::filesystem::create_directories(out/"states");std::filesystem::create_directories(out/"teacher");
        std::filesystem::create_directories(out/"validation");
        RetroRom& rom=retro_rom();retro_load_rom_locked(rom,RETRO_SMB1_ROM_PATH);retro_prepare_start_locked(rom,1,1);
        Nes_Emu e;retro_check(e.set_cart(&rom.cart,&rom.seed));e.set_rom_blocks(false);e.set_idle_skip(true);
        std::vector<uint8_t> world(rom.cart.prg(),rom.cart.prg()+SMB_PRG);
        const char* paths[]={"ocean/mario_sim/reference/learned_pipe_route.actions","ocean/mario_sim/reference/routes/episode_7.actions",
            "ocean/mario_sim/reference/routes/episode_26.actions","ocean/mario_sim/reference/routes/episode_41.actions"};
        auto successful=pipe_tape(paths[0]);fpt_rom_require(successful.size()>=1510,"short successful prefix");successful.resize(1510);
        auto suffix=pipe_tape("ocean/mario_sim/reference/fpg_suffix.actions",true);successful.insert(successful.end(),suffix.begin(),suffix.end());
        std::vector<PipeStart> starts;std::set<uint64_t> seen;std::vector<int> exits;
        auto capture=[&](int route,int exit_frame,const std::vector<int>& branch,const std::vector<int>& teacher) {
            SmbLogic s=smb_import_logic(e);if(!fpt_is_pipe_start(&s))return false;
            uint64_t hash=smb_bank_hash(1469598103934665603ull,&s,sizeof(s));if(!seen.insert(hash).second)return false;
            PipeStart p;p.snapshot=std::make_unique<Nes_State>();e.save_state(p.snapshot.get());p.entry.scene.initial=s;
            p.entry.stage=0;p.entry.source_id=route;p.entry.source_frame=exit_frame+(unsigned)branch.size();
            // Task eligibility refers to the flag ahead, which is not loaded yet.
            p.entry.flags=SMB_BANK_FLAGPOLE|(teacher.empty()?0:SMB_BANK_FPG_TEACHER);
            for(int k=0;k<6;k++)if(s.ram[15+k]&&s.ram[15+k]<128)p.entry.actor_mask|=1ull<<(s.ram[22+k]&63);
            p.route=route;p.exit_frame=exit_frame;p.branch=branch;p.teacher=teacher;starts.push_back(std::move(p));return true;
        };
        for(int route=0;route<4;route++) {
            auto tape=pipe_tape(paths[route]);e.load_state(rom.starts[0]->state);
            bool underground=false;int exit_frame=-1;
            for(int t=0;t<(int)tape.size();t++) {
                const auto* m=e.low_mem();underground|=m[0x74e]==2;
                if(underground&&m[0x74e]==1&&m[0xe]==8&&m[0x770]==1&&m[0x772]==3&&m[0x6d]*256+m[0x86]==2616) {
                    exit_frame=t;break;
                }
                retro_check(e.emulate_skip_frame_fast(tape[t],0));
            }
            fpt_rom_require(exit_frame>=0,"recorded route did not reach the 1-1 pipe exit");exits.push_back(exit_frame);
            Nes_State anchor;e.save_state(&anchor);
            // Original continuation states include the complete canonical exit.
            const auto& continuation=route==0?successful:tape;
            std::vector<int> branch;
            for(int t=0;t<=32;t++) {
                if(!(t%8)) {
                    std::vector<int> teacher;if(route==0)teacher.assign(successful.begin()+exit_frame+t,successful.end());
                    capture(route,exit_frame,branch,teacher);
                }
                if(t<32){int buttons=continuation[exit_frame+t];branch.push_back(buttons);retro_check(e.emulate_skip_frame_fast(buttons,0));}
            }
            // Reset variation is produced by normal controller input, never
            // RAM edits. Keep only grounded, controllable states near the pipe.
            uint32_t rng=smb_seed(seed,(unsigned)route);int accepted=0;
            for(int attempt=0;accepted<branches&&attempt<4096;attempt++) {
                e.load_state(anchor);branch.clear();int length=1+(int)(smb_random(&rng)%64),buttons=0;
                for(int t=0;t<length;t++) {
                    if(!(t%8)) {
                        const int choices[]={0,0,0,128,130,64,66,129,131};buttons=choices[smb_random(&rng)%9];
                    }
                    branch.push_back(buttons);retro_check(e.emulate_skip_frame_fast(buttons,0));
                }
                if(capture(route,exit_frame,branch,{}))accepted++;
            }
            fpt_rom_require(accepted==branches,"could not fill the natural pipe-start bank");
            printf("route=%d pipe_exit_frame=%d natural_branches=%d\n",route,exit_frame,accepted);fflush(stdout);
        }
        fpt_rom_require(starts.size()>=4&&starts.size()<=65536,"invalid pipe bank size");
        std::vector<SmbBankEntry> entries;for(const auto& p:starts)entries.push_back(p.entry);
        SmbTraceHeader vh={0,SMB_TRACE_VERSION,sizeof(SmbScene),sizeof(SmbTraceCase),sizeof(SmbTraceFrame),0,0,1,rom.fingerprint},th=vh;
        std::ofstream validation(out/"validation/clips.bin",std::ios::binary),teacher(out/"teacher/clips.bin",std::ios::binary),metadata(out/"starts.jsonl");
        validation.write((char*)&vh,sizeof(vh));validation.write((char*)world.data(),SMB_PRG);
        teacher.write((char*)&th,sizeof(th));teacher.write((char*)world.data(),SMB_PRG);
        int min_x=65536,max_x=0,min_teacher=1000000,max_teacher=0;unsigned teacher_successes=0;
        for(unsigned i=0;i<starts.size();i++) {
            auto& p=starts[i];const auto* m=p.entry.scene.initial.ram;int x=m[0x6d]*256+m[0x86],y=((int)m[0xb5]-1)*256+m[0xce];
            min_x=std::min(min_x,x);max_x=std::max(max_x,x);
            std::string state_path=(out/"states"/(std::to_string(i)+".state")).string();fpt_save_snapshot(*p.snapshot,state_path);
            Nes_State restored;fpt_load_snapshot(&restored,state_path);e.load_state(restored);
            fpt_rom_require(fpt_rom_difference(p.entry.scene.initial,smb_import_logic(e))<0,"serialized ROM reset changed state");
            metadata<<"{\"scene\":"<<i<<",\"route\":"<<p.route<<",\"route_file\":\""<<paths[p.route]<<"\",\"exit_frame\":"<<p.exit_frame
                <<",\"branch_frames\":"<<p.branch.size()<<",\"x\":"<<x<<",\"y\":"<<y<<",\"vx\":"<<(int)(int8_t)m[0x57]
                <<",\"distance_to_pole\":"<<FPT_PIPE_POLE_X-x<<",\"grounded\":true,\"ram_edits\":0,\"branch_buttons\":[";
            for(size_t j=0;j<p.branch.size();j++){if(j)metadata<<',';metadata<<p.branch[j];}metadata<<"]}\n";
            for(int script=0;script<3;script++) {
                e.load_state(restored);SmbLogic s;smb_scene_reset(&s,&p.entry.scene);SmbTraceCase c={};c.scene=p.entry.scene;
                c.stage=0;c.source_frame=p.entry.source_frame;c.kind=16;c.seed=i;uint32_t rng=smb_seed(seed,i*3+script);int buttons=0;
                std::vector<SmbTraceFrame> frames;
                for(int t=0;t<240;t++) {
                    if(script==0){if(!(t%8))buttons=smb_action_buttons((int)(smb_random(&rng)%64));}
                    else buttons=script==1?130:(130|((t%48<24)?1:0));
                    fpt_rom_require(!smb_native_frame(&s,world.data(),buttons),"native pipe validation fault");
                    retro_check(e.emulate_skip_frame_fast(buttons,0));auto r=smb_import_logic(e);int field=fpt_rom_difference(s,r);
                    if(field>=0){fprintf(stderr,"pipe mismatch scene=%u script=%d frame=%d field=%x\n",i,script,t,field);throw std::runtime_error("pipe sim/ROM validation failed");}
                    frames.push_back(fpt_rom_record(r,buttons));
                }
                c.frames=frames.size();vh.cases++;vh.frames+=c.frames;validation.write((char*)&c,sizeof(c));validation.write((char*)frames.data(),frames.size()*sizeof(SmbTraceFrame));
            }
            if(!p.teacher.empty()) {
                e.load_state(restored);SmbLogic s;smb_scene_reset(&s,&p.entry.scene);SmbEpisode episode={};
                SmbTaskConfig task={seed,0,1800,SMB_TASK_FPG,0,1};std::vector<SmbTraceFrame> frames;
                for(int buttons:p.teacher) {
                    fpt_rom_require(!smb_native_frame(&s,world.data(),buttons),"pipe teacher native fault");retro_check(e.emulate_skip_frame_fast(buttons,0));
                    auto r=smb_import_logic(e);fpt_rom_require(fpt_rom_difference(s,r)<0,"pipe teacher sim/ROM mismatch");
                    frames.push_back(fpt_rom_record(r,buttons));smb_task_after_frame(&s,&episode,&task);if(episode.status)break;
                }
                fpt_rom_require(episode.status==SMB_EPISODE_SUCCESS&&frames.size()>100,"far pipe teacher did not complete FPG");
                SmbTraceCase c={};c.scene=p.entry.scene;c.frames=frames.size();c.stage=0;c.source_frame=p.entry.source_frame;c.kind=16;c.seed=i;
                teacher.write((char*)&c,sizeof(c));teacher.write((char*)frames.data(),frames.size()*sizeof(SmbTraceFrame));
                th.cases++;th.frames+=c.frames;teacher_successes++;min_teacher=std::min(min_teacher,(int)c.frames);max_teacher=std::max(max_teacher,(int)c.frames);
            }
        }
        finish_trace(validation,&vh);finish_trace(teacher,&th);metadata.close();fpt_rom_require(bool(metadata),"cannot write pipe metadata");
        SmbBankHeader h={SMB_BANK_MAGIC,SMB_BANK_VERSION,sizeof(SmbScene),sizeof(SmbBankEntry),(uint32_t)entries.size(),1,rom.fingerprint,1469598103934665603ull};
        h.payload_hash=smb_bank_hash(h.payload_hash,world.data(),world.size());h.payload_hash=smb_bank_hash(h.payload_hash,entries.data(),entries.size()*sizeof(SmbBankEntry));
        std::ofstream bank(out/"bank.bin.tmp",std::ios::binary);bank.write((char*)&h,sizeof(h));bank.write((char*)world.data(),world.size());
        bank.write((char*)entries.data(),entries.size()*sizeof(SmbBankEntry));bank.close();fpt_rom_require(bool(bank),"cannot write pipe bank");
        std::filesystem::rename(out/"bank.bin.tmp",out/"bank.bin");
        std::ofstream summary(out/"summary.json");
        summary<<"{\"schema\":1,\"scope\":\"natural_1_1_pipe_exit\",\"scenes\":"<<entries.size()<<",\"worlds\":1,\"routes\":4,\"seed\":"<<seed
            <<",\"branches_per_route\":"<<branches<<",\"ram_edits\":0,\"world_data_edits\":0,\"grounded_starts_only\":true,\"min_x\":"<<min_x
            <<",\"max_x\":"<<max_x<<",\"pole_x\":"<<FPT_PIPE_POLE_X<<",\"minimum_distance\":"<<FPT_PIPE_POLE_X-max_x
            <<",\"validation_cases\":"<<vh.cases<<",\"validation_frames\":"<<vh.frames<<",\"teacher_successes\":"<<teacher_successes
            <<",\"teacher_frames\":"<<th.frames<<",\"teacher_min_frames\":"<<min_teacher<<",\"teacher_max_frames\":"<<max_teacher
            <<",\"cpu_rom_mismatches\":0,\"serialized_rom_resets_checked\":"<<entries.size()<<",\"bank_hash\":"<<h.payload_hash<<"}\n";
        summary.close();fpt_rom_require(bool(summary),"cannot write pipe summary");
        printf("scenes=%zu x=%d..%d minimum_pole_distance=%d teacher_successes=%u teacher_frames=%u validation_frames=%u mismatches=0\n",
            entries.size(),min_x,max_x,FPT_PIPE_POLE_X-max_x,teacher_successes,th.frames,vh.frames);return 0;
    }catch(const std::exception& e){fprintf(stderr,"pipe bank: %s\n",e.what());return 1;}
}
