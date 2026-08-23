#include <stdio.h>

#include "protein.cu"

int main(void) {
    ProteinSweep sw = {0};
    sw.num_random_samples = 3;
    sw.global_random_fraction = 0.25f;

    const int expected[] = {1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1};
    for (int i = 0; i < (int)(sizeof(expected) / sizeof(expected[0])); i++) {
        sw.suggestion_idx = i + 1;
        int actual = protein_sweep_use_global_random(&sw);
        if (actual != expected[i]) {
            fprintf(stderr, "suggestion %d: expected random=%d, got %d\n",
                i + 1, expected[i], actual);
            return 1;
        }
    }

    puts("Protein exploration schedule passed");
    return 0;
}
