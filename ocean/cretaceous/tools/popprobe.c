// Headless population probe: how far is the player from the nearest live
// animal over a solo session? Mirrors the player's flow: the title screen
// runs the bots (slot 0 is a bot too) for --title seconds, then Play
// respawns slot 0 as the player's species (cret_sim_set_species), and a saved
// character optionally restores a position (cret_sim_restore). The player
// then idles or explores for --minutes. No window, GPU or ENet.
//   make -C ocean/cretaceous popprobe && ./build/cretaceous/popprobe --minutes 20
//   ./build/cretaceous/popprobe --restore far --player explore --sessions 4
//   ./build/cretaceous/popprobe --pop 0      (the old population: no director)
// Run from the repository root (the world cache is build/cretaceous/worlds).
#include "../sim.h"
#include "../population.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int cmpf(const void* a, const void* b) { float x = *(const float*)a, y = *(const float*)b; return x < y ? -1 : x > y; }
static float pct(float* v, int n, float p) { if (!n) return 0; qsort(v, (size_t)n, sizeof *v, cmpf); int k = (int)(p * (n - 1) + 0.5f); return v[k]; }

typedef struct { float near_all, near_prey, near_threat; int within150, within300; } Sample;

static Sample sample(const CretSim* s, int me) {
    const CretAgent* a = &s->agents[me]; Sample r = {1e9f, 1e9f, 1e9f, 0, 0};
    bool carn = CRET_SPECIES_TABLE[a->species].diet == CRET_DIET_CARNIVORE;
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* b = &s->agents[j]; if (j == me || !b->alive) continue;
        float d = hypotf(b->x - a->x, b->z - a->z);
        r.near_all = fminf(r.near_all, d); r.within150 += d < 150; r.within300 += d < 300;
        bool bc = CRET_SPECIES_TABLE[b->species].diet == CRET_DIET_CARNIVORE;
        if (carn ? !bc : bc) r.near_threat = fminf(r.near_threat, d);   // carnivore: prey; herbivore: predators
        if (carn && !bc) r.near_prey = fminf(r.near_prey, d);
        if (!carn && bc) r.near_prey = fminf(r.near_prey, d);
    }
    return r;
}

// Explorer: long legs toward random walkable points 250-600 m away at a trot.
static CretInput explore(CretSim* s, int me, float* gx, float* gz, uint32_t* rng) {
    CretAgent* a = &s->agents[me]; CretInput in = {0};
    float dx = *gx - a->x, dz = *gz - a->z;
    if (dx * dx + dz * dz < 25 * 25 || (*gx == 0 && *gz == 0)) {
        for (int t = 0; t < 64; t++) {
            *rng ^= *rng << 13; *rng ^= *rng >> 17; *rng ^= *rng << 5;
            float ang = (*rng % 6283) * 0.001f, d = 250 + (*rng >> 8) % 350;
            float x = a->x + cosf(ang) * d, z = a->z + sinf(ang) * d;
            if (cret_walkable(s->world, x, z)) { *gx = x; *gz = z; break; }
        }
        dx = *gx - a->x; dz = *gz - a->z;
    }
    float want = atan2f(dz, dx), d = want - a->yaw;
    while (d > 3.14159f) d -= 6.28318f; while (d < -3.14159f) d += 6.28318f;
    in.turn = fmaxf(-1, fminf(1, d * 2)); in.forward = 1;
    // Feel ahead like the bots: water or a cliff turns it.
    float L = 8; if (!cret_walkable(s->world, a->x + cosf(a->yaw) * L, a->z + sinf(a->yaw) * L)) { in.turn = 1; *gx = *gz = 0; }
    if (a->water < 0.4f || a->food < 0.3f) { CretInput b = cret_bot_input(s, me); return b; }   // stay alive: let the bot drink and eat
    return in;
}

