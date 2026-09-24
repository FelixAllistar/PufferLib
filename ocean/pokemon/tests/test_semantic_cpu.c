#define _POSIX_C_SOURCE 200809L
#include <unistd.h>
#ifdef PK_LEGACY_CPU
#include PK_LEGACY_CPU
#else
#include "puffercpu.c"
#include "../semantic_cpu.h"
#endif
#include "../pokemon_core.h"

int main(void) {
    uint64_t digest = UINT64_C(14695981039346656037);
    for (int layers = 1; layers <= 2; layers++) {
        int hidden = layers * 16;
        size_t count = pk_parameter_count(hidden, layers);
        Weights weights = {0};
        weights.size = count + 7;
        weights.data = calloc(weights.size, sizeof(float));
        for (size_t i = 0; i < count; i++) {
            weights.data[i] = ((int)(i * 17 % 101) - 50) * 0.002f;
        }
        PKCpuPolicy* batch = pk_cpu_make(&weights, 2, hidden, layers);
        assert((size_t)weights.idx == count);
        char path[] = "/tmp/pokemon-semantic-test.XXXXXX";
        int fd = mkstemp(path);
        assert(fd >= 0);
        FILE* file = fdopen(fd, "wb");
        assert(file && fwrite(weights.data, sizeof(float), count, file) == count);
        fclose(file);
        Weights* loaded = load_weights(path);
        unlink(path);
        assert(loaded && loaded->size == weights.size);
        PKCpuPolicy* single[2];
        for (int p = 0; p < 2; p++) {
            loaded->idx = 0;
            single[p] = pk_cpu_make(loaded, 1, hidden, layers);
        }
        PKGame game = {0};
        game.rng = 73;
        game.draft = 1;
        game.max_updates = 24;
        pk_game_reset(&game);
        uint64_t rng = 101;
        int phases[3] = {0};
        for (int step = 0; step < 128; step++) {
            float observations[2 * PK_OBS];
            phases[game.phase]++;
            for (int p = 0; p < 2; p++) {
                for (int i = 0; i < PK_OBS; i++) {
                    observations[p * PK_OBS + i] = game.obs[p][i];
                }
            }
            float* output = pk_cpu_forward(batch, observations);
            for (int p = 0; p < 2; p++) {
                float* reference = pk_cpu_forward(single[p], observations + p * PK_OBS);
                for (int j = 0; j <= PK_ACTIONS; j++) {
                    float value = output[p * (PK_ACTIONS + 1) + j];
                    assert(isfinite(value));
                    assert(fabsf(value - reference[j]) < 1e-6f);
                    uint32_t bits;
                    memcpy(&bits, &value, sizeof(bits));
                    digest = (digest ^ bits) * UINT64_C(1099511628211);
                }
            }
            int a = pk_random_action(game.masks[0], &rng);
            int b = pk_random_action(game.masks[1], &rng);
            pk_game_step(&game, a, b);
            if (game.result) {
                assert(game.result != 4);
                pk_game_reset(&game);
                memset(batch->mingru->state, 0, 2 * hidden * layers * sizeof(float));
                for (int p = 0; p < 2; p++) {
                    memset(single[p]->mingru->state, 0, hidden * layers * sizeof(float));
                }
            }
        }
        for (int phase = 0; phase < 3; phase++) assert(phases[phase] > 0);
        pk_cpu_free(batch);
        pk_cpu_free(single[0]);
        pk_cpu_free(single[1]);
        free(loaded);
        free(weights.data);
    }
    printf("Pokemon semantic CPU batch/single and recurrent trace: %016llx\n",
        (unsigned long long)digest);
}
