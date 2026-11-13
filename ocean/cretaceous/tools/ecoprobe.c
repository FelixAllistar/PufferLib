// Headless ecosystem probe (docs/ecosystem.md, section 11): how the scripted
// animals live. Three parts, each optional:
//   world    bots only on the island (the solo and host population, 48 animals)
//            for --days game days per seed: time budget per species, hunts
//            started, abandoned and succeeded, kills and energy, deaths by
//            cause, population swings, herd range, carrying, cost;
//   person   one scripted person of each species (the bots' own AI drives it,
//            the population director is on, as in solo play), --minutes per
//            session: time to the first sighting of prey (carnivores) or of a
//            predator (herbivores), encounters and attacks per 10 minutes;
//   stalk    an adult predator played like a person against a grazing young
//            herbivore, alone or in its herd: a careful stalk (walk in, creep
//            while its head is down, freeze while it looks up, charge from
//            close) against a blind charge from 60 m; the catch rate (a bite
//            lands within 40 s), like tools/catchprobe.c does for critters.
//   make -C ocean/cretaceous ecoprobe && ./build/cretaceous/ecoprobe
//   ./build/cretaceous/ecoprobe --only world --days 10 --seeds 2
//   ./build/cretaceous/ecoprobe --quick --check ocean/cretaceous/tests/ecoprobe_budget.txt
// Run from the repository root (the world cache is build/cretaceous/worlds).
#include "../sim.h"
#include "../eco.h"
#include "../population.h"
#include "../progress.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now(void) { struct timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static float wrap(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }
static const char* SHORT[CRET_SPECIES] = {"raptor", "stego", "trex", "pachy", "sauro", "galli"};
static bool herb(int sp) { return CRET_SPECIES_TABLE[sp].diet == CRET_DIET_HERBIVORE; }

// ------------------------------------------------------------------ results
// Every number the probe prints is also a named result, so a budget file can
// hold any of them: `name min max` per line (tests/ecoprobe_budget.txt).
#define MAX_RES 256
static struct { char name[48]; float v; } RES[MAX_RES]; static int NRES;
static void res(const char* name, float v) { if (NRES < MAX_RES) { snprintf(RES[NRES].name, sizeof RES[NRES].name, "%s", name); RES[NRES++].v = v; } }
static int check_budget(const char* path) {
    FILE* f = fopen(path, "r"); if (!f) { fprintf(stderr, "ecoprobe: no budget file %s\n", path); return 1; }
    char line[256]; int bad = 0, n = 0;
    while (fgets(line, sizeof line, f)) {
        char name[64]; float lo, hi;
        if (line[0] == '#' || sscanf(line, "%63s %f %f", name, &lo, &hi) != 3) continue;
        int k = 0; while (k < NRES && strcmp(RES[k].name, name)) k++;
        n++;
        if (k == NRES) { printf("  budget: %s not measured\n", name); bad++; continue; }
        if (!(RES[k].v >= lo && RES[k].v <= hi)) { printf("  FAIL budget: %s = %.3f, want %.3f .. %.3f\n", name, RES[k].v, lo, hi); bad++; }
    }
    fclose(f);
    printf("ecoprobe budget: %d of %d within range\n", n - bad, n);
    return bad ? 1 : 0;
}

// ------------------------------------------------------------------ world run
// Time budget: what an animal does this tick, from its activity and action.
enum { TB_GRAZE, TB_VIGIL, TB_TRAVEL, TB_REST, TB_FLEE, TB_DEFEND, TB_HUNT, TB_FEED, TB_DRINK, TB_N };
static const char* TB_NAME[TB_N] = {"graze", "vigil", "travel", "rest", "flee", "defend", "hunt", "feed", "drink"};
static int budget_of(const CretAgent* a) {
    bool h = herb(a->species), eat = a->act == CRET_ACT_EAT, moving = a->speed > 0.4f;
    switch (a->bot_state) {
    case BOT_FLEE: return TB_FLEE;
    case BOT_DEFEND: return TB_DEFEND;
    case BOT_HUNT: return TB_HUNT;
    case BOT_FEED: case BOT_RAID: return eat ? TB_FEED : TB_TRAVEL;
    case BOT_GRAZE: return eat ? TB_GRAZE : moving ? TB_TRAVEL : TB_VIGIL;
    case BOT_DRINK: return a->act == CRET_ACT_DRINK ? TB_DRINK : TB_TRAVEL;
    case BOT_REST: case BOT_NEST: return moving ? TB_TRAVEL : TB_REST;
    default: return eat ? (h ? TB_GRAZE : TB_FEED) : a->act == CRET_ACT_DRINK ? TB_DRINK : moving ? TB_TRAVEL : TB_REST;
    }
}

