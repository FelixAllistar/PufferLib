// Headless fight probe: staged fights between game-AI animals on open
// ground, many trials per pairing, and who wins. Each trial is a scenario
// (scenario.h) built in memory: the attackers start 25-35 m from the
// defenders at a random bearing, everyone a bot (predators kept hungry, as
// scenarios do). A fight ends when one side is wiped out or after --seconds.
// Prints per pairing the share of fights each side won (the rest: nobody
// died, usually because one side ran), the standard error of the attackers'
// share, the mean length, and the knockdowns each side gave per fight
// (sim.h, CRET_KD_*). --hostile makes the attackers press home on the first
// defender and never break off (scenario behaviour hostile), so most fights
// end in a death; the default leaves them to the game AI, which skips prey it
// cannot take and lets prey that runs go. No GPU or window.
//   make -C ocean/cretaceous fightprobe && ./build/cretaceous/fightprobe --trials 200
//   ./build/cretaceous/fightprobe --trials 200 --first 1001   (another set of trials: run-to-run spread)
//   ./build/cretaceous/fightprobe --trials 200 --hostile
// Run from the repository root (the world cache is build/cretaceous/worlds).
#include "../sim.h"
#include "../scenario.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char* name; const char* att; int natt; const char* def; int ndef; float def_growth; } Pairing;
static const Pairing PAIRS[] = {
    {"raptor v galli", "raptor", 1, "galli", 1, 1}, {"raptor v pachy", "raptor", 1, "pachy", 1, 1},
    {"raptor v stego", "raptor", 1, "stego", 1, 1}, {"raptor v sauro", "raptor", 1, "sauropelta", 1, 1},
    {"3 raptors v pachy", "raptor", 3, "pachy", 1, 1}, {"3 raptors v stego", "raptor", 3, "stego", 1, 1},
    {"3 raptors v sauro", "raptor", 3, "sauropelta", 1, 1}, {"3 raptors v galli", "raptor", 3, "galli", 1, 1},
    {"trex v stego", "trex", 1, "stego", 1, 1}, {"trex v young stego", "trex", 1, "stego", 1, 0.8f},
    {"trex v pachy", "trex", 1, "pachy", 1, 1}, {"trex v sauro", "trex", 1, "sauropelta", 1, 1},
    {"trex v galli", "trex", 1, "galli", 1, 1}, {"trex v 2 stegos", "trex", 1, "stego", 2, 1},
};
#define NPAIR ((int)(sizeof PAIRS / sizeof PAIRS[0]))

static uint32_t lcg(uint32_t* s) { *s = *s * 1664525u + 1013904223u; return *s >> 8; }
static float urand(uint32_t* s) { return (float)lcg(s) / 16777216.0f; }

