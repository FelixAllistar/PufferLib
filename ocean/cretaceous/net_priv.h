// Internals shared by net.c (codec, server, client transport), netclient.c
// (prediction, reconciliation, interpolation) and tools/netfuzz.c.
#ifndef CRET_NET_PRIV_H
#define CRET_NET_PRIV_H
#include "net.h"
#include <enet/enet.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

// MSG_REQUEST (client, reliable): 8-bit seq, 8-bit species (255: the same).
// MSG_SPAWN_REPLY (server, reliable, once per settled request): 8-bit seq,
// CRET_SPAWN_* result, species and wait_s, 8 bits each.
// MSG_PROFILE (client, reliable): 5-bit length (<= 23), that many 8-bit
// characters: the character name (the server loads and saves it).
// MSG_UNLOCK (client, reliable): 1 bit, slot-3 ability choice to buy or equip.
// MSG_LOADING (client, reliable, v20; netjoin.c): 1 bit done, the stage's
// fraction in 8 bits, then the stage name: 5-bit length (<= 31) and its
// 8-bit characters. Twice a second while the client loads the world.
enum { MSG_WELCOME = 2, MSG_INPUT, MSG_SNAPSHOT, MSG_REQUEST, MSG_SPAWN_REPLY, MSG_PROFILE, MSG_UNLOCK, MSG_LOADING };
enum { REQ_NONE = -2, REQ_SAME = -1 };   // NetPeer.pending: no request held; respawn as the same species (>= 0: a species)
enum { CH_RELIABLE, CH_STATE, CH_COUNT };
#define NET_REJECT_VERSION 0x56000000u   // disconnect data: 'V' << 24 | server version
#define NET_REJECT_LOAD 0x4c000000u      // disconnect data: 'L' << 24 | seconds the server waited for the load (netjoin.c)

