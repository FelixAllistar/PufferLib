// Telemetry for classifiers (net.h, docs/anticheat.md; read by
// tools/cret_telemetry.py). Included by net.c. The server writes one binary
// log of every logged slot: the clients' players, and the bots or policies
// a tool names (cret_net_telemetry_slot), in the same records, so human and
// bot play share one format. Nothing in it names a person: a slot, and a
// player id that counts the joins since the server started.
//
// Little-endian. The file starts with a 32-byte header:
//   "CRETTEL1", u16 format (NET_TL_FORMAT), u16 protocol version, u16 tick
//   rate, u16 snapshot rate, u16 agents, u16 entity ids (agents, then
//   carcasses from NET_CARCASS0), u32 world seed, f32 world size (m), u32
//   the tick it was opened at.
// Then records: u8 type, u8 slot, u16 payload bytes, u32 server tick, and
// the payload:
//   JOIN    u32 player, u8 human (1) or bot (0), u8 species
//   LEAVE   u32 player
//   INPUT   once a tick per logged slot: u32 input seq (a bot's: the tick),
//           u8 how (0 applied, 1 applied at once to catch up, 2 held: none
//           had come), the input as received (i8 forward, strafe, turn, u8
//           flags: sprint, jump, 3-bit action, 2-bit ability slot, carry; i8
//           aim; netp_pack_input), u8 alive, f32 the press's view (render
//           tick, -1: none), f32 x, y, z, yaw of the animal before it
//   VIS     every snapshot tick: what the slot could perceive (the relevance
//           rule, netaoi.c) and what it had in plain sight, two masks of
//           NET_TL_MASK bytes (entity id e is bit e % 8 of byte e / 8)
//   APPEAR  an entity came into plain sight: u16 entity, f32 distance (m),
//           f32 its bearing from the animal's heading (rad, + the way the heading grows)
//   WORLD   every snapshot tick (slot 255): u16 agents, then per agent u16
//           x and z in 1/8 m, u8 heading in 1/256 turn, u8 alive | species
//           << 1 | action << 4, u8 health (of 255), u8 speed (1/4 m/s)
//   SWING   a blow resolved: u8 action, u8 landed, u16 the nearest animal
//           to the strike point (0xffff: none), u8 it was relevant, u8 in
//           plain sight, f32 its distance from the strike point (m), f32
//           ticks the server judged the targets back (lag compensation)
//   ACK     an input packet: u32 snapshot acknowledged, u32 that snapshot's
//           tick (0: unknown), u32 newest input seq, u8 inputs in it, u8
//           server queue depth, u16 ENet round trip (ms)
//   FLAG    a check's finding (netcheck.c): u8 CRET_AC_* kind, u32 count so far, f32 value
#include <math.h>

typedef struct { uint8_t d[1200]; size_t n; } TlBuf;
static void tl_u8(TlBuf* b, uint32_t v) { if (b->n < sizeof b->d) b->d[b->n++] = (uint8_t)v; }
static void tl_u16(TlBuf* b, uint32_t v) { tl_u8(b, v & 255); tl_u8(b, (v >> 8) & 255); }
static void tl_u32(TlBuf* b, uint32_t v) { tl_u16(b, v & 0xffff); tl_u16(b, v >> 16); }
static void tl_f32(TlBuf* b, float v) { uint32_t u; memcpy(&u, &v, 4); tl_u32(b, u); }
static void tl_put(CretNet* n, int type, int slot, int tick, const TlBuf* p) {
    if (!n->tl) return;
    TlBuf h = {.n = 0}; tl_u8(&h, (uint32_t)type); tl_u8(&h, (uint32_t)slot); tl_u16(&h, (uint32_t)p->n); tl_u32(&h, (uint32_t)tick);
    fwrite(h.d, 1, h.n, n->tl); fwrite(p->d, 1, p->n, n->tl);
}
// Whether slot i is logged: a client plays it (and is not loading), or a tool named it.
static bool tl_logged(const CretNet* n, int i) {
    for (int k = 0; k < CRET_MAX_PEERS; k++) if (n->peers[k] && n->peers[k]->slot == i) return !join_loading(n->peers[k]);
    return n->tl_slot[i];
}

