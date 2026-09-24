#pragma once
#ifdef PUFFER_RETRO_LEGACY
#error "Legacy retro backends are retired; full-screen ROM observations are required"
#else
#ifndef __cplusplus
#error "retro requires C++ and the QuickNES ROM core"
#endif
#ifdef PUFFER_GPU_ENV
#error "retro has no validated CUDA ROM core"
#endif
#include "pufferenv.h"
#include "retro_obs.h"
#include "retro_timing.h"
#include "retro_rom_identity.h"
#include "nes_emu/Nes_Emu.h"
#include "nes_emu/Nes_State.h"
#include "nes_emu/Data_Reader.h"
#include <mutex>
#include <memory>
#include <vector>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#ifdef __AVX2__
#include <immintrin.h>
#endif

// All combinations of A, B, Up, Down, Left, Right. Raw replay also supports
// Start/Select through retro_frame(), without wrapper resets or RAM writes.
#define NUM_ATNS 1
#define ACT_SIZES {64}
#define RETRO_NUM_ACTIONS 64
#define RETRO_MAX_LEVELS 32
#define RETRO_BTN_A 1
#define RETRO_BTN_B 2
#define RETRO_BTN_SELECT 4
#define RETRO_BTN_START 8
#define RETRO_BTN_UP 16
#define RETRO_BTN_DOWN 32
#define RETRO_BTN_LEFT 64
#define RETRO_BTN_RIGHT 128
#if defined(from_float) && !defined(PRECISION_FLOAT)
typedef precision_t obs_t;
#else
typedef float obs_t;
#endif
struct Log {
    float perf, score, episode_return, episode_length, distance, flag, deaths, coins;
    float truncations, frames, decisions, clears, warps;
    float progress_pixels, checkpoints, clear_frame_sum;
    float rta_frame_sum, rta_count, practice;
    // `coins` is the ending ROM counter; these fields count reward events.
    float coin_events, idle_steps;
    float area_transitions, novel_areas;
    float level_episodes[32], level_clears[32];
    float n;
};
struct RetroStart {
    Nes_State state;
    unsigned char pixels[256*240];
    short palette[256];
    int world, stage, area, data, x, rta_offset_frames;
};
struct RetroRom {
    Nes_Cart cart;
    Nes_Emu seed;
    Nes_State title;
    std::unique_ptr<RetroStart> starts[32];
    std::unique_ptr<RetroStart> practice_start;
    std::string practice_replay;
    std::string path;
    std::mutex mutex;
    bool loaded = false;
    unsigned long long fingerprint = 0;
};
static RetroRom& retro_rom() { static RetroRom rom; return rom; }
struct RetroVecArena { Nes_Emu* emus; int count; };
struct RetroDisplay {
    unsigned char pixels[256*240];
    short palette[256];
    bool valid;
    // Viewer-only results survive an automatic reset; never enter observations.
    int last_clear_frames, last_clear_time, last_clear_level;
    int last_rta_frames, last_rta_total_frames, last_rta_level;
    unsigned long long rta_frame_sums[32], rta_counts[32];
};
// Episode-local frontiers, keyed by source level and actual loaded area data.
// The ROM's upcoming pipe destination ($0750) is not a stable area identity.
struct RetroFrontier { unsigned int key; int x; };
struct Env {
    Log log;
    Agent agents[1];
    int num_agents, tag, boundary_reached;
    unsigned int rng;
    int tick, world, stage, area, x_pos, x_pos_max, score, coins, time, life;
    int has_flag, is_dead, frameskip, max_frames, spawn_n, cur_spawn, spawn_pin;
    int terminate_on_clear;
    int completion_on_rta_split, completion_on_next_playable, pending_clear_level;
    int pipe_phase, underground_start_tick, pipe_segment_frames;
    unsigned int pipe_start_key;
    float pipe_segment_bonus;
    unsigned char spawn_w[32], spawn_l[32];
    int episode_spawn, episode_clears, episode_warps, last_frames;
    int level_start_tick, episode_clear_frames;
    RetroRtaClock rta;
    int completion_time_min_frames, completion_time_max_frames;
    int idle_streak, episode_idle_steps, episode_coin_events;
    int episode_area_transitions, episode_novel_areas, area_transition_key_count;
    unsigned int rewarded_levels;
    float episode_return, completion_reward, death_penalty, score_scale, reward_scale;
    float checkpoint_reward, completion_time_bonus, coin_reward, idle_penalty;
    float completion_time_target_bonus, completion_time_target_max;
    int completion_time_target;
    int checkpoint_distance, idle_grace_decisions, progress_pixels, episode_decisions, frontier_count;
    unsigned int last_area_key, area_transition_keys[256];
    RetroFrontier frontiers[256];
    bool emu_ok, emu_owned, full_render, reset_image, last_truncated, rom_blocks, practice;
    Nes_Emu* emu;
    RetroVecArena* arena;
    const RetroStart* start;
    RetroDisplay* display;
};
static inline unsigned char retro_action_mask(int a) { return (a&3)|((a&60)<<2); }
static inline int retro_mask_action(unsigned char m) { return (m&3)|((m>>2)&60); }
static inline unsigned int retro_random(unsigned int* state) {
    unsigned int x=*state?*state:0x9e3779b9u;
    x^=x<<13; x^=x>>17; x^=x<<5; return *state=x;
}
static unsigned char* retro_thread_pixels() {
    static thread_local unsigned char pixels[Nes_Emu::buffer_width*256]={};
    return pixels;
}
static void retro_bind_pixels(Nes_Emu* e) {
    e->set_pixels(retro_thread_pixels()+8*Nes_Emu::buffer_width,Nes_Emu::buffer_width);
}
static void retro_check(const char* err) { if(err) throw std::runtime_error(std::string("retro: ")+err); }
static int retro_level_id(int w,int l) { return w>=1&&w<=8&&l>=1&&l<=4?(w-1)*4+l-1:-1; }
static void retro_parse_spawns(Env* e,const char* spec) {
    e->spawn_n=0;
    if(!spec||!*spec) spec="1-1";
    if(!strcmp(spec,"all")) {
        for(int w=1;w<=8;w++) for(int l=1;l<=4;l++) {
            int n=e->spawn_n++; e->spawn_w[n]=w; e->spawn_l[n]=l;
        }
        return;
    }
    const char* p=spec;
    while(*p) {
        int w,l,n=0;
        if(sscanf(p," %d-%d %n",&w,&l,&n)!=2||n<=0||retro_level_id(w,l)<0
            ||e->spawn_n==32||(p[n]&&p[n]!=','))
            throw std::runtime_error("retro: spawn_levels must be all or a CSV of levels 1-1 through 8-4");
        e->spawn_w[e->spawn_n]=w; e->spawn_l[e->spawn_n++]=l; p+=n;
        if(*p==',') { ++p; if(!*p) throw std::runtime_error("retro: empty spawn entry"); }
    }
}
static void retro_pick_spawn(Env* e) { if(!e->spawn_pin) e->cur_spawn=retro_random(&e->rng)%e->spawn_n; }

