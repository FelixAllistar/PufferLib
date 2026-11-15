// Headless loopback test of joining while the world loads (protocol v20,
// netjoin.c; docs/net.md, Join and loading). A real server ticks at 30 Hz on
// its own thread with bots, two clients play on a second thread, and the main
// thread joins as a third client whose world load is made slow with
// CRET_TEST_SLOW_LOAD (world.c). The server's ENet timeouts are cut to 2 s,
// so a load of a few seconds stands in for a 45 s first bake.
//   1. A client that does not service its connection during the load (the old
//      join) is dropped by the server.
//   2. A client that does (cret_net_client_load_begin) joins: the server holds
//      its animal and sends it nothing until it is ready, then it plays; the
//      other clients get snapshots at 15 Hz throughout.
//   3. A client still loading after CRET_NET_LOAD_LIMIT is dropped, and says why.
// `test_net_join --long SECONDS` runs 1 and 2 with ENet's own timeouts and a
// load of that many seconds (45 for a first bake): about 2 x SECONDS.
// Runs from the repository root (the world cache).
#include "net.c"
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static void sleep_s(double s) { struct timespec t = {(time_t)s, (long)((s - (time_t)s) * 1e9)}; nanosleep(&t, NULL); }

// ---- the server: 30 Hz with bots, on its own thread. What the main thread
// reads is published through atomics.
typedef struct {
    CretNet* net; CretSim* s; bool short_timeouts;
    atomic_int stop, peers, loading, max_loading, held_slot;   // held_slot: ticks the joining slot was held since the main thread cleared it
    atomic_int join_slot;   // the slot whose holds are counted (-1: none)
    double tick_max_s;   // longest server tick (poll, bots, step, broadcast)
} Server;
static void* server_thread(void* p) {
    Server* v = p; CretSim* s = v->s;
    static CretInput inputs[CRET_MAX_AGENTS]; static bool remote[CRET_MAX_AGENTS];
    double next = now_s();
    while (!atomic_load(&v->stop)) {
        double t0 = now_s();
        for (int i = 0; i < s->cfg.num_agents; i++) inputs[i] = cret_bot_input(s, i);
        cret_net_server_poll(v->net, s, inputs, remote);
        if (v->short_timeouts)
            for (int k = 0; k < CRET_MAX_PEERS; k++) if (v->net->peers[k]) enet_peer_timeout(v->net->peers[k]->peer, 32, 1000, 2000);
        int js = atomic_load(&v->join_slot);
        if (js >= 0 && s->hold[js]) atomic_fetch_add(&v->held_slot, 1);
        cret_sim_step(s, inputs);
        if (s->tick % CRET_SNAPSHOT_DIV == 0) cret_net_server_broadcast(v->net, s);
        CretNetServerStats st; cret_net_server_stats(v->net, &st);
        int ld = cret_net_server_loading(v->net);
        atomic_store(&v->peers, st.peers); atomic_store(&v->loading, ld);
        if (ld > atomic_load(&v->max_loading)) atomic_store(&v->max_loading, ld);
        double dt = now_s() - t0; if (dt > v->tick_max_s) v->tick_max_s = dt;
        next += CRET_DT; double w = next - now_s();
        if (w > 0) sleep_s(w); else if (w < -0.5) next = now_s();
    }
    return NULL;
}

// ---- two clients that play throughout, on a second thread. Each second it
// records how many snapshots each received; the main thread marks the
// seconds that lie inside a slow load.
#define FAST 2
#define MAX_SECS 400
typedef struct {
    CretNet* c[FAST];
    atomic_int stop, in_load;
    int secs; uint16_t snaps[MAX_SECS][FAST]; uint8_t during[MAX_SECS];
} Fast;
static void* fast_thread(void* p) {
    Fast* f = p; double t_sec = now_s(), next = now_s(); uint64_t last[FAST] = {0};
    bool load_whole = atomic_load(&f->in_load);
    while (!atomic_load(&f->stop)) {
        for (int k = 0; k < FAST; k++) {
            CretInput in = {0}; in.forward = (k & 1) ? 0.5f : 0.0f;
            cret_net_client_poll(f->c[k], NULL); cret_net_client_tick(f->c[k], NULL, &in);
        }
        load_whole = load_whole && atomic_load(&f->in_load);
        if (now_s() - t_sec >= 1.0 && f->secs < MAX_SECS) {
            for (int k = 0; k < FAST; k++) {
                CretNetStats st; cret_net_client_stats(f->c[k], &st);
                f->snaps[f->secs][k] = (uint16_t)(st.snapshots - last[k]); last[k] = st.snapshots;
            }
            f->during[f->secs] = load_whole; f->secs++;
            t_sec += 1.0; load_whole = atomic_load(&f->in_load);
        }
        next += CRET_DT; double w = next - now_s();
        if (w > 0) sleep_s(w); else if (w < -0.5) next = now_s();
    }
    return NULL;
}

