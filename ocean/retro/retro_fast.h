#pragma once
// Fast native-C SMB1 backend (smb-vanilla-port smbcore, pinned tarball fetched
// by build.sh -- never vendored into git). Same ROM, same RAM map, same
// OBS256/ACT12 ABI as the QuickNES path (see retro_obs.h).
//
// Performance design (the "0 waste" part):
// - No 6502 interpreter: game logic runs as compiled C (~50x/frame vs
//   QuickNES on the same core, measured 806k vs 15k frames/s single-thread).
// - No APU synthesis, no per-env pixel framebuffers, no CHR/PRG re-parse:
//   the 32KB PRG + 8KB CHR are read once globally; per-env state is only the
//   varying part (2KB RAM + 16KB PPU nametables + regs ≈ 18KB/env).
// - Rendering happens only on observed frames (1 of `frameskip`), into one
//   small RGB scratch buffer per CPU worker (not per env), via the headless
//   software rasterizer. Skip frames run pure game logic with NULL draw
//   callbacks (draw_graphics early-outs).
// - Vectorization is at env level (contiguous state array, one state-pointer
//   swap per env per worker). Field-wise SoA of the game state itself is not
//   possible without rewriting smbcore (it indexes RAM arrays); env-level
//   batching is where the throughput lives. No CUDA env kernel: at ~800k
//   frames/s/core the rollout is policy-bound, and a branch-heavy logic port
//   would be divergence- + launch-overhead-bound on GPU.
//
// Exactness status (honest): same game code family as the ROM (upstream keeps
// bug-compat incl. minus world / clipping bugs, with movie+RAM regression
// tests), identical RAM map and engine states. Residual vs QuickNES: ~1-frame
// input/physics phase offset from different NMI/joypad-latch ordering, so
// full-RAM hashes never match bit-exact (stack + joypad shift regs also
// differ). Train here, evaluate the policy under the ROM path -- closed-loop
// PPO acting every 4 frames absorbs the phase gap; frame-perfect input tapes
// do NOT transfer 1:1.

#ifdef __cplusplus
// smbcore headers (mario.h/base.h/interface.h) and render_raster.h are
// provided by retro.h, which includes this file after struct Env. They are
// intentionally not re-included here: interface.h has no include guard.
#include <stddef.h> // ptrdiff_t for pointer rebase in fast_clone_env
#include <pthread.h> // stall-heartbeat monitor + tick-budget watchdog (diagnostic only)
#include <unistd.h> // sleep()/usleep() for monitor intervals
#include <time.h> // time() for heartbeat timestamps
#include <setjmp.h> // sigsetjmp/siglongjmp for tick-budget preemption
#include <execinfo.h> // backtrace: name the spinning loop at preempt time
#include <signal.h> // SIGURG preemption of over-budget ticks

static uint8_t* g_fast_rom = NULL;
static size_t g_fast_rom_size = 0;
static char g_fast_rom_error[512] = {0};

