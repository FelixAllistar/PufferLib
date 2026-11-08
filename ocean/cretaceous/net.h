// Networking over ENet. The server owns the sim; clients send inputs and draw
// snapshots. Solo play never touches this. Wire format is explicit
// big-endian with validated lengths; C structs never cross the wire.
#ifndef CRET_NET_H
#define CRET_NET_H
#include "sim.h"

#define CRET_NET_VERSION 6   // 2: species, action, gait flags and speed per agent; action in inputs. 3: survival stats, carcasses.
                             // 4: respawn/species requests from clients; group, broken-bone flag and rally point per agent
                             // 5: cloud cover (server weather) in snapshots
                             // 6: ability in inputs; profile and unlock requests; a trailing snapshot block
                             //    (status flags, people's progress and quests, group quests, nests)
#define CRET_DEFAULT_PORT 27480
#define CRET_MAX_PEERS 16

typedef struct CretNet CretNet;

// Server (listen host or dedicated). Peer slots map to agents 0..N-1 after the host.
CretNet* cret_net_host(int port, int first_slot);
// Returns joined/left events via callbacks-free polling: call every tick.
void cret_net_server_poll(CretNet* n, CretSim* s, CretInput* inputs, bool* remote_slot);
void cret_net_server_broadcast(CretNet* n, const CretSim* s);

// Client.
CretNet* cret_net_join(const char* address, int port, float timeout_s);
// After join: world parameters and assigned slot (valid once welcomed).
bool cret_net_client_welcomed(const CretNet* n, CretConfig* cfg, int* slot);
void cret_net_client_send(CretNet* n, const CretInput* in, int tick);
// Asks the server to respawn this client's animal: as `species`, or as the
// same species when -1 (the server ignores requests faster than one a second).
void cret_net_client_request(CretNet* n, int species);
// Character name (the server loads and saves that character; progress.c,
// character.c) and buying or equipping slot-3 ability choice 0 or 1.
void cret_net_client_profile(CretNet* n, const char* name);
void cret_net_client_unlock(CretNet* n, int choice);
// Applies the latest snapshot to a local presentation sim. False when disconnected.
bool cret_net_client_poll(CretNet* n, CretSim* s);
void cret_net_close(CretNet* n);
#endif
