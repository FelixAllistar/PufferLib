// Networking over ENet. The server owns the sim; clients send inputs and
// predict their own animal, and draw everyone else from a snapshot buffer.
// Solo play never touches this. The wire format is an explicit, versioned,
// bounds-checked bit stream; C structs never cross the wire. docs/net.md has
// the protocol, the client model and the netbench results.
#ifndef CRET_NET_H
#define CRET_NET_H
#include "sim.h"
#include "netsim.h"

#define CRET_NET_VERSION 15  // 2: species, action, gait flags and speed per agent; action in inputs. 3: survival stats, carcasses.
                             // 4: respawn/species requests from clients; group, broken-bone flag and rally point per agent
                             // 5: cloud cover (server weather) in snapshots
                             // 6: requests carry a sequence number; the server answers each one it settles (MSG_SPAWN_REPLY)
                             // 7: (cret/net) bit-packed per-client snapshots: quantised, delta-coded against per-entity
                             //    acked baselines, prioritised by distance under a byte budget; full-precision own state
                             //    with the last applied input; sequenced redundant inputs with snapshot acks; version
                             //    checked on connect; v6's held requests and replies in the bit codec.
                             //    (cret/progression also called its byte-codec protocol 7; neither shipped alone.)
                             // 8: v7 plus progression: ability in inputs; status flags and mark per agent; own status
                             //    timers, ability cooldowns, strike and latch at full precision; own progress block
                             //    (marks, unlocks, loadout, quest, group quest) and nests, resent while changing;
                             //    profile (character name) and slot-3 unlock requests
                             // 9: aimed attacks: an 8-bit aim in inputs (40-bit inputs) and the secondary attack
                             //    (action 7); a 6-bit aim per agent (remote heads turn); own secondary cooldown,
                             //    queued attack and aim at full precision
                             // 10: hit events (attacker, victim, kind, damage) near the client in each snapshot, for hit feedback
                             // 11: movable props: a prop block (id, position, rotation, rest) after the hits, acked per
                             //     prop; hit kinds 4 bits (CRET_HIT_CRUSH)
                             // 12: critters: the nearest small prey after the props (critters.h, netcritters.c); a quest
                             //     may lead to a spot (a flag and its x, z and radius) instead of a named place; catch quests
                             // 13: carrying: the carry press in the top bit of the input flags; own carried slot and load;
                             //     per carcass its item kind (body, critter, egg), critter kind and carrier
                             // 14: quests: 4-bit kinds (companion, ability, explore, nest, rest, scavenge, raid, guard) and
                             //     the first-steps step; the progress block adds the first steps done and the places visited
                             // 15: combat feel: the queued attack's ticks grow to 4 bits (CRET_ATTACK_BUFFER is 9 ticks)
#define CRET_DEFAULT_PORT 27480
#define CRET_MAX_PEERS 16
#define CRET_SNAPSHOT_DIV 2  // snapshots every 2 ticks (15 Hz)
#define CRET_NET_REQUEST_COOLDOWN CRET_TICK_HZ        // ticks between two respawns of one client (later ones wait)
#define CRET_NET_COMBAT_LOCK (10 * CRET_TICK_HZ)      // ticks after a hit during which a living client cannot respawn

// How the server settled a respawn request.
enum { CRET_SPAWN_OK, CRET_SPAWN_BAD_SPECIES, CRET_SPAWN_IN_COMBAT };
typedef struct CretSpawnReply {
    int seq;       // the request it answers (cret_net_client_request's return value)
    int result;    // CRET_SPAWN_*
    int species;   // the client's species after the reply
    int wait_s;    // CRET_SPAWN_IN_COMBAT: seconds until the lock lifts
} CretSpawnReply;

typedef struct CretNet CretNet;

// ---------------------------------------------------------------- server
// Server (listen host or dedicated). Peer slots map to agents first_slot.. in join order.
CretNet* cret_net_host(int port, int first_slot);
// Once per tick before cret_sim_step: network events, then each peer's next
// input into inputs[slot] (remote[slot] = true). A peer whose input has not
// arrived is held this tick (sim->hold); queued inputs beyond a small buffer
// are applied at once (cret_sim_move) so latency does not pile up.
void cret_net_server_poll(CretNet* n, CretSim* s, CretInput* inputs, bool* remote);
// After cret_sim_step, every CRET_SNAPSHOT_DIV ticks: one snapshot per peer.
void cret_net_server_broadcast(CretNet* n, const CretSim* s);
typedef struct CretNetServerStats {
    int peers;
    uint64_t ticks, held, caught_up, inputs_lost;   // peer-ticks: total, without input, extra inputs applied, never arrived
    uint64_t snapshots, snapshot_bytes, entities_sent, budget_full;
    uint64_t props_sent, prop_bits, props_full;   // v11: prop states sent, their bits, snapshots that left a prop waiting
} CretNetServerStats;
void cret_net_server_stats(const CretNet* n, CretNetServerStats* st);

