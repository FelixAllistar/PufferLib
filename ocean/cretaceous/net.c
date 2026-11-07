#include "net.h"
#include <enet/enet.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MSG_HELLO = 1, MSG_WELCOME, MSG_INPUT, MSG_SNAPSHOT };
enum { CH_RELIABLE, CH_STATE, CH_COUNT };

struct CretNet {
    ENetHost* host; ENetPeer* server;          // server is NULL on the host side
    int first_slot;
    ENetPeer* peers[CRET_MAX_PEERS]; CretInput peer_input[CRET_MAX_PEERS];
    bool welcomed; CretConfig cfg; int slot; bool connected;
};

// ---------------------------------------------------------------- codec
typedef struct { uint8_t* p; size_t n, cap; bool bad; } Buf;
static void put_u8(Buf* b, uint8_t v) { if (b->n + 1 > b->cap) { b->bad = true; return; } b->p[b->n++] = v; }
static void put_u32(Buf* b, uint32_t v) { for (int i = 3; i >= 0; i--) put_u8(b, (uint8_t)(v >> (8 * i))); }
static void put_f32(Buf* b, float f) { uint32_t u; memcpy(&u, &f, 4); put_u32(b, u); }
static void put_i8(Buf* b, float v) { int x = (int)lroundf(v * 127.0f); put_u8(b, (uint8_t)(int8_t)(x < -127 ? -127 : x > 127 ? 127 : x)); }
static uint8_t get_u8(Buf* b) { if (b->n + 1 > b->cap) { b->bad = true; return 0; } return b->p[b->n++]; }
static uint32_t get_u32(Buf* b) { uint32_t v = 0; for (int i = 0; i < 4; i++) v = (v << 8) | get_u8(b); return v; }
static float get_f32(Buf* b) { uint32_t u = get_u32(b); float f; memcpy(&f, &u, 4); return isfinite(f) ? f : (b->bad = true, 0.0f); }
static float get_i8(Buf* b) { return (int8_t)get_u8(b) / 127.0f; }

static void net_send(ENetPeer* peer, int ch, const Buf* b, bool reliable) {
    if (b->bad) return;
    ENetPacket* pk = enet_packet_create(b->p, b->n, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    enet_peer_send(peer, ch, pk);
}

static bool g_enet_up;
static bool enet_up(void) { if (!g_enet_up && enet_initialize() == 0) g_enet_up = true; return g_enet_up; }

// ---------------------------------------------------------------- server
CretNet* cret_net_host(int port, int first_slot) {
    if (!enet_up()) return NULL;
    ENetAddress addr = {.host = ENET_HOST_ANY, .port = (enet_uint16)port};
    CretNet* n = calloc(1, sizeof(*n)); n->first_slot = first_slot;
    n->host = enet_host_create(&addr, CRET_MAX_PEERS, CH_COUNT, 0, 0);
    if (!n->host) { free(n); fprintf(stderr, "cretaceous: cannot listen on port %d\n", port); return NULL; }
    fprintf(stderr, "cretaceous: hosting on UDP %d\n", port);
    return n;
}

void cret_net_server_poll(CretNet* n, CretSim* s, CretInput* inputs, bool* remote) {
    ENetEvent ev;
    while (enet_host_service(n->host, &ev, 0) > 0) {
        if (ev.type == ENET_EVENT_TYPE_CONNECT) {
            int k = -1; for (int i = 0; i < CRET_MAX_PEERS; i++) if (!n->peers[i]) { k = i; break; }
            int slot = n->first_slot + k;
            if (k < 0 || slot >= s->cfg.num_agents) { enet_peer_disconnect(ev.peer, 0); continue; }
            n->peers[k] = ev.peer; ev.peer->data = (void*)(intptr_t)(k + 1);
            memset(&n->peer_input[k], 0, sizeof(CretInput));
            uint8_t raw[64]; Buf b = {.p = raw, .n = 0, .cap = sizeof raw};
            put_u8(&b, MSG_WELCOME); put_u8(&b, CRET_NET_VERSION); put_u32(&b, s->cfg.world_seed);
            put_u32(&b, (uint32_t)s->cfg.world_n); put_f32(&b, s->cfg.world_cell); put_u8(&b, (uint8_t)s->cfg.num_agents); put_u8(&b, (uint8_t)slot);
            net_send(ev.peer, CH_RELIABLE, &b, true);
            cret_sim_respawn(s, slot);
            fprintf(stderr, "cretaceous: player joined slot %d\n", slot);
        } else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
            int k = (int)(intptr_t)ev.peer->data - 1;
            if (k >= 0 && k < CRET_MAX_PEERS) { n->peers[k] = NULL; fprintf(stderr, "cretaceous: player left slot %d\n", n->first_slot + k); }
        } else if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
            int k = (int)(intptr_t)ev.peer->data - 1;
            Buf b = {.p = ev.packet->data, .n = 0, .cap = ev.packet->dataLength};
            if (k >= 0 && get_u8(&b) == MSG_INPUT) {
                get_u32(&b); CretInput in;
                in.forward = get_i8(&b); in.strafe = get_i8(&b); in.turn = get_i8(&b);
                uint8_t f = get_u8(&b); in.sprint = f & 1; in.jump = (f >> 1) & 1;
                if (!b.bad) n->peer_input[k] = in;
            }
            enet_packet_destroy(ev.packet);
        }
    }
    for (int k = 0; k < CRET_MAX_PEERS; k++) {
        int slot = n->first_slot + k; if (slot >= s->cfg.num_agents) break;
        remote[slot] = n->peers[k] != NULL;
        if (n->peers[k]) inputs[slot] = n->peer_input[k];
    }
}

