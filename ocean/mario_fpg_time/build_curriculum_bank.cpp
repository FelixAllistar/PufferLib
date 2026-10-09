// Replay the successful ROM trajectory and save progressively earlier starts.
#include "../mario_sim/reference_sources.h"
#include "curriculum.h"
#include "rom_check.h"
#include <algorithm>

struct CurriculumStart {
    SmbBankEntry entry={};
    std::unique_ptr<Nes_State> snapshot;
    int offset,remaining;
};
static void curriculum_finish_trace(std::ofstream& out,SmbTraceHeader* h) {
    fpt_rom_require(h->cases&&h->frames,"empty curriculum trace");h->magic=SMB_TRACE_MAGIC;
    out.seekp(0);out.write((char*)h,sizeof(*h));out.close();fpt_rom_require(bool(out),"cannot finish curriculum trace");
}
int main(int argc,char** argv) {
    try {
        if(argc<2||argc>4)throw std::runtime_error("usage: build_curriculum_bank OUTPUT [PIPE_DIRECTORY=build/mario_fpg_time/pipe] [SEED=73]");
        std::filesystem::path out=argv[1],pipe=argc>2?argv[2]:"build/mario_fpg_time/pipe";
        uint32_t seed=argc>3?(uint32_t)std::stoul(argv[3]):73;fpt_rom_require(seed!=0,"invalid curriculum seed");
        SmbBank source((pipe/"bank.bin").c_str());fpt_require_pipe_bank(source);
        std::ifstream in(pipe/"teacher/clips.bin",std::ios::binary);SmbTraceHeader ih={};SmbTraceCase witness={};
        fpt_rom_require(bool(in.read((char*)&ih,sizeof(ih)))&&ih.magic==SMB_TRACE_MAGIC&&ih.version==SMB_TRACE_VERSION
            &&ih.scene_size==sizeof(SmbScene)&&ih.case_size==sizeof(SmbTraceCase)&&ih.frame_size==sizeof(SmbTraceFrame)
            &&ih.cases==1&&ih.rom_fingerprint==source.header.rom_fingerprint,"invalid canonical pipe teacher");
        std::vector<uint8_t> world(SMB_PRG);fpt_rom_require(bool(in.read((char*)world.data(),world.size()))
            &&world==source.worlds,"teacher ROM data mismatch");
        fpt_rom_require(bool(in.read((char*)&witness,sizeof(witness)))&&witness.seed<source.entries.size()
            &&witness.frames==425&&!memcmp(&witness.scene,&source.entries[witness.seed].scene,sizeof(SmbScene)),"invalid canonical pipe witness");
        std::vector<SmbTraceFrame> frames(witness.frames);
        fpt_rom_require(bool(in.read((char*)frames.data(),frames.size()*sizeof(SmbTraceFrame)))&&in.peek()==EOF,"truncated pipe witness");
        RetroRom& rom=retro_rom();retro_load_rom_locked(rom,RETRO_SMB1_ROM_PATH);
        fpt_rom_require(!memcmp(rom.cart.prg(),world.data(),SMB_PRG),"pipe data differs from original ROM");
        Nes_Emu e;retro_check(e.set_cart(&rom.cart,&rom.seed));e.set_rom_blocks(false);e.set_idle_skip(true);
        Nes_State anchor;fpt_load_snapshot(&anchor,(pipe/"states"/(std::to_string(witness.seed)+".state")).string());e.load_state(anchor);
        SmbLogic native;smb_scene_reset(&native,&witness.scene);fpt_rom_require(fpt_rom_difference(native,smb_import_logic(e))<0,"pipe snapshot mismatch");
        SmbEpisode episode={};SmbTaskConfig task={seed,0,1800,SMB_TASK_FPG,0,1};std::vector<CurriculumStart> starts;
        for(unsigned t=0;t<frames.size();t++) {
            int remaining=(int)frames.size()-(int)t;
            if(remaining>=3) {
                CurriculumStart p;p.offset=t;p.remaining=remaining;p.entry.scene.initial=smb_import_logic(e);
                p.entry.source_id=0;p.entry.source_frame=source.entries[witness.seed].source_frame+t;
                p.entry.flags=SMB_BANK_FLAGPOLE|SMB_BANK_FPG_TEACHER;
                const auto* m=p.entry.scene.initial.ram;
                for(int k=0;k<6;k++)if(m[15+k]&&m[15+k]<128)p.entry.actor_mask|=1ull<<(m[22+k]&63);
                p.snapshot=std::make_unique<Nes_State>();e.save_state(p.snapshot.get());starts.push_back(std::move(p));
            }
            int buttons=frames[t].buttons;
            fpt_rom_require(!smb_native_frame(&native,world.data(),buttons),"curriculum teacher native fault");retro_check(e.emulate_skip_frame_fast(buttons,0));
            auto real=smb_import_logic(e);fpt_rom_require(fpt_rom_difference(native,real)<0&&!memcmp(real.ram,frames[t].ram,SMB_RAM),"curriculum teacher ROM mismatch");
            smb_task_after_frame(&native,&episode,&task);
            fpt_rom_require(!episode.status||t+1==frames.size(),"curriculum witness ended early");
        }
        fpt_rom_require(episode.status==SMB_EPISODE_SUCCESS,"curriculum witness failed FPG");
        std::filesystem::create_directories(out/"states");std::filesystem::create_directories(out/"teacher");
        std::filesystem::create_directories(out/"validation");
        SmbTraceHeader vh={0,SMB_TRACE_VERSION,sizeof(SmbScene),sizeof(SmbTraceCase),sizeof(SmbTraceFrame),0,0,1,rom.fingerprint},th=vh;
        std::ofstream validation(out/"validation/clips.bin",std::ios::binary),teacher(out/"teacher/clips.bin",std::ios::binary),metadata(out/"starts.jsonl");
        validation.write((char*)&vh,sizeof(vh));validation.write((char*)world.data(),SMB_PRG);
        teacher.write((char*)&th,sizeof(th));teacher.write((char*)world.data(),SMB_PRG);
        std::vector<SmbBankEntry> entries;
        for(unsigned i=0;i<starts.size();i++) {
            auto& p=starts[i];entries.push_back(p.entry);const auto* m=p.entry.scene.initial.ram;
            std::string path=(out/"states"/(std::to_string(i)+".state")).string();fpt_save_snapshot(*p.snapshot,path);
            Nes_State restored;fpt_load_snapshot(&restored,path);e.load_state(restored);
            fpt_rom_require(fpt_rom_difference(p.entry.scene.initial,smb_import_logic(e))<0,"serialized curriculum reset mismatch");
            metadata<<"{\"scene\":"<<i<<",\"reference_frames\":"<<p.remaining<<",\"route_frame\":"<<p.entry.source_frame
                <<",\"x\":"<<m[0x6d]*256+m[0x86]<<",\"y\":"<<(int)m[0xce]<<",\"grounded\":"<<(m[0x1d]?"false":"true")
                <<",\"ram_edits\":0,\"source\":\"canonical_rom_fpg_trajectory\"}\n";
            SmbTraceCase tc={};tc.scene=p.entry.scene;tc.frames=p.remaining;tc.stage=0;tc.source_frame=p.entry.source_frame;tc.kind=32;tc.seed=i;
            teacher.write((char*)&tc,sizeof(tc));teacher.write((char*)(frames.data()+p.offset),(size_t)p.remaining*sizeof(SmbTraceFrame));
            th.cases++;th.frames+=p.remaining;
            for(int script=0;script<2;script++) {
                e.load_state(restored);SmbLogic s;smb_scene_reset(&s,&p.entry.scene);SmbTraceCase c=tc;c.frames=std::min(240,std::max(32,p.remaining*2));
                uint32_t rng=smb_seed(seed,i*2+script);std::vector<SmbTraceFrame> actual;
                for(unsigned t=0;t<c.frames;t++) {
                    int buttons=script?(130|((t%48<24)?1:0)):smb_action_buttons((int)(smb_random(&rng)%64));
                    fpt_rom_require(!smb_native_frame(&s,world.data(),buttons),"curriculum random validation native fault");
                    retro_check(e.emulate_skip_frame_fast(buttons,0));auto real=smb_import_logic(e);int field=fpt_rom_difference(s,real);
                    if(field>=0){fprintf(stderr,"curriculum mismatch scene=%u script=%d frame=%u field=%x\n",i,script,t,field);throw std::runtime_error("curriculum sim/ROM validation failed");}
                    actual.push_back(fpt_rom_record(real,buttons));
                }
                validation.write((char*)&c,sizeof(c));validation.write((char*)actual.data(),actual.size()*sizeof(SmbTraceFrame));vh.cases++;vh.frames+=c.frames;
            }
            if(!(i%64)){printf("validated %u/%zu natural curriculum starts\n",i+1,starts.size());fflush(stdout);}
        }
        curriculum_finish_trace(validation,&vh);curriculum_finish_trace(teacher,&th);metadata.close();fpt_rom_require(bool(metadata),"cannot write curriculum metadata");
        SmbBankHeader h={SMB_BANK_MAGIC,SMB_BANK_VERSION,sizeof(SmbScene),sizeof(SmbBankEntry),(uint32_t)entries.size(),1,rom.fingerprint,1469598103934665603ull};
        h.payload_hash=smb_bank_hash(h.payload_hash,world.data(),world.size());h.payload_hash=smb_bank_hash(h.payload_hash,entries.data(),entries.size()*sizeof(SmbBankEntry));
        std::ofstream bank(out/"bank.bin.tmp",std::ios::binary);bank.write((char*)&h,sizeof(h));bank.write((char*)world.data(),world.size());
        bank.write((char*)entries.data(),entries.size()*sizeof(SmbBankEntry));bank.close();fpt_rom_require(bool(bank),"cannot write curriculum bank");
        std::filesystem::rename(out/"bank.bin.tmp",out/"bank.bin");SmbBank check((out/"bank.bin").c_str());fpt_require_natural_bank(check);
        std::ofstream summary(out/"summary.json");
        summary<<"{\"schema\":1,\"scope\":\"natural_1_1_backward_curriculum\",\"scenes\":"<<entries.size()
            <<",\"worlds\":1,\"minimum_reference_frames\":3,\"maximum_reference_frames\":425,\"ram_edits\":0,\"world_data_edits\":0"
            <<",\"grounded_starts_only\":false,\"first_level_grounded\":true,\"first_level_reference_frames\":3"
            <<",\"teacher_successes\":"<<th.cases<<",\"teacher_replay_frames\":"<<th.frames
            <<",\"validation_cases\":"<<vh.cases<<",\"validation_frames\":"<<vh.frames
            <<",\"cpu_rom_mismatches\":0,\"serialized_rom_resets_checked\":"<<entries.size()<<",\"seed\":"<<seed<<"}\n";
        summary.close();fpt_rom_require(bool(summary),"cannot write curriculum summary");
        printf("curriculum scenes=%zu reference_frames=3..425 validation_frames=%u mismatches=0\n",entries.size(),vh.frames);return 0;
    }catch(const std::exception& ex){fprintf(stderr,"curriculum bank: %s\n",ex.what());return 1;}
}
