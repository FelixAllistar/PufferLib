#pragma once
// Historical two-backend environment. Build only with RETRO_LEGACY=1.
// Retro / SMB1 -- 8 worlds * 4 stages = 32 levels. No procgen.
// Each Env runs its own QuickNES state, while the immutable cartridge is
// shared. CPU vector workers remain independent and need no core mutex.
// OBS 256 = 64 ego/physics + 48 entities + 12*12 window sampled from
// framebuffer around Mario. Ego/entities come straight from NES RAM
// (low_mem, 0x800 bytes), so the same builder runs on libretro via
// RETRO_MEMORY_SYSTEM_RAM + video framebuffer (sim2real: policy only).
// ACT 12 discrete (RETRO_ACTION_MASKS). C++ (pufferl) path uses Nes_Emu
// directly; C fallback (if __cplusplus not defined) is stub.

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "pufferenv.h"

#ifdef PUFFER_TUI_CAPTURE
#include "puffer_tui.h"
#endif

#define RETRO_MAX_LEVELS 32
#define RETRO_LEVEL_W 256
#define RETRO_LEVEL_H 16
#define NUM_ATNS 1
#define ACT_SIZES {12}
#define RETRO_NUM_ACTIONS 12
// Obs layout lives in retro_obs.h (shared with the fast native-C backend).
#include "retro_obs.h"
#define RETRO_WINDOW_RADIUS_W (RETRO_WINDOW_W/2)
#define RETRO_WINDOW_RADIUS_H (RETRO_WINDOW_H/2)

#if defined(from_float) && !defined(PRECISION_FLOAT)
typedef precision_t obs_t;
#else
typedef float obs_t;
#endif

struct Log {
    float perf;
    float score;
    float episode_return;
    float episode_length;
    float distance;
    float flag;
    float deaths;
    float coins;
    float n;
};

#define RETRO_BTN_A      (1u<<0)
#define RETRO_BTN_B      (1u<<1)
#define RETRO_BTN_SELECT (1u<<2)
#define RETRO_BTN_START  (1u<<3)
#define RETRO_BTN_UP     (1u<<4)
#define RETRO_BTN_DOWN   (1u<<5)
#define RETRO_BTN_LEFT   (1u<<6)
#define RETRO_BTN_RIGHT  (1u<<7)

static const unsigned char RETRO_ACTION_MASKS[RETRO_NUM_ACTIONS] = {
    0,
    RETRO_BTN_RIGHT,
    RETRO_BTN_RIGHT | RETRO_BTN_A,
    RETRO_BTN_RIGHT | RETRO_BTN_B,
    RETRO_BTN_RIGHT | RETRO_BTN_A | RETRO_BTN_B,
    RETRO_BTN_A,
    RETRO_BTN_LEFT,
    RETRO_BTN_LEFT  | RETRO_BTN_A,
    RETRO_BTN_DOWN,
    RETRO_BTN_B,
    RETRO_BTN_UP,
    RETRO_BTN_RIGHT | RETRO_BTN_DOWN,
};

#define RETRO_TILE_EMPTY    0
#define RETRO_TILE_SOLID    1
#define RETRO_TILE_BRICK    2
#define RETRO_TILE_QUESTION 3
#define RETRO_TILE_PIPE     4
#define RETRO_TILE_ENEMY    5
#define RETRO_TILE_COIN     6
#define RETRO_TILE_FLAG     7

#ifndef PUFFER_GPU_ENV

#ifdef __cplusplus
// ===== C++ path (pufferl) : per-Env Nes_Emu =====
#include "nes_emu/Nes_Emu.h"
#include "nes_emu/Nes_State.h"
#include "nes_emu/Data_Reader.h"
// Fast native-C backend types (smbcore, fetched at build time by build.sh).
// Function bodies live in retro_fast.h, included after struct Env.
#include "smbcore/mario.h"
#include "smbcore/base.h" // likely()/unlikely() used by interface.h (via mario.h)
#include "smbcore/interface.h" // NOTE: no include guard; include exactly once
extern "C" {
#include "platform/render_raster.h"
}
extern "C" {
#include "platform/render_raster.h"
}

static uint8_t* g_rom_data = NULL;
static size_t g_rom_size = 0;
static Nes_Cart g_rom_cart;
static Nes_State g_initial_state;
static bool g_initial_state_valid = false;
// Curriculum: per-spawn cached boot states (quicknes backend). Lazily
// created on first reset of that level; level-select = poke $075F/$075C
// during the title dance (same trick as gym-super-mario-bros).
static Nes_State* g_level_states[40] = {nullptr};
static bool g_rom_loaded = false;
static char g_rom_error[512] = {0};
static bool g_verbose = true;
static bool g_full_render = false;
static __thread uint8_t* t_pixel_buffer = nullptr;
static inline uint8_t* retro_thread_pixels(){
    if(!t_pixel_buffer){
        t_pixel_buffer=(uint8_t*)calloc(256*256,1);
    }
    return t_pixel_buffer;
}

// forward for helper
struct Env;

