// Area of interest, server checks and telemetry (protocol v23,
// docs/anticheat.md). net.c is compiled as is against the in-memory ENet
// (tests/fake_enet), as in test_net_codec.c, so the tests see the bytes on
// the wire and the server's own state.
//   - the relevance rule: an animal past the sight range for its size is not
//     sent, a near one and a group mate far off are; an animal behind a
//     ridge is not, the same distance over open ground is; a diurnal viewer
//     loses at night what it saw by day; with the rule off everything goes;
//   - hysteresis: an animal walking back and forth across the edge never
//     leaves; one that walks out leaves after NET_AOI_LINGER, not before;
//     one that respawns far off leaves at once;
//   - leaves on the wire: the client drops what left, a late snapshot from
//     before the leave cannot bring it back, and it comes back from zero
//     (no snapshot refused) once it is relevant again; 127 animals leaving at
//     once still fit the snapshot budget;
//   - the checks: each finding on a crafted packet, nothing on honest ones;
//     ENFORCE refuses a second character name;
//   - telemetry: a run with a client and a logged bot written and read back,
//     every record well formed, the inputs exactly those applied.
//     tools/cret_telemetry.py reads the same file (make test runs it).
#include "test_util.h"
#include "../net.c"
#include "../progress.h"

static ENetPeer peer1 = {.id = 1};
#define MAX_SNAP 8192
typedef struct Pkt { size_t n; uint8_t d[MAX_SNAP]; } Pkt;

