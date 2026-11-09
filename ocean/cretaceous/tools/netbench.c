// netbench: a server and N clients in one process, over real UDP sockets on
// localhost, each client behind its own link conditioner (netsim.c), with
// scripted inputs. Reports per scenario: bandwidth per client, snapshot rate,
// input-to-ack latency, prediction error and corrections, interpolation
// underruns, and how far drawn remote agents are from the server's truth.
// Movable props (v11): every 2 s the server drops a boulder or log near each
// client from a few metres up (--no-props: off); reports prop states and
// bytes per snapshot and how far drawn props near the client are from the
// server's truth at the render time.
//
//   netbench [--agents 64[,128]] [--clients 4] [--seconds 20] [--warmup 3]
//            [--scenario all|lan,80ms,...] [--port 27600] [--md FILE]
//   netbench --connect HOST:PORT [--clients 2] [--seconds 10] [--scenario lan]
//            (clients only, against a running `play server`: 2-process smoke test)
//   --realtime   wall clock instead of virtual time (see below)
//   --no-props   no boulder drops (v11 props)
//
// Build: make -C ocean/cretaceous netbench CC=clang. Run from the repository root.
#include "../net_priv.h"
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

typedef struct {
    CretNet* net; CretSim* sim; CretSim* draw; int slot; bool ready;
    double next_tick, next_frame, period;
    uint32_t rng; int seg, ticks; CretInput cur; float turn0, phase;
    // Remote agents drawn vs the server (cm bins).
    uint64_t rerr_n; double rerr_sum; uint32_t rerr_hist[200];
    uint64_t perr_n; double perr_sum; uint32_t perr_hist[200];   // drawn props vs the server (cm bins)
} BClient;

// Virtual time (default): the clock advances in fixed steps and everything
// due runs, however long the machine takes, so the results do not depend on
// load. Packets still cross real localhost sockets and ENet; only ENet's own
// timers (pings, retransmits, its RTT) stay on wall time. --realtime runs on
// the wall clock (needed for --connect).
static double g_vt = 1000.0; static bool g_virtual = true;
static double vclock(void) { return g_vt; }
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

static void sleep_s(double s) { if (s <= 0) return; struct timespec ts = {(time_t)s, (long)((s - (time_t)s) * 1e9)}; nanosleep(&ts, NULL); }

typedef struct {
    char scen[64]; int agents, clients;
    CretNetStats st; CretNetServerStats sv; double rerr_mean, rerr_p95, srv_ms; uint64_t ent_extrap, ent_frames; double held_pct;
    double perr_mean, perr_p95, props_snap, prop_bytes_snap; uint64_t perr_n;
} Row;

