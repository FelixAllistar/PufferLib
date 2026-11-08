// Net codec tests (protocol v8). net.c, netclient.c and netsim.c are
// compiled as is against an in-memory ENet (tests/fake_enet), so the tests
// see the exact bytes on the wire.
//   - welcome, input (every action head), respawn request and reply,
//     profile and unlock, and snapshot round trips over a 3000-tick bot run:
//     every entity a snapshot carries decodes to the server's state within
//     its quantisation, the own state (with status timers and cooldowns),
//     progress block and nests exactly;
//   - a version mismatch is refused, a full server turns a peer away;
//   - a truncated snapshot changes nothing on the client (all or nothing);
//   - fuzzing (--fuzz N): truncated, bit-flipped, spliced and random packets
//     into the client, the server and the join handshake. Build with
//     sanitizers for this (make fuzz / make asan).
// tools/netfuzz.c is the codec's own fuzzer (fake transport, lossy channel
// round trips at 64 and 128 agents); this one drives the ENet-facing paths.
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
    CretNet* c = cret_net_join("127.0.0.1", CRET_DEFAULT_PORT, 0.001f);   // the first poll takes both events; a bad welcome would otherwise wait the whole timeout
    fake_enet_clear();
    return c;
}
static void snapshot(CretNet* srv, const CretSim* s, Pkt* p) {
    fake_enet_nsent = 0; cret_net_server_broadcast(srv, s);
    FakeSent* f = fake_enet_last(&peer1); p->n = 0;
    if (f) { p->n = f->n; memcpy(p->d, f->data, f->n); }
}
static bool deliver(CretNet* cl, CretSim* cs, const uint8_t* d, size_t n) {
    fake_enet_push(ENET_EVENT_TYPE_RECEIVE, &fake_enet_server_peer, d, n);
    bool ok = cret_net_client_poll(cl, cs); fake_enet_clear(); return ok;
}
// The client's next input (with its snapshot acks) to the server; returns the server's input for slot 1.
static CretInput send_input(CretNet* cl, CretSim* cs, CretNet* srv, CretSim* s, const CretInput* in) {
    fake_enet_nsent = 0; cret_net_client_tick(cl, cs, in);
    FakeSent* f = fake_enet_last(NULL); CretInput got[CRET_MAX_AGENTS] = {0}; bool remote[CRET_MAX_AGENTS] = {0};
    if (f) { uint8_t raw[MAX_SNAP]; size_t n = f->n; memcpy(raw, f->data, n); fake_enet_clear(); fake_enet_push(ENET_EVENT_TYPE_RECEIVE, &peer1, raw, n); }
    cret_net_server_poll(srv, s, got, remote); fake_enet_clear();
    return got[1];
}
// The server's latest reply to peer1 (respawn reply), delivered to the client.
static bool server_poll_reply(CretNet* srv, CretSim* s, CretNet* cl, CretSim* cs, CretSpawnReply* r) {
    CretInput in[CRET_MAX_AGENTS] = {0}; bool remote[CRET_MAX_AGENTS] = {0};
    fake_enet_nsent = 0; cret_net_server_poll(srv, s, in, remote);
    FakeSent* f = fake_enet_last(&peer1); uint8_t raw[64]; size_t n = 0;
    if (f && f->reliable && f->n <= sizeof raw) { n = f->n; memcpy(raw, f->data, n); }
    fake_enet_clear();
    if (!n) return false;
    deliver(cl, cs, raw, n);
    return cret_net_client_spawn_reply(cl, r);
}
static void push_from_peer(CretNet* cl, CretNet* srv, CretSim* s) {   // the client's last reliable packet, to the server
    FakeSent* f = fake_enet_last(NULL); uint8_t raw[64]; size_t n = f ? f->n : 0; if (n > sizeof raw) n = 0;
    if (n) memcpy(raw, f->data, n);
    fake_enet_clear();
    if (n) fake_enet_push(ENET_EVENT_TYPE_RECEIVE, &peer1, raw, n);
    (void)cl; (void)srv; (void)s;
}

