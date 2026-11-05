// Checkpoint upgrade (2026-11-05): rewrites a poker policy checkpoint of any older layout (POKER_OBS inputs, plain or
// zero-sum critic V1) as a current zero-sum critic learner checkpoint (POKER_PUB inputs, POKER_SEAT_PRIV critic), so
// the trainer's eval can load it and score old models with the same score eval as a sweep trial. The position inputs
// get zero encoder weights (the old policy ignores them, as in the league) and the critic is zero: eval never reads it.
// Build: clang -O2 -std=c11 -Iocean/poker ocean/poker/upgrade_policy.c -lm -o build/poker/upgrade_policy
// Usage: upgrade_policy IN OUT [HIDDEN LAYERS]  (default 1024 2)
#include <stdlib.h>
#include "policy.h"

int main(int argc, char** argv) {
    if (argc != 3 && argc != 5) { fprintf(stderr, "usage: upgrade_policy IN OUT [HIDDEN LAYERS]\n"); return 2; }
    int h = argc == 5 ? atoi(argv[3]) : 1024, layers = argc == 5 ? atoi(argv[4]) : 2;
    float* w = poker_policy_load(argv[1], h, layers);
    if (!w) return 1;
    size_t head = (size_t)h * (POKER_PUB + POKER_ACTIONS + 1), recurrent = (size_t)layers * 3 * h * h;
    size_t critic = (size_t)poker_critic_params(h, POKER_SEAT_PRIV);
    float* zeros = (float*)calloc(critic, sizeof(float));
    FILE* out = fopen(argv[2], "wb");
    if (!out) { perror(argv[2]); return 1; }
    size_t ok = fwrite(w, sizeof(float), head, out) + fwrite(zeros, sizeof(float), critic, out)
        + fwrite(w + head, sizeof(float), recurrent, out);
    if (fclose(out) || ok != head + critic + recurrent) { perror(argv[2]); return 1; }
    float* back = poker_policy_load(argv[2], h, layers);
    if (!back || memcmp(back, w, (head + recurrent) * sizeof(float))) { fprintf(stderr, "round trip failed\n"); return 1; }
    printf("%s -> %s (%zu floats)\n", argv[1], argv[2], head + critic + recurrent);
    return 0;
}