int main(int argc, char** argv) {
    int trials = 100, first = 1; float seconds = 90; const char* only = NULL; bool hostile = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--trials") && i + 1 < argc) trials = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--first") && i + 1 < argc) first = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
        else if (!strcmp(argv[i], "--hostile")) hostile = true;
        else { fprintf(stderr, "usage: fightprobe [--trials N] [--first SEED] [--seconds S] [--only NAME] [--hostile]\n"); return 2; }
    }
    CretSim* s = calloc(1, sizeof *s); CretScnRun* r = calloc(1, sizeof *r);
    CretWorld* hold = NULL;   // keeps the island loaded between trials
    printf("fightprobe: %d trials per pairing (seeds %d..%d), at most %.0f s each, attackers %s\n", trials, first, first + trials - 1, seconds,
           hostile ? "hostile (press home on the first defender)" : "game AI");
    printf("%-20s %6s %6s %6s %6s %7s %9s %9s\n", "pairing", "att%", "def%", "none%", "se", "len s", "kd att", "kd def");
    for (int p = 0; p < NPAIR; p++) {
        const Pairing* P = &PAIRS[p];
        if (only && !strstr(P->name, only)) continue;
        int won_a = 0, won_d = 0; double len = 0, kd_a = 0, kd_d = 0;
        for (int t = 0; t < trials; t++) {
            uint32_t seed = (uint32_t)(first + t), rng = seed * 2654435761u + (uint32_t)p * 97u;
            float ang = urand(&rng) * 6.2832f, dist = 25 + 10 * urand(&rng), face = urand(&rng) * 360;
            char text[1024];
            snprintf(text, sizeof text,
                     "[scenario]\ntitle = probe\nseconds = %.0f\nhour = 11\nweather = clear\nplace = open\ncamera = side\nend = time\n"
                     "[player]\nspecies = %s\nat = %.2f %.2f\nfacing = %.0f\ngrowth = %.2f\ncontrol = bot\n"
                     "[actor def]\nspecies = %s\ncount = %d\nat = %.2f %.2f\nspread = 6\nfacing = %.0f\ngrowth = %.2f\nbehaviour = bot\n"
                     "[actor att]\nspecies = %s\ncount = %d\nat = %.2f %.2f\nspread = 5\nfacing = player\nbehaviour = %s\n",
                     seconds, P->def, 0.0f, 0.0f, face, P->def_growth, P->def, P->ndef - 1 > 0 ? P->ndef - 1 : 1, 8.0f * cosf(face), 8.0f * sinf(face), face,
                     P->def_growth, P->att, P->natt, dist * cosf(ang), dist * sinf(ang), hostile ? "hostile\ntarget = player" : "bot");
            if (P->ndef < 2) {   // a lone defender: no second defender actor
                char* d = strstr(text, "[actor def]"), *e = strstr(text, "[actor att]");
                memmove(d, e, strlen(e) + 1);
            }
            CretScenario sc; char err[256];
            if (!cret_scenario_parse(text, P->name, &sc, err, sizeof err)) { fprintf(stderr, "%s: %s\n", P->name, err); return 1; }
            CretConfig c = cret_scenario_config(&sc, cret_default_config()); c.seed = seed;
            if (cret_sim_init(s, c) != 0) { fprintf(stderr, "world failed\n"); return 1; }
            if (!hold) hold = cret_world_acquire(c.world_seed, c.world_n, c.world_cell, 0);
            if (!cret_scenario_start(r, &sc, s)) { fprintf(stderr, "%s: start failed\n", P->name); return 1; }
            // Sides by scenario group: 0 (player) and the def actor defend, the att actor attacks.
            int ga = sc.nactor - 1, outcome = 0;
            CretInput in[CRET_MAX_AGENTS];
            while (!r->done) {
                cret_scenario_inputs(r, s, in); cret_sim_step(s, in); cret_scenario_after_step(r, s);
                int alive_a = 0, alive_d = 0;
                for (int i = 0; i < s->cfg.num_agents; i++) {
                    if (r->group_of[i] < 0 || !s->agents[i].alive) continue;
                    if (r->group_of[i] == ga + 1) alive_a++; else alive_d++;
                }
                if (!alive_d) { outcome = 1; break; }
                if (!alive_a) { outcome = 2; break; }
            }
            won_a += outcome == 1; won_d += outcome == 2; len += r->tick * CRET_DT;
            for (int g = 0; g <= sc.nactor; g++) { if (g == ga + 1) kd_a += r->st[g].knockdowns; else kd_d += r->st[g].knockdowns; }
            cret_sim_close(s);
        }
        float pa = (float)won_a / trials, se = sqrtf(pa * (1 - pa) / trials);
        printf("%-20s %6.1f %6.1f %6.1f %6.1f %7.1f %9.2f %9.2f\n", P->name, 100 * pa, 100.0f * won_d / trials, 100.0f * (trials - won_a - won_d) / trials,
               100 * se, len / trials, kd_a / trials, kd_d / trials);
        fflush(stdout);
    }
    if (hold) cret_world_release(hold);
    free(r); free(s);
    return 0;
}