// ------------------------------------------------------------------ round trips
#define Q8 (0.5f / 255.0f + 1e-6f)
#define QPOS (0.5f / NET_POS_Q + 5e-4f)   // half a step, plus float rounding of y + 2048 m before quantising
static float rally8(float v) { return (float)(uint32_t)fminf(fmaxf(v * 8, 0), 65535) / 8.0f; }   // 1/8 m, clamped to the map
static float wrapd(float d) { while (d > 3.14159265f) d -= 6.2831853f; while (d < -3.14159265f) d += 6.2831853f; return fabsf(d); }

// Entities the snapshot `seq` carried, against the server's state.
static int compare_snapshot(const CretSim* s, const CretSim* c, const NetClient* nc, uint32_t seq, int* bad) {
    int compared = 0;
    for (int i = 0; i < s->cfg.num_agents; i++) {
        if (i == nc->me || nc->latest_seq[i] != seq) continue;
        compared++;
        const CretAgent *a = &s->agents[i], *b = &c->agents[i];
        float hf = a->max_health > 0 ? a->health / a->max_health : 0, hb = b->max_health > 0 ? b->health / b->max_health : 0;
        int rally = a->rally_ticks > 0;
        uint8_t sf = (a->st[CRET_ST_STUN] > 0) | (a->st[CRET_ST_FEAR] > 0) << 1 | (a->latch != 0) << 2 | (a->latchers > 0) << 3 | (a->st[CRET_ST_DEFENSIVE] > 0) << 4 |
                     (a->st[CRET_ST_MARKED] > 0) << 5 | (a->st[CRET_ST_BURST] > 0) << 6 | (a->st[CRET_ST_MEND] > 0) << 7;
        uint8_t sb = (b->st[CRET_ST_STUN] > 0) | (b->st[CRET_ST_FEAR] > 0) << 1 | (b->latch != 0) << 2 | (b->latchers > 0) << 3 | (b->st[CRET_ST_DEFENSIVE] > 0) << 4 |
                     (b->st[CRET_ST_MARKED] > 0) << 5 | (b->st[CRET_ST_BURST] > 0) << 6 | (b->st[CRET_ST_MEND] > 0) << 7;
        bool ok = b->alive == a->alive && b->species == a->species && b->act == a->act && b->on_ground == a->on_ground && b->swimming == a->swimming &&
                  b->sprinting == a->sprinting && fabsf(b->x - a->x) <= QPOS && fabsf(b->y - a->y) <= QPOS && fabsf(b->z - a->z) <= QPOS &&
                  wrapd(b->yaw - a->yaw) <= 3.1416f / NET_YAW_Q + 1e-4f && fabsf(b->speed - fminf(a->speed, 255.0f / 16)) <= 1.0f / 32 + 1e-4f &&
                  fabsf(hb - hf) <= Q8 * 1.01f && fabsf(b->growth - a->growth) <= Q8 &&
                  b->hurt_ticks == a->hurt_ticks && (a->alive || b->dead_ticks == (a->dead_ticks & 0xffff)) &&
                  (b->bleed > 0) == (a->bleed > 0.002f * a->max_health) && (b->broken > 0) == (a->broken > 0) && b->group == a->group &&
                  b->rally_ticks == rally && (!rally || (b->rally_x == rally8(a->rally_x) && b->rally_z == rally8(a->rally_z))) &&
                  sb == sf && b->mark_by == (a->st[CRET_ST_MARKED] > 0 ? a->mark_by : 0) && fabsf(b->aim - a->aim) <= 0.5f / NET_AIM_Q + 1e-5f;
        if (!ok) {
            (*bad)++;
            CHECK_CAPPED(0, 5, "tick %d agent %d: snapshot round trip differs: alive %d/%d sp %d/%d act %d/%d pos %.4f %.4f %.4f / %.4f %.4f %.4f yaw %.4f/%.4f speed %.3f/%.3f "
                         "health %.4f/%.4f growth %.4f/%.4f hurt %d/%d dead %d/%d bleed %d/%d broken %d/%d group %d/%d rally %d/%d status %02x/%02x mark %d/%d",
                         s->tick, i, a->alive, b->alive, a->species, b->species, a->act, b->act, a->x, a->y, a->z, b->x, b->y, b->z, a->yaw, b->yaw, a->speed, b->speed,
                         hf, hb, a->growth, b->growth, a->hurt_ticks, b->hurt_ticks, a->dead_ticks, b->dead_ticks, a->bleed > 0.002f * a->max_health, b->bleed > 0,
                         a->broken > 0, b->broken > 0, a->group, b->group, rally, b->rally_ticks, sf, sb, a->mark_by, b->mark_by);
        }
    }
    for (int k = 0; k < CRET_MAX_CARCASSES; k++) {
        if (nc->latest_seq[NET_CARCASS0 + k] != seq) continue;
        compared++;
        const CretCarcass *a = &s->carcasses[k], *b = &c->carcasses[k];
        if (a->active != b->active) { (*bad)++; CHECK_CAPPED(0, 3, "tick %d carcass %d: active %d/%d", s->tick, k, a->active, b->active); continue; }
        if (!a->active) continue;
        float mf = a->max_meat > 0 ? a->meat / a->max_meat : 0;
        bool ok = b->species == a->species && fabsf(b->growth - a->growth) <= Q8 && fabsf(b->meat / b->max_meat - mf) <= Q8 &&
                  b->agent == (a->agent < 0 ? -1 : a->agent) && fabsf(b->x - a->x) <= QPOS && fabsf(b->z - a->z) <= QPOS && wrapd(b->yaw - a->yaw) <= 3.1416f / NET_YAW_Q + 1e-4f;
        if (!ok) { (*bad)++; CHECK_CAPPED(0, 3, "tick %d carcass %d: round trip differs", s->tick, k); }
    }
    // Own state at full precision (what prediction rewinds to), with its statuses.
    if (nc->self_seq == seq) {
        const CretAgent* a = &s->agents[nc->me]; const NetSelf* m = &nc->self;
        bool ok = m->alive == a->alive && m->x == a->x && m->y == a->y && m->z == a->z && m->vx == a->vx && m->vy == a->vy && m->vz == a->vz &&
                  m->speed == a->speed && m->stamina == a->stamina && m->growth == a->growth && m->act == a->act &&
                  m->act_ticks == (a->act_ticks > 255 ? 255 : a->act_ticks) && m->attack_cd == (a->attack_cd > 255 ? 255 : a->attack_cd) &&
                  m->strike == a->strike && m->latch == a->latch && m->latchers == a->latchers && m->nest_ticks == a->nest_ticks &&
                  fabsf(m->slow_amt - a->slow_amt) <= Q8 &&
                  m->attack2_cd == (a->attack2_cd > 255 ? 255 : a->attack2_cd) && m->buf_act == a->buf_act && m->buf_ticks == a->buf_ticks && m->aim == a->aim;
        for (int k = 0; k < CRET_ST_COUNT; k++) ok = ok && fabsf(m->st[k] - a->st[k]) <= 0.0006f;
        for (int k = 0; k < CRET_AB_SLOTS; k++) ok = ok && fabsf(m->ab_cd[k] - a->ab_cd[k]) <= 0.0006f;
        if (!ok) { (*bad)++; CHECK_CAPPED(0, 3, "tick %d: own state differs", s->tick); }
    }
    if (nc->prog_seq == seq) {
        const CretProgress* p = &s->prog[nc->me]; const NetProg* P = &nc->prog;
        bool ok = P->marks == p->marks && P->unlocked == p->unlocked && P->quests_done == p->quests_done && !memcmp(P->loadout, p->loadout, 3) &&
                  P->quest.kind == p->quest.kind && P->quest.progress == p->quest.progress && P->quest.goal == p->quest.goal && P->quest.step == p->quest.step;
        if (!ok) { (*bad)++; CHECK_CAPPED(0, 3, "tick %d: progress block differs", s->tick); }
    }
    if (nc->nest_seq == seq)
        for (int k = 0; k < CRET_MAX_NESTS; k++) {
            const CretNest *x = &s->nests[k], *y = &nc->nests[k];
            bool ok = x->active == y->active && (!x->active || (x->species == y->species && x->group == y->group && x->owner == y->owner && x->eggs == y->eggs && x->x == y->x && x->z == y->z));
            if (!ok) { (*bad)++; CHECK_CAPPED(0, 3, "tick %d: nest %d differs", s->tick, k); }
        }
    return compared;
}