bool cret_net_telemetry_open(CretNet* n, const char* path, const CretSim* s) {
    if (!n || !path) return false;
    cret_net_telemetry_close(n);
    n->tl = fopen(path, "wb");
    if (!n->tl) { fprintf(stderr, "cretaceous: cannot write telemetry to %s\n", path); return false; }
    TlBuf h = {.n = 0};
    for (int c = 0; c < 8; c++) tl_u8(&h, (uint8_t)NET_TL_MAGIC[c]);
    tl_u16(&h, NET_TL_FORMAT); tl_u16(&h, CRET_NET_VERSION); tl_u16(&h, CRET_TICK_HZ); tl_u16(&h, (uint32_t)(n->snap_hz ? n->snap_hz : CRET_SNAPSHOT_HZ));
    tl_u16(&h, (uint32_t)s->cfg.num_agents); tl_u16(&h, NET_NENT); tl_u32(&h, s->cfg.world_seed);
    tl_f32(&h, s->world ? s->world->size : s->cfg.world_n * s->cfg.world_cell); tl_u32(&h, (uint32_t)s->tick);
    fwrite(h.d, 1, h.n, n->tl);
    for (int k = 0; k < CRET_MAX_PEERS; k++) if (n->peers[k]) netp_tl_join(n, n->peers[k]->slot, n->peers[k]->player, true, s);
    fprintf(stderr, "cretaceous: telemetry to %s\n", path);
    return true;
}
void cret_net_telemetry_close(CretNet* n) { if (n && n->tl) { fclose(n->tl); n->tl = NULL; } }

void cret_net_telemetry_slot(CretNet* n, int slot, bool on) {
    if (!n || slot < 0 || slot >= CRET_MAX_AGENTS || (n->tl_slot[slot] != 0) == on) return;
    n->tl_slot[slot] = on;
    netp_aoi_reset(n, slot);
}

void netp_tl_join(CretNet* n, int slot, uint32_t player, bool human, const CretSim* s) {
    TlBuf b = {.n = 0}; tl_u32(&b, player); tl_u8(&b, human); tl_u8(&b, s->agents[slot].species);
    tl_put(n, NET_TL_JOIN, slot, s->tick, &b);
}
void netp_tl_leave(CretNet* n, int slot, uint32_t player, const CretSim* s) {
    TlBuf b = {.n = 0}; tl_u32(&b, player); tl_put(n, NET_TL_LEAVE, slot, s->tick, &b);
}

void netp_tl_input(CretNet* n, const CretSim* s, int slot, uint32_t seq, const CretInput* in, int how, double view) {
    if (!n->tl) return;
    // A bot's slot logs its first input: its JOIN (player id) comes with it.
    if (n->tl_slot[slot] == 1) { n->tl_slot[slot] = 2; netp_tl_join(n, slot, ++n->tl_players, false, s); }
    const CretAgent* a = &s->agents[slot]; uint64_t v = netp_pack_input(in);
    TlBuf b = {.n = 0};
    tl_u32(&b, seq); tl_u8(&b, (uint32_t)how);
    for (int k = 4; k >= 0; k--) tl_u8(&b, (uint32_t)(v >> (8 * k)) & 255);   // forward, strafe, turn, flags, aim
    tl_u8(&b, a->alive); tl_f32(&b, (float)view); tl_f32(&b, a->x); tl_f32(&b, a->y); tl_f32(&b, a->z); tl_f32(&b, a->yaw);
    tl_put(n, NET_TL_INPUT, slot, s->tick, &b);
}

void netp_tl_ack(CretNet* n, const CretSim* s, NetPeer* np, int rtt_ms, int qdepth) {
    if (!n->tl) return;
    TlBuf b = {.n = 0};
    tl_u32(&b, np->rx_ack); tl_u32(&b, np->rx_known); tl_u32(&b, np->rx_newest); tl_u8(&b, np->rx_count); tl_u8(&b, (uint32_t)qdepth);
    tl_u16(&b, (uint32_t)(rtt_ms < 0 ? 0 : rtt_ms > 65535 ? 65535 : rtt_ms));
    tl_put(n, NET_TL_ACK, np->slot, s->tick, &b);
}

void netp_tl_flag(CretNet* n, int tick, int slot, int kind, uint32_t count, float value) {
    TlBuf b = {.n = 0}; tl_u8(&b, (uint32_t)kind); tl_u32(&b, count); tl_f32(&b, value);
    tl_put(n, NET_TL_FLAG, slot, tick, &b);
}

