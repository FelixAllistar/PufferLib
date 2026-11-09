// Network-condition simulator: a UDP relay between a client and its server
// that adds latency, jitter, loss (independent or in bursts), duplication and
// reordering to both directions. The client's ENet host talks to the relay's
// local port; the relay forwards to the real server. It is pumped from the
// client's own loop (no threads), so its timing resolution is how often the
// client polls (about 1 ms in netbench, a frame in the player).
//
// Spec string (CRET_NETSIM or `join --netsim`), every value per direction:
//   lat=MS     one-way delay added to each datagram (RTT grows by 2*lat)
//   jit=MS     standard deviation of a normal delay variation (keeps order)
//   loss=PCT   datagrams dropped
//   burst=N    mean length of a loss burst in datagrams (1: independent)
//   dup=PCT    datagrams delivered twice
//   reorder=PCT datagrams held back 10-40 ms so later ones overtake them
//   up=0|1, down=0|1   condition only one direction (default both)
#ifndef CRET_NETSIM_H
#define CRET_NETSIM_H
#include <stdbool.h>
#include <stdint.h>
#ifdef _WIN32
// On Windows <enet/enet.h> pulls in <windows.h>, whose names (CloseWindow,
// ShowCursor, Rectangle, ...) clash with raylib.h in the player, which sees
// this header through net.h. Only the address type is needed here.
typedef struct _ENetAddress ENetAddress;
#else
#include <enet/enet.h>
#endif

typedef struct CretNetsimCfg {
    float lat_ms, jit_ms, loss_pct, burst, dup_pct, reorder_pct;
    bool up, down;
} CretNetsimCfg;

typedef struct CretNetsimStats {
    uint64_t in[2], out[2], dropped[2], duplicated[2], reordered[2];   // [0] up (client to server), [1] down
} CretNetsimStats;

typedef struct CretNetsim CretNetsim;

// Parses a spec; false on an unknown key or a bad value (cfg keeps defaults: nothing added).
bool cret_netsim_parse(const char* spec, CretNetsimCfg* cfg);
void cret_netsim_describe(const CretNetsimCfg* cfg, char* out, int n);
// Opens a relay on 127.0.0.1 toward `server`; *local gets the address to connect to.
CretNetsim* cret_netsim_open(const CretNetsimCfg* cfg, const ENetAddress* server, uint32_t seed, ENetAddress* local);
// Moves datagrams that arrived and delivers those that are due. Non-blocking.
void cret_netsim_pump(CretNetsim* ns);
void cret_netsim_stats(const CretNetsim* ns, CretNetsimStats* st);
void cret_netsim_close(CretNetsim* ns);
double cret_netsim_now(void);   // monotonic seconds
// Replaces the clock of the relay and the client's timing (netbench runs on
// virtual time so a busy machine does not change the results). NULL: real time.
void cret_netsim_set_clock(double (*fn)(void));
#endif
