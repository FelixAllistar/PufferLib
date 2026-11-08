// Protocol v6 codec test and fuzzer. Build with sanitizers (make netfuzz):
// any out-of-bounds access or undefined behaviour aborts.
//
//   netfuzz [--packets N] [--seed S] [--no-sim]
//
// 1. Round trip: a fake server encodes snapshots of a randomly evolving sim
//    for a fake client over a lossy, duplicating, reordering channel, with
//    the client's acks (inside its input packets) going back over the same
//    kind of channel. Every snapshot the client accepts must decode to
//    exactly what the server quantised; no valid snapshot may be refused for
//    a missing baseline; inputs must arrive as sent.
// 2. Fuzz: valid snapshots, inputs and welcomes mutated (bit flips, byte
//    overwrites, truncation, extension, splices) and pure random datagrams go
//    to the client and server decoders; accepted states are applied to a
//    real client sim and drawn (unless --no-sim), so the dequantised values
//    reach the interpolation and prediction code too.
// Run from the repository root (the sim loads the cached world).
#include "../net_priv.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t g_rng = 1;
static uint32_t r32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return (uint32_t)(g_rng >> 16); }
static float rf(void) { return (r32() & 0xffffff) / 16777216.0f; }
static int ri(int n) { return n > 0 ? (int)(r32() % (uint32_t)n) : 0; }

#define NA 128
static void evolve(CretSim* s, bool init) {
    for (int i = 0; i < s->cfg.num_agents; i++) {
        CretAgent* a = &s->agents[i];
        if (init) {
            memset(a, 0, sizeof *a); a->alive = true; a->species = (uint8_t)ri(CRET_SPECIES); a->growth = 0.35f + 0.65f * rf();
            a->max_health = 100; a->health = 100 * rf(); a->x = 8192 * rf(); a->z = 8192 * rf(); a->y = 600 * rf() - 50; a->yaw = 6.28f * rf() - 3.14f;
        }
        int mode = ri(100);
        if (mode < 2) { a->alive = !a->alive; a->dead_ticks = 0; }
        if (!a->alive) { a->dead_ticks++; continue; }
        if (mode < 60) {   // moving
            float sp = mode < 10 ? 12 * rf() : 3 * rf();
            a->speed = sp; a->x += cosf(a->yaw) * sp / 30 * 2; a->z += sinf(a->yaw) * sp / 30 * 2; a->y += rf() - 0.5f;
            a->yaw += (rf() - 0.5f) * 0.3f; if (a->yaw > 3.14159f) a->yaw -= 6.2831853f; if (a->yaw < -3.14159f) a->yaw += 6.2831853f;
        } else a->speed = 0;
        if (mode == 70) { a->x = 8192 * rf(); a->z = 8192 * rf(); }   // teleport (respawn)
        if (mode == 71) { a->x = -100; a->z = 1e7f; a->y = -5000; }  // out of range: clamps
        if (mode < 5) a->act = (uint8_t)ri(CRET_ACT_REST + 1);
        if (mode == 6) a->hurt_ticks = 12; else if (a->hurt_ticks > 0) a->hurt_ticks--;
        if (mode == 7) a->group = (uint8_t)ri(s->cfg.num_agents + 1);
        if (mode == 8) { a->rally_ticks = 600; a->rally_x = 8192 * rf(); a->rally_z = 8192 * rf(); } else if (a->rally_ticks > 0) a->rally_ticks--;
        if (mode == 9) a->species = (uint8_t)ri(CRET_SPECIES);
        a->health = fminf(a->max_health, fmaxf(0, a->health + (rf() - 0.5f) * 5)); a->growth = fminf(1, a->growth + 0.0005f);
        a->on_ground = mode % 3 != 0; a->swimming = mode % 7 == 0; a->sprinting = mode < 10; a->broken = mode % 11 == 0 ? 5 : 0;
        a->bleed = mode % 13 == 0 ? 10 : 0;
    }
    for (int c = 0; c < CRET_MAX_CARCASSES; c++) {
        CretCarcass* k = &s->carcasses[c];
        if (init || ri(200) == 0) {
            k->active = ri(2); k->species = (uint8_t)ri(CRET_SPECIES); k->x = 8192 * rf(); k->z = 8192 * rf(); k->y = 100 * rf(); k->yaw = rf() * 6 - 3;
            k->growth = rf(); k->max_meat = 100; k->meat = 100 * rf(); k->age = 0; k->agent = ri(s->cfg.num_agents + 1) - 1;
        }
        if (k->active) { k->age++; k->meat = fmaxf(0, k->meat - 0.01f); }
    }
    s->tick++; s->time_of_day = fmodf(s->time_of_day + 0.001f, 1.0f); s->cloud_cover = rf();
}

