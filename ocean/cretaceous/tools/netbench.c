// netbench: a server and N clients in one process, over real UDP sockets on
// localhost, each client behind its own link conditioner (netsim.c), with
// scripted inputs. Reports per scenario: bandwidth per client, snapshot rate,
// input-to-ack latency, prediction error and corrections, interpolation
// underruns, and how far drawn remote agents are from the server's truth.
// Movable props (v11): every 2 s the server drops a boulder or log near each
// client from a few metres up (--no-props: off); reports prop states and
// bytes per snapshot and how far drawn props near the client are from the
// server's truth at the render time.
// Hit registration (v18 lag compensation): half the clients (even ones) are
// fighters (raptor, tyrannosaur, raptor, pachycephalosaur, ...) that chase
// the nearest animal in their own drawn world, fast ones first (bots and
// other clients, each odd client starting beside a fighter), and attack it;
// every 4 s the server puts two bots (gallimimus, raptors first) 4-8 m in
// front of each fighter, running across at 60-100% of their sprint. For every
// swing a fighter makes, the client judges at its own hit moment whether its
// predicted animal's blow reaches anything in its drawn world (remote agents
// at their render time), and the server's hit log says whether that swing
// landed (--hits FILE: the table). Swings that caught small prey, that the
// server cut short (a stun) or that met a dodge-leap are counted apart.
// Casts: every client presses one of its ready ability slots now and then
// (the odd clients ask for a stegosaur, sauropelta, gallimimus, raptor,
// tyrannosaur and pachycephalosaur in turn, all slot-3 choices are unlocked
// on the server and each client switches its slot-3 choice every 20 s), and
// the clients bite while they move. Per ability and for the bite: the time
// from the press to the first frame whose predicted animal shows it, the
// prediction error at the first snapshot that acknowledges the press, the
// largest one in the 0.5 s after it and the corrections the client smoothed
// in over those 0.5 s (--casts FILE: the table).
//
// Replay (docs/net.md, Replay): --record FILE keeps the run's fights, every
// client's input at every one of its ticks with the seed, the scenario (the
// latency profile) and the run's hashes; --replay FILE plays them back
// instead of the scripted and fighting clients, so two builds or two
// settings (--snapshot-hz, --no-view) meet the same fights. A run on virtual
// time is a function of its inputs and seeds (ENet's clock is virtual too),
// so a replay at the recording's settings reproduces it exactly: --check
// exits 1 unless its hashes are the recording's. --swings FILE writes every
// fighter swing (recording, client, input, seen, landed, why left out), and
// --pair A B compares two such files swing by swing.
//
//   netbench [--agents 64[,128]] [--clients 4] [--seconds 20] [--warmup 3] [--seed 7]
//            [--scenario all|lan,80ms,...] [--port 27600] [--md FILE] [--hits FILE] [--casts FILE]
//   netbench --connect HOST:PORT [--clients 2] [--seconds 10] [--scenario lan]
//            (clients only, against a running `play server`: 2-process smoke test)
//   netbench --record FILE [one scenario, one agent count]   netbench --replay FILE [--check]
//   netbench --pair A.swings B.swings
//   --snapshot-hz 15|20|30   the server's snapshot rate (default 15)
//   --no-view    presses carry no view: lag compensation off (the server judges at the present)
//   --swings FILE   every fighter swing, one line each (for --pair; pair replays: a recording run logs id 0)
//   --window T W    replay the recording's own settings until T s into the statistics, then this
//                   run's (--no-view) for W s, and stop; log the swings in the window (below)
//   --hash       print the run's hashes (server sim, clients)
//   --realtime   wall clock instead of virtual time (see below)
//   --no-props   no boulder drops (v11 props)
//
// Build: make -C ocean/cretaceous netbench CC=clang (links with
// -Wl,--wrap=enet_time_get, below). Run from the repository root.
#include "../net_priv.h"
#include "../abilities.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct { const char* name; const char* label; const char* spec; } Scenario;
static const Scenario SCEN[] = {
    {"lan", "LAN", "lat=0.25 jit=0.1"},
    {"80ms", "80 ms RTT", "lat=40 jit=1"},
    {"150j20", "150 ms RTT, 20 ms jitter", "lat=75 jit=14"},
    {"250l5", "250 ms RTT, 5% loss", "lat=125 jit=3 loss=5"},
    {"burst", "80 ms RTT, 1% loss in bursts", "lat=40 jit=1 loss=1 burst=8"},
    {"mixed", "120 ms RTT, jitter, 2% loss, dup, reorder", "lat=60 jit=10 loss=2 dup=1 reorder=2"},
};
#define NSCEN (int)(sizeof SCEN / sizeof SCEN[0])
#define MAXC 16
#define HIST_TICKS 512

// A fighter's swing: the input that its predicted blow lands on, what the
// client saw then, and the client's view (render tick) at the press.
typedef struct { uint32_t seq; uint8_t seen, act; } Swing;
#define NSWING 64

// A press being measured: an ability slot, or a bite (KIND_BITE); a pounce
// whose raptor the server had caught hold by the end of the window counts
// as KIND_LATCHED (that catch is the server's). state 0: waiting for the
// snapshot that acknowledges it; 1: the 0.5 s after it.
#define KIND_BITE CRET_AB_COUNT
#define KIND_LATCHED (CRET_AB_COUNT + 1)
#define NKIND (CRET_AB_COUNT + 2)
typedef struct { uint32_t seq; double t_press, t_shown, t_ack; uint8_t kind, slot, state, cast, latched; float err_ack, err_max, corr; } Cast;
#define NCAST 16
#define CAST_WINDOW 0.5
// Per ability (and the bite), over all clients: presses the server
// carried out, presses it refused (and of those, the ones the client had
// shown: mispredicted), the press-to-start time (5 ms bins), the error at
// the ack (1 cm bins), the largest error and the corrections in the window.
typedef struct {
    uint64_t n, refused, mispredicted, unshown; double start_sum, err_sum, errmax_sum, corr_sum; float corr_max;
    uint32_t start_hist[200], err_hist[200];
} CastStat;
static CastStat g_cast[NKIND];
typedef struct {
    CretNet* net; CretSim* sim; CretSim* draw; int slot; bool ready;
    // Replay: the recorded ticks (REC_TICK bytes each) being written or played back.
    uint8_t* rec; uint32_t nrec, caprec, ri; bool play;
    double next_tick, next_frame, period;
    uint32_t rng; int seg, ticks; CretInput cur; float turn0, phase;
    // Hit registration (fighters only).
    bool fighter; int target; uint32_t judge;   // judge: the input whose predicted blow lands, to judge at the next frame
    Swing sw[NSWING]; uint32_t sw_head, sw_tail;
    double view[HIST_TICKS];                     // render tick at each input (by seq), for the rewind the server needs
    uint32_t known[HIST_TICKS];                  // the newest snapshot's tick at each input (the server clamps a view to 200 ms behind it)
    uint64_t attacks, seen, srv, agree, false_hit, lost, prey, dodged, out_of_step;
    uint64_t want_n, want_over; double want_sum; uint32_t want_hist[100];   // view age at the server (A + 1 - view) in ticks/4; views past the clamp
    // Remote agents drawn vs the server (cm bins).
    uint64_t rerr_n; double rerr_sum; uint32_t rerr_hist[200];
    uint64_t perr_n; double perr_sum; uint32_t perr_hist[200];   // drawn props vs the server (cm bins)
    Cast cast[NCAST]; int ncast; uint32_t rec_seen; int choice; double next_choice;   // casts being measured; the slot-3 choice
} BClient;

// Virtual time (default): the clock advances in fixed steps and everything
// due runs, however long the machine takes, so the results do not depend on
// load. Packets still cross real localhost sockets and ENet; only ENet's own
// timers (pings, retransmits, its RTT) stay on wall time. --realtime runs on
// the wall clock (needed for --connect).
static double g_vt = 1000.0; static bool g_virtual = true;
static double vclock(void) { return g_vt; }
// ENet's own clock follows the virtual one as well: netbench links with
// -Wl,--wrap=enet_time_get, so ENet's pings, retransmits and round trip run
// on virtual time and the whole run, sockets included, is a function of its
// inputs and seeds (Replay).
enet_uint32 __real_enet_time_get(void);
enet_uint32 __wrap_enet_time_get(void) { return g_virtual ? (enet_uint32)(uint64_t)(g_vt * 1000.0) : __real_enet_time_get(); }
static double now_s(void) { return cret_netsim_now(); }
static double real_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
static float frand(uint32_t* s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; *s = x ? x : 1; return (x >> 8) / 16777216.0f; }

// Scripted player: segments of trotting, sprinting, walking, backing up,
// standing and eating, with a wandering turn, jumps and attacks.
static CretInput script(BClient* c) {
    if (--c->seg <= 0) {
        float r = frand(&c->rng); CretInput in = {0};
        c->seg = 15 + (int)(frand(&c->rng) * 75);
        if (r < 0.35f) in.forward = 1;
        else if (r < 0.55f) { in.forward = 1; in.sprint = 1; }
        else if (r < 0.7f) in.forward = 0.5f;
        else if (r < 0.8f) in.forward = -0.5f;
        else if (r < 0.9f) { in.forward = 0; in.strafe = frand(&c->rng) < 0.5f ? -1 : 1; }
        else in.action = CRET_ACT_EAT;
        c->cur = in; c->turn0 = (frand(&c->rng) * 2 - 1) * 0.6f; c->phase = frand(&c->rng) * 6.28f;
    }
    CretInput in = c->cur;
    if (!in.action) in.turn = fmaxf(-1, fminf(1, c->turn0 + 0.5f * sinf(c->ticks * 0.07f + c->phase)));
    if (frand(&c->rng) < 1.0f / 90) in.jump = 1;
    if (frand(&c->rng) < 1.0f / 120) in.action = CRET_ACT_ATTACK;
    else if (frand(&c->rng) < 1.0f / 200) in.action = CRET_ACT_ATTACK2;
    in.aim = 0.8f * sinf(c->ticks * 0.031f + c->phase);   // v9: a head that looks about
    c->ticks++;
    return in;
}