#define NET_NENT (CRET_MAX_AGENTS + CRET_MAX_CARCASSES)   // entity ids: agents, then carcasses
#define NET_CARCASS0 CRET_MAX_AGENTS
#define NET_NF 19            // quantised fields per entity
#define NET_HIST 64          // snapshots both ends keep per entity (baselines, acks)
#define NET_REORDER 32       // a client still takes a snapshot this much older than its newest
#define NET_MAX_AGE (NET_HIST - NET_REORDER)   // baselines are younger (5-bit age), so a late snapshot's baseline is still kept
#define NET_IN_RING 128      // inputs: client history, server queue
#define NET_REDUNDANCY 32    // most inputs in one packet (all unacknowledged ones up to this)
#define NET_INTERP 16        // samples per agent in the interpolation buffer
#define NET_SNAP_BUDGET 1200 // snapshot payload bytes (one datagram under ENet's 1392 MTU)
#define NET_BUFFER_MAX 4     // queued inputs beyond this are applied at once
// v18 lag compensation: after each input that presses an attack or an
// ability (net_press), a presence bit, then the client's view: the server
// tick its remote agents were drawn at, as a 32-bit tick and NET_VIEW_FRAC
// bits of fraction. Other inputs carry nothing more.
#define NET_VIEW_FRAC 4
static inline bool net_press(const CretInput* in) { return cret_act_is_attack(in->action) || in->ability; }
// v10 hit events: each snapshot repeats the hits of the last NET_HIT_WINDOW
// ticks (so one lost snapshot loses none) that involve the client or land
// within NET_HIT_RANGE m of it, at most NET_HIT_MAX; the client drops repeats.
// Per hit: age in ticks before the snapshot (7 bits), attacker and victim (7
// each), kind (4 since v11: CRET_HIT_*), damage (8: fraction of max health, >= 1).
#define NET_HIT_WINDOW (2 * CRET_SNAPSHOT_DIV + 2)
#define NET_HIT_RANGE 160.0f
#define NET_HIT_MAX 24
#define NET_HIT_KIND_BITS 4
// v11 movable props (dynprops.c, netprops.c): after the hits, each snapshot
// carries up to NET_PROP_MAX props within NET_PROP_BUDGET bytes: those whose
// pose differs from the one the client acknowledged, by accumulated interest
// (as entities). Per prop: id (NET_PROP_ID_BITS), a gone bit (the server
// dropped its record: the prop is back at its scattered pose), else x and z
// (pos_bits), y (21, from -2048 m) at 1/256 m, the rotation as smallest three
// (2-bit index, 3 x 10 bits) and a rest bit. Props at rest and acknowledged
// cost nothing.
#define NET_PROP_BUDGET 300
#define NET_PROP_MAX 31          // 5-bit count
#define NET_PROP_ID_BITS 20
#define NET_PROPS (2 * CRET_MAX_DYN)   // server: props each peer knows or is being told about
#define NET_PROP_SAMP 8          // client: samples per prop (interpolation)
#define NET_PROP_TOMBS 1024      // client: recent gone props (>= NET_PROP_MAX x (NET_REORDER + 1))
// v12 critters (critters.c, netcritters.c): after the props, the peer's
// NET_CRIT_MAX nearest critters within NET_CRIT_RANGE m, whole every
// snapshot: a count (5 bits), a reference point (whole metres, 16 bits each
// for x and z), then per critter its pool slot (9), kind (3), state (3), x
// and z offsets from the reference at 1/16 m (12 signed each), heading (6),
// height above the ground at 0.5 m (4), mass within its kind's range (6) and
// speed at 0.5 m/s (4): 59 bits, at most 177 bytes.
#define NET_CRIT_MAX 24
#define NET_CRIT_COUNT_BITS 5
#define NET_CRIT_SLOT_BITS 9     // CRET_MAX_CRITTERS <= 512
#define NET_CRIT_RANGE 80.0f
#define NET_CRIT_Q 16.0f
// v23 area of interest (netaoi.c, docs/anticheat.md): an entity is sent to a
// client only while the client's animal could plausibly perceive it now or
// within NET_AOI_LEAD_S: within NET_AOI_NEAR m always; by sight out to a
// range that grows with the target's length (NET_AOI_SIGHT_*), scaled by
// the viewer's species at night, the weather's haze and a head wound, and
// only with a clear line over the terrain from the highest place the
// camera can be; by hearing (movement, calls, fights); by scent for
// carnivores; and always for its group, the animals marked for it, its
// quest companion and what it latches onto or carries. It stays relevant
// out to NET_AOI_KEEP times that (and a looser sight line) and leaves only
// after NET_AOI_LINGER s outside, so nothing pops at the edge. Each snapshot
// lists the entities the client may still hold that left (8-bit count, then
// 8-bit ids, before the entities), until a snapshot carrying the leave is
// acknowledged; the client drops them.
#define NET_AOI_NEAR 60.0f          // m: always relevant (the camera's orbit, prediction's collisions)
#define NET_AOI_SIGHT_MIN 150.0f    // m: daylight sight of an animal of no length ...
#define NET_AOI_SIGHT_PER_L 40.0f   // ... plus this per metre of its length ...
#define NET_AOI_SIGHT_MAX 700.0f    // ... at most (the renderer draws animals to 900 m, through haze)
#define NET_AOI_NIGHT_DIURNAL 0.5f  // night: a diurnal viewer sees this share as far (ECO_NIGHT_SIGHT) ...
#define NET_AOI_NIGHT_NOCTURNAL 0.8f // ... a raptor or tyrannosaur this share
#define NET_AOI_HAZE 0.5f           // full overcast or rain takes this share of the sight range away
#define NET_AOI_HEAR 1.5f           // a person hears this many times as far as a bot (cret_eco_hear_m, ECO_CALL_M)
#define NET_AOI_FIGHT_S 3.0f        // s an animal stays loud after a blow it dealt or took (heard out to NET_HIT_RANGE)
#define NET_AOI_LEAD_S 0.75f        // s of both animals' motion added to every range (prediction, the round trip)
#define NET_AOI_KEEP 1.15f          // a relevant entity stays so out to this times its range ...
#define NET_AOI_KEEP_M 10.0f        // ... plus this
#define NET_AOI_LOS_ENTER 0.5f      // m the terrain may rise over the sight line before it hides an entity coming in ...
#define NET_AOI_LOS_KEEP 3.0f       // ... and one already relevant
#define NET_AOI_LINGER 1.0f         // s an entity stays outside before it leaves
typedef struct {
    uint8_t in[NET_NENT];     // relevant now (sent; 1 for every entity with the rule off)
    uint8_t seen[NET_NENT];   // in plain sight now: within the sight range with a clear line, no margins (telemetry)
    int32_t out_tick[NET_NENT];   // the tick it was first found outside while relevant (-1: inside)
    int32_t tick;             // the tick these were worked out at (-1: never)
} NetAoi;

