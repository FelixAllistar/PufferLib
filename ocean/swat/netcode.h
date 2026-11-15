#ifndef SWAT_NETCODE_H
#define SWAT_NETCODE_H
// Transport-independent multiplayer pieces shared by net.c (ENet) and the
// deterministic network-conditions harness (tests/test_netcond.c). Nothing
// here owns a socket or advances gameplay: authority stays in the server's
// SwatSim, and everything on the client side only shapes presentation.
// See NETWORK.md for the design, wire format and measured results.
#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SWAT_INPUT_QUEUE 32
// Snapshots retained as delta baselines (30 Hz: about one second).
#define SWAT_SNAPSHOT_RING 32
// Ticks of rewind the server grants a remote shooter at most (sim.h).
#define SWAT_LAG_COMP_MAX_TICKS SWAT_LAG_MAX_TICKS

// Feature switches. Production uses swat_net_tuning_default(); the harness
// turns everything off to reproduce the protocol-53 behaviour for its
// before/after tables. Changing a switch never changes server authority.
typedef struct SwatNetTuning {
    bool delta;        // XOR/zero-run snapshot deltas against the client's ack
    bool redundancy;   // every input packet repeats all unacknowledged commands
    bool drain;        // server merges surplus queued commands (latency ratchet)
    bool interpolate;  // client renders remote actors from a jitter buffer
    bool predict;      // client predicts its own body from unacknowledged input
    bool lag_comp;     // server rewinds other actors to the shooter's view
} SwatNetTuning;
SwatNetTuning swat_net_tuning_default(void);
SwatNetTuning swat_net_tuning_legacy(void);

// ---- Server side: per-player command queue -------------------------------
typedef struct SwatRemoteSlot {
    void* peer;
    SwatCommand queue[SWAT_INPUT_QUEUE];
    SwatInput held;
    int head,count;
    uint32_t received,ack,last_receive_ms;
    // Newest snapshot revision this client reported holding (delta baseline).
    uint32_t snapshot_ack;
    // Lag compensation: view tick of the command consumed this tick.
    uint32_t view_tick;
    // Jitter-buffer drain: minimum queue depth over the current window.
    int window_ticks,window_min,drain;
    uint32_t starved,merged,lost;
} SwatRemoteSlot;
void swat_remote_reset(SwatRemoteSlot* remote,uint32_t now_ms);
// Queue a decoded command. Older or duplicate sequences are ignored. A full
// queue folds its two oldest commands together (swat_input_merge).
bool swat_remote_push(SwatRemoteSlot* remote,const SwatCommand* command,bool running,uint32_t now_ms);
// The input the authority executes this tick for that player.
SwatInput swat_remote_take(SwatRemoteSlot* remote,uint32_t now_ms,bool drain);
// Combine two consecutive ticks of input into one, preserving presses.
SwatInput swat_input_merge(const SwatInput* first,const SwatInput* second);

// ---- Snapshot baselines (both ends keep the full encoded bytes) ----------
typedef struct SwatSnapshotRing {
    uint32_t epoch;
    uint32_t revision[SWAT_SNAPSHOT_RING];
    size_t size[SWAT_SNAPSHOT_RING],capacity[SWAT_SNAPSHOT_RING];
    unsigned char* bytes[SWAT_SNAPSHOT_RING];
} SwatSnapshotRing;
void swat_ring_store(SwatSnapshotRing* ring,uint32_t epoch,uint32_t revision,const void* bytes,size_t size);
const unsigned char* swat_ring_find(const SwatSnapshotRing* ring,uint32_t epoch,uint32_t revision,size_t* size);
void swat_ring_free(SwatSnapshotRing* ring);
// Build the state packet for one client: a delta against its acknowledged
// baseline when that is smaller, otherwise a copy of the full snapshot.
size_t swat_snapshot_packet(void* out,size_t capacity,const SwatSnapshotRing* ring,uint32_t epoch,
    uint32_t revision,uint32_t client_ack,bool delta);
// Reconstruct full snapshot bytes from a SNAPSHOT or DELTA packet.
size_t swat_snapshot_expand(void* out,size_t capacity,const SwatSnapshotRing* ring,const void* packet,size_t size);

// ---- Client side: remote-actor interpolation and local prediction --------
#define SWAT_VIEW_FRAMES 32
typedef struct SwatViewActor {
    b3Pos position; b3Vec3 velocity;
    float yaw,pitch;
    bool present,alive;
} SwatViewActor;
typedef struct SwatViewFrame {
    int tick;
    SwatViewActor actors[SWAT_MAX_ACTORS];
} SwatViewFrame;
typedef struct SwatViewPose { b3Pos position; b3Vec3 velocity; float yaw,pitch; bool valid; } SwatViewPose;
typedef struct SwatNetView {
    SwatViewFrame frames[SWAT_VIEW_FRAMES];
    int count,newest_tick;
    uint32_t newest_revision,epoch;
    bool clock_ready;
    double offset_ms;     // min-tracked (arrival - tick*dt): latency + phase
    double jitter_ms;     // 95th percentile of arrival lateness above offset
    double loss;          // EWMA of missing snapshot revisions
    double delay_ms;      // target interpolation delay
    double render_tick;   // fractional server tick being presented
    double last_ms;
    // Presentation continuity per actor: residual error blended away.
    b3Vec3 error[SWAT_MAX_ACTORS];
    b3Pos shown[SWAT_MAX_ACTORS];
    bool shown_valid[SWAT_MAX_ACTORS],extrapolating[SWAT_MAX_ACTORS];
    uint32_t extrapolated_frames,snaps,frames_presented;
} SwatNetView;
void swat_net_view_reset(SwatNetView* view);
void swat_net_view_push(SwatNetView* view,const SwatSnapshot* state,double now_ms);
// Advance the presentation clock to now (call once per rendered frame).
void swat_net_view_advance(SwatNetView* view,double now_ms,bool interpolate);
// Pose of one actor at the current presentation time.
SwatViewPose swat_net_view_sample(SwatNetView* view,int actor,double frame_ms,bool interpolate);

// Client-side kinematic prediction of the local body: replays unacknowledged
// commands from the newest authoritative pose with the controller's ground
// acceleration model, collides against the replica with a read-only capsule
// cast, and hides reconciliation behind an exponentially decaying offset.
typedef struct SwatPredictState {
    b3Pos position; b3Vec3 velocity;
    float yaw,stamina;
    bool crouched,ground;
} SwatPredictState;
typedef struct SwatPredictor {
    SwatActorState base;              // newest authoritative local state
    bool has_base;
    SwatPredictState state;           // prediction through `sequence`
    uint32_t sequence;
    b3Pos position; b3Vec3 offset;
    bool valid;
    uint32_t base_tick;
    float correction_last,correction_sum,correction_max;
    uint32_t corrections;
} SwatPredictor;
void swat_predictor_reset(SwatPredictor* p);
// Record the local actor's state from each applied snapshot.
void swat_predictor_base(SwatPredictor* p,const SwatActorState* state);
// Rebuild the prediction from the newest authoritative local state plus the
// pending commands (oldest first); the replica supplies colliders only.
// frame_dt decays earlier corrections.
b3Pos swat_predict_local(SwatPredictor* p,const SwatSim* replica,int actor,
    const SwatCommand* pending,int head,int count,int ring,float frame_dt);

#ifdef __cplusplus
}
#endif
#endif
