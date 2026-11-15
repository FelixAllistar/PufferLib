#ifndef SWAT_NET_H
#define SWAT_NET_H
#include "netcode.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SWAT_DEFAULT_PORT 27474
typedef enum SwatNetStatus { SWAT_NET_OFFLINE,SWAT_NET_CONNECTING,SWAT_NET_ACTIVE,SWAT_NET_FAILED } SwatNetStatus;
typedef struct SwatNetServer {
    void* transport;
    SwatSim* sim;
    SwatRemoteSlot slots[SWAT_MAX_PLAYERS];
    uint32_t epoch,revision,pulses;
    unsigned int player_mask;
    int local_slot,port;
    SwatNetTuning tuning;
    SwatSnapshotRing ring;           // encoded snapshots: delta baselines
    unsigned char *full,*packet;     // SWAT_NET_PACKET_MAX scratch buffers
    uint64_t bytes_sent;
    char error[160];
} SwatNetServer;
typedef struct SwatNetClient {
    void* transport;
    void* peer;
    SwatSim* replica;
    SwatNetStatus status;
    uint32_t epoch,revision,sequence,ack,started_ms,last_packet_ms,sound_floor;
    int slot,actor,leader_slot,ping_ms;
    unsigned int player_mask;
    bool has_map;
    SwatCommand pending[128];
    int pending_head,pending_count;
    float pending_yaw,pending_pitch;
    // Netcode (NETWORK.md). view_tick is stamped on each command for lag
    // compensation; the view interpolates remote actors and the predictor
    // presents the local body ahead of acknowledged authority.
    SwatNetTuning tuning;
    SwatSnapshotRing ring;
    unsigned char* full;
    SwatNetView view;
    SwatPredictor predictor;
    uint32_t view_tick;
    uint64_t bytes_received;
    uint32_t deltas_applied;
    // Optional loading UI/other transports. Called synchronously during map
    // construction; never poll this client recursively or touch its replica.
    SwatBuildProgress loading_service;
    void* loading_context;
    char error[160];
} SwatNetClient;

// The transport owns sockets only. Authority always stays in SwatSim; a client
// is a read-only Box3D replica for presentation/querying, never a second game.
bool swat_server_open(SwatNetServer* server,SwatSim* sim,int port,bool local_player);
void swat_server_poll(SwatNetServer* server);
void swat_server_tick(SwatNetServer* server,const SwatInput* local);
// Publish current authority/acks while servicing loading or paused simulation.
// This advances only transport revision; it never steps or resets gameplay.
void swat_server_publish(SwatNetServer* server);
void swat_server_restart(SwatNetServer* server);
void swat_server_scenario(SwatNetServer* server,const SwatConfig* config);
void swat_server_close(SwatNetServer* server);
int swat_server_leader(const SwatNetServer* server);
bool swat_client_open(SwatNetClient* client,SwatSim* replica,const char* address,int port);
void swat_client_poll(SwatNetClient* client);
bool swat_client_input(SwatNetClient* client,const SwatInput* input);
void swat_client_restart(SwatNetClient* client);
void swat_client_scenario(SwatNetClient* client,const SwatConfig* config);
void swat_client_close(SwatNetClient* client);
// Presentation only: move the replica's remote actors to their interpolated
// poses and the local actor to its predicted pose for this frame. The next
// snapshot overwrites them; nothing here reaches the authority.
// frame_dt: seconds since the previous frame; tick_alpha: pacer phase in [0,1].
void swat_client_present(SwatNetClient* client,float frame_dt,float tick_alpha);
const char* swat_disconnect_reason(unsigned int reason);

#ifdef __cplusplus
}
#endif
#endif
