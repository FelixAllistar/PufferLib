#pragma once
// Retro / SMB1 -- 8 worlds * 4 stages = 32 levels. No procgen.
// Each Env runs its own QuickNES state, while the immutable cartridge is
// shared. CPU vector workers remain independent and need no core mutex.
// OBS 160 = 16 ego + 12*12 window sampled from framebuffer around Mario.
// ACT 12 discrete (RETRO_ACTION_MASKS). C++ (pufferl) path uses Nes_Emu directly;
// C fallback (if __cplusplus not defined) is stub.

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
#define RETRO_EGO_SIZE 16
#define RETRO_WINDOW_W 12
#define RETRO_WINDOW_H 12
#define RETRO_TILES (RETRO_WINDOW_W * RETRO_WINDOW_H)
#define OBS_SIZE (RETRO_EGO_SIZE + RETRO_TILES)
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

static uint8_t* g_rom_data = NULL;
static size_t g_rom_size = 0;
static Nes_Cart g_rom_cart;
static Nes_State g_initial_state;
static bool g_initial_state_valid = false;
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
static inline int smb_time(Nes_Emu* emu){ uint8_t* m=emu->low_mem(); if(!m) return 0; return (m[0x07F8] % 10)*100 + (m[0x07F9] % 10)*10 + (m[0x07FA] % 10); }
static inline int smb_world(Nes_Emu* emu){ return smb_ram_read(emu,0x075F)+1; }
static inline int smb_stage(Nes_Emu* emu){ return smb_ram_read(emu,0x075C)+1; }
static inline int smb_area(Nes_Emu* emu){ return smb_ram_read(emu,0x0760)+1; }
static inline int smb_score(Nes_Emu* emu){ uint8_t* m=emu->low_mem(); if(!m) return 0; int v=0; for(int i=0;i<6;i++) v=v*10 + (m[0x07DE + i] % 10); return v; }
static inline int smb_coins(Nes_Emu* emu){ uint8_t* m=emu->low_mem(); if(!m) return 0; return (m[0x07ED] % 10)*10 + (m[0x07EE] % 10); }
static inline int smb_life(Nes_Emu* emu){ return smb_ram_read(emu,0x075A); }
static inline int smb_x(Nes_Emu* emu){ return smb_ram_read(emu,0x6D)*256 + smb_ram_read(emu,0x86); }
static inline int smb_left_x(Nes_Emu* emu){ return (smb_ram_read(emu,0x86) - smb_ram_read(emu,0x071C)) & 0xFF; }
static inline int smb_y_pixel(Nes_Emu* emu){ return smb_ram_read(emu,0x03B8); }
static inline int smb_player_state(Nes_Emu* emu){ return smb_ram_read(emu,0x000E); }
static inline bool smb_is_dying(Nes_Emu* emu){ int s=smb_player_state(emu); int vp=smb_ram_read(emu,0x00B5); return s==0x0B || vp>1; }
static inline bool smb_is_dead(Nes_Emu* emu){ return smb_player_state(emu)==0x06; }
static inline bool smb_is_game_over(Nes_Emu* emu){ return smb_life(emu)==0xFF; }
static inline bool smb_is_world_over(Nes_Emu* emu){ return smb_ram_read(emu,0x0770)==2; }
static inline bool smb_flag_get(Nes_Emu* emu){
    if(smb_is_world_over(emu)) return true;
    for(int a=0x16;a<=0x1A;a++){ int t=smb_ram_read(emu,a); if(t==0x2D || t==0x31) return smb_ram_read(emu,0x001D)==3; }
    return false;
}

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

