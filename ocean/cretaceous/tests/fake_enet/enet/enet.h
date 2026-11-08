// In-memory stand-in for ENet, so tests/test_net.c can compile net.c as is
// and drive its codec with exact bytes: no sockets, no timing.
//   - every host shares one FIFO of incoming events (fake_enet_push_*);
//   - enet_peer_send records the packet in fake_enet_sent[] instead of sending.
// Mirrors the real signatures (vendor/enet/include/enet/enet.h). If net.c
// starts using another ENet call, add a stub here.
#ifndef FAKE_ENET_H
#define FAKE_ENET_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t enet_uint8;
typedef uint16_t enet_uint16;
typedef uint32_t enet_uint32;
#define ENET_HOST_ANY 0
#define ENET_HOST_BROADCAST 0xFFFFFFFFu
#define ENET_PORT_ANY 0
enum { ENET_PACKET_FLAG_RELIABLE = 1, ENET_PACKET_FLAG_UNSEQUENCED = 2, ENET_PACKET_FLAG_NO_ALLOCATE = 4, ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT = 8 };
typedef enum { ENET_EVENT_TYPE_NONE, ENET_EVENT_TYPE_CONNECT, ENET_EVENT_TYPE_DISCONNECT, ENET_EVENT_TYPE_RECEIVE } ENetEventType;
typedef enum { ENET_PEER_STATE_DISCONNECTED, ENET_PEER_STATE_CONNECTED = 5 } ENetPeerState;

typedef struct ENetAddress { enet_uint32 host; enet_uint16 port; } ENetAddress;
typedef struct ENetPacket { size_t referenceCount; enet_uint32 flags; enet_uint8* data; size_t dataLength; void* userData; } ENetPacket;
typedef struct ENetPeer { ENetAddress address; void* data; ENetPeerState state; enet_uint32 roundTripTime, packetLoss; int id; } ENetPeer;
typedef struct ENetHost { ENetAddress address; size_t peerCount; ENetPeer* peers; int open; } ENetHost;
typedef struct ENetEvent { ENetEventType type; ENetPeer* peer; enet_uint8 channelID; enet_uint32 data; ENetPacket* packet; } ENetEvent;

// ---------------------------------------------------------------- test side
#define FAKE_ENET_MAXQ 64
#define FAKE_ENET_MAXSENT 64
typedef struct FakeSent { ENetPeer* peer; int channel; int reliable; size_t n; enet_uint8 data[8192]; } FakeSent;
static ENetEvent fake_enet_q[FAKE_ENET_MAXQ];
static int fake_enet_qhead, fake_enet_qtail;
static FakeSent fake_enet_sent[FAKE_ENET_MAXSENT];
static int fake_enet_nsent, fake_enet_hosts, fake_enet_disconnects;
static ENetPeer fake_enet_server_peer;   // what enet_host_connect returns

static inline ENetPacket* enet_packet_create(const void* data, size_t n, enet_uint32 flags) {
    ENetPacket* p = (ENetPacket*)calloc(1, sizeof(ENetPacket));
    p->data = (enet_uint8*)malloc(n ? n : 1); if (data && n) memcpy(p->data, data, n);
    p->dataLength = n; p->flags = flags; return p;
}
static inline void enet_packet_destroy(ENetPacket* p) { if (p) { free(p->data); free(p); } }
static inline int enet_packet_resize(ENetPacket* p, size_t n) { p->data = (enet_uint8*)realloc(p->data, n ? n : 1); p->dataLength = n; return 0; }

static inline void fake_enet_push(ENetEventType type, ENetPeer* peer, const void* data, size_t n) {
    ENetEvent* e = &fake_enet_q[fake_enet_qtail++ % FAKE_ENET_MAXQ];
    memset(e, 0, sizeof *e); e->type = type; e->peer = peer;
    if (type == ENET_EVENT_TYPE_RECEIVE) e->packet = enet_packet_create(data, n, 0);
}
static inline void fake_enet_clear(void) {
    while (fake_enet_qhead < fake_enet_qtail) { ENetEvent* e = &fake_enet_q[fake_enet_qhead++ % FAKE_ENET_MAXQ]; enet_packet_destroy(e->packet); }
    fake_enet_qhead = fake_enet_qtail = 0; fake_enet_nsent = 0;
}
// Last packet sent (to `peer`, or to anyone when NULL); NULL if none.
static inline FakeSent* fake_enet_last(ENetPeer* peer) {
    for (int i = fake_enet_nsent - 1; i >= 0; i--) if (!peer || fake_enet_sent[i].peer == peer) return &fake_enet_sent[i];
    return NULL;
}