// Mirrors retro_load_rom_global's search list (same file, both backends).
static bool fast_load_rom_global(const char* hint) {
    if (g_fast_rom) return true;
    const char* candidates[] = {
        hint,
        "ocean/retro/roms/smb1.nes",
        "../ocean/retro/roms/smb1.nes",
        "pufferlib/ocean/retro/roms/smb1.nes",
        NULL
    };
    const char* chosen = NULL;
    for (int i = 0; candidates[i]; i++) {
        if (!candidates[i] || !candidates[i][0]) continue;
        FILE* f = fopen(candidates[i], "rb");
        if (f) {
            fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
            uint8_t hdr[16];
            if (sz > 16 && sz < 1024*1024 && fread(hdr, 1, 16, f) == 16 &&
               hdr[0] == 'N' && hdr[1] == 'E' && hdr[2] == 'S') {
                fclose(f); chosen = candidates[i]; break;
            }
            fclose(f);
        }
    }
    if (!chosen) {
        snprintf(g_fast_rom_error, sizeof(g_fast_rom_error), "ROM not found for fast backend");
        return false;
    }
    FILE* f = fopen(chosen, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    g_fast_rom = (uint8_t*)malloc(sz);
    if (fread(g_fast_rom, 1, sz, f) != (size_t)sz) { free(g_fast_rom); g_fast_rom = NULL; fclose(f); return false; }
    fclose(f);
    g_fast_rom_size = sz;
    return true;
}

// ---- per-init ROM reader (init runs single-threaded; tiny throwaway) ----
struct FastRomReader { const uint8_t* base; size_t size; size_t pos; };
static bool fast_rr_read(void* u, uint8_t* buf, size_t n) {
    FastRomReader* r = (FastRomReader*)u;
    if (r->pos + n > r->size) return false;
    memcpy(buf, r->base + r->pos, n);
    r->pos += n;
    return true;
}
static bool fast_rr_seek(void* u, size_t off) {
    FastRomReader* r = (FastRomReader*)u;
    if (off > r->size) return false;
    r->pos = off;
    return true;
}

// ---- input: current frame's pad lives thread-locally (OMP workers) ----
static __thread struct SMB_buttons t_fast_pad;

// Per-tick tile-draw cap. Legit ticks rasterize ~1100 tiles (960 BG nametable
// + sprites + status line). Wedged states can drive the NMI's VRAM-buffer
// flush into an unterminated walk that issues MILLIONS of draw calls (the
// tick-budget preempt's spin stacks land inside SMBraster_draw_tile, called
// via fast_tile_cb). Once the cap trips we skip drawing (visual-only, game
// RAM untouched) and the wrapper ends the episode: a tick whose render spun
// is a corrupted state, and the obs window would be garbage anyway.
#define FAST_TILE_CAP 4000
static __thread int t_tilecap = 0;
static __thread bool t_tilecap_hit = false;

// Render clip box (pixels, expanded): only tiles that can touch the 12x12
// obs patch are rasterized. Full-screen raster is ~95% of step cost;
// the patch covers ~1/7 of the screen.
static __thread int t_clip[4];
static void fast_joy1(void* u, struct SMB_buttons* b) { (void)u; *b = t_fast_pad; }
static inline void fast_mask_to_pad(unsigned char mask, struct SMB_buttons* p) {
    memset(p, 0, sizeof(*p));
    p->r = (mask & RETRO_BTN_RIGHT) ? 1 : 0;
    p->l = (mask & RETRO_BTN_LEFT) ? 1 : 0;
    p->u = (mask & RETRO_BTN_UP) ? 1 : 0;
    p->d = (mask & RETRO_BTN_DOWN) ? 1 : 0;
    p->a = (mask & RETRO_BTN_A) ? 1 : 0;
    p->b = (mask & RETRO_BTN_B) ? 1 : 0;
}

// ---- per-worker raster (4 workers, NOT 4096 envs) ----
struct FastWorker {
    bool init, patterns;
    struct SMBraster* raster;
    uint8_t* rgb; // 256*240*3
};
static __thread struct FastWorker* t_fworker = NULL;
static void fast_fworker_ensure(const uint8_t* chrrom) {
    FastWorker* w = t_fworker;
    if (!w) {
        w = (FastWorker*)calloc(1, sizeof(FastWorker));
        w->raster = (struct SMBraster*)malloc(SMBraster_size());
        SMBraster_init(w->raster);
        w->rgb = (uint8_t*)malloc(256*240*3);
        // Same 64-entry base palette the QuickNES window path uses, so the
        // luma patch matches across renderers.
        unsigned char lut[64*3];
        for (int i = 0; i < 64; i++) {
            lut[3*i+0] = Nes_Emu::nes_colors[i].red;
            lut[3*i+1] = Nes_Emu::nes_colors[i].green;
            lut[3*i+2] = Nes_Emu::nes_colors[i].blue;
        }
        SMBraster_provide_palette_lookup(w->raster, lut);
        w->init = true;
        t_fworker = w;
    }
    if (!w->patterns && chrrom) {
        SMBraster_update_pattern_tables(w->raster, chrrom);
        w->patterns = true;
    }
}
static void fast_pal_cb(void* u, const uint8_t* idx) {
    FastWorker* w = (FastWorker*)u;
    SMBraster_update_palette(w->raster, idx);
}
static void fast_tile_cb(void* u, const struct SMB_tile tile) {
    if (++t_tilecap > FAST_TILE_CAP) { t_tilecap_hit = true; return; }
    FastWorker* w = (FastWorker*)u;
    if (tile.x + 16 < t_clip[0] || tile.x > t_clip[2] + 16 ||
        tile.y + 16 < t_clip[1] || tile.y > t_clip[3] + 16) return;
    SMBraster_draw_tile(w->raster, tile);
}

// ---- stall heartbeat (diagnostic): per-env step-entry stamps + game state.
// A monitor thread snapshots the most-stale envs to /tmp/puf_envhb.log every
// 5s. If a rollout wedges inside one env tick, the lagging slot names the env
// plus its exact parser-relevant state (area/offsets/action) for standalone
// reproduction via fuzz_parse. Cost: ~10 loads + 1 atomic per env-step.
// Torn reads are acceptable (diagnostic only); x86 aligned int reads don't tear.
struct FastHbSlot {
    long seq;
    int act, x, area, tick, eng, ado, edo, ap;
};
static struct FastHbSlot* g_hb = NULL;
static int g_hb_n = 0;
static long g_hb_seq = 0;
static pthread_t g_hb_thr;
static int g_hb_on = 0;
static void* fast_hb_loop(void* arg) {
    (void)arg;
    FILE* f = fopen("/tmp/puf_envhb.log", "w");
    while (1) {
        sleep(5);
        if (!g_hb || !f) continue;
        long mx = 0;
        for (int i = 0; i < g_hb_n; i++) {
            long s = g_hb[i].seq;
            if (s > mx) mx = s;
        }
        long thresh = -1;
        for (int k = 0; k < 4; k++) {
            long mn = mx + 1;
            int bi = -1;
            for (int i = 0; i < g_hb_n; i++) {
                long s = g_hb[i].seq;
                if (s > thresh && s < mn) { mn = s; bi = i; }
            }
            if (bi < 0) break;
            struct FastHbSlot* s = &g_hb[bi];
            fprintf(f, "t=%ld max=%ld lag=%ld env=%d act=%d x=%d area=%d tick=%d eng=%02x ado=%02x edo=%02x ap=%02x\n",
                (long)time(NULL), mx, mx - mn, bi, s->act, s->x, s->area,
                s->tick, s->eng, s->ado, s->edo, s->ap);
            thresh = mn;
        }
        fflush(f);
    }
    return NULL;
}
static void fast_hb_init(int n) {
    if (g_hb_on || n < 64) return; // vector runs only; skip single-env tools
    g_hb = (struct FastHbSlot*)calloc((size_t)n, sizeof(struct FastHbSlot));
    if (!g_hb) return;
    g_hb_n = n;
    g_hb_on = 1;
    pthread_create(&g_hb_thr, NULL, fast_hb_loop, NULL);
}
static inline void fast_hb_mark(Env* env, int act) {
    if (!g_hb) return;
    int i = env->fast_idx;
    if (i < 0 || i >= g_hb_n || !env->fast) return;
    uint8_t* m = SMB_ram(env->fast);
    struct FastHbSlot* s = &g_hb[i];
    s->act = act;
    s->x = m[0x6D]*256 + m[0x86];
    s->area = m[0x0760];
    s->tick = env->tick;
    s->eng = m[0x000E];
    s->ado = m[0x072C];
    s->edo = m[0x0739];
    s->ap = m[0x0750];
    s->seq = __atomic_add_fetch(&g_hb_seq, 1, __ATOMIC_RELAXED);
}

// ---- tick-budget preemption: hang-proofing for untrusted game code ----
// A stuck env blocks its OMP thread, the rollout barrier never completes,
// and the trainer spins silently (observed 4x). Static analysis exhausted
// the in-tick loops (capped/bounded/proven), so this is defense in depth:
// each worker thread arms a recovery point per env-step; a watchdog thread
// preempts (SIGURG + siglongjmp) any step running >500ms (normal steps take
// ~25us: 20000x headroom). Budget is in THREAD-CPU milliseconds: the
// watchdog (tick_watchdog_loop) trips when the stuck thread has burned this
// much CPU inside the step — a genuinely spinning tick burns CPU, a thread
// descheduled by OS contention burns none, so OS load cannot false-positive
// (the wall-time rule killed healthy episodes under load ~12). The
// eng=00/render-wedge shapes are also caught cheaply in-loop (see
// puf_step_fast); this budget is the backstop for unseen shapes.
// Signal safety: game ticks never malloc, never take locks, never touch
// CUDA; the only tick-region syscall is a rare tripwire print, and a
// preemption landing inside one merely garbles one line. The longjmp stays
// within a single OMP iteration, invisible to the OpenMP runtime. On catch,
// the env resets from its pristine snap and the culprit state is logged.
#define FAST_TICK_BUDGET_MS 500
#define FAST_WD_INTERVAL_MS 25
struct TickGuard {
    int live;
    pthread_t self;
    long ticks;
    int in_tick;
    sigjmp_buf jb;
    // Stack captured by the SIGURG handler (runs on the stuck thread's
    // stack, inside the spinning loop); symbolized post-longjmp in the
    // safe preempt-logging path. Not async-safe to symbolize in-handler.
    void* bt[24];
    int bt_n;
};
static struct TickGuard g_guards[64];
static pthread_mutex_t g_guard_mtx = PTHREAD_MUTEX_INITIALIZER;
static __thread struct TickGuard* t_guard = NULL;
static pthread_t g_wd_thr;
static int g_wd_on = 0;
static void tickurg_handler(int sig) {
    (void)sig;
    if (t_guard) {
        // Snapshot the stack while we're still inside the spin. backtrace()
        // only reads memory / the loader lock; game ticks never malloc and
        // never touch the loader, so both are free. Symbolization happens
        // after the longjmp lands, in the normal logging path.
        t_guard->bt_n = backtrace(t_guard->bt, 24);
        siglongjmp(t_guard->jb, 1);
    }
}
static void tick_atfork_child(void) {
    // Fork (multi-GPU launcher) does not inherit threads: drop all guard
    // state so the child re-registers cleanly instead of preempting stale
    // thread handles.
    t_guard = NULL;
    g_wd_on = 0;
    memset(g_guards, 0, sizeof(g_guards));
}
static void* tick_watchdog_loop(void* arg) {
    (void)arg;
    long last[64] = {0};
    // Thread-CPU-time (ms) snapshot taken when a stall window opens. The
    // trip condition is CPU consumed, not wall time: a genuinely spinning
    // tick burns CPU, while a thread descheduled by OS contention (League
    // open, 3x-oversubscribed OMP pools) burns none. The wall-time rule
    // false-positived under load ~12 and killed healthy episodes en masse
    // (observed: return -1.0, length ~219-288, deaths 1.0).
    double cpu0[64] = {0};
    const double budget = (double)FAST_TICK_BUDGET_MS; // CPU ms
    while (1) {
        usleep(FAST_WD_INTERVAL_MS * 1000);
        pthread_mutex_lock(&g_guard_mtx);
        pthread_t tgt[64];
        int fire[64] = {0};
        for (int i = 0; i < 64; i++) {
            tgt[i] = 0;
            if (!g_guards[i].live) { cpu0[i] = -1; continue; }
            long t = __atomic_load_n(&g_guards[i].ticks, __ATOMIC_RELAXED);
            int in = __atomic_load_n(&g_guards[i].in_tick, __ATOMIC_ACQUIRE);
            if (!(in && t == last[i])) {
                last[i] = t;
                cpu0[i] = -1;
                continue;
            }
            clockid_t cid;
            double cpu = -1;
            if (pthread_getcpuclockid(g_guards[i].self, &cid) == 0) {
                struct timespec ts;
                if (clock_gettime(cid, &ts) == 0)
                    cpu = (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
            }
            if (cpu < 0) continue; // unmeasurable: never fire on a guess
            if (cpu0[i] < 0) { cpu0[i] = cpu; continue; }
            if (cpu - cpu0[i] >= budget) {
                cpu0[i] = -1;
                last[i] = t;
                memcpy(&tgt[i], &g_guards[i].self, sizeof(pthread_t));
                fire[i] = 1;
            }
        }
        pthread_mutex_unlock(&g_guard_mtx);
        for (int i = 0; i < 64; i++)
            if (fire[i]) pthread_kill(tgt[i], SIGURG);
    }
    return NULL;
}
static void tick_watchdog_ensure(void) {
    if (g_wd_on) return;
    g_wd_on = 1;
    pthread_create(&g_wd_thr, NULL, tick_watchdog_loop, NULL);
}
static struct TickGuard* tick_guard_ensure(void) {
    if (t_guard) return t_guard;
    static int handler_on = 0;
    if (!handler_on) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = tickurg_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGURG, &sa, NULL);
        pthread_atfork(NULL, NULL, tick_atfork_child);
        handler_on = 1;
    }
    pthread_t me = pthread_self();
    pthread_mutex_lock(&g_guard_mtx);
    for (int i = 0; i < 64; i++) {
        if (g_guards[i].live && pthread_equal(g_guards[i].self, me)) {
            t_guard = &g_guards[i];
            pthread_mutex_unlock(&g_guard_mtx);
            tick_watchdog_ensure();
            return t_guard;
        }
    }
    for (int i = 0; i < 64; i++) {
        if (!g_guards[i].live) {
            g_guards[i].self = me;
            g_guards[i].ticks = 0;
            g_guards[i].in_tick = 0;
            __atomic_store_n(&g_guards[i].live, 1, __ATOMIC_RELEASE);
            t_guard = &g_guards[i];
            pthread_mutex_unlock(&g_guard_mtx);
            tick_watchdog_ensure();
            return t_guard;
        }
    }
    pthread_mutex_unlock(&g_guard_mtx);
    return NULL; // >64 threads: run unguarded
}