// The blows the last step resolved: a logged animal whose swing is at its
// hit tick now struck in that step (the hit log has its tick, one before
// this one). Its target is the animal nearest the strike point.
void netp_tl_tick(CretNet* n, const CretSim* s) {
    if (!n->tl) return;
    for (int i = 0; i < s->cfg.num_agents; i++) {
        const CretAgent* a = &s->agents[i];
        if (!a->alive || !cret_act_is_attack(a->act) || !tl_logged(n, i)) continue;
        const CretSpecies* S = &CRET_SPECIES_TABLE[a->species % CRET_SPECIES];
        if (a->act_ticks != (int)((a->act == CRET_ACT_ATTACK2 ? S->alt.hit_time : S->hit_time) * CRET_TICK_HZ)) continue;
        bool landed = false;
        for (uint32_t k = s->hit_count > CRET_HIT_LOG ? s->hit_count - CRET_HIT_LOG : 0; k < s->hit_count; k++) {
            const CretHit* h = &s->hits[k % CRET_HIT_LOG];
            if (h->tick == s->tick - 1 && h->attacker == i && h->kind != CRET_HIT_THORNS && h->kind != CRET_HIT_CRUSH && h->kind != CRET_HIT_OTHER) landed = true;
        }
        float px, pz, best = 1e30f; int t = -1; cret_strike_point(a, &px, &pz);
        for (int j = 0; j < s->cfg.num_agents; j++) {
            if (j == i || !s->agents[j].alive) continue;
            float d = cret_body_dist(&s->agents[j], px, pz); if (d < best) { best = d; t = j; }
        }
        const NetAoi* A = &n->aoi[i];
        TlBuf b = {.n = 0};
        tl_u8(&b, a->act); tl_u8(&b, landed); tl_u16(&b, t < 0 ? 0xffff : (uint32_t)t);
        tl_u8(&b, t >= 0 && A->in[t]); tl_u8(&b, t >= 0 && A->seen[t]); tl_f32(&b, t < 0 ? -1 : best); tl_f32(&b, n->rewind.back[i]);
        tl_put(n, NET_TL_SWING, i, s->tick - 1, &b);
    }
}

// Every snapshot tick: where every animal is, and for each logged slot
// what it could perceive and what came into its plain sight.
void netp_tl_snapshot(CretNet* n, const CretSim* s) {
    if (!n->tl) return;
    TlBuf w = {.n = 0}; tl_u16(&w, (uint32_t)s->cfg.num_agents);
    for (int i = 0; i < s->cfg.num_agents; i++) {
        const CretAgent* a = &s->agents[i];
        float yaw = a->yaw / 6.2831853f; yaw -= floorf(yaw);
        tl_u16(&w, (uint32_t)clampf(a->x * 8, 0, 65535)); tl_u16(&w, (uint32_t)clampf(a->z * 8, 0, 65535));
        tl_u8(&w, (uint32_t)(yaw * 256) & 255); tl_u8(&w, (uint32_t)(a->alive ? 1 : 0) | (uint32_t)(a->species & 7) << 1 | (uint32_t)(a->act & 7) << 4);
        tl_u8(&w, q8(a->max_health > 0 ? a->health / a->max_health : 0)); tl_u8(&w, (uint32_t)clampf(a->speed * 4, 0, 255));
    }
    tl_put(n, NET_TL_WORLD, 255, s->tick, &w);
    for (int i = 0; i < s->cfg.num_agents; i++) {
        if (!tl_logged(n, i)) continue;
        NetAoi* A = &n->aoi[i]; uint8_t was[NET_NENT]; memcpy(was, A->seen, sizeof was);
        netp_aoi_update(n, s, i);
        TlBuf v = {.n = 0};
        for (int m = 0; m < 2; m++)
            for (int k = 0; k < NET_TL_MASK; k++) {
                uint32_t byte = 0;
                for (int q = 0; q < 8 && 8 * k + q < NET_NENT; q++) byte |= (uint32_t)((m ? A->seen : A->in)[8 * k + q] != 0) << q;
                tl_u8(&v, byte);
            }
        tl_put(n, NET_TL_VIS, i, s->tick, &v);
        const CretAgent* a = &s->agents[i];
        for (int e = 0; e < s->cfg.num_agents; e++) {
            if (e == i || !A->seen[e] || was[e] || !s->agents[e].alive) continue;
            const CretAgent* b = &s->agents[e];
            float dx = b->x - a->x, dz = b->z - a->z, rel = atan2f(dz, dx) - a->yaw;
            while (rel > 3.14159265f) rel -= 6.2831853f;
            while (rel < -3.14159265f) rel += 6.2831853f;
            TlBuf p = {.n = 0}; tl_u16(&p, (uint32_t)e); tl_f32(&p, sqrtf(dx * dx + dz * dz)); tl_f32(&p, rel);
            tl_put(n, NET_TL_APPEAR, i, s->tick, &p);
        }
    }
    if (s->tick % (2 * CRET_TICK_HZ) == 0) fflush(n->tl);   // a crash loses at most two seconds
}