// Server checks (netcheck.c, docs/anticheat.md): what an unmodified client
// never sends, counted per peer, logged, and with enforce refused.
#define NET_AC_RATE_SLACK 0.005f    // input clock: at most this much faster than the server's ...
#define NET_AC_RATE_TICKS 45        // ... plus this many ticks
#define NET_AC_VIEW_AHEAD 3         // ticks a press's view may be past the server's present
#define NET_AC_VIEW_BEHIND 60       // ticks it may be behind the newest snapshot the client had
#define NET_AC_FLOOD_S 5            // s windows for the packet rate ...
#define NET_AC_FLOOD_RATE 2.5f      // ... at most this many input packets per tick on average
#define NET_AC_REPORT_S 10          // s between two log lines about one peer and kind

// Telemetry (nettelem.c, docs/anticheat.md): record types of the binary log.
enum { NET_TL_JOIN = 1, NET_TL_LEAVE, NET_TL_INPUT, NET_TL_VIS, NET_TL_WORLD, NET_TL_SWING, NET_TL_APPEAR, NET_TL_ACK, NET_TL_FLAG };
#define NET_TL_MAGIC "CRETTEL1"
#define NET_TL_FORMAT 1
#define NET_TL_MASK ((NET_NENT + 7) / 8)   // bytes of an entity mask

typedef struct { uint32_t x, y, z, q; uint8_t rest, gone; } NetPropQ;
typedef struct { int32_t id; NetPropQ cur, ack, last; uint32_t ack_seq, last_seq; float prio; uint8_t live; } NetPropEnt;
typedef struct { int32_t id; NetPropQ q; } NetPropSent;
typedef struct { uint32_t tick, seq; NetPropQ q; } NetPropSample;
typedef struct { int32_t id; int ns; uint32_t seq; bool dirty; NetPropSample s[NET_PROP_SAMP]; } NetPropCl;

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
enum { AF_FLAGS, AF_KIND, AF_X, AF_Y, AF_Z, AF_YAW, AF_SPEED, AF_HEALTH, AF_GROWTH, AF_HURT, AF_DEAD, AF_GROUP, AF_RALLYX, AF_RALLYZ, AF_STATUS, AF_MARK, AF_AIM, AF_DOWN, AF_WOUND, AF_N };
enum { CF_KIND, CF_X, CF_Y, CF_Z, CF_YAW, CF_GROWTH, CF_MEAT, CF_BORN, CF_AGENT, CF_CARRIER, CF_N };
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
// v22: the same per snapshot at a snapshot interval of iv ticks. A faster
// rate sends what is within 40 m every snapshot and keeps every farther band
// at its rate per second, so the extra bandwidth goes to what is near. (The
// near band must stay at 1: scaled like the others it went out only every
// other snapshot at 30 Hz, and the client drew near animals a tick further
// back than the view its presses carry.)
static inline float netp_interest_at(float d, float iv) {
    float r = netp_interest(d);
    return r >= 1 ? 1 : fminf(r * iv / CRET_SNAPSHOT_DIV, 1);
}