int main(int argc, char** argv) {
    float minutes = 20, title_s = 60; int sessions = 3, agents = -1, pop = 1; const char* player = "idle"; const char* restore = NULL;
    int species = CRET_SP_RAPTOR; uint32_t seed0 = 1000;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--minutes") && i + 1 < argc) minutes = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--title") && i + 1 < argc) title_s = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--sessions") && i + 1 < argc) sessions = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--agents") && i + 1 < argc) agents = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--player") && i + 1 < argc) player = argv[++i];
        else if (!strcmp(argv[i], "--restore") && i + 1 < argc) restore = argv[++i];
        else if (!strcmp(argv[i], "--pop") && i + 1 < argc) pop = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed0 = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--species") && i + 1 < argc) {
            const char* sp = argv[++i]; for (int k = 0; k < CRET_SPECIES; k++) if (!strcmp(sp, CRET_SPECIES_TABLE[k].asset)) species = k;
        } else { fprintf(stderr, "usage: popprobe [--minutes M] [--title S] [--sessions N] [--agents N] [--player idle|explore] [--restore far|X,Z] [--pop 0|1] [--species S] [--seed N]\n"); return 2; }
    }
    CretSim* s = calloc(1, sizeof *s);
    int ticks = (int)(minutes * 60 * CRET_TICK_HZ), every = CRET_TICK_HZ * 5;
    float* all = malloc(sizeof(float) * (ticks / every + 2) * sessions);
    float* prey = malloc(sizeof(float) * (ticks / every + 2) * sessions);
    int n = 0, lonely = 0, lonely_run_max = 0, deaths = 0; double ms = 0; long nt = 0;
    float sum150 = 0, sum300 = 0;
    printf("popprobe: %s player (%s), %.0f min after %.0f s of title, %s, population %s\n", player, CRET_SPECIES_TABLE[species].name, minutes, title_s,
           restore ? "restored position" : "fresh character", pop ? "director on" : "old (no director)");
    for (int ss = 0; ss < sessions; ss++) {
        CretConfig c = cret_default_config(); c.seed = seed0 + (uint32_t)ss * 7919u; c.player_species = species;
        if (pop) cret_pop_config(&c); if (agents > 0) c.num_agents = agents;
        if (cret_sim_init(s, c) != 0) { fprintf(stderr, "world failed\n"); return 1; }
        CretPop P; cret_pop_init(&P, s, pop);
        CretInput in[CRET_MAX_AGENTS];
        for (int t = 0; t < (int)(title_s * CRET_TICK_HZ); t++) {   // the title screen's flyover: everyone is a bot
            for (int i = 0; i < s->cfg.num_agents; i++) in[i] = cret_bot_input(s, i);
            cret_sim_step(s, in); cret_pop_step(&P, s);
        }
        cret_sim_set_species(s, 0, species);
        if (restore) {
            float x = s->world->size * 0.15f, z = s->world->size * 0.15f;
            if (strcmp(restore, "far")) sscanf(restore, "%f,%f", &x, &z);
            else {   // a walkable spot at least 2 km from every animal (where a previous session ended)
                for (int t = 0; t < 4000; t++) {
                    float px = s->world->size * (0.1f + 0.8f * (float)((t * 7919u) % 1000) / 1000), pz = s->world->size * (0.1f + 0.8f * (float)((t * 104729u) % 997) / 997);
                    if (!cret_walkable(s->world, px, pz)) continue;
                    float dmin = 1e9f; for (int j = 1; j < s->cfg.num_agents; j++) if (s->agents[j].alive) dmin = fminf(dmin, hypotf(s->agents[j].x - px, s->agents[j].z - pz));
                    if (dmin > 2000) { x = px; z = pz; break; }
                }
            }
            cret_sim_restore(s, 0, 0.6f, 1, 1, 1, true, x, z);
            cret_pop_player_placed(&P, s, 0);
        }
        Sample s0 = sample(s, 0);
        printf("  session %d: player at %.0f %.0f, nearest animal %.0f m at the start, %d agents\n", ss, s->agents[0].x, s->agents[0].z, s0.near_all, s->cfg.num_agents);
        float gx = 0, gz = 0; uint32_t rng = 12345u + ss; int run = 0; bool was = true;
        for (int t = 0; t < ticks; t++) {
            struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
            for (int i = 0; i < s->cfg.num_agents; i++) in[i] = cret_bot_input(s, i);
            if (!strcmp(player, "explore")) in[0] = explore(s, 0, &gx, &gz, &rng);
            else if (!strcmp(player, "idle")) in[0] = (CretInput){0};
            if (s->agents[0].water < 0.3f || s->agents[0].food < 0.2f) in[0] = cret_bot_input(s, 0);
            cret_sim_step(s, in); cret_pop_step(&P, s);
            clock_gettime(CLOCK_MONOTONIC, &t1); ms += (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) * 1e-6; nt++;
            if (!s->agents[0].alive) { if (was) deaths++; was = false; if (s->agents[0].dead_ticks > 60) cret_sim_respawn(s, 0); continue; }
            was = true;
            if (t % every) continue;
            Sample q = sample(s, 0); all[n] = q.near_all; prey[n] = q.near_prey; n++;
            sum150 += q.within150; sum300 += q.within300;
            if (q.near_all > 300) { run += every; lonely += every; if (run > lonely_run_max) lonely_run_max = run; } else run = 0;
        }
        cret_sim_close(s);
    }
    float tot = (float)n;
    float a50 = pct(all, n, 0.5f), a90 = pct(all, n, 0.9f), p50 = pct(prey, n, 0.5f), p90 = pct(prey, n, 0.9f);
    printf("nearest animal: median %.0f m, p90 %.0f m; nearest prey/threat: median %.0f m, p90 %.0f m\n", a50, a90, p50, p90);
    printf("time with nothing within 300 m: %.0f%%; longest stretch %.1f min; animals within 150 m: %.1f, within 300 m: %.1f (mean)\n",
           100.0f * lonely / (float)(ticks * sessions), lonely_run_max / (60.0f * CRET_TICK_HZ), sum150 / tot, sum300 / tot);
    printf("player deaths: %d; sim + bots %.3f ms per tick\n", deaths, ms / nt);
    free(all); free(prey); free(s);
    return 0;
}