// Joins and waits for the welcome while the threads run.
static CretNet* join(int port, CretConfig* cfg, int* slot) {
    CretNet* c = cret_net_join("127.0.0.1", port, 5.0f);
    if (c && !cret_net_client_welcomed(c, cfg, slot)) { cret_net_close(c); c = NULL; }
    return c;
}

int main(int argc, char** argv) {
    double load_s = 3.0; bool lng = false;
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--long") && i + 1 < argc) { lng = true; load_s = atof(argv[++i]); }
    const int limit_s = 5;   // the load bound for part 3 (the default is CRET_NET_LOAD_LIMIT_S, 300 s)
    // Everything getenv reads is set before the threads start (setenv is not thread-safe).
    setenv("CRET_NO_PROP_COLLIDERS", "1", 1);
    if (!lng) { char b[16]; snprintf(b, sizeof b, "%d", limit_s); setenv("CRET_NET_LOAD_LIMIT", b, 1); }
    CretConfig cfg = cret_default_config();
    cfg.world_seed = 7; cfg.world_n = 256; cfg.num_agents = 8; cfg.seed = 3;
    CretSim* s = calloc(1, sizeof *s);
    if (cret_sim_init(s, cfg) != 0) { fprintf(stderr, "test_net_join: world failed\n"); return 1; }
    { char b[16]; snprintf(b, sizeof b, "%g", load_s); setenv("CRET_TEST_SLOW_LOAD", b, 1); }   // every later world load waits that long
    int port = 30000 + (int)((getpid() * 7) % 20000);
    CretNet* srv = cret_net_host(port, 0);
    if (!srv) { fprintf(stderr, "test_net_join: cannot host on %d\n", port); return 1; }
    Server sv = {.net = srv, .s = s, .short_timeouts = !lng}; atomic_store(&sv.join_slot, -1);
    pthread_t sth; pthread_create(&sth, NULL, server_thread, &sv);

    Fast fa = {0}; CretConfig cc; int slot;
    for (int k = 0; k < FAST; k++) {
        fa.c[k] = join(port, &cc, &slot);
        if (!fa.c[k]) { fprintf(stderr, "test_net_join: client %d could not join\n", k); return 1; }
    }
    pthread_t fth; pthread_create(&fth, NULL, fast_thread, &fa);
    sleep_s(1.5);
    double t0 = now_s();

    // ---- 1. the old join: the load blocks the connection, and the server drops it
    {
        CretNet* c = join(port, &cc, &slot);
        CHECK(c != NULL, "client could not join");
        if (c) {
            sleep_s(0.2);
            CHECK(atomic_load(&sv.peers) == FAST + 1, "%d peers after the join", atomic_load(&sv.peers));
            CretSim* cs = calloc(1, sizeof *cs);
            double l0 = now_s(); int rc = cret_sim_init(cs, cc); double lt = now_s() - l0;
            CHECK(rc == 0, "client world failed");
            int peers = atomic_load(&sv.peers);
            printf("join: a %.1f s load without servicing the connection: the server has %d of %d peers after it (%s)\n",
                   lt, peers, FAST + 1, peers == FAST ? "dropped" : "kept");
            CHECK(peers == FAST, "the server kept a client that went silent for %.1f s (%d peers)", lt, peers);
            if (rc == 0) cret_sim_close(cs);
            free(cs); cret_net_close(c);
        }
        sleep_s(0.5);
    }

    // ---- 2. the v20 join: the load's thread keeps the connection, the server waits
    {
        CretNet* c = join(port, &cc, &slot);
        CHECK(c != NULL, "client could not join");
        if (c) {
            atomic_store(&sv.held_slot, 0); atomic_store(&sv.join_slot, slot);
            CHECK(cret_net_client_load_begin(c), "load_begin failed");
            atomic_store(&fa.in_load, 1);
            CretSim* cs = calloc(1, sizeof *cs);
            double l0 = now_s(); int rc = cret_sim_init(cs, cc); double lt = now_s() - l0;
            CHECK(rc == 0, "client world failed");
            CHECK(cret_net_client_load_ok(c), "the connection was lost during the load");
            int loading = atomic_load(&sv.loading), held = atomic_load(&sv.held_slot), peers = atomic_load(&sv.peers);
            atomic_store(&fa.in_load, 0);
            CHECK(peers == FAST + 1, "%d peers during the load", peers);
            CHECK(loading == 1, "the server counts %d loading peers", loading);
            CHECK(held >= (int)(lt * CRET_TICK_HZ * 0.8), "the loading slot was held %d ticks of a %.1f s load", held, lt);
            bool ok = cret_net_client_load_end(c);   // the load's thread has stopped: the client is ours again
            CHECK(ok, "load_end failed");
            uint64_t during = c->cl ? c->cl->st.snapshots : 0;
            CHECK(during <= 2, "the server sent %llu snapshots to a loading client", (unsigned long long)during);
            sleep_s(0.3);
            CHECK(atomic_load(&sv.loading) == 0, "still %d loading after the end", atomic_load(&sv.loading));
            // Playing: one second of ticks; snapshots arrive and the slot is no longer held.
            cret_net_client_poll(c, NULL); cret_net_client_stats_reset(c); atomic_store(&sv.held_slot, 0);
            double p0 = now_s(), next = p0;
            while (now_s() - p0 < 1.0) {
                CretInput in = {0}; in.forward = 1;
                cret_net_client_poll(c, rc == 0 ? cs : NULL); cret_net_client_tick(c, rc == 0 ? cs : NULL, &in);
                next += CRET_DT; double w = next - now_s(); if (w > 0) sleep_s(w);
            }
            CretNetStats st; cret_net_client_stats(c, &st);
            int held_after = atomic_load(&sv.held_slot);
            printf("join: a %.1f s load with the connection serviced: joined; %llu snapshots before ready, %llu in the first second of play, slot held %d of 30 ticks\n",
                   lt, (unsigned long long)during, (unsigned long long)st.snapshots, held_after);
            CHECK(st.snapshots >= 12, "%llu snapshots in the first second of play", (unsigned long long)st.snapshots);
            CHECK(held_after <= 6, "the slot was held %d ticks of the first second of play", held_after);
            atomic_store(&sv.join_slot, -1);
            if (rc == 0) cret_sim_close(cs);
            free(cs); cret_net_close(c);
        }
        sleep_s(0.5);
    }

    // ---- 3. the bound: a client that is still loading after the limit is dropped
    if (!lng) {
        CretNet* c = join(port, &cc, &slot);
        CHECK(c != NULL, "client could not join");
        if (c) {
            CHECK(cret_net_client_load_begin(c), "load_begin failed");
            double l0 = now_s();
            while (cret_net_client_load_ok(c) && now_s() - l0 < limit_s + 4) sleep_s(0.05);
            double lt = now_s() - l0;
            CHECK(!cret_net_client_load_ok(c), "still connected after %.1f s of loading (limit %d s)", lt, limit_s);
            CHECK(lt >= limit_s - 0.5, "dropped after %.1f s, before the %d s limit", lt, limit_s);
            CHECK(!cret_net_client_load_end(c), "load_end succeeded after the drop");
            CHECK(c->reject == (NET_REJECT_LOAD | (uint32_t)limit_s), "disconnect data %08x", c->reject);
            printf("join: a client still loading after the %d s limit was dropped after %.1f s with the load reason\n", limit_s, lt);
            cret_net_close(c);
        }
    }

    // ---- the other clients' snapshot rate while the slow client loaded
    double elapsed = now_s() - t0;
    atomic_store(&fa.stop, 1); pthread_join(fth, NULL);
    int lo = 1 << 30, n = 0; double sum = 0;
    for (int i = 0; i < fa.secs; i++) {
        if (!fa.during[i]) continue;
        for (int k = 0; k < FAST; k++) { lo = fa.snaps[i][k] < lo ? fa.snaps[i][k] : lo; sum += fa.snaps[i][k]; n++; }
    }
    printf("join: the other %d clients during the load: %.1f snapshots/s on average, %d at least (15 sent), over %d client-seconds; longest server tick %.1f ms; %.1f s in all\n",
           FAST, n ? sum / n : 0, n ? lo : 0, n, sv.tick_max_s * 1000, elapsed);
    CHECK(n >= FAST, "no whole second inside the load (%d)", n);
    CHECK(n == 0 || lo >= 13, "a client got only %d snapshots in a second of the load", lo);
    for (int k = 0; k < FAST; k++) cret_net_close(fa.c[k]);
    atomic_store(&sv.stop, 1); pthread_join(sth, NULL);
    cret_net_close(srv); cret_sim_close(s); free(s);
    if (failures) { fprintf(stderr, "test_net_join: FAILED (%d)\n", failures); return 1; }
    printf("test_net_join: ok\n");
    return 0;
}