// ---------------------------------------------------------------- client
// Connects and waits for the welcome (blocking). The link conditioner is on
// when CRET_NETSIM is set (see netsim.h).
CretNet* cret_net_join(const char* address, int port, float timeout_s);
// Non-blocking connect for tools: progress with cret_net_client_poll until
// cret_net_client_welcomed. netsim NULL reads CRET_NETSIM.
CretNet* cret_net_join_start(const char* address, int port, const CretNetsimCfg* netsim, uint32_t seed);
// After join: world parameters and assigned slot (valid once welcomed).
bool cret_net_client_welcomed(const CretNet* n, CretConfig* cfg, int* slot);
// One client tick (30 Hz): sends the input (with the unacknowledged ones
// before it) and predicts the local animal in `s` through cret_sim_move.
void cret_net_client_tick(CretNet* n, CretSim* s, const CretInput* in);
// Asks the server to respawn this client's animal: as `species`, or as the
// same species when -1. The server picks the spawn point, holds a request
// that comes within a second of the last respawn until the second is up (a
// newer request replaces a held one), and refuses while the animal is alive
// and was hit in the last ten seconds. Returns the request's sequence number.
int cret_net_client_request(CretNet* n, int species);
// The latest reply to a request, once (cret_net_client_poll receives it).
bool cret_net_client_spawn_reply(CretNet* n, CretSpawnReply* out);
// Character name (the server loads and saves that character; progress.c,
// character.c) and buying or equipping slot-3 ability choice 0 or 1.
void cret_net_client_profile(CretNet* n, const char* name);
void cret_net_client_unlock(CretNet* n, int choice);
// Receives snapshots: remote agents and carcasses take their latest state
// (with collision bodies placed), the local animal is reconciled with the
// server (rewind to the acknowledged input, replay the rest). `s` may be
// NULL before the client's sim exists. False when disconnected.
bool cret_net_client_poll(CretNet* n, CretSim* s);
// The copy to draw: remote agents interpolated in the snapshot buffer (an
// adaptive delay of about 100 ms, capped extrapolation), the local animal
// between its last two predicted ticks (alpha) plus a decaying correction
// offset. dt: frame seconds.
void cret_net_client_draw(CretNet* n, const CretSim* s, CretSim* out, float alpha, float dt);
typedef struct CretNetStats {
    double seconds;                             // since the stats were last reset
    uint64_t up_bytes, down_bytes;              // UDP payloads plus 28 bytes of IP/UDP header per datagram
    uint64_t up_packets, down_packets;
    uint64_t snapshots, snapshots_dropped;      // received; refused (malformed, duplicate or a missing baseline)
    uint64_t ack_n; double ack_sum_ms, ack_max_ms; uint32_t ack_hist[200];   // input sent -> applied state received, 5 ms bins
    uint64_t err_n; double err_sum_m, err_max_m; uint32_t err_hist[200];     // predicted vs server position at the acked input, 1 mm bins
    uint64_t corrections, snaps; double corr_sum_m, corr_max_m;              // replays that moved the prediction; snaps (no smoothing)
    uint64_t frames, underruns, ent_frames, ent_extrapolated;               // render frames; frames with no snapshot past the render time
    double delay_ms, rtt_ms;                    // current interpolation delay, ENet round trip
} CretNetStats;
// Runs the link conditioner's relay (if any) without touching ENet; tools
// call it between polls so the simulated network keeps its own timing.
void cret_net_pump(CretNet* n);
void cret_net_client_stats(const CretNet* n, CretNetStats* st);
void cret_net_client_stats_reset(CretNet* n);
void cret_net_close(CretNet* n);
// Percentile (0..1) of a stats histogram in its bin units.
float cret_net_hist_pct(const uint32_t* hist, int bins, float p);
#endif
