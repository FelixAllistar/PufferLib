// Check for pk3_new_teams (2026-11-06): reads check_teams.js output and compares the engine's built battle with
// Showdown's turn-1 state (both serialized by the engine). Weather turns are not compared: Showdown keeps ability
// weather as 0 turns, the engine as -1, and both mean "never ends" in Gen 3.
// Build: cc -O2 check_teams.c ../../../build/pokemon3/libpokemon3_engine.a -lm -ldl -lpthread -o check_teams
// Usage: check_teams SETS < pairs.txt
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Pk3Game Pk3Game;
Pk3Game* pk3_new(const char* state, uint64_t seed);
Pk3Game* pk3_new_teams(const int* t1, const int* t2, uint64_t seed);
void pk3_free(Pk3Game* game);
long pk3_sets_load(const char* path);
int pk3_serialize(const Pk3Game* game, char* out, int cap);
static void unweather(char* s) {  // the field: ".../WEATHER;TURNS/TERRAIN;.." -> drop TURNS
    char* field = strstr(s, "/NONE;0/false;0/false");
    if (!field) return;
    char* w = field - 1;
    while (w > s && *w != '/') w--;
    char* semi = strchr(w, ';');
    if (semi && semi < field) memmove(semi, field, strlen(field) + 1);
}
int main(int argc, char** argv) {
    if (argc < 2 || pk3_sets_load(argv[1]) <= 0) { fprintf(stderr, "usage: check_teams SETS < pairs\n"); return 1; }
    static char line[1 << 16], a[1 << 16], b[1 << 16];
    int n = 0, same = 0;
    while (fgets(line, sizeof(line), stdin)) {
        int t[12];
        char* tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = 0;
        tab[strcspn(tab + 1, "\n") + 1] = 0;
        char* p = line;
        for (int i = 0; i < 12; i++) t[i] = (int)strtol(p, &p, 10);
        Pk3Game* want = pk3_new(tab + 1, 1);
        Pk3Game* got = pk3_new_teams(t, t + 6, 1);
        pk3_serialize(want, a, sizeof(a));
        pk3_serialize(got, b, sizeof(b));
        unweather(a);
        unweather(b);
        n++;
        if (!strcmp(a, b)) same++;
        else if (n - same <= 3) printf("DIFF\n want %s\n got  %s\n", a, b);
        pk3_free(want);
        pk3_free(got);
    }
    printf("%d / %d battles identical\n", same, n);
    return 0;
}