static CretSim* new_sim(int n, uint32_t seed) {
    CretSim* s = calloc(1, sizeof *s); CretConfig c = t_config(n); c.seed = seed; c.weather = 0; c.respawn_ticks = 60;
    if (cret_sim_init(s, c) != 0) exit(2);
    return s;
}
static CretNet* join_with(const uint8_t* welcome, size_t n) {
    fake_enet_push(ENET_EVENT_TYPE_CONNECT, &fake_enet_server_peer, NULL, 0);
    fake_enet_push(ENET_EVENT_TYPE_RECEIVE, &fake_enet_server_peer, welcome, n);
    CretNet* c = cret_net_join("127.0.0.1", CRET_DEFAULT_PORT, 0.001f);
    fake_enet_clear();
    return c;
}
static void snapshot(CretNet* srv, const CretSim* s, Pkt* p) {
    fake_enet_nsent = 0; cret_net_server_broadcast(srv, s);
    FakeSent* f = fake_enet_last(&peer1); p->n = 0;
    if (f) { p->n = f->n; memcpy(p->d, f->data, f->n); }
    fake_enet_clear();
}
static bool deliver(CretNet* cl, CretSim* cs, const uint8_t* d, size_t n) {
    fake_enet_push(ENET_EVENT_TYPE_RECEIVE, &fake_enet_server_peer, d, n);
    bool ok = cret_net_client_poll(cl, cs); fake_enet_clear(); return ok;
}
// The client's next input (with its acks) to the server, and the server's poll.
static void send_input(CretNet* cl, CretSim* cs, CretNet* srv, CretSim* s, const CretInput* in, CretInput* got) {
    fake_enet_nsent = 0; cret_net_client_tick(cl, cs, in);
    FakeSent* f = fake_enet_last(NULL); bool remote[CRET_MAX_AGENTS] = {0};
    if (f) { uint8_t raw[MAX_SNAP]; size_t n = f->n; memcpy(raw, f->data, n); fake_enet_clear(); fake_enet_push(ENET_EVENT_TYPE_RECEIVE, &peer1, raw, n); }
    cret_net_server_poll(srv, s, got, remote); fake_enet_clear();
}
// A server (slots from 1: peer1 plays slot 1), a client joined to it and its sim.
typedef struct { CretSim* s; CretNet* srv; CretNet* cl; CretSim* c; } Rig;
static Rig rig_new(int agents, bool aoi) {
    Rig r; fake_enet_clear(); fake_enet_connect_data = CRET_NET_VERSION;
    r.s = new_sim(agents, 11); r.srv = cret_net_host(CRET_DEFAULT_PORT, 1); cret_net_set_aoi(r.srv, aoi);
    bool remote[CRET_MAX_AGENTS] = {0}; CretInput in[CRET_MAX_AGENTS] = {0};
    fake_enet_push(ENET_EVENT_TYPE_CONNECT, &peer1, NULL, 0); cret_net_server_poll(r.srv, r.s, in, remote);
    FakeSent* w = fake_enet_last(&peer1); uint8_t wel[64]; size_t wn = w ? w->n : 0; if (w) memcpy(wel, w->data, wn);
    fake_enet_clear();
    r.cl = join_with(wel, wn); CretConfig cc; int slot;
    if (!r.cl || !cret_net_client_welcomed(r.cl, &cc, &slot) || slot != 1) { fprintf(T_ERR, "aoi: client not welcomed\n"); exit(2); }
    r.c = calloc(1, sizeof *r.c); if (cret_sim_init(r.c, cc) != 0) exit(2);
    for (int i = 0; i < cc.num_agents; i++) { r.c->agents[i].alive = false; cret_sim_place(r.c, i); }   // as play and netbench start a client
    r.s->time_of_day = 0.5f; r.s->cloud_cover = 0.3f;   // noon, clear
    return r;
}
static void rig_free(Rig* r) {
    cret_net_close(r->cl); cret_net_close(r->srv);
    cret_sim_close(r->c); free(r->c); cret_sim_close(r->s); free(r->s); fake_enet_clear();
}
// One round: the client's input, two server ticks (nothing simulated: the
// test places every animal itself), a snapshot delivered.
static Pkt g_last;
static void rig_round(Rig* r) {
    CretInput idle = {0}, got[CRET_MAX_AGENTS] = {{0}};
    send_input(r->cl, r->c, r->srv, r->s, &idle, got);
    r->s->tick += CRET_SNAPSHOT_DIV;
    snapshot(r->srv, r->s, &g_last); deliver(r->cl, r->c, g_last.d, g_last.n);
}
// Puts agent i standing still, silent, at (x, z) on the ground.
static void put(CretSim* s, int i, float x, float z) {
    CretAgent* a = &s->agents[i];
    a->x = x; a->z = z; a->y = cret_height(s->world, x, z); a->vx = a->vy = a->vz = a->speed = 0; a->act = CRET_ACT_NONE; a->alive = true;
    cret_sim_place(s, i);
}
// The rule's sight range for target e seen by viewer i, as netaoi.c works it out.
static float sight_of(const CretSim* s, int i, int e) {
    const CretAgent* b = &s->agents[e];
    float L = CRET_SPECIES_TABLE[b->species].length * cret_growth_scale(b->growth);
    return fminf(NET_AOI_SIGHT_MIN + NET_AOI_SIGHT_PER_L * L, NET_AOI_SIGHT_MAX) * aoi_sight_scale(s, &s->agents[i]);
}
// A heading from agent i along which the ground leaves the sight line clear out to `far` m.
static bool open_heading(const CretSim* s, int i, float far, float* hx, float* hz) {
    const CretAgent* a = &s->agents[i];
    for (int k = 0; k < 64; k++) {
        float ang = k * 0.0981748f, cx = cosf(ang), cz = sinf(ang); bool ok = true;
        for (float d = 20; d <= far && ok; d += 10) {
            float x = a->x + cx * d, z = a->z + cz * d;
            if (x < 40 || z < 40 || x > s->world->size - 40 || z > s->world->size - 40) { ok = false; break; }
            ok = aoi_clear(s->world, a->x, aoi_eye_y(a), a->z, x, cret_height(s->world, x, z) + 0.5f, z, 0);
        }
        if (ok) { *hx = cx; *hz = cz; return true; }
    }
    return false;
}

// Puts viewer i somewhere on dry ground with an open heading out to `far` m (false: none in the test world).
static bool open_place(CretSim* s, int i, float far, float* hx, float* hz) {
    uint32_t rng = 77; float W = s->world->size;
    for (int t = 0; t < 400; t++) {
        float x = 60 + (W - 120) * t_frand(&rng), z = 60 + (W - 120) * t_frand(&rng);
        if (cret_water(s->world, x, z) > cret_height(s->world, x, z) - 0.2f) continue;
        put(s, i, x, z);
        if (open_heading(s, i, far, hx, hz)) return true;
    }
    return false;
}