static Pkt g_snaps[64]; static int g_nsnaps;
static uint8_t g_welcome[64]; static size_t g_welcome_n;
static uint8_t g_input[512]; static size_t g_input_n;
static uint8_t g_req[3][32]; static size_t g_req_n[3];   // respawn request, profile, unlock

static void test_round_trips(void) {
    fake_enet_clear();
    fake_enet_connect_data = CRET_NET_VERSION;
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
    NetClient* nc = cl->cl;

    // Snapshots over a bot run, with deaths, carcasses, rallies, statuses,
    // progress and nests forced in. Each round the client's input (and acks)
    // reaches the server first, so baselines move and deltas are exercised.
    int bad = 0, rallies = 0, carc = 0, dead = 0, compared = 0, statuses = 0, truncated = 0, changed = 0;
    for (int t = 0; t < 3000; t += 50) {
        CretInput idle = {0}; send_input(cl, c, srv, s, &idle);
        step_bots(s, 50);
        if (t == 500) for (int i = 4; i < 10; i++) { s->agents[i].health = 0.001f; s->agents[i].bleed = 1e4f; }   // carcasses
        if (t == 900) { s->agents[2].rally_ticks = 100; s->agents[2].rally_x = 123.5f; s->agents[2].rally_z = -7.25f; s->agents[3].hurt_ticks = 400; }
        if (t >= 1200 && t < 2000) {   // statuses on others and on the client's own animal; a nest; the client's progress
            s->agents[2].st[CRET_ST_STUN] = 1.5f; s->agents[3].st[CRET_ST_MARKED] = 4; s->agents[3].mark_by = 102; s->agents[11].latchers = 2;
            s->agents[1].st[CRET_ST_SLOW] = 2.25f; s->agents[1].slow_amt = 0.3f; s->agents[1].ab_cd[1] = 6.5f;
            s->nests[5] = (CretNest){.active = true, .species = CRET_SP_PACHY, .group = 3, .owner = 4, .eggs = 2, .x = 100, .y = 20, .z = 200};
            s->prog[1].marks = 40 + t; s->prog[1].quest.kind = CRET_Q_DRINK; s->prog[1].quest.goal = 30; s->prog[1].quest.progress = t / 100.0f;
            // v9: aims on others (exactly representable or not), the own secondary cooldown, a queued attack and the own aim (inputs carry k/127).
            s->agents[2].aim = -0.73f; s->agents[3].aim = 1; s->agents[5].aim = 0.5f; s->agents[3].act = CRET_ACT_ATTACK2;
            s->agents[1].attack2_cd = 17; s->agents[1].buf_act = CRET_ACT_ATTACK2; s->agents[1].buf_ticks = 4; s->agents[1].aim = -45 / 127.0f;
            statuses++;
        }
        Pkt p; snapshot(srv, s, &p);
        CHECK_CAPPED(p.n > 0, 3, "no snapshot sent");
        if (g_nsnaps < 64) g_snaps[g_nsnaps++] = p;
        // Every truncation of the new snapshot is refused and changes nothing.
        if (t % 500 == 0) {
            cret_net_client_poll(cl, c);   // whatever the last input's prediction left is applied first
            uint64_t h0 = t_sim_hash(c);
            for (size_t cut = 0; cut < p.n; cut += 1 + p.n / 97) { deliver(cl, c, p.d, cut); truncated++; changed += t_sim_hash(c) != h0; }
        }
        uint32_t seq = (uint32_t)p.d[1] << 24 | (uint32_t)p.d[2] << 16 | (uint32_t)p.d[3] << 8 | p.d[4];
        CHECK_CAPPED(deliver(cl, c, p.d, p.n), 3, "client disconnected");
        CHECK_CAPPED(nc->newest_seq == seq, 3, "snapshot %u not accepted (newest %u)", seq, nc->newest_seq);
        compared += compare_snapshot(s, c, nc, seq, &bad);
        for (int i = 0; i < s->cfg.num_agents; i++) { rallies += s->agents[i].rally_ticks > 0; dead += !s->agents[i].alive; }
        for (int k = 0; k < CRET_MAX_CARCASSES; k++) carc += s->carcasses[k].active;
    }
    fprintf(T_ERR, "snapshots: %d round trips (%zu..%zu bytes), %d entities compared, %d differed; covered %d rallies, %d dead agents, %d carcasses, %d status rounds\n",
            g_nsnaps, g_snaps[0].n, g_snaps[g_nsnaps - 1].n, compared, bad, rallies, dead, carc, statuses);
    CHECK(rallies > 0 && carc > 0 && dead > 0 && compared > 200, "round trips missed rallies, carcasses or deaths (compared %d)", compared);
    CHECK(changed == 0, "%d of %d truncated snapshots changed the client's state", changed, truncated);
    CHECK(c->prog[1].marks == s->prog[1].marks && c->nests[5].active == s->nests[5].active && c->nests[5].eggs == s->nests[5].eggs,
          "client progress %d (server %d), nest %d/%d", c->prog[1].marks, s->prog[1].marks, c->nests[5].active, s->nests[5].active);
    CHECK(fabsf(c->agents[1].ab_cd[1] - s->agents[1].ab_cd[1]) < 0.2f, "own cooldown %.2f vs %.2f", c->agents[1].ab_cd[1], s->agents[1].ab_cd[1]);

    // Input round trip: every head (quantised to 1/127; flags, action and ability exact), in order.
    uint32_t rng = 8; int ibad = 0;
    for (int k = 0; k < 2000; k++) {
        CretInput ci = {.forward = t_frand(&rng) * 2 - 1, .strafe = t_frand(&rng) * 2 - 1, .turn = t_frand(&rng) * 2 - 1,
                        .sprint = t_rand(&rng) & 1, .jump = t_rand(&rng) & 1, .action = (uint8_t)(t_rand(&rng) % (CRET_ACT_MAX + 1)),
                        .ability = (uint8_t)(t_rand(&rng) % (CRET_AB_SLOTS + 1)), .aim = t_frand(&rng) * 2 - 1};
        if (k % 7 == 0) ci.aim = k % 14 ? -1.0f : 1.0f;   // the ends of the arc
        fake_enet_nsent = 0;
        CretInput g = send_input(cl, c, srv, s, &ci);
        if (fabsf(g.forward - ci.forward) > 0.5f / 127 + 1e-6f || fabsf(g.strafe - ci.strafe) > 0.5f / 127 + 1e-6f || fabsf(g.turn - ci.turn) > 0.5f / 127 + 1e-6f ||
            fabsf(g.aim - ci.aim) > 0.5f / 127 + 1e-6f || g.sprint != ci.sprint || g.jump != ci.jump || g.action != ci.action || g.ability != ci.ability) ibad++;
        if (k % 64 == 63) { Pkt p; snapshot(srv, s, &p); deliver(cl, c, p.d, p.n); }   // acks keep the resend window short
    }
    CHECK(ibad == 0, "%d of 2000 inputs did not round-trip", ibad);
    // v9 input validation: an aim byte of -128 or a bit past the 40 is refused; every valid packing is accepted.
    {
        CretInput ci = {.forward = 0.5f, .action = CRET_ACT_ATTACK2, .aim = -1};
        uint64_t v = netp_pack_input(&ci);
        CHECK(netp_input_valid(v) && (int8_t)(uint8_t)v == -127, "packed aim -1 is %d", (int8_t)(uint8_t)v);
        CHECK(!netp_input_valid((v & ~0xffull) | 0x80) && !netp_input_valid(v | (1ull << NET_INPUT_BITS)) && !netp_input_valid(v | 0x8000), "invalid inputs accepted");
        CretInput nan_in = {.forward = NAN, .aim = NAN}; CretInput q = netp_unpack_input(netp_pack_input(&nan_in));
        CHECK(q.forward == 0 && q.aim == 0, "non-finite input packed to %g / %g", q.forward, q.aim);
    }
    {   // keep one input packet for the fuzz
        fake_enet_nsent = 0; CretInput ci = {.forward = 1, .action = CRET_ACT_ATTACK2, .aim = 0.4f}; cret_net_client_tick(cl, c, &ci);
        FakeSent* f = fake_enet_last(NULL); if (f && f->n <= sizeof g_input) { g_input_n = f->n; memcpy(g_input, f->data, f->n); }
        fake_enet_clear();
    }

    // Respawn requests: settled at once after the cooldown, a second within a
    // second is held until it is up, each answered once.
    s->agents[1].hurt_ticks = 0; srv->peers[0]->hurt_tick = s->tick - CRET_NET_COMBAT_LOCK;
    s->tick += 2 * CRET_TICK_HZ;
    CretSpawnReply r;
    fake_enet_nsent = 0; int seq = cret_net_client_request(cl, CRET_SP_TREX);
    FakeSent* rq = fake_enet_last(NULL);
    CHECK(rq && rq->reliable, "request not sent reliably");
    if (rq) { g_req_n[0] = rq->n; memcpy(g_req[0], rq->data, rq->n); }
    push_from_peer(cl, srv, s);
    CHECK(server_poll_reply(srv, s, cl, c, &r) && r.seq == seq && r.result == CRET_SPAWN_OK && r.species == CRET_SP_TREX, "request: reply %d species %d", r.result, r.species);
    CHECK(s->agents[1].species == CRET_SP_TREX && s->agents[1].alive, "request: slot 1 is %s", CRET_SPECIES_TABLE[s->agents[1].species].name);
    fake_enet_nsent = 0; seq = cret_net_client_request(cl, CRET_SP_GALLI); push_from_peer(cl, srv, s);
    CHECK(!server_poll_reply(srv, s, cl, c, &r) && s->agents[1].species == CRET_SP_TREX, "a second request in the same second was settled");
    s->tick += CRET_TICK_HZ; s->agents[1].hurt_ticks = 0;
    CHECK(server_poll_reply(srv, s, cl, c, &r) && r.seq == seq && r.result == CRET_SPAWN_OK && s->agents[1].species == CRET_SP_GALLI, "the held request was not settled a second later");
    // An unknown species is refused at once.
    { uint8_t badsp[3] = {MSG_REQUEST, 99, CRET_SPECIES + 2}; fake_enet_push(ENET_EVENT_TYPE_RECEIVE, &peer1, badsp, 3); }
    CHECK(server_poll_reply(srv, s, cl, c, &r) && r.seq == 99 && r.result == CRET_SPAWN_BAD_SPECIES, "bad species: result %d", r.result);
    // Profile and unlock reach the server.
    fake_enet_nsent = 0; cret_net_client_profile(cl, "Bonehead"); FakeSent* pf = fake_enet_last(NULL);
    if (pf) { g_req_n[1] = pf->n; memcpy(g_req[1], pf->data, pf->n); }
    push_from_peer(cl, srv, s); server_poll_reply(srv, s, cl, c, &r);
    CHECK(!strcmp(s->prog[1].name, "Bonehead") && s->prog[1].load_pending, "profile: name '%s'", s->prog[1].name);
    s->prog[1].marks = 1000; s->agents[1].alive = true;
    fake_enet_nsent = 0; cret_net_client_unlock(cl, 1); FakeSent* uf = fake_enet_last(NULL);
    if (uf) { g_req_n[2] = uf->n; memcpy(g_req[2], uf->data, uf->n); }
    push_from_peer(cl, srv, s); server_poll_reply(srv, s, cl, c, &r);
    CHECK(cret_choice_unlocked(s, 1, 1) && s->prog[1].marks < 1000, "unlock: marks %d", s->prog[1].marks);

    // Disconnect frees the slot.
    fake_enet_push(ENET_EVENT_TYPE_DISCONNECT, &peer1, NULL, 0); cret_net_server_poll(srv, s, in, remote); fake_enet_clear();
    CHECK(!remote[1] && !s->human[1], "disconnect: remote %d human %d", remote[1], s->human[1]);
    // More peers than slots are turned away; so is another protocol version.
    CretSim* tiny = new_sim(2, 5); CretNet* srv2 = cret_net_host(CRET_DEFAULT_PORT, 1);
    int before = fake_enet_disconnects;
    fake_enet_push(ENET_EVENT_TYPE_CONNECT, &peer1, NULL, 0); fake_enet_push(ENET_EVENT_TYPE_CONNECT, &peer2, NULL, 0);
    bool r2[CRET_MAX_AGENTS] = {0}; cret_net_server_poll(srv2, tiny, in, r2); fake_enet_clear();
    CHECK(r2[1] && fake_enet_disconnects == before + 1, "a full server took a second peer");
    CretNet* srv3 = cret_net_host(CRET_DEFAULT_PORT, 1); before = fake_enet_disconnects;
    fake_enet_connect_data = CRET_NET_VERSION - 1;
    fake_enet_push(ENET_EVENT_TYPE_CONNECT, &peer2, NULL, 0); bool r3[CRET_MAX_AGENTS] = {0}; cret_net_server_poll(srv3, tiny, in, r3); fake_enet_clear();
    fake_enet_connect_data = CRET_NET_VERSION;
    CHECK(fake_enet_disconnects == before + 1 && !srv3->peers[0], "a client with protocol %d was let in", CRET_NET_VERSION - 1);
    cret_net_close(srv3); cret_net_close(srv2); cret_sim_close(tiny); free(tiny);
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
    case 4: { m = t_rand(rng) % 600; for (size_t k = 0; k < m; k++) dst[k] = (uint8_t)t_rand(rng); if (m) dst[0] = (uint8_t)(MSG_WELCOME + t_rand(rng) % (MSG_UNLOCK - MSG_WELCOME + 1)); break; }   // random body, real type
    default: if (n > 2) { size_t a = t_rand(rng) % n, b = t_rand(rng) % n; uint8_t tmp = dst[a]; dst[a] = dst[b]; dst[b] = tmp; dst[1 + t_rand(rng) % (n - 1)] = 0xff; } break;
    }
    *out = m;
}
static void check_client_state(const CretSim* c, long it) {
    for (int i = 0; i < c->cfg.num_agents; i++) {
        const CretAgent* a = &c->agents[i];
        bool ok = a->species < CRET_SPECIES && a->act <= CRET_ACT_MAX && isfinite(a->aim) && fabsf(a->aim) <= 1 && isfinite(a->x) && isfinite(a->y) && isfinite(a->z) && isfinite(a->yaw) && isfinite(a->speed) &&
                  isfinite(a->health) && a->health >= 0 && isfinite(a->max_health) && a->max_health > 0 && a->strike < CRET_AB_COUNT && a->latch <= c->cfg.num_agents;
        for (int k = 0; k < CRET_ST_COUNT; k++) ok = ok && isfinite(a->st[k]) && a->st[k] >= 0;
        CHECK_CAPPED(ok, 5, "fuzz %ld: client agent %d decoded into an invalid state", it, i);
    }
    for (int k = 0; k < CRET_MAX_CARCASSES; k++) {
        const CretCarcass* q = &c->carcasses[k];
        if (q->active) CHECK_CAPPED(q->species < CRET_SPECIES && isfinite(q->x) && isfinite(q->z) && q->meat >= 0, 5, "fuzz %ld: carcass %d invalid", it, k);
    }
    for (int k = 0; k < CRET_MAX_NESTS; k++) {
        const CretNest* q = &c->nests[k];
        if (q->active) CHECK_CAPPED(q->species < CRET_SPECIES && isfinite(q->x) && isfinite(q->z), 5, "fuzz %ld: nest %d invalid", it, k);
    }
    for (int k = 0; k < CRET_AB_SLOTS; k++) CHECK_CAPPED(c->prog[1].loadout[k] < CRET_AB_COUNT, 3, "fuzz %ld: loadout %d", it, c->prog[1].loadout[k]);
    CHECK_CAPPED(c->prog[1].quest.kind < CRET_Q_COUNT, 3, "fuzz %ld: quest kind %d", it, c->prog[1].quest.kind);
}

