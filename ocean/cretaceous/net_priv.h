// Internals shared by net.c (codec, server, client transport), netclient.c
// (prediction, reconciliation, interpolation) and tools/netfuzz.c.
#ifndef CRET_NET_PRIV_H
#define CRET_NET_PRIV_H
#include "net.h"
#include <enet/enet.h>
#include <string.h>

// MSG_REQUEST (client, reliable): 8-bit seq, 8-bit species (255: the same).
// MSG_SPAWN_REPLY (server, reliable, once per settled request): 8-bit seq,
// CRET_SPAWN_* result, species and wait_s, 8 bits each.
// MSG_PROFILE (client, reliable): 5-bit length (<= 23), that many 8-bit
// characters: the character name (the server loads and saves it).
// MSG_UNLOCK (client, reliable): 1 bit, slot-3 ability choice to buy or equip.
enum { MSG_WELCOME = 2, MSG_INPUT, MSG_SNAPSHOT, MSG_REQUEST, MSG_SPAWN_REPLY, MSG_PROFILE, MSG_UNLOCK };
enum { REQ_NONE = -2, REQ_SAME = -1 };   // NetPeer.pending: no request held; respawn as the same species (>= 0: a species)
enum { CH_RELIABLE, CH_STATE, CH_COUNT };
#define NET_REJECT_VERSION 0x56000000u   // disconnect data: 'V' << 24 | server version

#define NET_NENT (CRET_MAX_AGENTS + CRET_MAX_CARCASSES)   // entity ids: agents, then carcasses
#define NET_CARCASS0 CRET_MAX_AGENTS
#define NET_NF 17            // quantised fields per entity
#define NET_HIST 64          // snapshots both ends keep per entity (baselines, acks)
#define NET_REORDER 32       // a client still takes a snapshot this much older than its newest
#define NET_MAX_AGE (NET_HIST - NET_REORDER)   // baselines are younger (5-bit age), so a late snapshot's baseline is still kept
#define NET_IN_RING 128      // inputs: client history, server queue
#define NET_REDUNDANCY 32    // most inputs in one packet (all unacknowledged ones up to this)
#define NET_INTERP 16        // samples per agent in the interpolation buffer
#define NET_SNAP_BUDGET 1200 // snapshot payload bytes (one datagram under ENet's 1392 MTU)
#define NET_BUFFER_MAX 4     // queued inputs beyond this are applied at once

// ---------------------------------------------------------------- bit stream
// MSB-first; every write and read is bounds-checked and sets `bad` instead
// of touching memory past `cap` bytes. Reads past the end return 0.
typedef struct { uint8_t* p; size_t cap; size_t pos; bool bad; } Bits;
static inline void bits_put(Bits* b, uint32_t v, int n) {
    for (int i = n - 1; i >= 0; i--) {
        if (b->pos >= b->cap * 8) { b->bad = true; return; }
        uint8_t m = (uint8_t)(0x80u >> (b->pos & 7));
        if ((v >> i) & 1u) b->p[b->pos >> 3] |= m; else b->p[b->pos >> 3] &= (uint8_t)~m;
        b->pos++;
    }
}
static inline uint32_t bits_get(Bits* b, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        if (b->pos >= b->cap * 8) { b->bad = true; return 0; }
        v = (v << 1) | ((b->p[b->pos >> 3] >> (7 - (b->pos & 7))) & 1u);
        b->pos++;
    }
    return v;
}
static inline size_t bits_bytes(const Bits* b) { return (b->pos + 7) >> 3; }