static void test_rule(void) {
    Rig r = rig_new(8, true); CretSim* s = r.s;
    // The viewer is the only stegosaur and eats no meat (no smell); the others are gallimimus and pachycephalosaurs.
    cret_sim_set_species(s, 1, CRET_SP_STEGO);
    for (int i = 0; i < 8; i++) if (i != 1) cret_sim_set_species(s, i, i % 2 ? CRET_SP_GALLI : CRET_SP_PACHY);
    for (int i = 0; i < 8; i++) s->agents[i].group = 0;
    float W = s->world->size;
    float hx, hz; float S2 = sight_of(s, 1, 2);
    bool open = open_place(s, 1, S2 * 1.5f, &hx, &hz);
    CHECK(open, "aoi: no place in the test world with open ground %.0f m out", S2 * 1.5f); if (!open) { rig_free(&r); return; }
    float cx = s->agents[1].x, cz = s->agents[1].z;
    for (int i = 0; i < 8; i++) if (i != 1) put(s, i, cx - hz * (300 + 40 * i), cz + hx * (300 + 40 * i));   // parked off to the side
    // 2: past its sight range (and the keep margin) along the open heading; 3: near; 4: a group mate as far.
    put(s, 2, cx + hx * (S2 * NET_AOI_KEEP + NET_AOI_KEEP_M + 25), cz + hz * (S2 * NET_AOI_KEEP + NET_AOI_KEEP_M + 25));
    put(s, 3, cx + hx * 30, cz + hz * 30);
    put(s, 4, cx + hx * (S2 * 1.4f), cz + hz * (S2 * 1.4f)); s->agents[4].group = 2; s->agents[1].group = 2;
    for (int k = 0; k < 20; k++) rig_round(&r);
    const NetAoi* A = &r.srv->aoi[1];
    CHECK(!A->in[2] && !r.c->agents[2].alive, "aoi: an animal %.0f m off, past its %.0f m sight range, was sent", S2 * NET_AOI_KEEP + NET_AOI_KEEP_M + 25, S2);
    CHECK(A->in[3] && r.c->agents[3].alive && fabsf(r.c->agents[3].x - s->agents[3].x) < 0.01f, "aoi: an animal 30 m off was not sent");
    CHECK(A->in[4] && r.c->agents[4].alive && fabsf(r.c->agents[4].x - s->agents[4].x) < 0.01f, "aoi: a group mate %.0f m off was not sent", S2 * 1.4f);
    // Inside the sight range over open ground it is sent.
    put(s, 2, cx + hx * S2 * 0.8f, cz + hz * S2 * 0.8f);
    for (int k = 0; k < 3; k++) rig_round(&r);
    CHECK(A->in[2] && r.c->agents[2].alive, "aoi: an animal at 0.8 of its sight range over open ground was not sent");
    // Night: a diurnal viewer sees half as far (plus nothing: it stands still and silent).
    s->time_of_day = 0.0f;
    for (int k = 0; k < 2 * CRET_TICK_HZ / CRET_SNAPSHOT_DIV; k++) rig_round(&r);
    CHECK(!A->in[2] && !r.c->agents[2].alive, "aoi: at night a stegosaur still sees a gallimimus at 0.8 of its daylight range");
    s->time_of_day = 0.5f;
    for (int k = 0; k < 3; k++) rig_round(&r);
    CHECK(A->in[2] && r.c->agents[2].alive, "aoi: back at noon the gallimimus did not come back");
    // Behind a ridge: a viewer place and a target within sight with the
    // ground well over the line from the highest camera, against the same
    // distance over open ground.
    int found = 0;
    uint32_t rng = 5;
    for (int t = 0; t < 4000 && !found; t++) {
        float vx = 100 + (W - 200) * t_frand(&rng), vz = 100 + (W - 200) * t_frand(&rng), ang = 6.2831853f * t_frand(&rng);
        put(s, 1, vx, vz);
        float d = NET_AOI_NEAR * 2 + (sight_of(s, 1, 2) * 0.8f - NET_AOI_NEAR * 2) * t_frand(&rng);
        float tx = vx + cosf(ang) * d, tz = vz + sinf(ang) * d;
        if (tx < 60 || tz < 60 || tx > W - 60 || tz > W - 60 || cret_water(s->world, tx, tz) > cret_height(s->world, tx, tz)) continue;
        put(s, 2, tx, tz);
        const CretAgent* a = &s->agents[1]; const CretAgent* b = &s->agents[2];
        if (aoi_clear(s->world, a->x, aoi_eye_y(a), a->z, b->x, b->y + b->height + 1, b->z, 3 * NET_AOI_LOS_KEEP)) continue;
        found = 1;
        bool seen, far, in = netp_aoi_judge(s, 1, 2, false, &seen, &far);
        CHECK(!in && !seen, "aoi: an animal %.0f m off behind a ridge was judged visible", d);
        float ox, oz;
        if (open_heading(s, 1, d + 20, &ox, &oz)) {
            put(s, 2, vx + ox * d, vz + oz * d);
            in = netp_aoi_judge(s, 1, 2, false, &seen, &far);
            CHECK(in && seen, "aoi: the same animal %.0f m off over open ground was not visible", d);
        }
    }
    CHECK(found, "aoi: no place in the test world with a ridge between two points");
    rig_free(&r);
    // With the rule off the far animal is sent, as before v23.
    Rig q = rig_new(8, false); s = q.s;
    put(s, 1, 100, 100); put(s, 2, W - 100, W - 100);   // across the island
    for (int k = 0; k < 40; k++) rig_round(&q);
    CHECK(q.c->agents[2].alive && fabsf(q.c->agents[2].x - s->agents[2].x) < 0.01f, "aoi off: a far animal was not sent");
    rig_free(&q);
}