typedef struct { uint8_t d[NET_SNAP_BUDGET]; size_t n; int due; } Pkt;
static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); if (++g_fail > 20) exit(1); } } while (0)

// Pool of valid packets to mutate.
#define POOL 256
static Pkt g_snap_pool[POOL], g_in_pool[POOL]; static int g_nsnap, g_nin;

typedef struct { CretSim* s; CretNet* srv; NetPeer* np; CretNet* cn; } Kept;
static uint64_t round_trip(int rounds, int na, int loss, int late_pct, int late_max, Kept* keep) {
    CretSim* s = calloc(1, sizeof *s); s->cfg = cret_default_config(); s->cfg.num_agents = na; evolve(s, true);
    CretNet* srv = netp_fake_server(0, 8192); NetPeer* np = netp_fake_peer(srv, 0, 0);
    CretNet* cn = netp_fake_client(na, 0, 2048, 4.0f); NetClient* c = cn->cl;
    Pkt down[64], up[64]; int nd = 0, nu = 0; uint64_t packets = 0, accepted = 0, refused_valid = 0;
    for (int r = 0; r < rounds; r++) {
        for (int t = 0; t < CRET_SNAPSHOT_DIV; t++) evolve(s, false);
        for (int i = 0; i < na; i++) netp_quant_agent(&srv->sc, s, i, &srv->q[i]);
        for (int k = 0; k < CRET_MAX_CARCASSES; k++) netp_quant_carcass(&srv->sc, s, k, &srv->q[NET_CARCASS0 + k]);
        Pkt p; p.n = netp_server_snapshot(srv, np, s, p.d, sizeof p.d);
        CHECK(p.n > 0 && p.n <= NET_SNAP_BUDGET, "snapshot size %zu", p.n);
        if (g_nsnap < POOL || ri(8) == 0) g_snap_pool[g_nsnap < POOL ? g_nsnap++ : ri(POOL)] = p;
        // Channel: loss, 3% duplicates, some packets late by up to late_max rounds (reorders).
        if (ri(100) >= loss && nd < 63) { p.due = r + (ri(100) < late_pct ? ri(late_max + 1) : 0); down[nd++] = p; if (ri(100) < 3 && nd < 63) { p.due = r + ri(3); down[nd++] = p; } }
        for (int k = 0; k < nd;) {
            if (down[k].due > r) { k++; continue; }
            Pkt q = down[k]; down[k] = down[--nd];
            uint32_t seq = (uint32_t)q.d[1] << 24 | (uint32_t)q.d[2] << 16 | (uint32_t)q.d[3] << 8 | q.d[4];
            bool dup = c->recv_max && (seq == c->recv_max || (seq < c->recv_max && (c->recv_max - seq > 32 || ((c->recv_mask >> (c->recv_max - seq - 1)) & 1))));
            bool ok = netp_client_packet(cn, q.d, q.n, r * 0.0667);
            packets++;
            if (!ok && !dup) refused_valid++;
            CHECK(ok || dup, "valid snapshot %u refused (round %d)", seq, r);
            if (ok) {   // every entity the server put in it decodes exactly
                accepted++;
                int h = (int)(seq % NET_HIST);
                if (np->sent_seq[h] == seq)
                    for (int e = 0; e < NET_NENT; e++)
                        if (np->sent_has[h][e]) CHECK(c->hist_seq[e][h] == seq && !memcmp(&c->hist[e][h], &np->sent[h][e], sizeof(NetEnt)), "entity %d of snapshot %u decoded wrong", e, seq);
            }
        }
        // Client tick: an input and the acks, up the same kind of channel.
        CretInput in = {.forward = rf() * 2 - 1, .strafe = rf() * 2 - 1, .turn = rf() * 2 - 1, .sprint = (uint8_t)ri(2), .jump = (uint8_t)ri(2), .action = (uint8_t)ri(CRET_ACT_REST + 1)};
        if (ri(3) == 0 && c->seq) in = c->in[c->seq % NET_IN_RING];   // repeats (run-length coded)
        for (int t = 0; t < CRET_SNAPSHOT_DIV; t++) {
            c->seq++; c->in[c->seq % NET_IN_RING] = netp_unpack_input(netp_pack_input(&in));
            Pkt u; u.n = netp_client_input(c, u.d, sizeof u.d);
            CHECK(u.n > 0, "input encode");
            if (g_nin < POOL || ri(8) == 0) g_in_pool[g_nin < POOL ? g_nin++ : ri(POOL)] = u;
            if (ri(100) >= loss && nu < 63) { u.due = r + (ri(100) < late_pct ? ri(late_max + 1) : 0); up[nu++] = u; }
        }
        for (int k = 0; k < nu;) {
            if (up[k].due > r) { k++; continue; }
            Pkt q = up[k]; up[k] = up[--nu];
            CHECK(netp_server_input(np, q.d, q.n), "valid input refused"); packets++;
        }
        // The server applies what it has, in order (as cret_net_server_poll does).
        while (np->in_seq[(np->applied + 1) % NET_IN_RING] == np->applied + 1) {
            np->applied++;
            CHECK(!memcmp(&np->in[np->applied % NET_IN_RING], &c->in[np->applied % NET_IN_RING], sizeof(CretInput)), "input %u differs", np->applied);
        }
        if (np->newest > np->applied + NET_REDUNDANCY) np->applied = np->newest - NET_REDUNDANCY;   // gap the client gave up on
    }
    printf("round trip: %d snapshots (%d agents, %d%% loss, %d%% late by up to %d), %llu accepted, %llu valid refused, %u inputs applied, %llu packets\n", rounds, na, loss, late_pct, late_max,
           (unsigned long long)accepted, (unsigned long long)refused_valid, np->applied, (unsigned long long)packets);
    if (keep) { *keep = (Kept){s, srv, np, cn}; return packets; }
    cret_net_close(srv); cret_net_close(cn); free(s);
    return packets;
}