typedef struct { int target, start; } Hunt;
typedef struct {
    double budget[CRET_SPECIES][TB_N], day_budget[CRET_SPECIES][TB_N];   // ticks; day_: daylight only
    double slot_days[CRET_SPECIES];                                    // animal-days alive
    long hunts[CRET_SPECIES], abandoned[CRET_SPECIES], succeeded[CRET_SPECIES];
    double hunt_s[CRET_SPECIES];                                       // seconds spent in hunts that ended
    double pop_sum[CRET_SPECIES]; int pop_min[CRET_SPECIES], pop_max[CRET_SPECIES], pop_n;   // alive per species, sampled each game hour
    double cells[CRET_SPECIES]; long cell_days[CRET_SPECIES];          // 32 m cells visited per animal per day
    double water_d[CRET_SPECIES]; long water_n[CRET_SPECIES];          // distance to water, m
    int groups_young_days;                                             // predator group-days with young (growth < 0.6) alive
    double bots_s; long ticks;
    CretStats st;
} WorldRes;

// Distance to the nearest water (m) on a coarse grid, by a two-pass chamfer
// transform from the cells holding water.
typedef struct { int n; float cell; float* d; } WaterField;
static void water_field(const CretWorld* w, WaterField* f) {
    f->cell = 32.0f; f->n = (int)(w->size / f->cell); int n = f->n; f->d = malloc(sizeof(float) * n * n);
    for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) {
        float px = (x + 0.5f) * f->cell, pz = (z + 0.5f) * f->cell;
        f->d[z * n + x] = cret_water(w, px, pz) > cret_height(w, px, pz) + 0.3f ? 0 : 1e9f;
    }
    float c = f->cell, dg = c * 1.4142f;
    for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) {
        float* v = &f->d[z * n + x];
        if (x > 0) *v = fminf(*v, v[-1] + c);
        if (z > 0) { *v = fminf(*v, v[-n] + c); if (x > 0) *v = fminf(*v, v[-n - 1] + dg); if (x < n - 1) *v = fminf(*v, v[-n + 1] + dg); }
    }
    for (int z = n - 1; z >= 0; z--) for (int x = n - 1; x >= 0; x--) {
        float* v = &f->d[z * n + x];
        if (x < n - 1) *v = fminf(*v, v[1] + c);
        if (z < n - 1) { *v = fminf(*v, v[n] + c); if (x < n - 1) *v = fminf(*v, v[n + 1] + dg); if (x > 0) *v = fminf(*v, v[n - 1] + dg); }
    }
}
static float water_dist(const WaterField* f, float x, float z) {
    int gx = (int)(x / f->cell), gz = (int)(z / f->cell);
    if (gx < 0 || gz < 0 || gx >= f->n || gz >= f->n) return 0;
    return f->d[gz * f->n + gx];
}

static void stats_add(CretStats* t, const CretStats* s) {
    for (int a = 0; a < CRET_SPECIES; a++) {
        for (int b = 0; b < CRET_SPECIES; b++) t->kills[a][b] += s->kills[a][b];
        t->deaths[a] += s->deaths[a]; t->deaths_env[a] += s->deaths_env[a];
        for (int c = 0; c < CRET_DEATH_CAUSES; c++) t->death_cause[a][c] += s->death_cause[a][c];
        t->eaten_kg[a] += s->eaten_kg[a]; t->burned_kg[a] += s->burned_kg[a];
    }
    t->pickups += s->pickups; t->carried_home += s->carried_home; t->forced_drops += s->forced_drops;
    t->nests_built += s->nests_built; t->eggs_laid += s->eggs_laid; t->eggs_eaten += s->eggs_eaten; t->nest_spawns += s->nest_spawns;
}