// The local player's own state at full precision (prediction replays from it).
typedef struct {
    bool alive, on_ground, swimming, sprinting;
    uint8_t act, act_ticks, attack_cd, hurt_ticks, food, water;
    uint8_t fat;   // v15: the fat reserve (the HUD's line under the food bar)
    uint8_t graze; // v16: biomass of the plant cell at the mouth (the HUD's graze prompt, eco.h)
    float x, y, z, yaw, vx, vy, vz, speed, stamina, broken, growth;
    // Abilities and statuses (v8): what movement and the HUD read.
    uint8_t strike, latch, latchers, leap; uint16_t nest_ticks;
    float slow_amt, def_speed, leap_t, st[CRET_ST_COUNT], ab_cd[CRET_AB_SLOTS];
    // Aimed attacks (v9): secondary cooldown, the queued attack, the aim (exact: inputs carry it in 1/127).
    uint8_t attack2_cd, buf_act, buf_ticks; float aim;
    // Carrying (v13): carcass slot + 1 and the load (movement reads it).
    uint8_t carry; float carry_kg;
    // Knockdown (v19): ticks lying, getting up and before it can be knocked down again; the fall's length and side (movement reads them).
    int down_ticks, getup_ticks, kd_immune, down_len, down_side;
    // Wounds (v20): severity by place, exact (movement reads the leg's).
    float wound[CRET_WOUND_PLACES];
} NetSelf;

// The local player's progress (v8): marks, unlocks, loadout, quests; first steps done and places visited (v13).
typedef struct {
    int marks, quests_done; uint32_t unlocked; uint8_t loadout[CRET_AB_SLOTS];
    CretQuest quest, group; bool has_group;
    uint8_t chain; uint32_t visited;
} NetProg;
#define NET_BLOCK_RESEND 8   // progress and nest blocks: sent this many snapshots after a change
#define NET_BLOCK_REFRESH 15 // ... and every this many snapshots regardless