// ---------------------------------------------------------------- entities
// Quantised state: what the codec compares, deltas and stores as baselines.
typedef struct { uint32_t f[NET_NF]; } NetEnt;
typedef struct { uint8_t bits, small, medium, wrap; } NetField;   // small/medium: signed delta widths (0: not delta-coded)
typedef struct { NetField agent[NET_NF], carcass[NET_NF]; int pos_bits; } NetSchema;
enum { AF_FLAGS, AF_KIND, AF_X, AF_Y, AF_Z, AF_YAW, AF_SPEED, AF_HEALTH, AF_GROWTH, AF_HURT, AF_DEAD, AF_GROUP, AF_RALLYX, AF_RALLYZ, AF_STATUS, AF_MARK, AF_AIM, AF_N };
enum { CF_KIND, CF_X, CF_Y, CF_Z, CF_YAW, CF_GROWTH, CF_MEAT, CF_BORN, CF_AGENT, CF_N };
// AF_STATUS bits (what other animals show: nameplates, HUD marks).
enum { SF_STUN = 1, SF_FEAR = 2, SF_LATCH = 4, SF_LATCHED = 8, SF_DEFENSIVE = 16, SF_MARKED = 32, SF_BURST = 64, SF_MEND = 128 };
enum { FL_ALIVE = 1, FL_GROUND = 2, FL_SWIM = 4, FL_SPRINT = 8, FL_BLEED = 16, FL_BROKEN = 32, FL_RALLY = 64, FL_HURT = 128 };
#define NET_POS_Q 256.0f     // 1/256 m
#define NET_Y_OFS 2048.0f    // y is stored from -2048 m
#define NET_YAW_Q 4096       // 12-bit heading
#define NET_AIM_Q 31         // agent aim: 6 bits, (aim + 1) * 31 (63 is refused)

// How often the server updates an entity, by distance from the viewer (in
// snapshots per snapshot): every one within 40 m, down to about once a
// second beyond 800 m. Clients use it to know how stale a sample may be.
static inline float netp_interest(float d) { return d < 40 ? 1.0f : d < 120 ? 0.5f : d < 300 ? 0.25f : d < 800 ? 0.12f : 0.06f; }

// The local player's own state at full precision (prediction replays from it).
typedef struct {
    bool alive, on_ground, swimming, sprinting;
    uint8_t act, act_ticks, attack_cd, hurt_ticks, food, water;
    float x, y, z, yaw, vx, vy, vz, speed, stamina, broken, growth;
    // Abilities and statuses (v8): what movement and the HUD read.
    uint8_t strike, latch, latchers, leap; uint16_t nest_ticks;
    float slow_amt, def_speed, leap_t, st[CRET_ST_COUNT], ab_cd[CRET_AB_SLOTS];
    // Aimed attacks (v9): secondary cooldown, the queued attack, the aim (exact: inputs carry it in 1/127).
    uint8_t attack2_cd, buf_act, buf_ticks; float aim;
} NetSelf;

// The local player's progress (v8): marks, unlocks, loadout, quests.
typedef struct {
    int marks, quests_done; uint32_t unlocked; uint8_t loadout[CRET_AB_SLOTS];
    CretQuest quest, group; bool has_group;
} NetProg;
#define NET_BLOCK_RESEND 8   // progress and nest blocks: sent this many snapshots after a change
#define NET_BLOCK_REFRESH 15 // ... and every this many snapshots regardless

typedef struct NetPeer {
    ENetPeer* peer; int slot;
    // Inputs by sequence (client tick); applied: the last one the sim used.
    CretInput in[NET_IN_RING]; uint32_t in_seq[NET_IN_RING];
    uint32_t applied, newest; int qmin, qwin;
    // Respawn requests: tick of the last respawn by request, last tick the
    // animal flinched from a hit, the request held for the cooldown.
    int req_tick, hurt_tick, pending; uint8_t pending_seq;
    // Progress and nest blocks: hash of the last content and the snapshot it changed in.
    uint32_t prog_hash, prog_changed, nest_hash, nest_changed;
    // Snapshots: per entity the newest state the client acknowledged (the
    // delta baseline) and the last one sent; what each recent snapshot held.
    uint32_t seq;
    NetEnt base[NET_NENT]; uint32_t base_seq[NET_NENT];
    NetEnt last[NET_NENT]; uint32_t last_seq[NET_NENT];
    float prio[NET_NENT];
    NetEnt sent[NET_HIST][NET_NENT]; uint8_t sent_has[NET_HIST][NET_NENT];
    uint32_t sent_seq[NET_HIST]; uint8_t sent_acked[NET_HIST];
} NetPeer;

typedef struct { uint32_t tick; NetEnt e; } NetSample;