static void run_world(WorldRes* R, int seeds, float days, int agents, bool quiet) {
    memset(R, 0, sizeof *R);
    for (int b = 0; b < CRET_SPECIES; b++) R->pop_min[b] = 1 << 30;
    CretSim* s = calloc(1, sizeof *s); WaterField wf = {0};
    for (int seed = 1; seed <= seeds; seed++) {
        CretConfig c = cret_default_config();
        c.num_agents = agents; c.seed = (uint32_t)seed; c.weather = 0; c.max_ticks = 1 << 30; c.player_growth = -1;
        if (cret_sim_init(s, c) != 0) { fprintf(stderr, "ecoprobe: world failed\n"); exit(1); }
        if (!wf.d) water_field(s->world, &wf);
        int day_ticks = (int)(c.day_seconds * CRET_TICK_HZ), hour = day_ticks / 24, ticks = (int)(days * day_ticks);
        Hunt hunt[CRET_MAX_AGENTS]; for (int i = 0; i < agents; i++) hunt[i].target = -1;
        int ncell = wf.n * wf.n; uint8_t* seen = calloc((size_t)agents * ncell, 1);
        CretInput in[CRET_MAX_AGENTS];
        for (int t = 0; t < ticks; t++) {
            double a0 = now();
            for (int i = 0; i < agents; i++) in[i] = cret_bot_input(s, i);
            R->bots_s += now() - a0;
            cret_sim_step(s, in);
            bool day = s->time_of_day >= 0.25f && s->time_of_day < 0.75f;
            for (int i = 0; i < agents; i++) {
                const CretAgent* a = &s->agents[i]; int sp = a->species;
                // Hunts: a bot hunting a herbivore; it ends when the bot stops (or switches prey).
                Hunt* h = &hunt[i]; int tg = a->alive && a->bot_state == BOT_HUNT ? a->bot_target : -1;
                if (tg >= 0 && (tg >= agents || !herb(s->agents[tg].species) || !s->agents[tg].alive)) tg = h->target == tg ? tg : -1;
                if (h->target >= 0 && tg != h->target) {
                    const CretAgent* q = &s->agents[h->target]; int k = q->last_attacker;
                    bool won = !q->alive && k >= 0 && (k == i || (a->group && s->agents[k].group == a->group));
                    if (won) R->succeeded[sp]++; else if (q->alive) R->abandoned[sp]++;
                    R->hunt_s[sp] += (t - h->start) / (float)CRET_TICK_HZ;
                    h->target = -1;
                }
                if (tg >= 0 && h->target < 0) { h->target = tg; h->start = t; R->hunts[sp]++; }
                if (!a->alive) continue;
                int cat = budget_of(a);
                R->budget[sp][cat]++; if (day) R->day_budget[sp][cat]++;
                R->slot_days[sp] += 1.0 / day_ticks;
                if (herb(sp)) {
                    int gx = (int)(a->x / wf.cell), gz = (int)(a->z / wf.cell);
                    if (gx >= 0 && gz >= 0 && gx < wf.n && gz < wf.n) seen[(size_t)i * ncell + gz * wf.n + gx] = 1;
                    if (t % CRET_TICK_HZ == 0) { R->water_d[sp] += water_dist(&wf, a->x, a->z); R->water_n[sp]++; }
                }
            }
            if (t % hour == 0) {
                int alive[CRET_SPECIES] = {0};
                for (int i = 0; i < agents; i++) alive[s->agents[i].species] += s->agents[i].alive;
                for (int b = 0; b < CRET_SPECIES; b++) {
                    R->pop_sum[b] += alive[b]; if (alive[b] < R->pop_min[b]) R->pop_min[b] = alive[b]; if (alive[b] > R->pop_max[b]) R->pop_max[b] = alive[b];
                }
                R->pop_n++;
            }
            if ((t + 1) % day_ticks == 0) {   // end of a day: cells visited per herbivore; predator groups with young
                for (int i = 0; i < agents; i++) {
                    int sp = s->agents[i].species; if (!herb(sp)) continue;
                    long n = 0; uint8_t* m = seen + (size_t)i * ncell; for (int k = 0; k < ncell; k++) n += m[k];
                    R->cells[sp] += n; R->cell_days[sp]++;
                }
                memset(seen, 0, (size_t)agents * ncell);
                for (int g = 1; g <= agents; g++) {
                    bool pred = false, young = false;
                    for (int i = 0; i < agents; i++) {
                        const CretAgent* a = &s->agents[i]; if (!a->alive || a->group != g) continue;
                        pred |= !herb(a->species); young |= a->growth < 0.6f;
                    }
                    R->groups_young_days += pred && young;
                }
            }
        }
        R->ticks += ticks;
        stats_add(&R->st, &s->stats);
        free(seen);
        if (!quiet) { printf("  world seed %d: %d deaths\n", seed, s->stats.deaths[0] + s->stats.deaths[1] + s->stats.deaths[2] + s->stats.deaths[3] + s->stats.deaths[4] + s->stats.deaths[5]); fflush(stdout); }
        cret_sim_close(s);
    }
    free(wf.d); free(s);
}

