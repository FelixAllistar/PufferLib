// Headless early-game probe: can a new hatchling feed itself? Mirrors solo
// play: the title screen runs the bots for --title seconds, then Play
// respawns slot 0 as a hatchling of --species (growth 0.35) with the
// population director on. A scripted player then plays for --minutes:
//   predator: chases and bites small prey within 30 m when it could eat
//             (stalks at a walk, rushes from 8 m), eats a carcass in reach,
//             else trots to its quest;
//   grazer:   grazes where plants are thick when it could eat, else trots
//             to its quest (and grazes there for a graze quest);
//   both:     drink when thirsty (the scripted bot finds water).
// It reports the time to the first meal, food minute by minute, the first
// quest's distance and whether the hatchling reached it before its stomach
// ran empty, and what it caught. --critters 0 turns small prey off (the
// game before critters) for comparison. No window, GPU or ENet.
//   make -C ocean/cretaceous critprobe && ./build/cretaceous/critprobe --species raptor
//   ./build/cretaceous/critprobe --species stego --sessions 4 --minutes 15
//   ./build/cretaceous/critprobe --critters 0
// Run from the repository root (the world cache is build/cretaceous/worlds).
#include "../sim.h"
#include "../population.h"
#include "../progress.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static float wrap(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }
static CretInput toward(const CretAgent* a, float x, float z, float fwd) {
    float d = wrap(atan2f(z - a->z, x - a->x) - a->yaw);
    CretInput in = {.turn = fmaxf(-1, fminf(1, d * 2)), .forward = fwd};
    if (fabsf(d) > 1.2f) in.forward *= 0.4f;
    return in;
}

// One tick of the scripted hatchling.
static CretInput play(CretSim* s, int me, int* mode) {
    CretAgent* a = &s->agents[me]; const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
    bool carn = S->diet == CRET_DIET_CARNIVORE; float g = cret_growth_scale(a->growth), d;
    if (a->water < 0.35f) { *mode = 3; return cret_bot_input(s, me); }   // the bot finds water and drinks
    const CretQuest* q = cret_quest_of(s, me, 0); float qx, qz, qr; bool has_q = q && cret_quest_target(s, q, &qx, &qz, &qr);
    bool at_q = has_q && hypotf(qx - a->x, qz - a->z) < qr;
    if (has_q && q->kind == CRET_Q_DRINK && at_q) { *mode = 3; return cret_bot_input(s, me); }   // drinking quest: the bot drinks there
    if (carn) {
        float fd; int fs = cret_food_state(s, me, &fd);
        if (fs == 1 && a->food < 0.95f) { *mode = 1; return (CretInput){.action = CRET_ACT_EAT}; }   // a carcass at the jaws
        int k = a->food < 0.9f || (has_q && q->kind == CRET_Q_CATCH) ? cret_critter_nearest(s, me, 30.0f, &d) : -1;
        if (k >= 0) {   // stalk, then rush and bite
            const CretCritter* c = &s->critters[k];
            CretInput in = toward(a, c->x, c->z, d > 8 ? 0.5f : 1.0f);
            in.sprint = d <= 8 && a->stamina > 0.1f; in.aim = cret_aim_at(a, c->x, c->z);
            float px, pz; cret_strike_point(a, &px, &pz);
            if (a->attack_cd == 0 && hypotf(c->x - px, c->z - pz) < S->reach * g + 0.3f) in.action = CRET_ACT_ATTACK;
            *mode = 1; return in;
        }
    } else {
        float L = S->length * g * 0.45f, f = cret_forage(s->world, a->x + cosf(a->yaw) * L, a->z + sinf(a->yaw) * L);
        bool graze_here = has_q && q->kind == CRET_Q_GATHER && at_q;
        if ((a->food < 0.9f && f > 0.25f) || (graze_here && f > 0.12f)) { *mode = 1; return (CretInput){.action = CRET_ACT_EAT}; }   // a graze quest counts grazing even when full
        if (graze_here || a->food < 0.6f) {   // hungry, or grazing for the quest, and nothing here: the thickest plants within 40 m
            float bx = 0, bz = 0, bf = graze_here ? 0.12f : 0.3f; uint32_t h = (uint32_t)s->tick / 30 * 2654435761u + 7;
            for (int t = 0; t < 12; t++) {
                float ang = t * 0.5236f + (h % 100) * 0.01f, r = 12 + 28 * ((t * 7 + h) % 10) / 10.0f, x = a->x + cosf(ang) * r, z = a->z + sinf(ang) * r;
                float v = cret_walkable(s->world, x, z) ? cret_forage(s->world, x, z) : 0;
                if (graze_here && hypotf(x - qx, z - qz) > qr) v = 0;   // stay in the quest's place
                if (v > bf) { bf = v; bx = x; bz = z; }
            }
            if (bf > (graze_here ? 0.12f : 0.3f)) { *mode = 1; return toward(a, bx, bz, 0.5f); }
        }
    }
    if (has_q && !at_q) { *mode = 2; return toward(a, qx, qz, 1.0f); }   // trot to the quest
    *mode = 0;
    return (CretInput){.forward = 0.5f, .turn = 0.1f};   // amble
}

