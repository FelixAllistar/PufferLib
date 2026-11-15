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
    // Prediction pass (NETWORK.md; client/server behaviour, wire unchanged):
    bool controller;   // predict with the server's own controller/physics step
    bool starve_decay; // starved server ticks decay held movement to neutral
    bool view_state;   // remote stance/lean/animation state and door leaves interpolated
    bool lag_doors;    // lag-compensated hitscan also rewinds door leaves
} SwatNetTuning;
SwatNetTuning swat_net_tuning_default(void);
SwatNetTuning swat_net_tuning_legacy(void);
// The first netcode pass (protocol 54 as merged): default with the
// prediction-pass switches off. The harness's middle column.
SwatNetTuning swat_net_tuning_pass1(void);

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
    int starve_run;    // consecutive starved ticks (decay policy)
} SwatRemoteSlot;
void swat_remote_reset(SwatRemoteSlot* remote,uint32_t now_ms);
// Queue a decoded command. Older or duplicate sequences are ignored. A full
// queue folds its two oldest commands together (swat_input_merge).
bool swat_remote_push(SwatRemoteSlot* remote,const SwatCommand* command,bool running,uint32_t now_ms);
// The input the authority executes this tick for that player.
SwatInput swat_remote_take(SwatRemoteSlot* remote,uint32_t now_ms,bool drain);
// As above; decay selects the prediction-pass starvation policy (netcode.c).
SwatInput swat_remote_take_policy(SwatRemoteSlot* remote,uint32_t now_ms,bool drain,bool decay);
// Share of held movement kept per consecutive starved tick under decay.
extern float swat_starve_decay;
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
// Door leaves interpolated per view (prediction pass); doors past this present on arrival.
#define SWAT_VIEW_DOORS 96
typedef struct SwatViewActor {
    b3Pos position; b3Vec3 velocity;
    float yaw,pitch;
    bool present,alive;
    // Animation-relevant state (prediction pass, tuning.view_state).
    bool crouched,grounded,sprinting;
    b3Vec3 upper_offset;
    float eye_height,lean,ads,ready_blend;
    SwatMantle mantle;
} SwatViewActor;
typedef struct SwatViewDoor { float angle; b3Pos position; b3Quat rotation; bool moving,active; } SwatViewDoor;
typedef struct SwatViewFrame {
    int tick;
    SwatViewActor actors[SWAT_MAX_ACTORS];
    SwatViewDoor doors[SWAT_VIEW_DOORS];
} SwatViewFrame;
typedef struct SwatViewPose {
    b3Pos position; b3Vec3 velocity; float yaw,pitch; bool valid;
    // Stance/animation at the presented time: discrete fields from the frame
    // at or before it, continuous ones interpolated (stance height eases
    // across one snapshot interval instead of popping on arrival).
    bool state,crouched,grounded,sprinting;
    float height_blend;     // 0 standing .. 1 crouched
    b3Vec3 upper_offset;
    float eye_height,lean,ads,ready_blend;
    SwatMantle mantle;
} SwatViewPose;
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
    // Door objects of the bound map (swat_net_view_bind); kept across resets.
    int door_count,door_object[SWAT_VIEW_DOORS];
} SwatNetView;
// Record which replica objects are doors so their leaves can be interpolated.
void swat_net_view_bind(SwatNetView* view,const SwatSim* replica);
void swat_net_view_reset(SwatNetView* view);
void swat_net_view_push(SwatNetView* view,const SwatSnapshot* state,double now_ms);
// Advance the presentation clock by one rendered frame (frame_ms), steering
// toward now_ms - offset - delay with a bounded time warp.
void swat_net_view_advance(SwatNetView* view,double now_ms,double frame_ms,bool interpolate);
// Pose of one actor at the current presentation time.
SwatViewPose swat_net_view_sample(SwatNetView* view,int actor,double frame_ms,bool interpolate);
// Fill the stance/animation fields of a sampled pose (prediction pass).
void swat_net_view_sample_state(const SwatNetView* view,int actor,SwatViewPose* pose);
// Presentation only: write a sampled pose into a replica actor (position,
// velocity, aim and, when pose->state, stance/lean/animation state). The next
// applied snapshot overwrites all of it.
void swat_net_view_present_actor(SwatSim* replica,int actor,const SwatViewPose* pose);
// Presentation only: pose the bound door leaves at the presented time.
void swat_net_view_present_doors(SwatNetView* view,SwatSim* replica);

// Client-side prediction of the local body. The prediction pass (tuning.controller)
// keeps a private copy of the world (`world`, built from the same map and
// overwritten by every authoritative snapshot) and replays the unacknowledged
// commands through swat_sim_step_movement: the server's own controller,
// step-up, clamber, door and Box3D contact code, so step-ups, clambers,
// shoves and door pushes are predicted rather than corrected. Without a world
// (or with the switch off) it falls back to the protocol-54 kinematic ground
// model with a read-only capsule cast. Either way reconciliation differences
// join an exponentially decaying offset. Nothing here reaches the authority.
typedef struct SwatPredictState {
    b3Pos position; b3Vec3 velocity;
    float yaw,stamina;
    bool crouched,ground;
} SwatPredictState;
#define SWAT_PREDICT_RING 256
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
    // Shared-controller prediction (prediction pass).
    bool shared;                      // use `world` when it exists
    struct SwatSim* world;            // private prediction replica (owned)
    struct SwatMap* map;              // its map, kept to rebuild after a refused snapshot
    int actor;                        // local actor in `world`
    uint32_t ack;                     // newest command the base includes
    bool rebased;                     // a snapshot was applied since the last replay
    uint32_t ring_seq[SWAT_PREDICT_RING];
    b3Pos ring_pos[SWAT_PREDICT_RING];
    SwatInput ring_input[SWAT_PREDICT_RING];
    b3Pos last_raw; bool last_raw_valid; // presentation continuity (step smoothing)
    uint32_t replays,steps,rebuilds,smoothed;
} SwatPredictor;
// Frees the private world; the predictor is then empty.
void swat_predictor_reset(SwatPredictor* p);
// Build (or rebuild) the private prediction world from a map.
bool swat_predictor_map(SwatPredictor* p,const SwatMap* map);
// Record the local actor's state from each applied snapshot.
void swat_predictor_base(SwatPredictor* p,const SwatActorState* state);
// Prediction pass: record the whole applied snapshot. It is applied to the
// private world (the base for the next replay) and `ack` is the newest local
// command it includes. Also records the base (swat_predictor_base).
void swat_predictor_snapshot(SwatPredictor* p,const SwatSnapshot* state,int actor,uint32_t ack);
// Rebuild the prediction from the newest authoritative local state plus the
// pending commands (oldest first); the replica supplies colliders only.
// frame_dt decays earlier corrections; alpha in [0,1] is the elapsed share of
// the tick after the newest command (the pacer phase), blending the last two
// predicted ticks.
b3Pos swat_predict_local(SwatPredictor* p,const SwatSim* replica,int actor,
    const SwatCommand* pending,int head,int count,int ring,float frame_dt,float alpha);
// The predicted local controller (stance, lean, eye height) after the newest
// replayed command, or NULL when the shared-controller path is not active.
const SwatController* swat_predictor_controller(const SwatPredictor* p);

#ifdef __cplusplus
}
#endif
#endif
