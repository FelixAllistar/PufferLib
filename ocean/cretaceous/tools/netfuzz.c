// Protocol v11 codec test and fuzzer. Build with sanitizers (make netfuzz):
// any out-of-bounds access or undefined behaviour aborts.
//
//   netfuzz [--packets N] [--seed S] [--no-sim]
//
// 1. Round trip: a fake server encodes snapshots of a randomly evolving sim
//    for a fake client over a lossy, duplicating, reordering channel, with
//    the client's acks (inside its input packets) going back over the same
//    kind of channel. Every snapshot the client accepts must decode to
//    exactly what the server quantised; no valid snapshot may be refused for
//    a missing baseline; inputs must arrive as sent; the own status timers,
//    progress block, nests and hit events (v10) must arrive as sent (checked
//    on snapshots delivered in the round they were made); every prop state
//    (v11) a snapshot carried must be in the client's samples, and after a
//    last stretch without loss the client's props must match the server's.
// 2. Fuzz: valid snapshots, inputs, welcomes, respawn requests and replies,
//    profile and unlock requests mutated (bit flips, byte
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
static bool g_props_frozen;
// v11 props: records appear, roll and turn, come to rest, wake, are dropped.
static void evolve_props(CretSim* s, bool init) {
    if (init) s->ndyn = 0;
    if (g_props_frozen) return;
    if (ri(3) == 0 && s->ndyn < CRET_MAX_DYN) {
        int id = ri(1 << NET_PROP_ID_BITS);
        if (cret_dyn_find(s, id) < 0) {
            CretDynProp* r = &s->dyn[s->ndyn++]; memset(r, 0, sizeof *r);
            r->id = id; r->awake = 1; r->p[0] = 8192 * rf(); r->p[1] = 600 * rf() - 50; r->p[2] = 8192 * rf(); r->q[3] = 1;
            if (ri(10) == 0) { r->p[0] = -50; r->p[1] = 1e6f; }   // out of range: clamps
        }
    }
    for (int k = 0; k < s->ndyn; k++) {
        CretDynProp* r = &s->dyn[k];
        if (r->awake) {
            for (int i = 0; i < 3; i++) r->p[i] += (rf() - 0.5f) * 0.4f;
            float l = 0; for (int i = 0; i < 4; i++) { r->q[i] += (rf() - 0.5f) * 0.2f; l += r->q[i] * r->q[i]; }
            for (int i = 0; i < 4; i++) r->q[i] /= sqrtf(l);
            if (ri(40) == 0) r->awake = 0;
        } else if (ri(400) == 0) r->awake = 1;
    }
    if (s->ndyn && ri(25) == 0) { int k = ri(s->ndyn); s->dyn[k] = s->dyn[--s->ndyn]; }
}
static const NetPropCl* client_prop(const NetClient* c, int32_t id) {
    for (int k = 0; k < c->nprops; k++) if (c->props[k].id == id) return &c->props[k];
    return NULL;
}
static bool same_pq(const NetPropQ* a, const NetPropQ* b) { return a->gone == b->gone && a->x == b->x && a->y == b->y && a->z == b->z && a->q == b->q && a->rest == b->rest; }
static void evolve(CretSim* s, bool init) {
    evolve_props(s, init);
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
        if (mode < 5) a->act = (uint8_t)ri(CRET_ACT_MAX + 1);
        if (mode < 15) a->aim = mode == 3 ? 1.0f : mode == 4 ? -1.0f : rf() * 2 - 1;   // v9: head aim
        if (mode == 12) { a->attack2_cd = ri(200); a->buf_act = (uint8_t)(ri(2) ? CRET_ACT_ATTACK : CRET_ACT_ATTACK2); a->buf_ticks = (uint8_t)ri(8); }
        else if (a->attack2_cd > 0) a->attack2_cd--;
        if (mode == 6) a->hurt_ticks = 12; else if (a->hurt_ticks > 0) a->hurt_ticks--;
        if (mode == 7) a->group = (uint8_t)ri(s->cfg.num_agents + 1);
        if (mode == 8) { a->rally_ticks = 600; a->rally_x = 8192 * rf(); a->rally_z = 8192 * rf(); } else if (a->rally_ticks > 0) a->rally_ticks--;
        if (mode == 9) a->species = (uint8_t)ri(CRET_SPECIES);
        a->health = fminf(a->max_health, fmaxf(0, a->health + (rf() - 0.5f) * 5)); a->growth = fminf(1, a->growth + 0.0005f);
        a->on_ground = mode % 3 != 0; a->swimming = mode % 7 == 0; a->sprinting = mode < 10; a->broken = mode % 11 == 0 ? 5 : 0;
        a->bleed = mode % 13 == 0 ? 10 : 0;
        // v8: statuses, abilities, marks
        if (mode == 20) a->st[ri(CRET_ST_COUNT)] = 30 * rf();
        for (int k = 0; k < CRET_ST_COUNT; k++) a->st[k] = fmaxf(a->st[k] - 1.0f / 15, 0);
        if (mode == 21) { a->latch = (uint8_t)ri(s->cfg.num_agents + 1); a->latchers = (uint8_t)ri(4); }
        if (mode == 22) { a->strike = (uint8_t)ri(CRET_AB_COUNT); a->leap = (uint8_t)ri(CRET_AB_COUNT); a->leap_t = rf(); }
        if (mode == 23) { a->slow_amt = rf(); a->def_speed = 0.5f + rf(); a->nest_ticks = ri(200); a->mark_by = (uint8_t)ri(229); }
        if (mode == 24) for (int k = 0; k < CRET_AB_SLOTS; k++) a->ab_cd[k] = 20 * rf();
    }
    if (ri(30) == 0) {   // the viewer's (slot 0) progress and the nests
        CretProgress* p = &s->prog[0]; p->marks = ri(70000); p->unlocked = r32(); p->quests_done = ri(100);
        for (int k = 0; k < CRET_AB_SLOTS; k++) p->loadout[k] = (uint8_t)ri(CRET_AB_COUNT);
        CretQuest* q = &p->quest; q->kind = (uint8_t)ri(CRET_Q_COUNT); q->steps = (uint8_t)ri(4); q->step = (uint8_t)ri(q->steps + 1);
        for (int k = 0; k < 3; k++) q->poi[k] = (uint8_t)ri(256);
        q->species = (uint8_t)ri(CRET_SPECIES); q->progress = 100 * rf(); q->goal = 100 * rf(); q->marks = ri(500);
        s->agents[0].group = (uint8_t)ri(s->cfg.num_agents + 1);
        if (s->agents[0].group) { s->gquest[s->agents[0].group] = *q; s->gquest[s->agents[0].group].group = s->agents[0].group; }
    } else if (ri(4) == 0) s->prog[0].quest.progress += 1.0f / 15;   // a quest in progress changes every snapshot
    if (ri(60) == 0) {
        CretNest* q = &s->nests[ri(CRET_MAX_NESTS)]; q->active = ri(2); q->species = (uint8_t)ri(CRET_SPECIES); q->group = (uint8_t)ri(s->cfg.num_agents + 1);
        q->owner = (uint8_t)ri(s->cfg.num_agents + 1); q->eggs = ri(6); q->x = 8192 * rf(); q->y = 300 * rf(); q->z = 8192 * rf();
    }
    for (int c = 0; c < CRET_MAX_CARCASSES; c++) {
        CretCarcass* k = &s->carcasses[c];
        if (init || ri(200) == 0) {
            k->active = ri(2); k->species = (uint8_t)ri(CRET_SPECIES); k->x = 8192 * rf(); k->z = 8192 * rf(); k->y = 100 * rf(); k->yaw = rf() * 6 - 3;
            k->growth = rf(); k->max_meat = 100; k->meat = 100 * rf(); k->age = 0; k->agent = ri(s->cfg.num_agents + 1) - 1;
        }
        if (k->active) { k->age++; k->meat = fmaxf(0, k->meat - 0.01f); }
    }
    // v10 hits: some on or by the viewer (slot 0), some between others (near or far), a few to itself.
    for (int k = ri(4); k-- > 0;) {
        int at = ri(s->cfg.num_agents), vi = ri(s->cfg.num_agents);
        if (ri(3) == 0) { if (ri(2)) at = 0; else vi = 0; }
        cret_log_hit(s, at, vi, ri(CRET_HIT_KINDS), ri(10) == 0 ? 0 : rf());
    }
    s->tick++; s->time_of_day = fmodf(s->time_of_day + 0.001f, 1.0f); s->cloud_cover = rf();
}

