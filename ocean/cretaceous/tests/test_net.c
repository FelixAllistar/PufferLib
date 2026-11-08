// Net codec tests. net.c is compiled as is against an in-memory ENet
// (tests/fake_enet), so the tests see the exact bytes on the wire.
//   - welcome, input, respawn request and snapshot round trips (every field,
//     with its quantisation) over a 3000-tick bot run;
//   - a truncated snapshot changes nothing on the client (all or nothing);
//   - fuzzing (--fuzz N): truncated, bit-flipped, spliced and random packets
//     into the client, the server and the join handshake. Build with
//     sanitizers for this (make fuzz / make asan).
#include "test_util.h"
#include "../net.c"

static ENetPeer peer1 = {.id = 1}, peer2 = {.id = 2};
#define MAX_SNAP 8192
typedef struct Pkt { size_t n; uint8_t d[MAX_SNAP]; } Pkt;

static CretSim* new_sim(int n, uint32_t seed) {
    CretSim* s = calloc(1, sizeof *s); CretConfig c = t_config(n); c.seed = seed; c.weather = 1; c.respawn_ticks = 60;
    if (cret_sim_init(s, c) != 0) exit(2);
    return s;
}
static void step_bots(CretSim* s, int ticks) {
    CretInput in[CRET_MAX_AGENTS];
    for (int t = 0; t < ticks; t++) { for (int i = 0; i < s->cfg.num_agents; i++) in[i] = cret_bot_input(s, i); cret_sim_step(s, in); }
}
// Client handle with a fake connection: CONNECT, then the server's welcome bytes.
static CretNet* join_with(const uint8_t* welcome, size_t n) {
    fake_enet_push(ENET_EVENT_TYPE_CONNECT, &fake_enet_server_peer, NULL, 0);
    fake_enet_push(ENET_EVENT_TYPE_RECEIVE, &fake_enet_server_peer, welcome, n);
    CretNet* c = cret_net_join("127.0.0.1", CRET_DEFAULT_PORT, 0.1f);
    fake_enet_clear();
    return c;
}
static void snapshot(CretNet* srv, const CretSim* s, Pkt* p) {
    fake_enet_nsent = 0; cret_net_server_broadcast(srv, s);
    FakeSent* f = fake_enet_last(&peer1); p->n = 0;
    if (f) { p->n = f->n; memcpy(p->d, f->data, f->n); }
}
static bool deliver_snapshot(CretNet* cl, CretSim* cs, const uint8_t* d, size_t n) {
    fake_enet_push(ENET_EVENT_TYPE_RECEIVE, &fake_enet_server_peer, d, n);
    bool ok = cret_net_client_poll(cl, cs); fake_enet_clear(); return ok;
}

// ------------------------------------------------------------------ round trips
static float q8err(void) { return 0.5f / 255.0f + 1e-6f; }

