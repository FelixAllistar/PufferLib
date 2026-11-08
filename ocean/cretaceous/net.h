// Networking over ENet. The server owns the sim; clients send inputs and
// predict their own animal, and draw everyone else from a snapshot buffer.
// Solo play never touches this. The wire format is an explicit, versioned,
// bounds-checked bit stream; C structs never cross the wire. docs/net.md has
// the protocol, the client model and the netbench results.
#ifndef CRET_NET_H
#define CRET_NET_H
#include "sim.h"
#include "netsim.h"

#define CRET_NET_VERSION 6   // 2: species, action, gait flags and speed per agent; action in inputs. 3: survival stats, carcasses.
                             // 4: respawn/species requests from clients; group, broken-bone flag and rally point per agent
                             // 5: cloud cover (server weather) in snapshots
                             // 6: bit-packed per-client snapshots: quantised, delta-coded against per-entity acked
                             //    baselines, prioritised by distance under a byte budget; full-precision own state with
                             //    the last applied input; sequenced redundant inputs with snapshot acks; version
                             //    checked on connect
#define CRET_DEFAULT_PORT 27480
#define CRET_MAX_PEERS 16
#define CRET_SNAPSHOT_DIV 2  // snapshots every 2 ticks (15 Hz)

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
// same species when -1 (the server ignores requests faster than one a second).
void cret_net_client_request(CretNet* n, int species);
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
