// Replays a battle.mjs trace in our engine (Showdown-compatible build) and prints the same per-turn HP lines, so the
// two can be diffed: identical lines mean our engine reproduced the Showdown battle exactly.
// Build: clang -O2 -Iocean/pokemon ocean/pokemon/showdown/engine_replay.c build/pokemon/libpokemon.a -lm -o build/pokemon/engine_replay
// Usage: engine_replay < trace.txt   (ENGINE_OBS=1 also prints both observations before each decision)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bridge.h"

int main(void) {
    char line[4096];
    unsigned long long seed = 0;
    PKMon teams[12] = {0};
    PKBattle b;
    int started = 0, result = 0;
    while (fgets(line, sizeof line, stdin)) {
        if (!strncmp(line, "seed ", 5)) seed = strtoull(line + 5, NULL, 10);
        else if (!strncmp(line, "team", 4)) {
            int side = line[4] - '1';
            char* tok = strtok(line + 6, " \n");
            for (int i = 0; tok && i < 6; i++, tok = strtok(NULL, " \n")) {
                int v[5] = {0}, n = sscanf(tok, "%d,%d,%d,%d,%d", v, v + 1, v + 2, v + 3, v + 4);
                teams[side * 6 + i].species = v[0];
                for (int j = 1; j < n; j++) teams[side * 6 + i].moves[j - 1] = v[j];
            }
        } else if (!strncmp(line, "do ", 3)) {
            if (!started) { result = pk_start_free(&b, seed, teams); started = 1; if (result) { printf("start error %d\n", result); return 1; } }
            if (getenv("ENGINE_OBS")) {  // both players' observation bytes before this decision, as hex
                for (int p = 0; p < 2; p++) {
                    uint8_t o[PK_OBS]; pk_observe(&b, p, o);
                    printf("obs %d ", p + 1);
                    for (int i = 0; i < PK_OBS; i++) printf("%02x", o[i]);
                    printf("\n");
                }
            }
            int a[2]; sscanf(line + 3, "%d %d", a, a + 1);
            for (int p = 0; p < 2; p++) {  // Showdown's single forced move (recharge, Struggle, locked) is engine action 11
                uint8_t mask[PK_ACTIONS]; pk_mask(&b, p, mask);
                if (a[p] < 4 && !mask[a[p]] && mask[11]) a[p] = 11;
                if (a[p] == 11 && !mask[11]) {  // a locked move the engine still offers by slot
                    int only = -1, n = 0;
                    for (int m = 0; m < 4; m++) if (mask[m]) { only = m; n++; }
                    if (n == 1) a[p] = only;
                }
                if (a[p] == 10 && !mask[10]) { printf("pass not legal for p%d\n", p + 1); return 1; }
                if (!mask[a[p]]) { printf("illegal action %d for p%d (legal:", a[p], p + 1); for (int m = 0; m < PK_ACTIONS; m++) if (mask[m]) printf(" %d", m); printf("): %s", line); return 1; }
            }
            result = pk_update(&b, a[0], a[1]);
            uint16_t f[192]; pk_benchmark_fields(&b, f);
            printf("hp %d ", pk_turn(&b));
            for (int s = 0; s < 2; s++) {
                for (int i = 0; i < 6; i++) {
                    // order in fields is party slot order; map back via species to the original team index
                    for (int k = 0; k < 6; k++) if (f[(s * 6 + k) * 16] == teams[s * 6 + i].species) { printf("%s%d", i ? "," : "", f[(s * 6 + k) * 16 + 1]); break; }
                }
                printf(s ? "\n" : " ");
            }
            if (result) { printf("end %d\n", result); break; }
        }
    }
    return 0;
}