static void compare_snapshot(const CretSim* s, const CretSim* c, int* bad) {
    if (c->tick != s->tick || c->time_of_day != s->time_of_day || fabsf(c->cloud_cover - s->cloud_cover) > q8err()) {
        (*bad)++; CHECK_CAPPED(0, 3, "snapshot header: tick %d/%d tod %g/%g cloud %g/%g", c->tick, s->tick, c->time_of_day, s->time_of_day, c->cloud_cover, s->cloud_cover);
    }
    for (int i = 0; i < s->cfg.num_agents; i++) {
        const CretAgent *a = &s->agents[i], *b = &c->agents[i];
        float hf = a->max_health > 0 ? a->health / a->max_health : 0, hb = b->max_health > 0 ? b->health / b->max_health : 0;
        int rally = a->rally_ticks > 0;
        bool ok = b->alive == a->alive && b->species == a->species && b->act == a->act && b->on_ground == a->on_ground && b->swimming == a->swimming &&
                  b->sprinting == a->sprinting && b->x == a->x && b->y == a->y && b->z == a->z && b->yaw == a->yaw && b->speed == a->speed &&
                  fabsf(hb - hf) <= q8err() * 1.01f && fabsf(b->growth - a->growth) <= q8err() && fabsf(b->food - a->food) <= q8err() &&
                  fabsf(b->water - a->water) <= q8err() && fabsf(b->stamina - a->stamina) <= q8err() &&
                  b->hurt_ticks == (a->hurt_ticks > 255 ? 255 : a->hurt_ticks) && b->dead_ticks == (a->dead_ticks > 65535 ? 65535 : a->dead_ticks) &&
                  (b->bleed > 0) == (a->bleed > 0.002f * a->max_health) && (b->broken > 0) == (a->broken > 0) && b->group == a->group &&
                  b->rally_ticks == rally && (!rally || (b->rally_x == a->rally_x && b->rally_z == a->rally_z));
        if (!ok) { (*bad)++; CHECK_CAPPED(0, 5, "tick %d agent %d: snapshot round trip differs", s->tick, i); }
    }
    for (int k = 0; k < CRET_MAX_CARCASSES; k++) {
        const CretCarcass *a = &s->carcasses[k], *b = &c->carcasses[k];
        if (a->active != b->active) { (*bad)++; CHECK_CAPPED(0, 3, "tick %d carcass %d: active %d/%d", s->tick, k, a->active, b->active); continue; }
        if (!a->active) continue;
        float mf = a->max_meat > 0 ? a->meat / a->max_meat : 0;
        bool ok = b->species == a->species && fabsf(b->growth - a->growth) <= q8err() && fabsf(b->meat / b->max_meat - mf) <= q8err() &&
                  b->age == (a->age > 65535 ? 65535 : a->age) && b->agent == (a->agent < 0 ? -1 : a->agent) &&
                  b->x == a->x && b->y == a->y && b->z == a->z && b->yaw == a->yaw;
        if (!ok) { (*bad)++; CHECK_CAPPED(0, 3, "tick %d carcass %d: round trip differs", s->tick, k); }
    }
}

static Pkt g_snaps[64]; static int g_nsnaps;
static uint8_t g_welcome[64]; static size_t g_welcome_n;
static uint8_t g_input[16]; static size_t g_input_n;