typedef struct NetPeer {
    ENetPeer* peer; int slot;
    // Inputs by sequence (client tick); applied: the last one the sim used.
    CretInput in[NET_IN_RING]; uint32_t in_seq[NET_IN_RING];
    // v18: each press's view (render tick, -1: none) and the tick of the
    // newest snapshot the client had acknowledged when the press first came (0: unknown).
    double in_view[NET_IN_RING]; uint32_t in_known[NET_IN_RING];
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
    uint32_t sent_seq[NET_HIST], sent_tick[NET_HIST]; uint8_t sent_acked[NET_HIST];
    // v11 props: what the client acknowledged and was sent, by prop (index: id hash).
    NetPropEnt pe[NET_PROPS]; int npe; int16_t pidx[2 * NET_PROPS];
    NetPropSent psent[NET_HIST][NET_PROP_MAX]; uint8_t npsent[NET_HIST];
    // v20 join (netjoin.c): whether the client is loading the world (NET_LOAD_*),
    // since which tick, and the stage and fraction it last reported.
    int loading, load_tick; char load_stage[32]; float load_frac;
    // v23 area of interest: entities the client may hold (sent since their
    // last acknowledged leave), the snapshot each one's pending leave first
    // went out in (0: none), the seq before which acks no longer move its
    // baseline (its last leave), and which entities each recent snapshot told to leave.
    uint8_t aoi_has[NET_NENT]; uint32_t aoi_left[NET_NENT], aoi_cut[NET_NENT];
    uint8_t sent_leave[NET_HIST][NET_NENT];
    // Checks (netcheck.c): the tick it joined, input packets in the current
    // window and when it began, a profile already named, and per kind the
    // count and the tick of the last log line.
    int join_tick, win_tick, win_packets; bool named;
    uint32_t ac_count[CRET_AC_KINDS]; int ac_said[CRET_AC_KINDS];
    // The newest input packet (telemetry): its ack, that snapshot's tick (0: unknown), newest input and count.
    uint32_t rx_ack, rx_known, rx_newest; uint8_t rx_count;
    bool rx_bad_byte, rx_ack_future;   // ... and what netcheck.c looks at: a movement byte of -128, an ack past the newest snapshot sent
    uint32_t player;   // telemetry: the connection's player id (the n-th join since the server started)
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
    // v10 hits: new ones waiting for netp_client_apply, and the recent ones seen (repeats dropped).
    CretHit hit_new[CRET_HIT_LOG]; int nhit_new; CretHit hit_seen[CRET_HIT_LOG]; uint32_t nhit_seen;
    // v11 props: samples by prop, and the ids the server dropped (for netp_client_apply).
    // Gone props are remembered (tomb) so a late snapshot cannot bring one back.
    NetPropCl props[NET_PROPS]; int nprops; int32_t pgone[NET_PROP_TOMBS]; int npgone;
    int32_t tomb_id[NET_PROP_TOMBS]; uint32_t tomb_seq[NET_PROP_TOMBS]; uint32_t ntomb;
    // v23 area of interest: the snapshot each entity last left in (its updates
    // from older snapshots are no longer drawn).
    uint32_t left_seq[NET_NENT];
    // v12 critters: the newest set the server sent (pool slots and states), for netp_client_apply.
    CretCritter crit[NET_CRIT_MAX]; uint16_t crit_slot[NET_CRIT_MAX]; int ncrit; uint32_t crit_seq; bool crit_new;
    // v22: ticks between the server's snapshots (its rate, from the welcome).
    float snap_iv;
    // Server clock estimate (ticks) and the interpolation delay (ticks).
    bool clock_ok, in_underrun; double clock_off, jitter, delay, boost;
    // Prediction: inputs and predicted agent after each, by sequence.
    uint32_t seq, acked;
    CretInput in[NET_IN_RING]; double in_time[NET_IN_RING];
    double in_view[NET_IN_RING], view;   // v18: the view each press carries; the render tick of the last draw (-1: none yet)
    CretAgent pred[NET_IN_RING]; uint32_t pred_seq[NET_IN_RING];
    bool pred_ok; float off[3], off_yaw;
    // The predicted lunge (netp_client_predict): per input the render tick
    // the server judges its swing's targets at (in_rv, -1: the present), as
    // netp_take_view sets it from the press and keeps it for the inputs
    // after; and the drawn poses around it, which the client sim rewinds to (sim.h).
    double in_rv[NET_IN_RING]; CretRewind seen;
    // The newest reconciliation, for tools/netbench.c: how many so far, the
    // input it acknowledged, the prediction error there (-1: nothing to
    // compare) and the shift of the drawn animal it smoothed in (0: none).
    uint32_t rec_n, rec_ack; float rec_err_m, rec_shift_m;
    // Stats since the last reset.
    CretNetStats st; double t0; uint64_t base_up, base_down, base_up_pk, base_down_pk;
    // A predicted slot-3 choice (cret_net_client_choose): choice + 1 (0: none) and the server tick it was made at.
    int choice; uint32_t choice_tick;
} NetClient;