static bool retro_load_rom_global(const char* hint) {
    if (g_rom_loaded) return true;
    const char* candidates[] = {
        hint,
        "ocean/retro/roms/smb1.nes",
        "../ocean/retro/roms/smb1.nes",
        "pufferlib/ocean/retro/roms/smb1.nes",
        "/tmp/smb1_pure.nes",
        "ocean/retro/roms/smb1.nes",
        NULL
    };
    const char* chosen = NULL;
    for (int i=0; candidates[i]; i++) {
        if (!candidates[i] || !candidates[i][0]) continue;
        FILE* f = fopen(candidates[i], "rb");
        if (f) {
            fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
            if (sz>16 && sz<1024*1024) {
                uint8_t hdr[16];
                fread(hdr,1,16,f);
                if (hdr[0]=='N' && hdr[1]=='E' && hdr[2]=='S') {
                    fclose(f);
                    chosen = candidates[i];
                    break;
                }
            }
            fclose(f);
        }
        // try zip
        if (strstr(candidates[i], ".zip")) {
            char cmd[1024];
            snprintf(cmd,sizeof(cmd),"unzip -p \"%s\" \"*.nes\" 2>/dev/null | head -c 16 | od -An -t x1 | head -n1", candidates[i]);
            // just assume zip contains valid nes, try actual load via unzip -p in Data_Reader? For now skip and rely on extracted smb1.nes
        }
    }
    if (!chosen) {
        // fallback to known pure path
        chosen = "ocean/retro/roms/smb1.nes";
        FILE* f=fopen(chosen,"rb");
        if (!f) {
            snprintf(g_rom_error,sizeof(g_rom_error),"ROM not found (tried ocean/retro/roms/smb1.nes)");
            return false;
        }
        fclose(f);
    }
    FILE* f=fopen(chosen,"rb");
    if (!f) { snprintf(g_rom_error,sizeof(g_rom_error),"cannot open %s",chosen); return false; }
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    g_rom_data = (uint8_t*)malloc(sz);
    fread(g_rom_data,1,sz,f); fclose(f);
    g_rom_size = sz;
    const char* full_render = getenv("RETRO_FULL_RENDER");
    g_full_render = full_render && *full_render && strcmp(full_render,"0") != 0;
    Mem_File_Reader cart_rdr(g_rom_data, (long)g_rom_size);
    const char* cart_err = g_rom_cart.load_ines(cart_rdr);
    if (cart_err) {
        snprintf(g_rom_error,sizeof(g_rom_error),"load cartridge: %s",cart_err);
        free(g_rom_data);
        g_rom_data = NULL;
        g_rom_size = 0;
        return false;
    }
    g_rom_loaded = true;
    if (g_verbose) fprintf(stderr,"[retro] rom loaded %s %zu bytes\n", chosen, g_rom_size);
    // build initial state once via temp emu
    {
        Nes_Emu* tmp = new Nes_Emu();
        uint8_t* pix = retro_thread_pixels();
        tmp->set_pixels(pix + 8*256, 256);
        const char* err = tmp->set_cart(&g_rom_cart);
        if (err) { snprintf(g_rom_error,sizeof(g_rom_error),"load_ines: %s",err); delete tmp; return false; }
        // tmp->set_sample_rate(0); // no audio - skip to avoid crash
        // skip start screen like gym
        // press START 1 frame then run until time !=0 (like gym _skip_start_screen)
        auto read_time = [&]()->int {
            uint8_t* m = tmp->low_mem();
            return (m[0x07F8]%10)*100 + (m[0x07F9]%10)*10 + (m[0x07FA]%10);
        };
        // initial press
        tmp->emulate_frame(RETRO_BTN_START,0);
        tmp->emulate_frame(0,0);
        for(int i=0;i<300;i++){
            int t=read_time();
            if(t!=0) break;
            tmp->emulate_frame(RETRO_BTN_START,0);
            tmp->emulate_frame(0,0);
            uint8_t* m=tmp->low_mem();
            if(m) m[0x07A0]=0;
            if (tmp->low_mem()[0x075F]!=0 || tmp->low_mem()[0x075C]!=0) {
                // if target stage logic, not needed
            }
        }
        // wait for time to start decrementing (gym second loop)
        int last_t = read_time();
        for(int i=0;i<100;i++){
            if(read_time()!=last_t) break;
            tmp->emulate_frame(RETRO_BTN_START,0);
            tmp->emulate_frame(0,0);
            uint8_t* m=tmp->low_mem(); if(m) m[0x07A0]=0;
        }
        int saved_time = read_time();
        tmp->save_state(&g_initial_state);
        g_initial_state_valid = true;
        delete tmp;
        if (g_verbose) fprintf(stderr,"[retro] initial state saved time=%d\n", saved_time);
    }
    return true;
}

static inline int smb_ram_read(Nes_Emu* emu, int addr) {
    if (!emu) return 0;
    uint8_t* m = emu->low_mem();
    if (!m || addr<0 || addr>=0x800) return 0;
    return m[addr];
}
// Thin wrappers over the shared RAM readers in retro_obs.h (same addresses
// the fast backend and libretro adapter use).
static inline uint8_t* smb_mem(Nes_Emu* emu){ return emu ? emu->low_mem() : NULL; }
static inline int smb_time(Nes_Emu* emu){ uint8_t* m=smb_mem(emu); return m?robs_time(m):0; }
static inline int smb_world(Nes_Emu* emu){ uint8_t* m=smb_mem(emu); return m?robs_world(m):0; }
static inline int smb_stage(Nes_Emu* emu){ uint8_t* m=smb_mem(emu); return m?robs_stage(m):0; }
static inline int smb_area(Nes_Emu* emu){ uint8_t* m=smb_mem(emu); return m?robs_area(m):0; }
static inline int smb_score(Nes_Emu* emu){ uint8_t* m=smb_mem(emu); return m?robs_score(m):0; }
static inline int smb_coins(Nes_Emu* emu){ uint8_t* m=smb_mem(emu); return m?robs_coins(m):0; }
static inline int smb_life(Nes_Emu* emu){ return smb_ram_read(emu,0x075A); }
static inline int smb_x(Nes_Emu* emu){ uint8_t* m=smb_mem(emu); return m?robs_x(m):0; }
static inline int smb_left_x(Nes_Emu* emu){ return (smb_ram_read(emu,0x86) - smb_ram_read(emu,0x071C)) & 0xFF; }
static inline int smb_y_pixel(Nes_Emu* emu){ return smb_ram_read(emu,0x03B8); }
static inline int smb_player_state(Nes_Emu* emu){ uint8_t* m=smb_mem(emu); return m?robs_pstate(m):0; }
static inline bool smb_is_dying(Nes_Emu* emu){ uint8_t* m=smb_mem(emu); return m&&robs_dying(m); }
static inline bool smb_is_dead(Nes_Emu* emu){ uint8_t* m=smb_mem(emu); return m&&robs_dead(m); }
static inline bool smb_is_game_over(Nes_Emu* emu){ uint8_t* m=smb_mem(emu); return m&&robs_gameover(m); }
static inline bool smb_is_world_over(Nes_Emu* emu){ uint8_t* m=smb_mem(emu); return m&&robs_worldover(m); }
static inline bool smb_flag_get(Nes_Emu* emu){ uint8_t* m=smb_mem(emu); return m&&robs_flagget(m); }