static void test_round_trips(void) {
    fake_enet_clear();
    CretSim* s = new_sim(32, 4);
    CretNet* srv = cret_net_host(CRET_DEFAULT_PORT, 1);
    CHECK(srv != NULL, "host failed"); if (!srv) return;
    // A peer connects: it takes slot 1, which becomes a person and respawns.
    bool remote[CRET_MAX_AGENTS] = {0}; CretInput in[CRET_MAX_AGENTS] = {0};
    fake_enet_push(ENET_EVENT_TYPE_CONNECT, &peer1, NULL, 0);
    cret_net_server_poll(srv, s, in, remote);
    FakeSent* w = fake_enet_last(&peer1);
    CHECK(w && w->reliable && remote[1] && s->human[1] && !remote[2], "connect: welcome %p, remote %d, human %d", (void*)w, remote[1], s->human[1]);
    if (!w) return;
    g_welcome_n = w->n; memcpy(g_welcome, w->data, w->n);
    // Welcome round trip.
    CretNet* cl = join_with(g_welcome, g_welcome_n);
    CretConfig cc; int slot = -1;
    CHECK(cl && cret_net_client_welcomed(cl, &cc, &slot), "client not welcomed");
    if (!cl) return;
    CHECK(slot == 1 && cc.world_seed == s->cfg.world_seed && cc.world_n == s->cfg.world_n && cc.world_cell == s->cfg.world_cell && cc.num_agents == s->cfg.num_agents,
          "welcome: slot %d seed %u n %d cell %g agents %d", slot, cc.world_seed, cc.world_n, cc.world_cell, cc.num_agents);
    CretSim* c = calloc(1, sizeof *c); if (cret_sim_init(c, cc) != 0) exit(2);

    // Snapshots over a bot run, with deaths, carcasses and rallies forced in.
    int bad = 0, rallies = 0, carc = 0, dead = 0;
    for (int t = 0; t < 3000; t += 50) {
        step_bots(s, 50);
        if (t == 500) for (int i = 4; i < 10; i++) { s->agents[i].health = 0.001f; s->agents[i].bleed = 1e4f; }   // carcasses
        if (t == 900) { s->agents[2].rally_ticks = 100; s->agents[2].rally_x = 123.5f; s->agents[2].rally_z = -7.25f; s->agents[3].hurt_ticks = 400; }
        Pkt p; snapshot(srv, s, &p);
        CHECK_CAPPED(p.n > 0, 3, "no snapshot sent");
        if (g_nsnaps < 64) g_snaps[g_nsnaps++] = p;
        CHECK_CAPPED(deliver_snapshot(cl, c, p.d, p.n), 3, "client disconnected");
        compare_snapshot(s, c, &bad);
        for (int i = 0; i < s->cfg.num_agents; i++) { rallies += s->agents[i].rally_ticks > 0; dead += !s->agents[i].alive; }
        for (int k = 0; k < CRET_MAX_CARCASSES; k++) carc += s->carcasses[k].active;
    }
    fprintf(T_ERR, "snapshots: %d round trips (%zu..%zu bytes), %d differed; covered %d rallies, %d dead agents, %d carcasses\n",
            g_nsnaps, g_snaps[0].n, g_snaps[g_nsnaps - 1].n, bad, rallies, dead, carc);
    CHECK(rallies > 0 && carc > 0 && dead > 0, "round trips missed rallies, carcasses or deaths");

    // Truncated snapshots are dropped whole: the client state does not change.
    uint64_t h0 = t_sim_hash(c); int changed = 0;
    for (int q = 0; q < g_nsnaps; q += 7) {
        Pkt* p = &g_snaps[q];
        for (size_t cut = 0; cut < p->n; cut += 1 + p->n / 97) { deliver_snapshot(cl, c, p->d, cut); changed += t_sim_hash(c) != h0; }
    }
    CHECK(changed == 0, "%d truncated snapshots changed the client's state", changed);

    // Input round trip (quantised to 1/127; flags and action exact).
    uint32_t rng = 8; int ibad = 0;
    for (int k = 0; k < 2000; k++) {
        CretInput ci = {.forward = t_frand(&rng) * 2 - 1, .strafe = t_frand(&rng) * 2 - 1, .turn = t_frand(&rng) * 2 - 1,
                        .sprint = t_rand(&rng) & 1, .jump = t_rand(&rng) & 1, .action = (uint8_t)(t_rand(&rng) % (CRET_ACT_REST + 1))};
        fake_enet_nsent = 0; cret_net_client_send(cl, &ci, k);
        FakeSent* f = fake_enet_last(NULL); if (!f) { ibad++; continue; }
        g_input_n = f->n; memcpy(g_input, f->data, f->n);
        fake_enet_push(ENET_EVENT_TYPE_RECEIVE, &peer1, g_input, g_input_n);
        CretInput got[CRET_MAX_AGENTS] = {0}; cret_net_server_poll(srv, s, got, remote); fake_enet_clear();
        CretInput* g = &got[1];
        if (fabsf(g->forward - ci.forward) > 0.5f / 127 + 1e-6f || fabsf(g->strafe - ci.strafe) > 0.5f / 127 + 1e-6f || fabsf(g->turn - ci.turn) > 0.5f / 127 + 1e-6f ||
            g->sprint != ci.sprint || g->jump != ci.jump || g->action != ci.action) ibad++;
    }
    CHECK(ibad == 0, "%d of 2000 inputs did not round-trip", ibad);

    // Respawn requests: species change, plain respawn, and the one-a-second limit.
    s->tick += 100;
    fake_enet_nsent = 0; cret_net_client_request(cl, CRET_SP_TREX);
    FakeSent* rq = fake_enet_last(NULL);
    CHECK(rq && rq->reliable, "request not sent reliably");
    if (rq) {
        uint8_t req[8]; size_t rn = rq->n; memcpy(req, rq->data, rn);
        fake_enet_push(ENET_EVENT_TYPE_RECEIVE, &peer1, req, rn); cret_net_server_poll(srv, s, in, remote); fake_enet_clear();
        CHECK(s->agents[1].species == CRET_SP_TREX && s->agents[1].alive, "request: slot 1 is %s", CRET_SPECIES_TABLE[s->agents[1].species].name);
        fake_enet_nsent = 0; cret_net_client_request(cl, CRET_SP_GALLI); rq = fake_enet_last(NULL); rn = rq->n; memcpy(req, rq->data, rn);
        fake_enet_push(ENET_EVENT_TYPE_RECEIVE, &peer1, req, rn); cret_net_server_poll(srv, s, in, remote); fake_enet_clear();
        CHECK(s->agents[1].species == CRET_SP_TREX, "a second request in the same second was honoured");
        s->tick += CRET_TICK_HZ;
        fake_enet_push(ENET_EVENT_TYPE_RECEIVE, &peer1, req, rn); cret_net_server_poll(srv, s, in, remote); fake_enet_clear();
        CHECK(s->agents[1].species == CRET_SP_GALLI, "a request a second later was ignored");
    }
    // Disconnect frees the slot.
    fake_enet_push(ENET_EVENT_TYPE_DISCONNECT, &peer1, NULL, 0); cret_net_server_poll(srv, s, in, remote); fake_enet_clear();
    CHECK(!remote[1] && !s->human[1], "disconnect: remote %d human %d", remote[1], s->human[1]);
    // More peers than slots are turned away.
    CretSim* tiny = new_sim(2, 5); CretNet* srv2 = cret_net_host(CRET_DEFAULT_PORT, 1);
    int before = fake_enet_disconnects;
    fake_enet_push(ENET_EVENT_TYPE_CONNECT, &peer1, NULL, 0); fake_enet_push(ENET_EVENT_TYPE_CONNECT, &peer2, NULL, 0);
    bool r2[CRET_MAX_AGENTS] = {0}; cret_net_server_poll(srv2, tiny, in, r2); fake_enet_clear();
    CHECK(r2[1] && fake_enet_disconnects == before + 1, "a full server took a second peer");
    cret_net_close(srv2); cret_sim_close(tiny); free(tiny);
    cret_net_close(cl); cret_net_close(srv);
    cret_sim_close(c); free(c); cret_sim_close(s); free(s);
}

