// Bounded encounter bank: natural ROM snapshots, no terrain/RAM interventions.
// Reuse reference routes and optionally harvest frozen policies in memory.
#include "reference_sources.h"
#include "rom_check.h"
#include "observation.h"
#include "policy.h"
#include "practice.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

struct Candidate {Source source;int progress;};
static bool useful(const SmbLogic& s,bool allow_entry=false) {
    const auto* m=s.ram;int y=fpt_y(m,0);
    return gameplay(m)&&m[0xe]==8&&fpt_x(m,0)>=(allow_entry?40:128)&&y>=32&&y<208
        &&(m[0x74e]==0?(y<176):m[0x1d]==0);
}
static int action(const float* logits,uint32_t* rng) {
    float peak=logits[0],sum=0,weights[64];
    for(int k=0;k<64;k++){fpt_rom_require(std::isfinite(logits[k]),"invalid policy output");peak=std::max(peak,logits[k]);}
    for(int k=0;k<64;k++){weights[k]=expf(logits[k]-peak);sum+=weights[k];}
    double draw=((double)smb_random(rng)+.5)/4294967296.0*sum;
    for(int k=0;k<64;k++){draw-=weights[k];if(draw<=0)return smb_action_buttons(k);}return smb_action_buttons(63);
}
int main(int argc,char** argv) {
    try {
        if(argc<2)throw std::runtime_error("usage: build_practice_bank OUTPUT [CONFIG MODEL ...]");
        fpt_rom_require(argc==2||argc>=4,"supply both config and model");
        std::filesystem::path out=argv[1];
        fpt_rom_require(!std::filesystem::exists(out),"practice output already exists");
        std::filesystem::create_directories(out/"states");std::filesystem::create_directories(out/"routes");
        const std::set<int> targets={0,5,14,15,19,22,23,25,27,28,29,30,31};
        RetroRom& rom=retro_rom();retro_load_rom_locked(rom,RETRO_SMB1_ROM_PATH);
        Nes_Emu emu;retro_check(emu.set_cart(&rom.cart,&rom.seed));emu.set_rom_blocks(false);emu.set_idle_skip(true);
        std::map<int,std::vector<Candidate>> candidates;
        auto originals=collect(rom,emu,false);
        for(auto& source:originals)if(targets.count(source.stage)&&source.frame>0&&useful(source.scene.initial)) {
            int progress=fpt_x(source.scene.initial.ram,0)-40;
            candidates[source.stage].push_back({std::move(source),progress});
        }
        std::ofstream provenance(out/"routes.jsonl");unsigned route_id=0;
        if(argc>=4) {
            Ini ini={};puf_ini_load_file(&ini,argv[2]);
            int hidden=(int)puf_ini_get(&ini,"policy","hidden_size"),layers=(int)puf_ini_get(&ini,"policy","num_layers");
            for(int model=3;model<argc;model++) {
                void* policy=fpt_policy_load(argv[model],hidden,layers);fpt_rom_require(policy,"cannot load practice policy");
                for(int stage:targets) {
                    emu.load_state(rom.starts[stage]->state);fpt_policy_reset(policy);FptObservationHistory history={};
                    uint32_t rng=smb_seed(811+(unsigned)(model-3),stage+1);std::vector<uint8_t> tape;
                    SmbGameConfig config;SmbGameProgress progress={};SmbEpisode episode={};
                    auto initial=smb_import_logic(emu);progress.stage=stage;progress.mode=initial.ram[0x770];
                    progress.lives=initial.ram[0x75a];progress.routine=initial.ram[0xe];smb_game_track_progress(&initial,&progress,&config);
                    std::set<unsigned> bins;unsigned captured=0;
                    for(int t=0;t<6000;t++) {
                        auto state=smb_import_logic(emu);const auto* m=state.ram;
                        if(m[0x75f]*4+m[0x75c]!=stage||m[0x770]!=1||m[0xe]==11||progress.deaths)break;
                        unsigned bin=((unsigned)m[0xe7]+256*m[0xe8])*64u+(unsigned)fpt_x(m,0)/64;
                        if(t>0&&captured<64&&useful(state)&&!bins.count(bin)) {
                            bins.insert(bin);captured++;Source source;source.stage=stage;source.frame=t;
                            source.scene={};source.scene.initial=state;source.name="policy_route_"+std::to_string(route_id);
                            source.snapshot=std::make_unique<Nes_State>();emu.save_state(source.snapshot.get());
                            candidates[stage].push_back({std::move(source),progress.progress_pixels});
                        }
                        float obs[FPT_OBS];fpt_observe(&state,rom.cart.prg(),&history,obs);
                        int buttons=action(fpt_policy_logits(policy,obs),&rng);tape.push_back((uint8_t)buttons);
                        retro_check(emu.emulate_skip_frame_fast(buttons,0));state=smb_import_logic(emu);
                        smb_game_after_frame(&state,&episode,&progress,&config);
                    }
                    std::ofstream route(out/"routes"/(std::to_string(route_id)+".actions"),std::ios::binary);
                    route.write((const char*)tape.data(),tape.size());route.close();fpt_rom_require(bool(route),"cannot write compact action route");
                    provenance<<"{\"route\":"<<route_id<<",\"model\":\""<<argv[model]<<"\",\"stage\":"<<stage
                        <<",\"seed\":"<<811+model-3<<",\"frames\":"<<tape.size()<<",\"candidates\":"<<captured<<"}\n";
                    printf("practice route=%u stage=%d-%d frames=%zu candidates=%u\n",route_id,stage/4+1,stage%4+1,tape.size(),captured);fflush(stdout);route_id++;
                }
                fpt_policy_free(policy);
            }
            puf_ini_free(&ini);
        }
        std::vector<SmbBankEntry> entries;std::ofstream metadata(out/"states.jsonl");
        std::ofstream clips(out/"validation.bin",std::ios::binary);
        SmbTraceHeader trace={0,SMB_TRACE_VERSION,sizeof(SmbScene),sizeof(SmbTraceCase),sizeof(SmbTraceFrame),0,0,1,0x6e01246e5d215cb3ull};
        clips.write((const char*)&trace,sizeof(trace));clips.write((const char*)rom.cart.prg(),SMB_PRG);
        uint64_t checked_frames=0;unsigned stages=0;
        for(int stage:targets) {
            auto& pool=candidates[stage];
            if(pool.size()<8) {
                // Some policies die at the first obstacle (notably 4-4).
                // Natural entry phases are honest coverage of that obstacle;
                // never invent a later-room state or label it solved.
                emu.load_state(rom.starts[stage]->state);
                for(int t=0;t<256&&pool.size()<8;t++) {
                    auto state=smb_import_logic(emu);
                    if(t>=8&&t%8==0&&useful(state,true)) {
                        Source source;source.stage=stage;source.frame=t;source.scene={};source.scene.initial=state;
                        source.name="idle_entry_phase";source.snapshot=std::make_unique<Nes_State>();emu.save_state(source.snapshot.get());
                        pool.push_back({std::move(source),0});
                    }
                    retro_check(emu.emulate_skip_frame_fast(0,0));
                }
            }
            if(pool.size()<8)throw std::runtime_error("stage "+std::to_string(stage)+" lacks eight safe natural approach states");
            std::stable_sort(pool.begin(),pool.end(),[](const Candidate& a,const Candidate& b){return a.progress>b.progress;});
            // Four ordered approach bands, two actual states per band. This is
            // progress ordering, not a claim of known optimal remaining time.
            for(int band=0;band<4;band++)for(int variant=0;variant<2;variant++) {
                size_t offset=std::min(pool.size()-1,(size_t)(band*2+variant)*pool.size()/8);
                const auto& candidate=pool[offset];const auto& source=candidate.source;
                unsigned index=entries.size();SmbBankEntry entry={};entry.scene=source.scene;entry.stage=stage;
                entry.source_frame=source.frame;entry.source_id=index;entry.flags=SMB_BANK_PRACTICE|(band<<SMB_PRACTICE_BAND_SHIFT);
                SmbLogic native;fpt_rom_require(!smb_scene_reset(&native,&entry.scene),"invalid practice reset");
                emu.load_state(*source.snapshot);auto real=smb_import_logic(emu);
                fpt_rom_require(fpt_rom_difference(native,real)<0,"practice ROM reset mismatch");
                SmbTraceCase c={};c.scene=entry.scene;c.frames=16;c.stage=stage;c.source_frame=source.frame;c.seed=index;c.kind=16;
                clips.write((const char*)&c,sizeof(c));
                for(int t=0;t<240;t++) {
                    int buttons=explorer(source.frame+t,variant);
                    fpt_rom_require(!smb_native_frame(&native,rom.cart.prg(),buttons),"practice native fault");
                    retro_check(emu.emulate_skip_frame_fast(buttons,0));real=smb_import_logic(emu);checked_frames++;
                    fpt_rom_require(fpt_rom_difference(native,real)<0,"practice rollout differs from ROM");
                    if(t<16){auto frame=fpt_rom_record(real,buttons);clips.write((const char*)&frame,sizeof(frame));}
                }
                fpt_save_snapshot(*source.snapshot,(out/"states"/(std::to_string(index)+".state")).string());
                const auto* m=entry.scene.initial.ram;
                metadata<<"{\"scene\":"<<index<<",\"stage\":"<<stage<<",\"band\":"<<band<<",\"source\":\""<<source.name
                    <<"\",\"frame\":"<<source.frame<<",\"progress_rank\":"<<candidate.progress<<",\"x\":"<<fpt_x(m,0)
                    <<",\"y\":"<<fpt_y(m,0)<<",\"area_type\":"<<(int)m[0x74e]<<",\"area_pointer\":"<<m[0xe7]+256*m[0xe8]<<"}\n";
                entries.push_back(entry);trace.cases++;trace.frames+=16;
            }
            stages++;
        }
        trace.magic=SMB_TRACE_MAGIC;clips.seekp(0);clips.write((const char*)&trace,sizeof(trace));clips.close();
        metadata.close();provenance.close();fpt_rom_require(bool(clips)&&bool(metadata)&&bool(provenance),"cannot publish practice records");
        SmbBankHeader h={SMB_BANK_MAGIC,SMB_BANK_VERSION,sizeof(SmbScene),sizeof(SmbBankEntry),(uint32_t)entries.size(),1,trace.rom_fingerprint,1469598103934665603ull};
        h.payload_hash=smb_bank_hash(h.payload_hash,rom.cart.prg(),SMB_PRG);
        h.payload_hash=smb_bank_hash(h.payload_hash,entries.data(),entries.size()*sizeof(SmbBankEntry));
        std::ofstream bank(out/"bank.bin.tmp",std::ios::binary);bank.write((const char*)&h,sizeof(h));bank.write((const char*)rom.cart.prg(),SMB_PRG);
        bank.write((const char*)entries.data(),entries.size()*sizeof(SmbBankEntry));bank.close();fpt_rom_require(bool(bank),"cannot write practice bank");
        uintmax_t bytes=0;for(const auto& f:std::filesystem::recursive_directory_iterator(out))if(f.is_regular_file())bytes+=f.file_size();
        fpt_rom_require(bytes<16*1024*1024,"practice artifacts exceed 16 MiB storage cap");
        std::filesystem::rename(out/"bank.bin.tmp",out/"bank.bin");
        std::ofstream report(out/"summary.json");report<<"{\"stages\":"<<stages<<",\"states\":"<<entries.size()
            <<",\"CPU_ROM_frames\":"<<checked_frames<<",\"CUDA_reference_frames\":"<<trace.frames<<",\"bytes\":"<<bytes
            <<",\"synthetic_RAM_edits\":0,\"guaranteed_full_level_solutions\":false,\"CUDA_validation_required\":true}\n";
        printf("practice states=%zu stages=%u bytes=%ju CPU_ROM_frames=%llu\n",entries.size(),stages,bytes,(unsigned long long)checked_frames);return 0;
    }catch(const std::exception& e){fprintf(stderr,"practice bank: %s\n",e.what());return 1;}
}
