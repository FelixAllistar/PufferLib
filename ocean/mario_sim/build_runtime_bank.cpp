#include "reference_sources.h"
#include "bank.h"
#include "generator.h"
#include <set>

static int frame_mismatch(const SmbLogic& s,const SmbLogic& reference) {
    for(int k=0;k<SMB_RAM;k++)if(s.ram[k]!=reference.ram[k])return k;
    if(s.pc!=reference.pc||s.a!=reference.a||s.x!=reference.x||s.y!=reference.y
        ||(s.p&207)!=(reference.p&207)||s.sp!=reference.sp
        ||s.timing.timestamp!=reference.timing.timestamp||s.timing.video_frame!=reference.timing.video_frame
        ||s.timing.control!=reference.timing.control||s.timing.mask!=reference.timing.mask)return SMB_RAM;
    return -1;
}
static SmbTraceFrame record(const SmbLogic& s,int buttons) {
    SmbTraceFrame f={};f.buttons=(uint8_t)buttons;memcpy(f.ram,s.ram,SMB_RAM);
    f.pc=s.pc;f.a=s.a;f.x=s.x;f.y=s.y;f.p=s.p&207;f.sp=s.sp;
    f.control=s.timing.control;f.mask=s.timing.mask;
    f.timestamp=s.timing.timestamp;f.video_frame=s.timing.video_frame;return f;
}
static unsigned template_flags(const SmbScene& scene,int frame) {
    const auto* m=scene.initial.ram;unsigned flags=frame==0?SMB_BANK_STAGE_START:0;
    int x=m[0x6d]*256+m[0x86],last=m[0x725]*16+m[0x726]-((m[0x71f]&3)==0);
    for(int col=last-31;col<=last;col++)if(col*16>=x-16&&col*16<=x+384)
        for(int row=2;row<=14;row++) {
            int raw=m[0x500+((col&16)?0xd0:0)+(row-2)*16+(col&15)];
            if(raw==0x24||raw==0x25)flags|=SMB_BANK_FLAGPOLE;
        }
    return flags;
}