// ---- compact per-env snapshot (defined in retro.h; PRG/CHR stay put) ----
static inline uint8_t* fast_ram(SMB_state* s) { return SMB_ram(s); }

static void fast_snap_save(RetroFastArena* a, int i) {
    SMB_state* s = &a->st[i];
    RetroFastSnap* sn = &a->snap[i];
    memcpy(sn->ram, SMB_ram(s), 0x800);
    memcpy(sn->ppuram, SMB_ppuram(s), 0x4000);
    sn->ppu = s->ppu;
    sn->area_data = s->area_data; sn->enemy_data = s->enemy_data; sn->music_data = s->music_data;
    sn->reset_occurred = s->reset_occurred;
    sn->start_world = s->start_on_world; sn->start_level = s->start_on_level;
}
static void fast_snap_load(RetroFastArena* a, int i) {
    SMB_state* s = &a->st[i];
    RetroFastSnap* sn = &a->snap[i];
    memcpy(SMB_ram(s), sn->ram, 0x800);
    memcpy(SMB_ppuram(s), sn->ppuram, 0x4000);
    s->ppu = sn->ppu;
    s->area_data = sn->area_data; s->enemy_data = sn->enemy_data; s->music_data = sn->music_data;
    s->reset_occurred = sn->reset_occurred;
    s->start_on_world = sn->start_world; s->start_on_level = sn->start_level;
    SMB_ram_finishwrite(s);
}