struct Env {
    Log log;
    Agent agents[1];
    int num_agents;
    int tag;
    int boundary_reached;
    unsigned int rng;
    int tick;
    int world, stage, area;
    int x_pos, x_pos_max;
    int score, coins, time, life;
    int has_flag, is_dead;
    int frameskip;
    int window_w, window_h;
    float potential_gamma; // must equal train.gamma (PBRS invariance)
    RetroWeights rw;       // sparse-event reward weights ([env] config)
    // Curriculum: per-episode spawn level ([env] spawn_levels: '' = 1-1 only
    // (default, byte-identical to pre-curriculum behavior); 'all' = every
    // world/stage; or CSV like '1-1,1-2,2-1'). Prevents 1-1 overfitting and
    // forces transferable skills; PBRS potential is already per-area gated.
    int spawn_n;
    unsigned char spawn_w[40], spawn_l[40];
    int cur_spawn;
    // Watch --continue: when set, retro_pick_spawn keeps cur_spawn (the
    // caller advances it manually after flag/d death). Default 0.
    int spawn_pin;
    // Enemy-kill tracking snapshot for retro_kill_scan (per-step deltas).
    unsigned char prev_eid[RETRO_NUM_ENEMIES];
    short prev_ex[RETRO_NUM_ENEMIES];
    int kills_total;
    float gravity, max_vx, jump_v, run_accel, friction;
    unsigned char* level_tiles;
    unsigned char* entity_tiles;
    char rom_path[512];
    char core_path[512];
    void* client; // retro client placeholder
    // per-Env emu
    Nes_Emu* emu;
    uint8_t* pixels;
    bool emu_ok;
    bool emu_owned;
    struct RetroVecArena* arena;
    // fast native-C backend (env.backend=fast): per-env smbcore state.
    // Full SMB_state per env (~90KB: 2KB RAM + 16KB PPU regs/nametables live
    // here; the 32KB PRG + 8KB CHR copies are immutable after init). Snap
    // holds only the varying part for instant reset.
    int use_fast;
    SMB_state* fast;
    bool fast_owned;
    struct RetroFastArena* fast_arena;
    int fast_idx;
    int fast_clone_src; // >=0: clone boot state from arena env instead of booting
};

// Refresh the kill-tracker snapshot at init/reset: a stale "enemy alive"
// entry from the previous episode would false-fire on the first scan.
static inline void retro_kill_reset(Env* env){
    for(int i=0;i<RETRO_NUM_ENEMIES;i++){ env->prev_eid[i]=0; env->prev_ex[i]=0; }
    env->kills_total = 0;
};

// Parse [env] spawn_levels into the per-episode spawn table.
static inline void retro_parse_spawns(Env* env, const char* spec){
    env->spawn_w[0]=1; env->spawn_l[0]=1; env->spawn_n=1;
    if(!spec || !spec[0] || !strcmp(spec,"1-1")) return;
    if(!strcmp(spec,"all")){
        env->spawn_n=0;
        for(int w=1;w<=8;w++) for(int l=1;l<=4;l++){
            if(env->spawn_n>=40) break;
            env->spawn_w[env->spawn_n]=(unsigned char)w;
            env->spawn_l[env->spawn_n]=(unsigned char)l;
            env->spawn_n++;
        }
        return;
    }
    env->spawn_n=0;
    const char* p=spec;
    while(*p && env->spawn_n<40){
        int w=0,l=0;
        if(sscanf(p,"%d-%d",&w,&l)==2 && w>=1 && w<=8 && l>=1 && l<=4){
            env->spawn_w[env->spawn_n]=(unsigned char)w;
            env->spawn_l[env->spawn_n]=(unsigned char)l;
            env->spawn_n++;
        }
        while(*p && *p!=',') p++;
        if(*p==',') p++;
    }
    if(env->spawn_n==0){ env->spawn_w[0]=1; env->spawn_l[0]=1; env->spawn_n=1; }
}

// Pick this episode's spawn level (rng-driven; same seed -> same sequence).
// Honors spawn_pin (watch --continue manages cur_spawn itself).
static inline void retro_pick_spawn(Env* env){
    if(env->spawn_pin) return;
    if(env->spawn_n<=1){ env->cur_spawn=0; return; }
    unsigned int r=env->rng;
    r^=r<<13; r^=r>>17; r^=r<<5;
    env->rng=r;
    env->cur_spawn=(int)(r%(unsigned)env->spawn_n);
};

// Index of world w, stage l in the spawn table (0 = 1-1 fallback).
static inline int retro_spawn_index(Env* env, int w, int l){
    for(int i=0;i<env->spawn_n;i++)
        if(env->spawn_w[i]==(unsigned char)w && env->spawn_l[i]==(unsigned char)l) return i;
    return 0;
};

struct RetroFastSnap {
    uint8_t ram[0x800];
    uint8_t ppuram[0x4000];
    struct ppu_state ppu;
    const uint8_t *area_data, *enemy_data, *music_data;
    bool reset_occurred;
    uint8_t start_world, start_level;
};

struct RetroFastArena {
    SMB_state* st;
    RetroFastSnap* snap;
    // Curriculum level snaps (indexed by Env.cur_spawn; lazily filled at
    // init time, single-threaded). Only used when spawn_n > 1.
    RetroFastSnap* lsnap;
    int lsnap_n;
    int count;
};

struct RetroVecArena {
    Nes_Emu* emus;
    int count;
};

static void retro_sync_from_emu(Env* env){
    Nes_Emu* e=env->emu;
    if(!e) return;
    env->world=smb_world(e);
    env->stage=smb_stage(e);
    env->area=smb_area(e);
    env->x_pos=smb_x(e);
    env->score=smb_score(e);
    env->coins=smb_coins(e);
    env->time=smb_time(e);
    env->life=smb_life(e);
    env->has_flag=smb_flag_get(e)?1:0;
    env->is_dead=smb_is_dead(e)||smb_is_dying(e)?1:0;
}

