// v20: joining while the world loads (docs/net.md, Join and loading).
// Included by net.c.
//
// A client that has the welcome loads or bakes the world before it can play
// (a load from the cache takes a few seconds, a first bake about 45 s). ENet
// drops a peer whose reliable packets go unacknowledged for 30 s, so the
// client services its connection from a thread of its own meanwhile and tells
// the server how far it is (MSG_LOADING, twice a second). The server holds a
// loading client's animal, sends it no snapshots and never waits for it: the
// sim and every other client go on at their own pace. A client still loading
// after CRET_NET_LOAD_LIMIT_S is dropped with NET_REJECT_LOAD, which the
// client reports.

#define NET_LOAD_REPORT_S 0.5   // seconds between two MSG_LOADING from a loading client
#define NET_LOAD_LOG_S 10       // seconds between two server log lines about a peer still loading

enum { NET_LOAD_NONE, NET_LOAD_ON, NET_LOAD_DROPPED, NET_LOAD_DONE };   // NetPeer.loading

static bool join_loading(const NetPeer* np) { return np->loading == NET_LOAD_ON || np->loading == NET_LOAD_DROPPED; }

static int load_limit_s(void) {
    const char* e = getenv("CRET_NET_LOAD_LIMIT");
    int v = e ? atoi(e) : 0;
    return v > 0 ? v : CRET_NET_LOAD_LIMIT_S;
}

// ---------------------------------------------------------------- server
bool netp_server_loading(NetPeer* np, const CretSim* s, const uint8_t* d, size_t len) {
    Bits b = {(uint8_t*)d, len, 0, false};
    if (bits_get(&b, 8) != MSG_LOADING) return false;
    bool done = bits_get(&b, 1); float frac = bits_get(&b, 8) / 255.0f;
    int n = (int)bits_get(&b, 5); char stage[32] = {0};
    for (int c = 0; c < n; c++) { int ch = (int)bits_get(&b, 8); stage[c] = ch >= 32 && ch < 127 ? (char)ch : '?'; }
    if (b.bad) return false;
    if (np->loading == NET_LOAD_DROPPED || np->loading == NET_LOAD_DONE) return true;   // a load ends once
    memcpy(np->load_stage, stage, sizeof stage); np->load_frac = frac;
    if (done) {
        if (np->loading == NET_LOAD_ON)
            fprintf(stderr, "cretaceous: slot %d loaded the world in %.0f s\n", np->slot, (double)(s->tick - np->load_tick) / CRET_TICK_HZ);
        np->loading = NET_LOAD_DONE;
    } else if (np->loading == NET_LOAD_NONE) {
        np->loading = NET_LOAD_ON; np->load_tick = s->tick;
        fprintf(stderr, "cretaceous: slot %d is loading the world (%s)\n", np->slot, stage);
    }
    return true;
}

bool netp_server_join_wait(CretNet* n, NetPeer* np, CretSim* s, CretInput* inputs, bool* remote) {
    if (!join_loading(np)) return false;
    if (np->loading == NET_LOAD_ON && np->newest > 0) {   // an input before the done message: it is playing
        fprintf(stderr, "cretaceous: slot %d loaded the world in %.0f s\n", np->slot, (double)(s->tick - np->load_tick) / CRET_TICK_HZ);
        np->loading = NET_LOAD_DONE; return false;
    }
    int slot = np->slot; remote[slot] = true; s->hold[slot] = 1; inputs[slot] = (CretInput){0};
    if (s->tick < np->load_tick) np->load_tick = s->tick;   // the sim was reset
    int waited = s->tick - np->load_tick, limit = load_limit_s();
    if (np->loading == NET_LOAD_ON && waited > limit * CRET_TICK_HZ) {
        fprintf(stderr, "cretaceous: dropped slot %d: still loading the world after %d s (%s, %.0f%%)\n", slot, limit, np->load_stage, np->load_frac * 100);
        if (np->peer) enet_peer_disconnect(np->peer, NET_REJECT_LOAD | (uint32_t)limit);
        np->loading = NET_LOAD_DROPPED;
    } else if (np->loading == NET_LOAD_ON && waited > 0 && waited % (NET_LOAD_LOG_S * CRET_TICK_HZ) == 0)
        fprintf(stderr, "cretaceous: slot %d still loading the world after %d s (%s, %.0f%%)\n", slot, waited / CRET_TICK_HZ, np->load_stage, np->load_frac * 100);
    (void)n;
    return true;
}

int cret_net_server_loading(const CretNet* n) {
    int k = 0;
    for (int i = 0; n && i < CRET_MAX_PEERS; i++) k += n->peers[i] && n->peers[i]->loading == NET_LOAD_ON;
    return k;
}

// ---------------------------------------------------------------- client
size_t netp_client_loading(uint8_t* out, size_t cap, bool done, const char* stage, float frac) {
    Bits b = {out, cap, 0, false};
    int len = stage ? (int)strnlen(stage, 31) : 0;
    bits_put(&b, MSG_LOADING, 8); bits_put(&b, done, 1); bits_put(&b, q8(frac), 8); bits_put(&b, (uint32_t)len, 5);
    for (int c = 0; c < len; c++) bits_put(&b, (uint8_t)stage[c], 8);
    return b.bad ? 0 : bits_bytes(&b);
}

// The current load status (world.h) to the server.
static void send_loading(CretNet* n, bool done) {
    char stage[64]; float f = cret_load_status(stage, sizeof stage);
    uint8_t raw[48]; size_t len = netp_client_loading(raw, sizeof raw, done, stage, f);
    if (len && n->server) send_bits(n->server, CH_RELIABLE, raw, len, true);
    enet_host_flush(n->host); cret_netsim_pump(n->netsim);
}

// The load's own thread: every 5 ms it services ENet (acknowledging the
// server's packets and pings, and running the link conditioner's relay), and
// twice a second it sends the load's progress.
static void* load_thread(void* p) {
    CretNet* n = p; double next = 0;
    while (!atomic_load(&n->load_stop)) {
        if (!cret_net_client_poll(n, NULL)) { atomic_store(&n->load_lost, 1); break; }
        double now = cret_netsim_now();
        if (now >= next) { send_loading(n, false); next = now + NET_LOAD_REPORT_S; }
        struct timespec ts = {0, 5000000L}; nanosleep(&ts, NULL);
    }
    return NULL;
}

bool cret_net_client_load_begin(CretNet* n) {
    if (!n || !n->welcomed || !n->server || n->failed || n->load_on) return false;
    atomic_store(&n->load_stop, 0); atomic_store(&n->load_lost, 0);
    send_loading(n, false);
    n->load_on = pthread_create(&n->load_thread, NULL, load_thread, n) == 0;
    return n->load_on;
}

static void load_stop(CretNet* n) {
    if (!n->load_on) return;
    atomic_store(&n->load_stop, 1); pthread_join(n->load_thread, NULL); n->load_on = false;
}

bool cret_net_client_load_end(CretNet* n) {
    if (!n) return false;
    load_stop(n);
    cret_net_client_poll(n, NULL);
    if (n->failed || !n->server) {
        if (n->reject >> 24 == NET_REJECT_LOAD >> 24)
            fprintf(stderr, "cretaceous: the server stopped waiting after %u s for this client to load the world\n", n->reject & 0xffffff);
        else fprintf(stderr, "cretaceous: lost the server while loading the world\n");
        return false;
    }
    send_loading(n, true);
    return true;
}

bool cret_net_client_load_ok(const CretNet* n) { return n && !atomic_load(&n->load_lost); }