struct CretNet {
    ENetHost* host;
    // Server.
    int first_slot; NetPeer* peers[CRET_MAX_PEERS]; NetSchema sc; NetEnt q[NET_NENT];
    CretRewind rewind;   // v18: the pose history and each slot's rewind (sim.h), the sim's s->rewind
    int snap_hz;         // v22: snapshots per second (cret_net_set_snapshot_hz)
    bool aoi_on;         // v23: the area of interest limits what each client is sent (cret_net_set_aoi)
    NetAoi aoi[CRET_MAX_AGENTS];   // per viewer slot: what it could perceive (netaoi.c)
    int checks;          // CRET_AC_OFF, CRET_AC_LOG or CRET_AC_ENFORCE (cret_net_set_checks)
    // Telemetry (nettelem.c): the log, the slots logged without a client
    // (bots, policies) and the joins so far (player ids).
    FILE* tl; uint8_t tl_slot[CRET_MAX_AGENTS]; uint32_t tl_players;
    CretNetServerStats sst;
    // Client.
    ENetPeer* server; CretNetsim* netsim;
    bool connected, failed, welcomed; uint32_t reject;
    CretConfig cfg; int slot;
    NetClient* cl;
    uint8_t req_seq; bool reply_ready; CretSpawnReply reply;
    // v20 join (netjoin.c): the thread that services the connection while the
    // world loads, and whether the connection was lost meanwhile.
    pthread_t load_thread; bool load_on; atomic_int load_stop, load_lost;
};