static void test_hysteresis(void) {
    Rig r = rig_new(8, true); CretSim* s = r.s;
    cret_sim_set_species(s, 1, CRET_SP_STEGO);
    for (int i = 0; i < 8; i++) { if (i != 1) cret_sim_set_species(s, i, CRET_SP_GALLI); s->agents[i].group = 0; }
    float hx, hz, S = sight_of(s, 1, 2);
    if (!open_place(s, 1, S * NET_AOI_KEEP + NET_AOI_KEEP_M + 40, &hx, &hz)) { CHECK(0, "hysteresis: no open ground %.0f m out", S * 1.4f); rig_free(&r); return; }
    float cx = s->agents[1].x, cz = s->agents[1].z;
    for (int i = 0; i < 8; i++) if (i != 1) put(s, i, cx - hz * (300 + 40 * i), cz + hx * (300 + 40 * i));
    // Walking back and forth 5 m either side of the edge, at a walk: once in, it never leaves.
    const NetAoi* A = &r.srv->aoi[1]; int flips = 0, was = -1; uint32_t left0 = r.cl->cl->left_seq[2];
    for (int k = 0; k < 300; k++) {
        float d = S + 5 * sinf(k * 0.07f);
        put(s, 2, cx + hx * d, cz + hz * d); s->agents[2].speed = 1.0f;   // slow: heard only within a few metres
        rig_round(&r);
        if (was >= 0 && A->in[2] != was) flips++;
        was = A->in[2];
    }
    CHECK(flips <= 1 && A->in[2], "hysteresis: an animal pacing 5 m about the edge flipped %d times", flips);
    CHECK(r.cl->cl->left_seq[2] == left0 && r.c->agents[2].alive, "hysteresis: the client was told to drop an animal pacing at the edge");
    // Walking out past the keep margin: still there before NET_AOI_LINGER, gone after.
    float out = S * NET_AOI_KEEP + NET_AOI_KEEP_M + 30;
    put(s, 2, cx + hx * out, cz + hz * out);
    int linger = (int)(NET_AOI_LINGER * CRET_TICK_HZ) / CRET_SNAPSHOT_DIV;
    for (int k = 0; k < linger - 2; k++) rig_round(&r);
    CHECK(A->in[2] && r.c->agents[2].alive, "hysteresis: the animal left before %.1f s outside", NET_AOI_LINGER);
    for (int k = 0; k < 4; k++) rig_round(&r);
    CHECK(!A->in[2] && !r.c->agents[2].alive && r.cl->cl->left_seq[2] != left0, "hysteresis: the animal did not leave %.1f s after walking out", NET_AOI_LINGER);
    // Back in, then a respawn far away: gone with the next snapshot, never shown at the new place.
    put(s, 2, cx + hx * 30, cz + hz * 30);
    for (int k = 0; k < 4; k++) rig_round(&r);
    CHECK(r.c->agents[2].alive, "hysteresis: the animal did not come back");
    uint64_t dropped0 = r.cl->cl->st.snapshots_dropped;
    float fx = cx - hx * 3 * S, fz = cz - hz * 3 * S;
    put(s, 2, fx, fz); rig_round(&r);
    CHECK(!A->in[2] && !r.c->agents[2].alive, "hysteresis: an animal that moved %.0f m away was still sent", 3 * S);
    CHECK(fabsf(r.cl->cl->latest[2].f[AF_X] / NET_POS_Q - fx) > 100, "hysteresis: the client learned where the animal went");
    // Late snapshots: one from before the leave arrives after it and must not bring the animal back.
    put(s, 2, cx + hx * 30, cz + hz * 30);
    for (int k = 0; k < 6; k++) rig_round(&r);
    Pkt p1 = g_last; s->tick += CRET_SNAPSHOT_DIV; put(s, 2, cx + hx * 32, cz + hz * 32);
    Pkt p2; snapshot(r.srv, s, &p2);
    s->tick += CRET_SNAPSHOT_DIV; put(s, 2, fx, fz);
    Pkt p3; snapshot(r.srv, s, &p3);
    (void)p1;
    deliver(r.cl, r.c, p3.d, p3.n);
    CHECK(!r.c->agents[2].alive, "late: the leave was not applied");
    deliver(r.cl, r.c, p2.d, p2.n);
    CHECK(!r.c->agents[2].alive && r.cl->cl->nsamp[2] == 0, "late: a snapshot from before the leave brought the animal back");
    // It comes back from zero once relevant again, with no snapshot refused.
    put(s, 2, cx + hx * 25, cz + hz * 25);
    for (int k = 0; k < 6; k++) rig_round(&r);
    CHECK(r.c->agents[2].alive && fabsf(r.c->agents[2].x - s->agents[2].x) < 0.01f, "re-entry: the animal did not come back where it is");
    CHECK(r.cl->cl->st.snapshots_dropped == dropped0, "re-entry: %llu snapshots refused", (unsigned long long)(r.cl->cl->st.snapshots_dropped - dropped0));
    CHECK(r.srv->sst.leaves > 0, "leaves: none counted");
    rig_free(&r);
}