typedef struct { uint8_t d[NET_SNAP_BUDGET]; size_t n; int due, made; } Pkt;
static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); if (++g_fail > 20) exit(1); } } while (0)

// Pool of valid packets to mutate.
#define POOL 256
static Pkt g_snap_pool[POOL], g_in_pool[POOL]; static int g_nsnap, g_nin;

// v8 blocks of a snapshot made from `s` as it is now (the viewer is slot 0).
static uint64_t g_blocks_checked;
static void check_blocks(const CretSim* s, const NetClient* c, uint32_t seq) {
    const CretAgent* a = &s->agents[0]; const NetSelf* m = &c->self;
    if (c->self_seq == seq) {
        bool ok = m->strike == a->strike && m->latch == a->latch && m->latchers == a->latchers && m->leap == a->leap && m->nest_ticks == a->nest_ticks &&
                  fabsf(m->slow_amt - a->slow_amt) <= 0.5f / 255 + 1e-6f && fabsf(m->def_speed - a->def_speed) <= 0.5f / 16384 + 1e-6f && fabsf(m->leap_t - a->leap_t) <= 0.0006f;
        for (int k = 0; k < CRET_ST_COUNT; k++) ok = ok && fabsf(m->st[k] - a->st[k]) <= 0.0006f;
        for (int k = 0; k < CRET_AB_SLOTS; k++) ok = ok && fabsf(m->ab_cd[k] - a->ab_cd[k]) <= 0.0006f;
        CHECK(ok, "own statuses of snapshot %u decoded wrong", seq);
    }
    if (c->prog_seq == seq) {
        const CretProgress* p = &s->prog[0]; const NetProg* P = &c->prog; const CretQuest *q = &p->quest, *Q = &P->quest;
        bool ok = P->marks == (p->marks > 65535 ? 65535 : p->marks) && P->unlocked == p->unlocked && P->quests_done == p->quests_done &&
                  !memcmp(P->loadout, p->loadout, sizeof P->loadout) && Q->kind == q->kind && Q->step == q->step && Q->steps == q->steps &&
                  !memcmp(Q->poi, q->poi, 3) && Q->species == q->species && Q->progress == q->progress && Q->goal == q->goal && Q->marks == q->marks;
        const CretQuest* g = a->group ? &s->gquest[a->group] : NULL;
        ok = ok && P->has_group == (g && g->kind) && (!P->has_group || (P->group.kind == g->kind && P->group.group == g->group && P->group.progress == g->progress));
        CHECK(ok, "progress block of snapshot %u decoded wrong", seq);
        g_blocks_checked++;
    }
    if (c->nest_seq == seq) {
        bool ok = true;
        for (int k = 0; k < CRET_MAX_NESTS; k++) {
            const CretNest *x = &s->nests[k], *y = &c->nests[k];
            ok = ok && x->active == y->active && (!x->active || (x->species == y->species && x->group == y->group && x->owner == y->owner && x->eggs == y->eggs &&
                                                                 x->x == y->x && x->y == y->y && x->z == y->z));
        }
        CHECK(ok, "nest block of snapshot %u decoded wrong", seq);
        g_blocks_checked++;
    }
    if (c->newest_seq == seq) {   // v10: every hit the snapshot carried is in the client's recent hits
        uint8_t buf[256]; Bits b = {buf, sizeof buf, 0, false}; netp_put_hits(&b, s, 0);
        Bits r = {buf, bits_bytes(&b), 0, false}; CretHit want[NET_HIT_MAX]; int n = netp_get_hits(&r, want, (uint32_t)s->tick, s->cfg.num_agents);
        CHECK(!r.bad, "hit block of snapshot %u does not decode", seq);
        for (int k = 0; k < n; k++) {
            bool seen = false;
            for (uint32_t q = c->nhit_seen > CRET_HIT_LOG ? c->nhit_seen - CRET_HIT_LOG : 0; q < c->nhit_seen && !seen; q++) seen = !memcmp(&c->hit_seen[q % CRET_HIT_LOG], &want[k], sizeof want[k]);
            CHECK(seen, "hit %d of snapshot %u (%d->%d) missing on the client", k, seq, want[k].attacker, want[k].victim);
        }
        g_blocks_checked++;
    }
}

