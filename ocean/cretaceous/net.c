#include "net.h"
#include <enet/enet.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MSG_HELLO = 1, MSG_WELCOME, MSG_INPUT, MSG_SNAPSHOT, MSG_REQUEST };
enum { CH_RELIABLE, CH_STATE, CH_COUNT };

struct CretNet {
    ENetHost* host; ENetPeer* server;          // server is NULL on the host side
    int first_slot;
    ENetPeer* peers[CRET_MAX_PEERS]; CretInput peer_input[CRET_MAX_PEERS]; int peer_req_tick[CRET_MAX_PEERS];
    bool welcomed; CretConfig cfg; int slot; bool connected;
};

// ---------------------------------------------------------------- codec
typedef struct { uint8_t* p; size_t n, cap; bool bad; } Buf;
static void put_u8(Buf* b, uint8_t v) { if (b->n + 1 > b->cap) { b->bad = true; return; } b->p[b->n++] = v; }
static void put_u32(Buf* b, uint32_t v) { for (int i = 3; i >= 0; i--) put_u8(b, (uint8_t)(v >> (8 * i))); }
static void put_f32(Buf* b, float f) { uint32_t u; memcpy(&u, &f, 4); put_u32(b, u); }
static void put_i8(Buf* b, float v) { int x = (int)lroundf(v * 127.0f); put_u8(b, (uint8_t)(int8_t)(x < -127 ? -127 : x > 127 ? 127 : x)); }
static uint8_t q8(float v) { return (uint8_t)(v <= 0 ? 0 : v >= 1 ? 255 : v * 255 + 0.5f); }
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
            memset(&n->peer_input[k], 0, sizeof(CretInput)); n->peer_req_tick[k] = -CRET_TICK_HZ;
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
            uint8_t type = k >= 0 ? get_u8(&b) : 0;
            if (type == MSG_INPUT) {
                get_u32(&b); CretInput in = {0};
                in.forward = get_i8(&b); in.strafe = get_i8(&b); in.turn = get_i8(&b);
                uint8_t f = get_u8(&b); in.sprint = f & 1; in.jump = (f >> 1) & 1; in.action = (uint8_t)((f >> 2) & 7);
                if (!b.bad) n->peer_input[k] = in;
            } else if (type == MSG_REQUEST) {   // respawn, optionally as another species
                int sp = get_u8(&b), slot = n->first_slot + k;
                if (!b.bad && slot < s->cfg.num_agents && s->tick - n->peer_req_tick[k] >= CRET_TICK_HZ) {
                    n->peer_req_tick[k] = s->tick;
                    if (sp < CRET_SPECIES) cret_sim_set_species(s, slot, sp); else cret_sim_respawn(s, slot);
                    fprintf(stderr, "cretaceous: slot %d respawned as %s\n", slot, CRET_SPECIES_TABLE[s->agents[slot].species].name);
                }
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
    uint8_t raw[16 + CRET_MAX_AGENTS * 48 + 1 + CRET_MAX_CARCASSES * 24]; Buf b = {.p = raw, .n = 0, .cap = sizeof raw};
    put_u8(&b, MSG_SNAPSHOT); put_u32(&b, (uint32_t)s->tick); put_f32(&b, s->time_of_day); put_u8(&b, (uint8_t)s->cfg.num_agents);
    for (int i = 0; i < s->cfg.num_agents; i++) {
        const CretAgent* a = &s->agents[i];
        put_u8(&b, a->alive); put_u8(&b, a->species); put_u8(&b, a->act);
        put_u8(&b, (uint8_t)((a->on_ground ? 1 : 0) | (a->swimming ? 2 : 0) | (a->sprinting ? 4 : 0) | (a->bleed > 0.002f * a->max_health ? 8 : 0) |
                           (a->broken > 0 ? 16 : 0) | (a->rally_ticks > 0 ? 32 : 0)));
        put_f32(&b, a->x); put_f32(&b, a->y); put_f32(&b, a->z); put_f32(&b, a->yaw); put_f32(&b, a->speed);
        put_u8(&b, q8(a->max_health > 0 ? a->health / a->max_health : 0)); put_u8(&b, q8(a->growth)); put_u8(&b, q8(a->food));
        put_u8(&b, q8(a->water)); put_u8(&b, q8(a->stamina)); put_u8(&b, (uint8_t)(a->hurt_ticks > 255 ? 255 : a->hurt_ticks));
        int dt = a->dead_ticks > 65535 ? 65535 : a->dead_ticks; put_u8(&b, (uint8_t)(dt >> 8)); put_u8(&b, (uint8_t)dt);
        put_u8(&b, a->group);
        if (a->rally_ticks > 0) { put_f32(&b, a->rally_x); put_f32(&b, a->rally_z); }
    }
    int nc = 0; for (int c = 0; c < CRET_MAX_CARCASSES; c++) nc += s->carcasses[c].active;
    put_u8(&b, (uint8_t)nc);
    for (int c = 0; c < CRET_MAX_CARCASSES; c++) {
        const CretCarcass* k = &s->carcasses[c]; if (!k->active) continue;
        put_u8(&b, (uint8_t)c); put_u8(&b, k->species); put_u8(&b, q8(k->growth)); put_u8(&b, q8(k->max_meat > 0 ? k->meat / k->max_meat : 0));
        int age = k->age > 65535 ? 65535 : k->age; put_u8(&b, (uint8_t)(age >> 8)); put_u8(&b, (uint8_t)age);
        put_u8(&b, (uint8_t)(k->agent < 0 ? 255 : k->agent));
        put_f32(&b, k->x); put_f32(&b, k->y); put_f32(&b, k->z); put_f32(&b, k->yaw);
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
    put_u8(&b, (uint8_t)((in->sprint ? 1 : 0) | (in->jump ? 2 : 0) | ((in->action & 7) << 2)));
    net_send(n->server, CH_STATE, &b, false); enet_host_flush(n->host);
}
void cret_net_client_request(CretNet* n, int species) {
    uint8_t raw[4]; Buf b = {.p = raw, .n = 0, .cap = sizeof raw};
    put_u8(&b, MSG_REQUEST); put_u8(&b, (uint8_t)(species >= 0 && species < CRET_SPECIES ? species : 255));
    net_send(n->server, CH_RELIABLE, &b, true); enet_host_flush(n->host);
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
                    tmp[i].alive = get_u8(&b); tmp[i].species = get_u8(&b); tmp[i].act = get_u8(&b); uint8_t fl = get_u8(&b);
                    tmp[i].on_ground = fl & 1; tmp[i].swimming = (fl >> 1) & 1; tmp[i].sprinting = (fl >> 2) & 1;
                    tmp[i].x = get_f32(&b); tmp[i].y = get_f32(&b); tmp[i].z = get_f32(&b); tmp[i].yaw = get_f32(&b); tmp[i].speed = get_f32(&b);
                    tmp[i].health = get_u8(&b) / 255.0f; tmp[i].growth = get_u8(&b) / 255.0f; tmp[i].food = get_u8(&b) / 255.0f;
                    tmp[i].water = get_u8(&b) / 255.0f; tmp[i].stamina = get_u8(&b) / 255.0f; tmp[i].hurt_ticks = get_u8(&b);
                    tmp[i].dead_ticks = get_u8(&b) << 8; tmp[i].dead_ticks |= get_u8(&b);
                    tmp[i].bleed = (fl >> 3) & 1;   // flags only; scaled below
                    tmp[i].broken = (fl >> 4) & 1; tmp[i].group = get_u8(&b); tmp[i].rally_ticks = (fl >> 5) & 1;
                    if (tmp[i].rally_ticks) { tmp[i].rally_x = get_f32(&b); tmp[i].rally_z = get_f32(&b); }
                    if (tmp[i].species >= CRET_SPECIES) b.bad = true;
                }
                CretCarcass car[CRET_MAX_CARCASSES]; memset(car, 0, sizeof car);
                int ncar = get_u8(&b);
                for (int q = 0; q < ncar && !b.bad; q++) {
                    int c = get_u8(&b); CretCarcass k = {.active = true};
                    k.species = get_u8(&b); k.growth = get_u8(&b) / 255.0f; k.max_meat = 1; k.meat = get_u8(&b) / 255.0f;
                    k.age = get_u8(&b) << 8; k.age |= get_u8(&b); int ag = get_u8(&b); k.agent = ag == 255 ? -1 : ag;
                    k.x = get_f32(&b); k.y = get_f32(&b); k.z = get_f32(&b); k.yaw = get_f32(&b);
                    if (c >= CRET_MAX_CARCASSES || k.species >= CRET_SPECIES) b.bad = true; else car[c] = k;
                }
                if (!b.bad) {
                    s->tick = tick; s->time_of_day = tod;
                    for (int i = 0; i < cnt && i < s->cfg.num_agents; i++) {
                        CretAgent* a = &s->agents[i]; a->alive = tmp[i].alive; a->x = tmp[i].x; a->y = tmp[i].y; a->z = tmp[i].z; a->yaw = tmp[i].yaw;
                        a->species = tmp[i].species; a->act = tmp[i].act; a->on_ground = tmp[i].on_ground; a->swimming = tmp[i].swimming;
                        a->sprinting = tmp[i].sprinting; a->speed = tmp[i].speed;
                        a->growth = tmp[i].growth; a->max_health = 0; cret_apply_growth(a);   // size and max health from species and growth
                        a->health = tmp[i].health * a->max_health; a->food = tmp[i].food; a->water = tmp[i].water; a->stamina = tmp[i].stamina;
                        a->hurt_ticks = tmp[i].hurt_ticks; a->dead_ticks = tmp[i].dead_ticks; a->bleed = tmp[i].bleed > 0 ? a->max_health * 0.01f : 0;
                        a->broken = tmp[i].broken; a->group = tmp[i].group; a->rally_ticks = tmp[i].rally_ticks; a->rally_x = tmp[i].rally_x; a->rally_z = tmp[i].rally_z;
                    }
                    memcpy(s->carcasses, car, sizeof car);
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