// ------------------------------------------------------------------ fuzz
static void mutate(uint32_t* rng, const uint8_t* src, size_t n, uint8_t* dst, size_t* out) {
    size_t m = n; memcpy(dst, src, n);
    switch (t_rand(rng) % 6) {
    case 0: m = n ? t_rand(rng) % n : 0; break;                                   // truncate
    case 1: for (int k = 1 + t_rand(rng) % 8; k-- && n;) dst[t_rand(rng) % n] ^= (uint8_t)(1u << (t_rand(rng) % 8)); break;   // bit flips
    case 2: for (int k = 1 + t_rand(rng) % 4; k-- && n;) dst[t_rand(rng) % n] = (uint8_t)t_rand(rng); break;   // byte writes
    case 3: { size_t extra = t_rand(rng) % 64; for (size_t k = 0; k < extra && m < MAX_SNAP; k++) dst[m++] = (uint8_t)t_rand(rng); break; }   // trailing junk
    case 4: { m = t_rand(rng) % 600; for (size_t k = 0; k < m; k++) dst[k] = (uint8_t)t_rand(rng); if (m) dst[0] = (uint8_t)(1 + t_rand(rng) % 5); break; }   // random body, real type
    default: if (n > 2) { size_t a = t_rand(rng) % n, b = t_rand(rng) % n; uint8_t tmp = dst[a]; dst[a] = dst[b]; dst[b] = tmp; dst[1 + t_rand(rng) % (n - 1)] = 0xff; } break;
    }
    *out = m;
}
static void check_client_state(const CretSim* c, long it) {
    for (int i = 0; i < c->cfg.num_agents; i++) {
        const CretAgent* a = &c->agents[i];
        bool ok = a->species < CRET_SPECIES && isfinite(a->x) && isfinite(a->y) && isfinite(a->z) && isfinite(a->yaw) && isfinite(a->speed) &&
                  isfinite(a->health) && a->health >= 0 && isfinite(a->max_health) && a->max_health > 0;
        CHECK_CAPPED(ok, 5, "fuzz %ld: client agent %d decoded into an invalid state", it, i);
    }
    for (int k = 0; k < CRET_MAX_CARCASSES; k++) {
        const CretCarcass* q = &c->carcasses[k];
        if (q->active) CHECK_CAPPED(q->species < CRET_SPECIES && isfinite(q->x) && isfinite(q->z) && q->meat >= 0, 5, "fuzz %ld: carcass %d invalid", it, k);
    }
}