typedef struct { CretSim* s; CretNet* srv; NetPeer* np; CretNet* cn; } Kept;
static uint64_t g_props_checked;
static uint64_t round_trip(int rounds, int na, int loss_, int late_pct_, int late_max, Kept* keep) {
    CretSim* s = calloc(1, sizeof *s); s->cfg = cret_default_config(); s->cfg.num_agents = na; evolve(s, true);
    CretNet* srv = netp_fake_server(0, 8192); NetPeer* np = netp_fake_peer(srv, 0, 0);
    CretNet* cn = netp_fake_client(na, 0, 2048, 4.0f); NetClient* c = cn->cl;
    Pkt down[64], up[64]; int nd = 0, nu = 0; uint64_t packets = 0, accepted = 0, refused_valid = 0;
    for (int r = 0; r < rounds; r++) {
        // The last 60 rounds: props hold still and the channel is clean (they must converge).
        bool tail = r >= rounds - 60; int loss = tail ? 0 : loss_, late_pct = tail ? 0 : late_pct_; g_props_frozen = tail;
        for (int t = 0; t < CRET_SNAPSHOT_DIV; t++) evolve(s, false);
        for (int i = 0; i < na; i++) netp_quant_agent(&srv->sc, s, i, &srv->q[i]);
        for (int k = 0; k < CRET_MAX_CARCASSES; k++) netp_quant_carcass(&srv->sc, s, k, &srv->q[NET_CARCASS0 + k]);
        Pkt p; p.n = netp_server_snapshot(srv, np, s, p.d, sizeof p.d); p.made = r;
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
                if (q.made == r) check_blocks(s, c, seq);
                if (np->sent_seq[h] == seq && q.made == r)   // v11: every prop state it carried is a client sample (or the prop is gone)
                    for (int j = 0; j < np->npsent[h]; j++) {
                        const NetPropSent* ps = &np->psent[h][j]; const NetPropCl* e = client_prop(c, ps->id);
                        bool found = false;
                        if (ps->q.gone) found = !e || e->seq > seq;
                        else if (e) for (int i = 0; i < e->ns && !found; i++) found = e->s[i].seq == seq && same_pq(&e->s[i].q, &ps->q);
                        else found = false;
                        CHECK(found, "prop %d of snapshot %u missing on the client", ps->id, seq);
                        g_props_checked++;
                    }
            }
        }
        // Client tick: an input and the acks, up the same kind of channel.
        CretInput in = {.forward = rf() * 2 - 1, .strafe = rf() * 2 - 1, .turn = rf() * 2 - 1, .sprint = (uint8_t)ri(2), .jump = (uint8_t)ri(2), .action = (uint8_t)ri(CRET_ACT_MAX + 1), .ability = (uint8_t)ri(CRET_AB_SLOTS + 1), .aim = rf() * 2 - 1};
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
    // v11: converged. Every server record is on the client at its quantised pose; nothing else is.
    int pbad = 0, pextra = 0;
    for (int k = 0; k < s->ndyn; k++) {
        NetPropQ want; netp_prop_quant(&srv->sc, s->dyn[k].p, s->dyn[k].q, !s->dyn[k].awake, &want);
        const NetPropCl* e = client_prop(c, s->dyn[k].id);
        const NetPropSample* last = NULL;
        if (e) for (int i = 0; i < e->ns; i++) if (!last || e->s[i].seq > last->seq) last = &e->s[i];
        pbad += !last || !same_pq(&last->q, &want);
    }
    for (int k = 0; k < c->nprops; k++) pextra += cret_dyn_find(s, c->props[k].id) < 0;
    CHECK(pbad == 0 && pextra == 0, "props did not converge: %d of %d wrong or missing, %d stale on the client", pbad, s->ndyn, pextra);
    g_props_frozen = false;
    printf("round trip: %d snapshots (%d agents, %d%% loss, %d%% late by up to %d), %llu accepted, %llu valid refused, %u inputs applied, %llu packets, %llu v8 blocks checked, %llu prop states checked, %d props converged\n", rounds, na, loss_, late_pct_, late_max,
           (unsigned long long)accepted, (unsigned long long)refused_valid, np->applied, (unsigned long long)packets, (unsigned long long)g_blocks_checked, (unsigned long long)g_props_checked, s->ndyn);
    g_blocks_checked = 0; g_props_checked = 0;
    if (keep) { *keep = (Kept){s, srv, np, cn}; return packets; }
    cret_net_close(srv); cret_net_close(cn); free(s);
    return packets;
}