static void retro_load_rom_locked(RetroRom& rom,const char* path) {
    if(rom.loaded) {
        if(rom.path!=path) throw std::runtime_error("retro: cannot change ROM inside a vector process");
        return;
    }
    FILE* f=fopen(path,"rb");
    if(!f) throw std::runtime_error(std::string("retro: cannot open ROM ")+path);
    std::vector<unsigned char> bytes(40976);
    size_t size=fread(bytes.data(),1,bytes.size(),f); int extra=fgetc(f); fclose(f);
    if(size!=bytes.size()||extra!=EOF||memcmp(bytes.data(),"NES\x1a",4))
        throw std::runtime_error("retro: supported SMB1 iNES ROM required");
    unsigned long long hash=14695981039346656037ull;
    for(unsigned char b:bytes) hash=(hash^b)*1099511628211ull;
    if(!retro_ntsc_rom_fingerprint(hash))
        throw std::runtime_error("retro: SMB1 World/NTSC ROM required (verified iNES or NES 2.0); PAL/modified ROMs are rejected");
    rom.fingerprint=hash;
    Mem_File_Reader reader(bytes.data(),(long)bytes.size());
    retro_check(rom.cart.load_ines(reader));
    if(rom.cart.mapper_code()!=0||rom.cart.prg_size()!=32768||rom.cart.chr_size()!=8192)
        throw std::runtime_error("retro: SMB1 mapper-0 cartridge required");
    retro_check(rom.seed.set_cart(&rom.cart)); retro_bind_pixels(&rom.seed);
    bool ready=false;
    for(int i=0;i<600;i++) {
        retro_check(rom.seed.emulate_frame(0,0));
        const unsigned char* m=rom.seed.low_mem();
        if(i>30&&m[0x770]==0&&m[0x772]==3) { ready=true; break; }
    }
    if(!ready||rom.seed.error_count()) throw std::runtime_error("retro: ROM did not reach title menu without unsupported opcodes");
    rom.seed.save_state(&rom.title); rom.path=path; rom.loaded=true;
    fprintf(stderr,"[retro] ROM %s fingerprint=%016llx mapper=0 World/NTSC; %.8f fps\n",path,hash,RETRO_NTSC_FPS);
}
static int retro_start_area(const RetroRom& rom,int w,int l) {
    const unsigned char* prg=rom.cart.prg();
    int base=prg[RETRO_WORLD_OFFSETS-0x8000+w-1],area=0;
    for(int s=1;s<l;s++) { if(prg[RETRO_AREA_OFFSETS-0x8000+base+area]==0x29) area++; area++; }
    return area;
}
static void retro_prepare_start_locked(RetroRom& rom,int w,int l) {
    int id=retro_level_id(w,l); if(rom.starts[id]) return;
    std::unique_ptr<RetroStart> start(new RetroStart());
    Nes_Emu& emu=rom.seed;
    emu.load_state(rom.title); retro_bind_pixels(&emu);
    unsigned char* m=emu.low_mem(); int area=retro_start_area(rom,w,l);
    // 1-1 follows a completely unmodified new game from the title screen.
    // Other starts are explicit practice segments, not full-run RTA starts.
    if(w!=1||l!=1) { m[0x75f]=w-1; m[0x75c]=l-1; m[0x760]=area; }
    retro_check(emu.emulate_frame(RETRO_BTN_START,0));
    bool ready=false;
    int rta_start_frame=-1;
    for(int i=0;i<1200;i++) {
        int old_routine=m[0xe];
        retro_check(emu.emulate_frame(0,0));
        if(rta_start_frame<0&&m[0x770]==1&&m[0x772]>=3&&m[0xe]==8&&old_routine<8)
            rta_start_frame=i;
        if(m[0x770]==1&&m[0x772]==3&&m[0xe]==8&&robs_time(m)>0) {
            if(rta_start_frame<0) throw std::runtime_error("retro: RTA start was not observed while preparing reset");
            start->rta_offset_frames=i-rta_start_frame;
            ready=true; break;
        }
    }
    int base=rom.cart.prg()[RETRO_WORLD_OFFSETS-0x8000+w-1];
    int expected=rom.cart.prg()[RETRO_AREA_OFFSETS-0x8000+base+area];
    // Underground stages begin with a ROM-controlled pipe intermission.
    // By the first controllable frame, the ROM has entered the following area.
    if(expected==0x29) { area++; expected=rom.cart.prg()[RETRO_AREA_OFFSETS-0x8000+base+area]; }
    const unsigned char* prg=rom.cart.prg();
    int offset=prg[RETRO_AREA_DATA_OFFSETS-0x8000+((expected>>5)&3)]+(expected&31);
    int expected_data=prg[RETRO_AREA_DATA_LOW-0x8000+offset]+256*prg[RETRO_AREA_DATA_HIGH-0x8000+offset]+2;
    // $0750 is also the upcoming pipe destination and can already differ.
    // Validate the actual loaded level-data pointer, not that warp pointer.
    if(!ready||emu.error_count()||robs_world(m)!=w||robs_stage(m)!=l||m[0x760]!=area||m[0xe7]+256*m[0xe8]!=expected_data) {
        fprintf(stderr,"[retro] boot requested=%d-%d area=%d ptr=%02x got=%d-%d area=%d ptr=%02x mode=%d task=%d eng=%d time=%d\n",
            w,l,area,expected,robs_world(m),robs_stage(m),m[0x760],m[0x750],m[0x770],m[0x772],m[0xe],robs_time(m));
        throw std::runtime_error("retro: level start validation failed");
    }
    emu.save_state(&start->state); const auto& fr=emu.frame();
    for(int y=0;y<240;y++) memcpy(start->pixels+y*256,fr.pixels+y*fr.pitch,256);
    memcpy(start->palette,fr.palette,sizeof(start->palette));
    start->world=w; start->stage=l; start->area=area+1;
    start->data=m[0xe7]+256*m[0xe8]; start->x=robs_x(m);
    rom.starts[id]=std::move(start);
}
static void retro_sync_from_emu(Env* e) {
    const unsigned char* m=e->emu->low_mem();
    e->world=robs_world(m); e->stage=robs_stage(m); e->area=robs_area(m);
    e->x_pos=robs_x(m); e->score=robs_score(m); e->coins=robs_coins(m);
    e->time=robs_time(m); e->life=robs_life(m);
    e->has_flag=robs_flagget(m); e->is_dead=robs_dead(m)||robs_dying(m);
}
static inline bool retro_playable_area_state(const unsigned char* m) {
    return m[0x0770]==1&&m[0x0772]==3&&m[0x000e]==8&&!robs_dying(m);
}
// Identity of the actually loaded area, unlike $0750 which may already hold
// a future pipe destination. Include the level, area index, and data pointer.
static inline unsigned int retro_area_key(const unsigned char* m) {
    int level=retro_level_id(robs_world(m),robs_stage(m));
    unsigned int data=m[0x00e7]+256u*m[0x00e8];
    if(level<0||data<0x8000u) return 0xffffffffu;
    return ((unsigned int)level<<24)|((unsigned int)m[0x0760]<<16)|data;
}
static bool retro_area_key_seen(const Env* e,unsigned int key) {
    for(int i=0;i<e->area_transition_key_count;i++)
        if(e->area_transition_keys[i]==key) return true;
    return false;
}
// Return 0 for no confirmed event, 1 for a repeated destination, and 2 for a
// novel destination, for diagnostics only. A destination
// is confirmed only after the ROM is back in its ordinary playable state.
static int retro_record_area_transition(Env* e,const unsigned char* m) {
    if(!retro_playable_area_state(m)) return 0;
    unsigned int key=retro_area_key(m);
    if(key==0xffffffffu||key==e->last_area_key) return 0;
    unsigned int old_key=e->last_area_key;
    int old_level=old_key==0xffffffffu?-1:(int)(old_key>>24), new_level=(int)(key>>24);
    e->last_area_key=key;
    if(old_level<0||old_level!=new_level) return 0;
    e->episode_area_transitions++;
    if(retro_area_key_seen(e,key)) return 1;
    if(e->area_transition_key_count<256)
        e->area_transition_keys[e->area_transition_key_count++]=key;
    else
        // Keep logging transitions after the bounded frontier fills, but do
        // not turn an unremembered destination into a repeatable reward farm.
        return 1;
    e->episode_novel_areas++;
    return 2;
}
static int retro_progress(Env* e,unsigned int key,int x) {
    // Bound coordinate-wrap/glitch jackpots without changing ROM execution.
    x=std::max(0,std::min(3400,x)); // Bound checkpoint distance per area.
    for(int i=0;i<e->frontier_count;i++) if(e->frontiers[i].key==key) {
        int novel=std::max(0,x-e->frontiers[i].x);
        e->frontiers[i].x=std::max(x,e->frontiers[i].x);
        int before=e->progress_pixels/e->checkpoint_distance;
        e->progress_pixels+=novel;
        return e->progress_pixels/e->checkpoint_distance-before;
    }
    // First arrival establishes a baseline; loading an area isn't movement.
    // Exhaustion suppresses new-area rewards, never alters/emits ROM inputs.
    if(e->frontier_count<256) e->frontiers[e->frontier_count++]={key,x};
    return 0;
}
static int retro_track_progress(Env* e) {
    const unsigned char* m=e->emu->low_mem();
    int level=retro_level_id(robs_world(m),robs_stage(m));
    if(level<0||m[0x770]!=1||m[0x772]!=3||m[0xe]!=8||robs_dying(m)) return 0;
    unsigned int data=m[0xe7]+256u*m[0xe8];
    if(data<0x8000) return 0;
    return retro_progress(e,((unsigned int)level<<16)|data,robs_x(m));
}
static inline obs_t retro_obs_value(float value) {
#if defined(from_float) && !defined(PRECISION_FLOAT)
    return from_float(value);
#else
    return value;
#endif
}
static void retro_compute_obs_real(const Env* e,obs_t* obs) {
    constexpr int ram_size=RETRO_EGO_SIZE+RETRO_ENT_SIZE;
#ifdef __AVX2__
    float values[ram_size];
#else
    float values[OBS_SIZE];
#endif
    RetroScalars sc={e->x_pos,e->x_pos_max,e->coins,e->score,e->tick,e->world,e->stage,e->area,e->time,e->has_flag,e->is_dead,0,0};
    const unsigned char* m=e->emu->low_mem(); retro_ego_ent(values,m,&sc);
    const auto& fr=e->emu->frame();
    const unsigned char* pixels=e->reset_image?e->start->pixels:fr.pixels;
    const short* palette=e->reset_image?e->start->palette:fr.palette;
    int pitch=e->reset_image?256:(int)fr.pitch;
    struct PaletteCache { short palette[256]; float lut[256]; bool valid; };
    static thread_local PaletteCache cache={};
    if(!cache.valid||memcmp(cache.palette,palette,sizeof(cache.palette))) {
        memcpy(cache.palette,palette,sizeof(cache.palette));
        for(int i=0;i<256;i++) {
            const auto& c=Nes_Emu::nes_colors[palette[i]&(Nes_Emu::color_table_size-1)];
            cache.lut[i]=retro_luma(c.red,c.green,c.blue);
        }
        cache.valid=true;
    }
    const float* lut=cache.lut;
    int idx=ram_size;
#ifdef __AVX2__
    for(int i=0;i<ram_size;i++) obs[i]=retro_obs_value(values[i]);
    if(!pixels) {
        for(int i=ram_size;i<OBS_SIZE;i++) obs[i]=retro_obs_value(0);
        return;
    }
    // Eight exact box means at a time, straight from NES palette indices to
    // the final observation. Preserve the scalar addition order. Luminance
    // is finite and nonnegative, so the integer bf16 round-to-nearest-even
    // conversion below is identical to from_float without its NaN slow path.
    const __m256i byte_mask=_mm256_set1_epi32(255);
    for(int y=0;y<240;y+=RETRO_OBS_SCALE) for(int x=0;x<256;x+=8*RETRO_OBS_SCALE) {
        const unsigned char* row=pixels+y*pitch+x;
#if RETRO_OBS_SCALE == 2
        __m256i top=_mm256_cvtepu16_epi32(_mm_loadu_si128((const __m128i*)row));
        __m256i bottom=_mm256_cvtepu16_epi32(_mm_loadu_si128((const __m128i*)(row+pitch)));
        __m256 sum=_mm256_i32gather_ps(lut,_mm256_and_si256(top,byte_mask),4);
        sum=_mm256_add_ps(sum,_mm256_i32gather_ps(lut,_mm256_srli_epi32(top,8),4));
        sum=_mm256_add_ps(sum,_mm256_i32gather_ps(lut,_mm256_and_si256(bottom,byte_mask),4));
        sum=_mm256_add_ps(sum,_mm256_i32gather_ps(lut,_mm256_srli_epi32(bottom,8),4));
        __m256 mean=_mm256_mul_ps(sum,_mm256_set1_ps(0.25f));
#else
        __m256 sum=_mm256_setzero_ps();
        for(int dy=0;dy<4;dy++) {
            __m256i pixels4=_mm256_loadu_si256((const __m256i*)(row+dy*pitch));
            sum=_mm256_add_ps(sum,_mm256_i32gather_ps(lut,_mm256_and_si256(pixels4,byte_mask),4));
            sum=_mm256_add_ps(sum,_mm256_i32gather_ps(lut,_mm256_and_si256(_mm256_srli_epi32(pixels4,8),byte_mask),4));
            sum=_mm256_add_ps(sum,_mm256_i32gather_ps(lut,_mm256_and_si256(_mm256_srli_epi32(pixels4,16),byte_mask),4));
            sum=_mm256_add_ps(sum,_mm256_i32gather_ps(lut,_mm256_srli_epi32(pixels4,24),4));
        }
        __m256 mean=_mm256_mul_ps(sum,_mm256_set1_ps(1.0f/16));
#endif
#if defined(from_float) && !defined(PRECISION_FLOAT)
        __m256i bits=_mm256_castps_si256(mean);
        __m256i tie=_mm256_and_si256(_mm256_srli_epi32(bits,16),_mm256_set1_epi32(1));
        bits=_mm256_add_epi32(bits,_mm256_add_epi32(_mm256_set1_epi32(0x7fff),tie));
        bits=_mm256_srli_epi32(bits,16);
        __m128i packed=_mm_packus_epi32(_mm256_castsi256_si128(bits),_mm256_extracti128_si256(bits,1));
        _mm_storeu_si128((__m128i*)(obs+idx),packed);
#else
        _mm256_storeu_ps(obs+idx,mean);
#endif
        idx+=8;
    }
#else
    // Keep the previous portable path: full-frame conversion benchmarks
    // faster than per-pixel/row bf16 packing without the explicit SIMD path.
    for(int y=0;y<240;y+=RETRO_OBS_SCALE) for(int x=0;x<256;x+=RETRO_OBS_SCALE) {
        float sum=0;
        if(pixels) {
            const unsigned char* row=pixels+y*pitch+x;
            for(int dy=0;dy<RETRO_OBS_SCALE;dy++) for(int dx=0;dx<RETRO_OBS_SCALE;dx++)
                sum+=lut[row[dy*pitch+dx]];
        }
        values[idx++]=sum*(1.0f/(RETRO_OBS_SCALE*RETRO_OBS_SCALE));
    }
    for(int i=0;i<OBS_SIZE;i++) obs[i]=retro_obs_value(values[i]);
#endif
}
static double retro_option(Dict* cfg,const char* key,double fallback) { DictItem* i=dict_find(cfg,key); return i?i->value:fallback; }
// Replay controller inputs once, then cache an exact state/image for cheap
// resets. No position, timer, velocity, RNG or bus-phase RAM is edited.
static void retro_prepare_practice_locked(RetroRom& rom,const char* path) {
    if(rom.practice_start) {
        if(rom.practice_replay!=path) throw std::runtime_error("retro: one practice replay per process");
        return;
    }
    FILE* file=fopen(path,"r");
    if(!file) throw std::runtime_error("retro: cannot open practice_replay");
    unsigned long long identity=0; int count=0;
    if(fscanf(file,"RETRO_PRACTICE_V1 %llx %d",&identity,&count)!=2
        ||identity!=rom.fingerprint||count<1||count>6000) {
        fclose(file); throw std::runtime_error("retro: invalid practice replay header/ROM identity");
    }
    auto& emu=rom.seed;
    emu.load_state(rom.starts[0]->state); retro_bind_pixels(&emu);
    bool side_pipe=false,underground=false;
    for(int i=0;i<count;i++) {
        int buttons=-1;
        if(fscanf(file,"%d",&buttons)!=1||buttons<0||buttons>255||(buttons&12)) {
            fclose(file); throw std::runtime_error("retro: invalid/truncated practice controller replay");
        }
        retro_check(emu.emulate_frame(buttons,0));
        const auto* ram=emu.low_mem();
        underground |= ram[0xe]==8&&(ram[0xe7]+256*ram[0xe8])!=rom.starts[0]->data;
        if(underground&&ram[0xe]==2) side_pipe=true;
    }
    char trailing=0;
    bool extra=fscanf(file," %c",&trailing)==1;
    fclose(file);
    const auto* m=emu.low_mem(); const auto& frame=emu.frame();
    bool black=true;
    for(int y=0;y<240;y++) for(int x=0;x<256;x++)
        black &= frame.palette[frame.pixels[y*frame.pitch+x]]==0x0f;
    if(extra||emu.error_count()||!side_pipe||!black||robs_world(m)!=1||robs_stage(m)!=1)
        throw std::runtime_error("retro: practice replay must end on the return-pipe black screen in 1-1");
    std::unique_ptr<RetroStart> start(new RetroStart{});
    emu.save_state(&start->state);
    for(int y=0;y<240;y++) memcpy(start->pixels+y*256,frame.pixels+y*frame.pitch,256);
    memcpy(start->palette,frame.palette,sizeof(start->palette));
    start->world=1; start->stage=1; start->area=robs_area(m);
    start->data=m[0xe7]+256*m[0xe8]; start->x=robs_x(m);
    start->rta_offset_frames=0; // Segment clock, deliberately NOT a full-run RTA.
    rom.practice_start=std::move(start); rom.practice_replay=path;
    fprintf(stderr,"[retro] PRACTICE return-pipe black screen after %d prefix frames; segment clock starts at zero\n",count);
}
void puf_init(Env* e,Dict* cfg) {
    Nes_Emu* supplied=e->emu; RetroVecArena* arena=e->arena; unsigned int seed=e->rng;
    memset(e,0,sizeof(*e)); e->num_agents=1; e->rng=seed?seed:0x9e3779b9u; e->emu=supplied; e->arena=arena;
    e->frameskip=retro_option(cfg,"frameskip",1); e->max_frames=retro_option(cfg,"max_frames",30000);
    double terminate_on_clear=retro_option(cfg,"terminate_on_clear",0);
    if(terminate_on_clear!=0&&terminate_on_clear!=1)
        throw std::runtime_error("retro: terminate_on_clear must be 0 or 1");
    e->terminate_on_clear=(int)terminate_on_clear;
    double rta_split=retro_option(cfg,"completion_on_rta_split",0);
    double next_playable=retro_option(cfg,"completion_on_next_playable",0);
    if((rta_split!=0&&rta_split!=1)||(next_playable!=0&&next_playable!=1)
        ||(rta_split&&next_playable))
        throw std::runtime_error("retro: select only one completion endpoint: RTA split or next playable (0/1)");
    e->completion_on_rta_split=(int)rta_split;
    double segment_frames=retro_option(cfg,"pipe_segment_frames",600);
    e->pipe_segment_bonus=retro_option(cfg,"pipe_segment_bonus",0);
    if((next_playable!=0&&next_playable!=1)||!std::isfinite(segment_frames)
        ||segment_frames<1||segment_frames>10000000||segment_frames!=floor(segment_frames)
        ||!std::isfinite(e->pipe_segment_bonus)||e->pipe_segment_bonus<0)
        throw std::runtime_error("retro: invalid RTA/pipe segment configuration");
    e->completion_on_next_playable=(int)next_playable;
    e->pipe_segment_frames=(int)segment_frames;
    e->completion_reward=retro_option(cfg,"completion_reward",10); e->death_penalty=retro_option(cfg,"death_penalty",0.125);
    e->completion_time_bonus=retro_option(cfg,"completion_time_bonus",0);
    double fast=retro_option(cfg,"completion_time_min_frames",0);
    double slow=retro_option(cfg,"completion_time_max_frames",0);
    if(!std::isfinite(fast)||!std::isfinite(slow)||fast<0||slow<0
        ||fast!=floor(fast)||slow!=floor(slow)||fast>10000000||slow>10000000
        ||(slow==0?fast!=0:slow<=fast))
        throw std::runtime_error("retro: completion frame range requires 0 <= min < max (or both zero)");
    e->completion_time_min_frames=(int)fast;
    e->completion_time_max_frames=(int)slow;
    if(!std::isfinite(e->completion_time_bonus)||e->completion_time_bonus<0)
        throw std::runtime_error("retro: completion_time_bonus must be finite and nonnegative");
    e->coin_reward=retro_option(cfg,"coin_reward",0);
    e->idle_penalty=retro_option(cfg,"idle_penalty",0);
    for(const char* key:{"area_transition_reward","area_transition_timer_bonus"})
        if(retro_option(cfg,key,0)!=0)
            throw std::runtime_error("retro: area transition rewards were removed; legacy options must be zero");
    e->completion_time_target_bonus=retro_option(cfg,"completion_time_target_bonus",0);
    e->completion_time_target_max=retro_option(cfg,"completion_time_target_max",0);
    e->completion_time_target=(int)retro_option(cfg,"completion_time_target",0);
    if(e->completion_on_rta_split&&e->completion_time_target_bonus!=0)
        throw std::runtime_error("retro: RTA rewards require area_transition_timer_bonus=0 and completion_time_target_bonus=0; HUD TIME is not elapsed time");
    double idle_grace=retro_option(cfg,"idle_grace_decisions",8);
    if(!std::isfinite(e->coin_reward)||e->coin_reward<0
        ||!std::isfinite(e->idle_penalty)||e->idle_penalty<0
        ||!std::isfinite(e->completion_time_target_bonus)||e->completion_time_target_bonus<0
        ||!std::isfinite(e->completion_time_target_max)||e->completion_time_target_max<0
        ||e->completion_time_target<0||e->completion_time_target>999
        ||!std::isfinite(idle_grace)||idle_grace<0||idle_grace>100000
        ||idle_grace!=floor(idle_grace))
        throw std::runtime_error("retro: invalid coin/idle/area-transition reward configuration");
    e->idle_grace_decisions=(int)idle_grace;
    double spacing=retro_option(cfg,"checkpoint_distance",128);
    e->checkpoint_reward=retro_option(cfg,"checkpoint_reward",0.125);
    if(!std::isfinite(spacing)||spacing<1||spacing>3400||spacing!=floor(spacing)
        ||!std::isfinite(e->checkpoint_reward)||e->checkpoint_reward<0)
        throw std::runtime_error("retro: invalid checkpoint_distance/checkpoint_reward");
    e->checkpoint_distance=(int)spacing;
    e->reward_scale=retro_option(cfg,"reward_scale",0.0625);
    e->score_scale=retro_option(cfg,"score_scale",0); e->full_render=retro_option(cfg,"full_render",0)!=0;
    const char* full=getenv("RETRO_FULL_RENDER"); if(full&&strcmp(full,"0")) e->full_render=true;
    if(e->frameskip<1||e->frameskip>16||e->max_frames<1
        ||!std::isfinite(e->reward_scale)||e->reward_scale<=0)
        throw std::runtime_error("retro: invalid frameskip, max_frames, or reward_scale");
    DictItem* be=dict_find(cfg,"backend");
    if(be&&be->str&&strcmp(be->str,"quicknes")) throw std::runtime_error("retro: ROM-only build; legacy port requires RETRO_LEGACY=1");
    DictItem* cpu=dict_find(cfg,"cpu_backend");
    if(cpu&&cpu->str&&strcmp(cpu->str,"reference")&&strcmp(cpu->str,"blocks"))
        throw std::runtime_error("retro: cpu_backend must be reference or blocks");
#ifdef RETRO_DEFAULT_CPU_BLOCKS
    e->rom_blocks=!cpu||!cpu->str||!strcmp(cpu->str,"blocks");
#else
    e->rom_blocks=cpu&&cpu->str&&!strcmp(cpu->str,"blocks");
#endif
    DictItem* sp=dict_find(cfg,"spawn_levels"); retro_parse_spawns(e,sp&&sp->str?sp->str:"all");
    DictItem* rp=dict_find(cfg,"rom_path"); const char* path=rp&&rp->str?rp->str:RETRO_SMB1_ROM_PATH;
    RetroRom& rom=retro_rom();
    std::lock_guard<std::mutex> lock(rom.mutex);
    retro_load_rom_locked(rom,path);
    for(int i=0;i<e->spawn_n;i++) retro_prepare_start_locked(rom,e->spawn_w[i],e->spawn_l[i]);
    DictItem* practice=dict_find(cfg,"practice_replay");
    e->practice=practice&&practice->str&&*practice->str&&strcmp(practice->str,"None");
    if(e->practice) {
        if(e->spawn_n!=1||e->spawn_w[0]!=1||e->spawn_l[0]!=1||!e->completion_on_rta_split
            ||!e->terminate_on_clear||e->pipe_segment_bonus!=0||e->completion_time_min_frames!=0)
            throw std::runtime_error("retro: practice requires only 1-1, RTA split, terminate_on_clear=1, pipe bonus=0 and min frames=0");
        retro_prepare_practice_locked(rom,practice->str);
    }
    if(!e->emu) { e->emu=new Nes_Emu(); e->emu_owned=true; }
    retro_check(e->emu->set_cart(&rom.cart,&rom.seed));
    e->emu->set_idle_skip(retro_option(cfg,"idle_loop_skip",1)!=0);
    DictItem* rb=dict_find(cfg,"render_backend");
    const char* render=rb&&rb->str?rb->str:"reference";
    if(strcmp(render,"reference")&&strcmp(render,"wide"))
        throw std::runtime_error("retro: only full-screen reference/wide rendering is supported; crop mode is retired");
    if(dict_find(cfg,"render_crop_margin"))
        throw std::runtime_error("retro: remove obsolete render_crop_margin; the entire screen is always observed");
    if(!e->emu->set_wide_background(strcmp(render,"reference")!=0))
        throw std::runtime_error("retro: wide background requires immutable mapper-0 CHR");
    if(!e->emu->set_rom_blocks(e->rom_blocks))
        throw std::runtime_error("retro: blocks CPU requires the experimental ROM-block build and matching PRG");
    e->emu_ok=true;
}
void puf_reset(Env* e) {
    retro_pick_spawn(e);
    int id=retro_level_id(e->spawn_w[e->cur_spawn],e->spawn_l[e->cur_spawn]);
    e->start=e->practice?retro_rom().practice_start.get():retro_rom().starts[id].get();
    if(!e->start) throw std::runtime_error("retro: unprepared level start");
    e->emu->load_state(e->start->state); e->reset_image=true; retro_sync_from_emu(e);
    // State restore remaps the cartridge and invalidates the specialization.
    if(e->rom_blocks&&!e->emu->set_rom_blocks(true))
        throw std::runtime_error("retro: restored PRG does not match compiled blocks");
    e->x_pos_max=e->x_pos; e->tick=0; e->episode_return=0; e->episode_clears=0; e->episode_warps=0;
    e->rewarded_levels=0; e->episode_spawn=id;
    e->level_start_tick=0; e->episode_clear_frames=0;
    retro_rta_reset(&e->rta,e->world,e->stage,e->start->rta_offset_frames);
    e->pending_clear_level=-1; e->pipe_phase=0; e->underground_start_tick=0;
    e->pipe_start_key=retro_area_key(e->emu->low_mem());
    e->idle_streak=0; e->episode_idle_steps=0; e->episode_coin_events=0;
    e->episode_area_transitions=0; e->episode_novel_areas=0;
    e->area_transition_key_count=0; e->last_area_key=retro_area_key(e->emu->low_mem());
    if(e->last_area_key!=0xffffffffu)
        e->area_transition_keys[e->area_transition_key_count++]=e->last_area_key;
    e->frontier_count=0; e->progress_pixels=0; e->episode_decisions=0;
    retro_track_progress(e);
    if(e->agents[0].observations) retro_compute_obs_real(e,(obs_t*)e->agents[0].observations);
}
// Exact controller-frame API: no RAM writes, shaping, episode limits or resets.
static void retro_frame(Env* e,unsigned char buttons,bool draw=true) {
    retro_bind_pixels(e->emu);
    retro_check(draw?e->emu->emulate_frame(buttons,0):e->emu->emulate_skip_frame_fast(buttons,0));
    // The vendored core approximates a few unsupported opcodes as NOPs.
    // Never silently train through that path under a ROM-fidelity contract.
    if(e->emu->error_count())
        throw std::runtime_error("retro: unsupported CPU opcode; refusing approximate ROM execution (see README)");
    if(draw&&e->display) {
        // Only an explicitly watched environment owns a persistent image.
        // Native evaluation draws after workers have reused their scratch.
        const auto& fr=e->emu->frame();
        for(int y=0;y<240;y++) memcpy(e->display->pixels+y*256,fr.pixels+y*fr.pitch,256);
        memcpy(e->display->palette,fr.palette,sizeof(e->display->palette));
        e->display->valid=true;
    }
    e->reset_image=false;
}
static bool retro_level_advance(int ow,int ol,int w,int l) { return retro_level_id(w,l)>=0&&(w>ow||(w==ow&&l>ol)); }
// A bounded speed bonus paid only for a new clear. No per-step cost that
// could be avoided by dying, and no reward for merely running down the clock.
// NES frames (not decisions) keep the meaning independent of frameskip.
static float retro_clear_speed(int elapsed_frames,int budget_frames,int fast_frames=0,int slow_frames=0) {
    if(slow_frames==0) slow_frames=budget_frames;
    return std::max(0.0f,std::min(1.0f,
        (float)(slow_frames-elapsed_frames)/(slow_frames-fast_frames)));
}
// The ROM enters FlagpoleSlide immediately on contact, before time conversion
// and fireworks. Vine climbing uses a different game-engine routine.
static bool retro_flag_contact(const unsigned char* m) {
    return m[0x770]==1&&m[0x772]==3&&m[0xe]==4;
}
// The ROM stores coins as a two-digit decimal counter. A reset/death can make
// the counter decrease, but that is never an earned negative reward.
static int retro_coin_delta(int before,int after) {
    int delta=after-before;
    if(delta<-50) delta+=100;
    return std::max(0,std::min(99,delta));
}
// Charge at most one anti-stall event per decision. Movement, coin pickup,
// level advance, and terminal death/win all reset the grace streak.
static int retro_idle_event(int* streak,int before_x,int after_x,int coin_delta,
        bool meaningful_event,int grace,bool terminal_death_or_win) {
    if(terminal_death_or_win||meaningful_event||after_x!=before_x||coin_delta>0) {
        *streak=0;
        return 0;
    }
    *streak=std::max(0,*streak+1);
    return *streak>grace ? 1 : 0;
}
static float retro_rom_reward(const Env* e,int advances,bool dead,int score_delta,int checkpoints=0,float clear_speed=0,
        int coin_delta=0,int idle_events=0,float completion_target_bonus=0) {
    float reward=advances*e->completion_reward-(dead?e->death_penalty:0);
    reward+=e->completion_time_bonus*std::max(0.0f,std::min((float)advances,clear_speed));
    reward+=std::max(0,score_delta)*e->score_scale;
    // `coin_delta` is already normalized by retro_coin_delta; do not run a
    // signed reward delta through the wrap detector a second time.
    reward+=std::max(0,std::min(99,coin_delta))*e->coin_reward;
    reward-=std::max(0,idle_events)*e->idle_penalty;
    reward+=std::max(0.0f,completion_target_bonus)*e->completion_time_target_bonus;
    reward+=checkpoints*e->checkpoint_reward;
    return reward*e->reward_scale;
}
// Training ends an episode at the first death, the final win, the frame
// budget, or -- when terminate_on_clear=1 -- the configured clear endpoint.
// Standalone playback can let the ROM finish its death animation, spend a
// life, and respawn naturally.
static void retro_step(Env* e,bool continue_lives) {
    if(e->practice) e->log.practice=1;
    int action=std::max(0,std::min(63,(int)e->agents[0].actions[0]));
    unsigned char buttons=retro_action_mask(action);
    int old_score=e->score, old_coins=e->coins, old_x=e->x_pos;
    bool dead=false,death_end=false,won=false,meaningful_event=false,saw_clear=false;
    int advances=0,checkpoints=0,life_losses=0; e->last_frames=0;
    float clear_speed=0, completion_target_bonus=0;
    float segment_reward=0;
    for(int f=0;f<e->frameskip;f++) {
        const unsigned char* m=e->emu->low_mem(); int ow=robs_world(m),ol=robs_stage(m),mode=m[0x770];
        int old_life=robs_life(m);
        int old_routine=m[0xe];
        int old_screen_timer=m[0x7a0];
        retro_frame(e,buttons,e->full_render||f+1==e->frameskip||e->tick+1>=e->max_frames);
        e->tick++; e->last_frames++;
        bool rta_split=retro_rta_update(&e->rta,e->tick,m,old_routine,old_screen_timer,mode);
        if(rta_split&&e->display) {
            int level=e->rta.split_level;
            e->display->last_rta_frames=e->rta.split_frames;
            e->display->last_rta_total_frames=e->rta.split_total_frames;
            e->display->last_rta_level=level;
            // Only first splits have the same reset start as dashboard samples.
            if(e->rta.split_total_frames==e->rta.first_split_frames&&level>=0&&level<32) {
                e->display->rta_frame_sums[level]+=e->rta.first_split_frames;
                e->display->rta_counts[level]++;
            }
        }
        checkpoints+=retro_track_progress(e);
        int area_event=retro_record_area_transition(e,m);
        // Explicit 1-1 route events, independent of the novelty frontier.
        // Pay only after a confirmed arrival, once for each leg of the route.
        if(ow==1&&ol==1&&e->episode_spawn==0) {
            if(e->pipe_phase==0&&old_routine==8&&m[0xe]==3) e->pipe_phase=1;
            if(area_event&&e->pipe_phase==1&&e->last_area_key!=e->pipe_start_key) {
                segment_reward+=e->pipe_segment_bonus*retro_clear_speed(e->tick,e->pipe_segment_frames);
                e->underground_start_tick=e->tick; e->pipe_phase=2;
            } else if(area_event&&e->pipe_phase==2&&e->last_area_key==e->pipe_start_key) {
                segment_reward+=e->pipe_segment_bonus*retro_clear_speed(e->tick-e->underground_start_tick,e->pipe_segment_frames);
                e->pipe_phase=3;
            }
        }
        if(area_event) meaningful_event=true;
        bool level_advance=retro_level_advance(ow,ol,robs_world(m),robs_stage(m))||(mode!=2&&m[0x770]==2);
        if(e->completion_on_next_playable&&level_advance&&e->pending_clear_level<0)
            e->pending_clear_level=retro_level_id(ow,ol);
        bool clear=e->completion_on_rta_split ? rta_split : (e->completion_on_next_playable
            ? (e->pending_clear_level>=0&&(retro_playable_area_state(m)||m[0x770]==2))
            : (level_advance||retro_flag_contact(m)));
        if(clear) meaningful_event=true;
        int id=e->completion_on_rta_split ? e->rta.split_level :
            (e->completion_on_next_playable?e->pending_clear_level:retro_level_id(ow,ol));
        if(clear&&id>=0&&!(e->rewarded_levels&(1u<<id))) {
            e->rewarded_levels|=1u<<id; advances++; e->episode_clears++; e->log.level_clears[id]++;
            int clear_frames=e->completion_on_rta_split?e->rta.split_frames:e->tick-e->level_start_tick;
            if(e->display) {
                e->display->last_clear_frames=clear_frames;
                e->display->last_clear_time=robs_time(m);
                e->display->last_clear_level=id;
            }
            e->episode_clear_frames+=clear_frames;
            clear_speed+=retro_clear_speed(clear_frames,e->max_frames,
                e->completion_time_min_frames,e->completion_time_max_frames);
            // Extra HUD-time shaping, interpolated from target to target_max.
            if(e->completion_time_target>0 && e->completion_time_target_max>e->completion_time_target)
                completion_target_bonus=std::max(0.0f,std::min(1.0f,
                    (float)(robs_time(m)-e->completion_time_target) /
                    (e->completion_time_target_max-e->completion_time_target)));
            int source_world=id/4+1,source_stage=id%4+1;
            if(robs_world(m)>source_world+(source_stage==4)
                ||(robs_world(m)==source_world&&robs_stage(m)>source_stage+1)) e->episode_warps++;
        }
        if(clear) saw_clear=true;
        if(e->completion_on_rta_split) {
            if(clear) e->level_start_tick=e->tick;
        } else if(e->completion_on_next_playable) {
            if(clear) { e->level_start_tick=e->tick; e->pending_clear_level=-1; }
        } else if(level_advance) e->level_start_tick=e->tick;
        dead=robs_dead(m)||robs_gameover(m); won=ow==8&&mode!=2&&m[0x770]==2;
        if(continue_lives&&old_life!=255&&(robs_gameover(m)||robs_life(m)<old_life)) life_losses++;
        death_end=continue_lives?robs_gameover(m):dead;
        bool clear_stop=e->terminate_on_clear&&saw_clear;
        if(death_end||won||clear_stop||e->tick>=e->max_frames) break;
    }
    retro_sync_from_emu(e); e->x_pos_max=std::max(e->x_pos_max,e->x_pos);
    bool cleared=e->terminate_on_clear&&saw_clear;
    e->last_truncated=e->tick>=e->max_frames&&!death_end&&!won&&!cleared;
    bool done=death_end||won||cleared||e->last_truncated;
    // A terminal reset can look like the 99->0 counter wrap. Never turn a
    // death/win/clear (or a decreasing clear transition) into a coin pickup.
    int coin_delta=(dead||won||cleared||(meaningful_event&&e->coins<old_coins))
        ? 0 : retro_coin_delta(old_coins,e->coins);
    int idle_event=retro_idle_event(&e->idle_streak,old_x,e->x_pos,coin_delta,
        meaningful_event,e->idle_grace_decisions,dead||won||cleared);
    e->episode_coin_events+=coin_delta;
    e->episode_idle_steps+=idle_event;
    float reward=retro_rom_reward(e,advances,
        continue_lives?life_losses>0:dead,e->score-old_score,checkpoints,clear_speed,
        coin_delta,idle_event,completion_target_bonus);
    reward+=segment_reward*e->reward_scale;
    e->episode_return+=reward; e->episode_decisions++;
    if(continue_lives) e->log.deaths+=life_losses;
    if(done) {
        e->log.n++; e->log.episode_return+=e->episode_return; e->log.episode_length+=e->tick;
        e->log.frames+=e->tick; e->log.decisions+=e->episode_decisions;
        e->log.progress_pixels+=e->progress_pixels;
        e->log.checkpoints+=e->progress_pixels/e->checkpoint_distance;
        e->log.score+=e->score; e->log.distance+=e->x_pos_max; e->log.coins+=e->coins;
        e->log.flag+=e->episode_clears>0; e->log.clears+=e->episode_clears; e->log.perf+=e->episode_clears>0;
        e->log.clear_frame_sum+=e->episode_clear_frames;
        if(e->rta.first_split_frames>0) {
            e->log.rta_frame_sum+=e->rta.first_split_frames;
            e->log.rta_count++;
        }
        e->log.coin_events+=e->episode_coin_events;
        e->log.idle_steps+=e->episode_idle_steps;
        e->log.area_transitions+=e->episode_area_transitions;
        e->log.novel_areas+=e->episode_novel_areas;
        if(!continue_lives) e->log.deaths+=dead;
        e->log.truncations+=e->last_truncated; e->log.warps+=e->episode_warps;
        e->log.level_episodes[e->episode_spawn]++; e->boundary_reached=1;
        puf_reset(e);
    } else if(e->agents[0].observations) retro_compute_obs_real(e,(obs_t*)e->agents[0].observations);
    e->agents[0].rewards[0]=reward; e->agents[0].terminals[0]=done?1:0;
}
void puf_step(Env* e) { retro_step(e,false); }
void puf_log(Log* log,Dict* out) {
    // Ratio of sums: failed/unfinished attempts add neither frames nor a
    // sample. Normalization by episode count cancels out. Put these first so
    // the terminal dashboard shows them even with only a few rows available.
    double rta_frames=log->rta_count>0?log->rta_frame_sum/log->rta_count:0;
    // Never publish PAL-on-NTSC simulation times as valid speedrun results.
    bool practice=log->practice>0;
    dict_set(out,practice?"segment_seconds":RETRO_RTA_COMPARABLE?"rta_seconds":"sim_seconds",retro_frame_seconds(rta_frames));
    dict_set(out,practice?"segment_clear_rate":RETRO_RTA_COMPARABLE?"rta_clear_rate":"split_clear_rate",log->rta_count);
    dict_set(out,practice?"segment_frames":RETRO_RTA_COMPARABLE?"rta_frames":"split_frames",rta_frames);
    dict_set(out,"rta_valid",!practice&&RETRO_RTA_COMPARABLE?1:0);
    dict_set(out,"clear_seconds",log->clears>0?retro_frame_seconds(log->clear_frame_sum/log->clears):0);
#define RETRO_LOG(name) dict_set(out,#name,log->name)
    RETRO_LOG(perf); RETRO_LOG(score); RETRO_LOG(episode_return); RETRO_LOG(episode_length);
    RETRO_LOG(distance); RETRO_LOG(flag); RETRO_LOG(deaths); dict_set(out,"hud_coins",log->coins);
    RETRO_LOG(truncations); RETRO_LOG(frames); RETRO_LOG(decisions); RETRO_LOG(clears); RETRO_LOG(warps);
    RETRO_LOG(progress_pixels); RETRO_LOG(checkpoints); RETRO_LOG(coin_events); RETRO_LOG(idle_steps);
    RETRO_LOG(area_transitions); RETRO_LOG(novel_areas);
    // All raw Log fields are divided by episode count before this hook.
    // The ratio below is therefore frames per cleared level, not per episode.
    dict_set(out,"clear_frames",log->clears>0?log->clear_frame_sum/log->clears:0);
#undef RETRO_LOG
    for(int i=0;i<32;i++) {
        char key[64]; snprintf(key,sizeof(key),"level_%d_%d_episodes",i/4+1,i%4+1); dict_set(out,key,log->level_episodes[i]);
        snprintf(key,sizeof(key),"level_%d_%d_clears",i/4+1,i%4+1); dict_set(out,key,log->level_clears[i]);
    }
}
void puf_close(Env* e) {
    if(e->emu_owned) delete e->emu;
    e->emu=nullptr; delete e->display; e->display=nullptr;
}
void puf_render(Env* e) {
    if(!IsWindowReady()) { InitWindow(768,874,"Retro / original SMB1 ROM"); SetTargetFPS(60/e->frameskip); }
    if(!e->display) e->display=new RetroDisplay{};
    if(!e->reset_image&&!e->display->valid) {
        BeginDrawing(); ClearBackground(BLACK); EndDrawing(); return;
    }
    static Texture2D texture={0}; unsigned char rgb[256*240*4];
    const unsigned char* pixels=e->reset_image?e->start->pixels:e->display->pixels;
    const short* pal=e->reset_image?e->start->palette:e->display->palette; int pitch=256;
    for(int y=0;y<240;y++) for(int x=0;x<256;x++) {
        const auto& c=Nes_Emu::nes_colors[pal[pixels[y*pitch+x]]&(Nes_Emu::color_table_size-1)];
        int p=(y*256+x)*4; rgb[p]=c.red; rgb[p+1]=c.green; rgb[p+2]=c.blue; rgb[p+3]=255;
    }
    if(!texture.id) { Image im={rgb,256,240,1,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8}; texture=LoadTextureFromImage(im); }
    UpdateTexture(texture,rgb); BeginDrawing(); ClearBackground(BLACK);
    DrawTextureEx(texture,(Vector2){0,0},0,3,WHITE);
    char clock[64],segment[64]; int elapsed=retro_rta_elapsed(e->rta,e->tick);
    retro_clock_text(clock,sizeof(clock),elapsed);
    DrawText(TextFormat("Live %s  %s   %df   TIME %d",e->practice?"SEGMENT":RETRO_RTA_COMPARABLE?"RTA":"SIM",clock,elapsed,e->time),12,730,26,RAYWHITE);
    if(e->display->last_rta_frames) {
        int id=e->display->last_rta_level;
        retro_clock_text(clock,sizeof(clock),e->display->last_rta_total_frames);
        retro_clock_text(segment,sizeof(segment),e->display->last_rta_frames);
        DrawText(TextFormat("Finished %d-%d: %s   (segment %s)",id/4+1,id%4+1,clock,segment),12,765,20,GREEN);
    } else DrawText("Finished split: -- (still running)",12,765,20,GRAY);
    int source=e->episode_spawn; unsigned long long samples=e->display->rta_counts[source];
    if(samples) {
        retro_clock_text(clock,sizeof(clock),(double)e->display->rta_frame_sums[source]/samples);
        DrawText(TextFormat("%d-%d completed avg: %s   (%llu runs)",source/4+1,source%4+1,clock,samples),12,793,20,YELLOW);
    } else DrawText("Completed avg: -- (0 runs)",12,793,20,GRAY);
    if(e->display->last_clear_frames) {
        retro_clock_text(clock,sizeof(clock),e->display->last_clear_frames);
        int id=e->display->last_clear_level;
        DrawText(TextFormat("Reward finish %d-%d: %s / %df",id/4+1,id%4+1,
            clock,e->display->last_clear_frames),12,823,18,LIGHTGRAY);
    }
    DrawText(e->practice?"PIPE-EXIT PRACTICE | segment time, NOT full-run RTA | R resets":RETRO_RTA_COMPARABLE?"NTSC RTA | black-screen splits | R restarts":
        "PAL ROM + NTSC core: NOT speedrun RTA | R restarts",12,852,14,RETRO_RTA_COMPARABLE?GRAY:ORANGE);
    EndDrawing();
}
#ifdef PUFFERLIB_BUILD_MAIN
static Env* my_vec_init(int* num_envs,int* starts,int* counts,Dict* vec,Dict* cfg) {
    int n=dict_get(vec,"total_agents"),buffers=dict_get(vec,"num_buffers");
    if(n<=0||buffers<=0||n%buffers) throw std::runtime_error("retro: total_agents must divide evenly into buffers");
    Env* envs=(Env*)calloc(n,sizeof(Env)); RetroVecArena* arena=new RetroVecArena{new Nes_Emu[n],n};
    for(int i=0;i<n;i++) {
        envs[i].rng=(unsigned int)i*2654435761u+12345u; envs[i].emu=&arena->emus[i]; envs[i].arena=arena;
        puf_init(&envs[i],cfg);
    }
    for(int b=0;b<buffers;b++) { starts[b]=b*(n/buffers); counts[b]=n/buffers; }
    *num_envs=n; return envs;
}
static void my_vec_close(Env* envs) {
    if(!envs) return;
    RetroVecArena* arena=envs[0].arena;
    for(int i=0;i<arena->count;i++) { delete envs[i].display; envs[i].display=nullptr; }
    delete[] arena->emus; delete arena; free(envs);
}
#define MY_VEC_INIT
#define MY_VEC_CLOSE
#endif
#if defined(__CUDACC__) && defined(PUFFERLIB_BUILD_MAIN)
#include "retro_sweep.h"
#endif
#endif