void cret_net_server_broadcast(CretNet* n, const CretSim* s) {
    uint8_t raw[16 + CRET_MAX_AGENTS * 20]; Buf b = {.p = raw, .n = 0, .cap = sizeof raw};
    put_u8(&b, MSG_SNAPSHOT); put_u32(&b, (uint32_t)s->tick); put_f32(&b, s->time_of_day); put_u8(&b, (uint8_t)s->cfg.num_agents);
    for (int i = 0; i < s->cfg.num_agents; i++) {
        const CretAgent* a = &s->agents[i];
        put_u8(&b, a->alive); put_f32(&b, a->x); put_f32(&b, a->y); put_f32(&b, a->z); put_f32(&b, a->yaw);
    }
    for (int k = 0; k < CRET_MAX_PEERS; k++) if (n->peers[k]) net_send(n->peers[k], CH_STATE, &b, false);
    enet_host_flush(n->host);
}

// ---------------------------------------------------------------- client
CretNet* cret_net_join(const char* address, int port, float timeout_s) {
    if (!enet_up()) return NULL;
    CretNet* n = calloc(1, sizeof(*n));
    n->host = enet_host_create(NULL, 1, CH_COUNT, 0, 0);
    ENetAddress addr; enet_address_set_host(&addr, address); addr.port = (enet_uint16)port;
    n->server = enet_host_connect(n->host, &addr, CH_COUNT, 0);
    ENetEvent ev;
    if (!n->server || enet_host_service(n->host, &ev, (enet_uint32)(timeout_s * 1000)) <= 0 || ev.type != ENET_EVENT_TYPE_CONNECT) {
        fprintf(stderr, "cretaceous: could not reach %s:%d\n", address, port);
        cret_net_close(n); return NULL;
    }
    n->connected = true;
    // Wait for the welcome (world parameters and slot).
    for (int i = 0; i < 50 && !n->welcomed; i++) {
        if (enet_host_service(n->host, &ev, 100) > 0 && ev.type == ENET_EVENT_TYPE_RECEIVE) {
            Buf b = {.p = ev.packet->data, .n = 0, .cap = ev.packet->dataLength};
            if (get_u8(&b) == MSG_WELCOME && get_u8(&b) == CRET_NET_VERSION) {
                n->cfg = cret_default_config();
                n->cfg.world_seed = get_u32(&b); n->cfg.world_n = (int)get_u32(&b); n->cfg.world_cell = get_f32(&b);
                n->cfg.num_agents = get_u8(&b); n->slot = get_u8(&b);
                n->welcomed = !b.bad && n->cfg.world_n >= 256 && n->cfg.world_n <= 8192 && n->slot < n->cfg.num_agents;
            }
            enet_packet_destroy(ev.packet);
        }
    }
    if (!n->welcomed) { fprintf(stderr, "cretaceous: server did not send a compatible welcome\n"); cret_net_close(n); return NULL; }
    return n;
}
bool cret_net_client_welcomed(const CretNet* n, CretConfig* cfg, int* slot) {
    if (!n->welcomed) return false;
    *cfg = n->cfg; *slot = n->slot; return true;
}
void cret_net_client_send(CretNet* n, const CretInput* in, int tick) {
    uint8_t raw[16]; Buf b = {.p = raw, .n = 0, .cap = sizeof raw};
    put_u8(&b, MSG_INPUT); put_u32(&b, (uint32_t)tick); put_i8(&b, in->forward); put_i8(&b, in->strafe); put_i8(&b, in->turn);
    put_u8(&b, (uint8_t)((in->sprint ? 1 : 0) | (in->jump ? 2 : 0)));
    net_send(n->server, CH_STATE, &b, false); enet_host_flush(n->host);
}
bool cret_net_client_poll(CretNet* n, CretSim* s) {
    ENetEvent ev;
    while (enet_host_service(n->host, &ev, 0) > 0) {
        if (ev.type == ENET_EVENT_TYPE_DISCONNECT) { n->connected = false; return false; }
        if (ev.type != ENET_EVENT_TYPE_RECEIVE) continue;
        Buf b = {.p = ev.packet->data, .n = 0, .cap = ev.packet->dataLength};
        if (get_u8(&b) == MSG_SNAPSHOT) {
            int tick = (int)get_u32(&b); float tod = get_f32(&b); int cnt = get_u8(&b);
            if (cnt <= CRET_MAX_AGENTS) {
                CretAgent tmp[CRET_MAX_AGENTS];
                for (int i = 0; i < cnt; i++) {
                    tmp[i].alive = get_u8(&b); tmp[i].x = get_f32(&b); tmp[i].y = get_f32(&b); tmp[i].z = get_f32(&b); tmp[i].yaw = get_f32(&b);
                }
                if (!b.bad) {
                    s->tick = tick; s->time_of_day = tod;
                    for (int i = 0; i < cnt && i < s->cfg.num_agents; i++) {
                        CretAgent* a = &s->agents[i]; a->alive = tmp[i].alive; a->x = tmp[i].x; a->y = tmp[i].y; a->z = tmp[i].z; a->yaw = tmp[i].yaw;
                    }
                }
            }
        }
        enet_packet_destroy(ev.packet);
    }
    return n->connected;
}
void cret_net_close(CretNet* n) {
    if (!n) return;
    if (n->server) enet_peer_disconnect_now(n->server, 0);
    if (n->host) enet_host_destroy(n->host);
    free(n);
}