// ---------------------------------------------------------------- ENet API
static inline int enet_initialize(void) { return 0; }
static inline void enet_deinitialize(void) {}
static inline enet_uint32 enet_time_get(void) { return 0; }
static inline void enet_time_set(enet_uint32 t) { (void)t; }
static inline int enet_address_set_host(ENetAddress* a, const char* name) { (void)name; a->host = 0x0100007f; return 0; }
static inline int enet_address_set_host_ip(ENetAddress* a, const char* name) { return enet_address_set_host(a, name); }
static inline int enet_address_get_host_ip(const ENetAddress* a, char* name, size_t n) { (void)a; strncpy(name, "127.0.0.1", n); return 0; }
static inline int enet_address_get_host(const ENetAddress* a, char* name, size_t n) { return enet_address_get_host_ip(a, name, n); }
static inline ENetHost* enet_host_create(const ENetAddress* a, size_t peers, size_t ch, enet_uint32 in, enet_uint32 out) {
    (void)ch; (void)in; (void)out; ENetHost* h = (ENetHost*)calloc(1, sizeof(ENetHost));
    if (a) h->address = *a; h->peerCount = peers; h->open = 1; fake_enet_hosts++; return h;
}
static inline void enet_host_destroy(ENetHost* h) { if (h) { fake_enet_hosts--; free(h); } }
static inline ENetPeer* enet_host_connect(ENetHost* h, const ENetAddress* a, size_t ch, enet_uint32 data) {
    (void)h; (void)ch; (void)data; memset(&fake_enet_server_peer, 0, sizeof fake_enet_server_peer);
    fake_enet_server_peer.address = *a; fake_enet_server_peer.state = ENET_PEER_STATE_CONNECTED; return &fake_enet_server_peer;
}
static inline int enet_host_service(ENetHost* h, ENetEvent* ev, enet_uint32 timeout) {
    (void)h; (void)timeout;
    if (fake_enet_qhead >= fake_enet_qtail) { memset(ev, 0, sizeof *ev); return 0; }
    *ev = fake_enet_q[fake_enet_qhead++ % FAKE_ENET_MAXQ];
    return 1;
}
static inline int enet_host_check_events(ENetHost* h, ENetEvent* ev) { return enet_host_service(h, ev, 0); }
static inline void enet_host_flush(ENetHost* h) { (void)h; }
static inline void enet_host_bandwidth_limit(ENetHost* h, enet_uint32 a, enet_uint32 b) { (void)h; (void)a; (void)b; }
static inline void enet_host_channel_limit(ENetHost* h, size_t n) { (void)h; (void)n; }
static inline int enet_host_compress_with_range_coder(ENetHost* h) { (void)h; return 0; }
static inline int enet_peer_send(ENetPeer* peer, enet_uint8 ch, ENetPacket* p) {
    if (fake_enet_nsent < FAKE_ENET_MAXSENT && p->dataLength <= sizeof fake_enet_sent[0].data) {
        FakeSent* s = &fake_enet_sent[fake_enet_nsent++];
        s->peer = peer; s->channel = ch; s->reliable = (p->flags & ENET_PACKET_FLAG_RELIABLE) != 0; s->n = p->dataLength; memcpy(s->data, p->data, p->dataLength);
    }
    enet_packet_destroy(p);
    return 0;
}
static inline void enet_host_broadcast(ENetHost* h, enet_uint8 ch, ENetPacket* p) { (void)h; enet_peer_send(NULL, ch, p); }
static inline void enet_peer_disconnect(ENetPeer* p, enet_uint32 d) { (void)p; (void)d; fake_enet_disconnects++; }
static inline void enet_peer_disconnect_now(ENetPeer* p, enet_uint32 d) { enet_peer_disconnect(p, d); }
static inline void enet_peer_disconnect_later(ENetPeer* p, enet_uint32 d) { enet_peer_disconnect(p, d); }
static inline void enet_peer_reset(ENetPeer* p) { (void)p; }
static inline void enet_peer_ping(ENetPeer* p) { (void)p; }
static inline void enet_peer_ping_interval(ENetPeer* p, enet_uint32 v) { (void)p; (void)v; }
static inline void enet_peer_timeout(ENetPeer* p, enet_uint32 a, enet_uint32 b, enet_uint32 c) { (void)p; (void)a; (void)b; (void)c; }
static inline void enet_peer_throttle_configure(ENetPeer* p, enet_uint32 a, enet_uint32 b, enet_uint32 c) { (void)p; (void)a; (void)b; (void)c; }
#endif
