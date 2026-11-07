// Debug tool for the Gen 3 bridge (2026-11-06): reads "TURN\tSEEN\tSTATE" lines and prints each observation
// (pk3_view, side one) as one line of comma-separated values, for comparing the tracker's view with the env's.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define OBS 5343
int pk3_view(const char* state, const char* seen, int turns, float* out);
static char line[1 << 20];
int main(void) {
    static float o[OBS];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\n")] = 0;
        char* a = strchr(line, '\t'); if (!a) continue; *a++ = 0;
        char* b = strchr(a, '\t'); if (!b) continue; *b++ = 0;
        if (pk3_view(b, a, atoi(line), o)) { puts("ERR"); continue; }
        for (int i = 0; i < OBS; i++) printf(i ? ",%g" : "%g", o[i]);
        putchar('\n');
    }
    return 0;
}
