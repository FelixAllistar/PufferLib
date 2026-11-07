// Check for the Gen 3 Showdown bridge (2026-11-06): plays env battles from build/pokemon3/positions.txt with side one
// chosen by the policy server's code path (STATE + SEEN -> pk3_view -> CPU network, argmax) and side two random.
// Asserts that pk3_view's observation equals the env's own (pk3_observe) on every turn, and prints the win rate, to
// compare with ./puffer eval CHECKPOINT --env.bot_policy=1 on the GPU.
// Usage: check_view CHECKPOINT.bin HIDDEN LAYERS GAMES
#include "../../../src/puffercpu.c"
#include <stdint.h>
#define OBS 5343
#define ACTIONS 128
typedef struct Pk3Game Pk3Game;
Pk3Game* pk3_new(const char* state, uint64_t seed);
void pk3_free(Pk3Game* game);
int pk3_mask(const Pk3Game* game, int side, unsigned char* mask);
int pk3_step(Pk3Game* game, int a1, int a2);
void pk3_observe(const Pk3Game* game, int side, float* out);
int pk3_view(const char* state, const char* seen, int turns, float* out);
int pk3_debug_view(const Pk3Game* game, char* state, char* seen, int cap);

int main(int argc, char** argv) {
    int hidden = atoi(argv[2]), layers = atoi(argv[3]), games = atoi(argv[4]);
    Weights* w = load_weights(argv[1]);
    FILE* f = fopen("build/pokemon3/positions.txt", "r");
    static char lines[2000][8192]; int n = 0;
    while (n < 2000 && fgets(lines[n], sizeof lines[n], f)) { lines[n][strcspn(lines[n], "\n")] = 0; n++; }
    int act_sizes[] = {ACTIONS};
    Multidiscrete* head = make_multidiscrete(1, act_sizes, 1);
    static float a[OBS], b[OBS]; static char st[1 << 16], seen[256];
    int wins = 0, draws = 0, turns_total = 0; uint64_t rng = 7;
    for (int g = 0; g < games; g++) {
        Pk3Game* game = pk3_new(lines[g % n], 1000 + g);
        w->idx = 0;
        Linear* enc = make_linear(w, 1, OBS, hidden); Linear* dec = make_linear(w, 1, hidden, ACTIONS + 1);
        MinGRU* gru = make_mingru(w, 1, hidden, layers);
        int result = 0;
        while (!result) {
            int turns = pk3_debug_view(game, st, seen, sizeof st);
            assert(turns >= 0);
            pk3_observe(game, 0, a);
            assert(pk3_view(st, seen, turns, b) == 0);
            for (int i = 0; i < OBS; i++) if (a[i] != b[i]) { fprintf(stderr, "obs differs at %d: %g vs %g\n", i, a[i], b[i]); return 1; }
            for (int i = 0; i < OBS; i++) { uint32_t u; memcpy(&u, &b[i], 4); u &= 0xffff0000u; memcpy(&b[i], &u, 4); }
            unsigned char m1[ACTIONS], m2[ACTIONS];
            pk3_mask(game, 0, m1); int legal = pk3_mask(game, 1, m2);
            linear(enc, b); mingru(gru, enc->output); linear(dec, gru->output);
            float act = 0; multidiscrete(head, dec->output, &act, getenv("SAMPLE") ? 0 : 1, m1);
            rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
            int pick = rng % legal, a2 = 0;
            for (int k = 0; k < ACTIONS; k++) if (m2[k] && pick-- == 0) { a2 = k; break; }
            result = pk3_step(game, (int)act, a2);
            turns_total++;
        }
        wins += result == 1; draws += result == 3 || result == 4;
        free(enc); free(dec); free_mingru(gru); pk3_free(game);
    }
    printf("CHECK_VIEW games=%d wins=%d draws=%d win_rate=%.3f turns=%d obs_equal=all\n", games, wins, draws,
        (wins + 0.5 * draws) / games, turns_total);
    return 0;
}