// One game frame. render=true only on observed frames (uses worker raster).
static void fast_tick(SMB_state* s, bool render) {
    if (render) {
        t_tilecap = 0;
        t_tilecap_hit = false;
        fast_fworker_ensure(s->chrrom);
        FastWorker* w = t_fworker;
        uint8_t* ram0 = SMB_ram(s);
        int mx = (ram0[0x86] - ram0[0x071C]) & 0xFF;
        int my = ram0[0x03B8];
        t_clip[0] = mx - 56 < 0 ? 0 : mx - 56;
        t_clip[1] = my - 56 < 0 ? 0 : my - 56;
        t_clip[2] = mx + 56 > 256 ? 256 : mx + 56;
        t_clip[3] = my + 56 > 240 ? 240 : my + 56;
        SMBraster_set_buffer(w->raster, w->rgb, 256);
        s->callbacks.update_palette = fast_pal_cb;
        s->callbacks.draw_tile = fast_tile_cb;
        s->callbacks.userdata = w;
        SMB_tick(s);
        s->callbacks.update_palette = NULL;
        s->callbacks.draw_tile = NULL;
        s->callbacks.userdata = NULL;
    } else {
        s->callbacks.update_palette = NULL;
        s->callbacks.draw_tile = NULL;
        s->callbacks.userdata = NULL;
        SMB_tick(s);
    }
}

