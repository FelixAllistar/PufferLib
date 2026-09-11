#pragma once
#ifdef PUFFER_RETRO_LEGACY
#include "retro_legacy.h"
#else
#ifndef __cplusplus
#error "retro requires C++ and the QuickNES ROM core"
#endif
#ifdef PUFFER_GPU_ENV
#error "retro has no validated CUDA ROM core"
#endif
#include "pufferenv.h"
#include "retro_obs.h"
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
    float progress_pixels, checkpoints;
    float level_episodes[32], level_clears[32];
    float n;
};
struct RetroStart {
    Nes_State state;
    unsigned char pixels[256*240];
    short palette[256];
    int world, stage, area, data, x;
};
struct RetroRom {
    Nes_Cart cart;
    Nes_Emu seed;
    Nes_State title;
    std::unique_ptr<RetroStart> starts[32];
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
    unsigned char spawn_w[32], spawn_l[32];
    int episode_spawn, episode_clears, episode_warps, last_frames;
    unsigned int rewarded_levels;
    float episode_return, potential_gamma, completion_reward, death_penalty, score_scale, reward_scale;
    float checkpoint_reward;
    int checkpoint_distance, progress_pixels, episode_decisions, frontier_count;
    RetroFrontier frontiers[256];
    bool emu_ok, emu_owned, full_render, reset_image, last_truncated;
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
    if(hash!=0x31d802e3779199daull)
        throw std::runtime_error("retro: ROM differs from validated SMB1 NTSC image (see README SHA-256)");
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
    fprintf(stderr,"[retro] ROM %s fingerprint=%016llx mapper=0 NTSC; original CPU/PPU/APU\n",path,hash);
}
static int retro_start_area(const RetroRom& rom,int w,int l) {
    const unsigned char* prg=rom.cart.prg();
    int base=prg[0x9cb4-0x8000+w-1],area=0;
    for(int s=1;s<l;s++) { if(prg[0x9cbc-0x8000+base+area]==0x29) area++; area++; }
    return area;
}
static void retro_prepare_start_locked(RetroRom& rom,int w,int l) {
    int id=retro_level_id(w,l); if(rom.starts[id]) return;
    std::unique_ptr<RetroStart> start(new RetroStart());
    Nes_Emu& emu=rom.seed;
    emu.load_state(rom.title); retro_bind_pixels(&emu);
    unsigned char* m=emu.low_mem(); int area=retro_start_area(rom,w,l);
    // Reset setup only: ROM menu code resolves the area and runs the entrance.
    m[0x75f]=w-1; m[0x75c]=l-1; m[0x760]=area;
    retro_check(emu.emulate_frame(RETRO_BTN_START,0));
    bool ready=false;
    for(int i=0;i<1200;i++) {
        retro_check(emu.emulate_frame(0,0));
        if(m[0x770]==1&&m[0x772]==3&&m[0xe]==8&&robs_time(m)>0) { ready=true; break; }
    }
    int base=rom.cart.prg()[0x9cb4-0x8000+w-1];
    int expected=rom.cart.prg()[0x9cbc-0x8000+base+area];
    // Underground stages begin with a ROM-controlled pipe intermission.
    // By the first controllable frame, the ROM has entered the following area.
    if(expected==0x29) { area++; expected=rom.cart.prg()[0x9cbc-0x8000+base+area]; }
    const unsigned char* prg=rom.cart.prg();
    int offset=prg[0x9d28-0x8000+((expected>>5)&3)]+(expected&31);
    int expected_data=prg[0x9d2c - 0x8000 + offset]+256*prg[0x9d4e - 0x8000 + offset]+2;
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
static int retro_progress(Env* e,unsigned int key,int x) {
    // Bound coordinate-wrap/glitch jackpots without changing ROM execution.
    x=std::max(0,std::min((int)RETRO_POT_XMAX,x));
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
static void retro_compute_obs_real(const Env* e,obs_t* obs) {
    float values[OBS_SIZE];
    RetroScalars sc={e->x_pos,e->x_pos_max,e->coins,e->score,e->tick,e->world,e->stage,e->area,e->time,e->has_flag,e->is_dead,0,0};
    const unsigned char* m=e->emu->low_mem(); retro_ego_ent(values,m,&sc);
    const auto& fr=e->emu->frame();
    const unsigned char* pixels=e->reset_image?e->start->pixels:fr.pixels;
    const short* palette=e->reset_image?e->start->palette:fr.palette;
    int pitch=e->reset_image?256:(int)fr.pitch;
    float lut[256];
    for(int i=0;i<256;i++) {
        const auto& c=Nes_Emu::nes_colors[palette[i]&(Nes_Emu::color_table_size-1)];
        lut[i]=retro_luma(c.red,c.green,c.blue);
    }
    int mx=(m[0x86]-m[0x71c])&255,my=m[0x3b8]; if(my>=240) my=120;
    int xs[96],ys[96];
    for(int i=0;i<96;i++) { xs[i]=std::max(0,std::min(255,mx-48+i)); ys[i]=std::max(0,std::min(239,my-48+i))*pitch; }
    int idx=RETRO_EGO_SIZE+RETRO_ENT_SIZE;
    for(int ty=0;ty<12;ty++) for(int tx=0;tx<12;tx++) {
        float sum=0;
        if(pixels) for(int y=0;y<8;y++) {
            const unsigned char* row=pixels+ys[ty*8+y];
            for(int x=0;x<8;x++) sum+=lut[row[xs[tx*8+x]]];
        }
        values[idx++]=sum*(1.0f/64);
    }
    for(int i=0;i<OBS_SIZE;i++) {
#if defined(from_float) && !defined(PRECISION_FLOAT)
        obs[i]=from_float(values[i]);
#else
        obs[i]=values[i];
#endif
    }
}
static double retro_option(Dict* cfg,const char* key,double fallback) { DictItem* i=dict_find(cfg,key); return i?i->value:fallback; }
void puf_init(Env* e,Dict* cfg) {
    Nes_Emu* supplied=e->emu; RetroVecArena* arena=e->arena; unsigned int seed=e->rng;
    memset(e,0,sizeof(*e)); e->num_agents=1; e->rng=seed?seed:0x9e3779b9u; e->emu=supplied; e->arena=arena;
    e->frameskip=retro_option(cfg,"frameskip",1); e->max_frames=retro_option(cfg,"max_frames",30000);
    e->potential_gamma=retro_option(cfg,"potential_gamma",0.997);
    e->completion_reward=retro_option(cfg,"completion_reward",10); e->death_penalty=retro_option(cfg,"death_penalty",0.125);
    double spacing=retro_option(cfg,"checkpoint_distance",128);
    e->checkpoint_reward=retro_option(cfg,"checkpoint_reward",0.125);
    if(!std::isfinite(spacing)||spacing<1||spacing>RETRO_POT_XMAX||spacing!=floor(spacing)
        ||!std::isfinite(e->checkpoint_reward)||e->checkpoint_reward<0)
        throw std::runtime_error("retro: invalid checkpoint_distance/checkpoint_reward");
    e->checkpoint_distance=(int)spacing;
    e->reward_scale=retro_option(cfg,"reward_scale",0.0625);
    e->score_scale=retro_option(cfg,"score_scale",0); e->full_render=retro_option(cfg,"full_render",0)!=0;
    const char* full=getenv("RETRO_FULL_RENDER"); if(full&&strcmp(full,"0")) e->full_render=true;
    if(e->frameskip<1||e->frameskip>16||e->max_frames<1||!(e->potential_gamma>0&&e->potential_gamma<=1)
        ||!std::isfinite(e->reward_scale)||e->reward_scale<=0)
        throw std::runtime_error("retro: invalid frameskip, max_frames, or potential_gamma");
    DictItem* be=dict_find(cfg,"backend");
    if(be&&be->str&&strcmp(be->str,"quicknes")) throw std::runtime_error("retro: ROM-only build; legacy port requires RETRO_LEGACY=1");
    DictItem* sp=dict_find(cfg,"spawn_levels"); retro_parse_spawns(e,sp&&sp->str?sp->str:"all");
    DictItem* rp=dict_find(cfg,"rom_path"); const char* path=rp&&rp->str?rp->str:"ocean/retro/roms/smb1.nes";
    RetroRom& rom=retro_rom();
    std::lock_guard<std::mutex> lock(rom.mutex);
    retro_load_rom_locked(rom,path);
    for(int i=0;i<e->spawn_n;i++) retro_prepare_start_locked(rom,e->spawn_w[i],e->spawn_l[i]);
    if(!e->emu) { e->emu=new Nes_Emu(); e->emu_owned=true; }
    retro_check(e->emu->set_cart(&rom.cart,&rom.seed));
    e->emu->set_idle_skip(retro_option(cfg,"idle_loop_skip",1)!=0);
    e->emu_ok=true;
}
void puf_reset(Env* e) {
    retro_pick_spawn(e);
    int id=retro_level_id(e->spawn_w[e->cur_spawn],e->spawn_l[e->cur_spawn]);
    e->start=retro_rom().starts[id].get();
    if(!e->start) throw std::runtime_error("retro: unprepared level start");
    e->emu->load_state(e->start->state); e->reset_image=true; retro_sync_from_emu(e);
    e->x_pos_max=e->x_pos; e->tick=0; e->episode_return=0; e->episode_clears=0; e->episode_warps=0;
    e->rewarded_levels=0; e->episode_spawn=id;
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
static float retro_rom_reward(const Env* e,float old_potential,float next_potential,
        int advances,bool dead,bool done,int score_delta,int checkpoints=0) {
    float reward=(done?0:e->potential_gamma*next_potential)-old_potential;
    reward+=advances*e->completion_reward-(dead?e->death_penalty:0);
    reward+=std::max(0,score_delta)*e->score_scale;
    // Earned checkpoints are base rewards, never cancelled by terminal PBRS.
    reward+=checkpoints*e->checkpoint_reward;
    return reward*e->reward_scale;
}
void puf_step(Env* e) {
    int action=std::max(0,std::min(63,(int)e->agents[0].actions[0]));
    unsigned char buttons=retro_action_mask(action);
    float old_potential=retro_potential(e->x_pos); int old_score=e->score;
    bool dead=false,won=false; int advances=0,checkpoints=0; e->last_frames=0;
    for(int f=0;f<e->frameskip;f++) {
        const unsigned char* m=e->emu->low_mem(); int ow=robs_world(m),ol=robs_stage(m),mode=m[0x770];
        retro_frame(e,buttons,e->full_render||f+1==e->frameskip||e->tick+1>=e->max_frames);
        e->tick++; e->last_frames++;
        checkpoints+=retro_track_progress(e);
        bool clear=retro_level_advance(ow,ol,robs_world(m),robs_stage(m))||(mode!=2&&m[0x770]==2);
        int id=retro_level_id(ow,ol);
        if(clear&&id>=0&&!(e->rewarded_levels&(1u<<id))) {
            e->rewarded_levels|=1u<<id; advances++; e->episode_clears++; e->log.level_clears[id]++;
            if(robs_world(m)>ow+(ol==4)||(robs_world(m)==ow&&robs_stage(m)>ol+1)) e->episode_warps++;
        }
        dead=robs_dead(m)||robs_gameover(m); won=ow==8&&mode!=2&&m[0x770]==2;
        if(dead||won||e->tick>=e->max_frames) break;
    }
    retro_sync_from_emu(e); e->x_pos_max=std::max(e->x_pos_max,e->x_pos);
    e->last_truncated=e->tick>=e->max_frames&&!dead&&!won;
    bool done=dead||won||e->last_truncated;
    // Finite-horizon task; all task terminals have zero potential. Area
    // transitions retain the complete potential difference (no omitted edge).
    float reward=retro_rom_reward(e,old_potential,retro_potential(e->x_pos),advances,dead,done,e->score-old_score,checkpoints);
    e->episode_return+=reward; e->episode_decisions++;
    if(done) {
        e->log.n++; e->log.episode_return+=e->episode_return; e->log.episode_length+=e->tick;
        e->log.frames+=e->tick; e->log.decisions+=e->episode_decisions;
        e->log.progress_pixels+=e->progress_pixels;
        e->log.checkpoints+=e->progress_pixels/e->checkpoint_distance;
        e->log.score+=e->score; e->log.distance+=e->x_pos_max; e->log.coins+=e->coins;
        e->log.flag+=e->episode_clears>0; e->log.clears+=e->episode_clears; e->log.perf+=e->episode_clears>0;
        e->log.deaths+=dead; e->log.truncations+=e->last_truncated; e->log.warps+=e->episode_warps;
        e->log.level_episodes[e->episode_spawn]++; e->boundary_reached=1;
        puf_reset(e);
    } else if(e->agents[0].observations) retro_compute_obs_real(e,(obs_t*)e->agents[0].observations);
    e->agents[0].rewards[0]=reward; e->agents[0].terminals[0]=done?1:0;
}
void puf_log(Log* log,Dict* out) {
#define RETRO_LOG(name) dict_set(out,#name,log->name)
    RETRO_LOG(perf); RETRO_LOG(score); RETRO_LOG(episode_return); RETRO_LOG(episode_length);
    RETRO_LOG(distance); RETRO_LOG(flag); RETRO_LOG(deaths); RETRO_LOG(coins);
    RETRO_LOG(truncations); RETRO_LOG(frames); RETRO_LOG(decisions); RETRO_LOG(clears); RETRO_LOG(warps);
    RETRO_LOG(progress_pixels); RETRO_LOG(checkpoints);
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
    if(!IsWindowReady()) { InitWindow(768,720,"Retro / original SMB1 ROM"); SetTargetFPS(60/e->frameskip); }
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
    DrawTextureEx(texture,(Vector2){0,0},0,3,WHITE); EndDrawing();
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
