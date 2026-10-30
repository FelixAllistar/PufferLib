// Gen 1 trainer tournament (2026-10-30): every pair of trainers in trainers.tsv plays N battles under the cartridge AI
// on both sides (arena.zig). Writes one line per pair: i j a_wins b_wins draws.
// Usage: arena TRAINERS.tsv N OUT.tsv [SEED] [PERFECT]   (PERFECT=1: DVs 15 and max stat experience)
#include <omp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { uint8_t dex, level, moves[4]; } Mon;
typedef struct { uint8_t cls, n; Mon mons[6]; } Team;
uint32_t arena_play(const Team* a, const Team* b, uint32_t n, uint64_t seed, int perfect, uint32_t* out);

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: arena TRAINERS.tsv N OUT.tsv [SEED]\n"); return 1; }
    FILE* f = fopen(argv[1], "r");
    static Team teams[512];
    int count = 0, cls, n;
    while (count < 512 && fscanf(f, "%d %d", &cls, &n) == 2) {
        teams[count].cls = cls; teams[count].n = n;
        for (int k = 0; k < n; k++) {
            int d, l, m[4];
            if (fscanf(f, "%d %d %d %d %d %d", &d, &l, &m[0], &m[1], &m[2], &m[3]) != 6) return 1;
            teams[count].mons[k] = (Mon){d, l, {m[0], m[1], m[2], m[3]}};
        }
        count++;
    }
    fclose(f);
    uint32_t battles = atoi(argv[2]);
    uint64_t seed = argc > 4 ? strtoull(argv[4], 0, 10) : 1;
    int perfect = argc > 5 ? atoi(argv[5]) : 0;
    long pairs = (long)count * (count - 1) / 2;
    uint32_t (*res)[3] = calloc(pairs, sizeof *res);
    long done = 0, errors = 0;
    double start = omp_get_wtime();
    #pragma omp parallel for schedule(dynamic, 16) reduction(+:errors)
    for (long p = 0; p < pairs; p++) {
        long i = 0, rem = p;
        while (rem >= count - 1 - i) { rem -= count - 1 - i; i++; }
        long j = i + 1 + rem;
        errors += arena_play(&teams[i], &teams[j], battles, seed * 1000003u + p, perfect, res[p]);
        long d = __atomic_add_fetch(&done, 1, __ATOMIC_RELAXED);
        if (d % 2000 == 0) fprintf(stderr, "%ld / %ld pairs, %.0f s\n", d, pairs, omp_get_wtime() - start);
    }
    FILE* o = fopen(argv[3], "w");
    long p = 0;
    for (int i = 0; i < count; i++)
        for (int j = i + 1; j < count; j++, p++) fprintf(o, "%d %d %u %u %u\n", i, j, res[p][0], res[p][1], res[p][2]);
    fclose(o);
    fprintf(stderr, "%d trainers, %ld pairs x %u battles in %.0f s, %ld engine errors\n", count, pairs, battles,
        omp_get_wtime() - start, errors);
    return 0;
}
