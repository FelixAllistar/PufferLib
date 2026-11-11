// Bounded encounter bank: natural ROM snapshots, no terrain interventions.
// Reuse reference routes and optionally harvest frozen policies in memory.
// --route MOVIE.fm2 adds states along a reference movie for all 32 stages; those are taken as small Mario
// (a two-byte RAM edit, flagged SMB_BANK_EDITED) when the movie's Mario is big or fire.
#include "reference_sources.h"
#include "rom_check.h"
#include "observation.h"
#include "policy.h"
#include "practice.h"
#include "route_movie.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

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
// --per-stage N (N != 8): up to N states per stage spread over observed progress, at most two per
// (area, 64-px) bin preferring distinct sources; the band flag is the progress quartile so the
// legacy loader validation still holds. N == 8 (the default) keeps the original two-per-band output.
static std::vector<const Candidate*> spread(const std::vector<Candidate>& pool,size_t limit) {
    auto bin_of=[](const Candidate& c){const auto* m=c.source.scene.initial.ram;return ((unsigned)m[0xe7]+256*m[0xe8])*64u+(unsigned)fpt_x(m,0)/64;};
    std::map<unsigned,std::vector<const Candidate*>> bins;std::set<const Candidate*> taken;
    // Pool order is descending progress: a bin keeps its best candidate, then the best one from another source,
    // and only then a second candidate from the same source.
    for(const auto& c:pool) {
        auto& bin=bins[bin_of(c)];
        if(bin.empty()||(bin.size()==1&&bin[0]->source.name!=c.source.name)){bin.push_back(&c);taken.insert(&c);}
    }
    for(const auto& c:pool) {
        auto& bin=bins[bin_of(c)];
        if(bin.size()<2&&!taken.count(&c)){bin.push_back(&c);taken.insert(&c);}
    }
    std::vector<const Candidate*> kept;for(const auto& c:pool)if(taken.count(&c))kept.push_back(&c);
    if(kept.size()<=limit)return kept;
    std::vector<const Candidate*> chosen;
    for(size_t k=0;k<limit;k++)chosen.push_back(kept[k*kept.size()/limit]);
    return chosen;
}
// --route: replay the movie from power-on on a standalone core exactly as ocean/retro/tas_replay.cpp does
// (TAS_OFFSET=-2, emulate_frame(mask, 0)). After each frame a tracked (smb_game_tracked) and useful state is a
// candidate; the first one of each (stage, area pointer, x/64) bin is kept unless its (stage, area pointer, x/256)
// section already holds four. A kept state that is not small Mario is snapshotted from a copy of the core with
// $0754=1, $0756=0 written, and the replay continues from the untouched state (tas_states.cpp, TAS_SMALL=1).
// A loop-back the movie itself takes, found by smb_game_track_progress along the replay: stage, area key, the last
// tracked x before the loop and the frame it fired on.
struct RouteLoop {int stage;uint32_t key;int x,frame;};
struct RouteCandidate {
    int frame,stage,data,size,status;bool edited;SmbLogic state;std::unique_ptr<Nes_State> snapshot;
};
// Candidates between a movie loop-back and the next untracked frame are dropped (their x is the thrown-back one).
// --dense: in each listed stage, every tracked frame f with (f - f0) % dense_step == 0 (f0 = the stage's first
// tracked frame) is also a dense candidate, with no on-ground, position or after-loop filter, snapshotted as small
// Mario by the same edit rule. Dense candidates go to *dense in movie order.
static void route_snapshot(Nes_Emu& tas,const SmbLogic& state,RouteCandidate* c) {
    const auto* m=state.ram;
    c->size=m[0x754];c->status=m[0x756];c->edited=m[0x754]!=1||m[0x756]!=0;c->snapshot=std::make_unique<Nes_State>();
    if(c->edited) {
        // load_state clears the core's error count, so check it before every restore as well as at the end.
        fpt_rom_require(tas.error_count()==0,"route movie hit an unsupported opcode");
        auto keep=std::make_unique<Nes_State>();tas.save_state(keep.get());
        uint8_t* w=tas.low_mem();w[0x754]=1;w[0x756]=0;
        c->state=smb_import_logic(tas);tas.save_state(c->snapshot.get());
        tas.load_state(*keep);retro_bind_pixels(&tas);
    } else {c->state=state;tas.save_state(c->snapshot.get());}
}
static std::vector<RouteCandidate> route_candidates(RetroRom& rom,const std::string& path,std::map<int,std::vector<int>>* areas,
        std::vector<RouteLoop>* loops,int* dropped_after_loop,const std::set<int>& dense_stages,int dense_step,
        std::vector<RouteCandidate>* dense) {
    auto masks=smb_movie_masks(path,-2);fpt_rom_require(!masks.empty(),"empty route movie");
    Nes_Emu tas;retro_check(tas.set_cart(&rom.cart));retro_bind_pixels(&tas);
    std::vector<RouteCandidate> kept;std::set<uint64_t> bins;std::map<uint64_t,int> sections;
    bool was_dead=false,after_loop=false;int deaths=0;SmbGameConfig config;SmbGameProgress progress={};
    std::map<int,size_t> dense_first;
    for(size_t f=0;f<masks.size();f++) {
        retro_check(tas.emulate_frame(masks[f],0));
        auto state=smb_import_logic(tas);const auto* m=state.ram;
        bool dead=m[0xe]==0x0b||m[0xb5]>1;        // tas_replay's death predicate
        if(f>0&&dead&&!was_dead)deaths++;
        was_dead=dead;
        int loops_before=progress.maze_loops,x_before=progress.raw_x;
        smb_game_track_progress(&state,&progress,&config);
        if(progress.maze_loops>loops_before) {
            loops->push_back({m[0x75f]*4+m[0x75c],progress.last_key,x_before,(int)f});after_loop=true;
        }
        if(!smb_game_tracked(m)){after_loop=false;continue;}
        int stage=m[0x75f]*4+m[0x75c],data=m[0xe7]+256*m[0xe8],x=fpt_x(m,0);
        auto& sequence=(*areas)[stage];if(sequence.empty()||sequence.back()!=data)sequence.push_back(data);
        if(dense_stages.count(stage)) {
            if(!dense_first.count(stage))dense_first[stage]=f;
            if((f-dense_first[stage])%(size_t)dense_step==0) {
                RouteCandidate c;c.frame=(int)f;c.stage=stage;c.data=data;route_snapshot(tas,state,&c);dense->push_back(std::move(c));
            }
        }
        if(!useful(state))continue;
        uint64_t area=((uint64_t)stage<<16)|(uint64_t)data,bin=(area<<32)|(uint64_t)(x/64),section=(area<<32)|(uint64_t)(x/256);
        if(bins.count(bin)||sections[section]>=4)continue;
        if(after_loop){(*dropped_after_loop)++;continue;}
        bins.insert(bin);sections[section]++;
        RouteCandidate c;c.frame=(int)f;c.stage=stage;c.data=data;route_snapshot(tas,state,&c);
        kept.push_back(std::move(c));
    }
    const auto* m=tas.low_mem();
    printf("route movie: %zu frames, end %d-%d mode=%d x=%d deaths=%d errors=%ld candidates=%zu\n",masks.size(),m[0x75f]+1,m[0x75c]+1,
        m[0x770],m[0x6d]*256+m[0x86],deaths,(long)tas.error_count(),kept.size());
    if(!dense_stages.empty())printf("dense candidates: %zu (step %d)\n",dense->size(),dense_step);
    fpt_rom_require(m[0x75f]==7&&m[0x75c]==3&&m[0x770]==2&&deaths==0&&tas.error_count()==0,"route movie does not finish 8-4 cleanly (desync?)");
    return kept;
}
static std::string area_name(int data) {char b[8];snprintf(b,sizeof(b),"%04x",data);return b;}
int main(int raw_argc,char** raw_argv) {
    try {
        std::vector<char*> args;int per_stage=8,dense_step=4;bool dense_step_given=false;std::string route_movie;std::set<int> dense_stages;
        for(int i=0;i<raw_argc;i++) {
            if(!strcmp(raw_argv[i],"--per-stage")) {
                fpt_rom_require(i+1<raw_argc,"--per-stage needs a count");per_stage=atoi(raw_argv[++i]);
                fpt_rom_require(per_stage>=4&&per_stage<=256,"--per-stage must be 4..256");
            } else if(!strcmp(raw_argv[i],"--route")) {
                fpt_rom_require(i+1<raw_argc,"--route needs a movie");route_movie=raw_argv[++i];
            } else if(!strcmp(raw_argv[i],"--dense")) {
                fpt_rom_require(i+1<raw_argc,"--dense needs stages such as 4-4,7-4,8-4");
                std::stringstream list(raw_argv[++i]);std::string item;
                while(std::getline(list,item,',')) {
                    int world=0,level=0;char dash=0;std::stringstream one(item);
                    fpt_rom_require(bool(one>>world>>dash>>level)&&dash=='-'&&world>=1&&world<=8&&level>=1&&level<=4&&one.peek()==EOF,
                        "--dense stages must look like 4-4,7-4,8-4");
                    dense_stages.insert((world-1)*4+level-1);
                }
                fpt_rom_require(!dense_stages.empty(),"--dense needs at least one stage");
            } else if(!strcmp(raw_argv[i],"--dense-step")) {
                fpt_rom_require(i+1<raw_argc,"--dense-step needs a count");dense_step=atoi(raw_argv[++i]);dense_step_given=true;
                fpt_rom_require(dense_step>=1&&dense_step<=64,"--dense-step must be 1..64");
            } else args.push_back(raw_argv[i]);
        }
        fpt_rom_require(dense_stages.empty()||!route_movie.empty(),"--dense requires --route");
        fpt_rom_require(!dense_step_given||!dense_stages.empty(),"--dense-step requires --dense");
        int argc=(int)args.size();char** argv=args.data();
        if(argc<2)throw std::runtime_error("usage: build_practice_bank OUTPUT [--per-stage N] [--route MOVIE.fm2 [--dense STAGES [--dense-step N]]] [CONFIG MODEL ...]");
        fpt_rom_require(argc==2||argc>=4,"supply both config and model");
        std::filesystem::path out=argv[1];
        fpt_rom_require(!std::filesystem::exists(out),"practice output already exists");
        std::filesystem::create_directories(out/"states");std::filesystem::create_directories(out/"routes");
        const std::set<int> targets={0,5,14,15,19,22,23,25,27,28,29,30,31};
        RetroRom& rom=retro_rom();retro_load_rom_locked(rom,RETRO_SMB1_ROM_PATH);
        Nes_Emu emu;retro_check(emu.set_cart(&rom.cart,&rom.seed));emu.set_rom_blocks(false);emu.set_idle_skip(true);
        std::map<int,std::vector<int>> route_areas;std::vector<RouteCandidate> route;std::vector<RouteLoop> route_loops;
        int dropped_after_loop=0;std::vector<RouteCandidate> dense;
        if(!route_movie.empty()) {
            route=route_candidates(rom,route_movie,&route_areas,&route_loops,&dropped_after_loop,dense_stages,dense_step,&dense);
            printf("route loops (%zu):",route_loops.size());
            for(const auto& l:route_loops)printf(" %d-%d:%04x x=%d frame=%d",l.stage/4+1,l.stage%4+1,l.key&0xffff,l.x,l.frame);
            printf("; candidates dropped between a loop and the next untracked frame: %d\n",dropped_after_loop);
        }
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
        uint64_t checked_frames=0;unsigned stages=0;std::ostringstream per_stage_report;
        // Per-state checks: native reset equals the ROM snapshot, then 240 frames of native-vs-ROM rollout. The
        // 16-frame validation clip is returned in `clip`; `why` names the first failed check.
        auto check=[&](const SmbScene& scene,const Nes_State& snapshot,int frame,int variant,std::vector<SmbTraceFrame>* clip,const char** why) {
            clip->clear();SmbLogic native;
            if(smb_scene_reset(&native,&scene)){*why="invalid practice reset";return false;}
            emu.load_state(snapshot);auto real=smb_import_logic(emu);
            if(fpt_rom_difference(native,real)>=0){*why="practice ROM reset mismatch";return false;}
            for(int t=0;t<240;t++) {
                int buttons=explorer(frame+t,variant);
                if(smb_native_frame(&native,rom.cart.prg(),buttons)){*why="practice native fault";return false;}
                retro_check(emu.emulate_skip_frame_fast(buttons,0));real=smb_import_logic(emu);checked_frames++;
                if(fpt_rom_difference(native,real)>=0){*why="practice rollout differs from ROM";return false;}
                if(t<16)clip->push_back(fpt_rom_record(real,buttons));
            }
            return true;
        };
        std::vector<SmbTraceFrame> clip;
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
            std::vector<std::pair<const Candidate*,int>> picks;
            if(per_stage==8) {
                for(int band=0;band<4;band++)for(int variant=0;variant<2;variant++)
                    picks.push_back({&pool[std::min(pool.size()-1,(size_t)(band*2+variant)*pool.size()/8)],band});
            } else {
                auto chosen=spread(pool,(size_t)per_stage);
                fpt_rom_require(chosen.size()>=4,"stage lacks four distinct approach states");
                for(size_t k=0;k<chosen.size();k++)picks.push_back({chosen[k],(int)(k*4/chosen.size())});
            }
            per_stage_report<<(stages?",":"")<<"\""<<stage/4+1<<"-"<<stage%4+1<<"\":{\"candidates\":"<<pool.size()<<",\"states\":"<<picks.size()<<"}";
            for(size_t pick=0;pick<picks.size();pick++) {
                int band=picks[pick].second,variant=(int)(pick%2);
                const auto& candidate=*picks[pick].first;const auto& source=candidate.source;
                unsigned index=entries.size();SmbBankEntry entry={};entry.scene=source.scene;entry.stage=stage;
                entry.source_frame=source.frame;entry.source_id=index;entry.flags=SMB_BANK_PRACTICE|(band<<SMB_PRACTICE_BAND_SHIFT);
                const char* why="";
                if(!check(entry.scene,*source.snapshot,source.frame,variant,&clip,&why))throw std::runtime_error(why);
                SmbTraceCase c={};c.scene=entry.scene;c.frames=16;c.stage=stage;c.source_frame=source.frame;c.seed=index;c.kind=16;
                clips.write((const char*)&c,sizeof(c));clips.write((const char*)clip.data(),clip.size()*sizeof(SmbTraceFrame));
                fpt_save_snapshot(*source.snapshot,(out/"states"/(std::to_string(index)+".state")).string());
                const auto* m=entry.scene.initial.ram;
                metadata<<"{\"scene\":"<<index<<",\"stage\":"<<stage<<",\"band\":"<<band<<",\"source\":\""<<source.name
                    <<"\",\"frame\":"<<source.frame<<",\"progress_rank\":"<<candidate.progress<<",\"x\":"<<fpt_x(m,0)
                    <<",\"y\":"<<fpt_y(m,0)<<",\"area_type\":"<<(int)m[0x74e]<<",\"area_pointer\":"<<m[0xe7]+256*m[0xe8]<<"}\n";
                entries.push_back(entry);trace.cases++;trace.frames+=16;
            }
            stages++;
        }
        // Route entries, appended after the picks above (not counted toward --per-stage). Every state goes
        // through the same checks; a failing one is dropped and counted, more than 5% dropped is fatal.
        size_t route_dropped=0,route_edited=0;std::map<int,int> route_counts;std::vector<std::pair<uint32_t,uint32_t>> route_pairs;
        if(!route_movie.empty()) {
            std::vector<std::vector<SmbTraceFrame>> passed(route.size());std::vector<bool> ok(route.size());
            for(size_t k=0;k<route.size();k++) {
                SmbScene scene={};scene.initial=route[k].state;const char* why="";
                ok[k]=check(scene,*route[k].snapshot,route[k].frame,(int)(k%2),&passed[k],&why);
                if(!ok[k]){route_dropped++;printf("route drop: stage=%d-%d frame=%d x=%d edited=%d: %s\n",route[k].stage/4+1,route[k].stage%4+1,
                    route[k].frame,fpt_x(route[k].state.ram,0),(int)route[k].edited,why);}
                else route_counts[route[k].stage]++;
            }
            fpt_rom_require(route_dropped*20<=route.size(),"more than 5% of route states failed the ROM checks");
            // SMB_BANK_ROUTE_LOOP: for each movie loop, the kept route entry of that stage and area with the largest
            // x <= the pre-loop x among entries before the loop frame; it must lie within 512 px of the loop.
            std::vector<bool> loop_flag(route.size());
            for(const auto& l:route_loops) {
                int best=-1;
                for(size_t k=0;k<route.size();k++) {
                    const auto& r=route[k];int x=fpt_x(r.state.ram,0);
                    if(!ok[k]||r.stage!=l.stage||(((uint32_t)r.stage<<16)|(uint32_t)r.data)!=l.key||r.frame>=l.frame||x>l.x)continue;
                    if(best<0||x>fpt_x(route[best].state.ram,0))best=(int)k;
                }
                fpt_rom_require(best>=0&&l.x-fpt_x(route[best].state.ram,0)<=512,"no route entry within 512 px before a movie loop-back");
                loop_flag[best]=true;
                printf("route loop entry: %d-%d:%04x frame=%d x=%d (loop at x=%d)\n",l.stage/4+1,l.stage%4+1,l.key&0xffff,
                    route[best].frame,fpt_x(route[best].state.ram,0),l.x);
            }
            std::map<int,int> position;uint32_t previous_area=0;int previous_stage=-1;
            for(size_t k=0;k<route.size();k++) {
                if(!ok[k])continue;const auto& r=route[k];
                int band=position[r.stage]++*4/route_counts[r.stage];
                unsigned index=entries.size();SmbBankEntry entry={};entry.scene.initial=r.state;entry.stage=r.stage;
                entry.source_frame=r.frame;entry.source_id=index;
                entry.flags=SMB_BANK_PRACTICE|SMB_BANK_ROUTE|(r.edited?SMB_BANK_EDITED:0)|(loop_flag[k]?SMB_BANK_ROUTE_LOOP:0)
                    |(band<<SMB_PRACTICE_BAND_SHIFT);
                SmbTraceCase c={};c.scene=entry.scene;c.frames=16;c.stage=r.stage;c.source_frame=r.frame;c.seed=index;c.kind=16;
                clips.write((const char*)&c,sizeof(c));clips.write((const char*)passed[k].data(),passed[k].size()*sizeof(SmbTraceFrame));
                const auto* m=entry.scene.initial.ram;
                metadata<<"{\"scene\":"<<index<<",\"stage\":"<<r.stage<<",\"band\":"<<band<<",\"source\":\"route_movie\",\"frame\":"<<r.frame
                    <<",\"progress_rank\":"<<fpt_x(m,0)-40<<",\"x\":"<<fpt_x(m,0)<<",\"y\":"<<fpt_y(m,0)<<",\"area_type\":"<<(int)m[0x74e]
                    <<",\"area_pointer\":"<<m[0xe7]+256*m[0xe8]<<",\"edited\":"<<(int)r.edited<<",\"movie_size\":"<<r.size<<",\"movie_status\":"<<r.status<<",\"route_loop\":"<<(int)loop_flag[k]<<"}\n";
                entries.push_back(entry);trace.cases++;trace.frames+=16;route_edited+=r.edited;
                // Route table preview (the runtime builds the same pairs from the bank's route entries).
                uint32_t area=((uint32_t)r.stage<<16)|(uint32_t)r.data;
                if(previous_stage==r.stage&&previous_area!=area
                    &&std::find(route_pairs.begin(),route_pairs.end(),std::make_pair(previous_area,area))==route_pairs.end())
                    route_pairs.push_back({previous_area,area});
                previous_stage=r.stage;previous_area=area;
            }
            for(const auto& [stage,sequence]:route_areas) {
                printf("route areas %d-%d (%d states):",stage/4+1,stage%4+1,route_counts[stage]);
                for(size_t i=0;i<sequence.size();i++)printf("%s%s",i?" -> ":" ",area_name(sequence[i]).c_str());printf("\n");
            }
            printf("route pairs (%zu):",route_pairs.size());
            for(const auto& [a,b]:route_pairs)printf(" %d-%d:%s->%s",(int)(a>>16)/4+1,(int)(a>>16)%4+1,area_name(a&0xffff).c_str(),area_name(b&0xffff).c_str());
            printf("\nroute states=%zu dropped=%zu edited=%zu\n",route.size()-route_dropped,route_dropped,route_edited);
        }
        // Dense entries (--dense), appended after the route entries: same checks and drop rule, flag SMB_BANK_DENSE,
        // band = quartile of the position among the stage's kept dense entries, no states/N.state file.
        size_t dense_dropped=0,dense_edited=0;std::map<int,int> dense_counts;std::map<int,std::pair<int,int>> dense_frames;
        if(!dense.empty()) {
            std::vector<std::vector<SmbTraceFrame>> passed(dense.size());std::vector<bool> ok(dense.size());
            for(size_t k=0;k<dense.size();k++) {
                SmbScene scene={};scene.initial=dense[k].state;const char* why="";
                ok[k]=check(scene,*dense[k].snapshot,dense[k].frame,(int)(k%2),&passed[k],&why);
                if(!ok[k]){dense_dropped++;printf("dense drop: stage=%d-%d frame=%d x=%d y=%d edited=%d: %s\n",dense[k].stage/4+1,dense[k].stage%4+1,
                    dense[k].frame,smb_game_x(dense[k].state.ram),fpt_y(dense[k].state.ram,0),(int)dense[k].edited,why);}
                else {
                    dense_counts[dense[k].stage]++;auto& span=dense_frames[dense[k].stage];
                    if(dense_counts[dense[k].stage]==1)span.first=dense[k].frame;span.second=dense[k].frame;
                }
            }
            fpt_rom_require(dense_dropped*20<=dense.size(),"more than 5% of dense states failed the ROM checks");
            std::map<int,int> position;
            for(size_t k=0;k<dense.size();k++) {
                if(!ok[k])continue;const auto& r=dense[k];
                int band=position[r.stage]++*4/dense_counts[r.stage];
                unsigned index=entries.size();SmbBankEntry entry={};entry.scene.initial=r.state;entry.stage=r.stage;
                entry.source_frame=r.frame;entry.source_id=index;
                entry.flags=SMB_BANK_PRACTICE|SMB_BANK_ROUTE|SMB_BANK_DENSE|(r.edited?SMB_BANK_EDITED:0)|(band<<SMB_PRACTICE_BAND_SHIFT);
                SmbTraceCase c={};c.scene=entry.scene;c.frames=16;c.stage=r.stage;c.source_frame=r.frame;c.seed=index;c.kind=16;
                clips.write((const char*)&c,sizeof(c));clips.write((const char*)passed[k].data(),passed[k].size()*sizeof(SmbTraceFrame));
                const auto* m=entry.scene.initial.ram;
                metadata<<"{\"scene\":"<<index<<",\"stage\":"<<r.stage<<",\"band\":"<<band<<",\"source\":\"route_dense\",\"frame\":"<<r.frame
                    <<",\"x\":"<<smb_game_x(m)<<",\"y\":"<<fpt_y(m,0)<<",\"area_type\":"<<(int)m[0x74e]
                    <<",\"area_pointer\":"<<m[0xe7]+256*m[0xe8]<<",\"edited\":"<<(int)r.edited<<",\"movie_size\":"<<r.size<<",\"movie_status\":"<<r.status<<"}\n";
                entries.push_back(entry);trace.cases++;trace.frames+=16;dense_edited+=r.edited;
            }
            printf("dense states=%zu dropped=%zu edited=%zu step=%d:",dense.size()-dense_dropped,dense_dropped,dense_edited,dense_step);
            for(const auto& [stage,n]:dense_counts)printf(" %d-%d=%d (frames %d..%d)",stage/4+1,stage%4+1,n,dense_frames[stage].first,dense_frames[stage].second);
            printf("\n");
        }
        trace.magic=SMB_TRACE_MAGIC;clips.seekp(0);clips.write((const char*)&trace,sizeof(trace));clips.close();
        metadata.close();provenance.close();fpt_rom_require(bool(clips)&&bool(metadata)&&bool(provenance),"cannot publish practice records");
        SmbBankHeader h={SMB_BANK_MAGIC,SMB_BANK_VERSION,sizeof(SmbScene),sizeof(SmbBankEntry),(uint32_t)entries.size(),1,trace.rom_fingerprint,1469598103934665603ull};
        h.payload_hash=smb_bank_hash(h.payload_hash,rom.cart.prg(),SMB_PRG);
        h.payload_hash=smb_bank_hash(h.payload_hash,entries.data(),entries.size()*sizeof(SmbBankEntry));
        std::ofstream bank(out/"bank.bin.tmp",std::ios::binary);bank.write((const char*)&h,sizeof(h));bank.write((const char*)rom.cart.prg(),SMB_PRG);
        bank.write((const char*)entries.data(),entries.size()*sizeof(SmbBankEntry));bank.close();fpt_rom_require(bool(bank),"cannot write practice bank");
        uintmax_t bytes=0;for(const auto& f:std::filesystem::recursive_directory_iterator(out))if(f.is_regular_file())bytes+=f.file_size();
        if(route_movie.empty())fpt_rom_require(bytes<16*1024*1024,"practice artifacts exceed 16 MiB storage cap");
        else if(dense_stages.empty())fpt_rom_require(bytes<64*1024*1024,"practice artifacts exceed 64 MiB storage cap");
        else fpt_rom_require(bytes<192ull*1024*1024,"practice artifacts exceed 192 MiB storage cap");
        std::filesystem::rename(out/"bank.bin.tmp",out/"bank.bin");
        std::set<uint32_t> bank_stages;for(const auto& e:entries)bank_stages.insert(e.stage);
        if(!route_movie.empty())stages=(unsigned)bank_stages.size();
        std::ostringstream route_report;
        if(!route_movie.empty()) {
            route_report<<"\"route_states\":"<<route.size()-route_dropped<<",\"route_dropped\":"<<route_dropped<<",\"route_stages\":{";
            bool first=true;
            for(const auto& [stage,sequence]:route_areas) {
                route_report<<(first?"":",")<<"\""<<stage/4+1<<"-"<<stage%4+1<<"\":{\"route_states\":"<<route_counts[stage]<<",\"areas\":[";
                for(size_t i=0;i<sequence.size();i++)route_report<<(i?",":"")<<"\""<<area_name(sequence[i])<<"\"";
                route_report<<"]}";first=false;
            }
            route_report<<"},\"route_pairs\":[";
            for(size_t i=0;i<route_pairs.size();i++)route_report<<(i?",":"")<<"\""<<(route_pairs[i].first>>16)/4+1<<"-"<<(route_pairs[i].first>>16)%4+1
                <<":"<<area_name(route_pairs[i].first&0xffff)<<"->"<<area_name(route_pairs[i].second&0xffff)<<"\"";
            route_report<<"],\"route_loops\":[";
            for(size_t i=0;i<route_loops.size();i++)route_report<<(i?",":"")<<"{\"stage\":\""<<route_loops[i].stage/4+1<<"-"<<route_loops[i].stage%4+1
                <<"\",\"area\":\""<<area_name(route_loops[i].key&0xffff)<<"\",\"x\":"<<route_loops[i].x<<",\"frame\":"<<route_loops[i].frame<<"}";
            route_report<<"],\"route_dropped_after_loop\":"<<dropped_after_loop<<",";
            if(!dense_stages.empty()) {
                route_report<<"\"dense_states\":"<<dense.size()-dense_dropped<<",\"dense_dropped\":"<<dense_dropped<<",\"dense_step\":"<<dense_step
                    <<",\"dense_edited\":"<<dense_edited<<",\"dense_stages\":{";
                bool first_dense=true;
                for(int stage:dense_stages) {
                    route_report<<(first_dense?"":",")<<"\""<<stage/4+1<<"-"<<stage%4+1<<"\":{\"dense_states\":"<<dense_counts[stage]
                        <<",\"first_frame\":"<<dense_frames[stage].first<<",\"last_frame\":"<<dense_frames[stage].second<<"}";first_dense=false;
                }
                route_report<<"},";
            }
        }
        std::ofstream report(out/"summary.json");report<<"{\"stages\":"<<stages<<",\"states\":"<<entries.size()
            <<",\"CPU_ROM_frames\":"<<checked_frames<<",\"CUDA_reference_frames\":"<<trace.frames<<",\"bytes\":"<<bytes
            <<",\"synthetic_RAM_edits\":"<<route_edited<<","<<(per_stage==8?std::string():"\"per_stage\":"+std::to_string(per_stage)+",\"stage_counts\":{"+per_stage_report.str()+"},")
            <<route_report.str()<<"\"guaranteed_full_level_solutions\":false,\"CUDA_validation_required\":true}\n";
        printf("practice states=%zu stages=%u bytes=%ju CPU_ROM_frames=%llu\n",entries.size(),stages,bytes,(unsigned long long)checked_frames);return 0;
    }catch(const std::exception& e){fprintf(stderr,"practice bank: %s\n",e.what());return 1;}
}
