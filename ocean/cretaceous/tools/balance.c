// Headless balance run: scripted bots live on the island for M minutes of
// game time per seed; prints who kills whom, ability use, quests, nests and
// the tick cost. No GPU or window.
//   make -C ocean/cretaceous balance && ./build/cretaceous/balance --seeds 4 --minutes 30
// Run from the repository root (the world cache is build/cretaceous/worlds).
#include "../sim.h"
#include "../abilities.h"
#include "../progress.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

static double now(void) { struct timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static const char* SHORT[CRET_SPECIES] = {"raptor", "stego", "trex", "pachy", "sauro", "galli"};

int main(int argc, char** argv) {
    int seeds = 4, agents = 16; float minutes = 30; uint32_t world_seed = 1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seeds") && i + 1 < argc) seeds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--minutes") && i + 1 < argc) minutes = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--agents") && i + 1 < argc) agents = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--world-seed") && i + 1 < argc) world_seed = (uint32_t)atoi(argv[++i]);
        else { fprintf(stderr, "usage: balance [--seeds N] [--minutes M] [--agents N] [--world-seed S]\n"); return 2; }
    }
    CretStats tot; memset(&tot, 0, sizeof tot);
    double t_bots = 0, t_step = 0, t_worst = 0; long ticks_all = 0; int ticks = (int)(minutes * 60 * CRET_TICK_HZ);
    int sp_slots[CRET_SPECIES] = {0}; int marks = 0, qdone = 0; float growth_end[CRET_SPECIES] = {0};
    CretSim* s = calloc(1, sizeof *s);
    for (int seed = 1; seed <= seeds; seed++) {
        CretConfig c = cret_default_config();
        c.world_seed = world_seed; c.num_agents = agents; c.seed = (uint32_t)seed; c.weather = 0; c.max_ticks = 1 << 30; c.player_growth = -1;
        if (cret_sim_init(s, c) != 0) { fprintf(stderr, "world failed\n"); return 1; }
        if (seed == 1) {
            printf("world %u: %d places\n", world_seed, s->world->npois);
            for (int k = 0; k < s->world->npois; k++) printf("  %-22s %-7s at %5.0f %5.0f  r %3.0f m\n", s->world->pois[k].name, cret_poi_kind_name(s->world->pois[k].kind), s->world->pois[k].x, s->world->pois[k].z, s->world->pois[k].r);
        }
        CretInput in[CRET_MAX_AGENTS];
        for (int t = 0; t < ticks; t++) {
            double a = now();
            for (int i = 0; i < agents; i++) in[i] = cret_bot_input(s, i);
            double b = now();
            cret_sim_step(s, in);
            double d = now();
            t_bots += b - a; t_step += d - b; if (d - a > t_worst) t_worst = d - a;
            if (getenv("CRET_BALANCE_TRACE") && t % (CRET_TICK_HZ * 120) == 0) {   // every 2 min: each agent's quest
                printf("t %4.0f s\n", t / (float)CRET_TICK_HZ);
                for (int i = 0; i < agents; i++) {
                    const CretAgent* g = &s->agents[i]; const CretQuest* q = cret_quest_of(s, i, 0); const CretQuest* gq = cret_quest_of(s, i, 1);
                    float tx, tz; float dq = cret_quest_target(s, gq, &tx, &tz, NULL) || cret_quest_target(s, q, &tx, &tz, NULL) ? sqrtf((tx - g->x) * (tx - g->x) + (tz - g->z) * (tz - g->z)) : -1;   // to the place it heads for
                    printf("  %2d %-6s grp %2d bot %d  quest %d %.1f/%.1f d %5.0f  group quest %d %.1f/%.1f\n", i, SHORT[g->species], g->group, g->bot_state,
                           q ? q->kind : 0, q ? q->progress : 0, q ? q->goal : 0, dq, gq ? gq->kind : 0, gq ? gq->progress : 0, gq ? gq->goal : 0);
                }
            }
        }
        ticks_all += ticks;
        const CretStats* st = &s->stats;
        for (int a = 0; a < CRET_SPECIES; a++) {
            for (int b = 0; b < CRET_SPECIES; b++) tot.kills[a][b] += st->kills[a][b];
            tot.deaths[a] += st->deaths[a]; tot.deaths_env[a] += st->deaths_env[a];
        }
        for (int k = 0; k < CRET_AB_COUNT; k++) { tot.ability_uses[k] += st->ability_uses[k]; tot.ability_hits[k] += st->ability_hits[k]; }
        for (int k = 0; k < CRET_Q_COUNT; k++) tot.quests[k] += st->quests[k];
        tot.latches += st->latches; tot.shake_offs += st->shake_offs; tot.stuns += st->stuns; tot.fears += st->fears; tot.marked += st->marked;
        tot.group_quests += st->group_quests; tot.marks_earned += st->marks_earned;
        tot.nests_built += st->nests_built; tot.eggs_laid += st->eggs_laid; tot.eggs_eaten += st->eggs_eaten; tot.nest_spawns += st->nest_spawns;
        tot.pickups += st->pickups; tot.carried_home += st->carried_home; tot.forced_drops += st->forced_drops;
        for (int i = 0; i < agents; i++) { sp_slots[s->agents[i].species]++; growth_end[s->agents[i].species] += s->agents[i].growth; marks += s->prog[i].marks; qdone += s->prog[i].quests_done; }
        int nd = 0, nq = 0, nu = 0;
        for (int a = 0; a < CRET_SPECIES; a++) nd += st->deaths[a];
        for (int k = 0; k < CRET_Q_COUNT; k++) nq += st->quests[k];
        for (int k = 0; k < CRET_AB_COUNT; k++) nu += st->ability_uses[k];
        printf("seed %d: %d deaths, %d quests, %d abilities used\n", seed, nd, nq, nu);
        fflush(stdout);
        cret_sim_close(s);
    }
    printf("\n%d seeds x %.0f min, %d agents\n", seeds, minutes, agents);
    printf("\nKills (row: killer, column: victim); env = hunger, thirst, falls, no attacker\n%-8s", "");
    for (int b = 0; b < CRET_SPECIES; b++) printf("%7s", SHORT[b]);
    printf("\n");
    for (int a = 0; a < CRET_SPECIES; a++) {
        printf("%-8s", SHORT[a]); for (int b = 0; b < CRET_SPECIES; b++) printf("%7d", tot.kills[a][b]); printf("\n");
    }
    printf("%-8s", "env"); for (int b = 0; b < CRET_SPECIES; b++) printf("%7d", tot.deaths_env[b]); printf("\n");
    printf("%-8s", "deaths"); for (int b = 0; b < CRET_SPECIES; b++) printf("%7d", tot.deaths[b]); printf("\n");
    printf("%-8s", "slots"); for (int b = 0; b < CRET_SPECIES; b++) printf("%7d", sp_slots[b]); printf("\n");
    printf("%-8s", "d/slot/h"); for (int b = 0; b < CRET_SPECIES; b++) printf("%7.2f", sp_slots[b] ? tot.deaths[b] / (float)sp_slots[b] / (minutes / 60) : 0); printf("\n");
    printf("%-8s", "growth"); for (int b = 0; b < CRET_SPECIES; b++) printf("%7.2f", sp_slots[b] ? growth_end[b] / sp_slots[b] : 0); printf("   (mean at the end)\n");
    printf("\nAbilities (uses, hits)\n");
    for (int k = 1; k < CRET_AB_COUNT; k++) printf("  %-18s %6d %6d\n", CRET_ABILITIES[k].name, tot.ability_uses[k], tot.ability_hits[k]);
    printf("  latches %d, shaken off %d, stuns %d, frightened %d, marked %d\n", tot.latches, tot.shake_offs, tot.stuns, tot.fears, tot.marked);
    static const char* QN[CRET_Q_COUNT] = {"-", "drink", "gather", "hunt", "patrol", "defend", "catch"};
    printf("\nQuests completed:");
    for (int k = 1; k < CRET_Q_COUNT; k++) printf(" %s %d,", QN[k], tot.quests[k]);
    printf(" of them group %d; marks earned %d (held at the end %d)\n", tot.group_quests, tot.marks_earned, marks);
    printf("Nests built %d, eggs laid %d, eaten %d, hatched as respawns %d\n", tot.nests_built, tot.eggs_laid, tot.eggs_eaten, tot.nest_spawns);
    printf("Carrying: %d picked up, %d put down at the carrier's nest, %d dropped by a blow, stun or death\n", tot.pickups, tot.carried_home, tot.forced_drops);
    printf("\nTick: sim step %.4f ms, bots %.4f ms, total %.4f ms (worst %.3f ms) over %ld ticks\n",
           t_step / ticks_all * 1e3, t_bots / ticks_all * 1e3, (t_step + t_bots) / ticks_all * 1e3, t_worst * 1e3, ticks_all);
    free(s);
    return 0;
}