static int run(const Scenario* sc, int agents, int nclients, double seconds, double warmup, int port, const char* connect, Row* row) {
    CretNetsimCfg ns; if (!cret_netsim_parse(sc->spec, &ns)) { fprintf(stderr, "bad spec %s\n", sc->spec); return 1; }
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
        CretConfig cfg = cret_default_config(); cfg.num_agents = agents; cfg.seed = 7;
        srv = calloc(1, sizeof *srv);
        if (cret_sim_init(srv, cfg) != 0) { fprintf(stderr, "netbench: world failed\n"); return 1; }
        host = cret_net_host(port, 0);
        if (!host) return 1;
    }
    BClient* C = calloc(MAXC, sizeof *C);
    for (int k = 0; k < nclients; k++) {
        C[k].net = cret_net_join_start(addr, port, &ns, 1234u + (uint32_t)k * 77u);
        if (!C[k].net) { fprintf(stderr, "netbench: join failed\n"); return 1; }
        C[k].rng = 99u + (uint32_t)k * 7919u;
        C[k].period = CRET_DT * (1.0 + (k % 2 ? 2e-4 : -2e-4) * (k + 1));   // client clocks drift a little
    }
    CretInput inputs[CRET_MAX_AGENTS] = {0}; bool remote[CRET_MAX_AGENTS] = {0};
    double t0 = now_s(), ts = t0, tend = t0 + warmup + seconds + 10; bool reset = false; double tstat = 0;
    double srv_ms = 0; uint64_t srv_ticks = 0; CretNetServerStats sv0 = {0};
    for (int h = 0; h < HIST_TICKS; h++) g_truth_tick[h] = -1;
    for (;;) {
        double now = now_s();
        if (now > tend) break;
        if (srv && now >= ts) {
            double a = real_s();
            for (int i = 0; i < srv->cfg.num_agents; i++) inputs[i] = cret_bot_input(srv, i);
            cret_net_server_poll(host, srv, inputs, remote);
            cret_sim_step(srv, inputs);
            int h = srv->tick % HIST_TICKS; g_truth_tick[h] = srv->tick;
            for (int i = 0; i < srv->cfg.num_agents; i++) g_truth[h][i] = (Truth){srv->agents[i].x, srv->agents[i].y, srv->agents[i].z, srv->agents[i].alive};
            g_pn[h] = srv->ndyn;
            for (int k = 0; k < srv->ndyn; k++) g_ptruth[h][k] = (PTruth){srv->dyn[k].id, srv->dyn[k].p[0], srv->dyn[k].p[1], srv->dyn[k].p[2]};
            if (g_props && srv->tick % (2 * CRET_TICK_HZ) == 0) {
                static uint32_t prng = 4242;
                for (int k = 0; k < nclients; k++) if (C[k].ready && srv->agents[C[k].slot].alive) drop_prop(srv, C[k].slot, &prng);
            }
            if (srv->tick % CRET_SNAPSHOT_DIV == 0) cret_net_server_broadcast(host, srv);
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
                }
                continue;
            }
            if (now >= c->next_tick) {
                CretInput in = script(c);
                cret_net_client_tick(c->net, c->sim, &in);
                c->next_tick += c->period; if (now - c->next_tick > 0.25) c->next_tick = now;
            }
            if (now >= c->next_frame) {
                if (!cret_net_client_poll(c->net, c->sim)) { fprintf(stderr, "netbench: client %d disconnected\n", k); return 1; }
                float alpha = (float)(1.0 - (c->next_tick - now) / c->period);
                cret_net_client_draw(c->net, c->sim, c->draw, alpha, 1.0f / 60);
                const NetClient* cl = c->net->cl;
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
            if (tstat == 0) { tstat = now + warmup; tend = tstat + seconds; }
            if (now >= tstat) {
                for (int k = 0; k < nclients; k++) cret_net_client_stats_reset(C[k].net);
                if (host) cret_net_server_stats(host, &sv0);
                reset = true;
            }
        }
        if (!all_ready && now - t0 > 10) { fprintf(stderr, "netbench: clients not welcomed in 10 s\n"); return 1; }
        if (g_virtual) g_vt += 0.00025; else sleep_s(0.0005);
    }
    // Aggregate (mean over clients; maxima are maxima).
    memset(row, 0, sizeof *row);
    snprintf(row->scen, sizeof row->scen, "%s", sc->label); row->agents = srv ? srv->cfg.num_agents : C[0].net->cl->num_agents; row->clients = nclients;
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
    }
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
    for (int k = 0; k < nclients; k++) {
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

int main(int argc, char** argv) {
    const char* scen = "all"; const char* agents_s = "64,128"; const char* md = NULL; const char* connect = NULL;
    int nclients = 4, port = 27600; double seconds = 20, warmup = 3;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--agents") && i + 1 < argc) agents_s = argv[++i];
        else if (!strcmp(argv[i], "--clients") && i + 1 < argc) nclients = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--warmup") && i + 1 < argc) warmup = atof(argv[++i]);
        else if (!strcmp(argv[i], "--scenario") && i + 1 < argc) scen = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--md") && i + 1 < argc) md = argv[++i];
        else if (!strcmp(argv[i], "--connect") && i + 1 < argc) { connect = argv[++i]; g_virtual = false; }
        else if (!strcmp(argv[i], "--realtime")) g_virtual = false;
        else if (!strcmp(argv[i], "--no-props")) g_props = false;
        else { fprintf(stderr, "usage: see the header of ocean/cretaceous/tools/netbench.c\n"); return 2; }
    }
    if (nclients < 1 || nclients > MAXC) { fprintf(stderr, "--clients 1..%d\n", MAXC); return 2; }
    if (g_virtual) cret_netsim_set_clock(vclock);
    FILE* out = md ? fopen(md, "w") : NULL;
    if (out) {
        fprintf(out, "| Scenario | Agents | Down KB/s | Up KB/s | Snap/s | Input to ack ms (mean / p95) | Prediction error mm (mean / p95) | "
                     "Corrections /min (mean, max) | Snaps | Underrun %% | Extrapolated %% | Delay ms | Remote error cm (mean / p95) | Held %% | Refused | Props / snapshot (bytes) | Prop error cm (mean / p95) |\n");
        fprintf(out, "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    }
    char list[256]; snprintf(list, sizeof list, "%s", agents_s);
    int agent_counts[8], na = 0;
    for (char* t = strtok(list, ","); t && na < 8; t = strtok(NULL, ",")) agent_counts[na++] = atoi(t);
    if (connect) na = 1;
    int rc = 0;
    for (int a = 0; a < na; a++)
        for (int s = 0; s < NSCEN; s++) {
            if (strcmp(scen, "all") && !strstr(scen, SCEN[s].name)) continue;
            Row r;
            fprintf(stderr, "netbench: %s (%s), %d agents, %d clients\n", SCEN[s].label, SCEN[s].spec, agent_counts[a], nclients);
            if (run(&SCEN[s], agent_counts[a], nclients, seconds, warmup, port, connect, &r) != 0) { rc = 1; continue; }
            print_row(stdout, &r, false); fflush(stdout);
            if (out) { print_row(out, &r, true); fflush(out); }
            port++;
        }
    if (out) fclose(out);
    return rc;
}