// Fast native-C backend (env.backend=fast). Same ABI; see retro_fast.h.
#include "retro_fast.h"

static void retro_compute_obs_real(const Env* env, obs_t* obs){
    const Nes_Emu* e = env->emu;
    Nes_Emu* m = (Nes_Emu*)e;
    float o[OBS_SIZE];
    uint8_t* ram = m ? m->low_mem() : NULL;
    if(ram){
        RetroScalars sc;
        sc.x_pos=env->x_pos; sc.x_pos_max=env->x_pos_max; sc.coins=env->coins;
        sc.score=env->score; sc.tick=env->tick; sc.world=env->world;
        sc.stage=env->stage; sc.area=env->area; sc.time=env->time;
        sc.has_flag=env->has_flag; sc.is_dead=env->is_dead;
        retro_ego_ent(o, ram, &sc);
    } else {
        for(int i=0;i<RETRO_EGO_SIZE+RETRO_ENT_SIZE;i++) o[i]=0;
    }
    for(int i=0;i<RETRO_EGO_SIZE+RETRO_ENT_SIZE;i++){
#if defined(from_float) && !defined(PRECISION_FLOAT)
        obs[i]=from_float(o[i]);
#else
        obs[i]=o[i];
#endif
    }
    int idx=RETRO_EGO_SIZE+RETRO_ENT_SIZE;
    if(e && e->frame().pixels){
        const Nes_Emu::frame_t& fr = e->frame();
        // 12x12-TILE window, per-tile mean luma of the full 8x8 block
        // (matches the fast backend; single-pixel sampling aliased away
        // coins and pits). Backend-agnostic luma: resolve the indexed pixel
        // through the base 64-entry NES palette (the fast native-C backend
        // renders through the same table), so the window matches across
        // renderers. Emphasis bits are masked out; SMB1 does not use them.
        int mx = smb_left_x((Nes_Emu*)e);
        int my = smb_y_pixel((Nes_Emu*)e);
        if(my<0) my=0; if(my>=240) my=120;
        for(int dy=-RETRO_WINDOW_RADIUS_H; dy<RETRO_WINDOW_H-RETRO_WINDOW_RADIUS_H; dy++){
            for(int dx=-RETRO_WINDOW_RADIUS_W; dx<RETRO_WINDOW_W-RETRO_WINDOW_RADIUS_W; dx++){
                int bx = mx + dx*8;
                int by = my + dy*8;
                float acc = 0; int n = 0;
                for(int py=0; py<8; py++){
                    int sy = by + py;
                    if(sy<0) sy=0; if(sy>=240) sy=239;
                    for(int px=0; px<8; px++){
                        int sx = bx + px;
                        if(sx<0) sx=0; if(sx>=256) sx=255;
                        uint8_t pix = 0;
                        if(fr.pixels) pix = fr.pixels[sy*256 + sx];
                        int slot = fr.palette[pix & 63] & (Nes_Emu::color_table_size-1);
                        const Nes_Emu::rgb_t& rgb = Nes_Emu::nes_colors[slot];
                        acc += retro_luma(rgb.red, rgb.green, rgb.blue); n++;
                    }
                }
                float luma = n ? acc / n : 0.0f;
#if defined(from_float) && !defined(PRECISION_FLOAT)
                obs[idx++]=from_float(luma);
#else
                obs[idx++]=luma;
#endif
            }
        }
    } else {
        for(int i=0;i<RETRO_TILES;i++){
#if defined(from_float) && !defined(PRECISION_FLOAT)
            obs[idx++]=from_float(0);
#else
            obs[idx++]=0;
#endif
        }
    }
}