int main(int argc,char** argv) {
    try {
        if(argc<2||argc>4)throw std::runtime_error("usage: build_runtime_bank OUTPUT [WORLDS=16] [SEED=73]");
        int world_count=argc>2?std::stoi(argv[2]):16;
        uint32_t seed=argc>3?(uint32_t)std::stoul(argv[3]):73;
        if(world_count<1||world_count>256||!seed)throw std::runtime_error("invalid bank parameters");
        std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
        RetroRom& rom=retro_rom();retro_load_rom_locked(rom,RETRO_SMB1_ROM_PATH);
        std::vector<uint8_t> original(rom.cart.prg(),rom.cart.prg()+SMB_PRG);
        std::vector<uint8_t> worlds((size_t)world_count*SMB_PRG),code(SMB_PRG);
        std::ifstream code_file("build/mario_sim/runtime/code_mask.bin",std::ios::binary);
        if(!code_file.read((char*)code.data(),code.size())||code_file.peek()!=EOF)
            throw std::runtime_error("missing runtime instruction mask; build runtime first");
        std::vector<int> data_edits(world_count);
        for(int w=0;w<world_count;w++) {
            auto* data=worlds.data()+(size_t)w*SMB_PRG;
            if(!w)memcpy(data,original.data(),SMB_PRG);
            else if(smb_generate_world(data,original.data(),smb_seed(seed,(uint32_t)w))<0)
                throw std::runtime_error("invalid generated world");
            for(int i=0;i<SMB_PRG;i++)if(data[i]!=original[i]) {
                if(code[i])throw std::runtime_error("world generator modified instruction bytes");
                data_edits[w]++;
            }
        }
        Nes_Emu e;retro_check(e.set_cart(&rom.cart,&rom.seed));e.set_rom_blocks(false);e.set_idle_skip(true);
        auto sources=collect(rom,e,false);auto flagpole_sources=collect_flagpole_sources(rom,e);
        for(auto& source:flagpole_sources)sources.push_back(std::move(source));
        std::vector<SmbBankEntry> entries;
        std::set<int> stages;int pole_entries=0,powerup_entries=0;
        for(unsigned i=0;i<sources.size();i++) {
            const auto& source=sources[i];const auto* m=source.scene.initial.ram;
            int y=((int)m[0xb5]-1)*256+m[0xce];
            if(!gameplay(m)||m[0xe]!=8||y>=240)continue;
            SmbBankEntry entry={};entry.scene=source.scene;entry.stage=source.stage;
            entry.source_frame=source.frame;entry.source_id=i;entry.flags=template_flags(entry.scene,source.frame);
            if(source.name=="flagpole_searched")entry.flags|=SMB_BANK_FPG_TEACHER;
            for(int k=0;k<6;k++)if(m[15+k]&&m[15+k]<128)entry.actor_mask|=1ull<<(m[22+k]&63);
            entries.push_back(entry);stages.insert(source.stage);pole_entries+=(entry.flags&SMB_BANK_FLAGPOLE)!=0;
            if(m[0x14]&&m[0x1b]==46)for(int type=0;type<4;type++) {
                SmbBankEntry variant=entry;variant.flags|=SMB_BANK_CONSTRUCTED;
                if(smb_scene_powerup(&variant.scene,type,0))throw std::runtime_error("invalid item template");
                entries.push_back(variant);powerup_entries++;
            }
        }
        if(entries.empty()||stages.size()!=32)throw std::runtime_error("reset bank is missing stage coverage");
        printf("Collected %zu templates across %zu stages (%d flagpole, %d powerup variants).\n",
               entries.size(),stages.size(),pole_entries,powerup_entries);fflush(stdout);
        const int horizon=120;unsigned cases=0,failures=0;uint64_t total_frames=0,actors=0;
        std::ofstream metadata(out/"cases.jsonl");
        for(int w=0;w<world_count;w++) {
            const auto* data=worlds.data()+(size_t)w*SMB_PRG;
            memcpy(rom.cart.prg(),data,SMB_PRG);
            auto directory=out/("world_"+std::to_string(w));std::filesystem::create_directories(directory);
            std::ofstream trace(directory/"clips.bin",std::ios::binary);
            SmbTraceHeader h={0,SMB_TRACE_VERSION,sizeof(SmbScene),sizeof(SmbTraceCase),sizeof(SmbTraceFrame),0,0,1,0x6e01246e5d215cb3ull};
            trace.write((const char*)&h,sizeof(h));trace.write((const char*)data,SMB_PRG);
            unsigned panel_failures=0;
            for(unsigned i=0;i<entries.size();i++)if(i%(unsigned)world_count==(unsigned)w) {
                const auto& entry=entries[i];const auto& source=sources[entry.source_id];
                uint32_t case_seed=smb_seed(seed,i+1),rng=case_seed;
                SmbLogic s;int fault=smb_generate(&s,&entry.scene,&rng,SMB_GEN_ALL);
                if(fault)throw std::runtime_error("generated reset failed");
                e.load_state(*source.snapshot);configure_reference(e,s);
                SmbTraceCase c={};c.scene.initial=s;c.frames=horizon;c.stage=entry.stage;
                c.source_frame=entry.source_frame;c.kind=16;c.seed=case_seed;
                std::vector<SmbTraceFrame> frames;int field=-1,bad_frame=-1,buttons=0;
                for(int t=0;t<horizon;t++) {
                    if(t%8==0) {
                        const int masks[]={0,128,130,131,64,65,66,67,1,16,32,160,144,2,3,129};
                        buttons=masks[smb_random(&rng)%16];
                    }
                    fault=smb_native_frame(&s,data,buttons);
                    retro_check(e.emulate_skip_frame_fast(buttons,0));auto reference=smb_import_logic(e);
                    frames.push_back(record(reference,buttons));total_frames++;
                    field=frame_mismatch(s,reference);
                    if(fault||field>=0){bad_frame=t;break;}
                    for(int k=0;k<6;k++)if(s.ram[15+k]&&s.ram[15+k]<128)actors|=1ull<<(s.ram[22+k]&63);
                }
                bool passed=!fault&&field<0;
                if(!passed){panel_failures++;failures++;if(failures<12)fprintf(stderr,"generated case=%u world=%d frame=%d field=%x fault=%x\n",i,w,bad_frame,field,fault);}
                c.frames=(uint32_t)frames.size();h.cases++;h.frames+=c.frames;cases++;
                trace.write((const char*)&c,sizeof(c));trace.write((const char*)frames.data(),frames.size()*sizeof(SmbTraceFrame));
                metadata<<"{\"case\":"<<i<<",\"world\":"<<w<<",\"stage\":"<<entry.stage
                        <<",\"seed\":"<<case_seed<<",\"source_id\":"<<entry.source_id<<",\"constructed\":true,\"frames\":"<<c.frames
                        <<",\"passed\":"<<(passed?"true":"false")<<",\"field\":"<<field<<",\"bad_frame\":"<<bad_frame<<"}\n";
            }
            if(!panel_failures&&h.cases)h.magic=SMB_TRACE_MAGIC;
            trace.seekp(0);trace.write((const char*)&h,sizeof(h));
            if(!trace)throw std::runtime_error("cannot write generated trace");
            printf("world=%d cases=%u frames=%u failures=%u edits=%d\n",w,h.cases,h.frames,panel_failures,data_edits[w]);fflush(stdout);
        }
        memcpy(rom.cart.prg(),original.data(),SMB_PRG);
        // A positive control uses the actual successful ROM action suffix with
        // no augmentation. Preserve it for testing reward and automatic reset.
        unsigned teacher_successes=0;
        std::filesystem::create_directories(out/"teacher");
        std::ofstream teacher_trace(out/"teacher/clips.bin",std::ios::binary);
        SmbTraceHeader th={0,SMB_TRACE_VERSION,sizeof(SmbScene),sizeof(SmbTraceCase),sizeof(SmbTraceFrame),0,0,1,0x6e01246e5d215cb3ull};
        teacher_trace.write((const char*)&th,sizeof(th));teacher_trace.write((const char*)original.data(),SMB_PRG);
        for(unsigned i=0;i<entries.size();i++)if(entries[i].flags&SMB_BANK_FPG_TEACHER) {
            const auto& entry=entries[i];const auto& source=sources[entry.source_id];
            if(source.teacher.empty()||source.teacher.size()>240)throw std::runtime_error("invalid FPG teacher horizon");
            SmbLogic s;smb_scene_reset(&s,&entry.scene);e.load_state(*source.snapshot);
            SmbTraceCase c={};c.scene=entry.scene;c.stage=0;c.source_frame=entry.source_frame;c.kind=16;c.seed=i;
            std::vector<SmbTraceFrame> frames;bool success=false;
            for(int buttons:source.teacher) {
                int fault=smb_native_frame(&s,original.data(),buttons);retro_check(e.emulate_skip_frame_fast(buttons,0));
                auto reference=smb_import_logic(e);int field=frame_mismatch(s,reference);
                if(fault||field>=0)throw std::runtime_error("FPG teacher diverged from ROM");
                frames.push_back(record(reference,buttons));
                if(s.ram[0xe]==5&&s.ram[0x70f]>=162&&s.ram[0xd4]==48){success=true;break;}
            }
            if(!success)throw std::runtime_error("ROM teacher did not perform flagpole glitch");
            c.frames=(uint32_t)frames.size();teacher_successes++;th.cases++;th.frames+=c.frames;
            teacher_trace.write((const char*)&c,sizeof(c));teacher_trace.write((const char*)frames.data(),frames.size()*sizeof(SmbTraceFrame));
        }
        if(!teacher_successes)throw std::runtime_error("no positive FPG teacher cases");
        th.magic=SMB_TRACE_MAGIC;teacher_trace.seekp(0);teacher_trace.write((const char*)&th,sizeof(th));
        if(!teacher_trace)throw std::runtime_error("cannot write FPG teacher trace");
        std::ofstream summary(out/"summary.json");
        summary<<"{\"scope\":\"generated_reset_and_world_data\",\"seed\":"<<seed<<",\"worlds\":"<<world_count
               <<",\"scenes\":"<<entries.size()<<",\"stages\":32,\"flagpole_scenes\":"<<pole_entries
               <<",\"powerup_variants\":"<<powerup_entries<<",\"cases\":"<<cases<<",\"frames\":"<<total_frames
               <<",\"failures\":"<<failures<<",\"horizon\":"<<horizon<<",\"knobs\":"<<SMB_GEN_ALL
               <<",\"instruction_bytes_modified\":0,\"teacher_successes\":"<<teacher_successes
               <<",\"teacher_frames\":"<<th.frames<<",\"actor_mask\":"<<actors<<",\"data_edits\":[";
        for(int w=0;w<world_count;w++)summary<<(w?",":"")<<data_edits[w];summary<<"]}\n";
        if(failures)return 1;
        SmbBankHeader header={SMB_BANK_MAGIC,SMB_BANK_VERSION,sizeof(SmbScene),sizeof(SmbBankEntry),
                              (uint32_t)entries.size(),(uint32_t)world_count,0x6e01246e5d215cb3ull,1469598103934665603ull};
        header.payload_hash=smb_bank_hash(header.payload_hash,worlds.data(),worlds.size());
        header.payload_hash=smb_bank_hash(header.payload_hash,entries.data(),entries.size()*sizeof(SmbBankEntry));
        std::ofstream bank(out/"bank.bin.tmp",std::ios::binary);
        bank.write((const char*)&header,sizeof(header));bank.write((const char*)worlds.data(),worlds.size());
        bank.write((const char*)entries.data(),entries.size()*sizeof(SmbBankEntry));bank.flush();
        if(!bank)throw std::runtime_error("cannot write reset bank");bank.close();
        std::filesystem::rename(out/"bank.bin.tmp",out/"bank.bin");
        printf("Wrote qualified CPU reset bank; CUDA generated-clip validation is still required.\n");return 0;
    }catch(const std::exception& e){fprintf(stderr,"runtime bank: %s\n",e.what());return 2;}
}