typedef struct NetClient {
    int num_agents, me;
    NetSchema sc;
    // Received snapshots: newest sequence and a mask of the 32 before it (acks).
    uint32_t recv_max, recv_mask;
    NetEnt hist[NET_NENT][NET_HIST]; uint32_t hist_seq[NET_NENT][NET_HIST];
    NetEnt latest[NET_NENT]; uint32_t latest_seq[NET_NENT], applied_seq[NET_NENT];
    NetSample samp[CRET_MAX_AGENTS][NET_INTERP]; int nsamp[CRET_MAX_AGENTS];
    float gap[CRET_MAX_AGENTS], extra[CRET_MAX_AGENTS];
    uint32_t newest_tick, newest_seq; float tod, cloud; int qdepth;
    // Own state from the server.
    NetSelf self; uint32_t self_seq, self_ack; bool self_new;
    // Own progress and the nests, from the newest snapshot that carried them.
    NetProg prog; uint32_t prog_seq; bool prog_new;
    CretNest nests[CRET_MAX_NESTS]; uint32_t nest_seq; bool nest_new;
    // Server clock estimate (ticks) and the interpolation delay (ticks).
    bool clock_ok, in_underrun; double clock_off, jitter, delay, boost;
    // Prediction: inputs and predicted agent after each, by sequence.
    uint32_t seq, acked;
    CretInput in[NET_IN_RING]; double in_time[NET_IN_RING];
    CretAgent pred[NET_IN_RING]; uint32_t pred_seq[NET_IN_RING];
    bool pred_ok; float off[3], off_yaw;
    // Stats since the last reset.
    CretNetStats st; double t0; uint64_t base_up, base_down, base_up_pk, base_down_pk;
} NetClient;

struct CretNet {
    ENetHost* host;
    // Server.
    int first_slot; NetPeer* peers[CRET_MAX_PEERS]; NetSchema sc; NetEnt q[NET_NENT];
    CretNetServerStats sst;
    // Client.
    ENetPeer* server; CretNetsim* netsim;
    bool connected, failed, welcomed; uint32_t reject;
    CretConfig cfg; int slot;
    NetClient* cl;
    uint8_t req_seq; bool reply_ready; CretSpawnReply reply;
};

void netp_schema(NetSchema* sc, float world_size);
void netp_quant_agent(const NetSchema* sc, const CretSim* s, int i, NetEnt* e);
void netp_quant_carcass(const NetSchema* sc, const CretSim* s, int c, NetEnt* e);
// Agent fields from a quantised state; tick_ref turns hurt/death ticks into counters.
void netp_ent_to_agent(const NetEnt* e, CretAgent* a, uint32_t tick_ref);
void netp_ent_to_carcass(const NetEnt* e, CretCarcass* k, uint32_t tick_ref);
// Inputs: 40 bits (v9): forward, strafe and turn as signed bytes, the flags byte
// (sprint, jump, 3-bit action, 2-bit ability slot, top bit 0), the aim as a signed byte (-128 refused).
#define NET_INPUT_BITS 40
uint64_t netp_pack_input(const CretInput* in);
CretInput netp_unpack_input(uint64_t v);
// The packed input is one the server accepts (every head in range).
bool netp_input_valid(uint64_t v);
// Server side: an input packet from peer np (inputs and snapshot acks). False if malformed.
bool netp_server_input(NetPeer* np, const uint8_t* d, size_t len);
// A MSG_REQUEST from the peer: held (replacing a held one), or refused at
// once for an unknown species. Settled each tick by netp_server_settle.
bool netp_server_request(NetPeer* np, const CretSim* s, const uint8_t* d, size_t len);
void netp_server_settle(NetPeer* np, CretSim* s);
// MSG_PROFILE (character name) and MSG_UNLOCK (slot-3 choice) from the peer.
bool netp_server_profile(NetPeer* np, CretSim* s, const uint8_t* d, size_t len);
// Encodes peer np's next snapshot into out (cap bytes); returns the length (0 on failure).
size_t netp_server_snapshot(CretNet* n, NetPeer* np, const CretSim* s, uint8_t* out, size_t cap);
// Client side: one received datagram (welcome or snapshot). False if refused.
bool netp_client_packet(CretNet* n, const uint8_t* d, size_t len, double now);
// Encodes the client's input packet (newest input and the unacknowledged ones before it).
size_t netp_client_input(const NetClient* c, uint8_t* out, size_t cap);
// Fake endpoints without sockets (fuzzing, codec tests).
CretNet* netp_fake_server(int first_slot, float world_size);
NetPeer* netp_fake_peer(CretNet* n, int k, int slot);
CretNet* netp_fake_client(int num_agents, int slot, int world_n, float world_cell);
// Client internals in netclient.c.
NetClient* netp_client_new(const CretConfig* cfg, int slot);
void netp_client_snapshot_arrived(NetClient* c, uint32_t tick, double now);
void netp_client_apply(CretNet* n, CretSim* s);
#endif