static void fuzz(long iters) {
    if (!g_nsnaps || !g_welcome_n || !g_input_n || !g_req_n[0] || !g_req_n[1] || !g_req_n[2]) { fprintf(T_ERR, "fuzz: no seed packets\n"); t_fail++; return; }
    fake_enet_clear(); fake_enet_connect_data = CRET_NET_VERSION;
    CretSim* s = new_sim(32, 6);
    CretNet* srv = cret_net_host(CRET_DEFAULT_PORT, 1);
    bool remote[CRET_MAX_AGENTS] = {0}; CretInput in[CRET_MAX_AGENTS] = {0};
    fake_enet_push(ENET_EVENT_TYPE_CONNECT, &peer1, NULL, 0); cret_net_server_poll(srv, s, in, remote); fake_enet_clear();
    CretNet* cl = join_with(g_welcome, g_welcome_n);
    CretConfig cc; int slot; cret_net_client_welcomed(cl, &cc, &slot);
    CretSim* c = calloc(1, sizeof *c); if (cret_sim_init(c, cc) != 0) exit(2);
    // The kept snapshots' baselines: deliver them all once, in order.
    for (int q = 0; q < g_nsnaps; q++) deliver(cl, c, g_snaps[q].d, g_snaps[q].n);
    uint32_t rng = 0xC0FFEEu; static uint8_t buf[MAX_SNAP]; size_t m;
    long applied = 0, welcomed = 0, replies = 0; double t0 = t_now();
    for (long it = 0; it < iters; it++) {
        int target = t_rand(&rng) % 10;
        if (target < 6) {                          // client: snapshots (each one new to it), spawn replies
            const Pkt* p = &g_snaps[t_rand(&rng) % g_nsnaps];
            if (target == 5) { uint8_t rep[5] = {MSG_SPAWN_REPLY, 3, CRET_SPAWN_IN_COMBAT, CRET_SP_RAPTOR, 7}; mutate(&rng, rep, sizeof rep, buf, &m); }
            else mutate(&rng, p->d, p->n, buf, &m);
            cl->cl->recv_max = 0; cl->cl->recv_mask = 0;
            uint64_t s0 = cl->cl->st.snapshots; deliver(cl, c, buf, m); applied += cl->cl->st.snapshots != s0;
            CretSpawnReply r; if (cret_net_client_spawn_reply(cl, &r)) { replies++; CHECK_CAPPED(r.result <= CRET_SPAWN_IN_COMBAT && r.species < CRET_SPECIES, 3, "fuzz %ld: bad reply", it); }
            check_client_state(c, it);
            if (it % 16 == 0) { CretInput ci = {.forward = 1, .ability = (uint8_t)(it % 4), .action = (uint8_t)(it % 3 ? 0 : CRET_ACT_ATTACK2), .aim = (float)(it % 5) * 0.5f - 1}; fake_enet_nsent = 0; cret_net_client_tick(cl, c, &ci); fake_enet_clear(); }
        } else if (target < 9) {                   // server: inputs, requests, profiles, unlocks, junk from a peer
            int w = target == 8 ? (int)(t_rand(&rng) % 3) : -1;
            const uint8_t* src = w < 0 ? g_input : g_req[w]; size_t sn = w < 0 ? g_input_n : g_req_n[w];
            if (w >= 0) s->tick += CRET_TICK_HZ;
            mutate(&rng, src, sn, buf, &m);
            fake_enet_push(ENET_EVENT_TYPE_RECEIVE, &peer1, buf, m);
            CretInput got[CRET_MAX_AGENTS] = {0}; cret_net_server_poll(srv, s, got, remote); fake_enet_clear();
            const CretInput* g = &got[1];
            CHECK_CAPPED(isfinite(g->forward) && fabsf(g->forward) <= 1.01f && fabsf(g->strafe) <= 1.01f && fabsf(g->turn) <= 1.01f, 5,
                         "fuzz %ld: server decoded input out of range", it);
            CHECK_CAPPED(g->action <= CRET_ACT_MAX && g->ability <= CRET_AB_SLOTS && isfinite(g->aim) && fabsf(g->aim) <= 1, 5, "fuzz %ld: action %d ability %d aim %g out of range", it, g->action, g->ability, g->aim);
            CHECK_CAPPED(s->agents[1].species < CRET_SPECIES, 3, "fuzz %ld: request set an invalid species", it);
            CHECK_CAPPED(memchr(s->prog[1].name, 0, sizeof s->prog[1].name) != NULL, 3, "fuzz %ld: name not terminated", it);
            CHECK_CAPPED(s->prog[1].loadout[2] < CRET_AB_COUNT, 3, "fuzz %ld: slot 3 ability %d", it, s->prog[1].loadout[2]);
            s->prog[1].load_pending = 0;
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
    fprintf(T_ERR, "fuzz: %ld packets in %.1f s; %ld mutated snapshots applied, %ld replies taken, %ld mutated welcomes accepted\n",
            iters, t_now() - t0, applied, replies, welcomed);
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
    unsetenv("CRET_NETSIM");   // the relay needs real sockets
    t_hold_world();
    test_round_trips();
    fuzz(iters);
    return t_finish("test_net_codec", t0);
}
