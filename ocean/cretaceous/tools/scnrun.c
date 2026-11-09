// Headless scenario runner: plays config/scenarios/NAME.ini (or every
// scenario) with no window or GPU and prints each summary. The same code runs
// under `play scenario` and `capture --scenario`.
//   make -C ocean/cretaceous scnrun && ./build/cretaceous/scnrun            (all)
//   ./build/cretaceous/scnrun raptor_vs_stego hitrun_stego --seed 3
// Run from the repository root (the world cache is build/cretaceous/worlds).
#include "../sim.h"
#include "../scenario.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    char names[64][48]; int n = 0; uint32_t seed = 1; float trace = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc) trace = (float)atof(argv[++i]);
        else if (argv[i][0] == '-') { fprintf(stderr, "usage: scnrun [NAME...] [--seed N] [--trace SECONDS]\n"); return 2; }
        else if (n < 64) snprintf(names[n++], 48, "%s", argv[i]);
    }
    if (!n) n = cret_scenario_list(names, 64);
    if (!n) { fprintf(stderr, "no scenarios in %s (run from the repository root)\n", CRET_SCN_DIR); return 1; }
    CretSim* s = calloc(1, sizeof *s); CretScnRun* r = calloc(1, sizeof *r); int bad = 0;
    for (int k = 0; k < n; k++) {
        CretScenario sc; char err[256];
        if (!cret_scenario_load(names[k], &sc, err, sizeof err)) { fprintf(stderr, "%s: %s\n", names[k], err); bad++; continue; }
        CretConfig c = cret_scenario_config(&sc, cret_default_config()); c.seed = seed;
        if (cret_sim_init(s, c) != 0) { fprintf(stderr, "world failed\n"); return 1; }
        if (!cret_scenario_start(r, &sc, s)) { fprintf(stderr, "%s: start failed\n", names[k]); bad++; cret_sim_close(s); continue; }
        CretInput in[CRET_MAX_AGENTS];
        int every = trace > 0 ? (int)(trace * CRET_TICK_HZ + 0.5f) : 0;
        while (!r->done) {
            cret_scenario_inputs(r, s, in); cret_sim_step(s, in); cret_scenario_after_step(r, s);
            if (every && r->tick % every == 0) {   // where everyone is: position from the origin, bot state, health, speed, input
                printf("  t=%5.1f", r->tick / (float)CRET_TICK_HZ);
                for (int i = 0; i < s->cfg.num_agents; i++) {
                    const CretAgent* a = &s->agents[i]; if (r->group_of[i] < 0) continue;
                    printf(" | %d %s %+.0f,%+.0f st%d hp%.0f v%.1f f%.1f a%d", i, CRET_SPECIES_TABLE[a->species].asset, a->x - r->ox, a->z - r->oz, a->bot_state,
                           a->alive ? a->health : -1.0f, a->speed, in[i].forward, in[i].action);
                }
                printf("\n");
            }
        }
        char out[2048]; cret_scenario_summary(r, out, sizeof out); printf("%s  (\"%s\" at %.0f %.0f)\n%s\n", sc.name, sc.title, r->ox, r->oz, out);
        cret_sim_close(s);
    }
    free(r); free(s);
    return bad ? 1 : 0;
}
