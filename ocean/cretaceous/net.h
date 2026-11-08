// Networking over ENet. The server owns the sim; clients send inputs and draw
// snapshots. Solo play never touches this. Wire format is explicit
// big-endian with validated lengths; C structs never cross the wire.
#ifndef CRET_NET_H
#define CRET_NET_H
#include "sim.h"

#define CRET_NET_VERSION 7   // 2: species, action, gait flags and speed per agent; action in inputs. 3: survival stats, carcasses.
                             // 4: respawn/species requests from clients; group, broken-bone flag and rally point per agent
                             // 5: cloud cover (server weather) in snapshots
                             // 6: requests carry a sequence number; the server answers each one it settles (MSG_SPAWN_REPLY)
                             // 7: ability in inputs; profile and unlock requests; a trailing snapshot block
                             //    (status flags, people's progress and quests, group quests, nests)
#define CRET_DEFAULT_PORT 27480
#define CRET_MAX_PEERS 16
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
// Applies the latest snapshot to a local presentation sim. False when disconnected.
bool cret_net_client_poll(CretNet* n, CretSim* s);
void cret_net_close(CretNet* n);
#endif