void puf_init(Env* env, Dict* kwargs){
    Nes_Emu* supplied_emu = env->emu;
    RetroVecArena* supplied_arena = env->arena;
    bool supplied_emu_owned = env->emu_owned;
    RetroFastArena* supplied_fast = env->fast_arena;
    int supplied_fast_idx = env->fast_idx;
    bool supplied_fast_owned = env->fast_owned;
    int supplied_clone_src = env->fast_clone_src;
    memset(env,0,sizeof(*env));
    env->num_agents=1; env->agents[0].policy=0;
    env->emu = supplied_emu;
    env->arena = supplied_arena;
    env->emu_owned = supplied_emu_owned;
    env->fast_arena = supplied_fast;
    env->fast_idx = supplied_fast_idx;
    env->fast_owned = supplied_fast_owned;
    env->fast_clone_src = supplied_clone_src;
    DictItem* it;
    DictItem* be_it=dict_find(kwargs,"backend");
    env->use_fast = (be_it && be_it->str && strcmp(be_it->str,"fast")==0) ? 1 : 0;
    it=dict_find(kwargs,"frameskip"); env->frameskip = it? (int)it->value : 4;
    it=dict_find(kwargs,"gravity"); env->gravity = it? (float)it->value : 0.52f;
    it=dict_find(kwargs,"potential_gamma"); env->potential_gamma = it? (float)it->value : 0.99f;
    // Sparse-event reward weights ([env] in the env ini). Defaults reproduce
    // the original hardcoded values exactly.
    it=dict_find(kwargs,"score_scale"); env->rw.score = it? (float)it->value : 0.01f;
    it=dict_find(kwargs,"coin_reward"); env->rw.coin = it? (float)it->value : 0.5f;
    it=dict_find(kwargs,"kill_reward"); env->rw.kill = it? (float)it->value : 1.0f;
    it=dict_find(kwargs,"death_penalty"); env->rw.death = it? (float)it->value : 2.5f;
    it=dict_find(kwargs,"flag_reward"); env->rw.flag = it? (float)it->value : 5.0f;
    it=dict_find(kwargs,"area_reward"); env->rw.area = it? (float)it->value : 2.0f;
    it=dict_find(kwargs,"idle_penalty"); env->rw.idle = it? (float)it->value : 0.0f;
    { DictItem* sp=dict_find(kwargs,"spawn_levels");
      retro_parse_spawns(env, (sp&&sp->str)?sp->str:""); }
    env->cur_spawn = 0;
    env->kills_total = 0;
    for(int i=0;i<RETRO_NUM_ENEMIES;i++){ env->prev_eid[i]=0; env->prev_ex[i]=0; }
    it=dict_find(kwargs,"max_vx"); env->max_vx = it? (float)it->value : 2.8f;
    it=dict_find(kwargs,"jump_v"); env->jump_v = it? (float)it->value : -6.2f;
    it=dict_find(kwargs,"run_accel"); env->run_accel = it? (float)it->value : 0.22f;
    it=dict_find(kwargs,"friction"); env->friction = it? (float)it->value : 0.88f;
    const char* rp=NULL; DictItem* rp_it=dict_find(kwargs,"rom_path"); if(rp_it&&rp_it->str) rp=rp_it->str;
    if(rp) snprintf(env->rom_path,sizeof(env->rom_path),"%s",rp);
    const char* cp=NULL; DictItem* cp_it=dict_find(kwargs,"core_path"); if(cp_it&&cp_it->str) cp=cp_it->str;
    if(cp) snprintf(env->core_path,sizeof(env->core_path),"%s",cp);
    env->window_w=RETRO_WINDOW_W; env->window_h=RETRO_WINDOW_H;
    const char* rom_hint = env->rom_path[0]?env->rom_path:NULL;
    if(env->use_fast){
        if(!fast_load_rom_global(rom_hint)){
            fprintf(stderr,"[retro] fast rom load failed: %s\n", g_fast_rom_error);
            env->emu_ok=false;
            env->fast=nullptr;
            return;
        }
        if(!env->fast_arena){
            env->fast_arena = (RetroFastArena*)calloc(1, sizeof(RetroFastArena));
            if(!fast_arena_alloc(env->fast_arena, 1)){
                fprintf(stderr,"[retro] fast arena alloc failed\n");
                env->emu_ok=false;
                return;
            }
            env->fast_owned = true;
            env->fast_idx = 0;
        }
        env->fast = &env->fast_arena->st[env->fast_idx];
        if (env->fast_clone_src >= 0 && env->fast_clone_src < env->fast_arena->count &&
            env->fast_clone_src != env->fast_idx) {
            // Boot-clone: bit-identical to a fresh boot (deterministic dance).
            fast_clone_env(env->fast_arena, env->fast_idx, env->fast_clone_src);
        } else if(!fast_boot_env(env->fast_arena, env->fast_idx)){
            fprintf(stderr,"[retro] fast boot to gameplay failed\n");
            env->emu_ok=false;
            env->fast=nullptr;
            return;
        }
        // Curriculum: pre-boot every spawn level's snap once, single-threaded
        // (env 0's state is scratch here). ~300 ticks per level, one-time.
        // snap[0]/st[0] are preserved: level snaps go to lsnap[]. Runs on the
        // boot env (fast_idx==0, not a clone) for owned AND external arenas.
        if (env->spawn_n > 1 && env->fast_idx == 0 && env->fast_clone_src < 0) {
            RetroFastArena* a = env->fast_arena;
            RetroFastSnap snap0 = a->snap[0]; // env0's 1-1 boot snap
            a->lsnap[0] = snap0;              // spawn 0 = fresh 1-1 boot
            for (int L = 1; L < env->spawn_n && L < a->lsnap_n; L++) {
                if (fast_boot_env_level(a, 0, env->spawn_w[L], env->spawn_l[L])) {
                    fast_snap_save(a, 0);
                    a->lsnap[L] = a->snap[0];
                } else {
                    fprintf(stderr, "[retro] curriculum boot %d-%d failed; using 1-1\n",
                        env->spawn_w[L], env->spawn_l[L]);
                    a->lsnap[L] = snap0;
                }
            }
            a->snap[0] = snap0;
            fast_snap_load(a, 0); // restore env 0 to the 1-1 boot state
            fprintf(stderr, "[retro] curriculum: %d spawn levels pre-booted\n", env->spawn_n);
        }
        fast_reset_to_snap(env);
        if (g_verbose) fprintf(stderr,"[retro-fast] env booted time=%d x=%d\n",
            env->time, env->x_pos);
        env->emu_ok=true;
        return;
    }
    if(!retro_load_rom_global(rom_hint)){
        fprintf(stderr,"[retro] rom load failed: %s\n", g_rom_error);
        env->emu_ok=false;
        env->emu=nullptr;
        return;
    }
    // per-Env emu
    if(!env->emu){
        env->emu = new Nes_Emu();
        env->emu_owned = true;
    }
    // per-thread pixel buffer to save 65KB*4096
    env->pixels = nullptr;
    uint8_t* pix = retro_thread_pixels();
    env->emu->set_pixels(pix + 8*256, 256);
    const char* err = env->emu->set_cart(&g_rom_cart);
    if(err){ fprintf(stderr,"[retro] load_ines failed: %s\n",err); env->emu_ok=false; return; }
    // env->emu->set_sample_rate(0);
    // load initial state
    if(g_initial_state_valid){
        env->emu->load_state(g_initial_state);
    } else {
        // fallback: should have been created
        env->emu->save_state(&g_initial_state);
        g_initial_state_valid=true;
    }
    retro_sync_from_emu(env);
    env->x_pos_max = env->x_pos;
    env->tick=0;
    env->emu_ok=true;
    retro_kill_reset(env);
}

void puf_log(Log* log, Dict* out){
    dict_set(out,"perf",log->perf);
    dict_set(out,"score",log->score);
    dict_set(out,"episode_return",log->episode_return);
    dict_set(out,"episode_length",log->episode_length);
    dict_set(out,"distance",log->distance);
    dict_set(out,"flag",log->flag);
    dict_set(out,"deaths",log->deaths);
    dict_set(out,"coins",log->coins);
}

