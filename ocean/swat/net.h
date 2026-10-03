#ifndef SWAT_NET_H
#define SWAT_NET_H
#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SWAT_DEFAULT_PORT 27474
#define SWAT_INPUT_QUEUE 32
typedef enum SwatNetStatus { SWAT_NET_OFFLINE,SWAT_NET_CONNECTING,SWAT_NET_ACTIVE,SWAT_NET_FAILED } SwatNetStatus;
typedef struct SwatRemoteSlot {
    void* peer;
    SwatCommand queue[SWAT_INPUT_QUEUE];
    SwatInput held;
    int head,count;
    uint32_t received,ack,last_receive_ms;
} SwatRemoteSlot;
typedef struct SwatNetServer {
    void* transport;
    SwatSim* sim;
    SwatRemoteSlot slots[SWAT_MAX_PLAYERS];
    uint32_t epoch,revision,pulses;
    unsigned int player_mask;
    int local_slot,port;
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
    char error[160];
} SwatNetClient;

// The transport owns sockets only. Authority always stays in SwatSim; a client
// is a read-only Box3D replica for presentation/querying, never a second game.
bool swat_server_open(SwatNetServer* server,SwatSim* sim,int port,bool local_player);
void swat_server_poll(SwatNetServer* server);
void swat_server_tick(SwatNetServer* server,const SwatInput* local);
void swat_server_restart(SwatNetServer* server);
void swat_server_close(SwatNetServer* server);
int swat_server_leader(const SwatNetServer* server);
bool swat_client_open(SwatNetClient* client,SwatSim* replica,const char* address,int port);
void swat_client_poll(SwatNetClient* client);
bool swat_client_input(SwatNetClient* client,const SwatInput* input);
void swat_client_restart(SwatNetClient* client);
void swat_client_close(SwatNetClient* client);
const char* swat_disconnect_reason(unsigned int reason);

#ifdef __cplusplus
}
#endif
#endif