int main(int argc, char** argv) {
    float minutes = 15, title_s = 60; int sessions = 4, critters = 1, species = CRET_SP_RAPTOR, world_n = 0; uint32_t seed0 = 2000;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--minutes") && i + 1 < argc) minutes = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--title") && i + 1 < argc) title_s = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--sessions") && i + 1 < argc) sessions = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--critters") && i + 1 < argc) critters = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed0 = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--world-n") && i + 1 < argc) world_n = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--species") && i + 1 < argc) {
            const char* sp = argv[++i]; for (int k = 0; k < CRET_SPECIES; k++) if (!strcmp(sp, CRET_SPECIES_TABLE[k].asset)) species = k;
        } else { fprintf(stderr, "usage: critprobe [--species raptor|stego|trex|pachy|sauropelta|galli] [--minutes M] [--sessions N] [--critters 0|1] [--title S] [--seed N] [--world-n N]\n"); return 2; }
    }
    CretSim* s = calloc(1, sizeof *s); CretSim* scratch = calloc(1, sizeof *s); int n_crit = 0; long ncrit_sum = 0;
    int ticks = (int)(minutes * 60 * CRET_TICK_HZ), nmin = (int)minutes;
    printf("critprobe: hatchling %s, %.0f min after %.0f s of title, critters %s\n", CRET_SPECIES_TABLE[species].name, minutes, title_s, critters ? "on" : "off");
    double sum_first = 0, sum_qd = 0, sum_qt = 0, ms_crit = 0, ms_all = 0; int starved_deaths = 0; long empty_all = 0; int n_first = 0, n_reached = 0, n_reached_fed = 0, n_starved = 0, deaths = 0, nq = 0;
    float food_min_sum[64] = {0}; long nt = 0; int eaten[CRET_CR_KINDS] = {0}, spawned[CRET_CR_KINDS] = {0}, quests = 0;
    for (int ss = 0; ss < sessions; ss++) {
        CretConfig c = cret_default_config(); c.seed = seed0 + (uint32_t)ss * 7919u; c.player_species = species;
        if (world_n) c.world_n = world_n;
        cret_pop_config(&c);
        if (cret_sim_init(s, c) != 0) { fprintf(stderr, "world failed\n"); return 1; }
        s->crit_cfg.enabled = critters;
        CretPop P; cret_pop_init(&P, s, true);
        CretInput in[CRET_MAX_AGENTS];
        for (int t = 0; t < (int)(title_s * CRET_TICK_HZ); t++) {   // the title screen's flyover: everyone is a bot
            for (int i = 0; i < s->cfg.num_agents; i++) in[i] = cret_bot_input(s, i);
            cret_sim_step(s, in); cret_pop_step(&P, s);
        }
        cret_sim_set_species(s, 0, species);   // Play: a fresh hatchling
        const CretAgent* a = &s->agents[0];
        float t_first = -1, t_reach = -1, q_dist = -1, food_at_reach = 0; bool starved = false; int empty_ticks = 0, q0 = s->prog[0].quests_done, mode = 0;
        float qx0 = 0, qz0 = 0, qr0 = 0; int qkind = 0;
        printf("  session %d: hatchling at %.0f %.0f, food %.2f\n", ss, a->x, a->z, a->food);
        char line[512]; int lp = snprintf(line, sizeof line, "    food by minute:");
        for (int t = 0; t < ticks; t++) {
            for (int i = 1; i < s->cfg.num_agents; i++) in[i] = cret_bot_input(s, i);
            in[0] = play(s, 0, &mode);
            struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
            cret_sim_step(s, in);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            ms_all += (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) * 1e-6; nt++;
            if (t % 300 == 150) {   // the critter layer alone, on a scratch copy (stepping the real sim twice would change it)
                memcpy(scratch, s, sizeof *s); clock_gettime(CLOCK_MONOTONIC, &t0);
                for (int r = 0; r < 20; r++) cret_critters_step(scratch);
                clock_gettime(CLOCK_MONOTONIC, &t1); ms_crit += ((t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) * 1e-6) / 20; n_crit++;
                ncrit_sum += s->ncrit;
            }
            cret_pop_step(&P, s);
            if (!a->alive) {   // as a player would: note why, respawn as a hatchling and go on
                if (a->dead_ticks == 0) {
                    int k = a->last_attacker; deaths++;
                    printf("    died at %.0f s: %s\n", t / 30.0f, k >= 0 && k < s->cfg.num_agents ? CRET_SPECIES_TABLE[s->agents[k].species].name :
                           a->food <= 0 ? "starved" : a->water <= 0 ? "thirst" : "fall or other");
                    if (k < 0 && a->food <= 0) starved_deaths++;
                }
                if (a->dead_ticks > 60) cret_sim_respawn(s, 0);
                continue;
            }
            if (a->ev_food > 0 && t_first < 0) t_first = t / 30.0f;
            if (a->food <= 0) { empty_ticks++; if (t_reach < 0) starved = true; }
            const CretQuest* q = cret_quest_of(s, 0, 0);
            if (q && q_dist < 0) {
                float x, z, r;
                if (cret_quest_target(s, q, &x, &z, &r)) { q_dist = hypotf(x - a->x, z - a->z); qx0 = x; qz0 = z; qr0 = r; qkind = q->kind; }
            }
            if (q_dist >= 0 && t_reach < 0 && (hypotf(qx0 - a->x, qz0 - a->z) < qr0 || s->prog[0].quests_done > q0)) { t_reach = t / 30.0f; food_at_reach = a->food; }
            if (t % (60 * CRET_TICK_HZ) == 60 * CRET_TICK_HZ - 1) {
                int m = t / (60 * CRET_TICK_HZ); if (m < 64) food_min_sum[m] += a->food;
                lp += snprintf(line + lp, sizeof line - (size_t)lp, " %.2f", a->food);
            }
        }
        printf("%s\n", line);
        static const char* QN[CRET_Q_COUNT] = {"-", "drink", "graze", "hunt", "patrol", "defend", "catch"};
        printf("    first meal %s; first quest %s %.0f m away, %s; quests done %d; growth %.2f\n",
               t_first >= 0 ? (snprintf(line, sizeof line, "at %.0f s", t_first), line) : "never",
               QN[qkind % CRET_Q_COUNT], q_dist, t_reach >= 0 ? (snprintf(line + 256, 200, "reached at %.0f s with food %.2f%s", t_reach, food_at_reach, starved ? " (starved first)" : ""), line + 256) : "not reached",
               s->prog[0].quests_done - q0, a->growth);
        if (t_first >= 0) { sum_first += t_first; n_first++; }
        if (q_dist >= 0) { sum_qd += q_dist; nq++; }
        if (t_reach >= 0) { n_reached++; sum_qt += t_reach; if (!starved) n_reached_fed++; }
        n_starved += starved; empty_all += empty_ticks;
        quests += s->prog[0].quests_done - q0;
        for (int k = 0; k < CRET_CR_KINDS; k++) { eaten[k] += s->stats.critters.eaten[k]; spawned[k] += s->stats.critters.spawned[k]; }
        cret_sim_close(s);
    }
    char first[48] = "never"; if (n_first) snprintf(first, sizeof first, "%.0f s (mean of %d)", sum_first / n_first, n_first);
    printf("summary (%d sessions): first meal after %s; first quest %.0f m away on average, reached or done in %d (%.0f s on average), %d before the stomach ran empty; %d ran empty before reaching it; %d deaths; %d quests done\n",
           sessions, first,
           nq ? sum_qd / nq : -1, n_reached, n_reached ? sum_qt / n_reached : -1, n_reached_fed, n_starved, deaths, quests);
    printf("  deaths from starvation: %d; time with an empty stomach: %.0f s in all\n", starved_deaths, empty_all / 30.0);
    printf("  mean food by minute:");
    for (int m = 0; m < nmin && m < 64; m++) printf(" %.2f", food_min_sum[m] / sessions);
    printf("\n  eaten (all animals): multituberculate %d, Repenomamus %d, lizard %d, bird %d, fish %d; spawned %d, %d, %d, %d, %d\n",
           eaten[0], eaten[1], eaten[2], eaten[3], eaten[4], spawned[0], spawned[1], spawned[2], spawned[3], spawned[4]);
    printf("  cost: sim step %.3f ms a tick (48 animals); critter layer alone %.4f ms a tick with %.0f critters on average\n",
           ms_all / nt, n_crit ? ms_crit / n_crit : 0, n_crit ? (double)ncrit_sum / n_crit : 0);
    free(s); free(scratch);
    return 0;
}