// Boot the quicknes core straight to world w, level l (level-select via
// $075F/$075C pokes during the title dance, as gym-super-mario-bros does).
// Saves the state into out. Requires the ROM to already be loaded.
static bool retro_boot_quicknes_level(int w, int l, Nes_State* out){
    Nes_Emu* tmp = new Nes_Emu();
    uint8_t* pix = retro_thread_pixels();
    tmp->set_pixels(pix + 8*256, 256);
    const char* err = tmp->set_cart(&g_rom_cart);
    if(err){ delete tmp; return false; }
    auto read_time = [&]()->int {
        uint8_t* m = tmp->low_mem();
        return (m[0x07F8]%10)*100 + (m[0x07F9]%10)*10 + (m[0x07FA]%10);
    };
    tmp->emulate_frame(RETRO_BTN_START,0);
    tmp->emulate_frame(0,0);
    for(int i=0;i<300;i++){
        uint8_t* m = tmp->low_mem();
        if(m){ m[0x075F]=(uint8_t)(w-1); m[0x075C]=(uint8_t)(l-1); m[0x07A0]=0; }
        if(read_time()!=0) break;
        tmp->emulate_frame(RETRO_BTN_START,0);
        tmp->emulate_frame(0,0);
    }
    int last_t = read_time();
    for(int i=0;i<100;i++){
        if(read_time()!=last_t) break;
        tmp->emulate_frame(RETRO_BTN_START,0);
        tmp->emulate_frame(0,0);
        uint8_t* m = tmp->low_mem();
        if(m){ m[0x075F]=(uint8_t)(w-1); m[0x075C]=(uint8_t)(l-1); m[0x07A0]=0; }
    }
    tmp->save_state(out);
    delete tmp;
    return true;
}

// Load this env's current spawn state (curriculum-aware); lazily boots the
// level for the quicknes backend on first use.
static void retro_quicknes_load_spawn(Env* env){
    if(env->spawn_n > 1){
        int L = env->cur_spawn; if(L<0||L>=40) L=0;
        if(!g_level_states[L]){
            g_level_states[L] = new Nes_State();
            if(!retro_boot_quicknes_level(env->spawn_w[L], env->spawn_l[L], g_level_states[L])){
                fprintf(stderr,"[retro] quicknes level boot %d-%d failed; using 1-1\n",
                    env->spawn_w[L], env->spawn_l[L]);
                delete g_level_states[L];
                g_level_states[L] = new Nes_State();
                *g_level_states[L] = g_initial_state;
            }
        }
        env->emu->load_state(*g_level_states[L]);
    } else {
        env->emu->load_state(g_initial_state);
    }
}

void puf_reset(Env* env){
    retro_pick_spawn(env);
    if(!env->emu_ok) return;
    if(env->use_fast){
        if(!env->fast) return;
        Log saved=env->log;
        fast_reset_to_snap(env);
        env->log=saved;
        if(env->agents[0].observations) retro_fast_obs(env, (obs_t*)env->agents[0].observations);
        return;
    }
    if(!env->emu) return;
    retro_quicknes_load_spawn(env);
    retro_sync_from_emu(env);
    env->x_pos_max = env->x_pos;
    env->tick=0; env->has_flag=0; env->is_dead=0;
    retro_kill_reset(env);
    if(env->agents[0].observations) retro_compute_obs_real(env, (obs_t*)env->agents[0].observations);
}