// 127 animals around the client's, then the client's animal far away: they
// all leave (most at once, those that see far after the linger), and every
// snapshot still fits its budget and goes out.
static void test_leave_burst(void) {
    Rig r = rig_new(128, true); CretSim* s = r.s; float W = s->world->size;
    for (int i = 0; i < 128; i++) { s->agents[i].group = 0; put(s, i, W * 0.5f + 30 * cosf(i * 0.37f), W * 0.5f + 30 * sinf(i * 0.37f)); }
    for (int k = 0; k < 6; k++) rig_round(&r);
    int held = 0; for (int i = 0; i < 128; i++) held += i != 1 && r.c->agents[i].alive;
    CHECK(held == 127, "burst: the client holds %d of 127 animals around it", held);
    uint64_t dropped0 = r.cl->cl->st.snapshots_dropped; int empty = 0;
    put(s, 1, 80, 80);
    int rounds = (int)(NET_AOI_LINGER * CRET_TICK_HZ) / CRET_SNAPSHOT_DIV + 4;   // the farthest-seeing (a tyrannosaur) may linger
    for (int k = 0; k < rounds; k++) { rig_round(&r); empty += g_last.n == 0; CHECK(g_last.n <= NET_SNAP_BUDGET, "burst: a %zu-byte snapshot", g_last.n); }
    held = 0; for (int i = 0; i < 128; i++) held += i != 1 && r.c->agents[i].alive;
    CHECK(empty == 0 && held == 0 && r.cl->cl->st.snapshots_dropped == dropped0, "burst: %d snapshots not sent, %d animals still held, %llu refused", empty, held,
          (unsigned long long)(r.cl->cl->st.snapshots_dropped - dropped0));
    rig_free(&r);
}

