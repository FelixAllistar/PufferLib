// Headless loopback test of the respawn/species and profile protocol (net.c, version 8):
// a server and a client in one process, over ENet on 127.0.0.1. The server
// sim is never stepped (only its tick advances), so no bot can hit the
// client's animal behind the test's back. net.c is included, not linked, so
// the test can send malformed packets and read the server's peer state.
//   make -C ocean/cretaceous net-test      (runs from the repository root;
//   bakes a 256x256 world into build/cretaceous/worlds the first time)
#include "net.c"
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static CretInput inputs[CRET_MAX_AGENTS];
static bool remote[CRET_MAX_AGENTS];

static void sleep_ms(int ms) { struct timespec t = {0, (long)ms * 1000000L}; nanosleep(&t, NULL); }

// The client blocks in cret_net_join until the welcome, so the server is
// serviced from a thread meanwhile (the sim is not touched by the main thread then).
typedef struct { CretNet* srv; CretSim* s; atomic_int stop; } Pump;
static void* pump_thread(void* p) {
    Pump* q = p;
    while (!atomic_load(&q->stop)) { cret_net_server_poll(q->srv, q->s, inputs, remote); sleep_ms(1); }
    return NULL;
}

// One server tick: poll (settles requests), broadcast, then the client polls.
static void pump(CretNet* srv, CretSim* s, CretNet* cli, CretSim* cs) {
    cret_net_server_poll(srv, s, inputs, remote);
    cret_net_server_broadcast(srv, s);
    s->tick++;
    sleep_ms(1);
    if (cli) cret_net_client_poll(cli, cs);
}
static bool wait_reply(CretNet* srv, CretSim* s, CretNet* cli, CretSim* cs, CretSpawnReply* r, int max_ticks) {
    for (int i = 0; i < max_ticks; i++) {
        pump(srv, s, cli, cs);
        if (cret_net_client_spawn_reply(cli, r)) return true;
    }
    return false;
}
static void settle(CretNet* srv, CretSim* s, CretNet* cli, CretSim* cs, int ticks) { for (int i = 0; i < ticks; i++) pump(srv, s, cli, cs); }
static void send_raw(CretNet* cli, const uint8_t* p, size_t n) {
    send_bits(cli->server, CH_RELIABLE, p, n, true); enet_host_flush(cli->host);
}

