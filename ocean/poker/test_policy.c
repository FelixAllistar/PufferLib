#include "../../src/puffercpu.c"
#include "poker.h"

int main(void) {
    PokerLeague bank = {.count = 2, .hidden = 32, .layers = 2};
    bank.stride = bank.hidden * (POKER_OBS + 9) + 6 * bank.hidden * bank.hidden;
    bank.weights = calloc(bank.count * bank.stride, sizeof(float));
    uint64_t rng = 983;
    for (int i = 0; i < bank.count * bank.stride; i++) {
        bank.weights[i] = ((int)(poker_random(&rng) % 2001) - 1000) * 0.00005f;
    }
    PufferNet* references[4];
    float states[4][64] = {0};
    int sizes[] = {8};
    for (int i = 0; i < 4; i++) {
        Weights weights = {.data = bank.weights + (i / 2) * bank.stride,
            .size = bank.stride};
        references[i] = make_puffernet(&weights, 1, POKER_OBS, 32, 2, sizes, 1);
        assert(weights.idx == bank.stride);
    }
    for (int step = 0; step < 500; step++) {
        int slot = poker_random(&rng) % 4;
        PufferNet* net = references[slot];
        float obs[POKER_OBS], logits[9];
        for (int i = 0; i < POKER_OBS; i++) {
            obs[i] = (poker_random(&rng) % 20000) / 100.0f;
        }
        if (step % 17 == 0) {
            memset(states[slot], 0, sizeof(states[slot]));
            memset(net->mingru->state, 0, sizeof(states[slot]));
        }
        poker_forward(&bank, slot / 2, states[slot], obs, logits);
        linear(net->encoder, obs);
        mingru(net->mingru, net->encoder->output);
        linear(net->decoder, net->mingru->output);
        for (int j = 0; j < 4; j++) {
            for (int i = 0; i < 64; i++) {
                assert(fabsf(states[j][i] - references[j]->mingru->state[i]) < 0.0001f);
            }
        }
        for (int i = 0; i < 9; i++) {
            assert(fabsf(logits[i] - net->decoder->output[i]) < 0.0001f);
        }
    }
    FILE* manifest = fopen("build/poker/test_league.txt", "w");
    assert(manifest);
    fprintf(manifest, "poker_v1 %d 32 2 2\n", POKER_PLAYERS);
    for (int i = 0; i < 2; i++) {
        char path[128];
        snprintf(path, sizeof(path), "build/poker/test_policy%d.bin", i);
        fprintf(manifest, "%s\n", path);
        FILE* file = fopen(path, "wb");
        assert(file);
        size_t count = fwrite(bank.weights + i * bank.stride, sizeof(float), bank.stride, file);
        assert(count == (size_t)bank.stride);
        fclose(file);
    }
    fclose(manifest);
    for (int i = 0; i < 4; i++) {
        free_puffernet(references[i]);
    }
    free(bank.weights);
    puts("PASS native frozen policy vs PufferNet: logits, separate seat state, terminal reset");
}