// Every client casts: about every 1.5 s it presses one of its three slots,
// if its own (predicted) animal has that ability ready.
static void pick_cast(BClient* c, CretInput* in) {
    if (frand(&c->rng) >= 1.0f / 45) return;
    int k = (int)(frand(&c->rng) * CRET_AB_SLOTS) % CRET_AB_SLOTS;
    if (cret_ability_ready(c->sim, c->slot, k)) in->ability = (uint8_t)(k + 1);
}

// A press to measure: an ability slot the client's animal has ready, or a
// fresh bite (a front blow that can start now).
static void cast_press(BClient* c, const CretInput* in, double now) {
    const CretAgent* me = &c->sim->agents[c->slot]; const NetClient* cl = c->net->cl;
    if (!me->alive || c->ncast >= NCAST) return;
    Cast w = {.seq = cl->seq + 1, .t_press = now};
    if (in->ability) { w.slot = (uint8_t)(in->ability - 1); w.kind = (uint8_t)cret_ability_of(c->sim, c->slot, w.slot); if (!w.kind) return; }
    else if (in->action == CRET_ACT_ATTACK && !me->act && me->attack_cd == 0 && !CRET_SPECIES_TABLE[me->species].tail && me->st[CRET_ST_STUN] <= 0 && !me->latch && !cret_down(me)) w.kind = KIND_BITE;
    else return;
    c->cast[c->ncast++] = w;
}

// After a poll and a draw: whether the predicted animal shows each press
// yet, and what the newest reconciliation corrected. At the first snapshot
// that acknowledges a press, its own state says whether the server carried
// the press out (the slot's cooldown, or the bite's, started at that input).
static void cast_frame(BClient* c, double now) {
    const NetClient* cl = c->net->cl; const CretAgent* me = &c->sim->agents[c->slot];
    bool rec = cl->rec_n != c->rec_seen; c->rec_seen = cl->rec_n;
    float err = cl->rec_err_m;   // -1: nothing to compare (a respawn)
    for (int k = 0; k < c->ncast;) {
        Cast* w = &c->cast[k]; bool done = false;
        if (!w->t_shown && (w->kind == KIND_BITE ? me->act == CRET_ACT_ATTACK : me->ab_cd[w->slot] > 0)) w->t_shown = now;
        if (rec && w->state == 0 && cl->rec_ack >= w->seq) {
            const NetSelf* S = &cl->self; float since = (float)(cl->rec_ack - w->seq + 2) * CRET_DT;
            if (w->kind == KIND_BITE) w->cast = S->attack_cd + since * CRET_TICK_HZ >= CRET_SPECIES_TABLE[me->species].cooldown * CRET_TICK_HZ - 0.5f;
            else w->cast = S->ab_cd[w->slot] > 0 && S->ab_cd[w->slot] + since >= CRET_ABILITIES[w->kind].cooldown - 0.05f;
            w->state = 1; w->t_ack = now; w->err_ack = w->err_max = fmaxf(err, 0); w->corr = cl->rec_shift_m;
        } else if (rec && w->state == 1 && now - w->t_ack <= CAST_WINDOW) {
            w->err_max = fmaxf(w->err_max, fmaxf(err, 0)); w->corr += cl->rec_shift_m;
        }
        // Not counted: no prediction to compare at the ack, or an error the
        // client snaps instead of smoothing (over 4 m: the animal was put
        // somewhere, such as a raptor riding a bot that bring_prey moved).
        if (rec && w->state == 1 && now - w->t_ack <= CAST_WINDOW && (err > 4.0f || (err < 0 && w->t_ack == now))) { done = true; w->cast = 2; }
        if (rec && w->state == 1 && cl->self.latch) w->latched = 1;
        if (w->state == 1 && now - w->t_ack > CAST_WINDOW) done = true;
        if (w->state == 0 && now - w->t_press > 3) { done = true; w->cast = 2; }   // never acknowledged (lost to a respawn)
        if (!done) { k++; continue; }
        CastStat* st = &g_cast[w->kind == CRET_AB_POUNCE && w->latched ? KIND_LATCHED : w->kind];
        if (w->cast == 0) { st->refused++; st->mispredicted += w->t_shown > 0; }
        else if (w->cast == 1 && !w->t_shown) st->unshown++;
        else if (w->cast == 1) {
            double ms = (w->t_shown - w->t_press) * 1000; int b = (int)(ms / 5), e = (int)(w->err_ack * 100);
            st->n++; st->start_sum += ms; st->start_hist[b < 0 ? 0 : b > 199 ? 199 : b]++;
            st->err_sum += w->err_ack; st->err_hist[e > 199 ? 199 : e]++; st->errmax_sum += w->err_max;
            st->corr_sum += w->corr; st->corr_max = fmaxf(st->corr_max, w->corr);
        }
        c->cast[k] = c->cast[--c->ncast];
    }
}

// A fighter: chases the nearest animal in its drawn world that is not a
// packmate (one running counts as nearer: 1.5 s of its speed), and attacks
// when it looks in reach (the secondary attack now and then); with nothing
// within 60 m it moves as the other clients do.
static float wrapf(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }
static bool fight(BClient* c, CretInput* out) {
    const CretAgent* me = &c->sim->agents[c->slot]; const CretSim* d = c->draw;
    int best = -1; float bd = 60;
    for (int j = 0; j < d->cfg.num_agents; j++) {
        const CretAgent* b = &d->agents[j];
        if (j == c->slot || !b->alive || (me->group && b->group == me->group)) continue;
        float r = hypotf(b->x - me->x, b->z - me->z) - 1.5f * b->speed - (j == c->target ? 2 : 0);   // the fast ones first; keep after the same one
        if (r < bd) { bd = r; best = j; }
    }
    c->target = best;
    if (best < 0 || !me->alive) return false;
    const CretAgent* b = &d->agents[best]; const CretSpecies* S = &CRET_SPECIES_TABLE[me->species]; float g = cret_growth_scale(me->growth);
    float dx = b->x - me->x, dz = b->z - me->z, gap = hypotf(dx, dz) - b->radius - 0.4f * S->length * g;
    float err = wrapf(atan2f(dz, dx) - me->yaw);
    CretInput in = {0};
    in.turn = fmaxf(-1, fminf(1, err * 3)); in.forward = 1; in.sprint = gap > 4;
    in.aim = cret_aim_at(me, b->x, b->z);
    if (!me->act && fabsf(err) < 0.9f) {
        if (me->attack_cd == 0 && gap < S->reach * g + 1.0f) in.action = CRET_ACT_ATTACK;
        else if (S->alt.kind != CRET_ALT_NONE && me->attack2_cd == 0 && gap < S->alt.reach * g + 0.5f && frand(&c->rng) < 0.1f) in.action = CRET_ACT_ATTACK2;
    }
    c->ticks++;
    *out = in;
    return true;
}

// Settings of a run beyond the scenario: the sim seed, the server's snapshot
// rate, lag compensation off (--no-view), and the swing log (--swings).
static uint32_t g_seed = 7; static int g_snap_hz = CRET_SNAPSHOT_HZ; static bool g_no_view = false;
static FILE* g_swings; static uint32_t g_rec_id;   // g_rec_id: the replayed recording (its FNV-1a), 0 for none
// A replay window (--window T W): a changed setting changes some blow, and
// from there the replayed world drifts from the recording (a few millimetres
// of lunge grow into fights the recorded inputs no longer steer into). So a
// windowed replay plays the recording's own settings, the same run exactly,
// until T seconds after the statistics start, then its own (--no-view) for W
// seconds, and stops. Swings whose blow lands at least WIN_LEAD in are logged;
// each met the same fight as in the recording up to the switch (--pair).
static double g_win_t = -1, g_win_w; static bool g_cond = true;   // g_cond: the run's own settings are on
#define WIN_LEAD (CRET_TICK_HZ / 2)