static void print_world(const WorldRes* R, int seeds, float days, int agents) {
    char k[64];
    printf("\n## World: bots only, %d animals, %d seeds x %.1f game days\n", agents, seeds, days);
    printf("\nTime budget (%% of time alive; herbivores over daylight, predators over the whole day)\n\n| species |");
    for (int c = 0; c < TB_N; c++) printf(" %s |", TB_NAME[c]);
    printf("\n|---|"); for (int c = 0; c < TB_N; c++) printf("---|"); printf("\n");
    for (int b = 0; b < CRET_SPECIES; b++) {
        const double* B = herb(b) ? R->day_budget[b] : R->budget[b]; double tot = 0; for (int c = 0; c < TB_N; c++) tot += B[c];
        printf("| %s |", SHORT[b]);
        for (int c = 0; c < TB_N; c++) {
            float v = tot > 0 ? (float)(100 * B[c] / tot) : 0; printf(" %.0f |", v);
            snprintf(k, sizeof k, "budget.%s.%s", SHORT[b], TB_NAME[c]); res(k, v);
        }
        printf("\n");
    }
    printf("\nHunts per predator per day (a hunt: a bot chasing one herbivore), and energy\n\n");
    printf("| species | animal-days | hunts | abandoned | succeeded | success %% | abandoned %% | mean hunt s | kills/day | eaten/burned |\n|---|---|---|---|---|---|---|---|---|---|\n");
    for (int b = 0; b < CRET_SPECIES; b++) {
        if (herb(b)) continue;
        double dd = R->slot_days[b] > 0 ? R->slot_days[b] : 1; int kills = 0; for (int v = 0; v < CRET_SPECIES; v++) kills += herb(v) ? R->st.kills[b][v] : 0;
        long n = R->hunts[b]; float sr = n ? 100.0f * R->succeeded[b] / n : 0, ar = n ? 100.0f * R->abandoned[b] / n : 0;
        float ratio = R->st.burned_kg[b] > 0 ? (float)(R->st.eaten_kg[b] / R->st.burned_kg[b]) : 0;
        printf("| %s | %.1f | %.2f | %.2f | %.2f | %.0f | %.0f | %.0f | %.2f | %.2f |\n", SHORT[b], dd, n / dd, R->abandoned[b] / dd, R->succeeded[b] / dd, sr, ar,
               n ? R->hunt_s[b] / n : 0, kills / dd, ratio);
        snprintf(k, sizeof k, "hunt.%s.per_day", SHORT[b]); res(k, (float)(n / dd));
        snprintf(k, sizeof k, "hunt.%s.success", SHORT[b]); res(k, sr);
        snprintf(k, sizeof k, "hunt.%s.abandoned", SHORT[b]); res(k, ar);
        snprintf(k, sizeof k, "kills.%s.per_day", SHORT[b]); res(k, (float)(kills / dd));
        snprintf(k, sizeof k, "energy.%s.ratio", SHORT[b]); res(k, ratio);
    }
    printf("\nHerbivores: food eaten against burned\n\n| species | eaten/burned |\n|---|---|\n");
    for (int b = 0; b < CRET_SPECIES; b++) if (herb(b)) {
        float ratio = R->st.burned_kg[b] > 0 ? (float)(R->st.eaten_kg[b] / R->st.burned_kg[b]) : 0;
        printf("| %s | %.2f |\n", SHORT[b], ratio); snprintf(k, sizeof k, "energy.%s.ratio", SHORT[b]); res(k, ratio);
    }
    printf("\nDeaths per animal-day by cause, and population (alive, sampled each game hour)\n\n");
    printf("| species | deaths/day | predation | starved | thirst | other | starved %% | pop mean | min/mean | max/mean |\n|---|---|---|---|---|---|---|---|---|---|\n");
    for (int b = 0; b < CRET_SPECIES; b++) {
        double dd = R->slot_days[b] > 0 ? R->slot_days[b] : 1; const int* C = R->st.death_cause[b]; int n = R->st.deaths[b];
        float mean = R->pop_n ? (float)(R->pop_sum[b] / R->pop_n) : 0, st = n ? 100.0f * C[CRET_DEATH_STARVED] / n : 0;
        printf("| %s | %.2f | %.2f | %.2f | %.2f | %.2f | %.0f | %.1f | %.2f | %.2f |\n", SHORT[b], n / dd, C[0] / dd, C[1] / dd, C[2] / dd, C[3] / dd, st,
               mean, mean > 0 ? R->pop_min[b] / mean : 0, mean > 0 ? R->pop_max[b] / mean : 0);
        snprintf(k, sizeof k, "deaths.%s.per_day", SHORT[b]); res(k, (float)(n / dd));
        snprintf(k, sizeof k, "deaths.%s.predation_share", SHORT[b]); res(k, n ? 100.0f * C[CRET_DEATH_PREDATION] / n : 0);
        snprintf(k, sizeof k, "deaths.%s.starved_share", SHORT[b]); res(k, st);
        snprintf(k, sizeof k, "pop.%s.min_ratio", SHORT[b]); res(k, mean > 0 ? R->pop_min[b] / mean : 0);
        snprintf(k, sizeof k, "pop.%s.max_ratio", SHORT[b]); res(k, mean > 0 ? R->pop_max[b] / mean : 0);
    }
    printf("\nHerd range (herbivores): 32 m cells visited per animal per day (area), mean distance to water\n\n| species | cells/day | km2/day | water m |\n|---|---|---|---|\n");
    for (int b = 0; b < CRET_SPECIES; b++) if (herb(b)) {
        float cells = R->cell_days[b] ? (float)(R->cells[b] / R->cell_days[b]) : 0, wd = R->water_n[b] ? (float)(R->water_d[b] / R->water_n[b]) : 0;
        printf("| %s | %.0f | %.2f | %.0f |\n", SHORT[b], cells, cells * 0.001024f, wd);
        snprintf(k, sizeof k, "range.%s.cells", SHORT[b]); res(k, cells);
        snprintf(k, sizeof k, "range.%s.water_m", SHORT[b]); res(k, wd);
    }
    float gd = R->groups_young_days ? (float)R->st.carried_home / R->groups_young_days : 0;
    printf("\nCarrying: %d picked up, %d put down at the nest, %d dropped; predator group-days with young %d: %.2f carried home per group-day\n",
           R->st.pickups, R->st.carried_home, R->st.forced_drops, R->groups_young_days, gd);
    printf("Nests built %d, eggs laid %d, eaten %d, hatched as respawns %d\n", R->st.nests_built, R->st.eggs_laid, R->st.eggs_eaten, R->st.nest_spawns);
    res("carry.per_group_day", gd); res("carry.pickups_per_day", (float)(R->st.pickups / (seeds * days)));
    float ms = (float)(R->bots_s / R->ticks * 1e3);
    printf("Cost: bots %.4f ms a tick, %.2f us per animal\n", ms, ms * 1e3f / agents);
    res("cost.bot_us_per_animal", ms * 1e3f / agents);
}