void netp_schema(NetSchema* sc, float world_size);
void netp_quant_agent(const NetSchema* sc, const CretSim* s, int i, NetEnt* e);
void netp_quant_carcass(const NetSchema* sc, const CretSim* s, int c, NetEnt* e);
// Agent fields from a quantised state; tick_ref turns hurt/death ticks into counters.
void netp_ent_to_agent(const NetEnt* e, CretAgent* a, uint32_t tick_ref);
void netp_ent_to_carcass(const NetEnt* e, CretCarcass* k, uint32_t tick_ref);
// Inputs: 40 bits (v9): forward, strafe and turn as signed bytes, the flags byte
// (sprint, jump, 3-bit action, 2-bit ability slot, the carry press as the top bit since v13), the aim as
// a signed byte (-128 refused).
#define NET_INPUT_BITS 40
uint64_t netp_pack_input(const CretInput* in);
CretInput netp_unpack_input(uint64_t v);
// The packed input is one the server accepts (every head in range).
bool netp_input_valid(uint64_t v);
// Server side: an input packet from peer np (inputs, their views and snapshot acks). False if malformed.
bool netp_server_input(NetPeer* np, const uint8_t* d, size_t len);
// v18: peer np's input seq is applied at this tick: a press sets the slot's
// rewind (n->rewind.back) from its view, validated (cret_net_server_poll).
void netp_take_view(CretNet* n, const NetPeer* np, const CretSim* s, uint32_t seq);
// A MSG_REQUEST from the peer: held (replacing a held one), or refused at
// once for an unknown species. Settled each tick by netp_server_settle.
bool netp_server_request(NetPeer* np, const CretSim* s, const uint8_t* d, size_t len);
void netp_server_settle(NetPeer* np, CretSim* s);
// MSG_PROFILE (character name) and MSG_UNLOCK (slot-3 choice) from the peer.
bool netp_server_profile(NetPeer* np, CretSim* s, const uint8_t* d, size_t len);
// v20 join (netjoin.c): a MSG_LOADING from the peer. False if malformed.
bool netp_server_loading(NetPeer* np, const CretSim* s, const uint8_t* d, size_t len);
// Each tick for each peer: true while it loads the world (its animal is held,
// it gets no snapshots); drops it once it has loaded for longer than the limit.
bool netp_server_join_wait(CretNet* n, NetPeer* np, CretSim* s, CretInput* inputs, bool* remote);
// Encodes a MSG_LOADING; returns the length.
size_t netp_client_loading(uint8_t* out, size_t cap, bool done, const char* stage, float frac);
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
// Predicts the local animal through input h (c->in[h]) with its swing's
// targets where the client drew them (c->in_rv[h], c->seen).
void netp_client_predict(NetClient* c, CretSim* s, int h);
void netp_client_snapshot_arrived(NetClient* c, uint32_t tick, double now);
void netp_client_apply(CretNet* n, CretSim* s);
// v10 hit block: written by the server for the client in slot `slot`; read
// (range-checked against num_agents) into hits, returning the count.
// rel: the entities relevant to that client (NULL: all); a hit goes out
// only when its attacker and victim are each the client or relevant.
void netp_put_hits(Bits* b, const CretSim* s, int slot, const uint8_t* rel);
int netp_get_hits(Bits* b, CretHit* hits, uint32_t tick, int num_agents);
// v11 prop block (netprops.c): the server writes peer np's (within limit
// bits) and records it in the peer's history for seq; the client reads one
// (validated; ids < 2^NET_PROP_ID_BITS) into out, returning the count or -1.
void netp_put_props(Bits* b, CretNet* n, NetPeer* np, const CretSim* s, uint32_t seq, size_t limit);
int netp_get_props(Bits* b, const NetSchema* sc, NetPropSent* out);
void netp_props_acked(NetPeer* np, int h, uint32_t seq);
// Client: an accepted snapshot's props; applying them to the sim; drawing them at render tick R.
void netp_client_props(NetClient* c, const NetPropSent* p, int n, uint32_t tick, uint32_t seq);
void netp_client_apply_props(NetClient* c, CretSim* s);
// v12 critter block (netcritters.c): the server writes peer `slot`'s nearest
// critters; the client reads them (count; refused on bad slots, kinds or
// states) and puts them in its sim.
void netp_put_critters(Bits* b, const CretSim* s, int slot);
int netp_get_critters(Bits* b, CretCritter* out, uint16_t* slots);
void netp_client_apply_critters(NetClient* c, CretSim* s);
void netp_client_draw_props(NetClient* c, CretSim* out, double R);
void netp_prop_quant(const NetSchema* sc, const float p[3], const float q[4], bool rest, NetPropQ* o);
void netp_prop_pose(const NetPropQ* o, float p[3], float q[4]);
// v23 area of interest (netaoi.c): works out what viewer slot i could
// perceive at this tick into n->aoi[i] (once a tick), and forgets it (a new
// player in the slot). With the rule off every entity is relevant.
void netp_aoi_update(CretNet* n, const CretSim* s, int i);
void netp_aoi_reset(CretNet* n, int i);
// The rule for one entity: could viewer i perceive entity e (agent, or
// NET_CARCASS0 + carcass) now? keep: the looser test of one already
// relevant. *seen: in plain sight (no margins); *far: twice past every
// range (it leaves at once: a respawn far off is not shown). Tools measure with it.
bool netp_aoi_judge(const CretSim* s, int i, int e, bool keep, bool* seen, bool* far);
// Checks (netcheck.c): counts and logs one finding of `kind` about a peer;
// the checks on an input packet just parsed (netp_server_input), on the
// peer's input clock each tick (true: an extra input now would run it
// ahead of real time, which ENFORCE refuses), and on a character name.
void netp_check(CretNet* n, NetPeer* np, int kind, int tick, float value);
void netp_check_packet(CretNet* n, NetPeer* np, const CretSim* s);
bool netp_check_clock(CretNet* n, NetPeer* np, const CretSim* s, uint32_t next);
void netp_check_profile(CretNet* n, NetPeer* np, CretSim* s, const uint8_t* d, size_t len);
// Telemetry (nettelem.c): the records the server writes as it goes.
void netp_tl_join(CretNet* n, int slot, uint32_t player, bool human, const CretSim* s);
void netp_tl_leave(CretNet* n, int slot, uint32_t player, const CretSim* s);
void netp_tl_input(CretNet* n, const CretSim* s, int slot, uint32_t seq, const CretInput* in, int how, double view);
void netp_tl_ack(CretNet* n, const CretSim* s, NetPeer* np, int rtt_ms, int qdepth);
void netp_tl_flag(CretNet* n, int tick, int slot, int kind, uint32_t count, float value);
void netp_tl_tick(CretNet* n, const CretSim* s);
void netp_tl_snapshot(CretNet* n, const CretSim* s);
#endif