static void fuzz(long iters) {
    if (!g_nsnaps || !g_welcome_n || !g_input_n) { fprintf(T_ERR, "fuzz: no seed packets\n"); t_fail++; return; }
    fake_enet_clear();
    CretSim* s = new_sim(32, 6);
    CretNet* srv = cret_net_host(CRET_DEFAULT_PORT, 1);
    bool remote[CRET_MAX_AGENTS] = {0}; CretInput in[CRET_MAX_AGENTS] = {0};
    fake_enet_push(ENET_EVENT_TYPE_CONNECT, &peer1, NULL, 0); cret_net_server_poll(srv, s, in, remote); fake_enet_clear();
    CretNet* cl = join_with(g_welcome, g_welcome_n);
    CretConfig cc; int slot; cret_net_client_welcomed(cl, &cc, &slot);
    CretSim* c = calloc(1, sizeof *c); if (cret_sim_init(c, cc) != 0) exit(2);
    uint8_t req[2] = {MSG_REQUEST, 0};
    uint32_t rng = 0xC0FFEEu; static uint8_t buf[MAX_SNAP]; size_t m;
    long applied = 0, welcomed = 0, bad_action = 0; double t0 = t_now();
    for (long it = 0; it < iters; it++) {
        int target = t_rand(&rng) % 10;
        if (target < 6) {                          // client: snapshots
            const Pkt* p = &g_snaps[t_rand(&rng) % g_nsnaps];
            mutate(&rng, p->d, p->n, buf, &m);
            int tick0 = c->tick; deliver_snapshot(cl, c, buf, m); applied += c->tick != tick0;
            check_client_state(c, it);
        } else if (target < 9) {                   // server: inputs, requests, junk from a peer
            const uint8_t* src = target == 8 ? req : g_input; size_t sn = target == 8 ? sizeof req : g_input_n;
            if (target == 8) { req[1] = (uint8_t)t_rand(&rng); s->tick += CRET_TICK_HZ; }
            mutate(&rng, src, sn, buf, &m);
            fake_enet_push(ENET_EVENT_TYPE_RECEIVE, &peer1, buf, m);
            CretInput got[CRET_MAX_AGENTS] = {0}; cret_net_server_poll(srv, s, got, remote); fake_enet_clear();
            const CretInput* g = &got[1];
            CHECK_CAPPED(isfinite(g->forward) && fabsf(g->forward) <= 1.01f && fabsf(g->strafe) <= 1.01f && fabsf(g->turn) <= 1.01f, 5,
                         "fuzz %ld: server decoded input out of range", it);
            bad_action += g->action > CRET_ACT_REST;
            CHECK_CAPPED(s->agents[1].species < CRET_SPECIES, 3, "fuzz %ld: request set an invalid species", it);
        } else {                                   // join handshake
            mutate(&rng, g_welcome, g_welcome_n, buf, &m);
            CretNet* j = join_with(buf, m);
            if (j) {
                CretConfig jc; int js = -1; welcomed++;
                CHECK_CAPPED(cret_net_client_welcomed(j, &jc, &js), 3, "join returned a handle without a welcome");
                CHECK_CAPPED(js >= 0 && js < jc.num_agents && jc.num_agents <= CRET_MAX_AGENTS && js < CRET_MAX_AGENTS, 3,
                             "fuzz %ld: welcome accepted slot %d of %d agents (CRET_MAX_AGENTS %d)", it, js, jc.num_agents, CRET_MAX_AGENTS);
                CHECK_CAPPED(isfinite(jc.world_cell) && jc.world_cell > 0, 3, "fuzz %ld: welcome accepted world_cell %g", it, jc.world_cell);
                cret_net_close(j);
            }
        }
    }
    fprintf(T_ERR, "fuzz: %ld packets in %.1f s; %ld mutated snapshots applied, %ld mutated welcomes accepted, %ld inputs with action > %d\n",
            iters, t_now() - t0, applied, welcomed, bad_action, CRET_ACT_REST);
    cret_net_close(cl); cret_net_close(srv);
    CHECK(fake_enet_hosts == 0, "%d ENet hosts leaked", fake_enet_hosts);
    cret_sim_close(c); free(c); cret_sim_close(s); free(s);
}

int main(int argc, char** argv) {
    double t0 = t_now(); long iters = 100000;
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--fuzz") && i + 1 < argc) iters = atol(argv[++i]);
    // net.c logs every join, respawn and bad welcome to stderr; the fuzz makes
    // thousands. Results go to the real stderr (sanitizer reports use fd 2 directly).
    t_errf = stderr;
    if (!getenv("CRET_NET_VERBOSE")) { FILE* nul = fopen("/dev/null", "w"); if (nul) stderr = nul; }
    t_hold_world();
    test_round_trips();
    fuzz(iters);
    return t_finish("test_net", t0);
}