// A crafted input packet: ack, newest seq and one input of the given bytes.
static size_t craft(uint8_t* out, uint32_t ack, uint32_t newest, uint64_t raw) {
    Bits b = {out, 64, 0, false};
    bits_put(&b, MSG_INPUT, 8); bits_put(&b, ack, 32); bits_put(&b, 0, 32); bits_put(&b, newest, 32); bits_put(&b, 0, 5);
    put_input(&b, raw);
    CretInput in = netp_unpack_input(raw); if (net_press(&in)) bits_put(&b, 0, 1);   // a press carries a view bit (none here)
    return bits_bytes(&b);
}
static void test_checks(void) {
    CretSim* s = new_sim(4, 3); CretNet* srv = netp_fake_server(1, s->world->size); NetPeer* np = netp_fake_peer(srv, 0, 1);
    cret_net_set_checks(srv, CRET_AC_LOG); s->tick = 0; np->join_tick = np->win_tick = 0;
    CretInput honest = {.forward = -1, .strafe = 1, .turn = -1, .aim = -1, .action = CRET_ACT_ATTACK, .ability = 3, .sprint = 1, .jump = 1, .carry = 1};
    uint8_t d[64]; size_t n;
    // Honest packets, one a tick for 10 s, extreme values included: nothing.
    for (int t = 1; t <= 300; t++) {
        s->tick = t; n = craft(d, 0, (uint32_t)t, netp_pack_input(&honest));
        CHECK(netp_server_input(np, d, n), "checks: an honest input was refused");
        netp_check_packet(srv, np, s);
    }
    uint32_t total = 0; for (int k = 0; k < CRET_AC_KINDS; k++) total += np->ac_count[k];
    CHECK(total == 0, "checks: %u findings on honest packets", total);
    // A movement byte of -128, an ack of a snapshot never sent.
    n = craft(d, 0, 301, (netp_pack_input(&honest) & ~(0xffull << 32)) | 0x80ull << 32); netp_server_input(np, d, n); netp_check_packet(srv, np, s);
    CHECK(np->ac_count[CRET_AC_BAD_BYTE] == 1, "checks: a forward byte of -128 was not found");
    n = craft(d, 500, 302, netp_pack_input(&honest)); netp_server_input(np, d, n); netp_check_packet(srv, np, s);
    CHECK(np->ac_count[CRET_AC_ACK_FUTURE] == 1, "checks: an ack of snapshot 500 (none sent) was not found");
    // Inputs numbered past real time: 400 ticks' worth after 300.
    n = craft(d, 0, 400, netp_pack_input(&honest)); netp_server_input(np, d, n); netp_check_packet(srv, np, s);
    CHECK(!netp_check_clock(srv, np, s, 400) && np->ac_count[CRET_AC_INPUT_CLOCK] == 1, "checks: a client 100 ticks ahead of real time was not found");
    srv->checks = CRET_AC_ENFORCE;
    CHECK(netp_check_clock(srv, np, s, 360), "checks: ENFORCE let an input run ahead of real time");
    CHECK(!netp_check_clock(srv, np, s, 300), "checks: ENFORCE refused an input on time");
    srv->checks = CRET_AC_LOG;
    // A flood: three packets a tick for a window.
    for (int t = 301; t <= 301 + NET_AC_FLOOD_S * CRET_TICK_HZ; t++) {
        s->tick = t;
        for (int k = 0; k < 3; k++) { n = craft(d, 0, (uint32_t)t, netp_pack_input(&honest)); netp_server_input(np, d, n); netp_check_packet(srv, np, s); }
    }
    CHECK(np->ac_count[CRET_AC_FLOOD] >= 1, "checks: three input packets a tick for %d s were not found", NET_AC_FLOOD_S);
    // Views: in the future, and far behind the newest snapshot the client had.
    int h = 77 % NET_IN_RING; np->in[h] = honest; np->in_seq[h] = 77; np->in_known[h] = (uint32_t)s->tick;
    np->in_view[h] = s->tick + 10; netp_take_view(srv, np, s, 77);
    CHECK(np->ac_count[CRET_AC_VIEW_AHEAD] == 1, "checks: a view 10 ticks in the future was not found");
    np->in_view[h] = s->tick - 100; netp_take_view(srv, np, s, 77);
    CHECK(np->ac_count[CRET_AC_VIEW_BEHIND] == 1, "checks: a view 100 ticks behind was not found");
    np->in_view[h] = s->tick - 5; netp_take_view(srv, np, s, 77);
    CHECK(np->ac_count[CRET_AC_VIEW_AHEAD] == 1 && np->ac_count[CRET_AC_VIEW_BEHIND] == 1, "checks: an ordinary view was flagged");
    // A second character name: counted; ENFORCE keeps the first.
    uint8_t pr[32]; Bits b = {pr, sizeof pr, 0, false};
    bits_put(&b, MSG_PROFILE, 8); bits_put(&b, 3, 5); for (int c = 0; c < 3; c++) bits_put(&b, (uint8_t)"Ann"[c], 8);
    size_t pn = bits_bytes(&b);
    netp_check_profile(srv, np, s, pr, pn);
    CHECK(np->ac_count[CRET_AC_RENAME] == 0 && !strcmp(s->prog[1].name, "Ann"), "checks: the first name was refused or flagged");
    srv->checks = CRET_AC_ENFORCE; s->prog[1].name[0] = 'B';
    netp_check_profile(srv, np, s, pr, pn);
    CHECK(np->ac_count[CRET_AC_RENAME] == 1 && s->prog[1].name[0] == 'B', "checks: ENFORCE took a second character name");
    // Off: nothing counted.
    cret_net_set_checks(srv, CRET_AC_OFF); uint32_t before = np->ac_count[CRET_AC_BAD_BYTE];
    n = craft(d, 0, 2000, (netp_pack_input(&honest) & ~(0xffull << 32)) | 0x80ull << 32); netp_server_input(np, d, n); netp_check_packet(srv, np, s);
    CHECK(np->ac_count[CRET_AC_BAD_BYTE] == before, "checks: counted while off");
    cret_net_close(srv); cret_sim_close(s); free(s);
}