// A recording: what a run needs to meet the same fights again (the header)
// and every client's input at each of its ticks. File layout, little-endian:
// "CRETREC1"; u32 protocol version, agents, clients, seed, warmup and seconds
// in ms, props on, the recording's snapshot rate; the scenario's name and
// netsim spec (64 bytes each, NUL-padded); per client a u32 tick count and
// REC_TICK bytes a tick (a flags byte: bit 0, the slot-3 switch sent before
// this tick, bit 1 its choice; then the 40-bit packed input the server
// receives, netp_pack_input); last the recording run's two hashes (u64).
#define REC_MAGIC "CRETREC1"
#define REC_TICK 6
typedef struct {
    uint32_t version, agents, clients, seed, warmup_ms, seconds_ms, props, snap_hz;
    char name[64], spec[64]; uint64_t hsrv, hcli;
} RecHead;
static void put_u32(FILE* f, uint32_t v) { for (int i = 0; i < 4; i++) fputc((int)(v >> (8 * i)) & 0xff, f); }
static void put_u64(FILE* f, uint64_t v) { put_u32(f, (uint32_t)v); put_u32(f, (uint32_t)(v >> 32)); }
static uint32_t get_u32(const uint8_t* p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t get_u64(const uint8_t* p) { return get_u32(p) | (uint64_t)get_u32(p + 4) << 32; }
static uint32_t fnv32(const uint8_t* d, size_t n) { uint32_t h = 2166136261u; for (size_t i = 0; i < n; i++) h = (h ^ d[i]) * 16777619u; return h; }
// The run's two hashes (FNV-1a 64): the server sim after every tick, and
// every client's predicted animal after each of its ticks, what it received
// and what it judged of each swing.
static uint64_t g_hsrv, g_hcli;
static void hmix(uint64_t* h, const void* p, size_t n) { const uint8_t* d = p; for (size_t i = 0; i < n; i++) *h = (*h ^ d[i]) * 1099511628211ull; }
static void hash_server(const CretSim* s) {
    for (int i = 0; i < s->cfg.num_agents; i++) {
        const CretAgent* a = &s->agents[i];
        float f[5] = {a->x, a->y, a->z, a->yaw, a->health}; int v[3] = {a->alive, a->act, a->act_ticks};
        hmix(&g_hsrv, f, sizeof f); hmix(&g_hsrv, v, sizeof v);
    }
    hmix(&g_hsrv, &s->hit_count, sizeof s->hit_count); hmix(&g_hsrv, &s->tick, sizeof s->tick);
}

// Writes a recording: the header of this run, every client's ticks, the hashes.
static bool rec_write(const char* path, const RecHead* h, const BClient* C) {
    FILE* f = fopen(path, "wb"); if (!f) { fprintf(stderr, "netbench: cannot write %s\n", path); return false; }
    fwrite(REC_MAGIC, 1, 8, f);
    const uint32_t u[8] = {h->version, h->agents, h->clients, h->seed, h->warmup_ms, h->seconds_ms, h->props, h->snap_hz};
    for (int i = 0; i < 8; i++) put_u32(f, u[i]);
    fwrite(h->name, 1, 64, f); fwrite(h->spec, 1, 64, f);
    for (uint32_t k = 0; k < h->clients; k++) { put_u32(f, C[k].nrec); fwrite(C[k].rec, REC_TICK, C[k].nrec, f); }
    put_u64(f, h->hsrv); put_u64(f, h->hcli);
    bool ok = !ferror(f); fclose(f); return ok;
}
// Reads a recording into its header and the per-client tick arrays (malloc'd);
// returns the file's FNV-1a (the recording's id), 0 if unreadable.
static uint32_t rec_read(const char* path, RecHead* h, uint8_t** ticks, uint32_t* nticks) {
    FILE* f = fopen(path, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* d = n > 0 ? malloc((size_t)n) : NULL; size_t got = d ? fread(d, 1, (size_t)n, f) : 0; fclose(f);
    size_t at = 8 + 32 + 128;
    if (!d || got != (size_t)n || got < at + 16 || memcmp(d, REC_MAGIC, 8)) { free(d); return 0; }
    uint32_t u[8]; for (int i = 0; i < 8; i++) u[i] = get_u32(d + 8 + 4 * i);
    *h = (RecHead){u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], {0}, {0}, 0, 0};
    memcpy(h->name, d + 40, 63); memcpy(h->spec, d + 104, 63);
    if (h->clients < 1 || h->clients > MAXC || h->agents < 1 || h->agents > CRET_MAX_AGENTS) { free(d); return 0; }
    for (uint32_t k = 0; k < h->clients; k++) {
        if (at + 4 > got) { free(d); return 0; }
        nticks[k] = get_u32(d + at); at += 4;
        if (nticks[k] > (got - at) / REC_TICK) { free(d); return 0; }
        ticks[k] = malloc((size_t)nticks[k] * REC_TICK + 1); memcpy(ticks[k], d + at, (size_t)nticks[k] * REC_TICK); at += (size_t)nticks[k] * REC_TICK;
    }
    if (at + 16 != got) { free(d); return 0; }
    h->hsrv = get_u64(d + at); h->hcli = get_u64(d + at + 8);
    uint32_t id = fnv32(d, got); free(d);
    return id ? id : 1;
}
// One recorded tick: the slot-3 switch before it (choice + 1, 0: none) and the input.
static void rec_push(BClient* c, int unlock, const CretInput* in) {
    if (c->nrec == c->caprec) { c->caprec = c->caprec ? 2 * c->caprec : 4096; c->rec = realloc(c->rec, (size_t)c->caprec * REC_TICK); }
    uint8_t* r = &c->rec[(size_t)c->nrec++ * REC_TICK]; uint64_t v = netp_pack_input(in);
    r[0] = (uint8_t)(unlock ? 1 | (unlock - 1) << 1 : 0);
    for (int i = 0; i < 5; i++) r[1 + i] = (uint8_t)(v >> (8 * i));
}
// The next recorded tick of a replaying client (an idle input past the end).
static CretInput rec_next(BClient* c, int* unlock) {
    *unlock = 0;
    if (c->ri >= c->nrec) return (CretInput){0};
    const uint8_t* r = &c->rec[(size_t)c->ri++ * REC_TICK]; uint64_t v = 0;
    for (int i = 0; i < 5; i++) v |= (uint64_t)r[1 + i] << (8 * i);
    if (r[0] & 1) *unlock = 1 + ((r[0] >> 1) & 1);
    return netp_unpack_input(v);
}

typedef struct { float x, y, z; bool alive; } Truth;
static Truth g_truth[HIST_TICKS][CRET_MAX_AGENTS];
static int g_truth_tick[HIST_TICKS];

static bool truth_at(int i, double R, float* x, float* y, float* z) {
    int t0 = (int)floor(R); double u = R - t0;
    int h0 = t0 % HIST_TICKS, h1 = (t0 + 1) % HIST_TICKS;
    if (t0 < 0 || g_truth_tick[h0] != t0 || g_truth_tick[h1] != t0 + 1) return false;
    const Truth *a = &g_truth[h0][i], *b = &g_truth[h1][i];
    if (!a->alive || !b->alive) return false;
    *x = a->x + (b->x - a->x) * (float)u; *y = a->y + (b->y - a->y) * (float)u; *z = a->z + (b->z - a->z) * (float)u;
    return true;
}

// Server props per tick (v11).
typedef struct { int32_t id; float x, y, z; } PTruth;
static PTruth g_ptruth[HIST_TICKS][CRET_MAX_DYN]; static int g_pn[HIST_TICKS];
static bool g_props = true;
// Props move at physics steps (every 2 ticks, on even ticks): the truth is
// interpolated between steps, as the server's own renderer draws it.
static bool ptruth_at(int32_t id, double R, float* x, float* y, float* z) {
    int t0 = 2 * (int)floor(R / 2); double u = (R - t0) / 2; int h[2] = {t0 % HIST_TICKS, (t0 + 2) % HIST_TICKS}; float p[2][3];
    if (t0 < 0 || g_truth_tick[h[0]] != t0 || g_truth_tick[h[1]] != t0 + 2) return false;
    for (int j = 0; j < 2; j++) {
        int k = 0; while (k < g_pn[h[j]] && g_ptruth[h[j]][k].id != id) k++;
        if (k == g_pn[h[j]]) return false;
        p[j][0] = g_ptruth[h[j]][k].x; p[j][1] = g_ptruth[h[j]][k].y; p[j][2] = g_ptruth[h[j]][k].z;
    }
    *x = p[0][0] + (p[1][0] - p[0][0]) * (float)u; *y = p[0][1] + (p[1][1] - p[0][1]) * (float)u; *z = p[0][2] + (p[1][2] - p[0][2]) * (float)u;
    return true;
}
// A boulder or log within 40 m of agent i, dropped from 3 m with a tilt and a shove.
static void drop_prop(CretSim* s, int i, uint32_t* rng) {
    const CretAgent* a = &s->agents[i]; const CretWorld* w = s->world; int pick = -1, seen = 0;
    for (int k = 0; k < w->nprops; k++) {
        const CretPropCollider* pc = &w->props[k];
        float dx = pc->x - a->x, dz = pc->z - a->z;
        if (!pc->dyn || dx * dx + dz * dz > 1600 || cret_prop_mass(w, k) < 100) continue;
        if (frand(rng) * (float)++seen < 1.0f) pick = k;   // reservoir sample
    }
    if (pick < 0) return;
    const CretPropCollider* pc = &w->props[pick]; int r = cret_dyn_find(s, pick);
    float p[3] = {r >= 0 ? s->dyn[r].p[0] : pc->x, (r >= 0 ? s->dyn[r].p[1] : pc->y) + 3, r >= 0 ? s->dyn[r].p[2] : pc->z};
    float q[4] = {frand(rng) - 0.5f, frand(rng) - 0.5f, frand(rng) - 0.5f, 1}, l = 0;
    for (int k = 0; k < 4; k++) l += q[k] * q[k];
    for (int k = 0; k < 4; k++) q[k] /= sqrtf(l);
    cret_dyn_set(s, pick, p, q, true);
}

// Every 4 s the server puts two bots near each fighter, fast ones first,
// 4-8 m off and running across in front of it at 60-100% of their sprint.
static void bring_prey(CretSim* s, int slot, uint32_t* rng) {
    const CretAgent* a = &s->agents[slot]; int placed = 0;
    static const int pref[] = {CRET_SP_GALLI, CRET_SP_RAPTOR, -1};
    for (int p = 0; p < 3 && placed < 2; p++)
        for (int tries = 0; tries < 64 && placed < 2; tries++) {
            int j = (int)(frand(rng) * s->cfg.num_agents); CretAgent* b = &s->agents[j];
            if (j == slot || s->human[j] || !b->alive || (pref[p] >= 0 && b->species != pref[p]) || (a->group && b->group == a->group)) continue;
            float ang = a->yaw + (frand(rng) - 0.5f) * 1.6f, r = 4 + 4 * frand(rng), side = frand(rng) < 0.5f ? -1.0f : 1.0f;
            b->x = a->x + cosf(ang) * r; b->z = a->z + sinf(ang) * r; b->y = cret_height(s->world, b->x, b->z);
            b->yaw = wrapf(ang + side * 1.5707963f); b->speed = b->sprint_speed * (0.6f + 0.4f * frand(rng));
            b->vx = cosf(b->yaw) * b->speed; b->vz = sinf(b->yaw) * b->speed; b->vy = 0; b->on_ground = true; b->swimming = false;
            cret_sim_place(s, j); placed++;
        }
}

static void sleep_s(double s) { if (s <= 0) return; struct timespec ts = {(time_t)s, (long)((s - (time_t)s) * 1e9)}; nanosleep(&ts, NULL); }

typedef struct {
    char scen[64]; int agents, clients;
    CretNetStats st; CretNetServerStats sv; double rerr_mean, rerr_p95, srv_ms; uint64_t ent_extrap, ent_frames; double held_pct;
    double perr_mean, perr_p95, props_snap, prop_bytes_snap; uint64_t perr_n;
    uint64_t attacks, seen, srv, agree, false_hit, lost, prey, dodged, out_of_step; double want_mean, want_p95, want_over;   // hit registration
    CastStat cast[NKIND];   // casts and bites
} Row;

// Replay: the recording being played back (per client ticks), or the one being made.
static uint8_t* g_play[MAXC]; static uint32_t g_playn[MAXC]; static bool g_replay; static const char* g_record;

static int run(const Scenario* sc, int agents, int nclients, double seconds, double warmup, int port, const char* connect, Row* row) {
    CretNetsimCfg ns; if (!cret_netsim_parse(sc->spec, &ns)) { fprintf(stderr, "bad spec %s\n", sc->spec); return 1; }
    // Every run starts from the same virtual time and hashes, so a run is a function of its inputs and seeds.
    if (g_virtual) g_vt = 1000.0;
    g_hsrv = g_hcli = 14695981039346656037ull; uint32_t prop_rng = 4242;
    g_cond = g_win_t < 0; int win_tick = 0;
    CretSim* srv = NULL; CretNet* host = NULL; CretWorld* pre = NULL;
    char addr[128] = "127.0.0.1";
    if (connect) {
        const char* colon = strrchr(connect, ':');
        if (colon) { snprintf(addr, sizeof addr, "%.*s", (int)(colon - connect), connect); port = atoi(colon + 1); }
        else snprintf(addr, sizeof addr, "%s", connect);
        // Load the (default) world before connecting: a client that loads it
        // while connected stops servicing ENet and its peers can time out.
        CretConfig d = cret_default_config(); pre = cret_world_acquire(d.world_seed, d.world_n, d.world_cell, 1);
    } else {
        CretConfig cfg = cret_default_config(); cfg.num_agents = agents; cfg.seed = g_seed;
        srv = calloc(1, sizeof *srv);
        if (cret_sim_init(srv, cfg) != 0) { fprintf(stderr, "netbench: world failed\n"); return 1; }
        host = cret_net_host(port, 0);
        if (!host) return 1;
        cret_net_set_snapshot_hz(host, g_snap_hz);
    }
    BClient* C = calloc(MAXC, sizeof *C);
    for (int k = 0; k < nclients; k++) {
        C[k].net = cret_net_join_start(addr, port, &ns, 1234u + (uint32_t)k * 77u);
        if (!C[k].net) { fprintf(stderr, "netbench: join failed\n"); return 1; }
        C[k].rng = 99u + (uint32_t)k * 7919u; C[k].fighter = k % 2 == 0; C[k].target = -1;
        C[k].period = CRET_DT * (1.0 + (k % 2 ? 2e-4 : -2e-4) * (k + 1));   // client clocks drift a little
        if (g_replay) { C[k].rec = g_play[k]; C[k].nrec = g_playn[k]; C[k].play = true; }
    }
    CretInput inputs[CRET_MAX_AGENTS] = {0}; bool remote[CRET_MAX_AGENTS] = {0};
    double t0 = now_s(), ts = t0, tend = t0 + warmup + seconds + 10; bool reset = false; double tstat = 0;
    double srv_ms = 0; uint64_t srv_ticks = 0; CretNetServerStats sv0 = {0};
    static int32_t seq_tick[MAXC][HIST_TICKS]; static uint32_t seq_of[MAXC][HIST_TICKS]; uint32_t applied[MAXC] = {0}; uint32_t prey_rng = 777;
    memset(seq_of, 0, sizeof seq_of);
    for (int h = 0; h < HIST_TICKS; h++) g_truth_tick[h] = -1;
    memset(g_cast, 0, sizeof g_cast);
    for (;;) {
        double now = now_s();
        if (now > tend) break;
        if (srv && now >= ts) {
            double a = real_s();
            for (int i = 0; i < srv->cfg.num_agents; i++) inputs[i] = cret_bot_input(srv, i);
            for (int k = 0; k < nclients; k++) if (C[k].ready) srv->prog[C[k].slot].unlocked = 0xffffffffu;   // every slot-3 choice is theirs
            cret_net_server_poll(host, srv, inputs, remote);
            for (int k = 0; k < nclients; k++) {   // which inputs each fighter's peer applied this tick
                if (!C[k].ready || !C[k].fighter) continue;
                for (int p = 0; p < CRET_MAX_PEERS; p++) {
                    const NetPeer* np = host->peers[p]; if (!np || np->slot != C[k].slot) continue;
                    for (uint32_t q = applied[k] + 1; q <= np->applied && np->applied - q < HIST_TICKS; q++) {
                        seq_tick[k][q % HIST_TICKS] = srv->tick; seq_of[k][q % HIST_TICKS] = q;
                        double v = C[k].view[q % HIST_TICKS];
                        if (reset && v > 0 && cret_act_is_attack(np->in[q % NET_IN_RING].action)) {   // a press: how far back the client saw
                            double w = srv->tick + 1 - v; int bin = (int)(w * 4);
                            C[k].want_n++; C[k].want_sum += w; C[k].want_over += v < (double)C[k].known[q % HIST_TICKS] - CRET_REWIND_MAX;
                            C[k].want_hist[bin < 0 ? 0 : bin > 99 ? 99 : bin]++;
                        }
                    }
                    applied[k] = np->applied;
                }
            }
            cret_sim_step(srv, inputs);
            for (int k = 0; k < nclients; k++) {   // fighters' swings whose blow the server has resolved
                BClient* c = &C[k];
                while (c->sw_tail != c->sw_head) {
                    const Swing* w = &c->sw[c->sw_tail % NSWING]; int h = (int)(w->seq % HIST_TICKS);
                    if (seq_of[k][h] != w->seq) {
                        if (applied[k] > w->seq + 64) {   // never applied
                            if (g_swings && g_win_t < 0) fprintf(g_swings, "%08x %d %u %d %d 0 4\n", g_rec_id, k, w->seq, w->act, w->seen);
                            c->lost++; c->sw_tail++; continue;
                        }
                        break;
                    }
                    bool hit = false;
                    for (uint32_t e = srv->hit_count > CRET_HIT_LOG ? srv->hit_count - CRET_HIT_LOG : 0; e < srv->hit_count; e++) {
                        const CretHit* x = &srv->hits[e % CRET_HIT_LOG];
                        if (x->tick == seq_tick[k][h] && x->attacker == c->slot && x->kind != CRET_HIT_THORNS && x->kind != CRET_HIT_CRUSH && x->kind != CRET_HIT_OTHER) hit = true;
                    }
                    // Swings that landed on nothing for reasons other than where the target
                    // was are left out, each counted: the bite caught small prey (the snap),
                    // the server's swing was not at its hit on that input (a stun or a
                    // throw cut it short: the prediction's miss, not the hit test's), or an
                    // animal at the strike point was mid dodge-leap (attacks miss it by rule).
                    const CretAgent* a = &srv->agents[c->slot]; const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
                    int hitt = (int)((w->act == CRET_ACT_ATTACK2 ? S->alt.hit_time : S->hit_time) * CRET_TICK_HZ);
                    bool dodge = false; float px, pz; cret_strike_point(a, &px, &pz);
                    for (int j = 0; j < srv->cfg.num_agents; j++)
                        if (j != c->slot && srv->agents[j].alive && srv->agents[j].st[CRET_ST_EVADE] > 0 && cret_body_dist(&srv->agents[j], px, pz) < 2.5f) dodge = true;
                    if (!hit && a->snapped && w->act == CRET_ACT_ATTACK) c->prey++;
                    else if (!hit && (a->act != w->act || a->act_ticks != hitt)) c->out_of_step++;
                    else if (!hit && dodge) c->dodged++;
                    else { c->attacks++; c->seen += w->seen; c->srv += hit; c->agree += hit && w->seen; c->false_hit += hit && !w->seen; }
                    // The swing log (--pair): 0 counted, 1 small prey, 2 out of step, 3 dodged.
                    int why = hit ? 0 : a->snapped && w->act == CRET_ACT_ATTACK ? 1 : a->act != w->act || a->act_ticks != hitt ? 2 : dodge ? 3 : 0;
                    if (g_swings && (g_win_t < 0 || (g_cond && seq_tick[k][h] >= win_tick + WIN_LEAD)))
                        fprintf(g_swings, "%08x %d %u %d %d %d %d\n", g_rec_id, k, w->seq, w->act, w->seen, hit, why);
                    c->sw_tail++;
                }
                if (c->ready && c->fighter && srv->tick % (4 * CRET_TICK_HZ) == 0 && srv->agents[c->slot].alive) bring_prey(srv, c->slot, &prey_rng);
            }
            int h = srv->tick % HIST_TICKS; g_truth_tick[h] = srv->tick;
            for (int i = 0; i < srv->cfg.num_agents; i++) g_truth[h][i] = (Truth){srv->agents[i].x, srv->agents[i].y, srv->agents[i].z, srv->agents[i].alive};
            g_pn[h] = srv->ndyn;
            for (int k = 0; k < srv->ndyn; k++) g_ptruth[h][k] = (PTruth){srv->dyn[k].id, srv->dyn[k].p[0], srv->dyn[k].p[1], srv->dyn[k].p[2]};
            if (g_props && srv->tick % (2 * CRET_TICK_HZ) == 0)
                for (int k = 0; k < nclients; k++) if (C[k].ready && srv->agents[C[k].slot].alive) drop_prop(srv, C[k].slot, &prop_rng);
            hash_server(srv);
            if (cret_net_snapshot_due(host, srv->tick)) cret_net_server_broadcast(host, srv);
            if (reset) { srv_ms += (real_s() - a) * 1000; srv_ticks++; }
            ts += CRET_DT; if (now - ts > 0.25) ts = now;   // fell far behind: do not spiral
        }
        bool all_ready = true;
        for (int k = 0; k < nclients; k++) {
            BClient* c = &C[k];
            cret_net_pump(c->net);
            if (!c->ready) {
                all_ready = false;
                if (!cret_net_client_poll(c->net, NULL)) { fprintf(stderr, "netbench: client %d disconnected before its welcome\n", k); return 1; }
                CretConfig cc; int slot;
                if (cret_net_client_welcomed(c->net, &cc, &slot)) {
                    c->sim = calloc(1, sizeof(CretSim)); c->draw = calloc(1, sizeof(CretSim));
                    if (cret_sim_init(c->sim, cc) != 0) { fprintf(stderr, "netbench: client world failed\n"); return 1; }
                    for (int i = 0; i < cc.num_agents; i++) { c->sim->agents[i].alive = false; cret_sim_place(c->sim, i); }
                    c->slot = slot; c->ready = true; c->next_tick = c->next_frame = now_s() + frand(&c->rng) * CRET_DT;   // a random phase against the server
                    static const int kinds[] = {CRET_SP_RAPTOR, CRET_SP_TREX, CRET_SP_RAPTOR, CRET_SP_PACHY};
                    static const int others[] = {CRET_SP_STEGO, CRET_SP_SAUROPELTA, CRET_SP_GALLI, CRET_SP_RAPTOR, CRET_SP_TREX, CRET_SP_PACHY};
                    cret_net_client_request(c->net, c->fighter ? kinds[(k / 2) % 4] : others[(k / 2) % 6]);
                }
                continue;
            }
            if (now >= c->next_tick) {
                CretInput in; int unlock = 0;
                if (c->play) {   // replay: the recorded tick, and the slot-3 switch sent before it
                    in = rec_next(c, &unlock);
                    if (unlock) cret_net_client_unlock(c->net, unlock - 1);
                } else {
                    if (!c->fighter || !fight(c, &in)) in = script(c);
                    pick_cast(c, &in);
                    if (now >= c->next_choice) { cret_net_client_unlock(c->net, c->choice); unlock = c->choice + 1; c->choice ^= 1; c->next_choice = now + 20; }   // the other slot-3 ability
                    if (g_record) rec_push(c, unlock, &in);
                }
                if (reset) cast_press(c, &in, now);
                if (g_no_view && g_cond) c->net->cl->view = -1;   // lag compensation off: the press carries no view
                const NetClient* cl = c->net->cl;
                c->view[(cl->seq + 1) % HIST_TICKS] = cl->clock_ok && !(g_no_view && g_cond) ? now * CRET_TICK_HZ + cl->clock_off - cl->delay : 0;
                c->known[(cl->seq + 1) % HIST_TICKS] = cl->newest_tick;
                cret_net_client_tick(c->net, c->sim, &in);
                const CretAgent* me = &c->sim->agents[c->slot]; const CretSpecies* S = &CRET_SPECIES_TABLE[me->species];
                int hit = (int)((me->act == CRET_ACT_ATTACK2 ? S->alt.hit_time : S->hit_time) * CRET_TICK_HZ);
                if (c->fighter && reset && me->alive && cret_act_is_attack(me->act) && me->act_ticks == hit) c->judge = cl->seq;   // the predicted blow lands now
                float hf[4] = {me->x, me->y, me->z, me->yaw}; uint32_t hu[3] = {cl->seq, cl->recv_max, (uint32_t)k};
                hmix(&g_hcli, hf, sizeof hf); hmix(&g_hcli, hu, sizeof hu);
                c->next_tick += c->period; if (now - c->next_tick > 0.25) c->next_tick = now;
            }
            if (now >= c->next_frame) {
                if (!cret_net_client_poll(c->net, c->sim)) { fprintf(stderr, "netbench: client %d disconnected\n", k); return 1; }
                float alpha = (float)(1.0 - (c->next_tick - now) / c->period);
                cret_net_client_draw(c->net, c->sim, c->draw, alpha, 1.0f / 60);
                if (reset) cast_frame(c, now);
                const NetClient* cl = c->net->cl;
                if (c->judge) {   // the frame showing the blow: does it reach anything drawn?
                    const CretAgent* me = &c->sim->agents[c->slot]; bool seen = false;
                    for (int j = 0; j < cl->num_agents; j++) if (j != c->slot && cret_swing_reaches(me, &c->draw->agents[j])) seen = true;
                    if (c->sw_head - c->sw_tail < NSWING) c->sw[c->sw_head++ % NSWING] = (Swing){c->judge, seen, me->act};
                    uint32_t hs[2] = {c->judge, seen}; hmix(&g_hcli, hs, sizeof hs);
                    c->judge = 0;
                }
                if (srv && reset && cl->clock_ok) {   // drawn remote agents near us vs the server at their render time
                    double R = now * CRET_TICK_HZ + cl->clock_off - cl->delay;
                    const CretAgent* me = &srv->agents[c->slot];
                    for (int i = 0; i < cl->num_agents; i++) {
                        if (i == c->slot) continue;
                        const CretAgent* a = &c->draw->agents[i]; float x, y, z;
                        if (!a->alive || fabsf(a->x - me->x) + fabsf(a->z - me->z) > 120) continue;
                        if (!truth_at(i, R - cl->extra[i], &x, &y, &z)) continue;
                        float e = sqrtf((a->x - x) * (a->x - x) + (a->y - y) * (a->y - y) + (a->z - z) * (a->z - z));
                        if (e > 20) continue;   // respawned between samples
                        c->rerr_n++; c->rerr_sum += e; int bin = (int)(e * 100); c->rerr_hist[bin > 199 ? 199 : bin]++;
                    }
                    for (int k = 0; k < c->draw->ndyn; k++) {   // v11: drawn props near us vs the server at the render time
                        const CretDynProp* r = &c->draw->dyn[k]; float x, y, z;
                        if (fabsf(r->p[0] - me->x) + fabsf(r->p[2] - me->z) > 120 || !ptruth_at(r->id, R, &x, &y, &z)) continue;
                        float e = sqrtf((r->p[0] - x) * (r->p[0] - x) + (r->p[1] - y) * (r->p[1] - y) + (r->p[2] - z) * (r->p[2] - z));
                        if (e > 20) continue;
                        c->perr_n++; c->perr_sum += e; int bin = (int)(e * 100); c->perr_hist[bin > 199 ? 199 : bin]++;
                    }
                }
                if (k == 0 && getenv("NETBENCH_DEBUG") && c->ticks % 30 == 0) {
                    double R = now * CRET_TICK_HZ + cl->clock_off - cl->delay;
                    fprintf(stderr, "dbg seq %u acked %u newest_tick %u srv_tick %d R %.1f off %.1f jit %.2f delay %.1f boost %.1f qdepth %d\n", cl->seq, cl->acked,
                            cl->newest_tick, srv ? srv->tick : -1, R, cl->clock_off - now * CRET_TICK_HZ, cl->jitter, cl->delay, cl->boost, cl->qdepth);
                }
                c->next_frame += 1.0 / 60; if (now - c->next_frame > 0.25) c->next_frame = now;
            }
        }
        if (all_ready && !reset) {
            if (tstat == 0) {
                tstat = now + warmup; tend = tstat + seconds;
                for (int k = 1; srv && k < nclients; k += 2) {   // each other client starts beside a fighter, 8 m off
                    CretAgent* b = &srv->agents[C[k].slot]; const CretAgent* a = &srv->agents[C[k - 1].slot];
                    b->x = a->x + 8 * cosf(a->yaw); b->z = a->z + 8 * sinf(a->yaw); b->y = cret_height(srv->world, b->x, b->z); cret_sim_place(srv, C[k].slot);
                }
            }
            if (now >= tstat) {
                if (g_win_t >= 0) tend = tstat + g_win_t + g_win_w;
                for (int k = 0; k < nclients; k++) cret_net_client_stats_reset(C[k].net);
                if (host) cret_net_server_stats(host, &sv0);
                reset = true;
            }
        }
        if (reset && !g_cond && now >= tstat + g_win_t) { g_cond = true; win_tick = srv ? srv->tick : 0; }   // the window opens
        if (!all_ready && now - t0 > 10) { fprintf(stderr, "netbench: clients not welcomed in 10 s\n"); return 1; }
        if (g_virtual) g_vt += 0.00025; else sleep_s(0.0005);
    }
    // Aggregate (mean over clients; maxima are maxima).
    memset(row, 0, sizeof *row);
    snprintf(row->scen, sizeof row->scen, "%s", sc->label); row->agents = srv ? srv->cfg.num_agents : C[0].net->cl->num_agents; row->clients = nclients;
    uint64_t wn = 0, wo = 0; double ws = 0; uint32_t wh[100] = {0};
    CretNetStats* A = &row->st; uint64_t rn = 0; double rs = 0; uint32_t rh[200] = {0}; uint64_t pn = 0; double ps = 0; uint32_t ph[200] = {0};
    for (int k = 0; k < nclients; k++) {
        CretNetStats s; cret_net_client_stats(C[k].net, &s);
        A->seconds += s.seconds / nclients; A->up_bytes += s.up_bytes; A->down_bytes += s.down_bytes; A->up_packets += s.up_packets; A->down_packets += s.down_packets;
        A->snapshots += s.snapshots; A->snapshots_dropped += s.snapshots_dropped;
        A->ack_n += s.ack_n; A->ack_sum_ms += s.ack_sum_ms; A->ack_max_ms = fmax(A->ack_max_ms, s.ack_max_ms);
        A->err_n += s.err_n; A->err_sum_m += s.err_sum_m; A->err_max_m = fmax(A->err_max_m, s.err_max_m);
        for (int b = 0; b < 200; b++) { A->ack_hist[b] += s.ack_hist[b]; A->err_hist[b] += s.err_hist[b]; }
        A->corrections += s.corrections; A->snaps += s.snaps; A->corr_sum_m += s.corr_sum_m; A->corr_max_m = fmax(A->corr_max_m, s.corr_max_m);
        A->frames += s.frames; A->underruns += s.underruns; A->ent_frames += s.ent_frames; A->ent_extrapolated += s.ent_extrapolated;
        A->delay_ms += s.delay_ms / nclients; A->rtt_ms += s.rtt_ms / nclients;
        rn += C[k].rerr_n; rs += C[k].rerr_sum; for (int b = 0; b < 200; b++) rh[b] += C[k].rerr_hist[b];
        pn += C[k].perr_n; ps += C[k].perr_sum; for (int b = 0; b < 200; b++) ph[b] += C[k].perr_hist[b];
        row->attacks += C[k].attacks; row->seen += C[k].seen; row->srv += C[k].srv; row->agree += C[k].agree; row->false_hit += C[k].false_hit; row->lost += C[k].lost;
        row->prey += C[k].prey; row->dodged += C[k].dodged; row->out_of_step += C[k].out_of_step;
        wn += C[k].want_n; ws += C[k].want_sum; wo += C[k].want_over; for (int b = 0; b < 100; b++) wh[b] += C[k].want_hist[b];
    }
    row->want_mean = wn ? ws / wn * 1000.0 / CRET_TICK_HZ : 0; row->want_p95 = cret_net_hist_pct(wh, 100, 0.95f) / 4 * 1000.0 / CRET_TICK_HZ;
    row->want_over = wn ? 100.0 * wo / wn : 0;
    memcpy(row->cast, g_cast, sizeof g_cast);
    row->rerr_mean = rn ? rs / rn * 100 : 0; row->rerr_p95 = cret_net_hist_pct(rh, 200, 0.95f);
    row->perr_mean = pn ? ps / pn * 100 : 0; row->perr_p95 = cret_net_hist_pct(ph, 200, 0.95f); row->perr_n = pn;
    if (host) {
        CretNetServerStats s1; cret_net_server_stats(host, &s1);
        row->sv = s1; row->sv.ticks -= sv0.ticks; row->sv.held -= sv0.held; row->sv.caught_up -= sv0.caught_up; row->sv.inputs_lost -= sv0.inputs_lost;
        row->sv.snapshots -= sv0.snapshots; row->sv.snapshot_bytes -= sv0.snapshot_bytes; row->sv.entities_sent -= sv0.entities_sent; row->sv.budget_full -= sv0.budget_full;
        row->sv.props_sent -= sv0.props_sent; row->sv.prop_bits -= sv0.prop_bits; row->sv.props_full -= sv0.props_full;
        row->props_snap = row->sv.snapshots ? (double)row->sv.props_sent / row->sv.snapshots : 0;
        row->prop_bytes_snap = row->sv.snapshots ? row->sv.prop_bits / 8.0 / row->sv.snapshots : 0;
        row->held_pct = row->sv.ticks ? 100.0 * row->sv.held / row->sv.ticks : 0;
        row->srv_ms = srv_ticks ? srv_ms / srv_ticks : 0;
    }
    if (pre) cret_world_release(pre);
    if (g_record && !connect) {
        RecHead h = {CRET_NET_VERSION, (uint32_t)agents, (uint32_t)nclients, g_seed, (uint32_t)lround(warmup * 1000), (uint32_t)lround(seconds * 1000),
                     g_props, (uint32_t)g_snap_hz, {0}, {0}, g_hsrv, g_hcli};
        snprintf(h.name, sizeof h.name, "%s", sc->name); snprintf(h.spec, sizeof h.spec, "%s", sc->spec);
        if (!rec_write(g_record, &h, C)) return 1;
    }
    for (int k = 0; k < nclients; k++) {
        if (!C[k].play) free(C[k].rec);
        cret_net_close(C[k].net);
        if (C[k].sim) { cret_sim_close(C[k].sim); free(C[k].sim); free(C[k].draw); }
    }
    free(C);
    if (host) {   // let the disconnects arrive, then stop
        for (int i = 0; i < 50; i++) { cret_net_server_poll(host, srv, inputs, remote); sleep_s(0.002); }
        cret_net_close(host); cret_sim_close(srv); free(srv);
    }
    return 0;
}

// Hit registration: agreement is server hits among the swings the client saw
// land, false hits are server hits among those it saw miss.
static void print_hits(FILE* f, const Row* r, bool md) {
    double agree = r->seen ? 100.0 * r->agree / r->seen : 0, fh = r->attacks > r->seen ? 100.0 * r->false_hit / (r->attacks - r->seen) : 0;
    if (md)
        fprintf(f, "| %s | %d | %llu | %llu | %llu | %.1f | %.1f | %llu / %llu / %llu | %.0f / %.0f | %.1f |\n", r->scen, r->agents, (unsigned long long)r->attacks,
                (unsigned long long)r->seen, (unsigned long long)r->srv, agree, fh, (unsigned long long)r->prey, (unsigned long long)r->dodged,
                (unsigned long long)r->out_of_step, r->want_mean, r->want_p95, r->want_over);
    else
        fprintf(f, "%-44s hits: attacks %llu  seen-hit %llu  server-hit %llu  agreement %.1f%%  false hits %.1f%%  (left out: prey %llu, dodged %llu, out of step %llu, lost %llu)  view age %.0f/%.0f ms, %.1f%% of views clamped\n",
                r->scen, (unsigned long long)r->attacks, (unsigned long long)r->seen, (unsigned long long)r->srv, agree, fh, (unsigned long long)r->prey,
                (unsigned long long)r->dodged, (unsigned long long)r->out_of_step, (unsigned long long)r->lost,
                r->want_mean, r->want_p95, r->want_over);
}

// Casts and bites: per ability with any press, the presses the server
// carried out and refused, press to start, the error at the ack, the largest
// error and the corrections over the 0.5 s after it.
static void print_casts(FILE* f, const Row* r, bool md) {
    for (int k = 1; k < NKIND; k++) {
        const CastStat* c = &r->cast[k]; if (!c->n && !c->refused) continue;
        const char* name = k == KIND_BITE ? "Bite (lunge)" : k == KIND_LATCHED ? "Pounce, caught hold" : CRET_ABILITIES[k].name; double n = c->n ? (double)c->n : 1;
        double start = c->start_sum / n, start95 = cret_net_hist_pct(c->start_hist, 200, 0.95f) * 5, err = c->err_sum / n * 100, err95 = cret_net_hist_pct(c->err_hist, 200, 0.95f);
        if (md)
            fprintf(f, "| %s | %s | %llu | %llu | %llu | %.0f / %.0f | %.1f / %.0f | %.1f | %.1f / %.0f |\n", r->scen, name, (unsigned long long)c->n, (unsigned long long)c->refused, (unsigned long long)c->mispredicted,
                    start, start95, err, err95, c->errmax_sum / n * 100, c->corr_sum / n * 100, c->corr_max * 100);
        else
            fprintf(f, "%-44s %-19s casts %4llu  refused %3llu  mispredicted %3llu  not shown %llu  press to start %4.0f/%4.0f ms  error at ack %6.1f/%4.0f cm  largest in 0.5 s %6.1f cm  corrected in 0.5 s %6.1f cm (max %4.0f)\n",
                    r->scen, name, (unsigned long long)c->n, (unsigned long long)c->refused, (unsigned long long)c->mispredicted, (unsigned long long)c->unshown, start, start95, err, err95,
                    c->errmax_sum / n * 100, c->corr_sum / n * 100, c->corr_max * 100);
    }
}

static void print_row(FILE* f, const Row* r, bool md) {
    const CretNetStats* s = &r->st; double sec = s->seconds > 0 ? s->seconds : 1; int nc = r->clients;
    double down = s->down_bytes / sec / nc / 1024, up = s->up_bytes / sec / nc / 1024, snaps = s->snapshots / sec / nc;
    double ack = s->ack_n ? s->ack_sum_ms / s->ack_n : 0, ack95 = cret_net_hist_pct(s->ack_hist, 200, 0.95f) * 5;
    double err = s->err_n ? s->err_sum_m / s->err_n * 1000 : 0, err95 = cret_net_hist_pct(s->err_hist, 200, 0.95f);
    double cpm = s->corrections / sec / nc * 60, csz = s->corrections ? s->corr_sum_m / s->corrections * 100 : 0;
    double under = s->frames ? 100.0 * s->underruns / s->frames : 0, ex = s->ent_frames ? 100.0 * s->ent_extrapolated / s->ent_frames : 0;
    if (md)
        fprintf(f, "| %s | %d | %.1f | %.1f | %.1f | %.0f / %.0f | %.1f / %.0f | %.1f (%.1f cm, max %.0f cm) | %llu | %.2f | %.2f | %.0f | %.1f / %.0f | %.1f | %llu | %.2f (%.1f B) | %.1f / %.0f |\n",
                r->scen, r->agents, down, up, snaps, ack, ack95, err, err95, cpm, csz, s->corr_max_m * 100, (unsigned long long)s->snaps,
                under, ex, s->delay_ms, r->rerr_mean, r->rerr_p95, r->held_pct, (unsigned long long)s->snapshots_dropped,
                r->props_snap, r->prop_bytes_snap, r->perr_mean, r->perr_p95);
    else
        fprintf(f, "%-44s agents %3d  rtt %4.0f ms  down %5.1f KB/s  up %4.1f KB/s  snaps %4.1f/s  ack %4.0f/%4.0f ms  pred err %5.1f/%3.0f mm  "
                   "corr %5.1f/min (%4.1f cm, max %3.0f cm) snaps %llu  underrun %5.2f%%  extrap %5.2f%%  delay %3.0f ms  remote err %4.1f/%3.0f cm  "
                   "held %4.1f%%  dropped %llu  srv %.2f ms/tick  budget-full %llu  props %.2f/snap (%.1f B, full %llu)  prop err %4.1f/%3.0f cm (%llu)\n",
                r->scen, r->agents, s->rtt_ms, down, up, snaps, ack, ack95, err, err95, cpm, csz, s->corr_max_m * 100, (unsigned long long)s->snaps,
                under, ex, s->delay_ms, r->rerr_mean, r->rerr_p95, r->held_pct, (unsigned long long)s->snapshots_dropped, r->srv_ms,
                (unsigned long long)r->sv.budget_full, r->props_snap, r->prop_bytes_snap, (unsigned long long)r->sv.props_full, r->perr_mean, r->perr_p95,
                (unsigned long long)r->perr_n);
}

// --pair: two swing logs (--swings) of the same recordings replayed by two
// builds or settings, matched swing by swing (recording, client, input).
typedef struct { uint32_t rec, k, seq; int act, seen, hit, why; } SwingRec;
static int cmp_swing(const void* a, const void* b) {
    const SwingRec *x = a, *y = b;
    if (x->rec != y->rec) return x->rec < y->rec ? -1 : 1;
    if (x->k != y->k) return x->k < y->k ? -1 : 1;
    return x->seq < y->seq ? -1 : x->seq > y->seq;
}
static int load_swings(const char* path, SwingRec** out) {
    FILE* f = fopen(path, "r"); if (!f) { fprintf(stderr, "netbench: cannot read %s\n", path); return -1; }
    int n = 0, cap = 0; SwingRec r; *out = NULL;
    while (fscanf(f, "%x %u %u %d %d %d %d", &r.rec, &r.k, &r.seq, &r.act, &r.seen, &r.hit, &r.why) == 7) {
        if (n == cap) { cap = cap ? 2 * cap : 1024; *out = realloc(*out, sizeof(SwingRec) * (size_t)cap); }
        (*out)[n++] = r;
    }
    fclose(f);
    qsort(*out, (size_t)n, sizeof(SwingRec), cmp_swing);
    return n;
}
// Two-sided exact McNemar test: the chance of discordant counts at least as
// uneven as b against c if neither condition were more likely to differ.
static double mcnemar_p(uint64_t b, uint64_t c) {
    uint64_t n = b + c, m = b < c ? b : c; double p = 0;
    if (!n) return 1;
    for (uint64_t i = 0; i <= m; i++) p += exp(lgamma((double)n + 1) - lgamma((double)i + 1) - lgamma((double)(n - i) + 1) - (double)n * log(2.0));
    return fmin(1.0, 2 * p);
}
// Mean, standard deviation and paired t of per-recording differences.
static void paired_t(const double* d, int n, double* mean, double* sd, double* t) {
    double m = 0, v = 0; for (int i = 0; i < n; i++) m += d[i] / n;
    for (int i = 0; i < n; i++) v += (d[i] - m) * (d[i] - m) / (n > 1 ? n - 1 : 1);
    *mean = m; *sd = sqrt(v); *t = n > 1 && v > 0 ? m / (sqrt(v) / sqrt(n)) : 0;
}
static int pair(const char* pa, const char* pb) {
    SwingRec *A, *B; int na = load_swings(pa, &A), nb = load_swings(pb, &B);
    if (na < 0 || nb < 0) return 1;
    // Over swings counted (not left out) in both: per condition seen, landed,
    // agreement and false hits; discordant pairs of the mismatch (seen !=
    // landed) and of the server's verdict among swings seen alike in both.
    uint64_t only_a = 0, only_b = 0, left_one = 0, both = 0, seen_diff = 0;
    uint64_t seen[2] = {0}, hit[2] = {0}, agree[2] = {0}, fals[2] = {0}, mis[2] = {0}, mis_b = 0, mis_c = 0;
    uint64_t sh_n = 0, sh_b = 0, sh_c = 0, sm_n = 0, sm_b = 0, sm_c = 0;
    enum { MAXREC = 256 }; uint32_t recs[MAXREC]; int nrec = 0; uint64_t pr[MAXREC][2][4];   // per recording: seen, agree, unseen, false
    memset(pr, 0, sizeof pr);
    for (int i = 0, j = 0; i < na || j < nb;) {
        int c = i >= na ? 1 : j >= nb ? -1 : cmp_swing(&A[i], &B[j]);
        if (c < 0) { only_a++; i++; continue; }
        if (c > 0) { only_b++; j++; continue; }
        const SwingRec *x = &A[i++], *y = &B[j++];
        if (x->why || y->why) { left_one += (x->why == 0) != (y->why == 0); continue; }
        both++;
        int r = nrec; for (int q = 0; q < nrec; q++) if (recs[q] == x->rec) r = q;
        if (r == nrec && nrec < MAXREC) recs[nrec++] = x->rec;
        const SwingRec* v[2] = {x, y};
        for (int k = 0; k < 2; k++) {
            seen[k] += v[k]->seen; hit[k] += v[k]->hit; agree[k] += v[k]->seen && v[k]->hit; fals[k] += !v[k]->seen && v[k]->hit; mis[k] += v[k]->seen != v[k]->hit;
            if (r < MAXREC) { pr[r][k][0] += v[k]->seen; pr[r][k][1] += v[k]->seen && v[k]->hit; pr[r][k][2] += !v[k]->seen; pr[r][k][3] += !v[k]->seen && v[k]->hit; }
        }
        mis_b += (x->seen != x->hit) && (y->seen == y->hit); mis_c += (x->seen == x->hit) && (y->seen != y->hit);
        seen_diff += x->seen != y->seen;
        if (x->seen && y->seen) { sh_n++; sh_b += x->hit && !y->hit; sh_c += !x->hit && y->hit; }
        if (!x->seen && !y->seen) { sm_n++; sm_b += x->hit && !y->hit; sm_c += !x->hit && y->hit; }
    }
    double pct[2][3];
    for (int k = 0; k < 2; k++) {
        pct[k][0] = seen[k] ? 100.0 * agree[k] / seen[k] : 0; pct[k][1] = both > seen[k] ? 100.0 * fals[k] / (both - seen[k]) : 0; pct[k][2] = both ? 100.0 * mis[k] / both : 0;
    }
    printf("paired swings: %llu counted in both (%llu only in A, %llu only in B, %llu left out in one of the two), the client's judgement differs on %llu\n",
           (unsigned long long)both, (unsigned long long)only_a, (unsigned long long)only_b, (unsigned long long)left_one, (unsigned long long)seen_diff);
    printf("                        A: %-28s  B: %s\n", pa, pb);
    printf("agreement %%             %6.2f (%llu/%llu)  %6.2f (%llu/%llu)\n", pct[0][0], (unsigned long long)agree[0], (unsigned long long)seen[0], pct[1][0], (unsigned long long)agree[1], (unsigned long long)seen[1]);
    printf("false hits %%            %6.2f (%llu/%llu)  %6.2f (%llu/%llu)\n", pct[0][1], (unsigned long long)fals[0], (unsigned long long)(both - seen[0]), pct[1][1], (unsigned long long)fals[1], (unsigned long long)(both - seen[1]));
    printf("mismatch %%              %6.2f  %6.2f   discordant: A only %llu, B only %llu, McNemar p = %.3g\n", pct[0][2], pct[1][2], (unsigned long long)mis_b, (unsigned long long)mis_c, mcnemar_p(mis_b, mis_c));
    printf("seen to land in both (%llu): landed in A only %llu, in B only %llu, McNemar p = %.3g\n", (unsigned long long)sh_n, (unsigned long long)sh_b, (unsigned long long)sh_c, mcnemar_p(sh_b, sh_c));
    printf("seen to miss in both (%llu): landed in A only %llu, in B only %llu, McNemar p = %.3g\n", (unsigned long long)sm_n, (unsigned long long)sm_b, (unsigned long long)sm_c, mcnemar_p(sm_b, sm_c));
    double da[MAXREC], df[MAXREC]; int nd = 0;
    for (int r = 0; r < nrec; r++) {
        if (!pr[r][0][0] || !pr[r][1][0] || !pr[r][0][2] || !pr[r][1][2]) continue;
        da[nd] = 100.0 * ((double)pr[r][1][1] / pr[r][1][0] - (double)pr[r][0][1] / pr[r][0][0]);
        df[nd] = 100.0 * ((double)pr[r][1][3] / pr[r][1][2] - (double)pr[r][0][3] / pr[r][0][2]); nd++;
    }
    double m, sd, t;
    paired_t(da, nd, &m, &sd, &t); printf("per recording (%d), B - A: agreement %+.2f points (sd %.2f, paired t %.2f)", nd, m, sd, t);
    paired_t(df, nd, &m, &sd, &t); printf(", false hits %+.2f points (sd %.2f, paired t %.2f)\n", m, sd, t);
    free(A); free(B);
    return 0;
}

int main(int argc, char** argv) {
    const char* scen = "all"; const char* agents_s = "64,128"; const char* md = NULL; const char* connect = NULL; const char* hits_md = NULL; const char* casts_md = NULL;
    const char* replay = NULL; const char* swings = NULL; bool show_hash = false, check = false;
    int nclients = 4, port = 27600; double seconds = 20, warmup = 3;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--agents") && i + 1 < argc) agents_s = argv[++i];
        else if (!strcmp(argv[i], "--clients") && i + 1 < argc) nclients = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--warmup") && i + 1 < argc) warmup = atof(argv[++i]);
        else if (!strcmp(argv[i], "--scenario") && i + 1 < argc) scen = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--md") && i + 1 < argc) md = argv[++i];
        else if (!strcmp(argv[i], "--hits") && i + 1 < argc) hits_md = argv[++i];
        else if (!strcmp(argv[i], "--casts") && i + 1 < argc) casts_md = argv[++i];
        else if (!strcmp(argv[i], "--connect") && i + 1 < argc) { connect = argv[++i]; g_virtual = false; }
        else if (!strcmp(argv[i], "--realtime")) g_virtual = false;
        else if (!strcmp(argv[i], "--no-props")) g_props = false;
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) g_seed = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--snapshot-hz") && i + 1 < argc) g_snap_hz = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--no-view")) g_no_view = true;
        else if (!strcmp(argv[i], "--record") && i + 1 < argc) g_record = argv[++i];
        else if (!strcmp(argv[i], "--replay") && i + 1 < argc) replay = argv[++i];
        else if (!strcmp(argv[i], "--swings") && i + 1 < argc) swings = argv[++i];
        else if (!strcmp(argv[i], "--hash")) show_hash = true;
        else if (!strcmp(argv[i], "--window") && i + 2 < argc) { g_win_t = atof(argv[i + 1]); g_win_w = atof(argv[i + 2]); i += 2; }
        else if (!strcmp(argv[i], "--check")) check = show_hash = true;
        else if (!strcmp(argv[i], "--pair") && i + 2 < argc) return pair(argv[i + 1], argv[i + 2]);
        else { fprintf(stderr, "usage: see the header of ocean/cretaceous/tools/netbench.c\n"); return 2; }
    }
    if (g_snap_hz != 15 && g_snap_hz != 20 && g_snap_hz != 30) { fprintf(stderr, "--snapshot-hz 15, 20 or 30\n"); return 2; }
    if (check && !replay) { fprintf(stderr, "netbench: --check goes with --replay\n"); return 2; }
    if (g_win_t >= 0 && (!replay || g_snap_hz != CRET_SNAPSHOT_HZ || g_win_w <= 0)) { fprintf(stderr, "netbench: --window T W goes with --replay, at the default snapshot rate\n"); return 2; }
    // A replay takes its clients, agents, seed, timing, props and scenario from the recording.
    RecHead rh = {0}; Scenario rsc = {0}; static char rlabel[96];
    if (replay) {
        if (g_record || connect) { fprintf(stderr, "netbench: --replay goes without --record and --connect\n"); return 2; }
        g_rec_id = rec_read(replay, &rh, g_play, g_playn);
        if (!g_rec_id) { fprintf(stderr, "netbench: %s is not a recording\n", replay); return 2; }
        if (rh.version != CRET_NET_VERSION) fprintf(stderr, "netbench: %s was recorded with protocol %u, this is %d (inputs are the same 40 bits)\n", replay, rh.version, CRET_NET_VERSION);
        g_replay = true; nclients = (int)rh.clients; g_seed = rh.seed; g_props = rh.props != 0; warmup = rh.warmup_ms / 1000.0; seconds = rh.seconds_ms / 1000.0;
        static char agents_one[16]; snprintf(agents_one, sizeof agents_one, "%u", rh.agents); agents_s = agents_one;
        snprintf(rlabel, sizeof rlabel, "%s", rh.name);
        for (int s = 0; s < NSCEN; s++) if (!strcmp(SCEN[s].name, rh.name)) snprintf(rlabel, sizeof rlabel, "%s", SCEN[s].label);
        rsc = (Scenario){rh.name, rlabel, rh.spec};
    }
    if (nclients < 1 || nclients > MAXC) { fprintf(stderr, "--clients 1..%d\n", MAXC); return 2; }
    if (g_virtual) cret_netsim_set_clock(vclock);
    if (swings && !(g_swings = fopen(swings, "w"))) { fprintf(stderr, "netbench: cannot write %s\n", swings); return 2; }
    FILE* out = md ? fopen(md, "w") : NULL;
    if (out) {
        fprintf(out, "| Scenario | Agents | Down KB/s | Up KB/s | Snap/s | Input to ack ms (mean / p95) | Prediction error mm (mean / p95) | "
                     "Corrections /min (mean, max) | Snaps | Underrun %% | Extrapolated %% | Delay ms | Remote error cm (mean / p95) | Held %% | Refused | Props / snapshot (bytes) | Prop error cm (mean / p95) |\n");
        fprintf(out, "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    }
    FILE* hout = hits_md ? fopen(hits_md, "w") : NULL;
    if (hout) {
        fprintf(hout, "| Scenario | Agents | Attacks | Seen hit | Server hit | Agreement %% | False hits %% | Left out: prey / dodged / out of step | View age ms (mean / p95) | Views clamped %% |\n");
        fprintf(hout, "|---|---|---|---|---|---|---|---|---|---|\n");
    }
    FILE* cout = casts_md ? fopen(casts_md, "w") : NULL;
    if (cout) {
        fprintf(cout, "| Scenario | Ability | Casts | Refused | Mispredicted | Press to start ms (mean / p95) | Error at the ack cm (mean / p95) | Largest error in 0.5 s cm | Corrected in 0.5 s cm (mean / max) |\n");
        fprintf(cout, "|---|---|---|---|---|---|---|---|---|\n");
    }
    char list[256]; snprintf(list, sizeof list, "%s", agents_s);
    int agent_counts[8], na = 0;
    for (char* t = strtok(list, ","); t && na < 8; t = strtok(NULL, ",")) agent_counts[na++] = atoi(t);
    if (connect) na = 1;
    int rc = 0, nrun = 0;
    for (int s = 0; s < NSCEN; s++) nrun += !strcmp(scen, "all") || strstr(scen, SCEN[s].name);
    if (g_record && (connect || na * nrun != 1)) { fprintf(stderr, "netbench: --record takes one scenario and one agent count\n"); return 2; }
    for (int a = 0; a < na; a++)
        for (int s = 0; s < (replay ? 1 : NSCEN); s++) {
            const Scenario* sc = replay ? &rsc : &SCEN[s];
            if (!replay && strcmp(scen, "all") && !strstr(scen, SCEN[s].name)) continue;
            Row r;
            fprintf(stderr, "netbench: %s (%s), %d agents, %d clients, %d Hz snapshots%s%s%s\n", sc->label, sc->spec, agent_counts[a], nclients, g_snap_hz,
                    g_no_view ? ", no views" : "", replay ? ", replaying " : "", replay ? replay : "");
            if (run(sc, agent_counts[a], nclients, seconds, warmup, port, connect, &r) != 0) { rc = 1; continue; }
            print_row(stdout, &r, false); print_hits(stdout, &r, false); print_casts(stdout, &r, false); fflush(stdout);
            if (show_hash) printf("hashes: server %016llx clients %016llx\n", (unsigned long long)g_hsrv, (unsigned long long)g_hcli);
            if (check && (g_hsrv != rh.hsrv || g_hcli != rh.hcli)) {
                fprintf(stderr, "netbench: the replay differs from the recording (server %016llx, clients %016llx recorded)\n", (unsigned long long)rh.hsrv, (unsigned long long)rh.hcli);
                rc = 1;
            } else if (check) fprintf(stderr, "netbench: the replay matches the recording (server %016llx, clients %016llx)\n", (unsigned long long)g_hsrv, (unsigned long long)g_hcli);
            if (out) { print_row(out, &r, true); fflush(out); }
            if (hout) { print_hits(hout, &r, true); fflush(hout); }
            if (cout) { print_casts(cout, &r, true); fflush(cout); }
            port++;
        }
    if (out) fclose(out);
    if (hout) fclose(hout);
    if (cout) fclose(cout);
    if (g_swings) fclose(g_swings);
    return rc;
}