void puf_step(Env* env){
    if(env->use_fast){ puf_step_fast(env); return; }
    if(!env->emu_ok || !env->emu){ env->agents[0].rewards[0]=0; env->agents[0].terminals[0]=1; return; }
    env->agents[0].rewards[0]=0; env->agents[0].terminals[0]=0;
    // puf_init runs on the main thread, while stepping runs on OMP workers.
    // Rebind the scratch framebuffer here so workers never render into one
    // shared thread-local buffer.
    uint8_t* pix = retro_thread_pixels();
    env->emu->set_pixels(pix + 8*256, 256);
    int act=0; if(env->agents[0].actions) act=(int)env->agents[0].actions[0];
    if(act<0) act=0; if(act>=RETRO_NUM_ACTIONS) act=RETRO_NUM_ACTIONS-1;
    unsigned char mask = RETRO_ACTION_MASKS[act];
    float reward=0;
    bool done=false;
    bool froze=false;
    int eng0_run=0;
    RetroScalars prev;
    prev.x_pos=env->x_pos; prev.x_pos_max=env->x_pos_max; prev.coins=env->coins;
    prev.score=env->score; prev.tick=env->tick; prev.world=env->world;
    prev.stage=env->stage; prev.area=env->area; prev.time=env->time;
    prev.has_flag=env->has_flag; prev.is_dead=env->is_dead;
    prev.kills=env->kills_total; prev.idle=0;
    for(int f=0; f<env->frameskip; f++){
        env->tick++;
        // PPO only observes after the action's final frame. QuickNES still
        // advances the complete CPU/PPU/APU state in skip mode, but avoids
        // writing an intermediate 256x240 framebuffer.
        const bool draw = (f + 1 == env->frameskip)
            || g_full_render;
        uint8_t* mb = env->emu->low_mem();
        int fc_before = mb ? mb[0x0009] : -1;
        const char* err = draw
            ? env->emu->emulate_frame(mask,0)
            : env->emu->emulate_skip_frame_fast(mask,0);
        (void)err;
        retro_sync_from_emu(env);
        uint8_t* ma = env->emu->low_mem();
        if(ma && fc_before >= 0 && ma[0x0009] == fc_before){
            // Frame counter stalled: input-dead freeze state. End the
            // episode so one frozen env can never wedge a rollout.
            froze = true;
            done = true;
            break;
        }
        // eng=00 (GR_ENTRANCE_GAMETIMERSETUP) is a 1-3 frame handoff at
        // every entrance/area reload. Persisting this long means the
        // entrance state machine is wedged (observed after transition
        // deaths: eng=00, x=0, stale warp pointers). End the episode for
        // ~30 frame-equivalents of cost instead of stalling the worker.
        if(ma && ma[0x000E] == 0x00){
            if(++eng0_run > 120){
                froze = true;
                done = true;
                break;
            }
        } else {
            eng0_run = 0;
        }
        // Fast-forward the death animation by forcing the dead state, but
        // ONLY during normal gameplay (engine 0x08). Forcing it mid-transition
        // (pipe/vine/flag/area-load) strands area-load state machines with
        // zeroed RAM and wedges area reload -- observed as tick-budget
        // preempts with eng=00/x=0. Transition deaths play out naturally.
        if(smb_is_dying(env->emu) && smb_player_state(env->emu)==0x08){
            uint8_t* m = env->emu->low_mem();
            if(m) m[0x000E]=0x06;
            env->emu->emulate_skip_frame_fast(0,0);
            retro_sync_from_emu(env);
        }
        if(env->tick>4000){ done=true; break; }
        if(smb_is_dead(env->emu) || smb_is_game_over(env->emu)){ done=true; break; }
        if(smb_flag_get(env->emu)){ done=true; break; }
    }
    retro_sync_from_emu(env);
    RetroScalars cur;
    cur.x_pos=env->x_pos; cur.x_pos_max=env->x_pos_max; cur.coins=env->coins;
    cur.score=env->score; cur.tick=env->tick; cur.world=env->world;
    cur.stage=env->stage; cur.area=env->area; cur.time=env->time;
    cur.has_flag=env->has_flag; cur.is_dead=env->is_dead;
    { // enemy-kill scan + anti-sit-still idle flag for this step
        uint8_t* mk = env->emu->low_mem();
        if(mk) env->kills_total += retro_kill_scan(mk, env->prev_eid, env->prev_ex, env->x_pos);
        cur.kills = env->kills_total;
        cur.idle = (env->x_pos == prev.x_pos && mk && mk[0x000E]==0x08
            && !smb_is_dying(env->emu) && !smb_is_dead(env->emu)
            && !smb_flag_get(env->emu)) ? 1 : 0;
    }
    reward = retro_reward(&prev, &cur, &env->x_pos_max,
        smb_is_dying(env->emu), smb_is_dead(env->emu),
        smb_flag_get(env->emu) && !env->has_flag, env->potential_gamma, &env->rw);
    if (froze) reward -= 1.0f;
    done = smb_is_dead(env->emu) || smb_is_game_over(env->emu) || smb_flag_get(env->emu) || env->tick>4000 || froze;
    if(done){
        env->log.n+=1;
        env->log.episode_length+=env->tick;
        env->log.episode_return+=reward;
        env->log.score+=env->score;
        float prog = env->x_pos_max/3200.0f; if(prog>1) prog=1; if(env->has_flag) prog=1;
        env->log.perf+=prog;
        env->log.distance+=env->x_pos_max;
        env->log.flag+= smb_flag_get(env->emu)?1:0;
        env->log.deaths+= (smb_is_dead(env->emu) || froze)?1:0;
        env->log.coins+= env->coins;
        env->agents[0].terminals[0]=1.0f;
    }
    env->agents[0].rewards[0]=reward;
    if(done){
        Log saved=env->log;
        retro_pick_spawn(env);
        retro_quicknes_load_spawn(env);
        retro_sync_from_emu(env);
        env->x_pos_max=env->x_pos;
        env->tick=0; env->has_flag=0; env->is_dead=0;
        retro_kill_reset(env);
        env->log=saved;
        if(env->agents[0].observations) retro_compute_obs_real(env,(obs_t*)env->agents[0].observations);
    } else {
        if(env->agents[0].observations) retro_compute_obs_real(env,(obs_t*)env->agents[0].observations);
    }
}

void puf_render(Env* env){
    if(!IsWindowReady()){
        const char* _d=getenv("DISPLAY"); const char* _w=getenv("WAYLAND_DISPLAY");
        if((!_d || !*_d) && (!_w || !*_w)) return;
        InitWindow(960,600,"PufferLib Retro // Super Mario Bros.");
        if(!IsWindowReady()) return;
        SetTargetFPS(60);
    }
    if(IsKeyDown(KEY_ESCAPE)) exit(0);
    if(!IsWindowReady()) return;
    const int view_x=24, view_y=72, scale=2;
    const int view_w=Nes_Emu::image_width*scale;
    const int view_h=Nes_Emu::image_height*scale;
    BeginDrawing();
    ClearBackground((Color){8,13,27,255});
    DrawRectangle(0,0,960,52,(Color){15,23,42,255});
    DrawRectangle(0,51,960,1,(Color){47,72,108,255});
    DrawText("PUFFERLIB  /  SMB1",24,14,22,(Color){236,244,255,255});
    DrawText(env->use_fast ? "NATIVE-C CORE" : "REAL NES ROM",774,18,14,(Color){91,221,190,255});
    DrawRectangle(view_x-4,view_y-4,view_w+8,view_h+8,(Color){42,61,88,255});
    DrawRectangle(view_x,view_y,view_w,view_h,(Color){0,0,0,255});
    if(env->emu && env->emu->frame().pixels){
        const auto& fr = env->emu->frame();
        for(int y=0;y<240;y++) for(int x=0;x<256;x++){
            uint8_t pix = fr.pixels[y*256 + x];
            // Frame pixels are palette slots. Resolve them through QuickNES'
            // actual NES palette instead of treating the slot as grayscale.
            int color_index = fr.palette[pix] & (Nes_Emu::color_table_size-1);
            const Nes_Emu::rgb_t& rgb = Nes_Emu::nes_colors[color_index];
            Color color=(Color){rgb.red,rgb.green,rgb.blue,255};
            DrawRectangle(view_x+x*scale,view_y+y*scale,scale,scale,color);
        }
    }
    DrawText("ARROWS / WASD move    X / SPACE jump    Z / C run    R reset",view_x,view_y+view_h+14,14,(Color){164,181,207,255});

    const int panel_x=576, panel_y=84, panel_w=344;
    DrawRectangle(panel_x,panel_y,panel_w,view_h-24,(Color){15,23,42,255});
    DrawRectangle(panel_x,panel_y,4,view_h-24,(Color){91,221,190,255});
    DrawText("RUN STATUS",panel_x+24,panel_y+22,16,(Color){91,221,190,255});
    DrawText(TextFormat("WORLD  %d-%d",env->world,env->stage),panel_x+24,panel_y+68,24,(Color){236,244,255,255});
    DrawText(TextFormat("X POSITION  %d",env->x_pos),panel_x+24,panel_y+116,17,(Color){184,201,224,255});
    DrawText(TextFormat("SCORE      %06d",env->score),panel_x+24,panel_y+148,17,(Color){184,201,224,255});
    DrawText(TextFormat("COINS      %02d",env->coins),panel_x+24,panel_y+180,17,(Color){255,211,91,255});
    DrawText(TextFormat("TIME       %03d",env->time),panel_x+24,panel_y+212,17,(Color){184,201,224,255});
    DrawText(TextFormat("FRAME      %d",env->tick),panel_x+24,panel_y+244,17,(Color){184,201,224,255});
    DrawLine(panel_x+24,panel_y+274,panel_x+panel_w-24,panel_y+274,(Color){47,72,108,255});
    DrawText("12-action PPO interface",panel_x+24,panel_y+302,15,(Color){137,158,188,255});
    DrawText("indexed pixels -> color preview",panel_x+24,panel_y+328,15,(Color){137,158,188,255});
    EndDrawing();
}