// Clone a booted env (boot env 0 once, memcpy the rest). The boot dance is
// fully deterministic. NOTE: this copies the LIVE post-reset state (not the
// pristine snap), so clones start exactly one neutral frame ahead of a fresh
// boot -- a systematic ~1px skew, constant across envs and runs, harmless
// for RL (verified: bit-identical checksums across repeats/worker counts).
// A snapshot-copy variant was tried and reverted: the snap's music/area
// pointers can be mid-transition values that crash the sound engine on the
// clone's first tick, while live-memcpy carries the consistent live set.
// The full state copy also carries the input callbacks, so clones respond
// to actions exactly like booted envs. Area/enemy/music pointers are rebased
// into the clone's own PRG image (plain memcpy would alias the source env).
static void fast_clone_env(RetroFastArena* a, int dst, int src) {
    SMB_state *S = &a->st[src], *D = &a->st[dst];
    const uint8_t *sbase = SMB_ram(S);
    ptrdiff_t offA = S->area_data ? (const uint8_t*)S->area_data - sbase : 0;
    ptrdiff_t offE = S->enemy_data ? (const uint8_t*)S->enemy_data - sbase : 0;
    ptrdiff_t offM = S->music_data ? (const uint8_t*)S->music_data - sbase : 0;
    memcpy(D, S, SMB_state_size());
    uint8_t *dbase = SMB_ram(D);
    D->area_data = S->area_data ? (const uint8_t*)(dbase + offA) : NULL;
    D->enemy_data = S->enemy_data ? (const uint8_t*)(dbase + offE) : NULL;
    D->music_data = S->music_data ? (const uint8_t*)(dbase + offM) : NULL;
    // Explicit input wiring: clones must respond to actions exactly like
    // booted envs (joy1 reads the thread-local pad). The per-boot ROM reader
    // is dead after init -- never inherit its userdata pointer.
    D->callbacks.joy1 = fast_joy1;
    D->callbacks.userdata = NULL;
    memset(&t_fast_pad, 0, sizeof(t_fast_pad));
    fast_snap_save(a, dst);
}

// Boot one env to gameplay start (START dance mirrors the ROM path), then
// snapshot. Returns false on failure.
static bool fast_boot_env(RetroFastArena* a, int i) {
    SMB_state* s = &a->st[i];
    FastRomReader* rr = (FastRomReader*)malloc(sizeof(FastRomReader));
    rr->base = g_fast_rom; rr->size = g_fast_rom_size; rr->pos = 0;
    struct SMB_callbacks cb;
    memset(&cb, 0, sizeof(cb));
    cb.userdata = rr;
    cb.read_rom_bytes = fast_rr_read;
    cb.seek_rom = fast_rr_seek;
    cb.joy1 = fast_joy1;
    if (!SMB_state_init(s, &cb)) { free(rr); return false; }
    free(rr);
    SMB_start_on_level(s, 1, 1);
    memset(&t_fast_pad, 0, sizeof(t_fast_pad));
    uint8_t* ram = SMB_ram(s);
    bool started = false;
    for (int t = 0; t < 3000; t++) {
        t_fast_pad.start = ((t % 4) < 2) ? 1 : 0;
        fast_tick(s, false);
        ram = SMB_ram(s);
        if (ram[0x0770] == 1 && ram[0x0772] == 3 && (ram[0x07F8] % 10) != 0) { started = true; break; }
    }
    if (!started) return false;
    memset(&t_fast_pad, 0, sizeof(t_fast_pad));
    fast_snap_save(a, i);
    return true;
}

