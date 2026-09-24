#include "../pokemon_core.h"
#include <stdio.h>

int main(void) {
    assert(PK_ACTIONS == 168 && PK_OBS == 648 && PK_ABI_VERSION == 3);
    for (int draft = 0; draft <= 1; draft++) {
        for (int seed = 1; seed <= 64; seed++) {
            PKGame a = {0}, b = {0};
            a.rng = b.rng = seed;
            a.draft = b.draft = draft;
            a.max_updates = b.max_updates = 256;
            pk_game_reset(&a);
            pk_game_reset(&b);
            uint64_t policy_rng = seed;
            int decisions = 0;
            while (!a.result) {
                assert(memcmp(a.obs, b.obs, sizeof(a.obs)) == 0);
                assert(memcmp(a.masks, b.masks, sizeof(a.masks)) == 0);
                int choices[2];
                for (int p = 0; p < 2; p++) {
                    choices[p] = pk_random_action(a.masks[p], &policy_rng);
                    assert(choices[p] >= 0 && choices[p] < PK_ACTIONS);
                    assert(a.masks[p][choices[p]]);
                }
                pk_game_step(&a, choices[0], choices[1]);
                pk_game_step(&b, choices[0], choices[1]);
                assert(a.result == b.result && a.result != 4);
                assert(++decisions <= 286);
            }
            assert(a.invalid_actions == 0 && b.invalid_actions == 0);
            assert(a.phase == PK_PHASE_BATTLE);
            for (int p = 0; p < 2; p++) {
                for (int i = 0; i < 6; i++) {
                    PKMon* mon = &a.teams[p][i];
                    assert(pk_moves_legal(mon->species, mon->moves, pk_move_count(mon)));
                    for (int j = 0; j < i; j++) {
                        assert(a.teams[p][i].species != a.teams[p][j].species);
                    }
                }
            }
        }
    }
    puts("Pokemon engine core: 128 seeded sampled/draft games, masks and determinism passed");
}