static size_t mutate(uint8_t* d, size_t n, size_t cap) {
    int kind = ri(8);
    if (kind == 0) { n = (size_t)ri((int)cap) + 1; for (size_t i = 0; i < n; i++) d[i] = (uint8_t)r32(); if (ri(2)) d[0] = (uint8_t)(MSG_WELCOME + ri(4)); return n; }
    if (kind == 1 && n) return (size_t)ri((int)n);                                     // truncation (can be empty)
    if (kind == 2) { size_t m = n + (size_t)ri(64); if (m > cap) m = cap; for (size_t i = n; i < m; i++) d[i] = (uint8_t)r32(); return m; }
    int flips = 1 + ri(8);
    for (int f = 0; f < flips && n; f++) {
        size_t at = (size_t)ri((int)n);
        if (kind == 3) d[at] = (uint8_t)r32();
        else if (kind == 4) d[at] = ri(2) ? 0xff : 0;
        else d[at] ^= (uint8_t)(1u << ri(8));
    }
    if (kind == 7 && n > 8) { size_t a = (size_t)ri((int)n), b = (size_t)ri((int)n), len = (size_t)ri(16); if (a + len <= n && b + len <= n) memmove(d + a, d + b, len); }
    return n;
}

int main(int argc, char** argv) {
    long packets = 1000000; bool use_sim = true;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--packets") && i + 1 < argc) packets = atol(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) g_rng = (uint64_t)atoll(argv[++i]) * 0x9e3779b97f4a7c15ull + 1;
        else if (!strcmp(argv[i], "--no-sim")) use_sim = false;
        else { fprintf(stderr, "usage: netfuzz [--packets N] [--seed S] [--no-sim]\n"); return 2; }
    }
    uint64_t total = 0;
    Kept k;
    total += round_trip(6000, 64, 10, 20, 3, NULL);
    total += round_trip(6000, NA, 30, 5, 40, NULL);   // late beyond the reorder window: refused as too old, never as broken
    total += round_trip(6000, NA, 10, 20, 3, &k);
    // Mutation pool: the next snapshots, valid against the kept client's
    // baselines (delta-coded ones first, then aged-out full ones).
    g_nsnap = 0;
    for (int j = 0; j < POOL; j++) {
        for (int t = 0; t < CRET_SNAPSHOT_DIV; t++) evolve(k.s, false);
        for (int i = 0; i < NA; i++) netp_quant_agent(&k.srv->sc, k.s, i, &k.srv->q[i]);
        for (int c = 0; c < CRET_MAX_CARCASSES; c++) netp_quant_carcass(&k.srv->sc, k.s, c, &k.srv->q[NET_CARCASS0 + c]);
        Pkt* p = &g_snap_pool[g_nsnap++]; p->n = netp_server_snapshot(k.srv, k.np, k.s, p->d, sizeof p->d);
        if (j >= 24 && ri(4)) g_nsnap--;   // all early (delta-coded) ones, a sample of the rest
    }
    NetClient* fc = k.cn->cl; uint32_t rmax = fc->recv_max, rmask = fc->recv_mask;
    // A real client sim for the apply and draw paths.
    CretSim* sim = NULL; CretSim* draw = NULL;
    if (use_sim) {
        CretConfig cfg = cret_default_config(); cfg.num_agents = NA;
        sim = calloc(1, sizeof *sim); draw = calloc(1, sizeof *draw);
        if (cret_sim_init(sim, cfg) != 0) { fprintf(stderr, "netfuzz: no world, skipping the sim paths\n"); free(sim); free(draw); sim = draw = NULL; }
    }
    CretNet* cn = k.cn;
    CretNet* wn = calloc(1, sizeof *wn);   // a client still waiting for its welcome
    CretNet* srv = netp_fake_server(0, 8192); NetPeer* np = netp_fake_peer(srv, 0, 3);
    uint8_t welcome[32] = {0}; size_t wlen;
    {   // a valid welcome to mutate
        Bits b = {welcome, sizeof welcome, 0, false}; float cell = 4.0f; uint32_t cu; memcpy(&cu, &cell, 4);
        bits_put(&b, MSG_WELCOME, 8); bits_put(&b, CRET_NET_VERSION, 8); bits_put(&b, 1, 32); bits_put(&b, 2048, 32); bits_put(&b, cu, 32);
        bits_put(&b, NA, 8); bits_put(&b, 3, 8); bits_put(&b, CRET_TICK_HZ, 8); bits_put(&b, CRET_SNAPSHOT_DIV, 8); wlen = bits_bytes(&b);
    }
    uint64_t acc_snap = 0, acc_in = 0, acc_wel = 0, applied = 0;
    long fuzzed = 0;
    for (; (long)total < packets; fuzzed++) {
        uint8_t d[NET_SNAP_BUDGET + 64]; size_t n; int target = ri(10);
        if (target < 6) { const Pkt* p = &g_snap_pool[ri(g_nsnap)]; memcpy(d, p->d, p->n); n = mutate(d, p->n, sizeof d);
            fc->recv_max = rmax; fc->recv_mask = rmask;   // every pool snapshot is new to it
            if (netp_client_packet(cn, d, n, fuzzed * 1e-3)) acc_snap++; }
        else if (target < 9) { const Pkt* p = &g_in_pool[ri(g_nin)]; memcpy(d, p->d, p->n); n = mutate(d, p->n, sizeof d);
            if (netp_server_input(np, d, n)) acc_in++;
            if (ri(4) == 0) np->applied = np->newest > NET_REDUNDANCY ? np->newest - NET_REDUNDANCY : 0; }
        else { memcpy(d, welcome, wlen); n = mutate(d, wlen, sizeof d);
            if (netp_client_packet(wn, d, n, 0)) { acc_wel++; free(wn->cl); wn->cl = NULL; wn->welcomed = false; } }
        total++;
        if (sim && fuzzed % 64 == 0) {   // whatever was accepted goes through apply, reconcile and draw
            netp_client_apply(cn, sim); cret_net_client_draw(cn, sim, draw, rf(), 1.0f / 60); applied++;
            if (fuzzed % 256 == 0) { CretInput in = {.forward = 1, .turn = rf() - 0.5f}; cret_net_client_tick(cn, sim, &in); }
        }
        if (fuzzed % 200000 == 0 && fuzzed) fprintf(stderr, "netfuzz: %ld fuzzed\n", fuzzed);
    }
    printf("fuzz: %ld mutated or random datagrams (snapshots accepted %llu, inputs accepted %llu, welcomes accepted %llu), %llu apply+draw passes\n",
           fuzzed, (unsigned long long)acc_snap, (unsigned long long)acc_in, (unsigned long long)acc_wel, (unsigned long long)applied);
    printf("total %llu packets, %d failures\n", (unsigned long long)total, g_fail);
    cret_net_close(cn); cret_net_close(srv); cret_net_close(k.srv); free(k.s); free(wn);
    if (sim) { cret_sim_close(sim); free(sim); free(draw); }
    return g_fail ? 1 : 0;
}