// Sample the 12x12 RGB-luma window from the worker's last rendered frame.
// 12x12-TILE window, one mean-luma value per 8x8 tile (player-anchored
// grid). Previously each cell was a SINGLE corner pixel: coins (sprite in
// the tile center) were invisible, pits were a 1-pixel flicker, and all
// vision aliased as the player moved. Per-tile means make tile content
// (coins, pits, pipes) robustly visible within the same OBS 256 layout.
static void fast_window(float* o, SMB_state* s) {
    FastWorker* w = t_fworker;
    uint8_t* ram = SMB_ram(s);
    int mx = (ram[0x86] - ram[0x071C]) & 0xFF;
    int my = ram[0x03B8];
    if (my < 0) my = 0; if (my >= 240) my = 120;
    for (int dy = -RETRO_WINDOW_RADIUS_H; dy < RETRO_WINDOW_H - RETRO_WINDOW_RADIUS_H; dy++) {
        for (int dx = -RETRO_WINDOW_RADIUS_W; dx < RETRO_WINDOW_W - RETRO_WINDOW_RADIUS_W; dx++) {
            float acc = 0; int n = 0;
            int bx = mx + dx*8, by = my + dy*8;
            if (w) {
                for (int py = 0; py < 8; py++) {
                    int sy = by + py; if (sy < 0) sy = 0; if (sy >= 240) sy = 239;
                    const uint8_t* row = &w->rgb[(size_t)sy*256*3];
                    for (int px = 0; px < 8; px++) {
                        int sx = bx + px; if (sx < 0) sx = 0; if (sx >= 256) sx = 255;
                        const uint8_t* p = &row[(size_t)sx*3];
                        acc += retro_luma(p[0], p[1], p[2]); n++;
                    }
                }
            }
            o[0] = n ? acc / n : 0.0f;
            o++;
        }
    }
}

// ---- Env-level API (struct Env is complete at the include point) ----
static bool fast_arena_alloc(RetroFastArena* a, int n) {
    a->st = (SMB_state*)calloc((size_t)n, SMB_state_size());
    a->snap = (RetroFastSnap*)calloc((size_t)n, sizeof(RetroFastSnap));
    a->count = (a->st && a->snap) ? n : 0;
    return a->count == n;
}
static void fast_arena_free(RetroFastArena* a) {
    if (!a) return;
    free(a->st); free(a->snap);
    a->st = NULL; a->snap = NULL; a->count = 0;
}

static void retro_sync_from_fast(Env* env) {
    if (!env->fast) return;
    uint8_t* m = SMB_ram(env->fast);
    env->world = robs_world(m);
    env->stage = robs_stage(m);
    env->area = robs_area(m);
    env->x_pos = robs_x(m);
    env->score = robs_score(m);
    env->coins = robs_coins(m);
    env->time = robs_time(m);
    env->life = robs_life(m);
    env->has_flag = robs_flagget(m) ? 1 : 0;
    env->is_dead = (robs_dead(m) || robs_dying(m)) ? 1 : 0;
}

static void retro_fast_obs(Env* env, obs_t* obs) {
    float o[OBS_SIZE];
    uint8_t* m = SMB_ram(env->fast);
    RetroScalars sc;
    sc.x_pos = env->x_pos; sc.x_pos_max = env->x_pos_max; sc.coins = env->coins;
    sc.score = env->score; sc.tick = env->tick; sc.world = env->world;
    sc.stage = env->stage; sc.area = env->area; sc.time = env->time;
    sc.has_flag = env->has_flag; sc.is_dead = env->is_dead;
    retro_ego_ent(o, m, &sc);
    fast_window(o + RETRO_EGO_SIZE + RETRO_ENT_SIZE, env->fast);
    if (env->fast_idx == 0 && env->tick == env->frameskip) {
        static int fast_dbg = -1;
        if (fast_dbg < 0) fast_dbg = getenv("FAST_OBS_DEBUG") ? 1 : 0;
        if (fast_dbg) {
            float mn = 1, mx = 0, mean = 0;
            for (int i = 0; i < RETRO_TILES; i++) {
                float v = o[RETRO_EGO_SIZE + RETRO_ENT_SIZE + i];
                if (v < mn) mn = v; if (v > mx) mx = v; mean += v;
            }
            mean /= RETRO_TILES;
            fprintf(stderr, "[retro-fast] obs x=%.3f win mean=%.3f min=%.3f max=%.3f\n",
                o[0], mean, mn, mx);
        }
    }
    for (int i = 0; i < OBS_SIZE; i++) {
#if defined(from_float) && !defined(PRECISION_FLOAT)
        obs[i] = from_float(o[i]);
#else
        obs[i] = o[i];
#endif
    }
}

// Restore gameplay-start snapshot + one deterministic neutral render tick so
// the obs window is populated. Used by both init tail and reset.
static void fast_reset_to_snap(Env* env) {
    fast_snap_load(env->fast_arena, env->fast_idx);
    if (getenv("FAST_SNAP_DEBUG")) {
        fprintf(stderr, "[reset] env=%d joy1=%p upd=%p draw=%p userdata=%p\n",
            env->fast_idx,
            (const void*)env->fast->callbacks.joy1,
            (const void*)env->fast->callbacks.update_palette,
            (const void*)env->fast->callbacks.draw_tile,
            env->fast->callbacks.userdata);
    }
    memset(&t_fast_pad, 0, sizeof(t_fast_pad));
    fast_tick(env->fast, true);
    retro_sync_from_fast(env);
    env->x_pos_max = env->x_pos;
    env->tick = 0; env->has_flag = 0; env->is_dead = 0;
    retro_kill_reset(env);
}

