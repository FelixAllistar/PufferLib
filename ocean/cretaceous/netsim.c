#include "netsim.h"
#include <enet/enet.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NS_MAX_DGRAM 4096   // ENet's largest MTU
#define NS_MAX_QUEUE 4096   // datagrams in flight per direction (beyond that they drop, like a full router queue)

typedef struct { double due; size_t len; uint8_t* data; } NsPacket;
typedef struct {
    NsPacket* q; int n, cap;        // sorted by due time
    double last_due;                // FIFO floor: jitter alone never reorders
    bool bad;                       // Gilbert-Elliott loss state
} NsLane;

struct CretNetsim {
    CretNetsimCfg cfg;
    ENetSocket front, back;         // front: the client's side (127.0.0.1), back: toward the server
    ENetAddress server, client; bool have_client;
    NsLane lane[2];                 // 0 up, 1 down
    uint64_t rng;
    CretNetsimStats st;
    uint8_t buf[NS_MAX_DGRAM];
};

static double (*g_clock)(void);
void cret_netsim_set_clock(double (*fn)(void)) { g_clock = fn; }
double cret_netsim_now(void) {
    if (g_clock) return g_clock();
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static double urand(CretNetsim* ns) {   // splitmix64 -> [0, 1)
    uint64_t z = (ns->rng += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull; z = (z ^ (z >> 27)) * 0x94d049bb133111ebull; z ^= z >> 31;
    return (z >> 11) * (1.0 / 9007199254740992.0);
}
static double nrand(CretNetsim* ns) {
    double u = urand(ns), v = urand(ns);
    return sqrt(-2.0 * log(u > 1e-12 ? u : 1e-12)) * cos(6.283185307179586 * v);
}

bool cret_netsim_parse(const char* spec, CretNetsimCfg* c) {
    *c = (CretNetsimCfg){.burst = 1, .up = true, .down = true};
    if (!spec) return true;
    const char* p = spec;
    while (*p) {
        while (*p == ' ' || *p == ',' || *p == '\t') p++;
        if (!*p) break;
        char key[16]; int k = 0;
        while (*p && *p != '=' && *p != ' ' && k < 15) key[k++] = *p++;
        key[k] = 0;
        if (*p != '=') return false;
        p++;
        char* end; double v = strtod(p, &end);
        if (end == p || !isfinite(v) || v < 0) return false;
        p = end;
        if (!strcmp(key, "lat")) c->lat_ms = (float)v;
        else if (!strcmp(key, "jit")) c->jit_ms = (float)v;
        else if (!strcmp(key, "loss")) c->loss_pct = (float)fmin(v, 100);
        else if (!strcmp(key, "burst")) c->burst = (float)fmax(v, 1);
        else if (!strcmp(key, "dup")) c->dup_pct = (float)fmin(v, 100);
        else if (!strcmp(key, "reorder")) c->reorder_pct = (float)fmin(v, 100);
        else if (!strcmp(key, "up")) c->up = v != 0;
        else if (!strcmp(key, "down")) c->down = v != 0;
        else return false;
    }
    return true;
}

void cret_netsim_describe(const CretNetsimCfg* c, char* out, int n) {
    snprintf(out, (size_t)n, "lat=%g jit=%g loss=%g burst=%g dup=%g reorder=%g%s", c->lat_ms, c->jit_ms, c->loss_pct, c->burst, c->dup_pct,
             c->reorder_pct, c->up && c->down ? "" : c->up ? " (up only)" : " (down only)");
}

static ENetSocket open_socket(enet_uint32 host) {
    ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (s == ENET_SOCKET_NULL) return s;
    ENetAddress a = {.host = host, .port = 0};
    if (enet_socket_bind(s, &a) < 0) { enet_socket_destroy(s); return ENET_SOCKET_NULL; }
    enet_socket_set_option(s, ENET_SOCKOPT_NONBLOCK, 1);
    enet_socket_set_option(s, ENET_SOCKOPT_RCVBUF, 1 << 20);
    enet_socket_set_option(s, ENET_SOCKOPT_SNDBUF, 1 << 20);
    return s;
}

CretNetsim* cret_netsim_open(const CretNetsimCfg* cfg, const ENetAddress* server, uint32_t seed, ENetAddress* local) {
    CretNetsim* ns = calloc(1, sizeof(*ns));
    if (!ns) return NULL;
    ns->cfg = *cfg; ns->server = *server; ns->rng = seed * 0x2545f4914f6cdd1dull + 1;
    ENetAddress lo; enet_address_set_host_ip(&lo, "127.0.0.1");
    ns->front = ns->back = ENET_SOCKET_NULL;
    ns->front = open_socket(lo.host);
    ns->back = open_socket(ENET_HOST_ANY);
    if (ns->front == ENET_SOCKET_NULL || ns->back == ENET_SOCKET_NULL) { cret_netsim_close(ns); return NULL; }
    enet_socket_get_address(ns->front, local);
    local->host = lo.host;
    return ns;
}

static void lane_push(NsLane* L, double due, const uint8_t* d, size_t len) {
    if (L->n >= NS_MAX_QUEUE) return;
    if (L->n == L->cap) {
        int cap = L->cap ? L->cap * 2 : 64;
        NsPacket* q = realloc(L->q, sizeof(NsPacket) * (size_t)cap);
        if (!q) return;
        L->q = q; L->cap = cap;
    }
    uint8_t* copy = malloc(len ? len : 1);
    if (!copy) return;
    memcpy(copy, d, len);
    int i = L->n++;
    while (i > 0 && L->q[i - 1].due > due) { L->q[i] = L->q[i - 1]; i--; }
    L->q[i] = (NsPacket){due, len, copy};
}

// One datagram entering a direction: lose it, delay it, maybe twice.
static void admit(CretNetsim* ns, int dir, const uint8_t* d, size_t len, double now) {
    const CretNetsimCfg* c = &ns->cfg; NsLane* L = &ns->lane[dir];
    ns->st.in[dir]++;
    if (!(dir == 0 ? c->up : c->down)) { lane_push(L, now, d, len); return; }
    // Gilbert-Elliott: bursts of mean length `burst` average `loss` percent.
    double loss = c->loss_pct / 100.0;
    if (loss > 0) {
        double leave = 1.0 / c->burst, enter = loss >= 1 ? 1 : loss * leave / (1 - loss);
        if (L->bad) { if (urand(ns) < leave) L->bad = false; }
        else if (urand(ns) < enter) L->bad = true;
        if (L->bad) { ns->st.dropped[dir]++; return; }
    }
    double delay = c->lat_ms + c->jit_ms * nrand(ns);
    if (delay < 0) delay = 0;
    double due = now + delay * 1e-3;
    if (c->reorder_pct > 0 && urand(ns) * 100 < c->reorder_pct) {
        due = fmax(due, L->last_due) + (10 + 30 * urand(ns)) * 1e-3;   // held back past its successors
        ns->st.reordered[dir]++;
    } else {
        if (due < L->last_due) due = L->last_due;   // queueing keeps order
        L->last_due = due;
    }
    lane_push(L, due, d, len);
    if (c->dup_pct > 0 && urand(ns) * 100 < c->dup_pct) {
        lane_push(L, due + (1 + c->jit_ms * urand(ns)) * 1e-3, d, len);
        ns->st.duplicated[dir]++;
    }
}

void cret_netsim_pump(CretNetsim* ns) {
    if (!ns) return;
    double now = cret_netsim_now();
    for (int side = 0; side < 2; side++) {
        ENetSocket s = side == 0 ? ns->front : ns->back;
        for (int k = 0; k < 512; k++) {
            ENetAddress from; ENetBuffer b = {.data = ns->buf, .dataLength = sizeof ns->buf};
            int r = enet_socket_receive(s, &from, &b, 1);
            if (r <= 0) break;
            if (side == 0) {
                if (!ns->have_client) { ns->client = from; ns->have_client = true; }
                if (from.host != ns->client.host || from.port != ns->client.port) continue;
                admit(ns, 0, ns->buf, (size_t)r, now);
            } else {
                if (from.host != ns->server.host || from.port != ns->server.port) continue;
                admit(ns, 1, ns->buf, (size_t)r, now);
            }
        }
    }
    for (int dir = 0; dir < 2; dir++) {
        NsLane* L = &ns->lane[dir]; int sent = 0;
        while (sent < L->n && L->q[sent].due <= now) {
            NsPacket* p = &L->q[sent++];
            ENetBuffer b = {.data = p->data, .dataLength = p->len};
            if (dir == 0) enet_socket_send(ns->back, &ns->server, &b, 1);
            else if (ns->have_client) enet_socket_send(ns->front, &ns->client, &b, 1);
            ns->st.out[dir]++;
            free(p->data);
        }
        if (sent) { memmove(L->q, L->q + sent, sizeof(NsPacket) * (size_t)(L->n - sent)); L->n -= sent; }
    }
}

void cret_netsim_stats(const CretNetsim* ns, CretNetsimStats* st) { if (ns) *st = ns->st; else memset(st, 0, sizeof *st); }

void cret_netsim_close(CretNetsim* ns) {
    if (!ns) return;
    if (ns->front != ENET_SOCKET_NULL) enet_socket_destroy(ns->front);
    if (ns->back != ENET_SOCKET_NULL) enet_socket_destroy(ns->back);
    for (int d = 0; d < 2; d++) { for (int i = 0; i < ns->lane[d].n; i++) free(ns->lane[d].q[i].data); free(ns->lane[d].q); }
    free(ns);
}