// ------------------------------------------------------------------ person runs
// Is b what person a looks for: prey it could hunt (carnivore), or a predator
// that could take it (herbivore)?
static bool of_interest(const CretSim* s, const CretAgent* a, const CretAgent* b) {
    if (herb(a->species)) return !herb(b->species) && cret_mass(b) >= 0.15f * cret_mass(a);
    return herb(b->species) && cret_quest_prey_ok(s, a, cret_mass(a), b);
}
typedef struct { double first_s[CRET_SPECIES]; int firsts[CRET_SPECIES], never[CRET_SPECIES]; double enc[CRET_SPECIES], hits[CRET_SPECIES], deaths[CRET_SPECIES], minutes[CRET_SPECIES]; } PersonRes;
#define SIGHT_M 150.0f
#define ENCOUNTER_M 60.0f
static void run_person(PersonRes* R, int sessions, float minutes, bool quiet) {
    memset(R, 0, sizeof *R);
    CretSim* s = calloc(1, sizeof *s);
    for (int sp = 0; sp < CRET_SPECIES; sp++)
        for (int k = 0; k < sessions; k++) {
            CretConfig c = cret_default_config();
            c.seed = (uint32_t)(500 + 31 * sp + k); c.weather = 0; c.max_ticks = 1 << 30; c.player_species = sp; c.player_growth = 1.0f;
            cret_pop_config(&c);
            if (cret_sim_init(s, c) != 0) { fprintf(stderr, "ecoprobe: world failed\n"); exit(1); }
            CretPop P; cret_pop_init(&P, s, true);
            int ticks = (int)(minutes * 60 * CRET_TICK_HZ), first = -1, last_enc[CRET_MAX_AGENTS], hits0 = 0; uint32_t hc = s->hit_count;
            for (int j = 0; j < CRET_MAX_AGENTS; j++) last_enc[j] = -(1 << 30);
            CretInput in[CRET_MAX_AGENTS];
            for (int t = 0; t < ticks; t++) {
                for (int i = 0; i < c.num_agents; i++) in[i] = cret_bot_input(s, i);
                cret_sim_step(s, in); cret_pop_step(&P, s);
                for (; hc < s->hit_count; hc++) {   // blows on the person by predators
                    const CretHit* h = &s->hits[hc % CRET_HIT_LOG];
                    if (h->victim == 0 && h->attacker < c.num_agents && !herb(s->agents[h->attacker].species)) hits0++;
                }
                const CretAgent* a = &s->agents[0];
                if (a->ev_died) R->deaths[sp]++;
                if (!a->alive) continue;
                for (int j = 1; j < c.num_agents; j++) {
                    const CretAgent* b = &s->agents[j]; if (!b->alive || !of_interest(s, a, b)) continue;
                    float d = hypotf(b->x - a->x, b->z - a->z);
                    if (d < SIGHT_M && first < 0) first = t;
                    if (d < ENCOUNTER_M) { if (t - last_enc[j] > 120 * CRET_TICK_HZ) R->enc[sp]++; last_enc[j] = t; }
                }
            }
            if (first >= 0) { R->first_s[sp] += first / (double)CRET_TICK_HZ; R->firsts[sp]++; } else R->never[sp]++;
            R->hits[sp] += hits0; R->minutes[sp] += minutes;
            cret_sim_close(s);
        }
    free(s);
    (void)quiet;
}
static void print_person(const PersonRes* R, int sessions, float minutes) {
    char k[64];
    printf("\n## A person's experience: one adult person of each species (the bot AI drives it), director on, %d x %.0f min\n\n", sessions, minutes);
    printf("| species | looks for | first sighting s (within %.0f m) | never | encounters / 10 min (within %.0f m) | predator blows / 10 min | deaths / h |\n|---|---|---|---|---|---|---|\n", SIGHT_M, ENCOUNTER_M);
    for (int sp = 0; sp < CRET_SPECIES; sp++) {
        float fs = R->firsts[sp] ? (float)(R->first_s[sp] / R->firsts[sp]) : -1, m = (float)R->minutes[sp];
        float enc = m > 0 ? (float)(R->enc[sp] * 10 / m) : 0, hits = m > 0 ? (float)(R->hits[sp] * 10 / m) : 0;
        printf("| %s | %s | %.0f | %d | %.1f | %.1f | %.2f |\n", SHORT[sp], herb(sp) ? "predator" : "prey", fs, R->never[sp], enc, hits, m > 0 ? R->deaths[sp] * 60 / m : 0);
        snprintf(k, sizeof k, "person.%s.first_s", SHORT[sp]); res(k, R->never[sp] ? 1e4f : fs);
        snprintf(k, sizeof k, "person.%s.encounters", SHORT[sp]); res(k, enc);
        snprintf(k, sizeof k, "person.%s.blows", SHORT[sp]); res(k, hits);
    }
}