static void puf_step_fast(Env* env) {
    if (!env->fast) { env->agents[0].rewards[0] = 0; env->agents[0].terminals[0] = 1; return; }
    env->agents[0].rewards[0] = 0; env->agents[0].terminals[0] = 0;
    int act = 0; if (env->agents[0].actions) act = (int)env->agents[0].actions[0];
    if (act < 0) act = 0; if (act >= RETRO_NUM_ACTIONS) act = RETRO_NUM_ACTIONS-1;
    unsigned char mask = RETRO_ACTION_MASKS[act];
    float reward = 0;
    bool done = false;
    bool froze = false;
    int eng0_run = 0;
    fast_hb_mark(env, act);
    // Tick-budget preemption: arm a recovery point for this step. If the
    // watchdog fires (step running >10s; normal steps take ~25us), we land
    // in the branch below with possibly-corrupt mid-tick state.
    TickGuard* tguard = tick_guard_ensure();
    if (tguard && sigsetjmp(tguard->jb, 1) != 0) {
        // Preempted over-budget tick -> terminal + pristine snap reset.
        // Logs the culprit state for trigger analysis.
        uint8_t* mc = SMB_ram(env->fast);
        int px = mc[0x6D]*256 + mc[0x86];
        fprintf(stderr, "[retro-fast] tick-budget preempt env=%d act=%d x=%d area=%d tick=%d eng=%02x ado=%02x edo=%02x ap=%02x\n",
            env->fast_idx, act, px, mc[0x0760],
            env->tick, mc[0x000E], mc[0x072C], mc[0x0739], mc[0x0750]);
        // Persist: terminal scrollback eats single lines, and this is the
        // primary trigger diagnostic. Append-only, catches are rare.
        FILE* pf = fopen("/tmp/puf_preempt.log", "a");
        if (pf) {
            fprintf(pf, "t=%ld env=%d act=%d x=%d area=%d tick=%d eng=%02x ado=%02x edo=%02x ap=%02x aec=%02x omt=%02x pec=%02x pst=%02x om=%02x\n",
                (long)time(NULL), env->fast_idx, act, px, mc[0x0760],
                env->tick, mc[0x000E], mc[0x072C], mc[0x0739], mc[0x0750],
                mc[0x0752], mc[0x0772], mc[0x0710], mc[0x001D], mc[0x0770]);
            // Engine state hex (low work RAM + misc/page state) so catches
            // can be replayed/analyzed offline.
            fprintf(pf, "ram00:");
            for (int i = 0; i < 0x100; i++) fprintf(pf, "%02x", mc[i]);
            fprintf(pf, "\nram06:");
            for (int i = 0x6d0; i < 0x780; i++) fprintf(pf, "%02x", mc[i]);
            fprintf(pf, "\n");
            // The prize: where the tick was spinning. Captured in-handler
            // on the stuck stack; symbolized here (safe context, -rdynamic).
            if (tguard->bt_n > 0) {
                char** syms = backtrace_symbols(tguard->bt, tguard->bt_n);
                fprintf(pf, "bt:");
                for (int i = 0; i < tguard->bt_n; i++)
                    fprintf(pf, " | %s", syms ? syms[i] : "?");
                fprintf(pf, "\n");
                free(syms);
            }
            fclose(pf);
        }
        if (tguard->bt_n > 0) {
            char** syms = backtrace_symbols(tguard->bt, tguard->bt_n);
            fprintf(stderr, "[retro-fast] spin stack:");
            for (int i = 0; i < tguard->bt_n && i < 8; i++)
                fprintf(stderr, " %s", syms ? syms[i] : "?");
            fprintf(stderr, "\n");
            free(syms);
        }
        tguard->in_tick = 0;
        env->agents[0].rewards[0] = -1.0f;
        env->agents[0].terminals[0] = 1.0f;
        env->log.n += 1;
        env->log.episode_length += env->tick;
        env->log.episode_return += -1.0f;
        env->log.score += env->score;
        env->log.deaths += 1;
        env->log.coins += env->coins;
        Log saved = env->log;
        fast_reset_to_snap(env);
        env->log = saved;
        if (env->agents[0].observations) retro_fast_obs(env, (obs_t*)env->agents[0].observations);
        return;
    }
    if (tguard) {
        tguard->in_tick = 1;
        __atomic_add_fetch(&tguard->ticks, 1, __ATOMIC_RELAXED);
    }
    RetroScalars prev;
    prev.x_pos = env->x_pos; prev.x_pos_max = env->x_pos_max; prev.coins = env->coins;
    prev.score = env->score; prev.tick = env->tick; prev.world = env->world;
    prev.stage = env->stage; prev.area = env->area; prev.time = env->time;
    prev.has_flag = env->has_flag; prev.is_dead = env->is_dead;
    prev.kills = env->kills_total; prev.idle = 0;
    for (int f = 0; f < env->frameskip; f++) {
        env->tick++;
        uint8_t* mb = SMB_ram(env->fast);
        int fc_before = mb[0x0009];
        fast_mask_to_pad(mask, &t_fast_pad);
        fast_tick(env->fast, (f + 1 == env->frameskip));
        if (t_tilecap_hit) {
            // NMI's VRAM-buffer flush walked into an unterminated buffer and
            // issued millions of draws (spin stacks land inside
            // SMBraster_draw_tile). Game RAM may still be fine (eng=08 seen),
            // but the obs window is garbage -> end the episode. Diagnostic
            // fields name the VRAM buffer state for the smb_patches fix.
            uint8_t* mt = SMB_ram(env->fast);
            fprintf(stderr,
                "[retro-fast] tile-cap hit env=%d tick=%d x=%d eng=%02x vram_ctrl=%02x vram1_off=%02x tiles>=%d\n",
                env->fast_idx, env->tick, env->x_pos, mt[0x000E],
                mt[0x0773], mt[0x0300], t_tilecap);
            froze = true;
            done = true;
            break;
        }
        retro_sync_from_fast(env);
        uint8_t* m = SMB_ram(env->fast);
        if (m[0x0009] == fc_before) {
            // Frame counter stalled: input-dead freeze state (SMB1 has
            // several documented ones; random exploration will find them).
            // End the episode here so one frozen env can never wedge a
            // rollout; the normal reset path below follows.
            if (g_verbose) fprintf(stderr, "[retro-fast] freeze watchdog fired (tick=%d x=%d)\n",
                env->tick, env->x_pos);
            froze = true;
            done = true;
            break;
        }
        // eng=00 (GR_ENTRANCE_GAMETIMERSETUP) is a 1-3 frame handoff at
        // every entrance/area reload. Persisting this long means the
        // entrance state machine is wedged (observed after transition
        // deaths: eng=00, x=0, stale warp pointers) -- previously only
        // caught by the tick-budget preempt, which cost a full 10s of
        // worker time per occurrence and dominated rollout latency.
        if (m[0x000E] == 0x00) {
            if (++eng0_run > 120) {
                if (g_verbose) fprintf(stderr,
                    "[retro-fast] entrance wedge guard fired (tick=%d x=%d)\n",
                    env->tick, env->x_pos);
                froze = true;
                done = true;
                break;
            }
        } else {
            eng0_run = 0;
        }
        // Fast-forward the death animation, but ONLY during normal gameplay
        // (engine 0x08). Forcing 0x06 mid-transition strands area-load state
        // machines with zeroed RAM and wedges area reload (observed as
        // tick-budget preempts with eng=00/x=0). Transition deaths play out.
        if (robs_dying(m) && m[0x000E] == 0x08) {
            m[0x000E] = 0x06;
            memset(&t_fast_pad, 0, sizeof(t_fast_pad));
            fast_tick(env->fast, false);
            retro_sync_from_fast(env);
            m = SMB_ram(env->fast);
        }
        if (env->tick > 4000) { done = true; break; }
        if (robs_dead(m) || robs_gameover(m)) { done = true; break; }
        if (robs_flagget(m)) { done = true; break; }
    }
    retro_sync_from_fast(env);
    uint8_t* m = SMB_ram(env->fast);
    RetroScalars cur;
    cur.x_pos = env->x_pos; cur.x_pos_max = env->x_pos_max; cur.coins = env->coins;
    cur.score = env->score; cur.tick = env->tick; cur.world = env->world;
    cur.stage = env->stage; cur.area = env->area; cur.time = env->time;
    cur.has_flag = env->has_flag; cur.is_dead = env->is_dead;
    // Enemy-kill scan + anti-sit-still idle flag for this step.
    env->kills_total += retro_kill_scan(m, env->prev_eid, env->prev_ex, env->x_pos);
    cur.kills = env->kills_total;
    cur.idle = (env->x_pos == prev.x_pos && m[0x000E] == 0x08
        && !robs_dying(m) && !robs_dead(m) && !robs_flagget(m)) ? 1 : 0;
    reward = retro_reward(&prev, &cur, &env->x_pos_max,
        robs_dying(m), robs_dead(m),
        robs_flagget(m) && !env->has_flag, env->potential_gamma, &env->rw);
    if (froze) reward -= 1.0f;
    done = robs_dead(m) || robs_gameover(m) || robs_flagget(m) || env->tick > 4000 || froze;
    if (done) {
        env->log.n += 1;
        env->log.episode_length += env->tick;
        env->log.episode_return += reward;
        env->log.score += env->score;
        float prog = env->x_pos_max/3200.0f; if (prog > 1) prog = 1; if (env->has_flag) prog = 1;
        env->log.perf += prog;
        env->log.distance += env->x_pos_max;
        env->log.flag += robs_flagget(m) ? 1 : 0;
        env->log.deaths += (robs_dead(m) || froze) ? 1 : 0;
        env->log.coins += env->coins;
        env->agents[0].terminals[0] = 1.0f;
    }
    env->agents[0].rewards[0] = reward;
    if (done) {
        Log saved = env->log;
        fast_reset_to_snap(env);
        env->log = saved;
        if (env->agents[0].observations) retro_fast_obs(env, (obs_t*)env->agents[0].observations);
    } else {
        if (env->agents[0].observations) retro_fast_obs(env, (obs_t*)env->agents[0].observations);
    }
    if (tguard) tguard->in_tick = 0;
}
#endif