static size_t mutate(uint8_t* d, size_t n, size_t cap) {
    int kind = ri(8);
    if (kind == 0) { n = (size_t)ri((int)cap) + 1; for (size_t i = 0; i < n; i++) d[i] = (uint8_t)r32(); if (ri(2)) d[0] = (uint8_t)(MSG_WELCOME + ri(MSG_UNLOCK - MSG_WELCOME + 1)); return n; }
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
    // Valid requests and replies to mutate: respawn request, profile, unlock (server); spawn reply (client).
    uint8_t reqs[4][32] = {{0}}; size_t rlen[4];
    {
        Bits b = {reqs[0], 32, 0, false}; bits_put(&b, MSG_REQUEST, 8); bits_put(&b, 7, 8); bits_put(&b, CRET_SP_TREX, 8); rlen[0] = bits_bytes(&b);
        b = (Bits){reqs[1], 32, 0, false}; bits_put(&b, MSG_PROFILE, 8); bits_put(&b, 4, 5); for (int c = 0; c < 4; c++) bits_put(&b, (uint8_t)"Rexy"[c], 8); rlen[1] = bits_bytes(&b);
        b = (Bits){reqs[2], 32, 0, false}; bits_put(&b, MSG_UNLOCK, 8); bits_put(&b, 1, 1); rlen[2] = bits_bytes(&b);
        b = (Bits){reqs[3], 32, 0, false}; bits_put(&b, MSG_SPAWN_REPLY, 8); bits_put(&b, 7, 8); bits_put(&b, CRET_SPAWN_IN_COMBAT, 8); bits_put(&b, CRET_SP_TREX, 8); bits_put(&b, 9, 8); rlen[3] = bits_bytes(&b);
    }
    k.s->agents[3].alive = true; k.s->prog[3].marks = 1000000;
    uint64_t acc_snap = 0, acc_in = 0, acc_wel = 0, acc_req = 0, applied = 0;
    long fuzzed = 0;
    for (; (long)total < packets; fuzzed++) {
        uint8_t d[NET_SNAP_BUDGET + 64]; size_t n; int target = ri(10);
        if (target < 6) { const Pkt* p = &g_snap_pool[ri(g_nsnap)]; memcpy(d, p->d, p->n); n = mutate(d, p->n, sizeof d);
            fc->recv_max = rmax; fc->recv_mask = rmask;   // every pool snapshot is new to it
            if (netp_client_packet(cn, d, n, fuzzed * 1e-3)) acc_snap++; }
        else if (target == 8) { int w = ri(4); memcpy(d, reqs[w], rlen[w]); n = mutate(d, rlen[w], sizeof d);
            if (w == 3) { CretSpawnReply r; acc_req += netp_client_packet(cn, d, n, 0) && cret_net_client_spawn_reply(cn, &r) && r.result <= CRET_SPAWN_IN_COMBAT && r.species < CRET_SPECIES; }
            else {
                acc_req += netp_server_request(np, k.s, d, n) || netp_server_profile(np, k.s, d, n);
                CHECK(np->pending == REQ_NONE || np->pending == REQ_SAME || (np->pending >= 0 && np->pending < CRET_SPECIES), "held species %d", np->pending);
                CHECK(memchr(k.s->prog[3].name, 0, sizeof k.s->prog[3].name) != NULL, "name not terminated");
                CHECK(k.s->prog[3].loadout[2] < CRET_AB_COUNT, "slot 3 ability %d", k.s->prog[3].loadout[2]);
                np->pending = REQ_NONE;
            } }
        else if (target < 9) { const Pkt* p = &g_in_pool[ri(g_nin)]; memcpy(d, p->d, p->n); n = mutate(d, p->n, sizeof d);
            if (netp_server_input(np, d, n)) acc_in++;
            if (ri(4) == 0) np->applied = np->newest > NET_REDUNDANCY ? np->newest - NET_REDUNDANCY : 0; }
        else { memcpy(d, welcome, wlen); n = mutate(d, wlen, sizeof d);
            if (netp_client_packet(wn, d, n, 0)) { acc_wel++; free(wn->cl); wn->cl = NULL; wn->welcomed = false; } }
        total++;
        if (sim && fuzzed % 64 == 0) {   // whatever was accepted goes through apply, reconcile and draw
            netp_client_apply(cn, sim); cret_net_client_draw(cn, sim, draw, rf(), 1.0f / 60); applied++;
            for (int q = 0; q < CRET_HIT_LOG && (uint32_t)q < sim->hit_count; q++) {   // v10: only valid hits reach the log
                const CretHit* h = &sim->hits[q];
                CHECK(h->attacker < NA && h->victim < NA && h->attacker != h->victim && h->kind < CRET_HIT_KINDS && h->dmg > 0, "hit %d invalid (%d->%d kind %d dmg %d)", q, h->attacker, h->victim, h->kind, h->dmg);
            }
            if (fuzzed % 256 == 0) { CretInput in = {.forward = 1, .turn = rf() - 0.5f}; cret_net_client_tick(cn, sim, &in); }
        }
        if (fuzzed % 200000 == 0 && fuzzed) fprintf(stderr, "netfuzz: %ld fuzzed\n", fuzzed);
    }
    printf("fuzz: %ld mutated or random datagrams (snapshots accepted %llu, inputs accepted %llu, welcomes accepted %llu, requests/replies accepted %llu), %llu apply+draw passes\n",
           fuzzed, (unsigned long long)acc_snap, (unsigned long long)acc_in, (unsigned long long)acc_wel, (unsigned long long)acc_req, (unsigned long long)applied);
    printf("total %llu packets, %d failures\n", (unsigned long long)total, g_fail);
    cret_net_close(cn); cret_net_close(srv); cret_net_close(k.srv); free(k.s); free(wn);
    if (sim) { cret_sim_close(sim); free(sim); free(draw); }
    return g_fail ? 1 : 0;
}