// ------------------------------------------------------------------ stalk trials
// A flat, open, grazeable spot near the middle of the island, away from water.
static bool open_spot(const CretSim* s, uint32_t* rng, float* x, float* z) {
    const CretWorld* w = s->world;
    for (int t = 0; t < 20000; t++) {
        *rng = *rng * 1664525u + 1013904223u; float u = (*rng >> 8) / 16777216.0f;
        *rng = *rng * 1664525u + 1013904223u; float v = (*rng >> 8) / 16777216.0f;
        float px = w->size * (0.2f + 0.6f * u), pz = w->size * (0.2f + 0.6f * v); bool ok = cret_forage(w, px, pz) > 0.3f;
        for (int k = 0; k < 16 && ok; k++) {
            float a = k * 0.3927f, r = 10 + 5 * (k % 4) * 4;
            ok = cret_walkable(w, px + cosf(a) * r, pz + sinf(a) * r) && cret_walkable(w, px + cosf(a) * 70, pz + sinf(a) * 70);
        }
        if (ok) { *x = px; *z = pz; return true; }
    }
    return false;
}
typedef struct { const char* name; int hunter; float hunter_g; int prey; float prey_g; int herd; } StalkCase;
static const StalkCase CASES[] = {
    {"raptor vs young galli", CRET_SP_RAPTOR, 1.0f, CRET_SP_GALLI, 0.6f, 0},
    {"raptor vs young pachy", CRET_SP_RAPTOR, 1.0f, CRET_SP_PACHY, 0.6f, 0},
    {"raptor vs young pachy, herd of 4", CRET_SP_RAPTOR, 1.0f, CRET_SP_PACHY, 0.6f, 3},
    {"trex vs pachy", CRET_SP_TREX, 1.0f, CRET_SP_PACHY, 1.0f, 0},
    {"trex vs young stego", CRET_SP_TREX, 1.0f, CRET_SP_STEGO, 0.6f, 0},
};
#define NCASES (int)(sizeof CASES / sizeof *CASES)
enum { STYLE_CAREFUL, STYLE_CHARGE, STYLES };
static const char* STYLE_NAME[STYLES] = {"careful", "charge"};
// One trial: the hunter (slot 0, a person played by this controller) starts
// 60 m downwind of the prey (slot 1, a bot grazing, with `herd` grown herd
// mates). Returns the seconds to the first bite that lands, or -1.
static float stalk_trial(CretSim* s, const StalkCase* C, int style, int trial, bool* bolted) {
    CretConfig c = cret_default_config();
    c.num_agents = 2 + C->herd; c.seed = (uint32_t)(900 + trial); c.weather = 0; c.player_species = C->hunter; c.player_growth = C->hunter_g;
    c.hunger_seconds = c.thirst_seconds = 1e6f;
    if (cret_sim_init(s, c) != 0) exit(1);
    uint32_t rng = 77 + trial * 13; float px, pz;
    if (!open_spot(s, &rng, &px, &pz)) { cret_sim_close(s); return -2; }
    float wx, wz, ws; cret_wind(s, &wx, &wz, &ws);
    float side = ((trial % 3) - 1) * 0.5f;   // straight downwind, or a little to either side
    float hx = px - (wx * cosf(side) - wz * sinf(side)) * 60, hz = pz - (wz * cosf(side) + wx * sinf(side)) * 60;
    for (int i = 0; i < c.num_agents; i++) {
        CretAgent* a = &s->agents[i];
        if (i >= 1) { cret_sim_set_species(s, i, i == 1 ? C->prey : C->prey); a->growth = i == 1 ? C->prey_g : 1.0f; }
        else a->growth = C->hunter_g;
        cret_apply_growth(a); a->health = a->max_health; a->food = i ? 0.5f : 0.3f; a->water = 1; a->group = i >= 1 && C->herd ? 2 : 0;
        float x = i == 0 ? hx : px + (i >= 2 ? cosf(i * 2.1f) * 9 : 0), z = i == 0 ? hz : pz + (i >= 2 ? sinf(i * 2.1f) * 9 : 0);
        a->x = x; a->z = z; a->y = cret_height(s->world, x, z); a->vx = a->vz = 0; a->on_ground = true;
        a->yaw = i == 0 ? atan2f(pz - hz, px - hx) : (float)(rng % 628) * 0.01f; rng = rng * 1664525u + 1013904223u;
        a->bot_state = BOT_GRAZE; a->bot_timer = 900; a->goal_x = x; a->goal_z = z;
        cret_sim_place(s, i);
    }
    CretAgent* h = &s->agents[0]; const CretAgent* q = &s->agents[1]; const CretSpecies* S = &CRET_SPECIES_TABLE[h->species];
    bool charging = style == STYLE_CHARGE; *bolted = false;
    CretInput in[8];
    for (int t = 0; t < 40 * CRET_TICK_HZ; t++) {
        for (int i = 1; i < c.num_agents; i++) in[i] = cret_bot_input(s, i);
        CretInput* m = &in[0]; memset(m, 0, sizeof *m);
        float dx = q->x - h->x, dz = q->z - h->z, d = sqrtf(dx * dx + dz * dz);
        float px2, pz2; cret_strike_point(h, &px2, &pz2); float gap = cret_body_dist(q, px2, pz2);
        bool fleeing = q->speed > q->walk_speed * 1.3f;
        *bolted |= fleeing;
        if (fleeing || gap < 12.0f) charging = true;
        m->turn = fmaxf(-1, fminf(1, wrap(atan2f(dz, dx) - h->yaw) * 2)); m->aim = cret_aim_at(h, q->x, q->z);
        m->aim = fmaxf(-1, fminf(1, m->aim));
        if (charging) { m->forward = 1; m->sprint = h->stamina > 0.05f; }
        else if (d > 40) m->forward = 0.5f;                                 // walk in
        else m->forward = q->act == CRET_ACT_EAT ? 0.3f : 0;               // creep while its head is down, freeze while it looks
        if (gap < S->reach * cret_growth_scale(h->growth) * 0.9f) m->action = CRET_ACT_ATTACK;
        cret_sim_step(s, in);
        if (!q->alive || (q->last_attacker == 0 && q->hurt_ticks > 0)) { cret_sim_close(s); return t / (float)CRET_TICK_HZ; }
    }
    cret_sim_close(s);
    return -1;
}
static void run_stalk(int trials) {
    char k[64];
    printf("\n## Stalking dinosaur prey: catch rate (a bite lands within 40 s), %d trials per case and style\n\n", trials);
    printf("| case | careful | bolted | mean s | charge | bolted | mean s |\n|---|---|---|---|---|---|---|\n");
    CretSim* s = calloc(1, sizeof *s);
    float tot[STYLES] = {0}; int n[STYLES] = {0};
    for (int ci = 0; ci < NCASES; ci++) {
        printf("| %s |", CASES[ci].name);
        for (int st = 0; st < STYLES; st++) {
            int caught = 0, bolted = 0, valid = 0; float secs = 0;
            for (int t = 0; t < trials; t++) {
                bool b; float r = stalk_trial(s, &CASES[ci], st, t, &b);
                if (r == -2) continue;
                valid++; bolted += b; if (r >= 0) { caught++; secs += r; }
            }
            float rate = valid ? 100.0f * caught / valid : 0;
            printf(" %.0f%% | %d | %.1f |", rate, bolted, caught ? secs / caught : 0);
            snprintf(k, sizeof k, "stalk.%d.%s", ci, STYLE_NAME[st]); res(k, rate);
            tot[st] += rate; n[st]++;
        }
        printf("\n"); fflush(stdout);
    }
    for (int st = 0; st < STYLES; st++) { snprintf(k, sizeof k, "stalk.mean.%s", STYLE_NAME[st]); res(k, n[st] ? tot[st] / n[st] : 0); }
    free(s);
}