int main(void) {
    setenv("CRET_NO_PROP_COLLIDERS", "1", 1);   // props are local assets; the protocol does not need them
    CretConfig cfg = cret_default_config();
    cfg.world_seed = 7; cfg.world_n = 256; cfg.num_agents = 4; cfg.seed = 3;
    CretSim* s = calloc(1, sizeof *s);
    if (cret_sim_init(s, cfg) != 0) { fprintf(stderr, "test_net_respawn: world failed\n"); return 1; }
    int port = 30000 + (int)(getpid() % 20000);
    CretNet* srv = cret_net_host(port, 1);   // listen-host layout: the first client gets slot 1
    if (!srv) { fprintf(stderr, "test_net_respawn: cannot host on %d\n", port); return 1; }

    // ---- join and welcome
    Pump q = {.srv = srv, .s = s}; atomic_init(&q.stop, 0);
    pthread_t th; pthread_create(&th, NULL, pump_thread, &q);
    CretNet* cli = cret_net_join("127.0.0.1", port, 3.0f);
    atomic_store(&q.stop, 1); pthread_join(th, NULL);
    if (!cli) { fprintf(stderr, "test_net_respawn: join failed\n"); return 1; }
    CretConfig ccfg; int me = -1;
    CHECK(cret_net_client_welcomed(cli, &ccfg, &me), "no welcome");
    CHECK(me == 1, "slot %d", me);
    CHECK(ccfg.num_agents == 4 && ccfg.world_n == 256, "agents %d n %d", ccfg.num_agents, ccfg.world_n);
    CHECK(s->species_pick[1] == 0, "a new player starts on the slot's default species");
    CHECK(s->human[1] && !s->human[2], "human flags %d %d", s->human[1], s->human[2]);
    CretSim* cs = calloc(1, sizeof *cs);
    if (cret_sim_init(cs, ccfg) != 0) { fprintf(stderr, "test_net_respawn: client world failed\n"); return 1; }
    settle(srv, s, cli, cs, 5);
    CHECK(remote[1], "slot 1 is not marked remote");

    // ---- a species request is settled at once, the server picks the spot, the snapshot carries it
    CretSpawnReply r = {0};
    int seq = cret_net_client_request(cli, CRET_SP_STEGO);
    CHECK(wait_reply(srv, s, cli, cs, &r, 500), "no reply to the stego request");
    CHECK(r.seq == seq && r.result == CRET_SPAWN_OK && r.species == CRET_SP_STEGO, "seq %d/%d result %d species %d", r.seq, seq, r.result, r.species);
    CHECK(s->agents[1].alive && s->agents[1].species == CRET_SP_STEGO, "server agent species %d", s->agents[1].species);
    CHECK(s->species_pick[1] == CRET_SP_STEGO + 1, "pick %d", s->species_pick[1]);
    CHECK(fabsf(s->agents[1].growth - cfg.player_growth) < 1e-4f, "a network player spawns like the player (growth %.3f)", s->agents[1].growth);
    int t_stego = srv->peers[0]->req_tick;

    // ---- within the cooldown: requests are held, the newest replaces the older, one reply
    int seq_pachy = cret_net_client_request(cli, CRET_SP_PACHY);
    int seq_galli = cret_net_client_request(cli, CRET_SP_GALLI);
    CHECK(wait_reply(srv, s, cli, cs, &r, 500), "no reply to the held request");
    CHECK(r.seq == seq_galli && r.seq != seq_pachy, "replied to seq %d (pachy %d, galli %d)", r.seq, seq_pachy, seq_galli);
    CHECK(r.result == CRET_SPAWN_OK && s->agents[1].species == CRET_SP_GALLI, "result %d species %d", r.result, s->agents[1].species);
    CHECK(srv->peers[0]->req_tick - t_stego >= CRET_NET_REQUEST_COOLDOWN, "respawned %d ticks after the last", srv->peers[0]->req_tick - t_stego);
    settle(srv, s, cli, cs, 10);
    CHECK(!cret_net_client_spawn_reply(cli, &r), "a second reply (seq %d) for the replaced request", r.seq);
    CHECK(cs->agents[1].species == CRET_SP_GALLI && cs->agents[1].alive, "client sees species %d alive %d", cs->agents[1].species, cs->agents[1].alive);
    CHECK(fabsf(cs->agents[1].x - s->agents[1].x) < 0.01f && fabsf(cs->agents[1].z - s->agents[1].z) < 0.01f, "client position differs");
    CHECK(fabsf(cs->cloud_cover - s->cloud_cover) <= 1.0f / 255 + 1e-4f, "cloud cover %.3f vs %.3f", cs->cloud_cover, s->cloud_cover);

    // ---- a living animal hit in the last 10 s cannot respawn; the lock lifts
    settle(srv, s, cli, cs, CRET_NET_REQUEST_COOLDOWN);
    s->agents[1].hurt_ticks = 8; settle(srv, s, cli, cs, 1); s->agents[1].hurt_ticks = 0;
    float x0 = s->agents[1].x, z0 = s->agents[1].z;
    seq = cret_net_client_request(cli, -1);
    CHECK(wait_reply(srv, s, cli, cs, &r, 500), "no reply in combat");
    CHECK(r.seq == seq && r.result == CRET_SPAWN_IN_COMBAT && r.wait_s > 0 && r.wait_s <= 10, "result %d wait %d", r.result, r.wait_s);
    CHECK(r.species == CRET_SP_GALLI && s->agents[1].x == x0 && s->agents[1].z == z0, "the refused request moved the animal");
    s->tick += CRET_NET_COMBAT_LOCK;
    seq = cret_net_client_request(cli, -1);
    CHECK(wait_reply(srv, s, cli, cs, &r, 500), "no reply after the lock");
    CHECK(r.seq == seq && r.result == CRET_SPAWN_OK && r.species == CRET_SP_GALLI, "result %d species %d (same species kept)", r.result, r.species);

    // ---- a dead animal respawns even right after a hit
    settle(srv, s, cli, cs, CRET_NET_REQUEST_COOLDOWN);
    s->agents[1].hurt_ticks = 8; settle(srv, s, cli, cs, 1); s->agents[1].hurt_ticks = 0; s->agents[1].alive = false;
    seq = cret_net_client_request(cli, CRET_SP_RAPTOR);
    CHECK(wait_reply(srv, s, cli, cs, &r, 500), "no reply when dead");
    CHECK(r.seq == seq && r.result == CRET_SPAWN_OK && s->agents[1].alive && s->agents[1].species == CRET_SP_RAPTOR, "result %d alive %d", r.result, s->agents[1].alive);

    // ---- validation: an unknown species byte is refused, a short packet ignored,
    // trailing bytes (a claimed position) never reach the sim
    settle(srv, s, cli, cs, CRET_NET_REQUEST_COOLDOWN);
    { uint8_t bad[3] = {MSG_REQUEST, 200, CRET_SPECIES + 3}; send_raw(cli, bad, sizeof bad); }
    CHECK(wait_reply(srv, s, cli, cs, &r, 500), "no reply to a bad species");
    CHECK(r.seq == 200 && r.result == CRET_SPAWN_BAD_SPECIES && s->agents[1].species == CRET_SP_RAPTOR, "result %d species %d", r.result, s->agents[1].species);
    { uint8_t shrt[2] = {MSG_REQUEST, 201}; send_raw(cli, shrt, sizeof shrt); }
    settle(srv, s, cli, cs, 20);
    CHECK(!cret_net_client_spawn_reply(cli, &r), "a truncated request got a reply (seq %d)", r.seq);
    CHECK(srv->peers[0]->pending == REQ_NONE, "a truncated request was held");
    {
        uint8_t raw[15] = {MSG_REQUEST, 202, CRET_SP_TREX}; float claim[3] = {-5000, 900, -5000};
        memcpy(raw + 3, claim, sizeof claim);
        send_raw(cli, raw, sizeof raw);
    }
    CHECK(wait_reply(srv, s, cli, cs, &r, 500), "no reply to a request with trailing bytes");
    float size = s->world->size;
    CHECK(r.seq == 202 && r.result == CRET_SPAWN_OK && s->agents[1].species == CRET_SP_TREX, "result %d species %d", r.result, s->agents[1].species);
    CHECK(s->agents[1].x >= 0 && s->agents[1].x <= size && s->agents[1].z >= 0 && s->agents[1].z <= size && s->agents[1].y < 800,
          "spawned at %.1f %.1f %.1f (the claimed position leaked)", s->agents[1].x, s->agents[1].y, s->agents[1].z);

    // ---- the character name reaches the server, marks and unlocks come back in snapshots
    cret_net_client_profile(cli, "Rexy");
    settle(srv, s, cli, cs, 20);
    CHECK(!strcmp(s->prog[1].name, "Rexy") && s->prog[1].load_pending, "profile name '%s' pending %d", s->prog[1].name, s->prog[1].load_pending);
    s->prog[1].load_pending = 0; s->prog[1].marks = 130;
    cret_net_client_unlock(cli, 0);   // trex slot-3 choice A (Rending Bite, 120 marks)
    settle(srv, s, cli, cs, 20);
    CHECK(cret_choice_unlocked(s, 1, 0) && s->prog[1].marks == 10, "unlock: marks %d", s->prog[1].marks);
    CHECK(cs->prog[1].marks == 10 && cs->prog[1].loadout[2] == CRET_AB_REND_BITE, "client sees marks %d slot 3 %d", cs->prog[1].marks, cs->prog[1].loadout[2]);

    // ---- v8: statuses (own timers in full, others as flags), cooldowns and nests reach the client
    s->agents[1].st[CRET_ST_SLOW] = 3.25f; s->agents[1].slow_amt = 0.4f; s->agents[1].ab_cd[0] = 7.5f;
    s->agents[2].st[CRET_ST_STUN] = 2; s->agents[2].st[CRET_ST_MARKED] = 5; s->agents[2].mark_by = 101 + 1;
    s->nests[3] = (CretNest){.active = true, .species = CRET_SP_STEGO, .group = 2, .owner = 3, .x = 50, .y = 10, .z = 60, .eggs = 4};
    settle(srv, s, cli, cs, 20);
    CHECK(fabsf(cs->agents[1].st[CRET_ST_SLOW] - 3.25f) < 0.01f && fabsf(cs->agents[1].slow_amt - 0.4f) < 0.01f && fabsf(cs->agents[1].ab_cd[0] - 7.5f) < 0.01f,
          "own slow %.3f (%.2f) cooldown %.3f", cs->agents[1].st[CRET_ST_SLOW], cs->agents[1].slow_amt, cs->agents[1].ab_cd[0]);
    CHECK(cs->agents[2].st[CRET_ST_STUN] > 0 && cs->agents[2].st[CRET_ST_MARKED] > 0 && cs->agents[2].mark_by == 102, "agent 2 stun %.1f marked %.1f by %d",
          cs->agents[2].st[CRET_ST_STUN], cs->agents[2].st[CRET_ST_MARKED], cs->agents[2].mark_by);
    CHECK(cs->nests[3].active && cs->nests[3].species == CRET_SP_STEGO && cs->nests[3].eggs == 4 && cs->nests[3].x == 50, "nest 3 active %d eggs %d", cs->nests[3].active, cs->nests[3].eggs);
    s->nests[3].active = false; settle(srv, s, cli, cs, 20);
    CHECK(!cs->nests[3].active, "a removed nest stays on the client");

    // ---- leaving clears the slot's pick: the bot (and the next player) get the default mix
    CHECK(s->species_pick[1] == CRET_SP_TREX + 1, "pick %d", s->species_pick[1]);
    cret_net_close(cli); cli = NULL;
    for (int i = 0; i < 500 && srv->peers[0]; i++) pump(srv, s, NULL, NULL);
    CHECK(!srv->peers[0], "the server did not see the disconnect");
    CHECK(s->species_pick[1] == 0 && !remote[1] && !s->human[1], "pick %d remote %d human %d after leaving", s->species_pick[1], remote[1], s->human[1]);

    cret_net_close(srv); cret_sim_close(cs); free(cs); cret_sim_close(s); free(s);
    if (failures) { fprintf(stderr, "test_net_respawn: %d failure(s)\n", failures); return 1; }
    fprintf(stderr, "test_net_respawn: ok\n");
    return 0;
}