static void retro_compute_obs_real(const Env* env, obs_t* obs){
    const Nes_Emu* e = env->emu;
    float o[RETRO_EGO_SIZE];
    o[0]= env->x_pos/3200.0f; if(o[0]>1) o[0]=1; if(o[0]<0) o[0]=0;
    o[1]= e? smb_y_pixel((Nes_Emu*)e)/255.0f : 0;
    o[2]=0; o[3]=0;
    o[4]= e && smb_player_state((Nes_Emu*)e)==0x08 ? 1.0f:0.0f;
    o[5]=1.0f;
    o[6]= e? (smb_ram_read((Nes_Emu*)e,0x0756)&0x03)/2.0f : 0;
    o[7]= env->coins/99.0f;
    o[8]= env->score/100000.0f;
    o[9]= env->tick/5000.0f;
    o[10]= env->world/8.0f;
    o[11]= env->stage/4.0f;
    o[12]= (env->x_pos%256)/256.0f;
    o[13]= e? smb_left_x((Nes_Emu*)e)/256.0f : 0;
    o[14]= env->has_flag?1.0f:0.0f;
    o[15]= env->is_dead?1.0f:0.0f;
    for(int i=0;i<RETRO_EGO_SIZE;i++){
#if defined(from_float) && !defined(PRECISION_FLOAT)
        obs[i]=from_float(o[i]);
#else
        obs[i]=o[i];
#endif
    }
    int idx=RETRO_EGO_SIZE;
    if(e && e->frame().pixels){
        const Nes_Emu::frame_t& fr = e->frame();
        // QuickNES palette to RGB: use emu->nes_colors? For now just use low 8 bits as luma
        int mx = smb_left_x((Nes_Emu*)e);
        int my = smb_y_pixel((Nes_Emu*)e);
        if(my<0) my=0; if(my>=240) my=120;
        // frame.pixels is 8-bit indexed, row_bytes = buffer_width
        // Use Nes_Emu::image_width=256, buffer_width maybe 272
        for(int dy=-RETRO_WINDOW_RADIUS_H; dy<RETRO_WINDOW_H-RETRO_WINDOW_RADIUS_H; dy++){
            for(int dx=-RETRO_WINDOW_RADIUS_W; dx<RETRO_WINDOW_W-RETRO_WINDOW_RADIUS_W; dx++){
                int sx = mx + dx*8;
                int sy = my + dy*8;
                if(sx<0) sx=0; if(sx>=256) sx=255;
                if(sy<0) sy=0; if(sy>=240) sy=239;
                // pixel index from frame
                uint8_t pix = 0;
                if(fr.pixels) {
                    // frame.pixels points to top-left of image with buffer_width stride
                    // Need to account for buffer offset: emu was set with pixels + 8*width
                    // So frame.pixels already adjusted. Use simple 256 stride approx.
                    // QuickNES buffer_width is 272, but image is 256 contiguous.
                    pix = fr.pixels[sy*256 + sx];
                }
                float luma = pix / 255.0f;
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
    memset(env,0,sizeof(*env));
    env->num_agents=1; env->agents[0].policy=0;
    env->emu = supplied_emu;
    env->arena = supplied_arena;
    env->emu_owned = supplied_emu_owned;
    DictItem* it;
    it=dict_find(kwargs,"frameskip"); env->frameskip = it? (int)it->value : 4;
    it=dict_find(kwargs,"gravity"); env->gravity = it? (float)it->value : 0.52f;
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

void puf_reset(Env* env){
    if(!env->emu_ok || !env->emu) return;
    env->emu->load_state(g_initial_state);
    retro_sync_from_emu(env);
    env->x_pos_max = env->x_pos;
    env->tick=0; env->has_flag=0; env->is_dead=0;
    if(env->agents[0].observations) retro_compute_obs_real(env, (obs_t*)env->agents[0].observations);
}

void puf_step(Env* env){
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
    int prev_score=env->score;
    int prev_coins=env->coins;
    for(int f=0; f<env->frameskip; f++){
        env->tick++;
        // PPO only observes after the action's final frame. QuickNES still
        // advances the complete CPU/PPU/APU state in skip mode, but avoids
        // writing an intermediate 256x240 framebuffer.
        const bool draw = (f + 1 == env->frameskip)
            || g_full_render;
        const char* err = draw
            ? env->emu->emulate_frame(mask,0)
            : env->emu->emulate_skip_frame_fast(mask,0);
        (void)err;
        retro_sync_from_emu(env);
        if(smb_is_dying(env->emu)){
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
    int cur_x = env->x_pos;
    if(cur_x > env->x_pos_max){
        int prog = cur_x - env->x_pos_max;
        if(prog>0 && prog<=5) reward += prog * 0.1f;
        env->x_pos_max = cur_x;
    }
    int dscore = env->score - prev_score;
    if(dscore>0) reward += dscore / 100.0f;
    int dcoins = env->coins - prev_coins;
    if(dcoins!=0){ if(dcoins<-50) dcoins+=100; if(dcoins>0) reward+= dcoins*0.5f; }
    if(smb_is_dying(env->emu) || smb_is_dead(env->emu)){ reward -= 2.5f; }
    if(smb_flag_get(env->emu) && !env->has_flag){ reward += 5.0f; }
    done = smb_is_dead(env->emu) || smb_is_game_over(env->emu) || smb_flag_get(env->emu) || env->tick>4000;
    if(done){
        env->log.n+=1;
        env->log.episode_length+=env->tick;
        env->log.episode_return+=reward;
        env->log.score+=env->score;
        float prog = env->x_pos_max/3200.0f; if(prog>1) prog=1; if(env->has_flag) prog=1;
        env->log.perf+=prog;
        env->log.distance+=env->x_pos_max;
        env->log.flag+= smb_flag_get(env->emu)?1:0;
        env->log.deaths+= smb_is_dead(env->emu)?1:0;
        env->log.coins+= env->coins;
        env->agents[0].terminals[0]=1.0f;
    }
    env->agents[0].rewards[0]=reward;
    if(done){
        Log saved=env->log;
        env->emu->load_state(g_initial_state);
        retro_sync_from_emu(env);
        env->x_pos_max=env->x_pos;
        env->tick=0; env->has_flag=0; env->is_dead=0;
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
    DrawText("REAL NES ROM",774,18,14,(Color){91,221,190,255});
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
    RetroVecArena* arena = (RetroVecArena*)calloc(1,sizeof(RetroVecArena));
    arena->emus = new Nes_Emu[total_agents];
    arena->count = total_agents;

    int buf=0, buf_agents=0;
    buffer_env_starts[0]=0;
    buffer_env_counts[0]=0;
    for(int i=0;i<total_agents;i++){
        Env* env=&envs[i];
        env->rng=(unsigned int)i;
        env->emu=&arena->emus[i];
        env->emu_owned=false;
        env->arena=arena;
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