int main(int argc, char** argv) {
    int seeds = 2, agents = CRET_POP_AGENTS, sessions = 3, trials = 12; float days = 10, minutes = 20; const char* only = NULL; const char* check = NULL; bool quick = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seeds") && i + 1 < argc) seeds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--days") && i + 1 < argc) days = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--agents") && i + 1 < argc) agents = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sessions") && i + 1 < argc) sessions = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--minutes") && i + 1 < argc) minutes = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--trials") && i + 1 < argc) trials = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
        else if (!strcmp(argv[i], "--check") && i + 1 < argc) check = argv[++i];
        else if (!strcmp(argv[i], "--quick")) quick = true;
        else { fprintf(stderr, "usage: ecoprobe [--only world|person|stalk] [--seeds N] [--days D] [--agents N] [--sessions N] [--minutes M] [--trials N] [--quick] [--check FILE]\n"); return 2; }
    }
    if (quick) { seeds = 1; days = 3; sessions = 1; minutes = 10; trials = 6; }   // what make test runs
    if (!only || !strcmp(only, "world")) { WorldRes* R = calloc(1, sizeof *R); run_world(R, seeds, days, agents, quick); print_world(R, seeds, days, agents); free(R); }
    if (!only || !strcmp(only, "person")) { PersonRes R; run_person(&R, sessions, minutes, quick); print_person(&R, sessions, minutes); }
    if (!only || !strcmp(only, "stalk")) run_stalk(trials);
    fflush(stdout);
    return check ? check_budget(check) : 0;
}