// ------------------------------------------------------------------ telemetry
static uint32_t rd16(const uint8_t* p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t* p) { return rd16(p) | rd16(p + 2) << 16; }
static float rdf(const uint8_t* p) { uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }
#define TL_PATH "build/cretaceous/test/telemetry_test.ctl"
#define TL_TICKS 240
static void test_telemetry(void) {
    Rig r = rig_new(8, true); CretSim* s = r.s;
    CHECK(cret_net_telemetry_open(r.srv, TL_PATH, s), "telemetry: cannot open %s", TL_PATH);
    cret_net_telemetry_slot(r.srv, 3, true);   // a bot, in the same records
    static uint64_t sent[NET_IN_RING * 4]; static uint64_t bot[TL_TICKS + 8]; int t0 = s->tick, snaps = 0;
    uint32_t rng = 9;
    for (int t = 0; t < TL_TICKS; t++) {
        CretInput in = {.forward = t_frand(&rng) * 2 - 1, .turn = t_frand(&rng) * 2 - 1, .aim = t_frand(&rng) * 2 - 1,
                        .action = t % 23 == 0 ? CRET_ACT_ATTACK : 0, .sprint = t % 7 == 0};
        fake_enet_nsent = 0; cret_net_client_tick(r.cl, r.c, &in);
        sent[r.cl->cl->seq % (NET_IN_RING * 4)] = netp_pack_input(&in);
        FakeSent* f = fake_enet_last(NULL);
        if (f) { uint8_t raw[MAX_SNAP]; size_t n = f->n; memcpy(raw, f->data, n); fake_enet_clear(); fake_enet_push(ENET_EVENT_TYPE_RECEIVE, &peer1, raw, n); }
        CretInput inputs[CRET_MAX_AGENTS]; bool remote[CRET_MAX_AGENTS] = {0};
        for (int i = 0; i < s->cfg.num_agents; i++) inputs[i] = cret_bot_input(s, i);
        bot[t] = netp_pack_input(&inputs[3]);
        cret_net_server_poll(r.srv, s, inputs, remote); fake_enet_clear();
        cret_sim_step(s, inputs);
        if (cret_net_snapshot_due(r.srv, s->tick)) { snapshot(r.srv, s, &g_last); deliver(r.cl, r.c, g_last.d, g_last.n); snaps++; }
    }
    cret_net_telemetry_close(r.srv);
    FILE* f = fopen(TL_PATH, "rb"); CHECK(f, "telemetry: %s not written", TL_PATH); if (!f) { rig_free(&r); return; }
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* d = malloc((size_t)len); size_t got = fread(d, 1, (size_t)len, f); fclose(f);
    CHECK(got == (size_t)len && len >= 32 && !memcmp(d, NET_TL_MAGIC, 8), "telemetry: bad header");
    CHECK(rd16(d + 8) == NET_TL_FORMAT && rd16(d + 10) == CRET_NET_VERSION && rd16(d + 12) == CRET_TICK_HZ && rd16(d + 16) == 8 && rd16(d + 18) == NET_NENT,
          "telemetry: header fields");
    static const int want_len[] = {[NET_TL_JOIN] = 6, [NET_TL_LEAVE] = 4, [NET_TL_INPUT] = 31, [NET_TL_VIS] = 2 * NET_TL_MASK, [NET_TL_APPEAR] = 10,
                                   [NET_TL_SWING] = 14, [NET_TL_ACK] = 16, [NET_TL_FLAG] = 9};
    int count[16] = {0}, bad = 0, joins_human = 0, joins_bot = 0; uint32_t last_seq = 0; int in_ok = 0, in_bad = 0, bot_ok = 0, bot_bad = 0;
    for (size_t at = 32; at < (size_t)len;) {
        if (at + 8 > (size_t)len) { bad++; break; }
        int type = d[at], slot = d[at + 1]; uint32_t n = rd16(d + at + 2), tick = rd32(d + at + 4); const uint8_t* p = d + at + 8;
        if (at + 8 + n > (size_t)len || type < NET_TL_JOIN || type > NET_TL_FLAG) { bad++; break; }
        if (type == NET_TL_WORLD ? n != 2 + 8 * rd16(p) : (int)n != want_len[type]) bad++;
        count[type]++;
        if (type == NET_TL_JOIN) { joins_human += slot == 1 && p[4] == 1; joins_bot += slot == 3 && p[4] == 0; }
        if (type == NET_TL_INPUT && slot == 1 && p[4] != 2) {   // the client's inputs, applied in order, as sent
            uint32_t seq = rd32(p); uint64_t v = 0; for (int k = 0; k < 5; k++) v = v << 8 | p[5 + k];
            if (seq == last_seq + 1 && v == sent[seq % (NET_IN_RING * 4)]) in_ok++; else in_bad++;
            last_seq = seq;
        }
        if (type == NET_TL_INPUT && slot == 3) {   // the bot's inputs, as passed to the poll
            uint64_t v = 0; for (int k = 0; k < 5; k++) v = v << 8 | p[5 + k];
            int t = (int)tick - t0; if (t >= 0 && t < TL_TICKS && v == bot[t] && rd32(p) == tick) bot_ok++; else bot_bad++;
        }
        if (type == NET_TL_INPUT && !isfinite(rdf(p + 15))) bad++;
        at += 8 + n;
    }
    CHECK(bad == 0, "telemetry: %d malformed records", bad);
    CHECK(joins_human == 1 && joins_bot == 1, "telemetry: joins (client %d, bot %d)", joins_human, joins_bot);
    CHECK(in_ok > TL_TICKS * 3 / 4 && in_bad == 0, "telemetry: client inputs %d as sent, %d not", in_ok, in_bad);
    CHECK(bot_ok == TL_TICKS && bot_bad == 0, "telemetry: bot inputs %d as passed, %d not", bot_ok, bot_bad);
    CHECK(count[NET_TL_WORLD] == snaps && count[NET_TL_VIS] == 2 * snaps, "telemetry: %d world and %d vis records for %d snapshots", count[NET_TL_WORLD], count[NET_TL_VIS], snaps);
    CHECK(count[NET_TL_ACK] > TL_TICKS / 2 && count[NET_TL_APPEAR] > 0, "telemetry: %d acks, %d appearances", count[NET_TL_ACK], count[NET_TL_APPEAR]);
    fprintf(T_ERR, "telemetry: %ld bytes for %d ticks (%d inputs, %d vis, %d world, %d appear, %d swings, %d acks)\n", len, TL_TICKS,
            count[NET_TL_INPUT], count[NET_TL_VIS], count[NET_TL_WORLD], count[NET_TL_APPEAR], count[NET_TL_SWING], count[NET_TL_ACK]);
    free(d);
    rig_free(&r);
}

int main(void) {
    double t0 = t_now();
    t_errf = stderr;
    if (!getenv("CRET_NET_VERBOSE")) { FILE* nul = fopen("/dev/null", "w"); if (nul) stderr = nul; }
    unsetenv("CRET_NETSIM");
    t_hold_world();
    test_rule();
    test_hysteresis();
    test_leave_burst();
    test_checks();
    test_telemetry();
    return t_finish("test_net_aoi", t0);
}