void puf_close(Env* env){
    if(env->emu){
        if(env->emu_owned) delete env->emu;
        env->emu=nullptr;
    }
    if(env->fast){
        if(env->fast_owned && env->fast_arena){
            fast_arena_free(env->fast_arena);
            free(env->fast_arena);
        }
        env->fast=nullptr;
        env->fast_arena=nullptr;
    }
    if(env->pixels){ free(env->pixels); env->pixels=nullptr; }
    if(env->level_tiles){ free(env->level_tiles); env->level_tiles=nullptr; }
    if(env->entity_tiles){ free(env->entity_tiles); env->entity_tiles=nullptr; }
    if(env->client){ free(env->client); env->client=nullptr; }
    if(IsWindowReady()) CloseWindow();
}

// The emulator objects are large and are touched in env order on every step.
// Keep them in one contiguous arena instead of scattering 4096 allocations.
// The public Env/Agent ABI remains unchanged, so PPO still sees the same buffers.
#ifdef PUFFERLIB_BUILD_MAIN
static Env* my_vec_init(int* num_envs_out, int* buffer_env_starts,
        int* buffer_env_counts, Dict* vec_kwargs, Dict* env_kwargs){
    int total_agents = (int)dict_get(vec_kwargs,"total_agents");
    int num_buffers = (int)dict_get(vec_kwargs,"num_buffers");
    int agents_per_buffer = total_agents / num_buffers;
    Env* envs = (Env*)calloc((size_t)total_agents,sizeof(Env));
    DictItem* be_it0=dict_find(env_kwargs,"backend");
    int use_fast0 = (be_it0 && be_it0->str && strcmp(be_it0->str,"fast")==0) ? 1 : 0;
    RetroVecArena* arena = NULL;
    RetroFastArena* farena = NULL;
    if(use_fast0){
        farena = (RetroFastArena*)calloc(1,sizeof(RetroFastArena));
        if(!fast_arena_alloc(farena, total_agents)){
            fprintf(stderr,"[retro] fast arena alloc failed\n");
            return envs;
        }
        fast_hb_init(total_agents);
    } else {
        arena = (RetroVecArena*)calloc(1,sizeof(RetroVecArena));
        arena->emus = new Nes_Emu[total_agents];
        arena->count = total_agents;
    }

    int buf=0, buf_agents=0;
    buffer_env_starts[0]=0;
    buffer_env_counts[0]=0;
    for(int i=0;i<total_agents;i++){
        Env* env=&envs[i];
        env->rng=(unsigned int)i;
        if(use_fast0){
            env->fast=&farena->st[i];
            env->fast_owned=false;
            env->fast_arena=farena;
            env->fast_idx=i;
            env->fast_clone_src=(i>0)?0:-1;
        } else {
            env->emu=&arena->emus[i];
            env->emu_owned=false;
            env->arena=arena;
        }
        puf_init(env,env_kwargs);
        buf_agents += env->num_agents;
        buffer_env_counts[buf]++;
        if(buf_agents>=agents_per_buffer && buf<num_buffers-1){
            buf++;
            buffer_env_starts[buf]=i+1;
            buffer_env_counts[buf]=0;
            buf_agents=0;
        }
    }
    *num_envs_out=total_agents;
    return envs;
}

static void my_vec_close(Env* envs){
    if(!envs) return;
    RetroVecArena* arena=envs[0].arena;
    if(arena){
        delete[] arena->emus;
        free(arena);
    }
    RetroFastArena* farena=envs[0].fast_arena;
    if(farena && !envs[0].fast_owned){
        // shared arena (owned by my_vec_init, not by envs[0])
        fast_arena_free(farena);
        free(farena);
    }
    free(envs);
}

#define MY_VEC_INIT
#define MY_VEC_CLOSE
#endif

#else
// C fallback for standalone compiled as C (should not happen via pufferl)
struct Env { Log log; Agent agents[1]; int num_agents; int tag; int boundary_reached; unsigned int rng; int tick; int world,stage,area; int x_pos,x_pos_max; int score,coins,time,life; int has_flag,is_dead; int frameskip; int window_w,window_h; float gravity,max_vx,jump_v,run_accel,friction; unsigned char* level_tiles; unsigned char* entity_tiles; char rom_path[512]; char core_path[512]; RetroClient* client; };
void puf_init(Env* e, Dict* k){ memset(e,0,sizeof(*e)); e->num_agents=1; fprintf(stderr,"[retro] C fallback: rebuild with C++\n"); }
void puf_log(Log* l, Dict* o){}
void puf_reset(Env* e){}
void puf_step(Env* e){ e->agents[0].terminals[0]=1; }
void puf_render(Env* e){}
void puf_close(Env* e){}
#endif

#endif
