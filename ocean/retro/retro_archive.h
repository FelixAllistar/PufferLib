#pragma once
// Exact ROM snapshots collected during ordinary play. Nes_State owns internal
// pointers and MUST NEVER be copied; snapshots are immutable shared objects.
#include <atomic>
#include <unordered_map>
// zlib's legacy Byte typedef conflicts with the native trainer's Byte enum.
#define Byte RetroZlibByte
#include <zlib.h>
#undef Byte
#include "nes_emu/abstract_file.h"
#define RETRO_ARCHIVE_AVAILABLE 1
struct RetroArchiveLedger {
    RetroFrontier frontiers[256];
    unsigned int areas[256],rewarded,last_area;
    int frontier_count,area_count,pipe_phase;
};
struct RetroArchiveSnapshot {
    RetroStart start;
    RetroArchiveLedger ledger;
};
struct RetroArchiveCell {
    uint64_t key=0,visits=0,selected=0,failures=0,successes=0,last=0;
    int level=0;
    float depth=0;  // progress into its own level when captured (retro_archive_depth)
    std::shared_ptr<RetroArchiveSnapshot> state;
};
struct RetroArchiveBank {
    std::mutex mutex;
    std::vector<RetroArchiveCell> cells;
    std::unordered_map<uint64_t,size_t> index;
    std::atomic<uint64_t> steps{0},mode_steps[3]{};
    uint64_t budget=1000000000,resets[3]{};
    size_t capacity=1;
    double resumed_phase=0,anneal=.8;
};
static RetroArchiveBank& retro_archive_bank(){static RetroArchiveBank b;return b;}
// Summed frontier x of the snapshot's own level, across that level's areas.
static float retro_archive_depth(const RetroArchiveSnapshot& s,int level) {
    float depth=0;
    for(int i=0;i<s.ledger.frontier_count;i++)
        if((int)(s.ledger.frontiers[i].key>>16)==level) depth+=retro_progress_x(s.ledger.frontiers[i].x);
    return depth;
}
struct RetroArchiveEpisode {
    std::shared_ptr<RetroArchiveSnapshot> state;
    double start=.6,end=.1,anneal=.8,priority=.5,level_focus=0,depth=0,backward=0,lane=0;
    int interval=128,width=128,mode=0,feedback_step=0,feedback_progress=0;
    uint64_t feedback_key=0;
    bool feedback=false;
};
static double retro_archive_phase(const RetroArchiveEpisode& a) {
    auto& b=retro_archive_bank();
    return std::min(1.0,std::max(b.resumed_phase,(double)b.steps.load(std::memory_order_relaxed)/std::max(1.0,b.budget*a.anneal)));
}
static void retro_archive_file(const char* path,bool write,double phase=0);
static void retro_archive_init(Env* e,Dict* cfg) {
    if(!retro_option(cfg,"archive_enabled",0))return;
    if(e->practice||e->terminate_on_clear)throw std::runtime_error("retro archive requires natural-life episodes");
    std::unique_ptr<RetroArchiveEpisode> a(new RetroArchiveEpisode);
    a->start=retro_option(cfg,"archive_start",.6);a->end=retro_option(cfg,"archive_end",.1);
    a->anneal=retro_option(cfg,"archive_anneal_fraction",.8);a->priority=retro_option(cfg,"archive_priority",.5);
    a->level_focus=retro_option(cfg,"archive_level_focus",0);
    a->depth=retro_option(cfg,"archive_depth_priority",0);
    a->backward=retro_option(cfg,"archive_backward",0);
    double interval=retro_option(cfg,"archive_capture_interval",128),width=retro_option(cfg,"archive_cell_width",128);
    double mb=retro_option(cfg,"archive_capacity_mb",128);
    if(!std::isfinite(a->start+a->end+a->anneal+a->priority+a->level_focus+a->depth+a->backward+interval+width+mb)
        ||a->level_focus<0||a->level_focus>1||a->depth<0||a->depth>10||a->backward<0||a->backward>10
        ||a->end<0||a->start<a->end||a->start>1||a->anneal<=0||a->anneal>1
        ||a->priority<0||a->priority>1||interval<1||interval>4096||interval!=floor(interval)
        ||width<16||width>1024||width!=floor(width)||mb<1||mb>1024)
        throw std::runtime_error("retro: invalid archive configuration");
    a->interval=interval;a->width=width;
    a->lane=(retro_random(&e->rng)+.5)/4294967296.0;
    auto& b=retro_archive_bank();
    {
        std::lock_guard<std::mutex> lock(b.mutex);
        b.capacity=std::max(size_t(1),size_t(mb*1024*1024/(sizeof(RetroArchiveSnapshot)+sizeof(RetroArchiveCell)+64)));
        b.anneal=a->anneal;
    }
    e->archive=a.release();
    // Optional starting states for a fresh policy (states do not depend on the policy).
    // Loaded once into an empty bank; its saved schedule phase is ignored.
    DictItem* init=dict_find(cfg,"archive_init_path");
    if(init&&init->str&&strcmp(init->str,"None")) {
        static std::once_flag once;
        std::call_once(once,[&]{
            if(!b.cells.empty())return;
            retro_archive_file(init->str,false);b.resumed_phase=0;
            fprintf(stderr,"[retro] archive_init_path: %zu starting cells from %s\n",b.cells.size(),init->str);
        });
    }
}
static void retro_archive_choose(Env* e,int* id) {
    if(!e->archive)return;
    auto& a=*e->archive;auto& b=retro_archive_bank();double phase=retro_archive_phase(a);
    double archived=a.start+(a.end-a.start)*phase;
    double full=(1-archived)*(.5+.5*phase);
    a.state.reset();a.feedback=false;
    a.mode=a.lane<archived?2:(a.lane<archived+full?1:0);
    std::lock_guard<std::mutex> lock(b.mutex);
    if(a.mode==2&&!b.cells.empty()) {
        // Uniform floor plus failure/uncertainty and staleness. Impossible cells
        // cannot monopolize resets; no hand-authored ordering or mastery gate.
        float deepest[32]={};
        if(a.backward>0)for(auto& c:b.cells)deepest[c.level]=std::max(deepest[c.level],c.depth);
        auto weight=[&](const RetroArchiveCell& c){
            double difficulty=(c.failures+1.0)/(c.failures+c.successes+2.0);
            double novel=1/std::sqrt(c.selected+1.0);
            double stale=std::min(1.0,(double)(b.steps.load()-c.last)/1000000.0);
            // Optional end-first curriculum: early on, cells deep into their level are picked
            // far more often; the bias fades to neutral as the archive phase reaches 1.
            double back=a.backward>0&&deepest[c.level]>0
                ?std::exp(a.backward*(1-phase)*(c.depth/deepest[c.level]-1)):1;
            // Optional depth priority: lower cells (key bits 42-45 hold y/32, 0-7)
            // are picked more often, so drops to lower floors get explored.
            double depth=1+a.depth*std::min(7.0,(double)((c.key>>42)&15))/7;
            return back*depth*(1+a.priority*(3*difficulty+2*novel+stale));
        };
        // Optional level focus: per-cell sampling gives every archived level about
        // the same share, however often it fails. With probability level_focus,
        // first choose a level by its measured failure rate (cell feedback with a
        // one-success/one-failure prior, plus a 0.1 floor), then a cell within it.
        int level=-1;
        if(a.level_focus>0&&(retro_random(&e->rng)+.5)/4294967296.0<a.level_focus) {
            double failures[32]={},trials[32]={},levels[32]={},total=0;
            for(auto& c:b.cells){failures[c.level]+=c.failures;trials[c.level]+=c.failures+c.successes;levels[c.level]=1;}
            for(int l=0;l<32;l++)if(levels[l])total+=levels[l]=.1+(failures[l]+1)/(trials[l]+2);
            double pick=(retro_random(&e->rng)+.5)/4294967296.0*total;
            for(int l=0;l<32;l++)if(levels[l]){level=l;if((pick-=levels[l])<=0)break;}
        }
        double sum=0;for(auto& c:b.cells)if(level<0||c.level==level)sum+=weight(c);
        double pick=(retro_random(&e->rng)+.5)/4294967296.0*sum;
        size_t chosen=b.cells.size();
        for(size_t i=0;i<b.cells.size();i++)if(level<0||b.cells[i].level==level){chosen=i;if((pick-=weight(b.cells[i]))<=0)break;}
        auto& c=b.cells[chosen];c.selected++;c.last=b.steps.load();
        a.state=c.state;a.feedback=true;a.feedback_key=c.key;
        a.feedback_step=0;a.feedback_progress=0;e->start=&a.state->start;*id=c.level;
    } else {
        if(a.mode==2)a.mode=0;
        if(a.mode==1){*id=0;e->start=retro_rom().starts[0].get();}
    }
    b.resets[a.mode]++;
}
static void retro_archive_restore(Env* e) {
    if(!e->archive||!e->archive->state)return;
    const auto& l=e->archive->state->ledger;
    memcpy(e->frontiers,l.frontiers,sizeof(l.frontiers));e->frontier_count=l.frontier_count;
    memcpy(e->area_transition_keys,l.areas,sizeof(l.areas));e->area_transition_key_count=l.area_count;
    e->rewarded_levels=l.rewarded;e->last_area_key=l.last_area;e->pipe_phase=l.pipe_phase;
}
static uint64_t retro_archive_key(Env* e) {
    const auto* m=e->emu->low_mem();
    // Physical room, spatial cell, power and vertical motion. Exact route and
    // enemy state remain in the snapshot; reservoir replacement adds variety.
    return uint64_t(retro_area_key(m)) | (uint64_t(robs_x(m)/e->archive->width)<<32)
        | (uint64_t(m[0xce]/32)<<42) | (uint64_t(m[0x756]&3)<<46)
        | (uint64_t(m[0x1d]&3)<<48);
}
static void retro_archive_capture(Env* e) {
    auto& a=*e->archive;auto& b=retro_archive_bank();const auto* m=e->emu->low_mem();
    if(!retro_playable_area_state(m)||robs_dead(m)||m[0xb5]!=1||m[0xce]>224)return;
    int level=retro_level_id(e->world,e->stage);if(level<0)return;
    uint64_t key=retro_archive_key(e);
    std::lock_guard<std::mutex> lock(b.mutex);
    auto it=b.index.find(key);size_t slot;
    if(it!=b.index.end()) {
        slot=it->second;
        // Replace occasionally, preserving coverage without freezing one enemy phase.
        auto& c=b.cells[slot];c.visits++;
        if(retro_random(&e->rng)%16)return;
    } else {
        if(b.cells.size()<b.capacity){slot=b.cells.size();b.cells.emplace_back();}
        else {
            // Evict from the most represented level, retaining broad coverage.
            int counts[32]={};for(auto& c:b.cells)counts[c.level]++;
            int victim=std::max_element(counts,counts+32)-counts;
            std::vector<size_t> candidates;
            for(size_t i=0;i<b.cells.size();i++)if(b.cells[i].level==victim)candidates.push_back(i);
            slot=candidates[retro_random(&e->rng)%candidates.size()];b.index.erase(b.cells[slot].key);
        }
        b.cells[slot]=RetroArchiveCell{};b.cells[slot].key=key;b.cells[slot].level=level;
        b.index[key]=slot;
    }
    auto s=std::make_shared<RetroArchiveSnapshot>();auto& st=s->start;auto& l=s->ledger;
    e->emu->save_state(&st.state);const auto& f=e->emu->frame();
    for(int y=0;y<240;y++)memcpy(st.pixels+y*256,f.pixels+y*f.pitch,256);
    memcpy(st.palette,f.palette,sizeof(st.palette));
    st.world=e->world;st.stage=e->stage;st.area=e->area;st.data=m[0xe7]+256*m[0xe8];st.x=e->x_pos;st.rta_offset_frames=0;
    memcpy(l.frontiers,e->frontiers,sizeof(l.frontiers));l.frontier_count=e->frontier_count;
    memcpy(l.areas,e->area_transition_keys,sizeof(l.areas));l.area_count=e->area_transition_key_count;
    l.rewarded=e->rewarded_levels;l.last_area=e->last_area_key;l.pipe_phase=e->pipe_phase;
    b.cells[slot].depth=retro_archive_depth(*s,level);
    b.cells[slot].state=std::move(s);
    a.feedback=true;a.feedback_key=key;a.feedback_step=e->episode_decisions;a.feedback_progress=e->progress_pixels;
}
static void retro_archive_step(Env* e,bool done,bool death,bool progress) {
    if(!e->archive)return;
    auto& a=*e->archive;auto& b=retro_archive_bank();
    b.steps.fetch_add(1,std::memory_order_relaxed);b.mode_steps[a.mode].fetch_add(1,std::memory_order_relaxed);
    if(a.feedback&&(done||death||progress||e->progress_pixels-a.feedback_progress>=128||e->episode_decisions-a.feedback_step>=std::min(256,a.interval))) {
        std::lock_guard<std::mutex> lock(b.mutex);auto it=b.index.find(a.feedback_key);
        if(it!=b.index.end()) {
            auto& c=b.cells[it->second];
            bool good=progress||e->progress_pixels-a.feedback_progress>=128;
            c.successes+=good;c.failures+=!good;
        }
        a.feedback=false;
    }
    // Last frame of a nonterminal decision is always rendered in retro_step.
    if(!done&&e->episode_decisions%a.interval==0)retro_archive_capture(e);
}
static void retro_archive_io(gzFile f,void* p,size_t n,bool write) {
    if(n>1000000||(write?gzwrite(f,p,(unsigned)n):gzread(f,p,(unsigned)n))!=(int)n)
        throw std::runtime_error("retro: truncated/corrupt archive");
}
// Versioned native-endian POD metadata, portable core state serialization. Never
// serialize Nes_State's internal pointers. CRC and length checks reject damage.
static void retro_archive_file(const char* path,bool write,double phase) {
    auto& b=retro_archive_bank();std::lock_guard<std::mutex> lock(b.mutex);
    std::string temp=std::string(path)+(write?".tmp":"");gzFile f=gzopen(temp.c_str(),write?"wb1":"rb");
    if(!f)throw std::runtime_error(std::string("retro: cannot open archive ")+path);
    try {
        uint64_t header[5]={0x524554524f415201ull,retro_rom().fingerprint,b.cells.size(),sizeof(RetroArchiveLedger),0x01020304};
        retro_archive_io(f,header,sizeof(header),write);
        if(header[0]!=0x524554524f415201ull||header[1]!=retro_rom().fingerprint||header[2]>b.capacity
            ||header[3]!=sizeof(RetroArchiveLedger)||header[4]!=0x01020304)
            throw std::runtime_error("retro: incompatible archive version, ROM, size or byte order");
        retro_archive_io(f,&phase,sizeof(phase),write);
        if(!std::isfinite(phase)||phase<0||phase>1)throw std::runtime_error("retro: invalid archive phase");
        std::vector<RetroArchiveCell> loaded;
        for(size_t i=0;i<header[2];i++) {
            RetroArchiveCell c;auto& cell=write?b.cells[i]:c;
            uint64_t stats[7]={cell.key,cell.visits,cell.selected,cell.failures,cell.successes,0,(uint64_t)cell.level};
            retro_archive_io(f,stats,sizeof(stats),write);
            if(!write){c.key=stats[0];c.visits=stats[1];c.selected=stats[2];c.failures=stats[3];c.successes=stats[4];c.level=stats[6];c.state=std::make_shared<RetroArchiveSnapshot>();}
            auto& s=*cell.state;auto& st=s.start;
            retro_archive_io(f,&s.ledger,sizeof(s.ledger),write);
            retro_archive_io(f,st.pixels,sizeof(st.pixels),write);retro_archive_io(f,st.palette,sizeof(st.palette),write);
            int metadata[7]={st.world,st.stage,st.area,st.data,st.x,st.rta_offset_frames,cell.level};
            retro_archive_io(f,metadata,sizeof(metadata),write);
            if(!write){st.world=metadata[0];st.stage=metadata[1];st.area=metadata[2];st.data=metadata[3];st.x=metadata[4];st.rta_offset_frames=metadata[5];
                if(cell.level<0||cell.level>=32||retro_level_id(st.world,st.stage)!=cell.level
                    ||s.ledger.frontier_count<0||s.ledger.frontier_count>256||s.ledger.area_count<0||s.ledger.area_count>256)
                    throw std::runtime_error("retro: invalid archive metadata");
                c.depth=retro_archive_depth(s,c.level);}
            Mem_Writer writer;if(write)retro_check(st.state.write(writer));
            uint32_t size=write?writer.size():0;retro_archive_io(f,&size,sizeof(size),write);
            if(!size||size>100000)throw std::runtime_error("retro: invalid core state size");
            std::vector<char> bytes(size);if(write)memcpy(bytes.data(),writer.data(),size);
            retro_archive_io(f,bytes.data(),size,write);
            if(!write){Mem_File_Reader reader(bytes.data(),size);retro_check(st.state.read(reader));loaded.push_back(std::move(c));}
        }
        if(!write){char extra;if(gzread(f,&extra,1)!=0)throw std::runtime_error("retro: archive trailing data or CRC failure");}
        if(gzclose(f)!=Z_OK){f=nullptr;throw std::runtime_error("retro: archive close failed");}f=nullptr;
        if(write){if(rename(temp.c_str(),path))throw std::runtime_error("retro: archive publish failed");}
        else {b.cells=std::move(loaded);b.index.clear();for(size_t i=0;i<b.cells.size();i++)if(!b.index.emplace(b.cells[i].key,i).second)throw std::runtime_error("retro: duplicate archive cell");b.resumed_phase=phase;}
    }catch(...){if(f)gzclose(f);throw;}
}
